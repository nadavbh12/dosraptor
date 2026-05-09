/* Per-second parity checkpoint emitter for the Godot port's L2 tests.
 * When RAPTOR_PARITY_OUT=path is set, opens that file and emits one
 * JSON object per simulated second to it. */
#ifndef RAPTOR_PARITY_H
#define RAPTOR_PARITY_H

void raptor_parity_init(void);             /* call once at startup */
void raptor_parity_tick(void);             /* call every simulated frame */
void raptor_parity_game_enter(int game);   /* call before Do_Game */
void raptor_parity_game_exit(void);        /* call after Do_Game */

#endif
