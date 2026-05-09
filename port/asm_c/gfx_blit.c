// port/asm_c/gfx_blit.c
//
// C rewrites of the 9 routines originally in GFX/GFXAPI_A.ASM.

#include "types.h"
#include "../platform/gfx_sdl.h"
#include <string.h>

#define SCREENWIDTH  320
#define SCREENHEIGHT 200

extern BYTE *displaybuffer;
extern BYTE *displayscreen;
extern DWORD ylookup[SCREENHEIGHT];
extern BYTE *gfx_inmem;
extern INT   gfx_xp;
extern INT   gfx_yp;
extern INT   gfx_lx;
extern INT   gfx_ly;
extern INT   gfx_imga;

// ---- GFX_PutPic / GFX_PutMaskPic ---------------------------------------
// gfx_imga convention is set by the caller (GFX_PutImage, tile blitter):
//   PutPic     : gfx_imga = source_width - lx (row stride to skip)
//   PutMaskPic : gfx_imga = source_width      (full row stride)

void GFX_PutPic(void)
{
    BYTE *src = gfx_inmem;
    BYTE *dst = displaybuffer + gfx_xp + ylookup[gfx_yp];
    const INT row_advance_src = gfx_lx + gfx_imga;
    for (INT y = 0; y < gfx_ly; y++) {
        memcpy(dst, src, (size_t)gfx_lx);
        src += row_advance_src;
        dst += SCREENWIDTH;
    }
}

void GFX_PutMaskPic(void)
{
    BYTE *src = gfx_inmem;
    BYTE *dst = displaybuffer + gfx_xp + ylookup[gfx_yp];
    for (INT y = 0; y < gfx_ly; y++) {
        for (INT x = 0; x < gfx_lx; x++) {
            BYTE c = src[x];
            if (c) dst[x] = c;
        }
        src += gfx_imga;
        dst += SCREENWIDTH;
    }
}

// ---- GFX_DisplayScreen -------------------------------------------------
void GFX_DisplayScreen(void)
{
    gfx_sdl_present(displaybuffer);
    if (displayscreen) memcpy(displayscreen, displaybuffer, 64000);
}

// ---- GFX_DrawChar ------------------------------------------------------
// Caller (GFX_PutChar) sets up: dest at (x,y), cdata at the glyph bitmap
// row data, lx/ly = clipped width/height, addx = font_glyph_width - lx
// (i.e. row stride to skip after each lx-byte row), color = base color
// to colorize non-zero glyph pixels.
void GFX_DrawChar(BYTE *dest, BYTE *cdata, INT lx, INT ly,
                  INT addx, INT color)
{
    const INT row_advance_src = lx + addx;
    for (INT y = 0; y < ly; y++) {
        for (INT x = 0; x < lx; x++) {
            BYTE c = cdata[x];
            if (c) dest[x] = (BYTE)(c + color);
        }
        cdata += row_advance_src;
        dest  += SCREENWIDTH;
    }
}

// ---- GFX_DrawSprite ----------------------------------------------------
// inmem points just past the GFX_PIC header at a sequence of records
// terminated by offset == EMPTY (~0). Each record (16 bytes):
//   INT x, INT y, INT offset, INT length
// then `length` bytes of pixel data. Records can be back-to-back at any
// byte alignment because variable-length payloads break 4-byte alignment;
// reads must use memcpy to avoid SIGBUS on ARM64.
#define SPRITE_REC_SIZE 16
static inline INT rec_int(const BYTE *p, int field) {
    INT v;
    memcpy(&v, p + field * 4, sizeof(INT));
    return v;
}

void GFX_DrawSprite(BYTE *dest, BYTE *inmem)
{
    if (!dest || !inmem) return;
    for (int iter = 0; iter < 4096; iter++) {
        INT rec_x      = rec_int(inmem, 0);
        INT rec_y      = rec_int(inmem, 1);
        INT rec_offset = rec_int(inmem, 2);
        INT rec_length = rec_int(inmem, 3);
        if ((unsigned)rec_offset == ~0u) break;
        if (rec_length < 0 || rec_length > SCREENWIDTH) break;
        inmem += SPRITE_REC_SIZE;
        memcpy(dest + rec_x + rec_y * SCREENWIDTH, inmem, (size_t)rec_length);
        inmem += rec_length;
    }
}

// ---- GFX_ShadeSprite ---------------------------------------------------
// Same record shape as GFX_DrawSprite, but each pixel-position in dest
// goes through a shade table (light/dark/grey) instead of being copied
// from inmem — that's how the original .ASM Light/Dark/Shadow blends
// translucent sprite shapes onto the existing scene.
void GFX_ShadeSprite(BYTE *dest, BYTE *inmem, BYTE *table)
{
    if (!dest || !inmem || !table) return;
    for (int iter = 0; iter < 4096; iter++) {
        INT rec_x      = rec_int(inmem, 0);
        INT rec_y      = rec_int(inmem, 1);
        INT rec_offset = rec_int(inmem, 2);
        INT rec_length = rec_int(inmem, 3);
        if ((unsigned)rec_offset == ~0u) break;
        if (rec_length < 0 || rec_length > SCREENWIDTH) break;
        inmem += SPRITE_REC_SIZE;
        BYTE *out = dest + rec_x + rec_y * SCREENWIDTH;
        for (INT i = 0; i < rec_length; i++) out[i] = table[out[i]];
        inmem += rec_length;
    }
}

// ---- GFX_Shade ---------------------------------------------------------
// Remap a horizontal run of `len` bytes through `table`.
void GFX_Shade(BYTE *outmem, INT len, BYTE *table)
{
    for (INT i = 0; i < len; i++) outmem[i] = table[outmem[i]];
}

// ---- GFX_ScaleLine / GFX_CScaleLine ------------------------------------
// Single-line sprite scaling. The caller (GFX_DrawScalePic etc.) fills
// stable[0..tablelen-1] with source-pixel offsets and inmem points at
// the source row; we write tablelen pixels into outmem. CScaleLine
// skips source pixel value 0 (transparent).
//
// The original asm walked the table backwards in an unrolled loop;
// pixel writes are independent so a forward loop is equivalent. Used
// by explosion/flame FX in flight gameplay.
extern INT  stable[];
extern INT  tablelen;

void GFX_ScaleLine(BYTE *outmem, BYTE *inmem)
{
    if (!outmem || !inmem) return;
    const INT n = tablelen;
    for (INT i = 0; i < n; i++) outmem[i] = inmem[stable[i]];
}

void GFX_CScaleLine(BYTE *outmem, BYTE *inmem)
{
    if (!outmem || !inmem) return;
    const INT n = tablelen;
    for (INT i = 0; i < n; i++) {
        BYTE c = inmem[stable[i]];
        if (c) outmem[i] = c;
    }
}
