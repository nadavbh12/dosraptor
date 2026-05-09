// port/platform/gfx_sdl.c
//
// All SDL state and the indexed-to-ARGB palette LUT live here.

#include "gfx_sdl.h"
#include "frame_dump.h"
#include "playthrough.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define LOGICAL_W    320
#define LOGICAL_H    200
#define WINDOW_SCALE 3

static SDL_Window    *g_window;
static SDL_Renderer  *g_renderer;
static SDL_Texture   *g_screen_tex;
static SDL_TimerID    g_frame_timer;
static int            g_deterministic;   /* RAPTOR_TEST_DETERMINISTIC=1 */

// Legacy global declared `volatile INT framecount` in GFX/GFXAPI.C.
// The original DOS build incremented this from a 70 Hz PIT ISR; we
// replicate that with SDL_AddTimer so busy-waits like
// `while (FRAME_COUNT == hold)` in GFX_DisplayUpdate / IMS_WaitTimed
// make progress even without a present call.
extern volatile int framecount;

// 256-entry ARGB8888 palette LUT, fed by GFX_SetPalette via
// gfx_sdl_set_palette. Initialized to opaque black so an unset palette
// shows as a uniform black screen rather than uninitialized noise.
static uint32_t g_pal_lut[256];

// Frame buffer in ARGB8888 format, filled per-present from displaybuffer
// through the LUT. Heap-allocated to avoid 256 KB on the BSS.
static uint32_t *g_argb;

static int g_should_quit = 0;

static Uint32 frame_tick_cb(Uint32 interval, void *param)
{
    (void)param;
    // Plain int store — atomic on aarch64, and the read side is volatile.
    framecount++;
    // Drive playthrough off the wall-clock timer too — the menu spin only
    // calls GFX_DisplayUpdate on input change, so a present-only tick
    // starves. playthrough writes single 32-bit ints; race vs main-thread
    // reader is bounded to a single stale read, no UB.
    raptor_playthrough_tick();
    return interval;
}

int gfx_sdl_init(void)
{
    // Tell SDL to set up NSApplication as a regular foreground app on
    // macOS. Without this, a binary launched from terminal stays a
    // background process and Cocoa routes keyboard events to Terminal.
    // Must be set BEFORE SDL_Init for it to take effect.
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    /* Deterministic clock mode: framecount is driven by pump_events
     * (one tick per pump call) instead of a 70Hz wall-clock timer.
     * Same input + same engine code path → same framecount progression
     * → reproducible frame hashes across machines. Without this,
     * pixel-perfect parity tests are impossible: a slow machine pumps
     * less often than a fast one in the same wall-clock interval. */
    const char *det = getenv("RAPTOR_TEST_DETERMINISTIC");
    g_deterministic = (det && *det && *det != '0') ? 1 : 0;

    if (!g_deterministic) {
        // 70 Hz tick rate matches the original PIT divisor used by GFX_Init.
        g_frame_timer = SDL_AddTimer(1000 / 70, frame_tick_cb, NULL);
    } else {
        fprintf(stdout, "gfx_sdl: deterministic clock (framecount driven by pump)\n");
        fflush(stdout);
    }

    g_window = SDL_CreateWindow(
        "Raptor: Call Of The Shadows",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        LOGICAL_W * WINDOW_SCALE, LOGICAL_H * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return -1; }

    g_renderer = SDL_CreateRenderer(g_window, -1,
        SDL_RENDERER_ACCELERATED);
    if (!g_renderer) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return -1; }
    SDL_RenderSetLogicalSize(g_renderer, LOGICAL_W, LOGICAL_H);

    g_screen_tex = SDL_CreateTexture(g_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        LOGICAL_W, LOGICAL_H);
    if (!g_screen_tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return -1; }

    // Pull the window in front and grab keyboard focus. macOS launched
    // from terminal often leaves the bundle unactivated — the window is
    // visible but Cocoa routes key events to Terminal.app, so the legacy
    // game loop never sees a keystroke.
    SDL_RaiseWindow(g_window);
    SDL_SetWindowInputFocus(g_window);

    if (getenv("RAPTOR_FOCUS_TRACE")) {
        Uint32 flags = SDL_GetWindowFlags(g_window);
        fprintf(stdout,
            "[focus] driver=%s flags=0x%08x input_focus=%d\n",
            SDL_GetCurrentVideoDriver(), flags,
            (flags & SDL_WINDOW_INPUT_FOCUS) != 0);
        fflush(stdout);
    }

    g_argb = (uint32_t *)malloc(LOGICAL_W * LOGICAL_H * sizeof(uint32_t));
    if (!g_argb) { fprintf(stderr, "gfx_sdl: argb buffer alloc failed\n"); return -1; }
    memset(g_argb, 0, LOGICAL_W * LOGICAL_H * sizeof(uint32_t));

    for (int i = 0; i < 256; i++) g_pal_lut[i] = 0xFF000000u;

    return 0;
}

