/* Paper Mario 64's badges on Aurora (port/include/port/pm64_media.h, port/src/common/pm64_badges_pc.c):
 * their names and descriptions from the linked ROM as the game's messages, Dizzy Stomp's, and Rowf, who
 * sells them at the Star Haven corner of Rogueport's plaza (gor_01, beside Goombario's spot), drawn as his
 * Paper Mario sprite over a Toad (gPortPm64RowfPose). With "pm64_badges" on and a ROM linked. */
#include "port/battlex.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_pouch.h"
#include "evt/evt_story.h"
#include "evt/evt_window.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* --- texts --- */

/* a description as the game's: lines of at most cols letters, broken between words */
static void wrap(const char* in, char* out, size_t size, int cols) {
    size_t n = 0;
    int line = 0;
    while (*in != '\0' && n + 2 < size) {
        const char* w;
        int len;
        while (*in == ' ' || *in == '\n') {
            in++;
        }
        w = in;
        while (*in != '\0' && *in != ' ' && *in != '\n') {
            in++;
        }
        len = (int)(in - w);
        if (len == 0) {
            break;
        }
        if (line > 0 && line + 1 + len > cols) {
            out[n++] = '\n';
            line = 0;
        } else if (line > 0) {
            out[n++] = ' ';
            line++;
        }
        if (n + (size_t)len + 1 >= size) {
            break;
        }
        memcpy(out + n, w, (size_t)len);
        n += (size_t)len;
        line += len;
    }
    out[n] = '\0';
}

/* a message's text for portModsSetMessage, which takes UTF-8 (the US and EU releases' Latin-1 made from it) and
 * the JP release's Shift-JIS as it is: portTrGame's Latin-1 there was read as UTF-8 (German "Münzen" broke) */
static const char* msgTr(const char* english) {
    return PORT_GAME_ID[3] == 'J' ? portTrGame(english) : portTr(english);
}

/* The ROM unlinked (a save made with it, played on): the badges it holds keep their effects (pm64_badges_pc.c),
 * named in English with a note, their icons their TTYD look-alikes' (portPm64BadgesFill) */
static void setStandInTexts(void) {
    char wrapped[300];
    int i;
    wrap(msgTr("A Paper Mario badge. Link your Paper Mario ROM again to see it as it is."), wrapped, sizeof(wrapped),
         30);
    for (i = 0; i < portPm64BadgeCount(); i++) {
        const int id = PORT_PM64_BADGE_MIN + i;
        portModsSetMessage(itemDataTable[id].name, portPm64BadgeEnglish(id));
        portModsSetMessage(itemDataTable[id].description, wrapped);
    }
    portModsSetMessage("pc_pm64_dizzy_stomp", portPm64BadgeEnglish(PORT_PM64_BADGE_DIZZY_STOMP));
    portModsSetMessage("pc_pm64_dizzy_stomp_desc", wrapped);
}

static void setTexts(void) {
    char name[64], desc[256], wrapped[300];
    int i;
    Pm64Move m;
    for (i = 0; i < portPm64BadgeCount(); i++) {
        const int id = PORT_PM64_BADGE_MIN + i;
        if (!portPm64BadgeText(id, name, sizeof(name), desc, sizeof(desc))) {
            snprintf(name, sizeof(name), "%s", portPm64BadgeEnglish(id));
            desc[0] = '\0';
        }
        wrap(desc, wrapped, sizeof(wrapped), 30);
        portModsSetMessage(itemDataTable[id].name, name);
        portModsSetMessage(itemDataTable[id].description, wrapped);
    }
    /* Dizzy Stomp in Sleepy Stomp's place in the battle menu: the move's battle description */
    portPm64BadgeText(PORT_PM64_BADGE_DIZZY_STOMP, name, sizeof(name), desc, sizeof(desc));
    if (portPm64Move(34, &m) && m.shortDescMsg != 0) {
        portPm64MessageText(m.shortDescMsg, 0, desc, sizeof(desc));
    }
    wrap(desc, wrapped, sizeof(wrapped), 30);
    portModsSetMessage("pc_pm64_dizzy_stomp", name);
    portModsSetMessage("pc_pm64_dizzy_stomp_desc", wrapped);
    /* Rowf's lines (Paper Cut's own: the ROM's are his Toad Town shop's) */
    portModsSetMessage("pc_pm64_rowf_00", msgTr("Hello there! I'm Rowf. I've come\nall the way from Toad Town\nwith my badges.<p>"
                                                     "Badges like these you won't\nfind anywhere around here!\nHave a look!<k>"));
    portModsSetMessage("pc_pm64_rowf_01", msgTr("Come again!<k>"));
    portModsSetMessage("pc_pm64_rowf_02", msgTr("Oh, you don't have\nenough coins.<k>"));
    portModsSetMessage("pc_pm64_rowf_03", msgTr("You can't carry\nany more badges!<k>"));
    portModsSetMessage("pc_pm64_rowf_04", msgTr("Thank you! Anything else?<k>"));
    portModsSetMessage("pc_pm64_rowf_05", msgTr("You can't carry\nany more items!<k>"));
}

