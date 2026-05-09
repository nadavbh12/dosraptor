// port/platform/playthrough.c
//
// Drives the legacy game without real keyboard input by writing directly
// into the keyboard[]/lastscan/kbd_ack globals — the same path
// RAPTOR_TEST=play uses to inject single keys. This bypasses the SDL
// event chain entirely, so it works regardless of the macOS focus issue
// and regardless of whether the menu spin is actively pumping events.
//
// Wait counters are driven by the 70Hz framecount timer (SDL_AddTimer
// in gfx_sdl.c) rather than gfx_sdl_present calls — the menu spin
// presents at ~1-2 Hz, so a present-based wait would take 30+ seconds
// for what should be a one-second pause.

#include "playthrough.h"
#include "frame_dump.h"

#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int keyboard[256];
extern int lastscan;
extern int lastascii;
extern int ASCIINames[];
extern int kbd_ack;
extern int cur_mx;
extern int cur_my;
extern int mouseb1;
extern int mouseaction;
extern volatile int framecount;

extern unsigned char kbd_sdl_xt_for_scancode(SDL_Scancode sdl);

static FILE *g_script;
static int   g_wait_until_fc;
static int   g_done;
static int   g_menu_ready;   /* gates dispatch until menu is fully painted */
static int   g_release_xt;   /* xt scancode of key to release on next tick */

void raptor_playthrough_init(void)
{
    const char *path = getenv("RAPTOR_PLAYTHROUGH");
    if (!path || !*path) return;

    g_script = fopen(path, "r");
    if (!g_script) {
        fprintf(stderr, "playthrough: fopen(%s): %s\n",
                path, strerror(errno));
        return;
    }
    fprintf(stdout, "playthrough: driving from %s\n", path);
    fflush(stdout);
}

static void inject_key(unsigned char xt)
{
    /* PORT: only set lastscan/lastascii — these are consume-once (cleared
     * by SWD_Dialog's first read). For genuinely held keys, use explicit
     * `down NAME` / `up NAME`.
     *
     * lastscan is written here on the timer thread and read-and-cleared
     * on the main thread inside SWD_Dialog (which uses __atomic_exchange).
     * Order matters: write the satellite fields (lastascii, kbd_ack)
     * BEFORE the release-store on lastscan, so the matching acquire-load
     * publishes them. Otherwise a reader could see g_key=xt with g_ascii
     * still stale. */
    if (xt < 128) lastascii = ASCIINames[xt];
    kbd_ack      = 1;
    __atomic_store_n(&lastscan, xt, __ATOMIC_RELEASE);
}

static unsigned char xt_for(const char *name)
{
    /* Aliases for keys whose canonical SDL name contains whitespace.
     * Our script parser splits on space, so "Left Ctrl" would arrive
     * as just "Left". Provide compact one-word aliases for the keys
     * gameplay needs (LCtrl is fire). */
    SDL_Scancode sc;
    if      (strcasecmp(name, "lctrl")  == 0) sc = SDL_SCANCODE_LCTRL;
    else if (strcasecmp(name, "rctrl")  == 0) sc = SDL_SCANCODE_RCTRL;
    else if (strcasecmp(name, "lalt")   == 0) sc = SDL_SCANCODE_LALT;
    else if (strcasecmp(name, "ralt")   == 0) sc = SDL_SCANCODE_RALT;
    else if (strcasecmp(name, "lshift") == 0) sc = SDL_SCANCODE_LSHIFT;
    else if (strcasecmp(name, "rshift") == 0) sc = SDL_SCANCODE_RSHIFT;
    else                                       sc = SDL_GetScancodeFromName(name);

    if (sc == SDL_SCANCODE_UNKNOWN) {
        fprintf(stderr, "playthrough: unknown key '%s'\n", name);
        return 0;
    }
    unsigned char xt = kbd_sdl_xt_for_scancode(sc);
    if (xt == 0) {
        fprintf(stderr, "playthrough: no XT scancode for '%s'\n", name);
    }
    return xt;
}

