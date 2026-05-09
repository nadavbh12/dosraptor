// port/platform/kbd_sdl.c
//
// Bridges SDL keyboard events into the legacy keyboard[256], lastscan,
// lastascii, kbd_ack, paused, capslock globals (declared in GFX/KBDAPI.C).
// The contract is the same one KBD_ReadScan kept under DOS, just driven
// off SDL events from the main thread instead of an INT 9 ISR.

#include <SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// ---- Legacy globals from GFX/KBDAPI.C ---------------------------------
extern int  keyboard[256];   // BOOL[] but storage is INT
extern int  lastscan;
extern int  lastascii;
extern int  kbd_ack;
extern int  paused;
extern int  capslock;
extern int  ASCIINames[];    // 128 entries
extern int  ShiftNames[];    // 128 entries (not exported but defined PRIVATE; we'd need to re-derive shift mapping if needed)

#define KBD_ISCAPS  (capslock != 0)

// SDL_Scancode → IBM XT scancode (set 1, the codes the .C files use via
// keys.h: SC_ESC=1, SC_A=0x1E, etc.). 0 means "no XT mapping" — events
// for that key are ignored. Filled at init time so the table size doesn't
// pre-bake SDL_NUM_SCANCODES (which is implementation-defined).
static unsigned char g_sdl_to_xt[SDL_NUM_SCANCODES];

// Bitmap of "shift currently held" so we pick the right ASCII column.
static int g_shift_held;

static void install_mapping(SDL_Scancode sdl, unsigned char xt)
{
    if ((unsigned)sdl < SDL_NUM_SCANCODES) g_sdl_to_xt[sdl] = xt;
}

// Public lookup so the playthrough harness can translate SDL key names
// directly into the legacy XT scancodes the .C files expect.
unsigned char kbd_sdl_xt_for_scancode(SDL_Scancode sdl)
{
    if ((unsigned)sdl >= SDL_NUM_SCANCODES) return 0;
    return g_sdl_to_xt[sdl];
}

void kbd_sdl_init(void)
{
    // Top row
    install_mapping(SDL_SCANCODE_ESCAPE,        0x01);
    install_mapping(SDL_SCANCODE_1,             0x02);
    install_mapping(SDL_SCANCODE_2,             0x03);
    install_mapping(SDL_SCANCODE_3,             0x04);
    install_mapping(SDL_SCANCODE_4,             0x05);
    install_mapping(SDL_SCANCODE_5,             0x06);
    install_mapping(SDL_SCANCODE_6,             0x07);
    install_mapping(SDL_SCANCODE_7,             0x08);
    install_mapping(SDL_SCANCODE_8,             0x09);
    install_mapping(SDL_SCANCODE_9,             0x0A);
    install_mapping(SDL_SCANCODE_0,             0x0B);
    install_mapping(SDL_SCANCODE_MINUS,         0x0C);
    install_mapping(SDL_SCANCODE_EQUALS,        0x0D);
    install_mapping(SDL_SCANCODE_BACKSPACE,     0x0E);
    install_mapping(SDL_SCANCODE_TAB,           0x0F);
    // QWERTY row
    install_mapping(SDL_SCANCODE_Q,             0x10);
    install_mapping(SDL_SCANCODE_W,             0x11);
    install_mapping(SDL_SCANCODE_E,             0x12);
    install_mapping(SDL_SCANCODE_R,             0x13);
    install_mapping(SDL_SCANCODE_T,             0x14);
    install_mapping(SDL_SCANCODE_Y,             0x15);
    install_mapping(SDL_SCANCODE_U,             0x16);
    install_mapping(SDL_SCANCODE_I,             0x17);
    install_mapping(SDL_SCANCODE_O,             0x18);
    install_mapping(SDL_SCANCODE_P,             0x19);
    install_mapping(SDL_SCANCODE_LEFTBRACKET,   0x1A);
    install_mapping(SDL_SCANCODE_RIGHTBRACKET,  0x1B);
    install_mapping(SDL_SCANCODE_RETURN,        0x1C);
    install_mapping(SDL_SCANCODE_LCTRL,         0x1D);
    // ASDF row
    install_mapping(SDL_SCANCODE_A,             0x1E);
    install_mapping(SDL_SCANCODE_S,             0x1F);
    install_mapping(SDL_SCANCODE_D,             0x20);
    install_mapping(SDL_SCANCODE_F,             0x21);
    install_mapping(SDL_SCANCODE_G,             0x22);
    install_mapping(SDL_SCANCODE_H,             0x23);
    install_mapping(SDL_SCANCODE_J,             0x24);
    install_mapping(SDL_SCANCODE_K,             0x25);
    install_mapping(SDL_SCANCODE_L,             0x26);
    install_mapping(SDL_SCANCODE_SEMICOLON,     0x27);
    install_mapping(SDL_SCANCODE_APOSTROPHE,    0x28);
    install_mapping(SDL_SCANCODE_GRAVE,         0x29);
    install_mapping(SDL_SCANCODE_LSHIFT,        0x2A);
    install_mapping(SDL_SCANCODE_BACKSLASH,     0x2B);
    // ZXCV row
    install_mapping(SDL_SCANCODE_Z,             0x2C);
    install_mapping(SDL_SCANCODE_X,             0x2D);
    install_mapping(SDL_SCANCODE_C,             0x2E);
    install_mapping(SDL_SCANCODE_V,             0x2F);
    install_mapping(SDL_SCANCODE_B,             0x30);
    install_mapping(SDL_SCANCODE_N,             0x31);
    install_mapping(SDL_SCANCODE_M,             0x32);
    install_mapping(SDL_SCANCODE_COMMA,         0x33);
    install_mapping(SDL_SCANCODE_PERIOD,        0x34);
    install_mapping(SDL_SCANCODE_SLASH,         0x35);
    install_mapping(SDL_SCANCODE_RSHIFT,        0x36);
    install_mapping(SDL_SCANCODE_KP_MULTIPLY,   0x37);
    install_mapping(SDL_SCANCODE_LALT,          0x38);
    install_mapping(SDL_SCANCODE_SPACE,         0x39);
    install_mapping(SDL_SCANCODE_CAPSLOCK,      0x3A);
    // Function keys
    install_mapping(SDL_SCANCODE_F1,            0x3B);
    install_mapping(SDL_SCANCODE_F2,            0x3C);
    install_mapping(SDL_SCANCODE_F3,            0x3D);
    install_mapping(SDL_SCANCODE_F4,            0x3E);
    install_mapping(SDL_SCANCODE_F5,            0x3F);
    install_mapping(SDL_SCANCODE_F6,            0x40);
    install_mapping(SDL_SCANCODE_F7,            0x41);
    install_mapping(SDL_SCANCODE_F8,            0x42);
    install_mapping(SDL_SCANCODE_F9,            0x43);
    install_mapping(SDL_SCANCODE_F10,           0x44);
    install_mapping(SDL_SCANCODE_NUMLOCKCLEAR,  0x45);
    install_mapping(SDL_SCANCODE_SCROLLLOCK,    0x46);
    install_mapping(SDL_SCANCODE_F11,           0x57);
    install_mapping(SDL_SCANCODE_F12,           0x58);
    // Arrow keys & navigation — extended scancodes in the original would
    // be 0xE0xx, but the legacy code stores them as 8-bit indices into
    // keyboard[256] using the low byte. Map to standard cursor pad codes.
    install_mapping(SDL_SCANCODE_UP,            0x48);
    install_mapping(SDL_SCANCODE_DOWN,          0x50);
    install_mapping(SDL_SCANCODE_LEFT,          0x4B);
    install_mapping(SDL_SCANCODE_RIGHT,         0x4D);
    install_mapping(SDL_SCANCODE_HOME,          0x47);
    install_mapping(SDL_SCANCODE_END,           0x4F);
    install_mapping(SDL_SCANCODE_PAGEUP,        0x49);
    install_mapping(SDL_SCANCODE_PAGEDOWN,      0x51);
    install_mapping(SDL_SCANCODE_INSERT,        0x52);
    install_mapping(SDL_SCANCODE_DELETE,        0x53);
    install_mapping(SDL_SCANCODE_RCTRL,         0x1D);
    install_mapping(SDL_SCANCODE_RALT,          0x38);
    install_mapping(SDL_SCANCODE_KP_ENTER,      0x1C);
}

