// port/platform/ptr_sdl.c
//
// Bridges SDL mouse + gamepad state into the legacy cursor/joystick
// globals in GFX/PTRAPI.C. Called once per gfx_sdl_present() so it runs
// on the main thread alongside everything else.
//
// SDL_GetMouseState returns window-space coordinates; the game expects
// 320x200 logical-space coords. SDL_RenderWindowToLogical handles the
// scaling.

#include <SDL.h>

extern int cur_mx, cur_my;
extern int mouseb1, mouseb2, mouseb3;
extern int mousepresent;

extern int joy_x, joy_y, joy_buttons;
extern int joypresent;
extern int joy2b1, joy2b2;

static SDL_GameController *g_pad;

void ptr_sdl_init(void)
{
    // Mouse is always available on a desktop. Mark it present so the
    // game's PTR_DrawCursor( TRUE ) path actually draws a cursor.
    mousepresent = 1;

    // Open the first available game controller, if any.
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) {
                joypresent = 1;
                break;
            }
        }
    }
}

void ptr_sdl_shutdown(void)
{
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = NULL; }
}

extern void *gfx_sdl_window_handle(void);

// Called from gfx_sdl_present every frame.
void ptr_sdl_poll(SDL_Renderer *renderer)
{
    /* If the SDL window doesn't have input focus, SDL_GetMouseState returns
     * stale or zero coords — overwriting cur_mx/cur_my with that would
     * clamp the cursor to the top-left and stomp any test/playthrough
     * harness that wrote synthetic mouse coords. Skip the update entirely
     * unless we have focus. */
    SDL_Window *win = (SDL_Window *)gfx_sdl_window_handle();
    if (win && !(SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS)) {
        return;
    }

    int wx, wy;
    Uint32 buttons = SDL_GetMouseState(&wx, &wy);

    // Window space -> 320x200 logical.
    if (renderer) {
        float lx = 0, ly = 0;
        SDL_RenderWindowToLogical(renderer, wx, wy, &lx, &ly);
        cur_mx = (int)lx;
        cur_my = (int)ly;
    } else {
        cur_mx = wx;
        cur_my = wy;
    }
    if (cur_mx < 0)   cur_mx = 0;
    if (cur_mx > 319) cur_mx = 319;
    if (cur_my < 0)   cur_my = 0;
    if (cur_my > 199) cur_my = 199;

    mouseb1 = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT))   ? 1 : 0;
    mouseb2 = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT))  ? 1 : 0;
    mouseb3 = (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? 1 : 0;

    /* PORT: in DOS, PTR_MouseHandler (the INT 33h callback) set
     * mouseaction=TRUE on every mouse event, which is what made
     * PTR_FrameHook redraw the cursor. We don't have that callback,
     * so the cursor only ever drew once after PTR_DrawCursor(TRUE)
     * and then froze. Bump mouseaction here every frame so the legacy
     * code keeps redrawing. */
    extern int mouseaction;
    mouseaction = 1;

    if (g_pad) {
        // Left stick -> joy_x/y in 0..255 (centered at 128). The legacy
        // PTR_ReadJoyStick reads raw timer-charge counts, so the 0..255
        // range is what most callers expect after PTR_CalJoy.
        Sint16 ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ay = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
        joy_x = 128 + (ax >> 8);
        joy_y = 128 + (ay >> 8);
        joy_buttons =
            (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A) ? 0x01 : 0) |
            (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B) ? 0x02 : 0) |
            (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X) ? 0x04 : 0) |
            (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y) ? 0x08 : 0);
        joy2b1 = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)  ? 1 : 0;
        joy2b2 = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 1 : 0;
    }
}