/* Tayce T.'s recipes' names and descriptions: the ROM's; with a ROM linked before the item table was read, or
 * none (a save holding them), their English names and what they restore */
static void setFoodTexts(void) {
    char name[64], desc[256], wrapped[300];
    int i;
    for (i = 0; i < portPm64FoodCount(); i++) {
        const int id = PORT_PM64_FOOD_MIN + i;
        int hp, fp;
        if (!portPm64FoodText(id, name, sizeof(name), desc, sizeof(desc)) || desc[0] == '\0') {
            const int kind = portPm64FoodRestores(id, &hp, &fp);
            if (name[0] == '\0') {
                snprintf(name, sizeof(name), "%s", portPm64FoodEnglish(id));
            }
            if (kind == 2) {
                snprintf(desc, sizeof(desc), "%s", msgTr("Throw it to damage an enemy."));
            } else if (kind == 3) {
                snprintf(desc, sizeof(desc), "%s", msgTr("Electrifies you, hides you or puts you to sleep."));
            } else if (hp > 0 && fp > 0) {
                snprintf(desc, sizeof(desc), msgTr("Restores %d HP and %d FP."), hp, fp);
            } else if (hp > 0) {
                snprintf(desc, sizeof(desc), msgTr("Restores %d HP."), hp);
            } else {
                snprintf(desc, sizeof(desc), msgTr("Restores %d FP."), fp);
            }
        }
        wrap(desc, wrapped, sizeof(wrapped), 30);
        portModsSetMessage(itemDataTable[id].name, name);
        portModsSetMessage(itemDataTable[id].description, wrapped);
    }
}

/* --- Rowf --- */

#define ROWF "pc_pm64_rowf"

USER_FUNC(evt_pc_rowf_list) {
    (void)isFirstCall;
    gPortMerchantStockHook = evtGetValue(event, event->args[0]) ? portPm64MerchantStock : NULL;
    return EVT_RETURN_DONE;
}

USER_FUNC(evt_pc_rowf_stock) {
    int stock[PORT_PM64_BADGE_COUNT + PORT_PM64_FOOD_COUNT];
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portPm64MerchantStock(stock, PORT_PM64_BADGE_COUNT + PORT_PM64_FOOD_COUNT));
    return EVT_RETURN_DONE;
}

/* the item bought is one of Tayce T.'s ingredients (with the bag full: "items", not "badges") */
USER_FUNC(evt_pc_rowf_is_food) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], portPm64FoodIs(evtGetValue(event, event->args[0])));
    return EVT_RETURN_DONE;
}

/* Rowf comes for his badges or Tayce T.'s ingredients */
static int rowfOn(void) {
    return portPm64BadgesActive() || portPm64FoodActive();
}

static EVT_BEGIN(rowf_talk)
    CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_00"), 0, STRING(ROWF))
    CALL(evt_win_coin_on, 0, LW(8))
    LABEL(0)
    CALL(evt_pc_rowf_stock, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_01"), 0, STRING(ROWF))
        RETURN
    END_IF
    CALL(evt_pc_rowf_list, 1)
    CALL(evt_win_other_select, PORT_SELECT_MERCHANT)
    CALL(evt_pc_rowf_list, 0)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_01"), 0, STRING(ROWF))
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(3))
        CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_02"), 0, STRING(ROWF))
        GOTO(0)
    END_IF
    CALL(evt_pouch_add_item, LW(1), LW(0))
    IF_INT_EQUAL(LW(0), -1)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_pc_rowf_is_food, LW(1), LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_05"), 0, STRING(ROWF))
        ELSE
            CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_03"), 0, STRING(ROWF))
        END_IF
        RETURN
    END_IF
    MULTIPLY(LW(3), -1)
    CALL(evt_pouch_add_coin, LW(3))
    CALL(evt_win_coin_wait, LW(8))
    CALL(evt_msg_print, 0, STRING("pc_pm64_rowf_04"), 0, STRING(ROWF))
    GOTO(0)
    RETURN
EVT_END()

