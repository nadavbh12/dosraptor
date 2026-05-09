// port/asm_c/anim_render.c
//
// C port of SOURCE/MOVIE_A.ASM's ANIM_Render. The animpic format is a
// stream of 8-byte record headers, each followed inline by `length`
// bytes of pixel data. The header layout (little-endian, 16-bit fields):
//
//   +0  WORD : terminator — 0 ends the frame, anything else continues
//   +2  WORD : (unused, padding)
//   +4  WORD : dst_offset — byte offset into displaybuffer
//   +6  WORD : length — bytes of inline pixel data to copy
//
// MOVIE.C calls this once per frame; the first byte of each frame's
// item buffer is a background fill color that MOVIE_Play handles
// before invoking us, so we always start straight at the first record.

#include "types.h"
#include <stdint.h>
#include <string.h>

#define DISPLAY_BYTES (320 * 200)

extern BYTE *displaybuffer;

void ANIM_Render(BYTE *inmem)
{
    if (!inmem || !displaybuffer) return;

    const BYTE *src = inmem;
    /* Cap iterations defensively — a malformed asset without a
     * 0-terminator could otherwise walk off the heap. 4096 records is
     * far more than any real frame ever produces. */
    for (int iter = 0; iter < 4096; iter++) {
        uint16_t term, dst_offset, length;
        memcpy(&term,       src + 0, 2);
        if (term == 0) return;
        memcpy(&dst_offset, src + 4, 2);
        memcpy(&length,     src + 6, 2);
        src += 8;

        /* Bound the copy at the displaybuffer edge — bad asset data
         * would otherwise corrupt the heap. */
        size_t end = (size_t)dst_offset + length;
        if (end > DISPLAY_BYTES) {
            if (dst_offset >= DISPLAY_BYTES) return;
            length = (uint16_t)(DISPLAY_BYTES - dst_offset);
        }

        memcpy(displaybuffer + dst_offset, src, length);
        src += length;
    }
}
