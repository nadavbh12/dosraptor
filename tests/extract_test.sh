#!/usr/bin/env bash
# Smoke-test the asset extractor: verifies it produces N+ files in
# each expected subdirectory. Run after rebuilding.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/build/tools/extract_assets/extract_assets"
GLB0="$REPO/FILE0000.GLB"
GLB1="$REPO/FILE0001.GLB"
OUT="$(mktemp -d -t raptor_extract_test)"
trap 'rm -rf "$OUT"' EXIT

if [[ ! -x "$BIN" ]]; then
    echo "build first: cmake --build build --target extract_assets"
    exit 2
fi

"$BIN" "$GLB0" "$GLB1" "$OUT" >/dev/null

check() {
    local d="$1" min="$2"
    local n
    n=$(ls "$OUT/$d" 2>/dev/null | wc -l | tr -d ' ')
    if (( n < min )); then
        echo "[extract_test] FAIL: $d has $n files, expected >= $min"
        return 1
    fi
    echo "[extract_test] $d: $n files (OK)"
}

check sprites      50
check sprites_meta 1
check levels       1
check demos        1
check sounds       1
check music        1
echo "[extract_test] PASS"
