#ifndef PORT_GAMEPLUS_H
#define PORT_GAMEPLUS_H

/* Gameplay+ and accessibility options (port/src/common/gameplus_pc.c), each a
 * declared setting (port/include/port/settings.h: portSettingsDeclare), off
 * by default: a stock playthrough is the original game. The game code calls
 * these from small TARGET_PC blocks; their controls are on the settings page
 * (port/settings/features/gameplus.cpp). */

#ifdef __cplusplus
extern "C" {
#endif

/* Declares every Gameplay+ setting (the settings page's file, at start; the
 * game code's first call, otherwise). Safe to call again. */
void portGameplusDeclare(void);
/* A Gameplay+ setting's value by key (declared on first use). */
int portGameplus(const char* key);

/* --- Partner quick-swap ("quick_swap") --- */
/* The quick-swap control was pressed (a key, a controller's button, the
 * on-screen button or a test script). */
void portQuickSwapPress(void);
/* Aurora: whether the quick-swap control is held now (keyboard Q, a
 * controller's left stick press); NULL elsewhere. Polled by the field. */
extern int (*gPortQuickSwapHeld)(void);
/* phones: the on-screen quick-swap button is down (port/touch/touch_overlay.cpp) */
extern int gPortQuickSwapTouch;
/* win_main.c, every field frame the pause menu could open: the partner to
 * switch to (the next one who has joined, in the partner menu's order), or
 * -1 for none. current: the partner out now (0: none). */
int portQuickSwapPoll(int current);

/* --- Bigger bag ("bigger_bag") --- */
/* mario_pouch.c: the held items without the Strange Sack, 10 (the original)
 * or 20 (every slot the save has). */
int portHeldItemBase(void);

/* --- Partners never at 0 HP ("partner_revive") --- */
/* battle_seq_end.c, after a battle wrote Mario's and the partners' HP back:
 * any partner at 0 HP gets 1. */
void portPartyReviveAfterBattle(void);

/* --- Level-up with the D-pad ("levelup_dpad"): battle_seq_end.c --- */

/* --- Healing every partner ("heal_all_partners") --- */
/* win_main.c / win_item.c, after a field item healed partner target by hp:
 * the other partners who have joined get it too. */
void portHealOtherPartners(int target, int hp);

/* Every frame (trace_pc.c portTraceFrame): the options that change the
 * game's data tables apply or undo their change. */
void portGameplusFrame(void);

/* --- Remake balance ("remake_balance"): Star Power gain x1.5, Power Bounce's
 * window narrowing half as fast, First Attack at 0 BP, Hammer Throw piercing
 * defense (data, set by portGameplusFrame) --- */
/* battle_audience.c BattleAudience_ApRecoveryBuild: the gain's scale. */
float portStarPowerScale(void);
/* ac_timing_a.c: the bounce count a narrowing timed-A window is judged at. */
int portPowerBounceCount(int count);

/* --- Faster battle menus ("fast_battle_menus") --- */
/* battle_menu_disp.c DrawMainMenu: the command wheel's animation steps per frame. */
int portBattleMenuStep(void);

/* --- Retry on Game Over ("gameover_retry"; save states, Aurora) --- */
/* seq_game.c / seq_battle.c: a room was entered or a battle began: the retry
 * state is saved at the end of the frame. */
void portRetryPoint(void);
/* seq_gameover.c, every frame of the Game Over screen: offers the retry and,
 * on Y, loads it (TRUE: the screen waits for it). */
int portRetryOffer(void);

/* --- Skip repeated scenes ("skip_scenes") --- */
#define PORT_SCENE_CRYSTAL_STAR 1 /* evt_sub.c evt_sub_starstone: the 9.7 s fanfare, cut to 2.5 s */
#define PORT_SCENE_COOKING 2      /* gor_01.c cooking_evt_sub: Zess T.'s cooking, skipped */
struct EventEntry;
/* An event command (CALL(portEvtSceneMs, scene, ms, LW(n))): LW(n) = ms, or
 * the shorter time (0: none) when the setting is on and the scene was seen
 * on this file before (a second Crystal Star; a recipe cooked before). */
int portEvtSceneMs(struct EventEntry* event, int isFirstCall);

/* --- Fast travel ("fast_travel"; the settings page's list) --- */
/* The hubs: Rogueport, then one more per Crystal Star (Petalburg, Boggly
 * Woods, Glitzville, Twilight Town, Keelhaul Key, Poshley Heights, Fahr
 * Outpost). */
int portFastTravelCount(void);
const char* portFastTravelName(int i);
/* A hub can be travelled to: the setting is on, a game is in the field and
 * the file has the Crystal Stars it needs. */
int portFastTravelUnlocked(int i);
/* Travels to hub i between frames, once the field is free (no scene running). */
void portFastTravelRequest(int i);

/* --- Pit of 100 Trials ("pit_plus") and Glitz Pit ("glitz_pit_kind") --- */
/* jon.c floor_inc (event command): coins for the floor just cleared, 1 more per
 * 10 floors down; the rest floors give none. */
int portEvtPitFloorCoins(struct EventEntry* event, int isFirstCall);
/* jon.c idouya_setup (CALL(portEvtPitMover, LW(0)) after the roll): the
 * Mover's roll made a sure one once floor 50 was cleared. */
int portEvtPitMover(struct EventEntry* event, int isFirstCall);
/* Glitz Pit: tou.c (a lost match keeps the rank) and battle_seq.c (the
 * match's condition stays on screen). */

/* --- New Game+ ("new_game_plus": 0 off, 1 Mario's level and stats, 2 also
 * the badges, 3 also the Tattle Log; "new_game_plus_from": the file 1-4 they
 * come from) --- */
/* seq_load.c, a new file just set up: copies them from that file. */
void portNewGamePlus(void);

/* --- Journal (the settings page's Gameplay+ tab, while a game runs) --- */
typedef struct PortJournal {
    int story;                    /* GSW 0, the story's progress */
    int troubles, troublesTotal;  /* Trouble Center jobs solved */
    char troublesOpen[128];       /* the numbers of those not solved yet */
    int badges, badgesTotal;      /* badges found (the Badge Log) */
    int recipes, recipesTotal;    /* recipes cooked (the Recipe Log) */
    int tattles, tattlesTotal;    /* enemies in the Tattle Log */
    int starPieces, shineSprites; /* held now */
    int crystalStars;
} PortJournal;
/* The file being played's progress, from its flags; 0 when no game runs. */
int portJournal(PortJournal* out);

/* --- Save Blocks without the question ("save_no_prompt"): evt_memcard.c --- */

/* --- Accessibility: difficulty ("difficulty": 0 original, 1 easy, 2 hard,
 * 3 custom: "difficulty_enemy_hp", "difficulty_enemy_attack" in %) --- */
/* battle_unit.c BtlUnit_Entry: an enemy's maximum HP. */
int portEnemyHp(int hp);
/* battle_damage.c BattleCalculateDamage: an enemy's attack's damage to Mario
 * or a partner (at least 1 when it was at least 1). */
int portEnemyDamage(int damage);

/* --- Accessibility: action-command assist ("ac_attack_assist",
 * "ac_guard_assist": 0 off, 1 wider windows, 2 automatic): battle_ac.c --- */

#ifdef __cplusplus
}
#endif

#endif
