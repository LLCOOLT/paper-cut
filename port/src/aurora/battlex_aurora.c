/* Battle extras on Aurora (port/include/port/battlex.h): the port's items'
 * names and descriptions as game messages, the enemy variants' names and
 * Tattles, the Paper Cut merchant in Rogueport's plaza, and the frame hook
 * that keeps them up to date.
 *
 * The texts are in the menu language, and English online (every player's
 * game draws the same letters: the game's font takes random numbers per
 * letter). The badge maker's names stay on this computer: online they're
 * "Custom Badge 1" and so on. */
#include "port/battlex.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "battle/battle_monosiri.h"
#include "data/item_data.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_pouch.h"
#include "evt/evt_window.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "evt/evt_story.h"
#include "driver/swdrv.h"

#include <stdio.h>
#include <string.h>

void portPuzzlesRegister(void);
void portBattlexHooks(void);

/* --- texts --- */

/* the menu language's, but English online, and English for Japanese on a
 * release whose font has no Japanese */
static const char* tr(const char* english, int online) {
    if (online || (portTrLanguage() == 5 && PORT_GAME_ID[3] != 'J')) {
        return english;
    }
    return portTr(english);
}

/* A text (UTF-8, before portModsSetMessage makes it the game's) laid out as
 * the game's own messages are: the game's font never wraps, so lines of at
 * most cols columns (a Japanese letter is two; it breaks between letters),
 * and for speech (pageLines > 0) pages of that many lines, each ending in a
 * key wait (<k>, then <p> on a line of its own, as the game's texts have
 * it). The text's own <p> starts a page; its <k> before one, or at its end,
 * is kept; other tags are copied and take no room. One long line ran off
 * the bubble's edge, and a bare <p> without its <k> made no page. */
static const char* gameLayout(const char* in, int cols, int pageLines) {
    static char out[2][1024];
    static int which;
    char* o = out[which ^= 1];
    const size_t size = sizeof(out[0]);
    size_t n = 0;
    int line = 0, lines = 1, wait = 0, prevCjk = 0;
#define PUT(str) (n + strlen(str) + 1 < size ? (memcpy(o + n, str, strlen(str)), n += strlen(str)) : 0)
    while (*in != '\0') {
        const char* w = in;
        int width = 0;
        if (*in == ' ' || *in == '\n') {
            in++;
            continue;
        }
        if ((unsigned char)in[0] == 0xE3 && (unsigned char)in[1] == 0x80 && (unsigned char)in[2] == 0x80) {
            in += 3; /* an ideographic space: a word's end, as a space */
            continue;
        }
        if (*in == '<') {
            const char* end = strchr(in, '>');
            const size_t len = end != NULL ? (size_t)(end - in) + 1 : strlen(in);
            if (len == 3 && in[1] == 'k') {
                wait = 1;
            } else if (len == 3 && in[1] == 'p') {
                PUT("\n<k>\n<p>\n");
                wait = 0;
                line = 0;
                lines = 1;
            } else if (n + len + 1 < size) {
                memcpy(o + n, in, len);
                n += len;
            }
            in += len;
            continue;
        }
        if ((unsigned char)*in >= 0xE0) {
            /* a Japanese letter: a word of its own */
            in += (unsigned char)*in >= 0xF0 ? 4 : 3;
            while ((unsigned char)*in >= 0x80 && (unsigned char)*in < 0xC0) {
                in++;
            }
            width = 2;
        } else {
            while (*in != '\0' && *in != ' ' && *in != '\n' && *in != '<' && (unsigned char)*in < 0xE0) {
                width += ((unsigned char)*in & 0xC0) != 0x80; /* (a letter, not its continuation) */
                in++;
            }
        }
        {
            const int cjk = (unsigned char)*w >= 0xE0;
            const int gap = line > 0 && !cjk && !prevCjk;
            if (line > 0 && line + gap + width > cols) {
                if (pageLines > 0 && lines >= pageLines) {
                    PUT("\n<k>\n<p>\n");
                    lines = 1;
                } else {
                    PUT("\n");
                    lines++;
                }
                line = 0;
            } else if (gap) {
                PUT(" ");
                line++;
            }
            if (n + (size_t)(in - w) + 1 < size) {
                memcpy(o + n, w, (size_t)(in - w));
                n += (size_t)(in - w);
            }
            line += width;
            prevCjk = cjk;
        }
    }
    if (wait) {
        PUT("\n<k>");
    }
#undef PUT
    o[n] = '\0';
    return o;
}

#define DESC_COLS 30   /* an item's description: as the game's (the shops' help, the menus) */
#define SPEECH_COLS 30 /* a speech bubble's line */
#define SPEECH_LINES 3

