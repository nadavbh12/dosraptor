#!/usr/bin/env bash
# Capture parity goldens for all scripts and 100 seeds.
# Output is written to $RAPTOR_GODOT_DIR/tests/parity/.
#
# Failure modes that this script MUST NOT silently swallow:
#   - binary missing or wrong path
#   - script file missing
#   - timeout / crash during the run (exit code != 0)
#   - empty output file for game scripts (zero checkpoints emitted)
#   - count of game-script output files != expected count
#
# Note: non-game scripts (credits, menus) do not enter Do_Game and
# therefore emit no parity rows — they are captured as empty files for
# completeness but are NOT validated for non-empty content.

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/build/raptor.app/Contents/MacOS/raptor"
GODOT_REPO="${RAPTOR_GODOT_DIR:-/Users/nadavb/dev/raptor-godot}"
PARITY_DIR="$GODOT_REPO/tests/parity"

if [[ ! -x "$BIN" ]]; then
    echo "[parity_capture] FATAL: binary not built at $BIN" >&2
    exit 2
fi

mkdir -p "$PARITY_DIR/scripts" "$PARITY_DIR/scripts/holdout" \
         "$PARITY_DIR/demos" "$PARITY_DIR/seeds"

# run_one_game: run a script that enters Do_Game; fail if output is empty.
run_one_game() {
    local script="$1" out="$2" extra_env="${3:-}"
    if [[ ! -f "$script" ]]; then
        echo "[parity_capture] FATAL: missing script $script" >&2
        return 1
    fi
    rm -f "$out"
    cd "$REPO"
    if ! eval "$extra_env" \
            RAPTOR_PLAYTHROUGH="$script" \
            RAPTOR_PARITY_OUT="$out" \
            RAPTOR_TEST_DETERMINISTIC=1 \
            timeout 120 "$BIN" >/dev/null 2>&1; then
        echo "[parity_capture] FATAL: run failed for $script" >&2
        return 1
    fi
    if [[ ! -s "$out" ]]; then
        echo "[parity_capture] FATAL: empty output $out (no Do_Game checkpoints emitted)" >&2
        return 1
    fi
}

# run_one_menu: run a non-game script (credits, menus, etc.); accept empty output.
run_one_menu() {
    local script="$1" out="$2"
    if [[ ! -f "$script" ]]; then
        echo "[parity_capture] FATAL: missing script $script" >&2
        return 1
    fi
    rm -f "$out"
    cd "$REPO"
    if ! RAPTOR_PLAYTHROUGH="$script" \
            RAPTOR_PARITY_OUT="$out" \
            RAPTOR_TEST_DETERMINISTIC=1 \
            timeout 120 "$BIN" >/dev/null 2>&1; then
        echo "[parity_capture] FATAL: run failed for $script" >&2
        return 1
    fi
    # Empty output is expected for non-game scripts — touch to ensure file exists.
    touch "$out"
}

# Non-game scripts: drive menus/text but never enter Do_Game.
# Parity output is empty (no game state to snapshot).
NON_GAME_SCRIPTS=(credits help_f1 menu_demo order load_mission)

# Game scripts: enter Do_Game and emit game-state checkpoints.
GAME_SCRIPTS=(mission_start full_demo mission_long)

for s in "${NON_GAME_SCRIPTS[@]}"; do
    echo "[parity_capture] menu-script: $s"
    run_one_menu "$REPO/tests/scripts/$s.txt" "$PARITY_DIR/scripts/$s.parity.txt"
done

for s in "${GAME_SCRIPTS[@]}"; do
    echo "[parity_capture] game-script: $s"
    run_one_game "$REPO/tests/scripts/$s.txt" "$PARITY_DIR/scripts/$s.parity.txt"
done

ALL_SCRIPTS=("${NON_GAME_SCRIPTS[@]}" "${GAME_SCRIPTS[@]}")

for s in holdout1 holdout2; do
    if [[ -f "$REPO/tests/scripts/holdout/$s.txt" ]]; then
        echo "[parity_capture] held-out: $s"
        run_one_game "$REPO/tests/scripts/holdout/$s.txt" \
                "$PARITY_DIR/scripts/holdout/$s.parity.txt"
    else
        echo "[parity_capture] note: $s.txt absent; held-out scripts not yet defined" >&2
    fi
done

# Demo capture deferred to Stage 6.

for n in $(seq 0 99); do
    echo "[parity_capture] seed: $n"
    run_one_game "$REPO/tests/scripts/mission_start.txt" \
            "$PARITY_DIR/seeds/seed_$n.parity.txt" \
            "RAPTOR_RNG_SEED_OVERRIDE=$n"
done

total_expected=${#ALL_SCRIPTS[@]}
visible_count=$(ls "$PARITY_DIR/scripts/"*.parity.txt 2>/dev/null | wc -l | tr -d ' ')
if [[ $visible_count -ne $total_expected ]]; then
    echo "[parity_capture] FATAL: scripts captured=$visible_count expected=$total_expected" >&2
    exit 1
fi
seed_count=$(ls "$PARITY_DIR/seeds/seed_"*.parity.txt 2>/dev/null | wc -l | tr -d ' ')
if [[ $seed_count -ne 100 ]]; then
    echo "[parity_capture] FATAL: seeds captured=$seed_count expected=100" >&2
    exit 1
fi

echo "[parity_capture] done: $visible_count scripts ($total_expected expected), $seed_count seeds"
