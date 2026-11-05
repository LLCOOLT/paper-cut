// Declares the shared star-power special move helpers.
#pragma once

#include "evt/evt_cmd.h"
#include "dolphin/types.h"

USER_FUNC(sac_ac_help_off);
USER_FUNC(sac_ac_help_on);
USER_FUNC(sac_cheer);
USER_FUNC(sac_cheer_end);
USER_FUNC(sac_enemy_slide_go);
USER_FUNC(sac_enemy_slide_return);
USER_FUNC(sac_handbeat);
USER_FUNC(sac_handbeat_end);
extern EvtWord sac_mario_back_home[];
extern EvtWord sac_mario_item_on_end_evt[];
extern EvtWord sac_mario_item_on_evt[];
extern EvtWord sac_start_camera_evt[];
extern EvtWord sac_start_stardust[];
USER_FUNC(sac_wao);

/* The alliance a Star Power hits (the game's: 1, the enemies). On PC a
 * versus fight's Luigi uses them too: then it's Mario's side, 0
 * (port/src/aurora/versus_aurora.c). */
#ifdef TARGET_PC
int portSacFoeAlliance(void);
#define SAC_FOE_ALLIANCE portSacFoeAlliance()

/* What a Star Power's game wants now, for a versus computer playing it
 * (port/src/aurora/versus_cpu_aurora.c, which knows which one runs: they
 * share the action command work). Each reads the game only. */
/* Earth Tremor (sac_deka.c): 1 while the gauge runs, with its cursor, its
 * speed a frame and the next star's place (pressed within 7 of it) */
int portEarthTremorWants(f32* cursor, f32* speed, f32* next);
/* Clock Out (sac_bakugame.c): 1 while the bomb game runs, with the button
 * that throws the bomb to the foes now */
int portClockOutWants(u32* foeButton);
/* Power Lift (sac_muki.c): 1 while the cursor waits for input (0 while it
 * moves; -1 not running), its square (0..8, rows from the top) and what
 * shows in each: -1 nothing yet, 0 an attack arrow, 1 defence, 2 poison */
int portPowerLiftGrid(s32* cursor, s32 types[9]);
/* Sweet Treat and Sweet Feast (sac_genki.c): a shot fired after delay
 * frames (the stick pulled now, let go next frame): 1 it would hit a good
 * item, 2 the poison mushroom, 0 nothing; -1 no shot can be fired now */
int portSweetTreatAim(s32 delay);
/* Art Attack (sac_scissor.c): 1 while a line is being drawn, with the star's
 * place and speed (screen space, y up), the points of this line so far and
 * the frames left; the foes it can hit as screen boxes (x0, y0, x1, y1) */
int portArtAttackDraw(f32* x, f32* y, f32* vx, f32* vy, s32* points, s32* timer);
int portArtAttackTargets(f32 (*box)[4], s32 max);
/* Supernova (sac_zubastar.c): 1 while A fills the gauge */
int portSupernovaWants(void);
#else
#define SAC_FOE_ALLIANCE 1
#endif
