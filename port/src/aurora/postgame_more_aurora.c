/* The rest of the post-game (port/include/port/postgame_extra.h), each with
 * its own setting, off by default, and none online or in a replay:
 *
 *   - Second-fight Tattles ("pg_second_tattles"): Doopliss's second fight in
 *     Twilight Town and the Shadow Queen's true form can be Tattled, in
 *     Paper Cut's own words (the game has none for them).
 *   - The Boo quiz ("pg_boo_quiz"): after the story, a Boo in Creepy
 *     Steeple's hall asks 15 questions of Paper Cut's own; all right, and
 *     the first time a prize.
 *   - Newsletter Trouble ("pg_newsletter"): after chapter 7, the Rogueport
 *     paper's editor in the plaza needs three newsletters taken to Petalburg,
 *     Glitzville and Twilight Town; a reward when they're all delivered.
 *   - Pianta tokens for coins ("pg_buy_tokens"): a Pianta in the Pianta
 *     Parlor sells tokens, 50 for 150 coins, or in bulk.
 *
 * Every NPC here is one of the game's models (a Boo, a Toad, a Pianta) with
 * Paper Cut's own lines, set beside the game's own in their rooms. */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_pouch.h"
#include "evt/evt_window.h"
#include "evt/evt_yuugijou.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <string.h>

static int on(const char* key) {
    int* v = portSettingsDeclare(key, 0, 0, 1);
    return v != NULL && *v != 0 && !portStateSyncLinked() && !portReplayActive();
}

/* --- second-fight Tattles ---------------------------------------------------- */

static void secondTattles(void) {
    static int sWas = -1;
    const int now = on("pg_second_tattles");
    if (now == sWas) {
        return;
    }
    sWas = now;
    battleGetUnitMonosiriPtr(UNIT_DOOPLISS_CHAPTER4_FIGHT2)->tattleBtlTag = now ? "pc_pg_tattle_doopliss2" : NULL;
    battleGetUnitMonosiriPtr(0x95)->tattleBtlTag = now ? "pc_pg_tattle_queen2" : NULL; /* the Shadow Queen, her true form */
    if (now) {
        printf("[postgame] second-fight Tattles: Doopliss and the Shadow Queen's own\n");
        fflush(stdout);
    }
}

/* --- the Boo quiz -------------------------------------------------------------- */

#define QUIZ "pg_quiz_boo"
#define QUESTIONS 15

/* each question's right answer (0-2): the order its options are written in */
static const int kRight[QUESTIONS] = {0, 1, 2, 0, 2, 1, 0, 1, 2, 2, 0, 1, 1, 0, 2};
static int sRight, sBest;

int portBooQuizBest(void) {
    return sBest;
}

/* in: the question (0-14) and the option picked; counts the right ones */
USER_FUNC(evt_pg_quiz_answer) {
    const int q = evtGetValue(event, event->args[0]);
    const int pick = evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (q == 0) {
        sRight = 0;
    }
    const int right = q >= 0 && q < QUESTIONS && pick == kRight[q]; /* (q outside the table: wrong, never read) */
    if (right) {
        sRight++;
    }
    evtSetValue(event, event->args[2], right);
    return EVT_RETURN_DONE;
}

/* out: how many were right, whether the prize is given now (all right, the first time) */
USER_FUNC(evt_pg_quiz_end) {
    (void)isFirstCall;
    if (sRight > sBest) {
        sBest = sRight;
    }
    evtSetValue(event, event->args[0], sRight);
    if (sRight == QUESTIONS && !swGet(PORT_GSWF_PG_QUIZ_WON)) {
        swSet(PORT_GSWF_PG_QUIZ_WON);
        evtSetValue(event, event->args[1], 1);
    } else {
        evtSetValue(event, event->args[1], 0);
    }
    return EVT_RETURN_DONE;
}

