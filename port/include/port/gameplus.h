#ifndef PORT_GAMEPLUS_H
#define PORT_GAMEPLUS_H

/* Enhancements and accessibility options (port/src/common/gameplus_pc.c), each a
 * declared setting (port/include/port/settings.h: portSettingsDeclare), off
 * by default: a stock playthrough is the original game. The game code calls
 * these from small TARGET_PC blocks; their controls are on the settings page
 * (port/settings/features/gameplus.cpp). */

#ifdef __cplusplus
extern "C" {
#endif

/* Declares every Enhancements setting (the settings page's file, at start; the
 * game code's first call, otherwise). Safe to call again. */
void portGameplusDeclare(void);
/* A Enhancements setting's value by key (declared on first use). */
int portGameplus(const char* key);

/* --- Partner quick-swap ("quick_swap") --- */
/* the partner menu's order of the partners' ids (win_party.c winPartyDt) */
#define PORT_PARTNER_MENU_COUNT 7
extern const int gPortPartnerMenuOrder[PORT_PARTNER_MENU_COUNT];
/* The quick-swap control was pressed (a key, a controller's button, the
 * on-screen button or a test script). */
void portQuickSwapPress(void);
/* The second player's L or R in the field (second_player_pc.c): the same,
 * whether the Enhancements tab's quick-swap is on or not. */
void portQuickSwapPressCoop(void);
/* the player menu's Character strip: player 2's partner switched to that member (1-7) at the next poll */
void portQuickSwapPickCoop(int member);
/* Aurora: whether the quick-swap control is held now (keyboard Q, a
 * controller's left stick press); NULL elsewhere. Polled by the field. */
extern int (*gPortQuickSwapHeld)(void);
/* phones: the on-screen quick-swap button is down (port/touch/touch_overlay.cpp) */
extern int gPortQuickSwapTouch;
/* The partner wheel (port/settings/features/partner_wheel.cpp): it has the
 * quick-swap control (a tap still swaps, through portQuickSwapPress; held, it
 * opens the wheel), and it is open now. Its choice comes back as the second
 * screen's partner tap (companion.h), in the controller's input, so online
 * and replays carry it. */
extern int gPortPartnerWheelOwns, gPortPartnerWheelOpen;
/* The port (0-3) of the player holding the wheel: their stick points it. */
extern int gPortPartnerWheelPort;
/* DEMOPad.c, this game's pads before online's exchange: the stick of the
 * wheel's player is the wheel's while it is open. */
struct PADStatus;
void portPartnerWheelPad(struct PADStatus* pads);
/* touch_overlay.cpp: the finger on the quick-swap button moved (window pixels). */
void portPartnerWheelPointer(float x, float y);
/* port/src/common/partner_wheel_pc.c: the partners who can be chosen now
 * (joined, not away; the partner menu's order) and the one out (current, 0
 * none); 0 when the wheel can't open (not Mario in the field). */
int portPartnerWheelList(int* ids, int max, int* current);
/* The field could take a switch now (no scene or menu running). */
int portPartnerWheelFieldFree(void);
/* Why the wheel can't open now (a battle, Peach or Bowser in the story, a scene, no partner), in English
 * for portTr: said as the quick-swap control is held. */
const char* portPartnerWheelWhyNot(void);
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

/* --- Challenge rules (port/src/common/challenge_pc.c; the Challenges tab) --- */
void portChallengeDeclare(void);
/* A challenge setting's value by key (0 when off). */
int portChallenge(const char* key);
/* battle_damage.c, an enemy's hit on Mario or a partner: with one-hit KO,
 * the damage that knocks the target out. */
int portChallengeDamage(int damage, int targetHp);
/* battle_ac.c, a guard's result (2 none, 4 Guard, 5 Superguard): taken away
 * as the guard challenge says. */
int portChallengeGuard(int result);
/* battle_ac.c, an attack command's difficulty (0-6) for Mario's side: harder. */
int portChallengeCommandLevel(int level);

/* Every frame (trace_pc.c portTraceFrame): the options that change the
 * game's data tables apply or undo their change. */
void portGameplusFrame(void);

/* --- Remake balance ("remake_balance"): Star Power by source, Power Bounce's
 * window narrowing twice as fast, First Attack at 0 BP, Hammer Throw 2
 * piercing damage (data, set by portGameplusFrame) --- */
/* battle_audience.c BattleAudience_ApRecoveryBuild: the crowd's root, the
 * command and Stylish bonuses, Mario's and the partner's Danger/Peril
 * multipliers and the BINGO boost's, changed in place (TRUE when on). */
int portStarPowerRemake(float* root, float* a, float* b, float* mm, float* pm, float* bingo);
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
#define PORT_SCENE_PEDESTAL 3     /* tik_05.c mahojin_event1-6: the map's reveal, 6 s cut to 1.5 s */
#define PORT_SCENE_GLITZ_OPENING 4 /* tou_03.c evt_default_opening: everyone placed, the gong */
#define PORT_SCENE_GLITZ_WIN 5    /* tou_03.c evt_default_win: no confetti, poses or lines */
#define PORT_SCENE_TRAIN_TICKET 6 /* gor_04.c ekiin_talk: aboard without the ticket hand-over */
#define PORT_SCENE_BLIMP_TICKET 7 /* gor_04.c hikosenkakari_talk: the same for the blimp */
/* From here on, a scene's "seen" is recorded beside the save (portSceneSeen)
 * at its safe end point (CALL(portEvtSceneSeen, scene)); the remake's list: */
#define PORT_SCENE_RECORDED 16
#define PORT_SCENE_INN_SPOT 16    /* evt_kinopio.c: the spotlight closing on the sleeping Mario, 6 s cut to 1 s */
#define PORT_SCENE_INN_DARK 17    /* evt_kinopio.c: the dark while the party rests, 3 s cut to 0.3 s */
#define PORT_SCENE_UPGRADE 18     /* gor_02.c powerupya_party_lank_rtn: Merlon's rank up, the 7 s gathering cut to 1.5 s */
#define PORT_SCENE_CHARM 19       /* tik_12.c majinaisi_talk: Merlee's charm cube, straight to her last line */
#define PORT_SCENE_BLACK_SKULL 20 /* gor_00.c / muj_00.c ship_start: Cortez's ship, straight to the arrival */
#define PORT_SCENE_EXPRESS 21     /* gor_04.c / pik_00.c hom_10_init_evt: the Excess Express, straight to the station */
/* From here on, a story scene played once a file: "seen" on any file of this
 * device (a replay; portSceneSeenEver) */
#define PORT_SCENE_EVER 48
#define PORT_SCENE_PROLOGUE 48    /* aaa_00.c evt_prologue: the letter and the map, straight to the boat */
#define PORT_SCENE_PALACE_STANDS 49 /* las_10.c kagi_8_on: the eight key stands rising, cut to an eighth */
#define PORT_SCENE_PALACE_GLOBE 50 /* las_10.c tenkyugi_evt: the celestial globe, its waits cut to a fifth */
#define PORT_SCENE_INTERLUDE 56   /* + 0-15: a Peach or Bowser interlude (skip_interludes, qol_pc.c) */
struct EventEntry;
/* An event command (CALL(portEvtSceneSkip, scene, LW(n))): LW(n) = 1 when
 * the setting is on and the scene was seen (its script then jumps to its end). */
int portEvtSceneSkip(struct EventEntry* event, int isFirstCall);
/* An event command (CALL(portEvtHeldMax, LW(n))): LW(n) = how many items the
 * bag holds now (Bag size, and 20 with the Strange Sack), for the scripts that
 * check for room with the original's 10 / 20 (Trouble Center rewards, gifts) */
int portEvtHeldMax(struct EventEntry* event, int isFirstCall);
/* gameplus2_pc.c: scene id (0-63) seen on any file of this device; mark: record it. */
int portSceneSeenEver(int id, int mark);
/* speed_aurora.c: the game's speed (percent) a scene asks for: a boss's
 * intro run again after a Game Over plays fast (100: none). */
int portSceneSpeedPct(void);
/* An event command (CALL(portEvtSceneMs, scene, ms, LW(n))): LW(n) = ms, or
 * the shorter time (0: none) when the setting is on and the scene was seen
 * on this file before (a second Crystal Star; a recipe cooked before). */
int portEvtSceneMs(struct EventEntry* event, int isFirstCall);
/* An event command (CALL(portEvtSceneSeen, scene)) at a recorded scene's
 * safe end: it was seen on this file. */
int portEvtSceneSeen(struct EventEntry* event, int isFirstCall);
/* gameplus2_pc.c: recorded scene id (0-63) seen on this file; mark: record it. */
int portSceneSeen(int id, int mark);

/* --- Start in Rogueport ("start_in_rogueport"): the prologue's other skip --- */
/* Skip scenes' prologue (PORT_SCENE_PROLOGUE) cuts the letter in Mario's house
 * and goes on to the boat; this one, picked per new file on the file menu,
 * skips the whole opening in the same branch of aaa_00's evt_prologue: what
 * the opening leaves is put in place (the story at Goombella joining, her in
 * the party and out, the Lord Crump fight's star points, the flags its scenes
 * set) and Mario walks into the plaza (gor_01) from the harbour, where the
 * played opening hands him control. The file menu's {A} on a new file asks
 * which (new_file_pick_pc.c's window): from the beginning or in Rogueport;
 * Always puts the cursor on Rogueport first. */
/* the file menu offers it now (not in Console-accurate mode, nor on an online guest's game: online the host's
   file is everyone's, and its Rogueport start is everyone's too) */
int portQuickStartOffered(void);
/* the setting on Always: the window's cursor on Rogueport first (the launcher's New adventure: Rogueport) */
int portQuickStartDefault(void);
/* seq_load.c: a new file is being started: 1 in Rogueport, 0 from the
 * beginning, -1 neither (back on the file menu) */
void portQuickStartPick(int rogueport);
/* An event command (CALL(portEvtQuickStart, apply, LW(n))): LW(n) = 1 when the
 * new file was started in Rogueport; with apply 1, the opening's state is put
 * in place too (and the pick used up). */
int portEvtQuickStart(struct EventEntry* event, int isFirstCall);
/* new_file_pick_pc.c: the file menu's window asking a new file's way in (title: the menu's "New Game", in
 * the game's encoding). portNewFilePick, from the file menu's main state each frame (trg and dir: the
 * frame's presses): 0 not up; 1 up (the menu takes no input meanwhile); 2 from the beginning, 3 in
 * Rogueport (picked, now closed); 4 Back (closed, the files again). */
void portNewFilePickOpen(const char* title, int rogueportFirst);
int portNewFilePickUp(void);
int portNewFilePick(unsigned trg, unsigned dir);

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

/* --- Journal (the settings page's Enhancements tab, while a game runs) --- */
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

/* --- Tattle without a turn ("free_tattle"; battle_seq_command.c) --- */
#define PORT_OPERATION_TATTLE 0x50 /* the Tactics entry's type, and the message route's reason */
/* Mario's side may read a tattle now: the setting is on, Goombella has joined
 * and an enemy on the field was tattled before (Tattle Log or this battle). */
int portTattleFreeAvailable(void);
/* The entry was picked: the list of tattles to read starts over. */
void portTattleFreeBegin(void);
/* Event commands of the menu's message event: LW = 1 when it is the free
 * Tattle; then each enemy kind's tattle message in turn, 0 at the end. */
int portEvtTattleFree(struct EventEntry* event, int isFirstCall);
int portEvtTattleNext(struct EventEntry* event, int isFirstCall);

/* --- Battle feedback pop-ups ("battle_popups"; port/src/common/battle_popup_pc.c) --- */
struct BattleUnit;
struct BattleUnitPart;
/* battle_damage.c: Mario or the partner guarded (super: a Superguard). */
void portPopupGuard(struct BattleUnit* unit, struct BattleUnitPart* part, int super);
/* battle_damage.c: unit resisted statusType (its roll failed). */
void portPopupResisted(struct BattleUnit* unit, int statusType);
/* battle_seq.c, every battle frame: ages and draws them; portPopupClear at a battle's start. */
void portPopupFrame(void);
void portPopupClear(void);
/* battle_status_icon.c: the status icons also show outside the command menu. */

/* --- Save Blocks without the question ("save_no_prompt"): evt_memcard.c --- */

/* --- Accessibility: difficulty ("difficulty": 0 original, 1 easy, 2 hard,
 * 3 custom: "difficulty_enemy_hp", "difficulty_enemy_attack" in %) --- */
/* battle_unit.c BtlUnit_Entry: an enemy's maximum HP. */
int portEnemyHp(int hp);
/* battle_damage.c BattleCalculateDamage: an enemy's attack's damage to Mario
 * or a partner (at least 1 when it was at least 1). */
int portEnemyDamage(int damage);
/* A mode's own scaling in place of the difficulty (the endless Glitz Pit: stronger each round), never the
 * player's settings, never saved: enemy HP and attack in % (0, 0: none, the difficulty again). */
void portDifficultyOverride(int hpPercent, int attackPercent);

/* --- Accessibility: action-command assist ("ac_attack_assist",
 * "ac_guard_assist": 0 off, 1 wider windows, 2 automatic, 3 by command type:
 * port/include/port/dojo.h): battle_ac.c --- */

/* ===== The second batch (port/src/common/gameplus2_pc.c; their controls in
 * port/settings/features/gameplus2.cpp) ===== */

/* Declares the second batch's settings (portGameplusDeclare does). */
void portGameplus2Declare(void);
/* seq_load.c (through portNewGamePlus), a new file just set up: what was
 * kept for the file in that slot before (the tracks heard) starts over. */
void portGameplus2NewFile(void);

/* --- Badge loadouts ("badge_loadouts"; the settings page's list). Kept beside
 * the save, not in it (gameplus_<version>_file<N>.txt in the user folder). --- */
#define PORT_LOADOUT_COUNT 4
#define PORT_LOADOUT_NAME 24
/* Loadout i of the file being played: its name and how many badges it has
 * (0: empty); -1 when no game runs. */
int portLoadoutGet(int i, char* name, int nameSize);
void portLoadoutRename(int i, const char* name);
/* Loadout i becomes the badges worn now (returns how many). */
int portLoadoutStore(int i);
/* Wears loadout i in place of the badges worn now, in the field only and only
 * when Mario's BP covers it: TRUE when it was put on; msg says what happened.
 * Asked with the Start menu open: TRUE, and it is worn once the menu is closed
 * (portLoadoutFrame, each frame; the result as a notice). */
int portLoadoutWear(int i, char* msg, int msgSize);
void portLoadoutFrame(void);

/* --- Item storage at every Save Block ("storage_anywhere"): evt_mobj.c
 * mobj_save_blk_sysevt asks what to do first; storing and taking out work as
 * at the Rogueport shop's counter. Event commands: --- */
/* LW(0) = 1 when the setting is on. */
int portEvtStorageOn(struct EventEntry* event, int isFirstCall);
/* LW(0) items held, LW(1) storage slots free, LW(2) items stored, LW(3) bag
 * slots free. */
int portEvtStorageCounts(struct EventEntry* event, int isFirstCall);
/* LW(arg 1) = the menu's text number arg 0 in the menu language, for
 * MSG_PRINT_DIRECT: 0 the question, 1-4 the notices (no items to store, storage
 * full, nothing stored, bag full), PORT_STORAGE_TEXT_SELECT the choice list. */
#define PORT_STORAGE_TEXT_SELECT 9
#define PORT_STORAGE_TEXT_SELECT_SHOP 10 /* the same with "Shop" first (the Howz of Badges' box, "shop_plus") */
#define PORT_STORAGE_TEXT_SELL_FROM 11   /* a shop's selling: from the bag, from storage, leave ("shop_plus") */
#define PORT_STORAGE_TEXT_SELL_ASK 5     /* its question */
int portEvtStorageText(struct EventEntry* event, int isFirstCall);
/* evt_mobj.c port_storage_evt (EvtWord[]): the storage menu; LW(12) its choice
 * list (PORT_STORAGE_TEXT_SELECT or _SELECT_SHOP); on return LW(13) is 0 for
 * the first choice, 1 for Leave. */

/* --- Walk-speed boost ("walk_boost", "walk_boost_speed" %, "walk_boost_key",
 * "walk_boost_pad") --- */
/* Aurora: the boost control is held now by the player on that port (0-3: its
 * controller, the keyboard its seat's; port/settings/features/gameplus2.cpp). */
extern int (*gPortWalkBoostHeld)(int port);
/* mot_walk.c marioGetWalkSpd / marioGetDashSpd: Mario's speed scale now
 * (port 0's control: his player's). */
float portWalkBoost(void);
/* The same for the player on that port (co-op's partners and companions,
 * Brothers mode's Luigi: their own player's control, their own speed). */
float portWalkBoostFor(int port);

/* --- Coin and EXP multipliers ("coin_multiplier", "exp_multiplier": 0-4 for
 * x1, x1.5, x2, x2.5, x3) --- */
/* npcdrv.c: the coins a won battle drops (after Merlee, Money Money, First Strike). */
int portBattleCoins(int coins);
/* battle_seq_end.c: the battle's Star Points (before the cap of 100). */
int portBattleExp(int exp);

/* --- Star Piece and Shine Sprite tracker ("collectible_tracker": 0 off,
 * 1-4 a corner TL TR BL BR) --- */
/* The current map's, by their flags: TRUE when it has any (in the field). */
int portCollectiblesHere(int* pieces, int* piecesTotal, int* sprites, int* spritesTotal);
/* A map's (its first six letters), with a file loaded: the area maps' (port/include/port/area_maps.h). */
int portCollectiblesForMap(const char* map, int* pieces, int* piecesTotal, int* sprites, int* spritesTotal);
/* The tracker's list, one by one (the Journal's hints, qol_pc.c): entry i's map,
 * whether it is a Shine Sprite and whether it was found; 0 past the end. */
int portCollectibleAt(int i, const char** map, int* sprite, int* found);

/* --- Faster text outside battle ("fast_field_text") --- */
/* msgdrv.c: the scale of a message window's time (text, page scroll, fades):
 * 1, or 3 for messages outside battle while the setting is on. */
int portFieldTextScale(void);

/* --- Jukebox ("jukebox"; the settings page's list) --- */
/* pmario_sound.c psndBGMOn_f_d: BGM id (psbgmlist) was asked for on channel ch. */
void portJukeboxHeard(int id, int ch);
/* pmario_sound.c: the BGM table's size and a track's name ("" for none). */
int portBgmCount(void);
const char* portBgmName(int id);
/* A track was heard on the file being played (while the setting was on). */
int portJukeboxKnown(int id);
/* Plays track id on the field channel (-1: the map's own music again). */
void portJukeboxPlay(int id);
/* The track the jukebox plays now, or -1. */
int portJukeboxPlaying(void);
/* The jukebox's own psndBGMOn is running (Music shuffle leaves its pick alone). */
int portJukeboxCalling(void);

/* --- Junk-item helper in shops ("sell_helper"): winmgr.c's sell lists mark
 * the items and badges Mario has more than one of --- */

#ifdef __cplusplus
}
#endif

#endif
