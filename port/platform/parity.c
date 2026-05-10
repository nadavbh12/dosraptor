// port/platform/parity.c
//
// Per-second parity checkpoint emitter for the Godot port's L2 tests.
// When RAPTOR_PARITY_OUT=path is set, opens that file and emits one
// JSON object per simulated second (70 frames at ~70 Hz) to it.
//
// This file is compiled as modern C11 (not gnu89) and does NOT receive
// the -include dos_compat.h injection. All legacy types are re-declared
// via explicit externs to avoid cascading include complexity.

#include "parity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// External state from the legacy game engine
// ---------------------------------------------------------------------------

// GFX/GFXAPI.C: frame counter incremented by SDL_AddTimer callback.
extern volatile int framecount;

// SOURCE/RAP.C: player screen position and current game slot (0-2).
extern int playerx;
extern int playery;
extern int cur_game;

// SOURCE/PUBLIC.H: PLAYEROBJ plr — we only need plr.score (a DWORD = unsigned int).
// Avoid pulling the full header chain; declare just what we use.
struct _parity_playerobj {
    char   name[20];
    char   callsign[12];
    int    id_pic;
    unsigned int score;   // DWORD
    // rest of fields not needed
};
extern struct _parity_playerobj plr;

// OBJECTS_GetAmt(S_ENERGY) returns shield level.
// S_ENERGY == 16 (from SOURCE/OBJECTS.H enum).
#define PARITY_S_ENERGY 16
extern int OBJS_GetAmt(int type);

// SOURCE/ENEMY.C: number of enemies currently on-screen.
extern int numships;

// SOURCE/SHOTS.C: number of player bullets currently alive.
extern int shotnum;

// SOURCE/ESHOT.C: number of enemy bullets currently alive.
extern int eshotnum;

// SOURCE/OBJECTS.C: OBJ linked-list sentinels.
// Replicate just the struct fields we traverse (prev/next/num/type).
struct _parity_obj {
    struct _parity_obj *prev;
    struct _parity_obj *next;
    int                 num;
    int                 type;
    // rest not needed
};
extern struct _parity_obj first_objs;
extern struct _parity_obj last_objs;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static FILE *g_out        = NULL;
static int   g_in_game    = 0;   // 1 while Do_Game is running
static int   g_game_num   = 0;   // 0-based game index (cur_game at entry)
static int   g_game_fc0   = 0;   // framecount at raptor_parity_game_enter
                                  // Emit relative ticks so captures from
                                  // different runs align on game time, not
                                  // absolute startup framecount.
static int   g_win_state  = 0;   // menu win-state (PARITY_WIN_* constants)
static int   g_menu_fc0   = 0;   // framecount at raptor_parity_set_win_state entry
                                  // Anchors menu-mode relative fc the same way
                                  // g_game_fc0 anchors in-game fc.
static int   g_last_emit_sec = -1; // last emitted "second" (rel_fc / 70)
                                    // Tracks crossing of 70-frame boundaries so
                                    // we don't miss emits when frame advances skip.

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static const char *win_state_name(void) {
    if (g_in_game) {
        // cur_game is 0-indexed; missions map to MISSION_1/2/3
        switch (g_game_num) {
            case 0:  return "MISSION_1";
            case 1:  return "MISSION_2";
            case 2:  return "MISSION_3";
            default: return "UNKNOWN";
        }
    }
    // Not in game — report the active menu/dialog state.
    switch (g_win_state) {
        case 1:  return "MENU";
        case 2:  return "CREDITS";
        case 3:  return "HELP";
        case 4:  return "ORDER";
        case 5:  return "HANGAR";
        case 6:  return "STORE";
        case 7:  return "BRIEFING";
        default: return "UNKNOWN";
    }
}