/* the question's message tags, by number (LW(10)): pc_pg_quiz_qNN and pc_pg_quiz_aNN */
USER_FUNC(evt_pg_quiz_tags) {
    static char sQ[24], sA[24];
    const int q = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    snprintf(sQ, sizeof(sQ), "pc_pg_quiz_q%02d", q);
    snprintf(sA, sizeof(sA), "pc_pg_quiz_a%02d", q);
    evtSetValue(event, event->args[1], (EvtWord)sQ);
    evtSetValue(event, event->args[2], (EvtWord)sA);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(quiz_talk)
    CALL(evt_msg_print, 0, STRING("pc_pg_quiz_00"), 0, STRING(QUIZ))
    CALL(evt_msg_select, 0, STRING("pc_pg_yesno"))
    CALL(evt_msg_continue)
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, 0, STRING("pc_pg_quiz_01"), 0, STRING(QUIZ))
        RETURN
    END_IF
    SET_VALUE(LW(10), 0)
    LABEL(1)
    CALL(evt_pg_quiz_tags, LW(10), LW(11), LW(12))
    CALL(evt_msg_print, 0, LW(11), 0, STRING(QUIZ))
    CALL(evt_msg_select, 0, LW(12))
    CALL(evt_msg_continue)
    CALL(evt_pg_quiz_answer, LW(10), LW(0), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_msg_print, 0, STRING("pc_pg_quiz_right"), 0, STRING(QUIZ))
    ELSE
        CALL(evt_msg_print, 0, STRING("pc_pg_quiz_wrong"), 0, STRING(QUIZ))
    END_IF
    ADD(LW(10), 1)
    IF_INT_LESS(LW(10), QUESTIONS)
        GOTO(1)
    END_IF
    CALL(evt_pg_quiz_end, LW(0), LW(1))
    IF_INT_EQUAL(LW(0), QUESTIONS)
        IF_INT_EQUAL(LW(1), 1)
            CALL(evt_msg_print, 0, STRING("pc_pg_quiz_prize"), 0, STRING(QUIZ))
            CALL(evt_pouch_add_coin, 50)
            CALL(evt_pouch_add_item, ITEM_BOOS_SHEET, LW(0))
        ELSE
            CALL(evt_msg_print, 0, STRING("pc_pg_quiz_again"), 0, STRING(QUIZ))
        END_IF
    ELSE
        CALL(evt_msg_print, 0, STRING("pc_pg_quiz_some"), 0, STRING(QUIZ))
    END_IF
    RETURN
EVT_END()

/* --- Newsletter Trouble ---------------------------------------------------------- */

#define EDITOR "pg_news_editor"
#define READER "pg_news_reader"

int portNewsletterStep(void) {
    if (gp == NULL || !swGet(PORT_GSWF_PG_NEWS_TAKEN)) {
        return 0;
    }
    if (swGet(PORT_GSWF_PG_NEWS_DONE)) {
        return 5;
    }
    return 1 + (swGet(PORT_GSWF_PG_NEWS_1) != 0) + (swGet(PORT_GSWF_PG_NEWS_2) != 0) + (swGet(PORT_GSWF_PG_NEWS_3) != 0);
}

/* out: 0 not taken, 1 taken (some left), 2 all delivered, 3 rewarded */
USER_FUNC(evt_pg_news_state) {
    int state = 0;
    (void)isFirstCall;
    if (swGet(PORT_GSWF_PG_NEWS_DONE)) {
        state = 3;
    } else if (swGet(PORT_GSWF_PG_NEWS_TAKEN)) {
        state = swGet(PORT_GSWF_PG_NEWS_1) && swGet(PORT_GSWF_PG_NEWS_2) && swGet(PORT_GSWF_PG_NEWS_3) ? 2 : 1;
    }
    evtSetValue(event, event->args[0], state);
    return EVT_RETURN_DONE;
}