// Forwarded from gfx_sdl.c's event pump.
void kbd_sdl_handle_keydown(SDL_Keysym ks)
{
    SDL_Scancode sdl = ks.scancode;
    if ((unsigned)sdl >= SDL_NUM_SCANCODES) return;
    unsigned char xt = g_sdl_to_xt[sdl];
    // Diagnostic: prints every keydown the SDL window actually receives.
    // Lets us tell "events arrive but legacy code ignores them" apart
    // from "window has no keyboard focus, no events arriving."
    if (getenv("RAPTOR_KBD_TRACE")) {
        fprintf(stdout, "[kbd] sdl=%d xt=0x%02x sym=%d name='%s'\n",
                (int)sdl, (unsigned)xt, (int)ks.sym, SDL_GetKeyName(ks.sym));
        fflush(stdout);
    }
    if (xt == 0) return;

    if (sdl == SDL_SCANCODE_LSHIFT || sdl == SDL_SCANCODE_RSHIFT)
        g_shift_held = 1;
    if (sdl == SDL_SCANCODE_CAPSLOCK)
        capslock = !capslock;

    keyboard[xt] = 1;
    lastscan     = xt;
    kbd_ack      = 1;
    if (xt < 128) {
        // ASCIINames covers 0..127. Shift state would normally pull from
        // ShiftNames[] which is PRIVATE in KBDAPI.C; for M4 we use
        // ASCIINames and let SDL_TEXTINPUT handle real text entry later.
        lastascii = ASCIINames[xt];
    }
}

void kbd_sdl_handle_keyup(SDL_Keysym ks)
{
    SDL_Scancode sdl = ks.scancode;
    if ((unsigned)sdl >= SDL_NUM_SCANCODES) return;
    unsigned char xt = g_sdl_to_xt[sdl];
    if (xt == 0) return;

    if (sdl == SDL_SCANCODE_LSHIFT || sdl == SDL_SCANCODE_RSHIFT)
        g_shift_held = 0;

    keyboard[xt] = 0;
}
