// port/asm_c/tile_blit.c
//
// C rewrites of SOURCE/TILE_A.ASM. The 32x32 tile blitter is the level
// scroll engine's hot path; per-frame draw of ~10x6 tiles per screen.

#include "types.h"
#include "../platform/gfx_sdl.h"
#include <string.h>

#define SCREENWIDTH  320
#define SCREENHEIGHT 200
#define TILESIZE     32

extern BYTE *displaybuffer;
extern BYTE *displayscreen;
extern BYTE *tilepic;
extern BYTE *tilestart;
extern INT   tileloopy;

// 32 wide, full 32 rows. Caller (TILE_Put) sets up tilestart, tilepic.
void TILE_Draw(void)
{
    BYTE *src = tilepic;
    BYTE *dst = tilestart;
    for (INT y = 0; y < TILESIZE; y++) {
        memcpy(dst, src, TILESIZE);
        src += TILESIZE;
        dst += SCREENWIDTH;
    }
}

// 32 wide, tileloopy rows (clipped at top or bottom of screen).
void TILE_ClipDraw(void)
{
    BYTE *src = tilepic;
    BYTE *dst = tilestart;
    for (INT y = 0; y < tileloopy; y++) {
        memcpy(dst, src, TILESIZE);
        src += TILESIZE;
        dst += SCREENWIDTH;
    }
}

/* MAP_LEFT is the x offset of the playfield within the 320-wide screen.
 * Defined in SOURCE/MAP.H as 16. The HUD lives in [0..MAP_LEFT) on the
 * left and [MAP_LEFT+288..320) on the right. */
#define MAP_LEFT 16

extern INT g_mapleft;

// In DOS this only copied the playfield strip (288 wide) from displaybuffer
// to displayscreen+VRAM. We do a full present plus a buffer-wide memcpy —
// presenting copies the whole screen anyway, and the wider memcpy keeps
// HUD pixels in sync without a separate path.
void TILE_DisplayScreen(void)
{
    gfx_sdl_present(displaybuffer);
    if (displayscreen) memcpy(displayscreen, displaybuffer, 64000);
}

// Screen shake on heavy explosions / level fades. The original blits a
// 296-wide playfield strip from displaybuffer (at MAP_LEFT-4) to
// displayscreen at (g_mapleft-4) — `g_mapleft = MAP_LEFT + shakes[i]`,
// where shakes[] is a small horizontal-jitter table set in RAP.C. The
// 4-pixel padding on each side gives room for the shake offset.
//
// Doing it pixel-equivalent on our buffer:
//   - HUD strips (left of MAP_LEFT-4 and right of MAP_LEFT-4+296) come
//     straight from displaybuffer → displayscreen
//   - Playfield slides horizontally based on g_mapleft - MAP_LEFT
void TILE_ShakeScreen(void)
{
    if (!displaybuffer) return;

    if (displayscreen) {
        const int strip_w   = 296;
        const int src_xstart = MAP_LEFT - 4;          /* 12 */
        const int dst_xstart = g_mapleft - 4;         /* shaken */
        for (int y = 0; y < 200; y++) {
            BYTE *src_row = displaybuffer + y * 320;
            BYTE *dst_row = displayscreen + y * 320;
            /* Copy the HUD/border first — straight 1:1 — so any shake
             * offset only displaces the playfield. */
            memcpy(dst_row, src_row, 320);
            /* Then overlay the playfield with the horizontal shake. */
            int sx = src_xstart;
            int dx = dst_xstart;
            int w  = strip_w;
            if (dx < 0)         { sx -= dx; w += dx; dx = 0; }
            if (dx + w > 320)   { w = 320 - dx; }
            if (w > 0)
                memcpy(dst_row + dx, src_row + sx, (size_t)w);
        }
    }

    gfx_sdl_present(displayscreen ? displayscreen : displaybuffer);
}
