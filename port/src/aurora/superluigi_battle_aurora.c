/* Super Luigi's battle pieces (port/include/port/superluigi.h, "Battles";
 * docs/SUPER_LUIGI.md, "The API"):
 *  - a chapter's unit stats: its table's HP, attack, defense and level put
 *    on each unit of a kind as it enters a battle while the chapter runs;
 *  - target gates: a part only some moves reach, only at some times (the
 *    statue's gem), filtered where the game builds a weapon's targets
 *    (battle_detect.c);
 *  - forced commands: the finishing blows, one command offered (the game's
 *    own menus, the rest greyed out as its tutorial battles grey them), with a
 *    prompt in the game's announcement window;
 *  - prompts over the battle or the field. */
#include "port/superluigi.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_unit.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/windowdrv.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "system.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* --- the chapter's unit stats --- */

#define DEFENSES 64
static BattleUnitDefense sDefense[DEFENSES]; /* per part changed, this battle */
static const BattleUnitDefense* sDefenseFrom[DEFENSES]; /* the game's table each was made from */
static int sDefenses;

/* the chapter's defense in a copy of one of the game's tables (immunities, 99, stay), shared by every
 * part that has that table this battle */
static BattleUnitDefense* chapterDefense(const PortSlUnitStats* s, const BattleUnitDefense* from) {
    int i, k;
    for (k = 0; k < sDefenses; k++) {
        if (sDefenseFrom[k] == from) {
            return &sDefense[k];
        }
    }
    if (sDefenses == DEFENSES) {
        return NULL;
    }
    sDefense[sDefenses] = *from;
    for (i = 0; i < 5; i++) {
        if (sDefense[sDefenses].defenses[i] < 99 && (i == 0 || sDefense[sDefenses].defenses[i] == from->defenses[0])) {
            sDefense[sDefenses].defenses[i] = (s8)s->defense;
        }
    }
    sDefenseFrom[sDefenses] = from;
    return &sDefense[sDefenses++];
}

static int isChapterDefense(const BattleUnitDefense* d) {
    return d >= &sDefense[0] && d < &sDefense[DEFENSES];
}

static const PortSlUnitStats* statsFor(int kind) {
    const PortSlChapter* c = portSlGetChapter(portSuperLuigiChapter());
    const PortSlUnitStats* s;
    if (c == NULL || c->stats == NULL) {
        return NULL;
    }
    for (s = c->stats; s->kind != 0; s++) {
        if (s->kind == kind) {
            return s;
        }
    }
    return NULL;
}

/* BtlUnit_Entry: the unit as the chapter has it */
static void unitEntryPost(BattleUnit** result, BattleUnitSetup* setup) {
    BattleUnit* unit = *result;
    const PortSlUnitStats* s;
    BattleUnitPart* part;
    (void)setup;
    if (unit == NULL || !portSuperLuigiActive() || (s = statsFor(unit->currentKind)) == NULL) {
        return;
    }
    if (s->hp > 0) {
        unit->maxHp = unit->baseMaxHp = unit->currentHp = s->hp;
    }
    if (s->level >= 0) {
        unit->level = (u8)s->level;
    }
    if (s->defense >= 0) {
        for (part = unit->parts; part != NULL; part = part->nextPart) {
            int i;
            if (part->defense == NULL || sDefenses == DEFENSES) {
                continue;
            }
            if (s->defense >= 100) {
                /* 100 + n: n, even for a foe the game makes immune (an Iron Cleft's iron) */
                static BattleUnitDefenseAttr sPlainAttr;
                sDefense[sDefenses] = *part->defense;
                for (i = 0; i < 5; i++) {
                    sDefense[sDefenses].defenses[i] = (s8)(s->defense - 100);
                }
                sDefenseFrom[sDefenses] = NULL;
                part->defenseAttr = &sPlainAttr;
                part->defense = &sDefense[sDefenses++];
            } else {
                BattleUnitDefense* d = chapterDefense(s, part->defense);
                if (d != NULL) {
                    part->defense = d;
                }
            }
        }
    }
}

