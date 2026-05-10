/* Per-second parity checkpoint emitter for the Godot port's L2 tests.
 * When RAPTOR_PARITY_OUT=path is set, opens that file and emits one
 * JSON object per simulated second to it. */
#ifndef RAPTOR_PARITY_H
#define RAPTOR_PARITY_H

/* win-state constants for raptor_parity_set_win_state() */
#define PARITY_WIN_UNKNOWN   0
#define PARITY_WIN_MENU      1
#define PARITY_WIN_CREDITS   2
#define PARITY_WIN_HELP      3
#define PARITY_WIN_ORDER     4
#define PARITY_WIN_HANGAR    5
#define PARITY_WIN_STORE     6
#define PARITY_WIN_BRIEFING  7

void raptor_parity_init(void);             /* call once at startup */
void raptor_parity_tick(void);             /* call every simulated frame */
void raptor_parity_game_enter(int game);   /* call before Do_Game */
void raptor_parity_game_exit(void);        /* call after Do_Game */
void raptor_parity_set_win_state(int win); /* call at menu-state transitions */

#endif