USER_FUNC(evt_pg_news_set) {
    (void)isFirstCall;
    swSet(evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* in: this room's reader's flag; out: 1 if already delivered, 2 if the papers aren't taken */
USER_FUNC(evt_pg_news_reader) {
    const int flag = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], !swGet(PORT_GSWF_PG_NEWS_TAKEN) ? 2 : swGet(flag) ? 1 : 0);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(editor_talk)
    CALL(evt_pg_news_state, LW(0))
    SWITCH(LW(0))
        CASE_EQUAL(0)
            CALL(evt_msg_print, 0, STRING("pc_pg_news_00"), 0, STRING(EDITOR))
            CALL(evt_msg_select, 0, STRING("pc_pg_yesno"))
            CALL(evt_msg_continue)
            IF_INT_EQUAL(LW(0), 0)
                CALL(evt_pg_news_set, PORT_GSWF_PG_NEWS_TAKEN)
                CALL(evt_msg_print, 0, STRING("pc_pg_news_01"), 0, STRING(EDITOR))
            ELSE
                CALL(evt_msg_print, 0, STRING("pc_pg_news_02"), 0, STRING(EDITOR))
            END_IF
        CASE_EQUAL(1)
            CALL(evt_msg_print, 0, STRING("pc_pg_news_03"), 0, STRING(EDITOR))
        CASE_EQUAL(2)
            CALL(evt_msg_print, 0, STRING("pc_pg_news_04"), 0, STRING(EDITOR))
            CALL(evt_pg_news_set, PORT_GSWF_PG_NEWS_DONE)
            CALL(evt_pouch_add_coin, 100)
            CALL(evt_pouch_add_item, ITEM_JAMMIN_JELLY, LW(0))
        CASE_DEFAULT
            CALL(evt_msg_print, 0, STRING("pc_pg_news_05"), 0, STRING(EDITOR))
    END_SWITCH
    RETURN
EVT_END()

static int sReaderFlag;

USER_FUNC(evt_pg_news_reader_flag) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sReaderFlag);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(reader_talk)
    CALL(evt_pg_news_reader_flag, LW(5))
    CALL(evt_pg_news_reader, LW(5), LW(0))
    SWITCH(LW(0))
        CASE_EQUAL(0)
            CALL(evt_msg_print, 0, STRING("pc_pg_news_10"), 0, STRING(READER))
            CALL(evt_pg_news_set, LW(5))
        CASE_EQUAL(1)
            CALL(evt_msg_print, 0, STRING("pc_pg_news_11"), 0, STRING(READER))
        CASE_DEFAULT
            CALL(evt_msg_print, 0, STRING("pc_pg_news_12"), 0, STRING(READER))
    END_SWITCH
    RETURN
EVT_END()

/* --- Pianta tokens --------------------------------------------------------------- */

#define SELLER "pg_token_seller"

/* in: the choice (0-2); out: the coins it costs (0: a choice to leave), the tokens */
USER_FUNC(evt_pg_token_deal) {
    /* (the bulk row is 300 for 900 coins: coins stop at 999, so its 500 for 1500 could never be bought) */
    static const int kTokens[3] = {50, 250, 300};
    const int pick = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], pick >= 0 && pick < 3 ? kTokens[pick] * 3 : 0);
    evtSetValue(event, event->args[2], pick >= 0 && pick < 3 ? kTokens[pick] : 0);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(seller_talk)
    CALL(evt_msg_print, 0, STRING("pc_pg_tokens_00"), 0, STRING(SELLER))
    CALL(evt_win_coin_on, 0, LW(8))
    CALL(evt_msg_select, 0, STRING("pc_pg_tokens_sel"))
    CALL(evt_msg_continue)
    CALL(evt_pg_token_deal, LW(0), LW(1), LW(2))
    IF_INT_EQUAL(LW(1), 0)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_pg_tokens_01"), 0, STRING(SELLER))
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(1))
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_pg_tokens_02"), 0, STRING(SELLER))
        RETURN
    END_IF
    MULTIPLY(LW(1), -1)
    CALL(evt_pouch_add_coin, LW(1))
    CALL(evt_win_coin_wait, LW(8))
    CALL(evt_win_coin_off, LW(8))
    CALL(yuugijou_add_montemedal, 0, LW(2))
    CALL(evt_msg_print, 0, STRING("pc_pg_tokens_03"), 0, STRING(SELLER))
    RETURN
EVT_END()

/* --- the NPCs, set in their rooms ------------------------------------------------- */