/* BattlePreCheckDamage: a unit that swapped its own table back in (a Spiny uncurled from its ball, a
 * shell got up: the game's defence table again, Defense 3 where the chapter has 0) has the chapter's
 * again before the blow lands. Seen: Bout 2's Spinies took nothing from the Hammer after their first
 * roll, and the bout was lost after twelve rounds. */
static int preCheckDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                             BattleWeapon** weapon, u32* flags) {
    const PortSlUnitStats* s;
    BattleUnitDefense* d;
    (void)result, (void)attacker, (void)weapon, (void)flags;
    if (*target == NULL || *part == NULL || (*part)->defense == NULL || !portSuperLuigiActive() ||
        isChapterDefense((*part)->defense) || (s = statsFor((*target)->currentKind)) == NULL || s->defense < 0 ||
        s->defense >= 100) {
        return 0;
    }
    if ((d = chapterDefense(s, (*part)->defense)) != NULL) {
        (*part)->defense = d;
    }
    return 0;
}

/* BattleCalculateDamage: an attack of a unit the chapter changed, with its damage */
static int calculateDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                              BattleWeapon** weapon, u32** out, u32* flags) {
    static BattleWeapon sCopy;
    const PortSlUnitStats* s;
    int i;
    (void)result, (void)target, (void)part, (void)out, (void)flags;
    if (*attacker == NULL || *weapon == NULL || !portSuperLuigiActive() || (*attacker)->alliance != 1 ||
        (s = statsFor((*attacker)->currentKind)) == NULL || (s->attack <= 0 && s->attackAdd == 0)) {
        return 0;
    }
    sCopy = **weapon;
    for (i = 0; i < 8; i++) {
        if (sCopy.hpDamageWork[i] > 0) {
            int d = s->attack > 0 ? s->attack : sCopy.hpDamageWork[i] + s->attackAdd;
            sCopy.hpDamageWork[i] = d < 1 ? 1 : d;
        }
    }
    *weapon = &sCopy;
    return 0;
}

/* --- target gates --- */

#define GATES 16
static PortSlTargetGate sGates[GATES];
static int sGateCount;

void portSlAddTargetGate(const PortSlTargetGate* gate) {
    if (sGateCount < GATES) {
        sGates[sGateCount++] = *gate;
    }
}

static int gateLists(const PortSlTargetGate* g, const BattleWeapon* w) {
    int i, any = 0;
    for (i = 0; i < 4; i++) {
        if (g->weapons[i] != NULL) {
            any = 1;
            if (g->weapons[i] == w) {
                return 1;
            }
        }
    }
    return any ? 0 : -1; /* -1: the gate lists none (any move) */
}

int portBtlTargetFilter(struct BattleWorkWeaponTargets* dp, int n) {
    BattleWorkWeaponTargets* d = (BattleWorkWeaponTargets*)dp;
    BattleWork* wp = _battleWorkPointer;
    int i, k, g;
    if (sGateCount == 0) {
        return n;
    }
    for (i = 0; i < n; i++) {
        BattleUnit* unit = BattleGetUnitPtr(wp, d->targets[i].unitIdx);
        BattleUnitPart* part = unit != NULL ? BtlUnit_GetPartsPtr(unit, d->targets[i].partIdx) : NULL;
        int keep = 1;
        for (g = 0; g < sGateCount && part != NULL; g++) {
            const PortSlTargetGate* gate = &sGates[g];
            int lists;
            if (unit->kindParams != gate->kind) {
                continue;
            }
            lists = gateLists(gate, d->weapon);
            if (part->kindPartParams->partNum == gate->part) {
                const int open = gate->openWork < 0 || unit->work[gate->openWork] == gate->openValue;
                if (lists == 0 || !open) {
                    keep = 0;
                }
            } else if (gate->only && lists == 1) {
                keep = 0;
            }
        }
        if (!keep) {
            for (k = i; k < n - 1; k++) {
                d->targets[k] = d->targets[k + 1];
            }
            n--;
            i--;
        }
    }
    return n;
}

/* --- forced commands --- */

