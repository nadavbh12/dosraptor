// port/platform/gfx_sdl.h
//
// SDL2 wrapper. Owns the window/renderer/texture and the indexed-to-ARGB
// palette LUT. Called from the legacy GFX layer (GFX_SetPalette redirects
// here, GFX_DisplayScreen presents through here) and from the M3 demo
// driver in sys_main.c.

#ifndef RAPTOR_GFX_SDL_H
#define RAPTOR_GFX_SDL_H

#include <stdint.h>

int  gfx_sdl_init(void);
void gfx_sdl_shutdown(void);

// Update entries [start, start+count) of the 256-entry palette LUT.
// `pal` is `count * 3` bytes of 6-bit-per-channel RGB (Watcom DAC layout);
// they're scaled to 8 bits and stored as 0xFFRRGGBB ARGB.
void gfx_sdl_set_palette(const uint8_t *pal, int start, int count);

// Upload the 320x200 indexed displaybuffer through the LUT to the
// streaming texture, present, and pump SDL events.
void gfx_sdl_present(const uint8_t *displaybuffer);

// True after SDL_QUIT or ESC was observed during a previous pump.
int  gfx_sdl_should_quit(void);

// Pump SDL events without doing a full render. Call this from legacy
// busy-waits (e.g., WIN_DemoDelay's frame-tick spin) so real keystrokes
// actually flow into kbd_sdl_handle_keydown when the menu hasn't
// presented for a while.
void gfx_sdl_pump_events_only(void);

// Opaque pointer to the SDL_Window — used by macos_activate.m to fetch
// the underlying NSWindow via SDL_SysWMinfo and force it to be key.
// Returns NULL before gfx_sdl_init runs.
void *gfx_sdl_window_handle(void);

// Read-only handle to the 256-entry ARGB8888 palette LUT. Used by the
// debug frame-dumper to materialize indexed pixels into BMP rows.
const uint32_t *gfx_sdl_palette_lut(void);

#endif
