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
#define PLAYFIELD_WIDTH 288  /* MAP_RIGHT - MAP_LEFT = 304 - 16 */

extern INT g_mapleft;

/* In DOS, displayscreen WAS the VRAM front buffer (0xa0000) and was the
 * compositing target: RAP_DisplayShieldLevel and other HUD code wrote
 * shield bars directly into displayscreen's side strips, while the
 * playfield (sprites, tiles, score) was composed in displaybuffer.
 * TILE_DisplayScreen blitted displaybuffer's playfield strip
 * [MAP_LEFT..MAP_LEFT+288) on top of displayscreen, leaving displayscreen's
 * HUD strips (x in [0..MAP_LEFT) and [MAP_LEFT+288..320)) intact — the
 * end result on VRAM was HUD-strips-from-displayscreen + playfield-from-
 * displaybuffer.
 *
 * In the port, displayscreen is just a host buffer; we have to present
 * the composed image explicitly. So: copy the playfield strip into
 * displayscreen the same way DOS did, then present displayscreen. This
 * preserves the shield bars that RAP_DisplayShieldLevel just wrote. */
void TILE_DisplayScreen(void)
{
    if (displayscreen && displaybuffer) {
        for (int y = 0; y < 200; y++) {
            memcpy(displayscreen + y * 320 + MAP_LEFT,
                   displaybuffer + y * 320 + MAP_LEFT,
                   PLAYFIELD_WIDTH);
        }
        gfx_sdl_present(displayscreen);
    } else {
        gfx_sdl_present(displaybuffer);
    }
}

/* Screen shake on heavy explosions / level fades. Original blits a
 * 296-wide strip from displaybuffer at (MAP_LEFT-4=12) into displayscreen
 * at (g_mapleft-4) — `g_mapleft = MAP_LEFT + shakes[i]`. The 4-pixel
 * padding on each side gives room for the shake offset; the HUD shield
 * bars at x=8 and x=308 sit OUTSIDE the 296-strip so they never get
 * stomped. */
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
            int sx = src_xstart;
            int dx = dst_xstart;
            int w  = strip_w;
            if (dx < 0)         { sx -= dx; w += dx; dx = 0; }
            if (dx + w > 320)   { w = 320 - dx; }
            if (w > 0)
                memcpy(dst_row + dx, src_row + sx, (size_t)w);
        }
        gfx_sdl_present(displayscreen);
    } else {
        gfx_sdl_present(displaybuffer);
    }
}