void gfx_sdl_shutdown(void)
{
    if (g_frame_timer) { SDL_RemoveTimer(g_frame_timer); g_frame_timer = 0; }
    if (g_argb)       { free(g_argb); g_argb = 0; }
    if (g_screen_tex) { SDL_DestroyTexture(g_screen_tex);  g_screen_tex = 0; }
    if (g_renderer)   { SDL_DestroyRenderer(g_renderer);   g_renderer = 0; }
    if (g_window)     { SDL_DestroyWindow(g_window);       g_window = 0; }
    SDL_Quit();
}

void gfx_sdl_set_palette(const uint8_t *pal, int start, int count)
{
    if (!pal) return;
    if (start < 0)   start = 0;
    if (start > 256) start = 256;
    if (count < 0)   count = 0;
    if (start + count > 256) count = 256 - start;

    // Watcom DAC entries are 6-bit. The classic 6->8 scale is
    // (v << 2) | (v >> 4): 0 -> 0, 63 -> 255, monotonic in between.
    // Some palette data on disk is already 8-bit (>63 in the high bits);
    // for those we leave the high bits and skip the OR-fold.
    for (int i = 0; i < count; i++) {
        uint8_t r = pal[i * 3 + 0];
        uint8_t g = pal[i * 3 + 1];
        uint8_t b = pal[i * 3 + 2];
        if (r <= 63) r = (uint8_t)((r << 2) | (r >> 4));
        if (g <= 63) g = (uint8_t)((g << 2) | (g >> 4));
        if (b <= 63) b = (uint8_t)((b << 2) | (b >> 4));
        g_pal_lut[start + i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

extern void kbd_sdl_handle_keydown(SDL_Keysym ks);
extern void kbd_sdl_handle_keyup  (SDL_Keysym ks);

static void pump_events(void)
{
    /* In deterministic mode, the SDL_AddTimer is disabled and framecount
     * advances here instead — once per pump call. Spins like
     * `while (FRAME_COUNT == hold) legacy_pump();` still terminate (after
     * one iteration), and replays produce identical framecount sequences
     * given identical input sequences. raptor_playthrough_tick is also
     * called from the timer callback in non-test mode; mirror that here. */
    if (g_deterministic) {
        framecount++;
        raptor_playthrough_tick();
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            // Legacy game loop doesn't poll gfx_sdl_should_quit, so a
            // queued SDL_QUIT just sits there. Bail directly when the
            // user closes the window or sends SIGTERM — the OS will
            // reclaim SDL/audio resources.
            fflush(stdout);
            exit(0);
        case SDL_KEYDOWN:
            kbd_sdl_handle_keydown(ev.key.keysym);
            // ESC also triggers the platform-level quit flag so the
            // outer driver in sys_main.c can break its loops without
            // having to poll the legacy keyboard[] array itself.
            if (ev.key.keysym.sym == SDLK_ESCAPE) g_should_quit = 1;
            if (ev.key.keysym.sym == SDLK_F12)    raptor_dump_keypress();
            break;
        case SDL_KEYUP:
            kbd_sdl_handle_keyup(ev.key.keysym);
            break;
        }
    }
}

int gfx_sdl_should_quit(void) { return g_should_quit; }

const uint32_t *gfx_sdl_palette_lut(void) { return g_pal_lut; }

void gfx_sdl_pump_events_only(void)
{
    pump_events();
    /* PORT: legacy busy-wait spins (e.g. WIN_Hangar's `while (IMS_IsAck())`
     * after a mouse-driven menu choice) need PTR_B1 to clear when the user
     * releases the button. mouseb1 is only updated by ptr_sdl_poll, which
     * is otherwise only called from gfx_sdl_present — so without polling
     * here, the spin sees PTR_B1 stuck high and hangs. */
    extern void ptr_sdl_poll(SDL_Renderer *renderer);
    ptr_sdl_poll(g_renderer);
}

void *gfx_sdl_window_handle(void) { return g_window; }

void gfx_sdl_warp_mouse(int x, int y)
{
    if (!g_window || !g_renderer) return;
    /* Map 320x200 logical → window pixels. Compute the scale directly
     * from the window size rather than going through
     * SDL_RenderLogicalToWindow (the API exists but reportedly has odd
     * behavior on hi-DPI displays in some SDL builds). */
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_window, &ww, &wh);
    int wx = (x * ww) / 320;
    int wy = (y * wh) / 200;
    SDL_WarpMouseInWindow(g_window, wx, wy);

    if (getenv("RAPTOR_MOUSE_TRACE")) {
        fprintf(stdout, "[warp] logical=(%d,%d) win=(%d,%d) ww=%d wh=%d\n",
                x, y, wx, wy, ww, wh);
        fflush(stdout);
    }
}

/* FNV-1a 64-bit hash of the 64 KB displaybuffer. The 64-bit width gives
 * 1-in-2^64 collision odds on a corpus of millions of frames — fine for
 * a regression hash and short enough for human eyeballing in golden
 * files. Only computed when RAPTOR_GOLDEN_OUT is set. */
static uint64_t hash_fb(const uint8_t *fb)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < LOGICAL_W * LOGICAL_H; i++) {
        h ^= fb[i];
        h *= 1099511628211ull;
    }
    return h;
}