static struct {
    int act; /* -1 none */
    const BattleWeapon* weapon;
    const char* prompt;
} sForced[2] = {{-1, NULL, NULL}, {-1, NULL, NULL}};

void portSlForceCommand(int actor, int act, const struct BattleWeapon* weapon, const char* prompt) {
    if (actor < 0 || actor > 1) {
        return;
    }
    sForced[actor].act = act;
    sForced[actor].weapon = (const BattleWeapon*)weapon;
    sForced[actor].prompt = act >= 0 ? prompt : NULL;
}

/* the actor (0 Luigi, 1 the partner) the active unit is, or -1 */
static int activeActor(BattleWork* wp) {
    BattleUnit* unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL) {
        return -1;
    }
    if (unit->currentKind == UNIT_MARIO) {
        return 0;
    }
    return unit->currentKind >= TYPE_PARTNER_MIN && unit->currentKind < TYPE_PARTNER_MAX ? 1 : -1;
}

void portBtlActFilter(struct BattleWork* wpp) {
    BattleWork* wp = (BattleWork*)wpp;
    BattleCommand* cmd = &wp->commandMenu;
    const int actor = activeActor(wp);
    BattleCursor* cursor;
    int i;
    if (getenv("TTYD_SL_DEBUG") != NULL) {
        for (i = 0; i < 6; i++) {
            printf("[slbtl] actor %d act %d type %d enabled %d why %d\n", actor, i, cmd->act[i].type, cmd->act[i].enabled,
                   cmd->act[i].disableReason);
        }
        fflush(stdout);
    }
    if (actor < 0 || sForced[actor].act < 0) {
        return;
    }
    cursor = &cmd->cursor[actor == 0 ? 0 : 1];
    for (i = 0; i < cursor->count; i++) {
        if (cmd->act[i].type == sForced[actor].act) {
            cursor->absPos = i;
        } else {
            cmd->act[i].enabled = 0;
            cmd->act[i].disableReason = 3;
        }
    }
}

void portBtlWeaponFilter(struct BattleWork* wpp, int type) {
    /* the weapon lists' cursors (battle_seq_command.c's _btlcmd_GetCursorPtr) */
    static const int kCursor[9] = {2, 3, 11, 12, 7, -1, 4, 5, 6};
    BattleWork* wp = (BattleWork*)wpp;
    BattleCommand* cmd = &wp->commandMenu;
    const int actor = activeActor(wp);
    BattleCursor* cursor;
    int i;
    if (getenv("TTYD_SL_DEBUG") != NULL) {
        for (i = 0; i < 6; i++) {
            printf("[slbtl] actor %d list %d: %s enabled %d why %d\n", actor, type,
                   cmd->weapon[i].weapon != NULL ? cmd->weapon[i].weapon->name : "-", cmd->weapon[i].enabled,
                   cmd->weapon[i].disableReason);
        }
        fflush(stdout);
    }
    if (actor < 0 || sForced[actor].act < 0 || sForced[actor].weapon == NULL || type < 0 || type > 8 ||
        kCursor[type] < 0) {
        return;
    }
    cursor = &cmd->cursor[kCursor[type]];
    for (i = 0; i < cursor->count; i++) {
        if (cmd->weapon[i].weapon == sForced[actor].weapon) {
            cursor->absPos = i;
            if (cursor->relPos > i || cursor->relPos + 6 <= i) {
                cursor->relPos = i > 5 ? i - 5 : 0;
            }
        } else {
            cmd->weapon[i].enabled = 0;
            cmd->weapon[i].disableReason = 3;
        }
    }
}

/* --- prompts: the game's announcement window (battle_message.c's _disp) --- */

static const char* sPrompt;
static Uint64 sPromptUntil;

void portSlPrompt(const char* tag, int ms) {
    sPrompt = ms > 0 ? tag : NULL;
    sPromptUntil = SDL_GetTicks() + (Uint64)(ms > 0 ? ms : 0);
}