// Returns 1 if a command consumed the tick (caller should stop processing
// for this tick), 0 if the line was inert and we should keep reading.
static int dispatch(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || *line == '\n') return 0;

    char cmd[32], arg[64];
    int n = sscanf(line, "%31s %63s", cmd, arg);
    if (n < 1) return 0;

    if (strcmp(cmd, "wait") == 0 && n == 2) {
        int frames = atoi(arg);
        g_wait_until_fc = framecount + frames;
        fprintf(stdout, "playthrough: wait %d (until fc=%d)\n",
                frames, g_wait_until_fc);
        fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "key") == 0 && n == 2) {
        unsigned char xt = xt_for(arg);
        if (xt) {
            inject_key(xt);
            /* Schedule release on the NEXT tick. SWD_Dialog has a
             * `while (SWD_IsButtonDown())` busy-wait on F_SELECT that
             * spins forever if keyboard[xt] never goes back to 0.
             * One-tick press is enough for the menu to dispatch. */
            g_release_xt = xt;
            fprintf(stdout, "playthrough: key %s (xt=0x%02x)\n", arg, xt);
            fflush(stdout);
        }
        return 1;
    }
    if (strcmp(cmd, "down") == 0 && n == 2) {
        unsigned char xt = xt_for(arg);
        if (xt) keyboard[xt] = 1;
        fprintf(stdout, "playthrough: down %s\n", arg); fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "up") == 0 && n == 2) {
        unsigned char xt = xt_for(arg);
        if (xt) keyboard[xt] = 0;
        fprintf(stdout, "playthrough: up %s\n", arg); fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "dump") == 0 && n == 2) {
        raptor_dump_frame(arg);
        return 1;
    }
    if (strcmp(cmd, "mouse") == 0 && n == 2) {
        /* "mouse X,Y" — set cur_mx and cur_my directly. ptr_sdl_poll
         * overwrites these every present from real SDL mouse state, so
         * this is most useful when the SDL window has no input focus
         * (real mouse coords end up clamped to 0,0). */
        int x = 0, y = 0;
        if (sscanf(arg, "%d,%d", &x, &y) == 2) {
            cur_mx = x;
            cur_my = y;
            mouseaction = 1;     /* nudge PTR_FrameHook to redraw */
            fprintf(stdout, "playthrough: mouse %d,%d\n", x, y);
            fflush(stdout);
        }
        return 1;
    }
    if (strcmp(cmd, "click") == 0) {
        /* one-frame mouseb1 pulse; legacy code reads it on the next
         * SWD_Dialog iteration and treats it as a click. */
        mouseb1 = 1;
        fprintf(stdout, "playthrough: click\n"); fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "quit") == 0) {
        fprintf(stdout, "playthrough: quit\n"); fflush(stdout);
        g_done = 1;
        exit(0);
    }

    fprintf(stderr, "playthrough: bad line: %s", line);
    return 0;
}

void raptor_playthrough_menu_ready(void)
{
    if (g_menu_ready) return;
    g_menu_ready = 1;
    /* Reset the wait clock to the menu's framecount so "wait N" in the
     * script means "N frames after the menu paints," not "N frames after
     * the binary started." */
    g_wait_until_fc = framecount;
    fprintf(stdout, "playthrough: menu ready at fc=%d, dispatching\n",
            framecount);
    fflush(stdout);
}

void raptor_playthrough_tick(void)
{
    if (!g_script || g_done) return;
    if (!g_menu_ready) return;

    /* Release any momentarily-pressed key from the previous tick, so the
     * legacy code's `while (SWD_IsButtonDown())` loops can exit. */
    if (g_release_xt) {
        keyboard[g_release_xt] = 0;
        g_release_xt = 0;
    }

    if (framecount < g_wait_until_fc) return;

    char buf[256];
    while (fgets(buf, sizeof buf, g_script)) {
        if (dispatch(buf)) return;
    }

    fprintf(stdout, "playthrough: end-of-script, exiting\n");
    fflush(stdout);
    g_done = 1;
    exit(0);
}
