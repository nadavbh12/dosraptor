// port/platform/dpmi_stub.c
//
// Replaces GFX/DPMIAPI.C. The original allocated DOS conventional memory
// via int 0x31 and handed back the real-mode segment so the caller could
// compute a flat pointer with `(BYTE *)(segment << 4)`.
//
// On macOS arm64 we cannot reliably get a low-32-bit mapping, so the
// segment-shift contract can't be honored. Instead, GFX_InitSystem,
// PTR_Init, and RAP_InitMem have been patched to call aligned_alloc /
// calloc directly. _dpmi_dosalloc remains as a guard: any unpatched
// caller fails loudly rather than silently truncating a 64-bit pointer.

#include "DPMIAPI.H"

#include <stdint.h>
#include <stdio.h>

int _dpmi_dosalloc(unsigned short size_paragraphs, unsigned int *segment)
{
    (void)size_paragraphs; (void)segment;
    fprintf(stderr,
            "dpmi_stub: _dpmi_dosalloc was called — every known caller "
            "should have been patched to use aligned_alloc directly\n");
    return -1;
}

int _dpmi_lockregion  (void *a, unsigned len) { (void)a; (void)len; return 0; }
int _dpmi_unlockregion(void *a, unsigned len) { (void)a; (void)len; return 0; }

int _dpmi_getmemsize(void)
{
    // RAP_InitMem allocates `g_highmem = calloc(memsize, 1)` from this.
    // Original cap is MAX_HMEM (4 MB). We hand back something larger so
    // the cap-then-cap-down branch in RAP_InitMem clamps cleanly.
    return 16 * 1024 * 1024;
}
