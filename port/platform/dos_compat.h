// port/platform/dos_compat.h
//
// Global -include shim for the 1992 Watcom/DOS source. Force-included by
// CMake before every legacy translation unit so none of SOURCE or GFX
// has to be touched to swallow:
//   Watcom attributes:  near far huge cdecl pascal _interrupt _far _loadds
//   DOS headers:        <dos.h> <conio.h> <i86.h> <io.h>
//   DOS intrinsics:     int386/int386x, _disable/_enable, inp/outp,
//                       _dos_getvect/_dos_setvect, _dos_getdate, segread,
//                       FP_OFF/FP_SEG/MK_FP
//
// The DPMI surface (_dpmi_dosalloc, _dpmi_lockregion, ...) is declared by
// GFX/dpmiapi.h and implemented by port/platform/dpmi_stub.c, not here.

#ifndef RAPTOR_DOS_COMPAT_H
#define RAPTOR_DOS_COMPAT_H

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- swallow vendored DOS headers ------------------------------------- *
 * These translate to nothing; legacy sources still write `#include <dos.h>`
 * at the top of every file. */
#define _DOS_H_INCLUDED
#define _CONIO_H_INCLUDED
#define _IO_H_INCLUDED

/* clang's preprocessor visits #include before macro expansion, so a bare
 * #define won't actually intercept <dos.h>. We instead rely on
 * `-Iport/platform/dos_shim` which holds empty headers of the same name —
 * see port/platform/dos_shim/. The macro guards above are belt-and-braces
 * for any build that still finds a real <dos.h>. */

/* ---- Watcom storage-class / calling-conv keywords --------------------- */
#define near
#define far
#define huge
#define cdecl
#define pascal
#define _interrupt
#define interrupt
#define _far
#define _near
#define _huge
#define _loadds
#define _saveregs
#define _export

/* ---- ISR guards -------------------------------------------------------- *
 * The port main loop is single-threaded; no ISRs. */
#define _disable() ((void)0)
#define _enable()  ((void)0)

/* ---- Port I/O --------------------------------------------------------- *
 * VGA palette / PIC-EOI / mouse-port writes become no-ops. inp() returns
 * a benign value so the retrace-poll loops in GFX_SetPalette don't spin. */
static inline unsigned char inp(unsigned short port)  { (void)port; return 0; }
static inline unsigned char outp(unsigned short port, unsigned char v) {
    (void)port; (void)v; return v;
}
#define inportb(p)     inp(p)
#define outportb(p,v)  outp((p),(v))

/* ---- BIOS regs structures --------------------------------------------- *
 * Field names match Watcom's <i86.h>. Storage is non-aliasing, but every
 * caller writes/reads through one accessor at a time and our int386 stub
 * doesn't observe the contents, so faithful aliasing is unnecessary. */
struct _RAPTOR_WORDREGS  { unsigned short ax, bx, cx, dx, si, di, cflag, flags; };
struct _RAPTOR_BYTEREGS  { unsigned char  al, ah, bl, bh, cl, ch, dl, dh; };
struct _RAPTOR_DWORDREGS { unsigned int   eax, ebx, ecx, edx, esi, edi, cflag, flags; };
union REGS {
    struct _RAPTOR_WORDREGS  w;
    struct _RAPTOR_BYTEREGS  h;
    struct _RAPTOR_DWORDREGS x;
};
struct SREGS { unsigned short es, cs, ss, ds, fs, gs; };

static inline void segread(struct SREGS *s) {
    if (s) memset(s, 0, sizeof *s);
}

static inline int int386(int n, const union REGS *in, union REGS *out) {
    if (out && in && out != in) *out = *in;
    /* INT 0x33 / AX=0 — mouse driver presence check. Returning AX=0xFFFF
     * tells PTR_Init "mouse is present"; otherwise it stays disabled and
     * the in-game cursor sprite never renders.
     * AX=2 is "hide mouse" — DOS hardware show/hide is a no-op for us;
     * cur_mx/cur_my are filled by ptr_sdl_poll instead.
     * AX=0x0C is "install mouse handler" — also a no-op; we don't fake
     * the DOS interrupt callback.
     * The earlier deadlock in WIN_MainMenu was actually
     * `while (!(volatile)not_in_update)` — a bogus cast that let clang
     * fold the loop. Fixed by declaring not_in_update volatile. */
    if (n == 0x33 && in && out) {
        switch (in->w.ax) {
        case 0x0000: out->w.ax = 0xFFFFu; break;
        case 0x0004: {
            /* Set mouse position. ECX is virtual-x*2 (DOS quirk —
             * 320x200 mode reports x in the 0..639 range), EDX is y.
             * Without this, PTR_SetPos's `cur_mx = x` is immediately
             * overwritten by ptr_sdl_poll on the next pump, so keyboard
             * menu navigation can't move the cursor onto the next
             * button. */
            extern void gfx_sdl_warp_mouse(int x, int y);
            int x = (int)(in->x.ecx >> 1);
            int y = (int)in->x.edx;
            gfx_sdl_warp_mouse(x, y);
            break;
        }
        default:     break;
        }
    }
    return 0;
}
static inline int int386x(int n, const union REGS *in, union REGS *out, struct SREGS *s) {
    (void)s;
    return int386(n, in, out);
}