static NpcSetupInfo sQuizSetup[] = {
    {QUIZ, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_TALK_ANIM, 0, NULL, NULL, quiz_talk},
    {NULL},
};
static NpcSetupInfo sEditorSetup[] = {
    {EDITOR, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_TALK_ANIM, 0, NULL, NULL, editor_talk},
    {NULL},
};
static NpcSetupInfo sReaderSetup[] = {
    {READER, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_TALK_ANIM, 0, NULL, NULL, reader_talk},
    {NULL},
};
static NpcSetupInfo sSellerSetup[] = {
    {SELLER, NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_TALK_ANIM, 0, NULL, NULL, seller_talk},
    {NULL},
};

/* LW(0) the name, LW(1) the model, LW(2) the tribe (the game's Shift-JIS), LW(3)-LW(5) the place,
 * LW(6) the setup, LW(7) its standing animation */
static EVT_BEGIN(npc_spawn)
    CALL(evt_npc_entry, LW(0), LW(1))
    CALL(evt_npc_set_tribe, LW(0), LW(2))
    CALL(evt_npc_set_position, LW(0), LW(3), LW(4), LW(5))
    CALL(evt_npc_setup, LW(6))
    CALL(evt_npc_set_anim, LW(0), LW(7))
    RETURN
EVT_END()

typedef struct Spot {
    const char* map;
    const char* name;
    const char* model;
    const char* tribe;
    int x, y, z;
    NpcSetupInfo* setup;
    const char* anim;
} Spot;

static const Spot kSpots[] = {
    /* Creepy Steeple's hall: a Boo ("テレサ") */
    {"jin_00", QUIZ, "c_teresa", "\x83\x65\x83\x8C\x83\x54", 60, 20, 40, sQuizSetup, "S_1"},
    /* Rogueport's plaza: the editor, a Toad ("キノピオ") */
    {"gor_01", EDITOR, "c_kinopio", "\x83\x4C\x83\x6D\x83\x73\x83\x49", 200, 0, 160, sEditorSetup, "KNP_S_1"},
    /* the readers: Petalburg, Glitzville, Twilight Town */
    {"nok_00", READER, "c_kinopio", "\x83\x4C\x83\x6D\x83\x73\x83\x49", 60, 0, 120, sReaderSetup, "KNP_S_1"},
    /* (Glitzville's plaza: by the other Toad, clear of the ticket booth at the middle) */
    {"tou_01", READER, "c_kinopio", "\x83\x4C\x83\x6D\x83\x73\x83\x49", -250, 0, 160, sReaderSetup, "KNP_S_1"},
    {"usu_00", READER, "c_kinopio", "\x83\x4C\x83\x6D\x83\x73\x83\x49", 60, 0, 120, sReaderSetup, "KNP_S_1"},
    /* the Pianta Parlor: a Pianta ("マフィア") */
    {"gor_03", SELLER, "c_m_buka", "\x83\x7D\x83\x74\x83\x42\x83\x41", -150, 155, -80, sSellerSetup, "S_1"},
};
#define SPOTS ((int)(sizeof(kSpots) / sizeof(kSpots[0])))

static int spotWanted(int i) {
    const int story = swByteGet(0);
    switch (i) {
    case 0: return on("pg_boo_quiz") && story >= STORY_STG8_END_AFTER_THE_BATTLE;
    case 1: return on("pg_newsletter") && story >= STORY_STG8_00_START && !swGet(PORT_GSWF_PG_NEWS_DONE);
    case 2:
    case 3:
    case 4:
        return on("pg_newsletter") && swGet(PORT_GSWF_PG_NEWS_TAKEN) && !swGet(PORT_GSWF_PG_NEWS_DONE) &&
               !swGet(PORT_GSWF_PG_NEWS_1 + (i - 2));
    case 5: return on("pg_buy_tokens") && story >= STORY_STG2_44_FINDING_DON_PIANTA;
    default: return 0;
    }
}

static char sMap[16];
static int sAsked;