static void setItemTexts(int online) {
    int id;
    char buf[96];
    for (id = PORT_ITEM_MIN; id < PORT_ITEM_END; id++) {
        const char* name = portItemName(id);
        const char* desc = portItemDescription(id);
        if (id >= PORT_BADGE_CUSTOM_MIN && id < PORT_BADGE_CUSTOM_MIN + PORT_BADGE_CUSTOM_COUNT) {
            const int slot = id - PORT_BADGE_CUSTOM_MIN;
            if (online || name[0] == 0) {
                snprintf(buf, sizeof(buf), tr("Custom Badge %d", online), slot + 1);
                name = buf;
            }
            desc = tr("A badge made with the badge maker: the effects of the badges it was made from.", online);
            portModsSetMessage(itemDataTable[id].name, name);
            portModsSetMessage(itemDataTable[id].description, gameLayout(desc, DESC_COLS, 0));
            continue;
        }
        if (name[0] == 0) {
            continue;
        }
        portModsSetMessage(itemDataTable[id].name, tr(name, online));
        portModsSetMessage(itemDataTable[id].description, gameLayout(tr(desc, online), DESC_COLS, 0));
    }
#define SPEECH(tag, english) portModsSetMessage(tag, gameLayout(tr(english, online), SPEECH_COLS, SPEECH_LINES))
    SPEECH("pc_merchant_00", "Hey there! I'm a travelling badge folder.<p>Every badge I sell is folded from two or "
                             "three you know, so one does the work of all of them.<p>Take a look!<k>");
    SPEECH("pc_merchant_01", "Come back any time!<k>");
    SPEECH("pc_merchant_02", "Not enough coins, I'm afraid.<k>");
    SPEECH("pc_merchant_03", "You can't carry any more badges!<k>");
    SPEECH("pc_merchant_04", "Thanks! Fold it with care.<p>Anything else?<k>");
    SPEECH("pc_merchant_05", "Sorry, I'm all sold out for now.<k>");
#undef SPEECH
}

void portBattlexVariantTexts(int kind, const char* name, const char* tattle) {
    TattleEntry* t = battleGetUnitMonosiriPtr(kind);
    if (t == NULL) {
        return;
    }
    if (t->enemyTag != NULL) {
        portModsSetMessage(t->enemyTag, name != NULL && name[0] != 0 ? name : NULL);
    }
    if (t->tattleBtlTag != NULL) {
        portModsSetMessage(t->tattleBtlTag, tattle != NULL && tattle[0] != 0 ? tattle : NULL);
    }
}

/* --- the merchant: a Toad by the plaza's fountain, while there's stock --- */

USER_FUNC(evt_pc_merchant_stock) {
    int stock[64];
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portMerchantStock(stock, 64));
    return EVT_RETURN_DONE;
}

#define MERCHANT "pc_merchant"

static EVT_BEGIN(merchant_talk)
    CALL(evt_msg_print, 0, STRING("pc_merchant_00"), 0, STRING(MERCHANT))
    CALL(evt_win_coin_on, 0, LW(8))
    LABEL(0)
    CALL(evt_pc_merchant_stock, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_merchant_05"), 0, STRING(MERCHANT))
        RETURN
    END_IF
    CALL(evt_win_other_select, PORT_SELECT_MERCHANT)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_merchant_01"), 0, STRING(MERCHANT))
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(3))
        CALL(evt_msg_print, 0, STRING("pc_merchant_02"), 0, STRING(MERCHANT))
        GOTO(0)
    END_IF
    CALL(evt_pouch_add_item, LW(1), LW(0))
    IF_INT_EQUAL(LW(0), -1)
        CALL(evt_win_coin_off, LW(8))
        CALL(evt_msg_print, 0, STRING("pc_merchant_03"), 0, STRING(MERCHANT))
        RETURN
    END_IF
    MULTIPLY(LW(3), -1)
    CALL(evt_pouch_add_coin, LW(3))
    CALL(evt_win_coin_wait, LW(8))
    CALL(evt_msg_print, 0, STRING("pc_merchant_04"), 0, STRING(MERCHANT))
    GOTO(0)
    RETURN
EVT_END()

static NpcSetupInfo sMerchantSetup[] = {
    {
        MERCHANT,                                                                // name
        NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_MOVE_ANIM | NPC_FLAG_TALK_ANIM, // flags
        0,                                                                       // reactionFlags
        NULL,                                                                    // initEvent
        NULL,                                                                    // regularEvent
        merchant_talk,                                                           // talkEvent
    },
    {NULL},
};

/* Rogueport's plaza (gor_01): a Toad ("キノピオ" in the game's Shift-JIS) */
static EVT_BEGIN(merchant_spawn)
    CALL(evt_npc_entry, STRING(MERCHANT), STRING("c_kinopio"))
    CALL(evt_npc_set_tribe, STRING(MERCHANT), STRING("\x83\x4C\x83\x6D\x83\x73\x83\x49"))
    CALL(evt_npc_set_position, STRING(MERCHANT), -235, 0, 160)
    CALL(evt_npc_set_ry, STRING(MERCHANT), 90)
    CALL(evt_npc_setup, PTR(sMerchantSetup))
    CALL(evt_npc_set_anim, STRING(MERCHANT), STRING("S_1"))
    RETURN
EVT_END()

static int sMerchantAsked;
static char sMerchantMap[16];

