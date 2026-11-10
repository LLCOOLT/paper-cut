/* The Paper Mario 64 boss list's board (port/include/port/pm64_media.h): a signboard at the Star Haven corner of
 * Rogueport's plaza (gor_01), in the corner's back row west of Rowf, with a ROM linked (not online or in a
 * replay), once the prologue is over. Read, it lists Paper Mario's eleven boss fights (the Koopa Bros.' rematch
 * after their first: portPm64FightAt; two pages of six and five, with the page turn and Never mind: the game's
 * select windows hold eight rows), each marked beaten or with the partner it brings ("Beat their boss"), then
 * asks for a loadout; the fight is the settings page's list's (portModePm64Fight): then back to the board. */
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/pm64.h"
#include "port/pm64_foes.h"
#include "port/pm64_media.h"
#include "port/postgame_extra.h"
#include "port/tr.h"
#include "driver/mobjdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "manager/fontmgr.h"
#include "evt/evt_cmd.h"
#include "evt/evt_mario.h"
#include "evt/evt_mobj.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

#define BOARD "pc_pm64_board"
#define PER_PAGE 6

/* --- texts: UTF-8 for portModsSetMessage (Latin-1 made from it), the JP release's Shift-JIS as it is --- */

static int jp(void) {
    return PORT_GAME_ID[3] == 'J';
}

static const char* tx(const char* english) {
    return jp() ? portTrGame(english) : portTr(english);
}

static const char* partnerName(int p) {
    return jp() ? portPm64PartnerGameName(p) : portPm64PartnerName(p);
}

/* a line of the game's window: at most 30 columns, three lines a page (the JP text is written broken) */
static void setLaidOut(const char* tag, const char* text) {
    portModsSetMessage(tag, jp() ? text : portGameLayout(text, 30, 3));
}

/* the same with a heading on its own line (the layout would run it into the text after it) */
static void setLaidOutHeaded(const char* tag, const char* text) {
    const char* nl = strchr(text, '\n');
    char out[512];
    if (jp() || nl == NULL) {
        setLaidOut(tag, text);
        return;
    }
    snprintf(out, sizeof(out), "%.*s\n%s", (int)(nl - text), text, portGameLayout(nl + 1, 30, 2));
    portModsSetMessage(tag, out);
}

/* "%s and %s", "brings %s": the names put in by hand (a language pack's text is no format) */
static void putIn(char* out, size_t size, const char* format, const char* a, const char* b) {
    const char* at = strstr(format, "%s");
    const char* at2;
    if (at == NULL) {
        snprintf(out, size, "%s", format);
        return;
    }
    at2 = b != NULL ? strstr(at + 2, "%s") : NULL;
    if (at2 == NULL) {
        snprintf(out, size, "%.*s%s%s", (int)(at - format), format, a, at + 2);
    } else {
        snprintf(out, size, "%.*s%s%.*s%s%s", (int)(at - format), format, a, (int)(at2 - at - 2), at + 2, b, at2 + 2);
    }
}

/* a boss's row: its name, then "(beaten)" or "(brings Kooper)" */
static void bossRow(int f, char* out, size_t size) {
    char names[96], brings[128];
    int who[2];
    const int n = portPm64BossBrings(f, who);
    char name[64];
    snprintf(name, sizeof(name), "%s", tx(portPm64FightEnglish(f))); /* (kept: the texts below are looked up after) */
    if (n > 0) {
        char a[32], b[32];
        snprintf(a, sizeof(a), "%s", partnerName(who[0]));
        snprintf(b, sizeof(b), "%s", n > 1 ? partnerName(who[1]) : "");
        if (n > 1) {
            putIn(names, sizeof(names), tx("%s and %s"), a, b);
        } else {
            snprintf(names, sizeof(names), "%s", a);
        }
        putIn(brings, sizeof(brings), tx("brings %s"), names, NULL);
        snprintf(out, size, "%s (%s)", name, brings);
    } else if (portPm64BossBeaten(f)) {
        snprintf(out, size, "%s (%s)", name, tx("beaten"));
    } else {
        snprintf(out, size, "%s", name);
    }
}

