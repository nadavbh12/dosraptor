// port/platform/test_hooks.c
//
// Headless test mode. Activated by setting RAPTOR_TEST=<checkpoint>:
//   init    — dump globals after raptor_main's init chain, exit
//   menu    — dump displaybuffer hash + key globals after the main menu
//             draws its first frame, exit
//
// The hooks are called from the legacy game code at known points; in
// non-test builds they're no-ops.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern unsigned char *displaybuffer;
extern int  cur_mx, cur_my;
extern int  mouseb1, mouseb2, mouseb3;
extern int  keyboard[256];
extern int  lastscan;
extern int  mousepresent;
extern volatile int framecount;

// FNV-1a 32-bit over the 64000-byte displaybuffer. Deterministic and
// cheap; perfect for a regression hash.
static uint32_t hash_displaybuffer(void)
{
    if (!displaybuffer) return 0;
    uint32_t h = 2166136261u;
    const uint8_t *p = displaybuffer;
    for (int i = 0; i < 64000; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Fraction of displaybuffer bytes that are non-zero — proves something
// is rendering rather than a clean black frame.
static int displaybuffer_nonzero_pct(void)
{
    if (!displaybuffer) return 0;
    int nz = 0;
    for (int i = 0; i < 64000; i++) if (displaybuffer[i]) nz++;
    return (nz * 100) / 64000;
}

void raptor_test_dump(const char *checkpoint)
{
    fprintf(stdout,
        "TEST checkpoint=%s db_hash=0x%08x db_nonzero_pct=%d "
        "framecount=%d cur_mx=%d cur_my=%d mouse_present=%d "
        "lastscan=%d kb_esc=%d\n",
        checkpoint,
        hash_displaybuffer(),
        displaybuffer_nonzero_pct(),
        framecount,
        cur_mx, cur_my, mousepresent,
        lastscan, keyboard[0x01]);
    fflush(stdout);
}

// Called from raptor_main right after init chain finishes. If
// RAPTOR_TEST=init, dump and exit cleanly.
void raptor_test_init_checkpoint(void)
{
    const char *t = getenv("RAPTOR_TEST");
    if (!t) return;
    if (strcmp(t, "init") == 0) {
        raptor_test_dump("init");
        exit(0);
    }
}

// Play test runs a 4-state machine that proves the input plumbing
// actually drives the menu. `RAPTOR_TEST=play`:
//   state 0: at menu first paint, dump "play_menu" hash, inject ENTER
//   state 1: ~30 frames later, dump "play_after_enter" hash
//            (assertion in run_tests.sh: must differ from play_menu)
//   state 2: ~30 frames later, dump "play_settled" hash + exit
//
// We don't try to drive the full new-game path because that needs name
// entry / hangar navigation. Just proving that one keypress transitions
// the menu validates input -> SWD_Dialog -> game_state plumbing.
static int g_play_state = 0;
static int g_play_state_start_fc = 0;

// Called from WIN_MainMenu after SWD_ShowAllWindows + GFX_DisplayUpdate.
void raptor_test_menu_checkpoint(void)
{
    const char *t = getenv("RAPTOR_TEST");
    if (!t) return;
    if (strcmp(t, "menu") == 0) {
        raptor_test_dump("menu");
        exit(0);
    }
    if (strcmp(t, "play") == 0 && g_play_state == 0) {
        raptor_test_dump("play_menu");
        // Press D to trigger DEM_DEMO1G1 — WIN_MainMenu has explicit
        // handling for SC_D that seeds cur_opt + d_count, then
        // WIN_DemoDelay forces WIN_MainAuto to run the demo. That
        // exercises level loading + rendering through the demo path
        // instead of the new-game registration path.
        lastscan = 0x20;           // SC_D
        keyboard[0x20] = 1;
        g_play_state = 1;
        g_play_state_start_fc = framecount;
    }
}

void raptor_test_play_tick(void)
{
    const char *t = getenv("RAPTOR_TEST");
    if (!t || strcmp(t, "play") != 0) return;

    // Use framecount instead of present-rate counting — SDL_Dialog spins
    // without presenting, so the SDL_AddTimer-driven framecount is the
    // only clock that keeps moving.
    int elapsed = framecount - g_play_state_start_fc;

    if (g_play_state == 1 && elapsed > 140 /* ~2s @ 70Hz */) {
        raptor_test_dump("play_after_enter");
        lastscan = 0x01;          // SC_ESC
        keyboard[0x01] = 1;
        g_play_state = 2;
        g_play_state_start_fc = framecount;
    }
    if (g_play_state == 2 && elapsed > 140) {
        raptor_test_dump("play_settled");
        fflush(stdout);
        exit(0);
    }
}