static NpcSetupInfo sRowfSetup[] = {
    {
        ROWF,                                                                    // name
        NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_MOVE_ANIM | NPC_FLAG_TALK_ANIM, // flags
        0,                                                                       // reactionFlags
        NULL,                                                                    // initEvent
        NULL,                                                                    // regularEvent
        rowf_talk,                                                               // talkEvent
    },
    {NULL},
};

/* (a Toad's model underneath, drawn as Rowf: gPortPm64RowfPose) at the east end of the corner's back row (pm64_aurora.c) */
static EVT_BEGIN(rowf_spawn)
    CALL(evt_npc_entry, STRING(ROWF), STRING("c_kinopio"))
    CALL(evt_npc_set_tribe, STRING(ROWF), STRING("\x83\x4C\x83\x6D\x83\x73\x83\x49"))
    CALL(evt_npc_set_position, STRING(ROWF), 358, 0, 415)
    CALL(evt_npc_set_ry, STRING(ROWF), 270)
    CALL(evt_npc_setup, PTR(sRowfSetup))
    CALL(evt_npc_set_anim, STRING(ROWF), STRING("S_1"))
    RETURN
EVT_END()

static int sRowfAsked;

/* Before the frame (marioStMain's pre hook): the badges turned off or the ROM unlinked in the room, Rowf gone (a
 * Toad, his model, stood there). Not after the frame's npcMain: his draw is queued by then, and a deleted NPC's
 * pose drawn is a crash. Hidden while Mario isn't free to move, deleted once he is. */
static int rowfGone(void) {
    NpcEntry* npc;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || strcmp(gp->currentMapName, "gor_01") != 0 ||
        rowfOn() || (npc = npcNameToPtr_NoAssert(ROWF)) == NULL) {
        return 0;
    }
    if (marioChkCtrl()) {
        npcDeleteGroup(npc);
    } else {
        npc->flags |= NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW;
    }
    return 0; /* (a pre hook: nonzero would skip marioStMain) */
}

static void rowfFrame(void) {
    NpcEntry* npc;
    gPortPm64RowfPose = -1;
    if (gp->inBattle || strcmp(gp->currentMapName, "gor_01") != 0) {
        sRowfAsked = 0;
        return;
    }
    if ((npc = npcNameToPtr_NoAssert(ROWF)) != NULL) {
        /* his pose drawn as him for as long as he stands, whatever the sequence: a battle's start before it
           loads, its end, a scene change's last frames (once only in the field: the Toad underneath showed then);
           without a ROM, nothing until he is gone (portPm64MediaDrawPose) */
        gPortPm64RowfPose = npc->poseId;
        if (seqGetSeq() != SEQ_GAME || !rowfOn()) {
            sRowfAsked = 0;
            return; /* (turned off: gone at the next frame's start, rowfGone) */
        }
        return;
    }
    if (seqGetSeq() != SEQ_GAME) {
        sRowfAsked = 0;
        return;
    }
    if (!rowfOn()) {
        sRowfAsked = 0; /* (on again in the room: he comes) */
    }
    if (sRowfAsked || !rowfOn() || swByteGet(0) < STORY_STG1_00_PETAL_MEADOWS) {
        return;
    }
    evtEntry(rowf_spawn, 10, 0);
    sRowfAsked = 1;
}

/* --- the frame --- */

static void frameHook(void) {
    static unsigned sGen = ~0u;
    static int sLang = -1;
    if (portPm64Linked() == PM64_NONE) {
        if (sGen != portPm64Generation() || sLang != portTrLanguage()) {
            portPm64BadgesFill(); /* (their icons the game's again) */
            portPm64FoodFill();
            setFoodTexts(); /* (a save's Paper Mario dishes keep a name) */
            setStandInTexts(); /* (and its badges) */
            sGen = portPm64Generation();
            sLang = portTrLanguage();
        }
        portPm64BadgesBattleFrame(); /* (Sleepy Stomp's weapon back) */
        if (gp != NULL) {
            rowfFrame(); /* (Rowf gone if he's here; drawn as nothing till then, never the Toad) */
        }
        return;
    }
    portPm64BadgesFill();
    portPm64FoodFill();
    if (sGen != portPm64Generation() || sLang != portTrLanguage()) {
        sGen = portPm64Generation();
        sLang = portTrLanguage();
        setTexts();
        setFoodTexts();
    }
    portPm64BadgesBattleFrame();
    portPm64FoodBattleFrame();
    rowfFrame();
}

void portPm64BadgesInit(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_marioStMain, (void*)rowfGone);
}