static void spots(void) {
    int i;
    if (gp->inBattle || seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return;
    }
    if (strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
        sAsked = 0;
    }
    if (sAsked) {
        return;
    }
    sAsked = 1;
    for (i = 0; i < SPOTS; i++) {
        EventEntry* e;
        if (strcmp(kSpots[i].map, sMap) != 0 || !spotWanted(i) || npcNameToPtr_NoAssert(kSpots[i].name) != NULL) {
            continue;
        }
        if (i >= 2 && i <= 4) {
            sReaderFlag = PORT_GSWF_PG_NEWS_1 + (i - 2);
        }
        e = evtEntry(npc_spawn, 10, 0);
        if (e != NULL) {
            e->lwData[0] = (EvtWord)kSpots[i].name;
            e->lwData[1] = (EvtWord)kSpots[i].model;
            e->lwData[2] = (EvtWord)kSpots[i].tribe;
            e->lwData[3] = kSpots[i].x;
            e->lwData[4] = kSpots[i].y;
            e->lwData[5] = kSpots[i].z;
            e->lwData[6] = (EvtWord)kSpots[i].setup;
            e->lwData[7] = (EvtWord)kSpots[i].anim;
        }
        printf("[postgame] %s set in %s\n", kSpots[i].name, sMap);
        fflush(stdout);
    }
}

/* --- the texts ------------------------------------------------------------------- */