/* "<select default cancel width top>" and a row a line (cancel: the last row) */
static void setSelect(const char* tag, char rows[][160], int n) {
    static char sText[1400];
    int i, widest = 0, at;
    for (i = 0; i < n; i++) {
        const int len = (int)FontGetMessageWidth(rows[i]);
        widest = len > widest ? len : widest;
    }
    /* (its top at y 89, the 2D units: under the question's last line, the bubble's third with its heading
     * ("Each one on its own, then back here..." laid out in two at most), which the game's 140 covered; eight
     * rows still end at the screen's bottom. As wide as its longest row as the font measures it (a guess by
     * letters let "Koopa Bros. (rematch) (brings Bombette)" touch the edge) and 40 on each side, as the game's
     * own widths (msgdrv: the widest row and 80) and 30 more, the drawn row running a little past the measure, up
     * to 620, the 4:3 screen less a margin) */
    at = snprintf(sText, sizeof(sText), "<select 0 %d %d 89>", n - 1, widest + 110 > 620 ? 620 : widest + 110);
    for (i = 0; i < n && at < (int)sizeof(sText); i++) {
        at += snprintf(sText + at, sizeof(sText) - (size_t)at, "\n%s", rows[i]);
    }
    portModsSetMessage(tag, sText);
}

/* the fights on a page (the second has the rest) */
static int onPage(int page) {
    const int n = PM64_BOSS_FIGHTS - page * PER_PAGE;
    return n < 0 ? 0 : n > PER_PAGE ? PER_PAGE : n;
}

/* the board's texts as the list is now (read again at each reading) */
USER_FUNC(evt_pc_board_texts) {
    char rows[PER_PAGE + 2][160];
    int page, i, n;
    (void)isFirstCall;
    (void)event;
    setLaidOutHeaded("pc_pm64_board_ask", tx("PAPER MARIO 64'S BOSSES\nEach one on its own, then back here. Who will you fight?<o>"));
    for (page = 0; page < 2; page++) {
        n = onPage(page);
        for (i = 0; i < n; i++) {
            bossRow(portPm64FightAt(page * PER_PAGE + i), rows[i], sizeof(rows[i]));
        }
        snprintf(rows[n], sizeof(rows[0]), "%s", tx(page == 0 ? "More..." : "Back..."));
        snprintf(rows[n + 1], sizeof(rows[0]), "%s", tx("Never mind"));
        setSelect(page == 0 ? "pc_pm64_board_p0" : "pc_pm64_board_p1", rows, n + 2);
    }
    setLaidOut("pc_pm64_board_how", tx("How will you fight?<o>"));
    snprintf(rows[0], sizeof(rows[0]), "%s", tx("As you are"));
    snprintf(rows[1], sizeof(rows[1]), "%s", tx("Mid-game loadout"));
    snprintf(rows[2], sizeof(rows[2]), "%s", tx("Late game loadout"));
    snprintf(rows[3], sizeof(rows[3]), "%s", tx("Never mind"));
    setSelect("pc_pm64_board_load", rows, 4);
    /* a boss with two versions (portPm64FightHasEarly: Jr. Troopa): his last fight first, the default */
    setLaidOut("pc_pm64_board_ver", tx("Which of his fights?<o>"));
    snprintf(rows[0], sizeof(rows[0]), "%s", tx("His last fight"));
    snprintf(rows[1], sizeof(rows[1]), "%s", tx("His first fight"));
    snprintf(rows[2], sizeof(rows[2]), "%s", tx("Never mind"));
    setSelect("pc_pm64_board_vers", rows, 3);
    return EVT_RETURN_DONE;
}

/* in: the fight; out: 1 it has a first fight to choose (portPm64FightHasEarly) */
USER_FUNC(evt_pc_board_has_early) {
    EvtWord* args = event->args;
    (void)isFirstCall;
    evtSetValue(event, args[1], portPm64FightHasEarly(evtGetValue(event, args[0])));
    return EVT_RETURN_DONE;
}

/* in: the page, the row chosen; out: the fight (0..PM64_BOSS_FIGHTS-1), -1 none, -2 the other page (the page
 * turned) */
USER_FUNC(evt_pc_board_pick) {
    EvtWord* args = event->args;
    const int page = evtGetValue(event, args[0]) != 0;
    const int row = evtGetValue(event, args[1]);
    const int n = onPage(page);
    (void)isFirstCall;
    if (row == n) {
        evtSetValue(event, args[0], !page);
        evtSetValue(event, args[2], -2);
    } else if (row >= 0 && row < n) {
        evtSetValue(event, args[2], portPm64FightAt(page * PER_PAGE + row));
    } else {
        evtSetValue(event, args[2], -1);
    }
    return EVT_RETURN_DONE;
}

