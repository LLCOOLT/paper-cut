#ifndef PORT_RULES_H
#define PORT_RULES_H

/* The Switch remake's rule changes and the players' rule wishes
 * (port/src/common/rules_pc.c), each its own setting, off by default, all
 * marked "changes play" (Console-accurate mode locks them; online sends
 * them with the host's settings). Read them with portGameplus("<key>"); the
 * game code calls the hooks below from small TARGET_PC blocks. Controls:
 * port/settings/features/rules.cpp. */

#ifdef __cplusplus
extern "C" {
#endif

struct BattleWork;
struct BattleUnit;
struct BattleWeapon;

/* gameplus_pc.c portGameplusDeclare: declares the settings below. */
void portRulesDeclare(void);
/* gameplus_pc.c portGameplusFrame: data tables in or out, the Trouble and
 * boss-coin bookkeeping. */
void portRulesFrame(void);

/* --- Remake item and badge data --- */
/* sac_muki.c (Power Lift, "rules_power_lift"): a gauge's step per arrow hit. */
float portRulesPowerLiftStep(float step);
/* sac_genki.c (Sweet Treat, "rules_sweet_treat"): its Poison Mushrooms. */
int portRulesSweetTreatPoisons(int count);
/* battle_item_data.c (Mystery, "rules_mystery_box"): the roulette's table
 * (zero-ended), given the game's own. */
const int* portRulesMysteryTable(const int* original);
/* gor_03.c (lottery, "rules_lottery"): the first prize for rank 0 or 1. */
int portRulesLotteryPrize(int rank, int item);
/* battle_damage.c ("rules_hazard_pierce"): TRUE when this attack is a stage
 * hazard's (the system unit's) and goes through defense. */
int portRulesHazardPierces(struct BattleUnit* attacker);
/* battle_damage.c ("rules_deep_sleep"): the percent chance a hit wakes a
 * sleeper. */
int portRulesWakeChance(int percent);
/* battle_seq_end.c: a battle was won (boss coins, "rules_boss_coins"; not in a mode's battle: a refight pays
 * none). */
void portRulesBattleWon(struct BattleWork* wp);
/* evt_sub.c evt_sub_get_coin (a Trouble's reward): the coins paid ("rules_trouble_coins": 20 becomes 30). */
int portRulesTroubleCoins(int coins);

/* --- The remake's enemy rules --- */
/* battle_event_cmd.c btlevtcmd_GetExp ("rules_bomb_no_sp"): the Star Points
 * a defeated unit gives. */
int portRulesExp(struct BattleUnit* unit, int exp);
/* battle_damage.c ("rules_buzzy_blast"): a part's defense attribute for the
 * element (2: immune). */
int portRulesDefenseAttr(struct BattleUnit* target, int element, int attr);
/* battle_detect.c BattleSamplingEnemy ("rules_flower_fuzzy"): the target
 * list built, trimmed. */
struct BattleWorkWeaponTargets;
void portRulesTargets(struct BattleWorkWeaponTargets* d);
/* battle_unit.c, a unit leaving holding what it stole ("rules_bandit_returns"):
 * TRUE when it went back to Mario (the field NPC keeps nothing). */
int portRulesStolenBack(struct BattleUnit* unit);
/* npcdrv.c _npcHitCheckSphere ("rules_spider_strike"): the reach of a hit
 * on this field NPC. */
struct NpcEntry;
float portRulesFirstStrikeReach(struct NpcEntry* npc, float r);

/* Unit scripts (jon/las/tst unit_basabasa_chururu.c, "rules_swoopula_swoop"):
 * args the bite's weapon and an out value: 0, or half the time a weapon for
 * the remake's Swoop (the bite's power, no piercing, no drain). */
struct EventEntry;
int portEvtRuleSwoop(struct EventEntry* event, int isFirstCall);
/* Unit scripts (jon/las/tst unit_wanwan.c, "rules_chomp_still"): args the
 * unit and an out value, 1 while Mario's side aims a jump at it (it holds
 * still instead of hopping). */
int portEvtRuleChompHold(struct EventEntry* event, int isFirstCall);

/* --- Bosses --- */
/* battle_monosiri.c ("rules_retattle"): TRUE when this kind's HP shows only
 * once tattled in this battle (a boss's second fight). */
int portRulesRetattle(int kind);

/* --- Players' wishes --- */
/* battle_damage.c ("balanced_badges"): how many of a Power Rush / Mega Rush
 * count. */
int portRulesBadgeStack(int count);
/* battle_damage.c ("balanced_badges"): a Charge's strength as it applies. */
int portRulesChargeCap(int strength);
/* sac_muki.c ("balanced_badges"): Power Lift's gauge cap. */
float portRulesPowerLiftMax(float max);
/* battle_event_cmd.c btlevtcmd_WeaponAftereffect ("no_stage_hazards"). */
int portRulesNoStageHazards(void);
/* battle_audience.c BattleAudienceItemOn ("no_audience_throws"). */
int portRulesNoAudienceThrows(void);
/* battle_camera.c, evt_cam.c ("less_shake"): a shake's size. */
float portRulesShakeScale(void);

/* battle_seq_command.c BattleCommandInput ("rules_audience_pause"): drops
 * this frame's presses while something the audience threw is in the air. */
void portRulesCommandInputPre(void);
/* battle_seq.c btlseqMove ("rules_sp_fill_wait"): TRUE while Star Power
 * sparkles still fly to the gauge (the enemy phase waits). */
int portRulesWaitStarPower(void);

/* battle_event_default.c _get_escape_rate ("rules_doopliss"): the Run Away
 * gauge's start (higher is easier), given the enemy group's. */
int portRulesEscapeRate(int rate);
/* gra_00.c, the Twilight Trail shed scene ("rules_doopliss_shed"): Doopliss's
 * surprise attack takes 1 HP from Mario (never his last); the out value is 1
 * when it did (the script plays Mario's hurt pose). */
int portEvtRuleShedHit(struct EventEntry* event, int isFirstCall);

/* --- The Glitz Pit ("rules_glitz_conditions"; tou.c, battle_seq.c) --- */
/* tou.c evt_tou_get_rule_info, given its mode (the opponent's id) and whether
 * it is after chapter 3: TRUE with the remake's condition (the speech's
 * message tag, the rule id and its two parameters) for Rawk Hawk (none) and
 * the Armored Harriers, the Iron Adonises (a partner's FP move at least once). */
int portRulesGlitzRule(int mode, int after, const char** speech, int* id, int* param, int* param2);
/* tou.c evt_tou_get_rule_msg2 and battle_seq.c's condition banner: the short
 * text's message tag for a rule id only the remake's conditions use, or NULL. */
const char* portRulesGlitzDesc(int id);
/* tou.c evt_eat_cake: arg 0 before the cake heals (keeps the Star Power), 1
 * after (puts it back): the first cake gives no Star Power. */
int portEvtRuleCakeSp(struct EventEntry* event, int isFirstCall);
/* msgdrv.c msgSearch: these rules' own messages (pc_rules_*), or NULL. */
const char* portRulesMsg(const char* tag);

/* --- The stagehand ("rules_tremor_stagehand") --- */
/* battle_seq.c btlseqMove, each phase's start: TRUE while a stagehand still
 * carries a partner (the phase waits). */
int portRulesStagehandWait(int phase);

/* --- Auto-win much weaker fights ("auto_win_weak") --- */
/* npcdrv.c fbatEncountCheck: Mario touched or struck this field enemy. */
void portRulesFieldTouch(struct NpcEntry* npc);
/* npcdrv.c fbatBattleMode: TRUE when the touched enemy's group is far below
 * Mario's level and the fight is won at once (the coins and drops set as a
 * won battle's, result 1). */
struct FieldBattleInfo;
int portRulesAutoWin(struct NpcEntry* npc, struct FieldBattleInfo* info);

/* the flash effects' draws (eff_akari_sflush_n64.c, eff_wangflush_n64.c,
 * eff_toge_flush.c, eff_hagetaka_flush_n64.c, eff_thunder_n64.c;
 * "less_flashing"): a flash's alpha, toned down. */
int portRulesFlashAlpha(int alpha);

/* --- Battle screen information (output only) --- */
/* battle_seq_command.c ("battle_sp_grey"): TRUE when no Special obtained is
 * affordable now (the Special command is greyed). */
int portRulesSpecialsShort(void);
/* ac_cue_pc.c, each frame a timed press is predicted ("spring_jump_cue"):
 * the unit, the kind (1 attack, 2 guard), frames to the judgement, the
 * window's width. */
void portRulesAcWindow(void* unit, int kind, int toJudge, int window);

#ifdef __cplusplus
}
#endif

#endif
