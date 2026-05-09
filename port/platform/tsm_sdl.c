// port/platform/tsm_sdl.c
//
// Replaces GFX/TSMAPI.C. Original sat on top of Apogee TASK_MAN, which
// programmed the 8253 PIT to fire scheduled callbacks at e.g. 70 Hz.
//
// Design: framecount is incremented by an SDL_AddTimer in gfx_sdl.c
// (single int store, race-free enough given `volatile`). Other TSM
// services — PTR_UpdateCursor @ 15 Hz, IPT_GetButtons @ 26 Hz — fire
// from the main thread inside tsm_sdl_dispatch_due(), which gfx_sdl
// calls once per present. That keeps service callbacks single-threaded
// (per the M5 spec note: "services touch globals not designed for
// thread re-entry — Not SDL_AddTimer").

#include "TSMAPI.H"

#include <SDL.h>
#include <stddef.h>

typedef struct {
    void   (*callback)(void);
    int     rate_hz;
    int     priority;
    int     paused;
    int     in_use;
    Uint32  next_fire_ms;     // wall-clock deadline
} tsm_service_t;

#define TSM_MAX_SERVICES 16
static tsm_service_t g_services[TSM_MAX_SERVICES];

void TSM_Install(int rate)
{
    (void)rate;
    for (int i = 0; i < TSM_MAX_SERVICES; i++) g_services[i].in_use = 0;
}

int TSM_NewService(void (*function)(void), int rate, int priority, int pause)
{
    if (rate <= 0) rate = 1;
    for (int i = 0; i < TSM_MAX_SERVICES; i++) {
        if (!g_services[i].in_use) {
            g_services[i].callback     = function;
            g_services[i].rate_hz      = rate;
            g_services[i].priority     = priority;
            g_services[i].paused       = pause;
            g_services[i].in_use       = 1;
            g_services[i].next_fire_ms = SDL_GetTicks() + (Uint32)(1000 / rate);
            return i;
        }
    }
    return -1;
}

void TSM_DelService(int id)
{
    if (id >= 0 && id < TSM_MAX_SERVICES) g_services[id].in_use = 0;
}
void TSM_PauseService(int id)
{
    if (id >= 0 && id < TSM_MAX_SERVICES) g_services[id].paused = 1;
}
void TSM_ResumeService(int id)
{
    if (id >= 0 && id < TSM_MAX_SERVICES) g_services[id].paused = 0;
}
void TSM_Remove(void)
{
    for (int i = 0; i < TSM_MAX_SERVICES; i++) g_services[i].in_use = 0;
}

// Called from gfx_sdl_present once per frame. Fires every active service
// whose deadline has elapsed; if multiple periods passed (e.g., the user
// alt-tabbed away), the deadline jumps forward without back-firing each
// missed tick — same compromise the original PIT ISR made on overrun.
void tsm_sdl_dispatch_due(void)
{
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < TSM_MAX_SERVICES; i++) {
        tsm_service_t *s = &g_services[i];
        if (!s->in_use || s->paused) continue;
        if ((Sint32)(now - s->next_fire_ms) >= 0) {
            if (s->callback) s->callback();
            Uint32 period = (Uint32)(1000 / s->rate_hz);
            if (period == 0) period = 1;
            // Skip-on-overrun: snap deadline to the next period after now.
            s->next_fire_ms = now + period;
        }
    }
}
