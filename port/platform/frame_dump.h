// port/platform/frame_dump.h
//
// Debug screenshot tool. Writes the current 320x200 indexed displaybuffer
// through the active palette LUT to a 24-bit BMP under a configurable
// dump directory.
//
// Triggers (all controlled by env, no recompile needed):
//   RAPTOR_DUMP_DIR=path   - output directory (default "dumps")
//   RAPTOR_DUMP_EVERY=N    - auto-dump every N gfx_sdl_present calls
//   RAPTOR_DUMP_KEY=1      - dump on F12 keypress
//
// Manual dumps via raptor_dump_frame("label") work regardless of env vars.

#ifndef RAPTOR_FRAME_DUMP_H
#define RAPTOR_FRAME_DUMP_H

void raptor_dump_init(void);

// Capture the current displaybuffer + palette and write it as
// dumps/NNNNN_<label>.bmp. NULL/empty label becomes "frame".
void raptor_dump_frame(const char *label);

// Called once per gfx_sdl_present; periodic auto-dump dispatcher.
void raptor_dump_on_present(void);

// Called from the SDL key handler when F12 is pressed.
void raptor_dump_keypress(void);

#endif
