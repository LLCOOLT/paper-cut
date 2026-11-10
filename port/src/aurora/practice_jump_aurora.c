/* The Practice tab's "Jump to chapter" (port/include/port/practice_jump.h): its question, in the game's own
 * window as the game asks one (a message and a Yes / No list, Mario still), once the page is closed and
 * Mario is free in the field. Yes asks for the jump itself (practice_jump_pc.c), done on the next frame once
 * the question's script is over; No leaves the file as it was. */
#include "port/postgame_extra.h"
#include "port/practice_jump.h"
#include "port/practice_tools.h"
#include "port/mod_host.h"
#include "port/tr.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_mario.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

static int sAskPoint = -1; /* the point to ask about, -1 none (__atomic: the page asks) */
static int sAsking = -1;   /* the point the question on screen is about */

/* UTF-8 for portModsSetMessage (Latin-1 made from it); the JP release's Shift-JIS as it is (pm64_board_aurora.c) */
static int jp(void) {
    return PORT_GAME_ID[3] == 'J';
}

static const char* tx(const char* english) {
    return jp() ? portTrGame(english) : portTr(english);
}

/* text with its one "%s" or "%d" replaced by what (a language pack's text is no format) */
static void putIn(char* out, size_t size, const char* format, const char* what) {
    const char* at = strstr(format, "%s");
    if (at == NULL) {
        at = strstr(format, "%d");
    }
    if (at == NULL) {
        snprintf(out, size, "%s", format);
        return;
    }
    snprintf(out, size, "%.*s%s%s", (int)(at - format), format, what, at + 2);
}

/* the question's texts for the point, in the menu language */
static void setTexts(int point) {
    char name[96], ask[512], number[8], rows[300];
    const int chapter = portPracticeJumpChapter(point);
    snprintf(number, sizeof(number), "%d", chapter);
    putIn(name, sizeof(name), tx(portPracticeJumpName(point)), number);
    putIn(ask, sizeof(ask),
          tx("Jump to %s? Story, party and items change. Not saved until you save.<o>"),
          name);
    /* (the JP text is written broken, as the game's own; the others laid out at 30 columns, 3 lines a page) */
    portModsSetMessage("pc_jump_ask", jp() ? ask : portGameLayout(ask, 30, 3));
    snprintf(rows, sizeof(rows), "<select 0 1 %d 140>\n%s\n%s", 60 + 13 * 6, tx("Yes"), tx("No"));
    portModsSetMessage("pc_jump_yesno", rows);
}

/* in: the select's answer (0 Yes) */
USER_FUNC(evt_pc_jump_answer) {
    const int answer = evtGetValue(event, event->args[0]);
    const char* why = NULL;
    (void)isFirstCall;
    if (answer == 0 && portPracticeJumpRequest(sAsking, &why)) {
        printf("[practice] jump asked: yes, point %d\n", sAsking);
    } else {
        printf("[practice] jump asked: %s, point %d\n", answer == 0 ? portPracticeRefusalEnglish(why) : "no", sAsking);
    }
    fflush(stdout);
    sAsking = -1;
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(jump_ask)
    CALL(evt_npc_stop_for_event)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_normalize)
    CALL(evt_msg_print, 0, STRING("pc_jump_ask"), 0, 0)
    CALL(evt_msg_select, 0, STRING("pc_jump_yesno"))
    CALL(evt_msg_continue)
    CALL(evt_pc_jump_answer, LW(0))
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_npc_start_for_event)
    RETURN
EVT_END()

int portPracticeJumpAsk(int point, const char** why) {
    const char* no = point >= 0 && point < PORT_JUMP_COUNT ? portPracticeJumpRefusal() : "No such chapter.";
    if (no != NULL) {
        *why = no;
        return 0;
    }
    __atomic_store_n(&sAskPoint, point, __ATOMIC_SEQ_CST);
    return 1;
}

void portPracticeJumpAskFrame(void) {
    int point = __atomic_load_n(&sAskPoint, __ATOMIC_SEQ_CST);
    if (seqGetSeq() != SEQ_GAME) {
        sAsking = -1; /* (a map change or the title ended the question's script) */
    }
    if (point < 0) {
        return;
    }
    if (portPracticeJumpRefusal() != NULL && seqGetSeq() != SEQ_MAP_CHANGE) {
        __atomic_store_n(&sAskPoint, -1, __ATOMIC_SEQ_CST); /* a battle, the title, online: dropped */
        return;
    }
    if (sAsking >= 0 || seqGetSeq() != SEQ_GAME || seqCheckSeq() || marioStGetSystemLevel() != 0 ||
        !marioChkKey()) {
        return; /* a scene, a menu or the question already up: wait */
    }
    point = __atomic_exchange_n(&sAskPoint, -1, __ATOMIC_SEQ_CST);
    if (point < 0) {
        return;
    }
    sAsking = point;
    setTexts(point);
    {
        char english[96], number[8];
        snprintf(number, sizeof(number), "%d", portPracticeJumpChapter(point));
        putIn(english, sizeof(english), portPracticeJumpName(point), number);
        printf("[practice] jump asked: point %d (%s), in the game's window\n", point, english);
    }
    fflush(stdout);
    evtEntry(jump_ask, 10, 0);
}