static void merchantFrame(void) {
    int stock[64];
    if (seqGetSeq() != SEQ_GAME || gp->inBattle || strcmp(gp->currentMapName, "gor_01") != 0) {
        sMerchantMap[0] = 0;
        sMerchantAsked = 0;
        return;
    }
    /* once the prologue's done (the plaza's scenes are over), and while there's stock */
    if (sMerchantAsked || swByteGet(0) < STORY_STG1_00_PETAL_MEADOWS || portMerchantStock(stock, 64) == 0) {
        return;
    }
    if (npcNameToPtr_NoAssert(MERCHANT) == NULL) {
        evtEntry(merchant_spawn, 10, 0);
    }
    sMerchantAsked = 1;
}

/* --- the frame --- */

static void frameHook(void) {
    static int sLang = -1, sOnline = -1;
    static unsigned sChanges = ~0u;
    const int lang = portTrLanguage();
    /* (replays too: the playback's machine has its own language and badges.txt) */
    const int online = portOnlineActive() || portReplayActive();
    const unsigned changes = portBattlexRefresh();
    if (lang != sLang || online != sOnline || changes != sChanges) {
        sLang = lang;
        sOnline = online;
        sChanges = changes;
        setItemTexts(online);
    }
    merchantFrame();
    if (!gp->inBattle) {
        portBattleGradesSave();
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portBattlexInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portItemsInit();
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portPuzzlesRegister();
    portBattlexHooks();
}

/* --- the Enemy maker's variants in every battle --- */

#include "port/enemy_variant.h"
#include "battle/battle.h"
#include "driver/msgdrv.h"

static PortEnemyVariant sUser[PORT_VARIANT_ARMED_MAX];
static int sUserCount;
static int sUserLock; /* __atomic */

void portBattlexSetUserVariants(const PortEnemyVariant* list, int count) {
    count = count < 0 ? 0 : count > PORT_VARIANT_ARMED_MAX ? PORT_VARIANT_ARMED_MAX : count;
    while (__atomic_test_and_set(&sUserLock, __ATOMIC_ACQUIRE)) {
    }
    if (count > 0) {
        memcpy(sUser, list, sizeof(PortEnemyVariant) * (size_t)count);
    }
    sUserCount = count;
    __atomic_clear(&sUserLock, __ATOMIC_RELEASE);
}

/* online: the host's list for the session (online_session.h), the same in every game of it */
static PortEnemyVariant sSession[PORT_VARIANT_ARMED_MAX];
static int sSessionCount, sSessionOn;

void portBattlexSetSessionVariants(const PortEnemyVariant* list, int count, int on) {
    count = count < 0 ? 0 : count > PORT_VARIANT_ARMED_MAX ? PORT_VARIANT_ARMED_MAX : count;
    while (__atomic_test_and_set(&sUserLock, __ATOMIC_ACQUIRE)) {
    }
    if (on && count > 0) {
        memcpy(sSession, list, sizeof(PortEnemyVariant) * (size_t)count);
    }
    sSessionCount = on ? count : 0;
    sSessionOn = on != 0;
    __atomic_clear(&sUserLock, __ATOMIC_RELEASE);
}

/* Btl_UnitSetup, before: the units are made from the armed variants */
static int unitSetupPre(BOOL* result, BattleWork** wp) {
    const PortEnemyVariant* armed;
    int* on = portSettingsDeclare("enemy_variants", 0, 0, 1);
    (void)result;
    (void)wp;
    /* (not in replays: the list is this player's enemies.txt; online, the session's: the host's) */
    if (on == NULL || *on == 0 || portReplayActive() || portEnemyVariantArmed(&armed) > 0) {
        return 0;
    }
    while (__atomic_test_and_set(&sUserLock, __ATOMIC_ACQUIRE)) {
    }
    if (sSessionOn) {
        if (sSessionCount > 0) {
            portEnemyVariantArm(sSession, sSessionCount);
        }
    } else if (sUserCount > 0 && !portOnlineActive()) {
        portEnemyVariantArm(sUser, sUserCount);
    }
    __atomic_clear(&sUserLock, __ATOMIC_RELEASE);
    return 0;
}

const char* portBattlexEnemyName(int kind, char* out, int size) {
    const TattleEntry* t = kind > 0 && kind < UNIT_MAX ? battleGetUnitMonosiriPtr(kind) : NULL;
    const char* text;
    int n = 0;
    out[0] = 0;
    if (t == NULL || t->enemyTag == NULL || gp == NULL || (text = msgSearch(t->enemyTag)) == NULL ||
        PORT_GAME_ID[3] == 'J') {
        return out;
    }
    for (; *text != '\0' && n < size - 3; text++) {
        const unsigned char c = (unsigned char)*text;
        if (c == '<') { /* a control tag */
            while (*text != '\0' && *text != '>') {
                text++;
            }
            if (*text == '\0') {
                break;
            }
            continue;
        }
        if (c < 0x80) {
            out[n++] = (char)c;
        } else { /* Latin-1 to UTF-8 */
            out[n++] = (char)(0xC0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = 0;
    return out;
}

void portBattlexHooks(void) {
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
}