#define FP_OFF(p)   ((unsigned int)(uintptr_t)(p))
#define FP_SEG(p)   (0u)
#define MK_FP(s, o) ((void *)(uintptr_t)(o))

/* ---- DOS interrupt vector hooking (INT 0x9, INT 0x33) ----------------- *
 * Used only to install/uninstall the keyboard and mouse ISRs in DOS. The
 * SDL event pump replaces both, so getvect/setvect are no-ops returning
 * NULL. KBDAPI.C stores the returned "old vector" so the type need not
 * match exactly — -Wno-incompatible-pointer-types covers it. */
typedef void (*_dos_intr_handler_t)(void);
static inline _dos_intr_handler_t _dos_getvect(unsigned int n) { (void)n; return 0; }
static inline void _dos_setvect(unsigned int n, _dos_intr_handler_t f) {
    (void)n; (void)f;
}
#define _chain_intr(h) ((void)(h))

/* ---- _dos_getdate ----------------------------------------------------- *
 * RAP.C reads system date for save-game timestamps and for the Bday
 * easter-egg trigger. localtime_r hands back the same fields. */
struct dosdate_t {
    unsigned char  day;
    unsigned char  month;
    unsigned short year;
    unsigned char  dayofweek;   /* 0=Sunday */
};
static inline void _dos_getdate(struct dosdate_t *d) {
    if (!d) return;
    time_t t = time(NULL);
    struct tm tm; localtime_r(&t, &tm);
    d->day       = (unsigned char)tm.tm_mday;
    d->month     = (unsigned char)(tm.tm_mon + 1);
    d->year      = (unsigned short)(tm.tm_year + 1900);
    d->dayofweek = (unsigned char)tm.tm_wday;
}

/* ---- POSIX-isms missing on Watcom side -------------------------------- */
/* Watcom <fcntl.h> defines O_BINARY for "no CR/LF translation". POSIX has
 * no text-mode munging, so 0 is the correct value. */
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_TEXT
#define O_TEXT 0
#endif

/* Watcom names for path-length constant. */
#ifndef _MAX_PATH
#define _MAX_PATH PATH_MAX
#endif
#ifndef MAXPATH
#define MAXPATH PATH_MAX
#endif

/* ---- Watcom libc helpers missing from POSIX --------------------------- *
 * Map to POSIX equivalents where they exist; roll a 1-line replacement
 * where they don't. */
#include <ctype.h>
#include <strings.h>     /* strcasecmp / strncasecmp */
#include <sys/stat.h>
#include <unistd.h>

#define stricmp  strcasecmp
#define strcmpi  strcasecmp
#define strnicmp strncasecmp

static inline char *strupr(char *s) {
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    return s;
}
static inline char *strlwr(char *s) {
    for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
    return s;
}

/* Watcom <stdlib.h> ltoa(value, buffer, base). POSIX never had it; only
 * base 10 is used by the Raptor source we care about. */
static inline char *ltoa(long v, char *buf, int base) {
    (void)base;   /* base 10 is the only caller */
    sprintf(buf, "%ld", v);
    return buf;
}

/* Watcom <io.h>: filelength(fd) returns size; chsize(fd, n) truncates. */
static inline long filelength(int fd) {
    struct stat st;
    return fstat(fd, &st) == 0 ? (long)st.st_size : -1L;
}
static inline int chsize(int fd, long size) { return ftruncate(fd, size); }

/* ---- random() --------------------------------------------------------- *
 * GFX/types.h does `#define random(x) (rand()%x)` — that still works on
 * macOS (rand() is in <stdlib.h>). Nothing to do. */

/* ---- legacy busy-wait pump -------------------------------------------- *
 * Several legacy spins (SWD_Dialog F_SELECT's `while (SWD_IsButtonDown())`,
 * KBD_Wait's `while (*ky)`, etc.) wait for a key/button to be released.
 * In DOS the keyboard ISR cleared keyboard[] independently. In the port,
 * keyboard[] only updates when SDL events are pumped, which only happens
 * inside gfx_sdl_present. An empty-body spin therefore deadlocks.
 *
 * legacy_pump() is a one-shot SDL_PollEvent drain that legacy code calls
 * inside its release-spins so the SDL_KEYUP / mouse-button-up actually
 * lands. It's a thin wrapper so we don't pull <SDL.h> into legacy TUs. */
extern void gfx_sdl_pump_events_only(void);
static inline void legacy_pump(void) { gfx_sdl_pump_events_only(); }

#endif /* RAPTOR_DOS_COMPAT_H */