static void promptDisp(CameraId cameraId, void* param) {
    const char* text = msgSearch((const char*)param);
    u16 lines = 0;
    u16 width;
    f32 x, y, w, h;
    (void)cameraId;
    if (text == NULL) {
        return;
    }
    width = FontGetMessageWidthLine(text, &lines);
    lines++;
    x = -(f32)((s32)width / 2);
    y = 170.0f;
    w = (f32)((s32)width + 20);
    h = (f32)(s32)(lines * 29 + 3);
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, x - 10.0f, -29.0f + (3.0f + y), w, h, 10.0f);
    FontDrawStart();
    FontDrawMessage((s32)x, (s32)y - 29, text);
}

/* BattleCommandDisplay: the forced command's prompt while its actor picks */
static void commandDisplayPost(BattleWork* wp) {
    const int actor = activeActor(wp);
    if (actor >= 0 && sForced[actor].act >= 0 && sForced[actor].prompt != NULL &&
        (wp->commandMenu.flags & (CMD_MENU_FLAG_ACT | CMD_MENU_FLAG_WEAPON_SELECT | CMD_MENU_FLAG_TARGET_SELECT))) {
        dispEntry(CAMERA_2D, 900.0f, 1, promptDisp, (void*)sForced[actor].prompt);
    }
}

/* seqMain: a timed prompt, each frame it lasts */
static void seqMainPost(void) {
    if (sPrompt != NULL) {
        if (SDL_GetTicks() >= sPromptUntil) {
            sPrompt = NULL;
        } else {
            dispEntry(CAMERA_2D, 900.0f, 1, promptDisp, (void*)sPrompt);
        }
    }
}

/* BattleInformationSetDropMaterial: a chapter's own battle group set up
 * without heart and flower drop tables (the game reads them unchecked) gets the defaults */
static int dropMaterialPre(FieldBattleInfo** info) {
    BattleGroupSetup* group;
    if (*info != NULL && (*info)->setup != NULL && (group = (*info)->setup->groupSetup) != NULL) {
        if (group->hpDropTable == NULL) {
            group->hpDropTable = battle_heart_drop_param_default;
        }
        if (group->fpDropTable == NULL) {
            group->fpDropTable = battle_flower_drop_param_default;
        }
    }
    return 0;
}

/* BattleEnd: what was for that battle */
static void battleEndPost(void) {
    sForced[0].act = sForced[1].act = -1;
    sDefenses = 0;
}

/* BattleInit: a new battle's part copies */
static void battleInitPost(void) {
    sDefenses = 0;
}

/* --- the scripts' functions --- */

/* in: actor (0 Luigi, 1 the partner), act (PORT_SL_ACT_*, -1 clears), weapon (or 0), prompt (a tag, or 0) */
USER_FUNC(evt_sl_force_command) {
    EvtWord* a = event->args;
    (void)isFirstCall;
    portSlForceCommand((int)evtGetValue(event, a[0]), (int)evtGetValue(event, a[1]),
                       (const struct BattleWeapon*)evtGetValue(event, a[2]), (const char*)evtGetValue(event, a[3]));
    return EVT_RETURN_DONE;
}

/* in: a tag and how long (ms; 0 hides it): a prompt over the screen */
USER_FUNC(evt_sl_prompt) {
    (void)isFirstCall;
    portSlPrompt((const char*)evtGetValue(event, event->args[0]), (int)evtGetValue(event, event->args[1]));
    return EVT_RETURN_DONE;
}

void portSlBattleRegister(void) {
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)unitEntryPost);
    portModHostAddHook(0, kSlot_BattleCalculateDamage, (void*)calculateDamagePre);
    portModHostAddHook(0, kSlot_BattlePreCheckDamage, (void*)preCheckDamagePre);
    portModHostAddHook(1, kSlot_BattleCommandDisplay, (void*)commandDisplayPost);
    portModHostAddHook(1, kSlot_seqMain, (void*)seqMainPost);
    portModHostAddHook(1, kSlot_BattleEnd, (void*)battleEndPost);
    portModHostAddHook(0, kSlot_BattleInformationSetDropMaterial, (void*)dropMaterialPre);
    portModHostAddHook(1, kSlot_BattleInit, (void*)battleInitPost);
}