static FILE *g_golden_out;
static unsigned long g_present_seq;

/* Called by playthrough's `checkpoint LABEL` script command. Tags the
 * golden stream with a human-readable label so a diff of two runs makes
 * the divergent moment obvious. */
void gfx_sdl_golden_label(const char *label)
{
    if (!g_golden_out || !label) return;
    fprintf(g_golden_out, "# checkpoint: %s (after frame %lu)\n",
            label, g_present_seq);
    fflush(g_golden_out);
}

void gfx_sdl_present(const uint8_t *displaybuffer)
{
    if (!displaybuffer || !g_argb || !g_renderer || !g_screen_tex) return;

    const int n = LOGICAL_W * LOGICAL_H;
    for (int i = 0; i < n; i++)
        g_argb[i] = g_pal_lut[displaybuffer[i]];

    SDL_UpdateTexture(g_screen_tex, NULL, g_argb, LOGICAL_W * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_screen_tex, NULL, NULL);
    SDL_RenderPresent(g_renderer);

    /* Per-frame hash output. Lazy-opens the file on first present. The
     * hash covers exactly the bytes we just rendered (post-compose,
     * including HUD), so two runs of the same script must produce
     * identical lines. */
    if (!g_golden_out) {
        const char *path = getenv("RAPTOR_GOLDEN_OUT");
        if (path && *path) {
            g_golden_out = fopen(path, "w");
            if (!g_golden_out) {
                fprintf(stderr, "gfx_sdl: fopen(%s): %s\n",
                        path, SDL_GetError());
            } else {
                fprintf(stdout, "gfx_sdl: writing per-frame hashes to %s\n", path);
                fflush(stdout);
            }
        }
    }
    if (g_golden_out) {
        fprintf(g_golden_out, "%lu %016llx\n",
                ++g_present_seq, (unsigned long long)hash_fb(displaybuffer));
        fflush(g_golden_out);
    }

    pump_events();


    // Poll mouse + gamepad once per present.
    extern void ptr_sdl_poll(SDL_Renderer *renderer);
    ptr_sdl_poll(g_renderer);

    // Fire any TSM services whose deadlines have elapsed. Single-threaded
    // — services run on the main thread, no re-entry vs. game logic.
    extern void tsm_sdl_dispatch_due(void);
    tsm_sdl_dispatch_due();

    // Tick the test-mode frame budget (no-op outside test mode).
    extern void raptor_test_play_tick(void);
    raptor_test_play_tick();

    // Periodic debug screenshot, gated by RAPTOR_DUMP_EVERY.
    raptor_dump_on_present();

    // Script-driven event injection. Main-thread by design — SWD_Dialog
    // reads-and-clears lastscan on every iteration, so a timer-thread
    // inject would routinely lose the race.
    raptor_playthrough_tick();
}