// FNV-1a 64-bit hash over the player object inventory list.
// Walk first_objs..last_objs, hashing each node's type+num.
// Returns 0 in Stage 2 first-cut (stub) — schema marks obj_hash advisory.
static uint64_t compute_obj_hash(void) {
    uint64_t h = UINT64_C(14695981039346656037); // FNV offset basis
    struct _parity_obj *cur;
    for (cur = first_objs.next; cur != &last_objs; cur = cur->next) {
        // Hash type
        unsigned char byte = (unsigned char)(cur->type & 0xff);
        h ^= byte;
        h *= UINT64_C(1099511628211);
        // Hash num
        byte = (unsigned char)(cur->num & 0xff);
        h ^= byte;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

// Clamp to schema bounds.
static int clamp_to_64(int v) { return v < 0 ? 0 : (v > 64 ? 64 : v); }
static int clamp_shield(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
static int clamp_px(int v) { return v < 0 ? 0 : (v > 319 ? 319 : v); }
static int clamp_py(int v) { return v < 0 ? 0 : (v > 199 ? 199 : v); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void raptor_parity_init(void) {
    const char *path = getenv("RAPTOR_PARITY_OUT");
    if (!path || !*path) return;
    g_out = fopen(path, "w");
    if (!g_out) {
        fprintf(stderr, "parity: fopen(%s) failed\n", path);
    }
}

void raptor_parity_game_enter(int game) {
    g_game_num    = game;
    g_in_game     = 1;
    g_game_fc0    = framecount;   // anchor for relative tick calculation
    g_last_emit_sec = -1;         // reset crossing tracker
}

void raptor_parity_game_exit(void) {
    g_in_game = 0;
}

void raptor_parity_set_win_state(int win) {
    if (win != 0) {
        // Entering (or transitioning between) menu/dialog contexts.
        // Re-anchor relative fc so each context's checkpoints start near zero,
        // making the fc values independent of machine startup time.
        // We reset on every non-UNKNOWN entry so that nested menus (e.g.
        // MENU → CREDITS → back to MENU) each get a fresh local timeline.
        g_menu_fc0 = framecount;
        g_last_emit_sec = -1;   // reset crossing tracker for the new context
    }
    g_win_state = win;
}

void raptor_parity_tick(void) {
    if (!g_out) return;
    // Emit once per simulated second: ~70 frames at 70 Hz.
    // Use relative framecount so that captures from different runs align on
    // context-local time rather than absolute startup framecount.
    // In-game: relative to raptor_parity_game_enter (g_game_fc0).
    // In menu: relative to the most recent raptor_parity_set_win_state (g_menu_fc0).
    //
    // Use boundary-crossing detection instead of strict modulo equality.
    // SWD_Dialog hot-spins and each iteration advances framecount by ≥2, so
    // strict `rel_fc % 70 == 0` frequently misses the boundary entirely.
    // Instead, emit whenever we cross into a new 70-frame "second" bucket.
    int fc0 = g_in_game ? g_game_fc0 : g_menu_fc0;
    int rel_fc = framecount - fc0;
    int cur_sec = rel_fc / 70;
    if (cur_sec <= g_last_emit_sec) return;  // still in same or past second
    g_last_emit_sec = cur_sec;
    // Snap the reported fc to the exact second boundary for determinism.
    rel_fc = cur_sec * 70;

    int shield  = clamp_shield(OBJS_GetAmt(PARITY_S_ENERGY));
    int enemies = clamp_to_64(numships);
    int pbulls  = clamp_to_64(shotnum);
    int ebulls  = clamp_to_64(eshotnum);
    int px      = clamp_px(playerx);
    int py      = clamp_py(playery);
    uint64_t oh = compute_obj_hash();

    fprintf(g_out,
        "{\"fc\":%d,\"win\":\"%s\",\"player_x\":%d,\"player_y\":%d,"
        "\"score\":%u,\"shield\":%d,\"enemies\":%d,"
        "\"pbullets\":%d,\"ebullets\":%d,\"obj_hash\":\"%016llx\"}\n",
        rel_fc,
        win_state_name(),
        px, py,
        plr.score,
        shield,
        enemies,
        pbulls, ebulls,
        (unsigned long long)oh);
    fflush(g_out);
}
