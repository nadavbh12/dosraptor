// port/asm_c/ptr_blit.c
//
// C rewrites of PTRAPI_A.ASM's cursor save-under primitives. Globals
// (cursorstart, displaypic, cursorsave, cursorloopx, cursorloopy) are
// set up by PTR_FrameHook in GFX/PTRAPI.C before each call.

#include "types.h"
#include <string.h>

#define SCREENWIDTH  320
#define CURSORWIDTH  16
#define CURSORHEIGHT 16

extern BYTE *cursorstart;
extern BYTE *cursorsave;
extern BYTE *displaypic;
extern INT   cursorloopx;
extern INT   cursorloopy;

void PTR_Save(void)
{
    BYTE *src = cursorstart;
    BYTE *dst = cursorsave;
    for (INT y = 0; y < CURSORHEIGHT; y++) {
        memcpy(dst, src, CURSORWIDTH);
        src += SCREENWIDTH;
        dst += CURSORWIDTH;
    }
}

void PTR_ClipSave(void)
{
    BYTE *src = cursorstart;
    BYTE *dst = cursorsave;
    for (INT y = 0; y < cursorloopy; y++) {
        memcpy(dst, src, (size_t)cursorloopx);
        src += SCREENWIDTH;
        dst += CURSORWIDTH;
    }
}

void PTR_Erase(void)
{
    BYTE *src = cursorsave;
    BYTE *dst = cursorstart;
    for (INT y = 0; y < CURSORHEIGHT; y++) {
        memcpy(dst, src, CURSORWIDTH);
        src += CURSORWIDTH;
        dst += SCREENWIDTH;
    }
}

void PTR_ClipErase(void)
{
    BYTE *src = cursorsave;
    BYTE *dst = cursorstart;
    for (INT y = 0; y < cursorloopy; y++) {
        memcpy(dst, src, (size_t)cursorloopx);
        src += CURSORWIDTH;
        dst += SCREENWIDTH;
    }
}

void PTR_Draw(void)
{
    BYTE *src = displaypic;
    BYTE *dst = cursorstart;
    for (INT y = 0; y < cursorloopy; y++) {
        for (INT x = 0; x < cursorloopx; x++) {
            BYTE c = src[x];
            if (c) dst[x] = c;
        }
        src += CURSORWIDTH;
        dst += SCREENWIDTH;
    }
}

// PTR_ReadJoyStick was the gameport timing loop on DOS. We feed
// joy_x/joy_y/joy_buttons from ptr_sdl.c instead, so this is a no-op.
void PTR_ReadJoyStick(void) {}
