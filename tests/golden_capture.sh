#!/usr/bin/env bash
# Capture pixel-hash goldens for a playthrough script.
#
# Usage: tests/golden_capture.sh <script-name>
#   <script-name> matches tests/scripts/<name>.txt (no .txt suffix).
# Output: tests/golden/<name>.frames.txt
#
# Run twice and confirm the two runs are byte-identical before treating
# the result as the golden — otherwise the script has nondeterminism we
# need to fix first.

set -euo pipefail

SCRIPT="${1:-}"
if [[ -z "$SCRIPT" ]]; then
    echo "usage: $0 <script-name>" >&2
    exit 2
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_PATH="$REPO_ROOT/tests/scripts/$SCRIPT.txt"
BIN="$REPO_ROOT/build/raptor.app/Contents/MacOS/raptor"
GOLDEN="$REPO_ROOT/tests/golden/$SCRIPT.frames.txt"

if [[ ! -f "$SCRIPT_PATH" ]]; then
    echo "no such script: $SCRIPT_PATH" >&2
    exit 2
fi
if [[ ! -x "$BIN" ]]; then
    echo "build the binary first (cmake --build build)" >&2
    exit 2
fi

run_once() {
    local out="$1"
    rm -f "$out"
    cd "$REPO_ROOT"
    RAPTOR_PLAYTHROUGH="$SCRIPT_PATH" \
    RAPTOR_GOLDEN_OUT="$out" \
    RAPTOR_TEST_DETERMINISTIC=1 \
    timeout 120 "$BIN" >/dev/null 2>&1 || true
}

A="$(mktemp -t raptor_golden_a.XXXXXX)"
B="$(mktemp -t raptor_golden_b.XXXXXX)"
trap 'rm -f "$A" "$B"' EXIT

echo "[golden_capture] $SCRIPT — run 1/2 (determinism check)"
run_once "$A"
echo "[golden_capture] $SCRIPT — run 2/2"
run_once "$B"

if ! diff -q "$A" "$B" >/dev/null; then
    echo "[golden_capture] FAIL: two runs of $SCRIPT diverged. Goldens are not safe to record." >&2
    diff "$A" "$B" | head -20 >&2
    exit 1
fi

mkdir -p "$(dirname "$GOLDEN")"
cp "$A" "$GOLDEN"
N=$(grep -cE '^[0-9]' "$GOLDEN" || true)
C=$(grep -cE '^# checkpoint' "$GOLDEN" || true)
echo "[golden_capture] OK: wrote $GOLDEN ($N frames, $C checkpoints)"
