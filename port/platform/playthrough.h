// port/platform/playthrough.h
//
// Script-driven SDL event injection. Lets us drive the game headlessly
// (no real keyboard, no window focus, no Accessibility permissions) and
// dump frames at named checkpoints. Combined with the multimodal frame
// viewer, this is "Playwright for Raptor."
//
// Activate with RAPTOR_PLAYTHROUGH=path/to/script.txt.
//
// Script format — one command per line, # for comments:
//
//   wait N        — skip N gfx_sdl_present calls before continuing
//   key NAME      — push SDL_KEYDOWN + SDL_KEYUP for SDL key NAME
//                   (e.g. "A", "Return", "Escape", "F1", "Left")
//   down NAME     — push SDL_KEYDOWN only (held key)
//   up   NAME     — push SDL_KEYUP only
//   dump LABEL    — raptor_dump_frame(LABEL)
//   quit          — exit(0) cleanly
//
// Names match SDL_GetScancodeFromName: any SDL key name works.

#ifndef RAPTOR_PLAYTHROUGH_H
#define RAPTOR_PLAYTHROUGH_H

void raptor_playthrough_init(void);
void raptor_playthrough_tick(void);

// Hook called from WIN_MainMenu after the menu has fully painted.
// The playthrough script doesn't start dispatching until this fires —
// otherwise commands targeting the menu can race ahead of the legacy
// code's KBD_Clear() and get wiped before SWD_Dialog reads them.
void raptor_playthrough_menu_ready(void);

#endif
