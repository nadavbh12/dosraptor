#!/usr/bin/env bash
# Verify a playthrough script still produces the golden frame hashes.
#
# Usage: tests/golden_check.sh <script-name>  [<script-name> ...]
# Exits 0 if every script matches its golden byte-for-byte.
# Exits 1 if any script diverges; prints the first divergent frame and
# the nearest preceding checkpoint label.
#
# Designed to run pre-commit or in CI: catches any change to engine
# code that alters output for the captured input sequence.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO_ROOT/build/raptor.app/Contents/MacOS/raptor"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <script-name> [<script-name> ...]" >&2
    exit 2
fi
if [[ ! -x "$BIN" ]]; then
    echo "build the binary first (cmake --build build)" >&2
    exit 2
fi

FAILED=0
for SCRIPT in "$@"; do
    SCRIPT_PATH="$REPO_ROOT/tests/scripts/$SCRIPT.txt"
    GOLDEN="$REPO_ROOT/tests/golden/$SCRIPT.frames.txt"

    if [[ ! -f "$SCRIPT_PATH" ]]; then
        echo "[golden_check] $SCRIPT: no such script ($SCRIPT_PATH)" >&2
        FAILED=1
        continue
    fi
    if [[ ! -f "$GOLDEN" ]]; then
        echo "[golden_check] $SCRIPT: no golden ($GOLDEN); run golden_capture.sh first" >&2
        FAILED=1
        continue
    fi

    OUT="$(mktemp -t raptor_check.XXXXXX)"
    cd "$REPO_ROOT"
    RAPTOR_PLAYTHROUGH="$SCRIPT_PATH" \
    RAPTOR_GOLDEN_OUT="$OUT" \
    RAPTOR_TEST_DETERMINISTIC=1 \
    timeout 120 "$BIN" >/dev/null 2>&1 || true

    if diff -q "$GOLDEN" "$OUT" >/dev/null; then
        N=$(grep -cE '^[0-9]' "$GOLDEN" || true)
        echo "[golden_check] $SCRIPT: PASS ($N frames)"
    else
        echo "[golden_check] $SCRIPT: FAIL — first divergence:"
        # `diff -u` exits 1 on difference; `|| true` keeps `set -e` happy
        # while still capturing the diff output. Trim to first 20 lines
        # so a wholesale corruption doesn't fill the terminal — usually
        # only the first few divergent frames are interesting.
        diff -u "$GOLDEN" "$OUT" | head -20 | sed 's/^/    /' || true

        # Show the most recent `# checkpoint:` label before the first
        # divergent line — tells the human which scripted moment broke.
        FIRST_DIFF_LINE=$(diff "$GOLDEN" "$OUT" | grep -m1 '^[<>]' | head -1 || true)
        if [[ -n "$FIRST_DIFF_LINE" ]]; then
            FIRST_FRAME=$(echo "$FIRST_DIFF_LINE" | awk '{print $2}')
            LAST_CKPT=$(awk -v f="$FIRST_FRAME" '
                /^# checkpoint:/ { ckpt=$0 }
                /^[0-9]+ / && $1+0 >= f+0 { print ckpt; exit }
            ' "$GOLDEN" || true)
            if [[ -n "$LAST_CKPT" ]]; then
                echo "    nearest preceding checkpoint: $LAST_CKPT"
            fi
        fi
        FAILED=1
    fi
    rm -f "$OUT"
done

if [[ $FAILED -ne 0 ]]; then
    exit 1
fi
