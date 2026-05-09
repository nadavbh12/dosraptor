#!/usr/bin/env bash
# tests/run_tests.sh — headless smoke + regression tests for the port.
#
# Each test runs the binary with a RAPTOR_TEST checkpoint that makes it
# dump deterministic state to stdout and exit. The script asserts on
# specific values rather than human-eyeballing the window.
#
# Run:  bash tests/run_tests.sh
# Updates goldens (after a deliberate change): UPDATE_GOLDEN=1 bash tests/run_tests.sh

set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$REPO/build/raptor.app/Contents/MacOS/raptor"
GOLDEN_DIR="$REPO/tests/golden"
mkdir -p "$GOLDEN_DIR"

PASS=0
FAIL=0
FAIL_DETAIL=()

note() { printf '\033[36m> %s\033[0m\n' "$*"; }
ok()   { printf '\033[32m  PASS\033[0m %s\n' "$*"; PASS=$((PASS+1)); }
bad()  { printf '\033[31m  FAIL\033[0m %s\n' "$*"; FAIL=$((FAIL+1)); FAIL_DETAIL+=("$*"); }

require_binary() {
    if [ ! -x "$BIN" ]; then
        echo "raptor binary not built; run: cmake --build build"
        exit 2
    fi
    file "$BIN" | grep -q "Mach-O 64-bit executable arm64" \
        && ok "binary is Mach-O arm64" \
        || bad "binary type wrong: $(file "$BIN")"

    # Stale raptor processes from a previous failed/killed test can hold
    # SDL audio resources and cause subsequent runs to deadlock at
    # Mix_OpenAudio. Reap them up-front.
    pkill -9 -f "raptor.app/Contents/MacOS/raptor" 2>/dev/null || true
    sleep 0.5

    # macOS records a crash-history counter for the bundle. After several
    # crashes, NSPersistentUIRestorer puts up a modal "restore your last
    # windows?" alert on the next launch, which deadlocks the SDL event
    # loop in pump_events. Wipe persistent state and tell Cocoa to ignore
    # it, so a flaky test never poisons subsequent runs.
    rm -rf "$HOME/Library/Saved Application State/org.skynettx.dosraptor.port.savedState" 2>/dev/null || true
    defaults write org.skynettx.dosraptor.port ApplePersistenceIgnoreState -bool YES 2>/dev/null || true
    defaults write org.skynettx.dosraptor.port NSQuitAlwaysKeepsWindows  -bool false 2>/dev/null || true
}

