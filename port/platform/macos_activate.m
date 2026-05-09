// port/platform/macos_activate.m
//
// On macOS, a binary launched from terminal is a "background process" by
// default — its windows are visible but Cocoa routes keyboard events to
// Terminal.app. SDL2 doesn't fix this for us unless we go through
// NSApplicationMain, which our setup bypasses.
//
// Calling this once after gfx_sdl_init promotes us to a regular foreground
// application and pulls keyboard focus to our window.

// dos_compat.h is force-included for every translation unit and clobbers
// identifiers AppKit uses as method names ("interrupt" on NSTask, etc).
// Undo just the offenders before pulling in Cocoa headers.
#undef interrupt
#undef _interrupt
#undef near
#undef far
#undef huge
#undef cdecl
#undef pascal
#undef _far
#undef _loadds

#import <AppKit/AppKit.h>
#import <SDL.h>
#import <SDL_syswm.h>

void macos_activate_app(void)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    /* SDL2 doesn't always call finishLaunching — without it, NSApp
     * never enters the "fully launched" state and activate calls are
     * silently dropped. Calling it explicitly is harmless if SDL
     * already did. */
    static BOOL g_finished_launching = NO;
    if (!g_finished_launching) {
        [NSApp finishLaunching];
        g_finished_launching = YES;
    }
    [NSApp activateIgnoringOtherApps:YES];
    /* Drive the Cocoa runloop briefly so the activation message lands. */
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
}

// Pull the raw NSWindow out of an SDL_Window via SDL_SysWMinfo and tell
// it directly to become the key window. This bypasses NSApp activation
// entirely — useful on macOS Sonoma+ where activateIgnoringOtherApps:
// is restricted but per-window key changes still go through.
//
// Returns 0 on success, -1 if we couldn't get the NSWindow.
int macos_make_window_key(void *sdl_window_ptr)
{
    SDL_Window *win = (SDL_Window *)sdl_window_ptr;
    if (!win) return -1;

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info)) return -1;
    if (info.subsystem != SDL_SYSWM_COCOA)  return -1;

    NSWindow *nswin = info.info.cocoa.window;
    if (!nswin) return -1;

    if (getenv("RAPTOR_FOCUS_TRACE")) {
        fprintf(stdout,
            "[focus] NSWindow canBecomeKey=%d canBecomeMain=%d isVisible=%d\n",
            [nswin canBecomeKeyWindow],
            [nswin canBecomeMainWindow],
            [nswin isVisible]);
        fflush(stdout);
    }

    [nswin makeKeyAndOrderFront:nil];
    [nswin makeMainWindow];
    /* Drive the Cocoa runloop briefly so the makeKey notification is
     * delivered, not just queued. */
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];

    if (getenv("RAPTOR_FOCUS_TRACE")) {
        fprintf(stdout,
            "[focus] after makeKey: isKeyWindow=%d isMainWindow=%d "
            "NSApp isActive=%d isRunning=%d\n",
            [nswin isKeyWindow], [nswin isMainWindow],
            [NSApp isActive], [NSApp isRunning]);
        fflush(stdout);
    }
    return 0;
}
