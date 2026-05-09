// port/platform/playthrough.c
//
// Drives the legacy game from a script of input events. Inputs are
// pushed onto the SDL event queue (SDL_PushEvent) so the full SDL pump
// path runs — kbd_sdl_handle_keydown, ptr_sdl_poll, the busy-wait
// pumps in IMS/SWD/KBD primitives. Direct lastscan/keyboard[] writes
// would bypass that path and silently mask SDL-bound bugs (we hit five
// of those in one session before this harness existed).
//
// Wait counters are driven by framecount, which in deterministic mode
// (RAPTOR_TEST_DETERMINISTIC=1) advances per pump rather than per
// wall-clock tick. Same script + same engine code → same frames.

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

/* Set by gfx_sdl when RAPTOR_GOLDEN_OUT is configured. The harness writes
 * `# checkpoint: <label>` lines into the same stream so labels show up
 * inline with frame hashes — trivial to spot the divergent frame in a
 * diff and immediately know which scripted moment broke. */
extern void gfx_sdl_golden_label(const char *label);

extern unsigned char kbd_sdl_xt_for_scancode(SDL_Scancode sdl);

static FILE *g_script;
static int   g_wait_until_fc;
static int   g_done;
static int   g_menu_ready;     /* gates dispatch until menu is fully painted */
static SDL_Scancode g_release_sc; /* SDL scancode of key to release on next tick */

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

/* Push a synthetic SDL_KEYDOWN onto the event queue. The next pump
 * (legacy_pump or pump_events) will dispatch it through
 * kbd_sdl_handle_keydown, which sets keyboard[xt]/lastscan/kbd_ack the
 * same way a real keystroke does. This goes through the SDL pump path,
 * so any code that depends on `legacy_pump()` to see input — IMS
 * primitives, SWD_Dialog, KBD_Wait — exercises the right code path. */
static void inject_keydown(SDL_Scancode sc)
{
    SDL_Event ev = {0};
    ev.type            = SDL_KEYDOWN;
    ev.key.type        = SDL_KEYDOWN;
    ev.key.state       = SDL_PRESSED;
    ev.key.repeat      = 0;
    ev.key.keysym.scancode = sc;
    ev.key.keysym.sym      = SDL_GetKeyFromScancode(sc);
    ev.key.timestamp   = SDL_GetTicks();
    SDL_PushEvent(&ev);
}

static void inject_keyup(SDL_Scancode sc)
{
    SDL_Event ev = {0};
    ev.type            = SDL_KEYUP;
    ev.key.type        = SDL_KEYUP;
    ev.key.state       = SDL_RELEASED;
    ev.key.repeat      = 0;
    ev.key.keysym.scancode = sc;
    ev.key.keysym.sym      = SDL_GetKeyFromScancode(sc);
    ev.key.timestamp   = SDL_GetTicks();
    SDL_PushEvent(&ev);
}

static SDL_Scancode scancode_for(const char *name)
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
    }
    return sc;
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
        SDL_Scancode sc = scancode_for(arg);
        if (sc != SDL_SCANCODE_UNKNOWN) {
            inject_keydown(sc);
            /* Schedule release on the NEXT tick. SWD_Dialog has a
             * `while (SWD_IsButtonDown())` busy-wait on F_SELECT that
             * spins forever if keyboard[xt] never goes back to 0.
             * One-tick press is enough for the menu to dispatch. */
            g_release_sc = sc;
            fprintf(stdout, "playthrough: key %s (sc=%d)\n", arg, (int)sc);
            fflush(stdout);
        }
        return 1;
    }
    if (strcmp(cmd, "down") == 0 && n == 2) {
        SDL_Scancode sc = scancode_for(arg);
        if (sc != SDL_SCANCODE_UNKNOWN) inject_keydown(sc);
        fprintf(stdout, "playthrough: down %s\n", arg); fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "up") == 0 && n == 2) {
        SDL_Scancode sc = scancode_for(arg);
        if (sc != SDL_SCANCODE_UNKNOWN) inject_keyup(sc);
        fprintf(stdout, "playthrough: up %s\n", arg); fflush(stdout);
        return 1;
    }
    if (strcmp(cmd, "dump") == 0 && n == 2) {
        raptor_dump_frame(arg);
        return 1;
    }
    if (strcmp(cmd, "checkpoint") == 0 && n == 2) {
        /* Write `# checkpoint: LABEL` into the golden file just before
         * the next frame hash. When two runs diverge, you see exactly
         * which scripted moment broke without counting frames. */
        gfx_sdl_golden_label(arg);
        fprintf(stdout, "playthrough: checkpoint %s\n", arg);
        fflush(stdout);
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
    if (g_release_sc != SDL_SCANCODE_UNKNOWN) {
        inject_keyup(g_release_sc);
        g_release_sc = SDL_SCANCODE_UNKNOWN;
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