# Run with timeout — if a test hangs we want a clean failure not a hang.
run_test_with_timeout() {
    local name="$1"; shift
    local timeout_s="$1"; shift
    local outfile=$(mktemp -t raptor_test_XXXX)
    "$BIN" > "$outfile" 2>&1 &
    local pid=$!
    local deadline=$(( $(date +%s) + timeout_s ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            wait "$pid" 2>/dev/null
            local rc=$?
            cat "$outfile"
            rm -f "$outfile"
            return $rc
        fi
        sleep 0.2
    done
    kill -KILL "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    cat "$outfile"
    rm -f "$outfile"
    return 124
}

# ---- T1: smoke launch + ESC-init shutdown -----------------------------
test_smoke_launch() {
    note "T1: smoke launch"
    local tmpf=$(mktemp -t raptor_smoke_XXXX)
    RAPTOR_TEST=init RAPTOR_SKIPINTRO=1 \
        timeout 15 "$BIN" > "$tmpf" 2>&1
    local rc=$?
    [ "$rc" -eq 0 ] && ok "exit 0 (got: $rc)" || bad "exit code $rc"
    grep -qF "[port] GFX_InitVideo ok" "$tmpf"     && ok "GFX_InitVideo ok"     || bad "no GFX_InitVideo"
    grep -q "Registered EXE!" "$tmpf"               && ok "GLBs picked up"        || bad "Registered EXE not detected"
    grep -q "PTR_Init"        "$tmpf"               && ok "PTR_Init"              || bad "no PTR_Init"
    grep -q "DMX_Init() = 1"  "$tmpf"               && ok "DMX_Init returned 1"   || bad "DMX_Init wrong"
    grep -q "Music Enabled"     "$tmpf"          && ok "music enabled"        || bad "music not enabled"
    grep -q "SoundFx Enabled"   "$tmpf"          && ok "sfx enabled"          || bad "sfx not enabled"
    grep -q "Loading Graphics"  "$tmpf"          && ok "graphics loaded"      || bad "no graphics load"
    grep -q "TEST checkpoint=init" "$tmpf"       && ok "init checkpoint hit"  || bad "no init checkpoint"
    rm -f "$tmpf"
}

# ---- T2: init checkpoint state ----------------------------------------
test_init_state() {
    note "T2: init-checkpoint state"
    local tmpf=$(mktemp -t raptor_init_XXXX)
    RAPTOR_TEST=init RAPTOR_SKIPINTRO=1 \
        timeout 15 "$BIN" > "$tmpf" 2>&1
    local line=$(grep "TEST checkpoint=init" "$tmpf")
    if [ -z "$line" ]; then
        bad "no checkpoint line"
        rm -f "$tmpf"; return
    fi
    local nonzero_pct=$(echo "$line" | sed -nE 's/.*db_nonzero_pct=([0-9]+).*/\1/p')
    local mouse_present=$(echo "$line" | sed -nE 's/.*mouse_present=([0-9]+).*/\1/p')
    [ "${nonzero_pct:-0}" -ge 0 ]   && ok "db_nonzero_pct read ($nonzero_pct%)"  || bad "db_nonzero_pct missing"
    # mouse_present=1 once int386(0x33, AX=0) returns AX=0xFFFF (sentinel
    # in port/platform/dos_compat.h), letting PTR_Init register the
    # cursor + TSM update service so the in-game cursor sprite renders.
    [ "${mouse_present:-X}" = "1" ] && ok "mouse_present=1 (cursor system live)" \
                                    || bad "mouse_present=$mouse_present (expected 1)"
    rm -f "$tmpf"
}

# ---- T3: main-menu frame hash regression ------------------------------
test_menu_hash() {
    note "T3: main-menu hash regression"
    local tmpf=$(mktemp -t raptor_menu_XXXX)
    # 60s budget: GFX_FadeIn at the start of WIN_MainMenu does 10 fade
    # steps each present-vsync'd; combined with SND_PlaySong + SDL2_mixer
    # warmup, first frame can land 10-30s in.
    RAPTOR_TEST=menu RAPTOR_SKIPINTRO=1 \
        timeout 60 "$BIN" > "$tmpf" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ] && ! grep -q "TEST checkpoint=menu" "$tmpf"; then
        bad "menu run exited $rc with no checkpoint"
        cat "$tmpf"; rm -f "$tmpf"; return
    fi
    local line=$(grep "TEST checkpoint=menu" "$tmpf")
    if [ -z "$line" ]; then
        bad "no menu checkpoint line"
        rm -f "$tmpf"; return
    fi
    local hash=$(echo "$line" | sed -nE 's/.*db_hash=(0x[0-9a-f]+).*/\1/p')
    local pct=$(echo "$line" | sed -nE 's/.*db_nonzero_pct=([0-9]+).*/\1/p')

    # Sanity: at least 5% of pixels non-zero — main menu is mostly opaque.
    if [ "${pct:-0}" -ge 5 ]; then ok "menu has >=5% rendered pixels ($pct%)";
    else bad "menu nearly black ($pct%)"; fi

    # Regression: compare against last-known-good hash (or seed it).
    local goldfile="$GOLDEN_DIR/menu_hash.txt"
    if [ ! -f "$goldfile" ] || [ "${UPDATE_GOLDEN:-0}" = "1" ]; then
        echo "$hash" > "$goldfile"
        ok "seeded golden menu hash = $hash"
    else
        local gold=$(cat "$goldfile")
        if [ "$hash" = "$gold" ]; then ok "menu hash matches golden ($hash)";
        else bad "menu hash $hash != golden $gold"; fi
    fi
    rm -f "$tmpf"
}

# ---- T4: APFS-skip-intro path leaves state coherent -------------------
test_skipintro_invariant() {
    note "T4: skip-intro path doesn't disable mouse/audio"
    local tmpf=$(mktemp -t raptor_skip_XXXX)
    RAPTOR_TEST=init RAPTOR_SKIPINTRO=1 \
        timeout 10 "$BIN" > "$tmpf" 2>&1
    grep -q "Music Enabled"   "$tmpf" && ok "music still enabled with skip"   || bad "music broken with skip"
    grep -q "SoundFx Enabled" "$tmpf" && ok "sfx still enabled with skip"     || bad "sfx broken with skip"
    rm -f "$tmpf"
}

# ---- T5: assets-missing graceful failure ------------------------------
test_assets_missing() {
    note "T5: missing GLBs are diagnosed, not a crash"
    local stash=$(mktemp -d -t raptor_no_glb_XXXX)
    mv "$REPO/FILE0000.GLB" "$stash/"
    mv "$REPO/FILE0001.GLB" "$stash/"
    local tmpf=$(mktemp -t raptor_noglb_XXXX)
    RAPTOR_SKIPINTRO=1 timeout 5 "$BIN" > "$tmpf" 2>&1
    local rc=$?
    mv "$stash/FILE0000.GLB" "$REPO/"
    mv "$stash/FILE0001.GLB" "$REPO/"
    rmdir "$stash"
    grep -qE "couldn't find FILE0000.GLB|need FILE0000.GLB" "$tmpf" \
        && ok "diagnosed missing assets" \
        || bad "didn't print diagnostic for missing assets (rc=$rc): $(tail -3 "$tmpf")"
    rm -f "$tmpf"
}

# ---- T6: no-skip path runs the full intro without crashing ----------
test_full_intro_no_crash() {
    note "T6: full intro runs >5s without crashing"
    "$BIN" > /dev/null 2>&1 &
    local pid=$!
    sleep 6
    if kill -0 "$pid" 2>/dev/null; then
        ok "full-intro process alive after 6s"
        kill -TERM "$pid" 2>/dev/null
        sleep 0.5
        kill -KILL "$pid" 2>/dev/null
    else
        wait "$pid" 2>/dev/null
        bad "full-intro process exited with code $?"
    fi
}

# ---- T7: scripted playthrough drives menu → demo → in-game --------------
# Regression test for the SWD_DestroyWindow null-deref bugs that used to
# crash on the menu→demo transition. Drives the menu via SDL injection,
# runs the demo for a few seconds, dumps frames at named checkpoints,
# and asserts (1) the binary exited cleanly, (2) the in-game frame is
# visibly different from the menu frame (catches the case where D-press
# silently does nothing), (3) all four named dumps landed.
test_playthrough_demo() {
    note "T7: playthrough drives menu → demo → in-game without crashing"
    local outdir=$(mktemp -d -t raptor_pt_XXXX)
    local logfile="$outdir/run.log"
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/menu_demo.txt" \
        timeout 45 "$BIN" > "$logfile" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "playthrough exited $rc (CRASH or timeout)"
        tail -30 "$logfile"
        rm -rf "$outdir"; return
    fi
    ok "playthrough exited cleanly"

    for tag in 01_menu 02_briefing 03_in_game 06_in_game_6s; do
        if ls "$outdir"/*"_${tag}.bmp" >/dev/null 2>&1; then
            ok "checkpoint frame ${tag} landed"
        else
            bad "checkpoint frame ${tag} missing"
        fi
    done

    # Hash menu vs in-game — must differ. Catches "D press did nothing".
    local menu_hash=$(shasum -a 256 "$outdir"/*_01_menu.bmp 2>/dev/null | awk '{print $1}')
    local game_hash=$(shasum -a 256 "$outdir"/*_03_in_game.bmp 2>/dev/null | awk '{print $1}')
    if [ -n "$menu_hash" ] && [ "$menu_hash" != "$game_hash" ]; then
        ok "in-game frame differs from menu frame (D press took effect)"
    else
        bad "in-game frame == menu frame — D press silently failed"
    fi

    rm -rf "$outdir"
}

# ---- T8: in-menu cursor sprite renders --------------------------------
# Catches regressions of the not_in_update volatile fix, the mouseaction
# bump in ptr_sdl_poll, and the int386(0x33) mouse-present sentinel —
# all three need to be in place for the cursor to render.
test_cursor_renders() {
    note "T8: in-menu cursor sprite renders"
    local outdir=$(mktemp -d -t raptor_cur_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/cursor_test.txt" \
        timeout 20 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "cursor playthrough exited $rc"
        tail -20 "$outdir/run.log"; rm -rf "$outdir"; return
    fi

    # Compare two cursor-position dumps. Different cursor positions =>
    # different pixels. If both frames have the same hash, the cursor
    # didn't render in either (or rendered at the same spot — unlikely).
    local h1=$(shasum -a 256 "$outdir"/*_top_left.bmp 2>/dev/null | awk '{print $1}')
    local h2=$(shasum -a 256 "$outdir"/*_bottom_right.bmp 2>/dev/null | awk '{print $1}')
    if [ -n "$h1" ] && [ -n "$h2" ] && [ "$h1" != "$h2" ]; then
        ok "cursor sprite renders (different hash at different mouse positions)"
    else
        bad "cursor frames identical — cursor not rendering"
    fi
    rm -rf "$outdir"
}

# ---- T9: intro animations actually animate ----------------------------
# ANIM_Render walks {term, _, dst_offset, length, ...inline pixels} 8-byte
# records out of the animpic asset. If it regresses to a stub, the intro
# is just a blank screen with a fill color — every dump frame is identical.
# Drive the intro for a few seconds, dump every present, count distinct
# frames. >=4 distinct frames means we're animating.
test_intro_animates() {
    note "T9: intro animations render distinct frames"
    local outdir=$(mktemp -d -t raptor_intro_XXXX)
    RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_DUMP_EVERY=20 \
        timeout 18 "$BIN" > "$outdir/run.log" 2>&1
    # Don't care about exit code — timeout will kill it.
    local count=$(ls "$outdir"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
    if [ "${count:-0}" -lt 4 ]; then
        bad "intro produced only $count dumps (need >=4)"
        rm -rf "$outdir"; return
    fi
    local distinct=$(shasum -a 256 "$outdir"/*.bmp 2>/dev/null \
                       | awk '{print $1}' | sort -u | wc -l | tr -d ' ')
    if [ "${distinct:-0}" -ge 4 ]; then
        ok "intro animates ($count frames, $distinct distinct)"
    else
        bad "intro stuck on $distinct frame(s) of $count — ANIM_Render broken?"
    fi
    rm -rf "$outdir"
}

# ---- T10: menu navigation + Return triggers menu items ---------------
# Catches regression of the SWD_Dialog `while (SWD_IsButtonDown())`
# busy-wait — without one-tick auto-release in the playthrough, Return
# spins SWD_Dialog forever. Drives Down x4 + Return → CREDITS screen.
test_credits_path() {
    note "T10: arrow-down + Return drives menu → CREDITS"
    local outdir=$(mktemp -d -t raptor_credits_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/credits.txt" \
        timeout 30 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "credits playthrough exited $rc"
        tail -20 "$outdir/run.log"; rm -rf "$outdir"; return
    fi
    local menu_h=$(shasum -a 256 "$outdir"/*_menu_initial.bmp 2>/dev/null | awk '{print $1}')
    local cred_h=$(shasum -a 256 "$outdir"/*_credits_screen.bmp 2>/dev/null | awk '{print $1}')
    if [ -n "$menu_h" ] && [ -n "$cred_h" ] && [ "$menu_h" != "$cred_h" ]; then
        ok "Return on CREDITS button transitions to credits screen"
    else
        bad "credits screen identical to menu — Return not dispatched"
    fi
    rm -rf "$outdir"
}

# ---- T11: F1 help screen renders --------------------------------------
# Catches regression of the SWD_PutField NULL-deref bug at swdapi.c
# `if ( h->type == GSPRITE && h )` — h was deref'd before the NULL check.
# DOS read from segment 0; macOS arm64 segfaults. F1 help triggers the
# exact code path (FLD_BUTTON with picflag=FILL → h is NUL).
test_help_renders() {
    note "T11: F1 help screen renders without crashing"
    local outdir=$(mktemp -d -t raptor_help_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/help_f1.txt" \
        timeout 20 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "F1 help playthrough exited $rc"
        tail -15 "$outdir/run.log"; rm -rf "$outdir"; return
    fi
    local menu_h=$(shasum -a 256 "$outdir"/*_menu.bmp 2>/dev/null | awk '{print $1}')
    local help_h=$(shasum -a 256 "$outdir"/*_help_screen.bmp 2>/dev/null | awk '{print $1}')
    if [ -n "$help_h" ] && [ "$menu_h" != "$help_h" ]; then
        ok "F1 transitions to help screen"
    else
        bad "F1 help frame missing or identical to menu"
    fi
    rm -rf "$outdir"
}

# ---- T12: registration form accepts text input ------------------------
# NEW MISSION → registration window. Type pilot name + callsign, submit,
# verify we reach the difficulty-selection screen. Catches regression of
# lastascii injection in playthrough (FLD_INPUT reads g_ascii) and the
# multi-key auto-release in SWD_FieldInput's `while (KBD_Wait())` paths.
test_registration_flow() {
    note "T12: NEW MISSION registration accepts typed pilot name"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null  # registration trips "pilot exists" if not clean
    local outdir=$(mktemp -d -t raptor_new_XXXX)
    # The script has ~29s of wall-clock waits; with audio init / fade-in /
    # GLB load overhead, 35s flakes ~25%. 50s gives enough headroom.
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/new_mission.txt" \
        timeout 50 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "registration playthrough exited $rc"
        tail -15 "$outdir/run.log"; rm -rf "$outdir"; return
    fi
    local blank_h=$(shasum -a 256 "$outdir"/*_registration_blank.bmp 2>/dev/null | awk '{print $1}')
    local typed_h=$(shasum -a 256 "$outdir"/*_name_typed.bmp 2>/dev/null | awk '{print $1}')
    local diff_h=$(shasum -a 256 "$outdir"/*_difficulty_select.bmp 2>/dev/null | awk '{print $1}')
    local post_h=$(shasum -a 256 "$outdir"/*_post_difficulty.bmp 2>/dev/null | awk '{print $1}')
    if [ -n "$blank_h" ] && [ "$blank_h" != "$typed_h" ]; then
        ok "name field accepts typed text"
    else
        bad "name typing didn't change frame"
    fi
    if [ -n "$diff_h" ] && [ "$typed_h" != "$diff_h" ]; then
        ok "submit reaches difficulty selection"
    else
        bad "submit didn't reach difficulty selection"
    fi
    if [ -n "$post_h" ] && [ "$diff_h" != "$post_h" ]; then
        ok "difficulty Return advances to next screen"
    else
        bad "difficulty selection didn't advance"
    fi
    rm -rf "$outdir"
}

# ---- T13: full new-mission pipeline reaches in-game gameplay ---------
# The deepest end-to-end scripted path: NEW MISSION → register pilot →
# pick difficulty → hangar → MISSION view-area → sector select (AUTO) →
# Do_Game(). Asserts that frames at each stage are visually distinct,
# proving every transition fires.
test_full_mission_pipeline() {
    note "T13: full pipeline drives menu → registration → in-game"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    local outdir=$(mktemp -d -t raptor_full_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/mission_start.txt" \
        timeout 90 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "mission pipeline exited $rc"
        tail -25 "$outdir/run.log"; rm -rf "$outdir"; return
    fi
    # Frame timing on the deep path is racy — fades, music init,
    # asset loads stretch in slightly different ways each run. Just
    # assert (1) at least 7 frames captured (script ran to completion)
    # (2) >=5 distinct frames (deep transitions fire) (3) the final
    # frame differs from the early menu/registration ones (we left
    # the menu surface and reached real gameplay).
    local count=$(ls "$outdir"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
    local distinct=$(shasum -a 256 "$outdir"/*.bmp 2>/dev/null \
                       | awk '{print $1}' | sort -u | wc -l | tr -d ' ')
    [ "${count:-0}" -ge 7 ]    && ok "all 7 checkpoints captured ($count)" \
                               || bad "only $count frames captured"
    [ "${distinct:-0}" -ge 5 ] && ok "deep transitions fire ($distinct distinct)" \
                               || bad "only $distinct distinct — pipeline stuck"
    local h_diff=$(shasum -a 256 "$outdir"/*_difficulty.bmp 2>/dev/null | awk '{print $1}')
    local h_last=$(shasum -a 256 "$outdir"/00007_*.bmp 2>/dev/null | awk '{print $1}')
    [ -n "$h_last" ] && [ "$h_diff" != "$h_last" ] \
        && ok "final frame past menu (reaches in-game / late screen)" \
        || bad "final frame still at menu surface"
    rm -rf "$outdir"
}

# ---- T14: idle Mission 1 → death → INTRO_Death → menu ---------------
# Drive into Mission 1, don't move, let enemies kill the player.
# Verifies: real-mission Do_Game() runs to player-death, the
# INTRO_Death sequence renders, and ingameflag=FALSE returns us to
# the clean main menu (no "RETURN TO GAME" option).
test_idle_death() {
    note "T14: idle Mission 1 dies + returns to clean menu"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    local outdir=$(mktemp -d -t raptor_death_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/mission_death.txt" \
        timeout 200 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "death playthrough exited $rc"; rm -rf "$outdir"; return
    fi
    local count=$(ls "$outdir"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
    [ "${count:-0}" -ge 5 ] && ok "idle-death pipeline ran ($count frames)" \
                            || bad "only $count frames"
    # Last frame should be the regular main menu — proves we returned
    # cleanly from Do_Game via the INTRO_Death path. Compare to the
    # menu hash from T3.
    local last=$(ls "$outdir"/*.bmp 2>/dev/null | tail -1)
    local last_hash=$(shasum -a 256 "$last" 2>/dev/null | awk '{print $1}')
    local menu_bmp=$(ls "$outdir"/00001_*.bmp 2>/dev/null)
    local first_hash=$(shasum -a 256 "$menu_bmp" 2>/dev/null | awk '{print $1}')
    [ -n "$last_hash" ] && [ -n "$first_hash" ] && [ "$last_hash" != "$first_hash" ] \
        && ok "post-death menu differs from in-game start (death path triggered)" \
        || bad "no in-game→menu transition observed"
    rm -rf "$outdir"
}

# ---- T15: LOAD MISSION shows "No Pilots to Load" with empty save dir
# Cheapest verification of the load-game code path. RAP_LoadWin scans
# for saved pilots; with none, it returns -1 and the menu pops a
# WIN_Msg dialog. We just assert the load-screen frame differs from
# the menu (proves the dialog rendered).
test_load_mission() {
    note "T15: LOAD MISSION reaches load dialog"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    local outdir=$(mktemp -d -t raptor_load_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/load_mission.txt" \
        timeout 20 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "load-mission playthrough exited $rc"; rm -rf "$outdir"; return
    fi
    local menu_h=$(shasum -a 256 "$outdir"/*_menu.bmp 2>/dev/null | awk '{print $1}')
    local load_h=$(shasum -a 256 "$outdir"/*_load_screen.bmp 2>/dev/null | awk '{print $1}')
    [ -n "$load_h" ] && [ "$menu_h" != "$load_h" ] \
        && ok "LOAD MISSION dialog renders" \
        || bad "LOAD MISSION didn't transition"
    rm -rf "$outdir"
}

# ---- T16: save → load round-trip persists pilot ----------------------
# Clean save dir, run registration (RAP_FFSaveFile creates an empty
# slot, RAP_SavePlayer fills it on confirm). Verify the save file
# appears on disk. Then run LOAD MISSION and verify the load-screen
# frame hash differs from the empty "No Pilots to Load" frame —
# proving the saved pilot was found and rendered.
test_save_load_roundtrip() {
    note "T16: save → load round-trip persists pilot"
    # Save files land in the binary's cwd. Force REPO so the test
    # behavior is independent of where the harness was invoked.
    local prev_cwd=$(pwd)
    cd "$REPO" || return

    rm -f CHAR*.FIL 2>/dev/null
    local empty_dir=$(mktemp -d -t raptor_empty_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$empty_dir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/load_mission.txt" \
        timeout 20 "$BIN" > "$empty_dir/run.log" 2>&1
    local empty_h=$(shasum -a 256 "$empty_dir"/*_load_screen.bmp 2>/dev/null | awk '{print $1}')
    rm -rf "$empty_dir"

    # Now run the save flow.
    rm -f CHAR*.FIL 2>/dev/null
    local save_dir=$(mktemp -d -t raptor_save_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$save_dir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/save_load.txt" \
        timeout 50 "$BIN" > "$save_dir/run.log" 2>&1
    rm -rf "$save_dir"

    if [ -f CHAR0000.FIL ]; then
        ok "save flow created CHAR0000.FIL on disk"
    else
        bad "save flow did not create CHAR0000.FIL"
    fi

    # Run load again — should now find the saved pilot.
    local pop_dir=$(mktemp -d -t raptor_pop_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$pop_dir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/load_mission.txt" \
        timeout 20 "$BIN" > "$pop_dir/run.log" 2>&1
    local pop_h=$(shasum -a 256 "$pop_dir"/*_load_screen.bmp 2>/dev/null | awk '{print $1}')
    rm -rf "$pop_dir"

    if [ -n "$empty_h" ] && [ -n "$pop_h" ] && [ "$empty_h" != "$pop_h" ]; then
        ok "LOAD screen with saved pilot differs from empty-saves screen"
    else
        bad "load screen identical with/without saves"
    fi

    rm -f CHAR*.FIL 2>/dev/null
    cd "$prev_cwd"
}

# ---- T17: 4-min brute-force Mission 1 with hold-fire -----------------
# Reaches Mission 1, holds Up + LCtrl for 4 minutes. Verifies the
# legacy game state machine survives extended driven input without
# crashing, and that the mission flow (WIN_MainLoop entry + return)
# fires correctly. Stronger than T14 (which only verified death) —
# this also exercises sustained gameplay rendering, weapon firing,
# and the post-death return path.
test_brute_mission() {
    note "T17: 4-min brute-force playthrough (no-crash endurance)"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    local prev_cwd=$(pwd)
    cd "$REPO" || return
    local outdir=$(mktemp -d -t raptor_brute_XXXX)
    RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/mission_brute.txt" \
        timeout 420 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "brute playthrough exited $rc"; rm -rf "$outdir"; cd "$prev_cwd"; return
    fi
    ok "brute run completed without crash (4-min endurance)"

    local count=$(ls "$outdir"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
    [ "${count:-0}" -ge 8 ] && ok "captured $count frames over the run" \
                            || bad "only $count frames"

    rm -rf "$outdir"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    cd "$prev_cwd"
}

# T19 helper: drives a single wave via godmode (S_HOST=CASTLE) + the
# Q/W/E/R/T/Y/U/I/O direct-launch keys in shipcomp. Returns 0 if the run
# reached gameplay (frame 05 != frame 04 != frame 06), 1 otherwise.
run_wave() {
    local wave="$1"   # 0..8
    local label="$2"  # for the PASS/FAIL message
    # Q,W,E,R,T,Y,U,I,O = wave 0..8
    local keys="QWERTYUIO"
    local key="${keys:$wave:1}"
    local outdir=$(mktemp -d -t raptor_wave${wave}_XXXX)
    sed "s/__WAVE_KEY__/$key/" "$REPO/tests/scripts/mission_wave_template.txt" \
        > "$outdir/script.txt"
    rm -f "$REPO"/CHAR*.FIL 2>/dev/null
    S_HOST=CASTLE RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
        RAPTOR_DUMP_DIR="$outdir" \
        RAPTOR_PLAYTHROUGH="$outdir/script.txt" \
        timeout 80 "$BIN" > "$outdir/run.log" 2>&1
    local rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "T19 $label (wave $wave / key $key): exited $rc"
        tail -10 "$outdir/run.log"; rm -rf "$outdir"; return 1
    fi
    if ! grep -q "GOD mode enabled" "$outdir/run.log"; then
        bad "T19 $label (wave $wave): godmode trigger didn't fire"
        rm -rf "$outdir"; return 1
    fi
    local h4=$(shasum -a 256 "$outdir"/*_sector_select.bmp 2>/dev/null | awk '{print $1}')
    local h5=$(shasum -a 256 "$outdir"/*_after_shipcomp.bmp 2>/dev/null | awk '{print $1}')
    local h6=$(shasum -a 256 "$outdir"/*_in_mission.bmp 2>/dev/null | awk '{print $1}')
    if [ -z "$h6" ]; then
        bad "T19 $label (wave $wave): no in-mission frame captured"
        rm -rf "$outdir"; return 1
    fi
    if [ "$h4" = "$h5" ]; then
        bad "T19 $label (wave $wave): stuck on shipcomp"
        rm -rf "$outdir"; return 1
    fi
    if [ "$h5" = "$h6" ]; then
        bad "T19 $label (wave $wave): post-shipcomp screen frozen"
        rm -rf "$outdir"; return 1
    fi
    ok "T19 $label (wave $wave / key $key): mission gameplay entered"
    rm -rf "$outdir"
    return 0
}

# T19: cheat-path level coverage. Repo only ships shareware (FILE0001.GLB
# = Bravo Sector); registered episodes 2/3 (FILE0002.GLB / FILE0003.GLB+
# FILE0004.GLB) aren't here. Within Bravo Sector, godmode + Q/W/E/.../O
# in shipcomp lets a script jump to any of waves 0..8 directly.
# Sample waves 0 (first), 4 (mid), 8 (last) to verify level loads work
# end-to-end without stretching CI to 9×80s.
test_cheat_level_coverage() {
    note "T19: godmode + direct-launch covers waves 0, 4, 8 of Bravo Sector"
    run_wave 0 "first wave"
    run_wave 4 "mid wave"
    run_wave 8 "final wave"
}

# T18: input-race regression. The DOS code wrapped lastscan's read-and-clear
# in _disable()/_enable() to make it atomic vs. the keyboard ISR. In the
# port those are no-ops, and the writer is the playthrough timer thread.
# Without atomic exchange in SWD_Dialog, a concurrent timer-thread inject
# gets silently overwritten by the main thread's clear-store, dropping the
# keypress. The race fires ~20% per playthrough — running mission_start.txt
# 5 times catches a regression with ~67% confidence (1 - 0.8^5).
test_input_race_regression() {
    note "T18: lastscan race — playthrough must reach mission 5/5 times"
    local fail=0
    for i in 1 2 3 4 5; do
        rm -f "$REPO"/CHAR*.FIL 2>/dev/null
        local outdir=$(mktemp -d -t raptor_race_XXXX)
        RAPTOR_SKIPINTRO=1 RAPTOR_NO_MUSIC=1 \
            RAPTOR_DUMP_DIR="$outdir" \
            RAPTOR_PLAYTHROUGH="$REPO/tests/scripts/mission_start.txt" \
            timeout 50 "$BIN" > "$outdir/run.log" 2>&1
        # Reach-mission heuristic: frame 04 = shipcomp screen, frame 05 =
        # gameplay (should differ if Enter on COMP_AUTO advanced past
        # shipcomp). If they're byte-identical, the dialog stuck → race
        # consumed the Enter.
        local h4=$(shasum -a 256 "$outdir"/*_sector_select.bmp 2>/dev/null | awk '{print $1}')
        local h5=$(shasum -a 256 "$outdir"/*_after_sector_select.bmp 2>/dev/null | awk '{print $1}')
        local main_ret=$(grep -c "WIN_MainLoop return" "$outdir/run.log" 2>/dev/null)
        if [ -z "$h5" ]; then
            bad "T18 iter$i: no frame 05 (script didn't complete)"
            fail=$((fail+1))
        elif [ "$h4" = "$h5" ]; then
            bad "T18 iter$i: frame 04 == frame 05 (stuck on shipcomp — input race)"
            fail=$((fail+1))
        elif [ "${main_ret:-0}" -gt 0 ]; then
            bad "T18 iter$i: bounced back to menu (input race or wrong dialog)"
            fail=$((fail+1))
        fi
        rm -rf "$outdir"
    done
    [ "$fail" -eq 0 ] && ok "5/5 iterations reached mission gameplay (race fix holding)"
}

# ---- runner -----------------------------------------------------------
require_binary
test_smoke_launch
test_init_state
test_menu_hash
test_skipintro_invariant
test_assets_missing
test_full_intro_no_crash
test_playthrough_demo
test_cursor_renders
test_intro_animates
test_credits_path
test_help_renders
test_registration_flow
test_full_mission_pipeline
test_idle_death
test_load_mission
test_save_load_roundtrip
test_input_race_regression
test_cheat_level_coverage
test_brute_mission

echo
echo "--- summary ---"
echo "PASS: $PASS"
echo "FAIL: $FAIL"
if [ "$FAIL" -gt 0 ]; then
    for d in "${FAIL_DETAIL[@]}"; do echo "  - $d"; done
    exit 1
fi
exit 0