static const PortPgText kTexts[] = {
    {"pc_pg_tattle_doopliss2",
     "It's Doopliss again, and this time he knows your name, Mario. Ugh, he even wears your face!<p>He copies "
     "whoever he fights, so don't let his tricks fool you.<p>Now that we know who he is, his spell is broken. "
     "Let's finish this!<k>",
     1},
    {"pc_pg_tattle_queen2",
     "This is the Shadow Queen's true form... the darkness that's been inside Peach all along.<p>Her hands "
     "attack on their own, and she can heal herself.<p>We've got everyone cheering for us now. Don't give "
     "up, Mario!<k>",
     1},
    /* the Boo quiz */
    {"pc_pg_quiz_00",
     "Boo! Did I scare you? No? Hmph.<p>Up here in the steeple I've had a thousand years to read every book. "
     "Want to take my quiz? Fifteen questions!<o>",
     1},
    {"pc_pg_quiz_01", "Suit yourself. I'll be floating right here.<k>", 1},
    {"pc_pg_quiz_right", "That's right! Booo-tiful!<k>", 1},
    {"pc_pg_quiz_wrong", "Nope! Not even close.<k>", 1},
    {"pc_pg_quiz_prize",
     "All fifteen! You know more than a ghost who never leaves home.<p>Here, take these. A Boo doesn't need "
     "coins, and that sheet was never my size.<k>",
     1},
    {"pc_pg_quiz_again", "All fifteen again! You really have been everywhere, haven't you?<k>", 1},
    {"pc_pg_quiz_some", "Not bad, not bad. Come back and try for all fifteen!<k>", 1},
    {"pc_pg_quiz_q00", "Question one! What does Goombella study at the University of Goom?<o>", 1},
    {"pc_pg_quiz_a00", "Archaeology|Cooking|Music", 2},
    {"pc_pg_quiz_q01", "Question two! Who lived in Hooktail Castle?<o>", 1},
    {"pc_pg_quiz_a01", "Gloomtail|Hooktail|Bonetail", 2},
    {"pc_pg_quiz_q02", "Question three! Who lives inside the Great Boggly Tree?<o>", 1},
    {"pc_pg_quiz_a02", "Piantas|Bub-ulbers|Punies", 2},
    {"pc_pg_quiz_q03", "Question four! Who was the Glitz Pit's champion before you?<o>", 1},
    {"pc_pg_quiz_a03", "Rawk Hawk|Macho Grubba|King K", 2},
    {"pc_pg_quiz_q04", "Question five! Who took your name in Twilight Town?<o>", 1},
    {"pc_pg_quiz_a04", "A Boo|Beldam|Doopliss", 2},
    {"pc_pg_quiz_q05", "Question six! Whose treasure was hidden in the Pirate's Grotto?<o>", 1},
    {"pc_pg_quiz_a05", "Flavio's|Cortez's|Bobbery's", 2},
    {"pc_pg_quiz_q06", "Question seven! What was the name of the train in chapter six?<o>", 1},
    {"pc_pg_quiz_a06", "The Excess Express|The Poshley Express|The Moon Express", 2},
    {"pc_pg_quiz_q07", "Question eight! Who leads the X-Nauts?<o>", 1},
    {"pc_pg_quiz_a07", "Lord Crump|Sir Grodus|Grubba", 2},
    {"pc_pg_quiz_q08", "Question nine! How many Crystal Stars are there?<o>", 1},
    {"pc_pg_quiz_a08", "Five|Eight|Seven", 2},
    {"pc_pg_quiz_q09", "Question ten! What do Vivian and her sisters call themselves?<o>", 1},
    {"pc_pg_quiz_a09", "The Night Witches|The Three Shades|The Shadow Sirens", 2},
    {"pc_pg_quiz_q10", "Question eleven! Which friend can hide you in the shadows?<o>", 1},
    {"pc_pg_quiz_a10", "Vivian|Flurrie|Ms. Mowz", 2},
    {"pc_pg_quiz_q11", "Question twelve! Who cooks dishes for you in Rogueport?<o>", 1},
    {"pc_pg_quiz_a11", "Charlieton|Zess T.|Frankly", 2},
    {"pc_pg_quiz_q12", "Question thirteen! Which friend is a Bob-omb?<o>", 1},
    {"pc_pg_quiz_a12", "Koops|Admiral Bobbery|Goombella", 2},
    {"pc_pg_quiz_q13", "Question fourteen! Which move of Flurrie's blows enemies away?<o>", 1},
    {"pc_pg_quiz_a13", "Gale Force|Shell Toss|Earth Tremor", 2},
    {"pc_pg_quiz_q14", "Last question! Who did the Shadow Queen take over?<o>", 1},
    {"pc_pg_quiz_a14", "Ms. Mowz|Vivian|Princess Peach", 2},
    /* Newsletter Trouble */
    {"pc_pg_news_00",
     "Mario! The Rogueport paper's in a pickle. Our delivery Koopa went to the moon to see what all the fuss "
     "was about!<p>Could you take this week's newsletter to Petalburg, Glitzville and Twilight Town?<o>",
     1},
    {"pc_pg_news_01",
     "Thank you! Give one to the Toad who reads it in each town. Come back when they've all got theirs!<k>", 1},
    {"pc_pg_news_02", "Oh... The news waits for no one, you know. Ask me again if you change your mind.<k>", 1},
    {"pc_pg_news_03", "Still some towns without the news! Petalburg, Glitzville and Twilight Town, please.<k>",
     1},
    {"pc_pg_news_04",
     "Every town has the news! You're faster than any Koopa I've hired.<p>Here, your pay, and a jar of something "
     "sweet for the road.<k>",
     1},
    {"pc_pg_news_05", "Next week's paper? Ha! You're in it. Front page!<k>", 1},
    {"pc_pg_news_10", "Oh, the newsletter from Rogueport! Thanks, I've been waiting all week.<k>", 1},
    {"pc_pg_news_11", "I'm reading it now. Did you know a hero saved the whole world?<k>", 1},
    {"pc_pg_news_12", "Hm? I'm waiting for the paper from Rogueport. It's late again.<k>", 1},
    /* Pianta tokens */
    {"pc_pg_tokens_00",
     "Hey, pal. Short on tokens? I'll sell you some. Three coins a token, and no questions asked.<o>", 1},
    {"pc_pg_tokens_sel", "50 tokens (150 coins)|250 tokens (750 coins)|300 tokens (900 coins)|No thanks", 2},
    {"pc_pg_tokens_01", "Come back when you're feeling lucky.<k>", 1},
    {"pc_pg_tokens_02", "You don't have the coins, pal. No coins, no tokens.<k>", 1},
    {"pc_pg_tokens_03", "There you go. Good luck in there!<k>", 1},
};

void portPgMoreFrame(void) {
    static int sRegistered;
    if (!sRegistered) {
        sRegistered = 1;
        portPgAddTexts(kTexts, (int)(sizeof(kTexts) / sizeof(kTexts[0])));
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME) {
        return;
    }
    secondTattles();
    spots();
}
