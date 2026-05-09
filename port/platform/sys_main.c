// port/platform/sys_main.c
//
// SDL entry point. M3 driver: walk the legacy init chain, then load
// APOGEE_PIC + POGPAL_DAT out of FILE0001.GLB, blit into displaybuffer,
// feed the palette LUT, and present until ESC. Proves the entire
// rendering plumbing (palette, asm-c blitters, texture upload) end-to-end.

#include "gfx_sdl.h"

#include <SDL.h>      /* SDL_main */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <mach-o/dyld.h>

// Reliable Ctrl-C exit. SDL installs its own SIGINT handler that posts
// SDL_QUIT, but the legacy game loops don't consult gfx_sdl_should_quit,
// so SDL_QUIT just sits in the queue. Force a hard exit instead — the
// OS reclaims SDL/audio resources on process exit.
static void on_sigint(int sig) { (void)sig; _exit(130); }

// ---- Legacy SOURCE/RAP.C entry point ----------------------------------
// RAP.C's `main()` was renamed to `raptor_main` via -Dmain=raptor_main
// when that translation unit is compiled (see CMakeLists.txt).
extern void  raptor_main(int argc, char *argv[]);

extern void  kbd_sdl_init(void);
extern void  ptr_sdl_init(void);

// Synthetic argv for raptor_main. The original RAP.C reads argv[1] for
// optional flags ("joycal", "REC", "PLAY") via strcmpi — passing an
// empty string keeps every comparison false, taking the default play path.
static char *g_synth_argv[] = { (char *)"raptor", (char *)"", NULL };

// Find a directory containing FILE0000.GLB and chdir into it. Search
// order: current cwd, the directory of the executable, then progressively
// shorter parent directories (up to 6 levels). This makes the bundle
// launchable via Finder/`open` regardless of where it was started from.
static int locate_assets_and_chdir(void)
{
    if (access("FILE0000.GLB", F_OK) == 0) return 0;

    char exe[1024];
    uint32_t len = sizeof(exe);
    if (_NSGetExecutablePath(exe, &len) != 0) return -1;

    // Strip the basename to get the executable's directory, then walk up.
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", exe);
    char *slash = strrchr(dir, '/');
    if (!slash) return -1;
    *slash = '\0';

    for (int up = 0; up < 6; up++) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s/FILE0000.GLB", dir);
        if (access(candidate, F_OK) == 0) {
            if (chdir(dir) != 0) return -1;
            fprintf(stdout, "raptor: assets found at %s\n", dir);
            return 0;
        }
        slash = strrchr(dir, '/');
        if (!slash) break;
        *slash = '\0';
    }
    return -1;
}


int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    extern void raptor_crash_trace_install(void);
    raptor_crash_trace_install();

    if (gfx_sdl_init() != 0) {
        gfx_sdl_shutdown();
        return 1;
    }
    // Install AFTER SDL_Init so we override SDL's own SIGINT handler.
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    // Promote the bundle and force the SDL NSWindow to be the key window.
    // The earlier "macOS hardening" hypothesis was wrong: input_focus=0
    // means our NSWindow never became key, not that activation was
    // blocked. Direct [nswin makeKeyAndOrderFront:] bypasses NSApp.
    extern void macos_activate_app(void);
    extern int  macos_make_window_key(void *sdl_window);
    extern void *gfx_sdl_window_handle(void);
    macos_activate_app();
    macos_make_window_key(gfx_sdl_window_handle());
    kbd_sdl_init();
    ptr_sdl_init();
    extern void raptor_dump_init(void);
    raptor_dump_init();
    extern void raptor_playthrough_init(void);
    raptor_playthrough_init();
    fprintf(stdout, "raptor: M6/M7 skeleton up (audio + mouse + gamepad)\n");
    if (locate_assets_and_chdir() != 0) {
        fprintf(stderr,
            "raptor: couldn't find FILE0000.GLB anywhere reachable from "
            "the executable's location. Drop the shareware GLBs and "
            "SETUP.INI next to the .app or run from the directory that "
            "contains them.\n");
    }
    // Dev shortcut: setting RAPTOR_SKIPINTRO pokes the legacy `lastscan`
    // global before raptor_main runs, so MOVIE_Play's first IMS_CheckAck
    // returns TRUE and the intro sequence skips. The main-menu loop is
    // what we're iterating against.
    if (getenv("RAPTOR_SKIPINTRO")) {
        extern int lastscan;
        extern int kbd_ack;
        lastscan = 0x01;  /* SC_ESC */
        kbd_ack  = 1;
        fprintf(stdout, "raptor: RAPTOR_SKIPINTRO set — pre-arming ESC to skip intro\n");
    }

    fprintf(stdout, "raptor: handing off to raptor_main ...\n"); fflush(stdout);

    raptor_main(2, g_synth_argv);

    gfx_sdl_shutdown();
    return 0;
}