/* in: the boss, the loadout row, 1 its first fight; out: 0 asked for (it starts once the board's text is closed), 1
 * not now (its reason in pc_pm64_board_why) */
USER_FUNC(evt_pc_board_fight) {
    EvtWord* args = event->args;
    const int f = evtGetValue(event, args[0]);
    const int loadout = evtGetValue(event, args[1]);
    const int early = evtGetValue(event, args[3]);
    const char* why = NULL;
    (void)isFirstCall;
    if (portModePm64FightAs(f, loadout, early, &why)) {
        printf("[pm64] board: %s, loadout %d%s\n", portPm64FightEnglish(f), loadout, early ? ", first fight" : "");
        fflush(stdout);
        evtSetValue(event, args[2], 0);
        return EVT_RETURN_DONE;
    }
    {
        char text[256];
        snprintf(text, sizeof(text), "%s<k>", tx(why != NULL ? why : "Not now."));
        setLaidOut("pc_pm64_board_why", text);
    }
    evtSetValue(event, args[2], 1);
    return EVT_RETURN_DONE;
}

/* LW(10) the page, LW(11) the boss, LW(0) the select's answer */
static EVT_BEGIN(board_read)
    CALL(evt_npc_stop_for_event)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_pc_board_texts)
    SET_VALUE(LW(10), 0)
    LABEL(0)
    CALL(evt_msg_print, 0, STRING("pc_pm64_board_ask"), 0, 0)
    IF_INT_EQUAL(LW(10), 0)
        CALL(evt_msg_select, 0, STRING("pc_pm64_board_p0"))
    ELSE
        CALL(evt_msg_select, 0, STRING("pc_pm64_board_p1"))
    END_IF
    CALL(evt_pc_board_pick, LW(10), LW(0), LW(11))
    IF_INT_EQUAL(LW(11), -2)
        CALL(evt_msg_continue)
        GOTO(0)
    END_IF
    IF_INT_EQUAL(LW(11), -1)
        CALL(evt_msg_continue)
        GOTO(9)
    END_IF
    CALL(evt_msg_continue)
    /* LW(12): his first fight (a boss with two: Jr. Troopa) */
    SET_VALUE(LW(12), 0)
    CALL(evt_pc_board_has_early, LW(11), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_msg_print, 0, STRING("pc_pm64_board_ver"), 0, 0)
        CALL(evt_msg_select, 0, STRING("pc_pm64_board_vers"))
        CALL(evt_msg_continue)
        IF_INT_GREATER_OR_EQUAL(LW(0), 2)
            GOTO(9)
        END_IF
        SET_VALUE(LW(12), LW(0))
    END_IF
    CALL(evt_msg_print, 0, STRING("pc_pm64_board_how"), 0, 0)
    CALL(evt_msg_select, 0, STRING("pc_pm64_board_load"))
    CALL(evt_msg_continue)
    IF_INT_GREATER_OR_EQUAL(LW(0), 3)
        GOTO(9)
    END_IF
    CALL(evt_pc_board_fight, LW(11), LW(0), LW(1), LW(12))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_msg_print, 0, STRING("pc_pm64_board_why"), 0, 0)
    END_IF
    LABEL(9)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_npc_start_for_event)
    RETURN
EVT_END()

/* in the corner's back row, west of Rowf (358, 0, 415; pm64_badges_aurora.c), where its partners once stood */
static EVT_BEGIN(board_spawn)
    CALL(evt_mobj_signboard, STRING(BOARD), 270, 0, 405, PTR(board_read), 0)
    RETURN
EVT_END()

static int boardOn(void) {
    return portPm64BossesReady() && swByteGet(0) >= STORY_STG1_00_PETAL_MEADOWS;
}

static int sAsked; /* this visit's board asked for */

static void frameHook(void) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || strcmp(gp->currentMapName, "gor_01") != 0) {
        if (seqGetSeq() != SEQ_GAME || (gp != NULL && strcmp(gp->currentMapName, "gor_01") != 0)) {
            sAsked = 0; /* (a battle in the plaza keeps the room's objects) */
        }
        return;
    }
    if (sAsked || seqCheckSeq() || !boardOn() || mobjNameToPtrNoAssert(BOARD) != NULL) {
        return;
    }
    evtEntry(board_spawn, 10, 0);
    sAsked = 1;
}

void portPm64BoardInit(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
