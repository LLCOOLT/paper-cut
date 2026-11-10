// Message driver: loads the message files, looks messages up by tag, parses their markup into
// characters, icons and animations, and runs the speech windows and selection lists.
#ifdef TARGET_PC
#include "port/open_world.h"
#include "port/duo.h"
#include "port/fire_breath.h"
#include "port/rules.h"
#endif
#include "port_hook.h"
#include "driver/msgdrv.h"
#ifdef TARGET_PC
#include "port/platform.h"
#include "port/speech.h"
#include "port/gameplay.h"
#include "port/gameplus.h"
#include "port/luigi.h"
#include "port/directors_cut.h"
#include "port/port_text.h"
#include "port/statesync.h"
#include "port/menu_tap.h"
#ifdef PORT_MODS
#include "port/mods.h"
#endif
#ifdef TTYD_BACKEND_AURORA
#include "port/menu_buttons.h"
#include "port/pause_settings.h" /* (used under TTYD_BACKEND_AURORA: tools/port/gen_hooks.py parses without PORT_MODS) */
#include "port/journal_hint.h"
#include "port/photo.h"
#include "port/stats.h"
#include "port/rando_tracker.h"
#include "port/pm64_media.h"
#include "port/warp_map.h"
#else
#include "port/pause_settings.h" /* (headless: portHeadlessMsg) */
#endif
#endif
#ifdef TARGET_PC
#include "port/view.h"
/* Enhancements: faster text outside battle, the message windows' 200 ms fades too */
#define MSG_FADE_MS(ticks) (OSTicksToMilliseconds(ticks) * portFieldTextScale())
#else
#define MSG_FADE_MS(ticks) OSTicksToMilliseconds(ticks)
#endif
#include "version.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/npcdrv.h"
#include "driver/windowdrv.h"
#include "manager/dvdmgr.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "libc_decls.h"
#include "pmario_sound.h"
#include "memory.h"
#include "system.h"
#include <string.h>
#include <stdlib.h>
#include "icon_ids.h"
#include <dolphin/pad.h>
#include <demo/DEMOPad.h>
#ifdef PORT_MODS
#include "port/hooks/src_driver_msgdrv.h"
#endif
#ifdef TARGET_PC
#include "port/bedecode.h"
#include "port/hud.h"
/* PC port: Shift-JIS text is a byte stream; read a double-byte code big-endian. */
#define SJIS_CODE(p) portBE16(p)
#else
#define SJIS_CODE(p) (*(const u16*)(p))
#endif
/** Red "No messages" text shown in place of a message key that is not found, Japanese version (ErrMessage). */
const char R_no_messages_JP[] = "<col ff0000ff>\203\201\203b\203Z\201[\203W\202\252\202\240\202\350\202\334\202\271\202\361</col>\n<key>";
#if VERSION >= VERSION_US
/** The same red "No messages" text for each other language, tagged with its name; ErrMessage picks one. */
const char R_no_messages_US[] = "<col ff0000ff>No messages.[English]<col>\012<key>";
const char R_no_messages_DE[] = "<col ff0000ff>No messages.[German]<col>\012<key>";
const char R_no_messages_FR[] = "<col ff0000ff>No messages.[French]<col>\012<key>";
const char R_no_messages_ES[] = "<col ff0000ff>No messages.[Spanish]<col>\012<key>";
const char R_no_messages_IT[] = "<col ff0000ff>No messages.[Italian]<col>\012<key>";
const char R_no_messages_DU[] = "<col ff0000ff>No messages.[Dutch]<col>\012<key>";
#else
/** Red "No messages" text shown in English in place of a message key that is not found (ErrMessage). */
const char R_no_messages_US[] = "<col ff0000ff>No messages.<col>\n<key>";
#endif

#define WIN_STATE(w) (*(u16*)(w)->state)

#ifdef TARGET_PC
/* PC port: callback holds the function address as an integer; convert rather than reinterpret its storage */
#define MSG_CB(d) ((void (*)(s32, smartEntry*))(intptr_t)(d)->callback)
#else
#define MSG_CB(d) (*(void (**)(s32, smartEntry*)) & (d)->callback)
#endif
#define WIN_DATA(w) ((MsgWinData*)(w)->userdata->address)

/** the text of a loaded message file (the demo keeps it in a smart heap block) */
static inline const char* msgEntryText(MessageEntry* entry) {
#if VERSION <= VERSION_DEMO_JP
    return entry->address->address;
#else
    return entry->address;
#endif
}

#ifdef TARGET_PC
/* the file in each slot (0 the area's, 1 global) and its message count, for the log of a tag not found */
static char sMsgFile[2][32];
static s32 sMsgCount[2];
static int sMsgQuiet; /* > 0: a lookup that only asks whether a tag is there (portMsgHas) */

static void portMsgLoaded(s32 index, const char* file, s32 count) {
    if (index < 0 || index > 1) {
        return;
    }
    snprintf(sMsgFile[index], sizeof(sMsgFile[index]), "%s", file != NULL ? file : "");
    sMsgCount[index] = count;
    if (file != NULL && count == 0) {
        printf("[msg] '%s' (slot %d): no messages loaded\n", file, (int)index);
        fflush(stdout);
    }
}
#endif

static void msgWindow_Disp(CameraId camId, void* param);
static void msgWindow_Main(WindowEntry* window);
static void msgWindow_Clear_Main(WindowEntry* window);
static void msgWindow_Delete(WindowEntry* window);
static void selectWindow_Main(WindowEntry* window);

static s32 msgMain(smartEntry* smart);
static void selectWindow_Disp(CameraId camId, void* param);

typedef struct MsgLookupEntry {
    u32 startaddr; // 0x0
    u32 endaddr;   // 0x4
} MsgLookupEntry;

typedef struct MsgIcon {
    const char* name; // 0x0
    s32 value;        // 0x4
} MsgIcon;

//.data
MsgIcon msgIcon[] = {
    {"PAD_A", ICON_064},
    {"PAD_B", ICON_066},
    {"STICK_LEFT", ICON_078},
    {"STICK", ICON_079},
    {"AC_ON", ICON_095},
    {"BUTTON_L", ICON_07E},
    {"BUTTON_R", ICON_080},
    {"HM", ICON_19F},
    {"PAD_X", ICON_068},
    {"PAD_X_ON", ICON_069},
    {"PAD_Y", ICON_06A},
    {"PAD_Y_ON", ICON_06B},
    {"PAD_Z_OFF", ICON_082},
    {"PAD_ST_OFF", ICON_084},
    {"black_key", ICON_130},
    {"ANM_PAD_A", ICON_20B},
    {"ANM_STICK_RIGHT", ICON_20C},
    {"ANM_PAD_START", ICON_20D}
};

#if VERSION >= VERSION_JP
/** Sound effects of the message windows by window type: the sound when it opens, then the one when it closes. */
s32 msgWindowOpen_SE_Data[] = {
    0x16, 0x17, 0x1A, 0x1B, 0x20, 0x21, 0x1C, 0x1D,
    0x20, 0x21, 0x1C, 0x1D, 0x18, 0x19, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x1E, 0x1F, -1, -1
};
#endif

//.sbss
MessageWork msgWork;
#if VERSION <= VERSION_DEMO_JP
static smartEntry dummySmartList; // stands in for a message file that failed to load
#endif
BOOL msgSEflag;             // msgSEInit and related functions are unused
const char* msgSortDvdData; // the text msg_compare sorts the lookup table by
u16 g5_active_s772;

//.sdata
MessageWork* msgw = &msgWork;
#if VERSION <= VERSION_DEMO_JP
/** Empty text that a message file which failed to load is replaced with. */
static char NoMessage[] = ""; // the text of dummySmartList
#endif
#if VERSION >= VERSION_US
const char* ErrMessage[7] = {R_no_messages_JP, R_no_messages_US, R_no_messages_DE, R_no_messages_FR,
                             R_no_messages_ES, R_no_messages_IT, R_no_messages_DU};
#else
const char* ErrMessage[2] = {R_no_messages_JP, R_no_messages_US};
#endif
const char* paperPoseTbl[6] = {
    "PM_Y_1B", "PM_Y_2B", "PM_F_1B",
    "PM_F_3B", "PM_C_1B", "PM_C_2B"
};
#if VERSION >= VERSION_US
const char* language_dir[7] = {"msg/JP", "msg/US", "msg/DE", "msg/FR", "msg/SP", "msg/IT", "msg/US"};
#else
const char* language_dir[2] = {"msg", "emsg"};
#endif

#if VERSION <= VERSION_DEMO_JP
// the demo's sound table follows the pose names in .data
s32 msgWindowOpen_SE_Data[] = {
    0x16, 0x17, 0x1A, 0x1B, 0x20, 0x21, 0x1C, 0x1D,
    0x20, 0x21, 0x1C, 0x1D, 0x18, 0x19, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x1E, 0x1F, -1, -1
};
#endif

// local prototypes
static int msg_compare(const void* a, const void* b);
static void msgDispKeyWait_render(s32 wXluStage);

/**
 * Sets up the message system at boot: the blinking star (hoshi_2) pose drawn at key waits
 * and the always-loaded global message file (slot 1).
 */
void PORT_HOOKABLE(msgInit)(void) {
    msgw->poseId = animPoseEntry("hoshi_2", 2);
    animPoseSetAnim(msgw->poseId, "HOS_C_1", TRUE);
    animPoseSetMaterialFlagOn(msgw->poseId, ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2);
    animPoseSetMaterialFlagOff(msgw->poseId, ANIM_MATERIAL_FLAG_EVT_COLOR);
    animPoseSetGXFunc(msgw->poseId, msgDispKeyWait_render, 0);
    msgw->entries[0].address = NULL;
    msgw->entries[0].lookup = NULL;
    msgw->entries[1].address = NULL;
    msgw->entries[1].lookup = NULL;
    msgLoad("global", 1);
}

#if VERSION <= VERSION_DEMO_JP
/** the demo loads the text into a smart heap block and points a missing file at an empty text */
void PORT_HOOKABLE(msgLoad)(const char* filename, s32 index) {
    MessageEntry* entry;
    const char* str;
    MsgLookupEntry* lookup;
    const char* msgptr;
    int i;
    DVDEntry* dvd;
    s32 msgcount;
    u32 ofs;
    char filepath[64];
    char dvdpath[128];

    entry = &msgw->entries[index];
    sprintf(filepath, "./%s/%s.txt", language_dir[gp->language], filename);
    if (entry->address) {
        if (entry->address != &dummySmartList) {
            smartFree(entry->address);
        }
        entry->address = NULL;
    }
    entry->length = 0;
    if (!entry->address) {
        sprintf(dvdpath, "%s/%s", getMarioStDvdRoot(), filepath);
        dvd = DVDMgrOpen(dvdpath, 2, 0);
        if (dvd) {
            entry->length = DVDMgrGetLength(dvd);
            entry->address = smartAlloc(OSRoundUp32B(entry->length), 0);
            DVDMgrRead(dvd, entry->address->address, OSRoundUp32B(entry->length), 0);
            DVDMgrClose(dvd);
        }
    }
    if (!entry->address) {
        entry->length = 0;
        entry->address = &dummySmartList;
        dummySmartList.address = NoMessage;
    }
    msgptr = msgEntryText(entry);
    msgcount = 0;
    while (*(const s8*)msgptr) {
        msgptr += strlen(msgptr) + 1;
        msgcount++;
    }
    entry->count = msgcount / 2; // tag, message

    if (!entry->count) {
        entry->lookup = NULL;
    } else {
        ofs = 0;
        if (entry->lookup) {
            smartFree(entry->lookup);
        }
        entry->lookup = smartAlloc(entry->count * sizeof(MsgLookupEntry), 0);
        str = msgEntryText(entry);
        lookup = entry->lookup->address;
        for (i = 0; i < entry->count; i++, lookup++) {
            lookup->startaddr = ofs;
            ofs += strlen(str) + 1;
            str += strlen(str) + 1;
            lookup->endaddr = ofs;
            ofs += strlen(str) + 1;
            str += strlen(str) + 1;
        }
        msgSortDvdData = msgEntryText(entry);
        qsort(entry->lookup->address, entry->count, sizeof(MsgLookupEntry), msg_compare);
    }
}
#else
/**
 * Loads message file <language dir>/<filename>.txt into slot index (0 = the current area's file, in map memory;
 * 1 = global), counts its tag/text pairs and builds a lookup table sorted by tag for msgSearch.
 * The US and later builds allocate slot 1 once, sized for the largest language's file.
 */
void PORT_HOOKABLE(msgLoad)(const char* filename, s32 index) {
    MessageEntry* entry;
    const char* str;
    MsgLookupEntry* lookup;
    const char* msgptr;
    int i;
#if VERSION >= VERSION_US
    s32 msgcount;
    DVDEntry* dvd;
    s32 max;
    s32 len;
#else
    DVDEntry* dvd;
    s32 msgcount;
#endif
    u32 ofs;
    char filepath[64];
    char dvdpath[128];

    entry = &msgw->entries[index];
#if VERSION >= VERSION_US
    // US: the global message buffer is allocated once, sized for the largest message file of all languages
    if (!entry->address && index == 1) {
        max = 0;
        for (i = 0; i <= 6; i++) {
            sprintf(filepath, "./%s/%s.txt", language_dir[i], filename);
            sprintf(dvdpath, "%s/%s", getMarioStDvdRoot(), filepath);
            dvd = DVDMgrOpen(dvdpath, 2, 0);
            if (dvd) {
                len = DVDMgrGetLength(dvd);
                DVDMgrClose(dvd);
                if (max < len) {
                    max = len;
                }
            }
        }
        entry->address = __memAlloc(HEAP_DEFAULT, OSRoundUp32B(max));
    }
    entry->length = 0;
    sprintf(filepath, "./%s/%s.txt", language_dir[gp->language], filename);
    sprintf(dvdpath, "%s/%s", getMarioStDvdRoot(), filepath);
    dvd = DVDMgrOpen(dvdpath, 2, 0);
    if (dvd) {
        entry->length = DVDMgrGetLength(dvd);
        switch (index) {
            case 0:
                entry->address = _mapAlloc(OSRoundUp32B(entry->length));
                break;
            case 1:
                break;
        }
        DVDMgrRead(dvd, entry->address, OSRoundUp32B(entry->length), 0);
        DVDMgrClose(dvd);
    }
#else
    sprintf(filepath, "./%s/%s.txt", language_dir[gp->language], filename);

    if (entry->address) {
        if (index == 1) {
            __memFree(HEAP_DEFAULT, entry->address);
        }
        entry->address = NULL;
    }
    entry->length = 0;

    if (!entry->address) {
        sprintf(dvdpath, "%s/%s", getMarioStDvdRoot(), filepath);
        dvd = DVDMgrOpen(dvdpath, 2, 0);
        if (dvd) {
            entry->length = DVDMgrGetLength(dvd);
            switch (index) {
                case 0:
                    entry->address = _mapAlloc(OSRoundUp32B(entry->length));
                    break;
                case 1:
                    entry->address = __memAlloc(HEAP_DEFAULT, OSRoundUp32B(entry->length));
                    break;
            }
            DVDMgrRead(dvd, entry->address, OSRoundUp32B(entry->length), 0);
            DVDMgrClose(dvd);
        }
    }
#endif

    if (!entry->address) {
        entry->length = 0;
        entry->count = 0;
    } else {
        msgptr = entry->address;
        msgcount = 0;
        while (*(const s8*)msgptr) {
            msgptr += strlen(msgptr) + 1;
            msgcount++;
        }
        entry->count = msgcount / 2; // tag, message
    }

    if (entry->lookup) {
        smartFree(entry->lookup);
    }
    if (!entry->count) {
        entry->lookup = NULL;
    } else {
        ofs = 0;
        entry->lookup = smartAlloc(entry->count * sizeof(MsgLookupEntry), 0);
        str = entry->address;
        lookup = entry->lookup->address;
        for (i = 0; i < entry->count; i++, lookup++) {
            lookup->startaddr = ofs;
            ofs += strlen(str) + 1;
            str += strlen(str) + 1;
            lookup->endaddr = ofs;
            ofs += strlen(str) + 1;
            str += strlen(str) + 1;
        }
        msgSortDvdData = entry->address;
        qsort(entry->lookup->address, entry->count, sizeof(MsgLookupEntry), msg_compare);
    }
#ifdef TARGET_PC
    portMsgLoaded(index, filename, entry->count);
#endif
}
#endif

#if defined(TARGET_PC) && VERSION > VERSION_DEMO_JP
/* PC: the map's unload (seq_mapchange.c _unload) frees the area's messages (slot 0, in map memory)
 * but leaves their lookup table, so msgSearch read freed text until the next map's msgLoad. A map
 * change that ends at no map (a Game Over, a soft reset or the title menu's way back to the title)
 * left it so through the file select. On the console the freed text is still there by luck; here
 * it's let go with the map. */
void msgUnLoadArea(void) {
    MessageEntry* entry = &msgw->entries[0];
    if (entry->lookup) {
        smartFree(entry->lookup);
    }
    entry->lookup = NULL;
    entry->address = NULL;
    entry->length = 0;
    entry->count = 0;
    portMsgLoaded(0, NULL, 0);
}
#endif

/** Records y as the top of a new page for paging back with Z, up to 110 pages, unless the window keeps no page list. */
static inline void msgAddPage(smartEntry* smart, s32 y) {
    MsgWinData* d = smart->address;
    if (!(d->flags & MSG_WIN_FLAG_NO_PAGES) && d->pageCount < 110) {
        if (d->pageCount == 0 || d->pageY[d->pageCount - 1] != y) {
            d->pageY[d->pageCount++] = y;
        }
    }
}

/** Lets A or B (and X after <keyxon>) speed up the printing, unless <dkey> turned that off. */
static inline void msgCheckSkip(smartEntry* smart, MsgWinData* data) {
    s32 btn, trgv;
    if (!(data->flags & MSG_WIN_FLAG_NO_FAST)) {
        btn = PAD_BUTTON_A;
        if (((MsgWinData*)smart->address)->flags & MSG_WIN_FLAG_KEY_X) {
            btn = PAD_BUTTON_A | PAD_BUTTON_X;
        }
        trgv = keyGetButtonTrg(0);
        if (trgv & (btn | PAD_BUTTON_B)) {
            data->flags |= MSG_WIN_FLAG_FAST;
        }
    }
}

/** Plays the talking blip once per frame at the window's talk volume, except in small (type 1) windows. */
static inline void msgVoiceSE(smartEntry* smart) {
    MsgWinData* d = smart->address;
    if (d->window->type != 1 && !msgSEflag) {
        psndSFXOnVol(SFX_NAME_WORD(0x20044), d->talkVolume);
        msgSEflag = 1;
    }
}

/**
 * Per-frame text update for a message: runs the window callback on the first frame,
 * reveals characters at the print speed (faster when skipping), plays talk blips,
 * drives the speaker's talk animation and stops at key waits, selections and the end;
 * returns the print state to the window.
 */
static s32 msgMain(smartEntry* smart) {
    MsgWinData* data = smart->address;
    MsgChar* entry;
    s32 i;
    s32 ms;
    NpcEntry* npc;
#ifdef TARGET_PC
    /* PC port: in a shared battle online, a window showing Paper Cut's text prints each page at once and scrolls
     * without sliding. That text is in each player's own menu language, so printed letter by
     * letter it would take a different time for each player and the script waiting on it would
     * go on in a different frame: out of step. Its pages and waits are the same in every
     * language (tools/port/gen_lang.py holds each translation to the English's <k>, <p>,
     * <dkey>...), so at once it takes the same frames everywhere (sync what matters plays a
     * shared battle frame for frame). Alone, as on the GameCube. */
    const int instant = data->portText && portStateSyncSharedBattle();
#endif

    msgSEflag = 0;
    if (data->flags & MSG_WIN_FLAG_FIRST_FRAME) {
        if (MSG_CB(data)) {
            MSG_CB(data)(0, smart);
        }
        data->flags &= ~MSG_WIN_FLAG_FIRST_FRAME;
        msgAddPage(smart, 0);
    }
#ifdef TARGET_PC
    if (instant && data->scrollTarget > data->scrollPos) {
        data->scrollPos = data->scrollTarget; /* (paging back with Z, too) */
    }
#endif
    if (data->scrollTarget > data->scrollPos) {
        data->scrollPos += 0.36f * OSTicksToMilliseconds(gp->renderTime - data->lastUpdateTime);
#ifdef TARGET_PC
        /* Enhancements: faster text outside battle, the page turn too */
        data->scrollPos += 0.36f * OSTicksToMilliseconds(gp->renderTime - data->lastUpdateTime) * (portFieldTextScale() - 1);
#endif
        msgCheckSkip(smart, data);
        if (!(data->flags & MSG_WIN_FLAG_SMALL) && (data->flags & MSG_WIN_FLAG_FAST)) {
            data->scrollPos += 0.36f * OSTicksToMilliseconds(gp->renderTime - data->lastUpdateTime);
        }
        if (data->scrollTarget < data->scrollPos) {
            data->scrollPos = data->scrollTarget;
        }
        data->lastUpdateTime = gp->renderTime;
        return 3;
    }
    switch (data->pageState) {
        case 1:
            return 2;
        case 2:
            data->pageAdvanced = 1;
            data->curPage++;
            data->pageState = 0;
            psndSFXOn(SFX_NAME_WORD(0x2003D));
            data->playTime += 100;
            if (MSG_CB(data)) {
                MSG_CB(data)(3, smart);
            }
            break;
        default:
            ms = OSTicksToMilliseconds(gp->renderTime - data->lastUpdateTime);
#ifdef TARGET_PC
            ms = portTextMs(ms); /* PC port: the text speed setting */
            ms *= portFieldTextScale(); /* Enhancements: faster text outside battle */
#endif
            msgCheckSkip(smart, data);
            if (!(data->flags & MSG_WIN_FLAG_SMALL) && (data->flags & MSG_WIN_FLAG_FAST)) {
                ms *= 100;
            }
            data->playTime += ms;
            break;
    }
#ifdef TARGET_PC
    if (instant) {
        data->playTime = 0x3FFFFFFF; /* up to the page's key wait (which sets playTime back to its own time) */
    }
#endif
    data->lastUpdateTime = gp->renderTime;
    entry = &data->entries[data->nextEntry];
    for (i = data->nextEntry; i < VERSION_PICK(0x500, 0xA00); i++, entry++) {
        if (entry->time > data->playTime) {
            break;
        }
        switch (entry->glyph) {
            case MSG_GLYPH_END:
                if (MSG_CB(data)) {
                    MSG_CB(data)(1, smart);
                }
                data->nextEntry = i;
                return 0;
            case MSG_GLYPH_KEY:
                if (entry->x == data->curPage) {
                    data->pageState = 1;
                    data->playTime = entry->time;
                    data->flags &= ~MSG_WIN_FLAG_FAST;
                    if (MSG_CB(data)) {
                        MSG_CB(data)(2, smart);
                    }
                }
                break;
            case MSG_GLYPH_SCROLL:
                data->scrollTarget -= entry->y;
#ifdef TARGET_PC
                if (instant) { /* (more lines than the window: the same frame, however many) */
                    msgAddPage(smart, data->scrollTarget);
                    data->scrollPos = data->scrollTarget;
                    break;
                }
#endif
                data->playTime = entry->time;
                msgAddPage(smart, data->scrollTarget);
                break;
            case MSG_GLYPH_ONCE_STOP:
                data->nextEntry = i + 1;
                data->playTime = entry->time;
                data->flags &= ~MSG_WIN_FLAG_FAST;
                return 1;
            case MSG_GLYPH_NEWLINE:
                if (MSG_CB(data)) {
                    MSG_CB(data)(4, smart);
                }
                break;
            case MSG_GLYPH_SE:
                data->talkSfxGroup = entry->value;
                break;
            case MSG_GLYPH_VOL:
                data->talkVolume = entry->value;
                break;
            case MSG_GLYPH_DKEY:
                if (entry->value) {
                    data->flags |= MSG_WIN_FLAG_NO_FAST;
                    data->flags &= ~MSG_WIN_FLAG_FAST;
                    data->playTime = entry->time;
                } else {
                    data->flags &= ~MSG_WIN_FLAG_NO_FAST;
                }
                break;
            case MSG_GLYPH_PAPER:
                npc = entry->npc;
                if (!npc) {
                    npc = npcNameToPtr(data->speakerName);
                }
                animPoseSetPaperAnimGroup(npc->poseId, "p_b_st", 1);
                animPosePaperPeraOn(npc->poseId);
                animPoseSetPaperAnim(npc->poseId, paperPoseTbl[entry->time], 1);
                break;
            case MSG_GLYPH_PAPER_END:
                npc = entry->npc;
                if (data->pageState) {
                    i--;
                    break;
                }
                if (!npc) {
                    npc = npcNameToPtr(data->speakerName);
                }
                animPoseSetPaperAnimGroup(npc->poseId, NULL, 1);
                break;
            case MSG_GLYPH_ANIM:
            case MSG_GLYPH_ICON:
                break;
            default:
                if (MSG_CB(data)) {
                    MSG_CB(data)(5, smart);
                }
                if (entry->value & MSG_CHAR_DOT) {
                    msgVoiceSE(smart);
                } else if (entry->value & MSG_CHAR_TALK) {
                    if (((MsgWinData*)smart->address)->window->type != 3) {
                        ((MsgWinData*)smart->address)->talkTimer = 15;
                    }
                    msgVoiceSE(smart);
                }
                break;
        }
    }
    data->nextEntry = i;
    if (data->pageState) {
        return 2;
    }
    return 3;
}

#ifdef TARGET_PC
/* PC port: a window showing Paper Cut's text (msgWindow_Entry, msgWindow_Add) draws it with the
 * game's random numbers put back after, as the font does (port/include/port/port_text.h) */
static BOOL msgDispText(smartEntry* smart, u8 alpha, f32 x, f32 y);
static BOOL msgDisp(smartEntry* smart, u8 alpha, f32 x, f32 y) {
    const int own = ((MsgWinData*)smart->address)->portText;
    BOOL ret;
    if (own) {
        portTextBegin();
    }
    ret = msgDispText(smart, alpha, x, y);
    if (own) {
        portTextEnd();
    }
    return ret;
}
#define msgDisp msgDispText
#endif
static BOOL msgDisp(smartEntry* smart, u8 alpha, f32 x, f32 y);
/**
 * Draws the parsed message text of a window at (x, y):
 * the characters revealed so far on the current page, with colour, shaking, waves, ruby text,
 * inline icons and the key-wait star, faded by alpha.
 * Returns TRUE when something was drawn below the visible area.
 */
static BOOL msgDisp(smartEntry* smart, u8 alpha, f32 x, f32 y) {
    s32 al;
    s32 hide;
    s32 w;
    MsgWinData* data = smart->address;
    MsgChar* c = data->entries;
    s32 i;
    BOOL ret = FALSE;
    u8 a;
    f32 sx, sy, hx, hy, t;
    s32 page, py, d;
    GXColor col, col2, col4;
    Mtx m;
    Mtx m2;
    Vec pos;

    FontDrawStart_alpha(alpha);
    al = alpha;
    for (i = 0; i < data->nextEntry; i++, c++) {
        switch (c->glyph) {
            case MSG_GLYPH_END:
                return FALSE;
            case MSG_GLYPH_KEY:
                if (data->curPage == c->x) {
                    ret = TRUE;
                }
                break;
            case MSG_GLYPH_COLOR:
                FontDrawColor(c->color);
                break;
            case MSG_GLYPH_NOISE:
                if (c->value) {
                    FontDrawNoise();
                } else {
                    FontDrawNoiseOff();
                }
                break;
            case MSG_GLYPH_ANIM:
                col = FontGetDrawColor();
                PSMTXIdentity(m);
                if (c->rot >= 90.0f && c->rot <= 270.0f) {
                    PSMTXScaleApply(m, m, 1.0f, 1.0f, -1.0f);
                }
                PSMTXTransApply(m, m, x + c->x, c->y + (y + data->scrollPos) - 40.0f, 0.0f);
                animPoseMain((s32)c->value);
                animPoseDrawMtx((s32)c->value, m, 2, c->rot, c->scale);
                FontDrawStart_alpha(alpha);
                FontDrawColor(col);
                break;
            case MSG_GLYPH_DKEY:
            case MSG_GLYPH_VOL:
            case MSG_GLYPH_SE:
            case MSG_GLYPH_NEWLINE:
            case MSG_GLYPH_ONCE_STOP:
            case MSG_GLYPH_SCROLL:
                break;
            default:
                hide = 0;
                a = 0xFF;
                sx = x + c->x;
                sy = c->y + (y + data->scrollPos);
                if (data->flags & MSG_WIN_FLAG_PAGE_BACK) {
                    page = data->page;
                    py = data->pageY[page];
                    if (data->scrollPos == py) {
                        if (c->y + data->pageY[page + 1] <= 0) {
                            break;
                        }
                        hide = 1;
                    } else if (data->scrollPos < py) {
                        if (c->y + py > 0) {
                            hide = 1;
                        } else {
                            d = py - data->scrollPos;
                            if (d > 100) {
                                d = 100;
                            }
                            a = (100 - d) * 255 / 100;
                            if (page != data->pageCount - 1) {
                                if (c->y + data->pageY[page + 1] <= 0) {
                                    break;
                                }
                                hide = 1;
                            } else {
                                col2 = FontGetDrawColor();
                                col2.a = col2.a * a / 255;
                                GXSetTevColor(GX_TEVREG0, col2);
                            }
                        }
                    } else {
                        if (c->y + data->pageY[page + 1] > 0) {
                            hide = 1;
                        } else {
                            d = data->scrollPos - py;
                            if (d > 100) {
                                d = 100;
                            }
                            a = d * 255 / 100;
                            if (page != data->pageCount - 2) {
                                if (c->y + data->pageY[page + 2] <= 0) {
                                    break;
                                }
                                hide = 1;
                            } else {
                                col2 = FontGetDrawColor();
                                col2.a = col2.a * a / 255;
                                GXSetTevColor(GX_TEVREG0, col2);
                            }
                        }
                    }
                }
                if (c->glyph == MSG_GLYPH_ICON) {
                    col4 = FontGetDrawColor();
#if VERSION >= VERSION_US
                    if ((((data->scrollPos + c->y) - 40) + c->iconY) > 0) {
                        break;
                    }
#endif
                    pos.x = 20.0f + (x + c->x);
                    pos.y = c->iconY + (c->y + (y + data->scrollPos) - 40.0f);
                    pos.z = 0.0f;
                    iconDispGxAlpha(&pos, ICON_FLAG_2D, (u16)c->value, a * al / 255, c->scale);
                    FontDrawStart_alpha(alpha);
                    FontDrawColor(col4);
                    break;
                }
                if ((f32)(c->y + data->scrollPos) >= 32.0f * c->scale - 10.0f) {
                    break;
                }
                if (hide) {
                    GXColor gray = {0x50, 0x50, 0x50, 0xFF};
                    switch (data->window->type) {
                        case 3:
                        case 8:
                        case 9: {
                            GXColor gray2 = {0x6E, 0x6E, 0x6E, 0xFF};
                            gray = gray2;
                            break;
                        }
                    }
                    gray.a = a;
                    GXSetTevColor(GX_TEVREG0, gray);
#if VERSION >= VERSION_US
                    if (c->value & MSG_CHAR_SCALE_X) {
                        FontDrawScaleVec((Vec){c->scale, 1.0f, 1.0f});
                    } else {
                        FontDrawScale(c->scale);
                    }
#else
                    FontDrawScale(c->scale);
#endif
                    FontDrawCode(sx, sy, c->glyph);
                    FontDrawColor_();
                } else if (c->value & MSG_CHAR_DYNAMIC) {
                    w = (u16)(kanjiGetWidth(c->glyph) / 2);
#if VERSION >= VERSION_US
                    if (c->value & MSG_CHAR_SHAKE) {
                        sx += irand(10000) % 3;
                        sy += irand(10000) % 3;
                    }
                    hx = sx;
                    hy = sy;
#else
                    hx = sx;
                    hy = sy;
                    if (c->value & MSG_CHAR_SHAKE) {
                        hx = sx + irand(10000) % 3;
                        hy = sy + irand(10000) % 3;
                    }
#endif
                    PSMTXTrans(m2, -w, 12.0f, 0.0f);
                    PSMTXScaleApply(m2, m2, c->rot, c->rot, c->rot);
                    PSMTXTransApply(m2, m2, (u32)w, -12.0f, 0.0f);
                    PSMTXScaleApply(m2, m2, c->scale, c->scale, c->scale);
                    PSMTXTransApply(m2, m2, hx, hy, 0.0f);
                    FontDrawCodeMtx(m2, c->glyph);
#ifdef TARGET_PC
                    portInterpKeep(&c->rot, sizeof(c->rot)); /* the glyph's pop settles in each draw */
#endif
                    c->rot = 1.0f + 0.5f * (c->rot - 1.0f);
                } else if (c->value & MSG_CHAR_SHAKE) {
#if VERSION >= VERSION_US
                    sx += irand(10000) % 3;
                    sy += irand(10000) % 3;
                    if (c->value & MSG_CHAR_SCALE_X) {
                        FontDrawScaleVec((Vec){c->scale, 1.0f, 1.0f});
                    } else {
                        FontDrawScale(c->scale);
                    }
                    FontDrawCode(sx, sy, c->glyph);
#else
                    hy = sx + irand(10000) % 3;
                    hx = sy + irand(10000) % 3;
                    FontDrawScale(c->scale);
                    FontDrawCode(hy, hx, c->glyph);
#endif
                } else if (c->value & MSG_CHAR_WAVE) {
#if VERSION >= VERSION_US
                    t = 0.01f * OSTicksToMilliseconds(gp->renderTime - data->openTime) - 0.02f * sx;
                    sx += 3.2f * (f32)cos(t);
                    sy -= 3.2f * (f32)sin(t);
                    if (c->value & MSG_CHAR_SCALE_X) {
                        FontDrawScaleVec((Vec){c->scale, 1.0f, 1.0f});
                    } else {
                        FontDrawScale(c->scale);
                    }
                    FontDrawCode(sx, sy, c->glyph);
#else
                    t = 0.01f * OSTicksToMilliseconds(gp->renderTime - data->openTime) - 0.02f * sx;
                    hx = cos(t);
                    hy = sin(t);
                    FontDrawScale(c->scale);
                    FontDrawCode(sx + 3.2f * hx, sy - 3.2f * hy, c->glyph);
#endif
                } else if (c->value & (MSG_CHAR_OPTION_0 << data->sel)) {
                    f32 hx2;
                    t = 0.01f * OSTicksToMilliseconds(gp->renderTime - data->openTime) - 0.02f * sx;
#if VERSION >= VERSION_US
                    hx2 = sx + 3.2f * (f32)cos(t);
                    sy -= 3.2f * (f32)sin(t);
                    hy = sy;
                    if (c->value & MSG_CHAR_SCALE_X) {
                        FontDrawScaleVec((Vec){c->scale, 1.0f, 1.0f});
                    } else {
                        FontDrawScale(c->scale);
                    }
#else
                    hx2 = sx + 3.2f * (f32)cos(t);
                    hy = sy - 3.2f * (f32)sin(t);
                    FontDrawScale(c->scale);
#endif
                    {
                        GXColor shadow = {0x00, 0x00, 0x00, 0x50};
                        FontDrawColor(shadow);
                    }
                    FontDrawCode(hx2, hy, c->glyph);
                    FontDrawEdge();
                    FontDrawRainbowColor();
                    {
                        GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
                        FontDrawColor(white);
                    }
                    FontDrawCode(hx2 - 4.0f, 4.0f + hy, c->glyph);
                    FontDrawEdgeOff();
                    FontDrawRainbowColorOff();
                    FontDrawColorIDX(0);
                } else {
#if VERSION >= VERSION_US
                    if (c->value & MSG_CHAR_SCALE_X) {
                        FontDrawScaleVec((Vec){c->scale, 1.0f, 1.0f});
                    } else {
                        FontDrawScale(c->scale);
                    }
#else
                    FontDrawScale(c->scale);
#endif
                    FontDrawCode(sx, sy, c->glyph);
                }
                break;
        }
    }
    return ret;
}
#ifdef TARGET_PC
#undef msgDisp
#endif

/**
 * GX set-up hook for the key-wait star pose: draws it on top of everything (Z always passes, no blending, alpha
 * test).
 */
static void msgDispKeyWait_render(s32 wXluStage) {
    AnimationWork* wp = animGetPtr();

    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_FALSE);
    GXSetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_FALSE);
    GXSetAlphaCompare(GX_GEQUAL, 0x80, GX_AOP_OR, GX_NEVER, 0);
    wp->currDispMode = wXluStage;
}

/* FAKE: opt_lifetimes off keeps total as one web, so the rubyW conversion int shares the loop-1 accumulator register
   as in the target. The webs the target does split are split by hand: glyphD, wwD, wwR and kR (declared first so they take
   the numbers of the split temps). See FAKES.md */
// FAKE: turns off lifetime splitting so the register allocation matches the target; removing it changes the code
#pragma opt_lifetimes off
/**
 * Parses a message's markup into the window's character list:
 * text and Japanese ruby, <col>, <shake>, <wave>, <icon>, <k> key waits, <p> pages, <select>, speeds and waits,
 * word wrap and scrolling. Called when a window opens and for each added message.
 */
void PORT_HOOKABLE(msgAnalize)(smartEntry* smart, const char* msg) {
    u16 glyphD;
    s32 wwD;
    s32 wwR;
    s32 kR;
    s16 pageTopY;
    s16 keyCount;
    s16 scroll;

    MsgWinData* data;
    /* FAKE: nn, fp, fx, ys and yx are one-element arrays (IRO scalarizes them into @ temps). nn is the ruby index copy
       of i. fp/fx both hold flags & 1 and ys/yx both hold y for the second ruby loop; a dead guard at the loop top
       redefines fp and ys (never runs), and fx/yx are re-copied from fp/ys before time is cleared. Same values; the fx copy
       merges only in the second colouring round and the y pair fixes the preheader order, which gives the target
       register order (FAKES.md) */
    s32 nn[1];
    /* FAKE: zz, z3 and z4 are write-only scratch; the kR init reads them back as 0 + 1 + 2 - 3 = 0. Their three constant
       loads only exist before register allocation and fill scheduler slots, so the loop-2 counter init lands after the
       rubyW truncation (FAKES.md) */
    s32 zz[1];
    s32 z3[1];
    s32 z4[1];
    u32 fp[1];
    u32 fx[1];
    s32 ys[1];
    s32 yx[1];
    WindowEntry* window;
    MsgChar* entry;
    s32 check;
    u16 glyph;
    s16 x;
    s16 y; // s16 in the original: extsh before every int->float use (0x20b0, 0x2b98) and after <pos> (0x23b0)
    s16 rubyX;
    s32 time;
    u32 flags;
    char tag[0x80];
    char args[0x80];
    char name[0x20];
    char anim1[0x20];
    char anim2[0x20];
    char paper[0x20];
    char paper2[0x20];
    s32 step;
#if VERSION >= VERSION_US
    Vec scale; // US: x and y scale apart (<scaleX>)
    u32 maxW;  // US: widest line so far, becomes the window width
#if VERSION >= VERSION_DEMO_EU
    s32 centerStart; // PAL: first character of the open <center> span
#endif
#else
    f32 scale;
#endif
    f32 dynScale;
    s32 posX;
    // FAKE: int, not s32 (same width/sign; changes colouring)
    int posY;
    s32 waitTime;
    s32 iconId;
    s32 iconOfsX;
    s32 iconOfsY;
    MsgWinData* data2;
    s32 iconAdvance;
    f32 iconScale;
    s32 animX;
    // FAKE: int, not s32 (same width/sign; changes colouring)
    int animY;
    s32 animFlip;
    f32 animScale;
    s32 paperTime;
    s16 w16;
    s16 w14;
    s16 w12;
    s16 w10;
    s16 sel0;
    s16 sel1;
    u16 iw;
    u16 ih;
    f32 maxScale;
    const char* next;
#if VERSION != VERSION_DEMO_US
    BOOL isHM;
#endif
    s32 i;
    s32 j;
    s32 k;
    s32 half;
    s32 total;
    s32 ww;

    data = smart->address;
    window = data->window;
    entry = &data->entries[data->charCount];
    flags = 0;
    maxScale = 0.0f;
#if VERSION >= VERSION_US
    {
        Vec one = {1.0f, 1.0f, 1.0f};
        scale = one;
    }
    maxW = 70;
#if VERSION >= VERSION_DEMO_EU
    centerStart = 0;
#endif
#else
    scale = 1.0f;
#endif
    if (window->type < 0) {
        window->type = 0;
    }
    time = data->parseTime;
    pageTopY = 0;
    keyCount = data->parsePage;
    x = data->parseX;
    scroll = data->parseScroll;
    y = data->parseY;
    step = data->parseSpeed;
    switch (window->type) {
        case 3:
        case 8:
            switch (window->type) {
                case 3: {
                    static const GXColor c = {0xDC, 0xC8, 0xDC, 0xFF};
                    entry->value = *((const u32*)(&c));
                    break;
                }
                case 8: {
                    static const GXColor c = {0x00, 0xD9, 0x14, 0xFF};
                    entry->value = *((const u32*)(&c));
                    break;
                }
            }
            entry->glyph = MSG_GLYPH_COLOR;
            entry->time = time;
            entry++;
            data->charCount++;
            break;
        default:
            break;
    }

    while (TRUE) {
#if VERSION <= VERSION_DEMO_JP
        // FAKE: dead guard (never runs) redefining y at the loop top; it gives y its own web there, so y is coloured
        // before the other parse state as in the target (FAKES.md)
        {
            s32 gz = 0;
            if (gz != 0) {
                y = 0;
            }
        }
#endif
        check = *((const u8*)msg);
#if VERSION >= VERSION_US
        if (_ismbblead((s8)check)) {
#else
        if (_ismbblead((u8)check)) {
#endif
            glyph = kanjiSearch(SJIS_CODE(msg));
#if VERSION >= VERSION_US
            ww = scale.x * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4));
            entry->glyph = glyph;
            entry->x = x;
            entry->y = y - (s16)(10.0f * scale.y);
            entry->scale = scale.x;
#else
            ww = scale * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4));
            entry->glyph = glyph;
            entry->x = x;
            entry->y = y - (s16)(10.0f * scale);
            entry->scale = scale;
#endif
            entry->value = flags;
            if ((SJIS_CODE(msg)) == 0x8145) {
                entry->value |= MSG_CHAR_DOT;
            } else if (((SJIS_CODE(msg)) != 0x8140) && ((SJIS_CODE(msg)) != 0x8151)) {
                entry->value |= MSG_CHAR_TALK;
            }
            if ((flags & MSG_CHAR_DYNAMIC) != 0) {
                entry->rot = dynScale;
            }
#if VERSION >= VERSION_US
            if (maxScale < scale.y) {
                maxScale = scale.y;
            }
#else
            if (maxScale < scale) {
                maxScale = scale;
            }
#endif
            entry->time = time;
            x += ww;
            msg += 2;
            time += step;
            entry++;
            data->charCount++;
            continue;
        }
        switch ((s8)check) {
            case 0:
                entry->glyph = MSG_GLYPH_END;
                entry->time = time;
#if VERSION >= VERSION_US
                if (data->selWidth == 0) {
                    if (maxW < x) {
                        maxW = x;
                    }
                    data->selWidth = maxW + 80;
                }
#endif
                data->parseTime = time;
                data->parseX = x;
                data->parseY = y;
                data->parsePage = keyCount;
                data->parseScroll = scroll;
                return;
            case '\n':
#if VERSION >= VERSION_US
                entry->glyph = MSG_GLYPH_NEWLINE;
                entry->time = time;
                if (0.0f == maxScale) {
                    maxScale = 1.0f;
                }
                if (data->flags & MSG_WIN_FLAG_SMALL) {
                    // US: tighter line spacing
                    y -= 0.7f * (32.0f * maxScale);
                    scroll -= 0.7f * (32.0f * maxScale);
                } else {
                    y -= 32.0f * maxScale;
                    scroll -= 32.0f * maxScale;
                }
#else
                entry->glyph = MSG_GLYPH_NEWLINE;
                entry->time = time;
                x = 0;
                if (0.0f == maxScale) {
                    maxScale = 1.0f;
                }
                y -= 32.0f * maxScale;
                scroll -= 32.0f * maxScale;
#endif
                msg++;
                entry++;
                time += step;
                data->charCount++;
                maxScale = 0.0f;
                if (data->flags & MSG_WIN_FLAG_SELECT) {
                    flags &= ~MSG_CHAR_OPTIONS;
                    flags |= MSG_CHAR_OPTION_0 << data->numSel;
                    data->numSel++;
#if VERSION >= VERSION_US
                    if (data->selWidth == 0) {
                        if (maxW < x) {
                            maxW = x;
                        }
                    }
#endif
                }
#if VERSION >= VERSION_US
                x = 0;
#endif
                continue;
            case '<':
                next = msgGetCommand(msg + 1, tag, args);
                msg = next;
                /* FAKE: dead int guard (COLOURING W): msg is redefined in the arm while next is still live
                   (the later newline test on next), so msg/next interfere and the target copy mr r21,r15 survives */
                {
                    s32 gz = 0;
                    if (gz != 0) {
                        msg++;
                    }
                }
                if ((strcmp(tag, "k") == 0) || (strcmp(tag, "key") == 0)) {
                    entry->glyph = MSG_GLYPH_KEY;
                    entry->x = keyCount;
                    entry->time = time;
                    time += 100;
                    entry++;
                    data->charCount++;
                    keyCount++;
                } else if (strcmp(tag, "keyxon") == 0) {
                    data->flags |= MSG_WIN_FLAG_KEY_X;
                } else if ((strcmp(tag, "p") == 0) || (strcmp(tag, "scrl_auto") == 0)) {
                    if (scroll != 0) {
                        entry->glyph = MSG_GLYPH_SCROLL;
                        entry->y = scroll - 3;
                        pageTopY = y - 6;
                        y -= 3;
                        entry->time = time;
                        time += 0x32;
                        entry++;
                        scroll = 0;
                        data->charCount++;
                    }
                } else if ((strcmp(tag, "o") == 0) || (strcmp(tag, "once_stop") == 0)) {
                    entry->glyph = MSG_GLYPH_ONCE_STOP;
                    entry->time = time;
                    time += 0x32;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "pos") == 0) {
                    sscanf(args, "%d%d", &posX, &posY);
                    x = posX;
#if VERSION >= VERSION_US
                    if (posY != 999) {
                        y = pageTopY - posY;
                        scroll = -posY;
                    }
#else
                    if (posY != 999) {
                        y = pageTopY - posY;
                    }
                    scroll = -posY;
#endif
#if VERSION >= VERSION_US
                } else if (strcmp(tag, "scale") == 0) {
                    sscanf(args, "%f", &scale.x);
                    flags &= ~MSG_CHAR_SCALE_X;
                    scale.y = scale.x;
                } else if (strcmp(tag, "scaleX") == 0) {
                    sscanf(args, "%f", &scale.x);
                    flags |= MSG_CHAR_SCALE_X;
                } else if (strcmp(tag, "/scale") == 0) {
                    Vec one = {1.0f, 1.0f, 1.0f};
                    flags &= ~MSG_CHAR_SCALE_X;
                    scale = one;
#else
                } else if (strcmp(tag, "scale") == 0) {
                    sscanf(args, "%f", &scale);
                } else if (strcmp(tag, "/scale") == 0) {
                    scale = 1.0f;
#endif
                } else if (strcmp(tag, "wait") == 0) {
                    sscanf(args, "%d", &waitTime);
                    time += waitTime;
                } else if (strcmp(tag, "speed") == 0) {
                    sscanf(args, "%d", &step);
                } else if (strcmp(tag, "/speed") == 0) {
                    step = data->parseSpeed;
                } else if (strcmp(tag, "dynamic") == 0) {
                    sscanf(args, "%f", &dynScale); // a <dynamic> tag without a number would leave dynScale unset
                    flags |= MSG_CHAR_DYNAMIC;
                } else if (strcmp(tag, "/dynamic") == 0) {
                    flags &= ~MSG_CHAR_DYNAMIC;
                } else if (strcmp(tag, "shake") == 0) {
                    flags |= MSG_CHAR_SHAKE;
                } else if (strcmp(tag, "/shake") == 0) {
                    flags &= ~MSG_CHAR_SHAKE;
                } else if (strcmp(tag, "wave") == 0) {
                    flags |= MSG_CHAR_WAVE;
                } else if (strcmp(tag, "/wave") == 0) {
                    flags &= ~MSG_CHAR_WAVE;
                } else if (strcmp(tag, "col") == 0) {
#ifdef TARGET_PC
                    {
                        /* PC port: the tag is 0xRRGGBBAA, which only a big-endian u32 lays out
                         * as the GXColor bytes that FontDrawColor reads */
                        u32 rgba = 0;
                        sscanf(args, "%x", &rgba);
                        entry->color = (GXColor){(u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)rgba};
                    }
#else
                    sscanf(args, "%x", &entry->value);
#endif
                    entry->glyph = MSG_GLYPH_COLOR;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "/col") == 0) {
#ifdef TARGET_PC
                    entry->color = (GXColor){0x00, 0x00, 0x00, 0xFF}; /* PC port: 0x000000FF as GXColor bytes */
#else
                    entry->value = 0xFF;
#endif
                    entry->glyph = MSG_GLYPH_COLOR;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "noise") == 0) {
                    entry->value = 1;
                    entry->glyph = MSG_GLYPH_NOISE;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "/noise") == 0) {
                    entry->value = 0;
                    entry->glyph = MSG_GLYPH_NOISE;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "dkey") == 0) {
                    entry->value = 1;
                    entry->glyph = MSG_GLYPH_DKEY;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "/dkey") == 0) {
                    entry->value = 0;
                    entry->glyph = MSG_GLYPH_DKEY;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "plate") == 0) {
                    window->type = 2;
                    step = 0;
                    data->parseSpeed = 0;
                } else if (strcmp(tag, "system") == 0) {
                    static const GXColor c = {0xDC, 0xC8, 0xDC, 0xFF};
                    window->type = 3;
                    entry->value = *((const u32*)(&c));
                    entry->glyph = MSG_GLYPH_COLOR;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "tec") == 0) {
                    static const GXColor c = {0x00, 0xD9, 0x14, 0xFF};
                    window->type = 8;
                    entry->value = *((const u32*)(&c));
                    entry->glyph = MSG_GLYPH_COLOR;
                    entry->time = time;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "majo") == 0) {
                    window->type = 9;
                } else if (strcmp(tag, "diary") == 0) {
                    window->type = 7;
                } else if (strcmp(tag, "housou") == 0) {
                    window->type = 10;
                } else if (strcmp(tag, "boss") == 0) {
                    window->type = 6;
                } else if (strcmp(tag, "kanban") == 0) {
                    window->type = 4;
                    step = 0;
                    data->parseSpeed = 0;
                } else if (strcmp(tag, "small") == 0) {
                    window->type = 1;
                    data->flags |= MSG_WIN_FLAG_SMALL;
                } else if (strcmp(tag, "clear") == 0) {
                    window->type = 11;
                } else if (strcmp(tag, "wpos") == 0) {
                    sscanf(args, "%hd%hd%hd%hd", &w16, &w14, &w12, &w10);
                    window->flags |= WINDOW_FLAG_FIXED_POS;
                    window->x = w16;
                    window->y = w14;
                    window->height = w12;
                    window->width = w10;
                } else if (strcmp(tag, "select") == 0) {
                    window->type = 5;
                    data->flags |= MSG_WIN_FLAG_SELECT;
                    // BUG: five %hd for four pointers; a fifth number in the tag would be written through a stray argument
#ifdef TARGET_PC
                {
                    /* PC port: the fifth number goes to a dummy. On x86-64 and arm64 the missing fifth argument is
                     * whatever the stack or register holds, a wild write for a text (a mod's, a language pack's)
                     * whose select tag has five numbers. */
                    s16 unused;
                    sscanf(args, "%hd%hd%hd%hd%hd", &sel0, &sel1, &data->selWidth, &data->selY, &unused);
                }
#else
                    sscanf(args, "%hd%hd%hd%hd%hd", &sel0, &sel1, &data->selWidth, &data->selY);
#endif
                    flags |= MSG_CHAR_OPTION_0;
                    data->defSel = sel0;
                    data->cancel = sel1;
                    data->numSel = 1;
                    step = 0;
                } else if (strcmp(tag, "se") == 0) {
                    sscanf(args, "%d", &entry->value);
                    entry->glyph = MSG_GLYPH_SE;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "vol") == 0) {
                    sscanf(args, "%d", &entry->value);
                    entry->glyph = MSG_GLYPH_VOL;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "nowinse") == 0) {
                    window->flags |= WINDOW_FLAG_NO_SE;
#if VERSION >= VERSION_DEMO_EU
                } else if (strcmp(tag, "center") == 0) {
                    centerStart = data->charCount;
                } else if (strcmp(tag, "/center") == 0) {
                    s32 c = centerStart;
                    s32 shift = (420 - x) / 2;
                    MsgChar* ch = &data->entries[c];
                    for (; c < data->charCount; c++, ch++) {
                        if (ch->glyph < MSG_GLYPH_PAPER_END) {
                            ch->x += shift;
                        }
                    }
#endif
                } else if (strcmp(tag, "icon") == 0) {
#if VERSION != VERSION_DEMO_US
                    isHM = FALSE;
#endif
                    if ((((s8)args[0]) >= '0') && (((s8)args[0]) <= '9')) {
                        sscanf(args, "%d%f%d%d%d", &iconId, &iconScale, &iconOfsX, &iconOfsY, &iconAdvance);
                    } else {
                        sscanf(args, "%s%f%d%d%d", name, &iconScale, &iconOfsX, &iconOfsY, &iconAdvance);
#if VERSION != VERSION_DEMO_US
                        if (strcmp(name, "HM") == 0) {
                            isHM = TRUE;
                        }
#endif
                        iconId = msgIconStr2ID(name);
                        if (iconId < 0) {
                            iconId = 0;
                        }
                    }
#if VERSION != VERSION_DEMO_US
                    if (!isHM)
#endif
                    {
                        entry->value = iconId;
                        // FAKE: called through a u16-parameter cast (target passes clrlwi r5 with no extsh to the s16 parameter)
                        ((void (*)(u16*, u16*, u16))iconGetWidthHight)(&iw, &ih, iconId);
                        entry->glyph = MSG_GLYPH_ICON;
                        entry->x = (x + ((0.5f * iw) * iconScale)) + iconOfsX;
#if VERSION <= VERSION_JP
                        x += 0x28;
#endif
                        entry->y = y - (ih * iconScale);
                        entry->iconY = iconOfsY;
                        entry->scale = iconScale;
                        entry->time = time;
                        entry++;
#if VERSION >= VERSION_US
                        x += iconAdvance + 0x28;
#endif
                        time += step;
                        data->charCount++;
                    }
#if VERSION != VERSION_DEMO_US
                    else {
#if VERSION >= VERSION_US
                        if ((u32)gp->language == 0) {
                            glyph = kanjiSearch(0x94B5);
                        } else {
                            glyph = hankakuSearch(-48);
                        }
                        iw = scale.x * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4));
                        entry->glyph = glyph;
                        entry->x = x;
                        entry->y = y - (s16)(10.0f * scale.y);
                        entry->scale = scale.x;
#else
                        glyph = kanjiSearch(0x94B5);
                        iw = scale * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4));
                        entry->glyph = glyph;
                        entry->x = x;
                        entry->y = y - (s16)(10.0f * scale);
                        entry->scale = scale;
#endif
                        entry->value = flags;
                        entry->value |= 8;
                        if (flags & MSG_CHAR_DYNAMIC) {

                            entry->rot = dynScale;
                        }
#if VERSION >= VERSION_US
                        if (maxScale < scale.y) {
                            maxScale = scale.y;
                        }
#else
                        if (maxScale < scale) {
                            maxScale = scale;
                        }
#endif
                        entry->time = time;
                        entry++;
                        x += iw;
                        time += step;
                        data->charCount++;
                    }
#endif
                } else if (strcmp(tag, "anim") == 0) {
                    sscanf(args, "%s%s%d%d%f%d", anim1, anim2, &animX, &animY, &animScale, &animFlip);
                    entry->value = animPoseEntry(anim1, 2);
                    animPoseSetAnim(entry->value, anim2, 1);
                    animPoseSetMaterialFlagOn(entry->value, ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2);
                    animPoseSetMaterialFlagOff(entry->value, ANIM_MATERIAL_FLAG_EVT_COLOR);
                    /* FAKE: one-trip loop (j is dead here); it enables the backend array-to-register pass that removes the zz/z3/z4 stores */
                    for (j = 0; j < 1; j++) {
                        animPoseSetGXFunc(msgw->poseId, msgDispKeyWait_render, 0);
                    }
                    entry->glyph = MSG_GLYPH_ANIM;
                    entry->x = animX;
                    entry->y = pageTopY - animY;
                    entry->scale = animScale;
                    entry->rot = (animFlip == 0) ? (0.0f) : (180.0f);
                    entry++;
                    time += step;
                    data->charCount++;
                } else if (strcmp(tag, "paper") == 0) {
                    sscanf(args, "%s%d", paper, &paperTime);
                    entry->npc = (strcmp(paper, "me") != 0) ? npcNameToPtr(paper) : NULL;
                    entry->glyph = MSG_GLYPH_PAPER;
                    entry->time = paperTime;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "/paper") == 0) {
                    sscanf(args, "%s", paper2);
                    entry->npc = (strcmp(paper2, "me") != 0) ? npcNameToPtr(paper2) : NULL;
                    entry->glyph = MSG_GLYPH_PAPER_END;
                    entry++;
                    data->charCount++;
                } else if (strcmp(tag, "r") == 0) {
                    data2 = data;
                    if (data2->window->type != 1) {
                        rubyX = x; // a <R...> tag without an <r> before it would read rubyX unset
                    }
                } else if ((((s8*)tag)[0] == 'R') || ((((s8*)tag)[0] == '/') && (((u32)((s8*)tag)[0]) == 0x2F72))) {
                    // BUG: the closing test compares one character with '/r' (0x2F72, a two-character constant), so it
                    // is always false and only <R...> tags are handled here
                    i = 1;
                    if (((s8*)tag)[0] != 'R') {
                        i = 2;
                    }
                    if (data->window->type == 1) {
                        continue;
                    }
                    half = ((s32)strlen(tag + i)) / 2;
                    total = 0;
                    j = i;
                    // FAKE: (u32) index casts here and in the second loop stop the IR optimizer from strength-reducing tag[j]/tag[i]; the backend
                    // does it instead and its pointer-IV init in the preheader keeps the dead j/i steps alive, as in the target (FAKES.md)
                    for (k = 0; k < half; k++) {
                        glyph = kanjiSearch(SJIS_CODE(&tag[(u32)j]));
#if VERSION >= VERSION_US
                        total += (s32)(0.55f * (scale.y * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4))));
#else
                        total += (s32)(0.55f * (scale * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4))));
#endif
                        j += 2;
                    }
#if VERSION >= VERSION_US
                    total = (u16)total - (0.55f * (4.0f * scale.x));
                    rubyX = ((rubyX + x) - 4.0f * scale.x - (u16)total) / 2;
#else
                    total = (u16)total - (0.55f * (4.0f * scale));
                    rubyX = ((rubyX + x) - 4.0f * scale - (u16)total) / 2;
#endif
                    /* second loop open-coded: kR is its counter and glyph its glyph (split webs, numbered as in the target) */
                    yx[0] = y;
                    ys[0] = y;
                    fp[0] = flags & 1;
                    fx[0] = flags & 1;
                    nn[0] = i;
                    *(int*)zz = 0;
                    *(int*)z3 = 1;
                    *(int*)z4 = 2;
                    for (kR = zz[0] + z3[0] + z4[0] - 3; kR < half; kR++) {
                        // FAKE: dead guard (never runs) redefining fp and ys at the ruby loop top; without it the whole
                        // function colours differently and gains an instruction (FAKES.md)
                        {
                            s32 gz = 0;
                            if (gz != 0) {
                                fp[0] = fx[0];
                                ys[0] = yx[0] + ys[0];
                            }
                        }
                        glyph = kanjiSearch(SJIS_CODE(&tag[(u32)nn[0]]));
#if VERSION >= VERSION_US
                        wwR = 0.55f * (scale.x * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4)));
#else
                        wwR = 0.55f * (scale * ((f32)(kanjiGetWidth((u16)(glyph + 0)) + 4)));
#endif
                        entry->glyph = glyph;
                        entry->x = rubyX;
                        entry->y = ys[0];
#if VERSION >= VERSION_US
                        entry->scale = 0.55f * scale.x;
#else
                        entry->scale = 0.55f * scale;
#endif
                        entry->value = flags;
                        if (fp[0]) {
                            entry->rot = dynScale;
                        }
                        fx[0] = fp[0];
                        yx[0] = ys[0];
                        entry->time = 0;
                        rubyX += wwR;
                        entry++;
                        data->charCount++;
                        nn[0] += 2;
                    }
                    continue;
                }
                if ((*((const s8*)next)) == '\n') {
                    msg++;
                }
                continue;
            default:
                if (((((u32)gp->language) == 0) && (((s8)check) >= '0')) && (((s8)check) <= '9')) {
                    glyphD = kanjiSearch((u16)(check + 0x821F));
                } else {
                    glyphD = hankakuSearch(check);
                }
#if VERSION >= VERSION_US
                wwD = scale.x * ((f32)(kanjiGetWidth((u16)(glyphD + 0)) + 2));
                entry->glyph = glyphD;
                entry->x = x;
                entry->y = y - (s16)(10.0f * scale.y);
                entry->scale = scale.x;
#else
                wwD = scale * ((f32)(kanjiGetWidth((u16)(glyphD + 0)) + 2));
                entry->glyph = glyphD;
                entry->x = x;
                entry->y = y - (s16)(10.0f * scale);
                entry->scale = scale;
#endif
                entry->value = flags;
                if ((*((const s8*)msg)) != ' ') {
                    entry->value |= 8;
                }
                if (flags & MSG_CHAR_DYNAMIC) {
                    entry->rot = dynScale;
                }
#if VERSION >= VERSION_US
                if (maxScale < scale.y) {
                    maxScale = scale.y;
                }
#else
                if (maxScale < scale) {
                    maxScale = scale;
                }
#endif
                entry->time = time;
                x += wwD;
                msg++;
                time += step;
                entry++;
                data->charCount++;
                continue;
        }
    }
}
#pragma opt_lifetimes reset

/** Returns the message driver's work (loaded files and the key-wait star pose). */
MessageWork* PORT_HOOKABLE(L_msgGetWork)(void) {
    return &msgWork;
}

/** qsort comparator for the message lookup table: orders entries by tag string, so msgSearch can binary-search. */
static int msg_compare(const void* a, const void* b) {
    s32 result = strcmp(msgSortDvdData + *(const u32*)a, msgSortDvdData + *(const u32*)b);
    if (result > 0) {
        return 1;
    }
    if (result < 0) {
        return -1;
    }
    return 0;
}

#ifdef TARGET_PC
/*
 * A room's message file read on its own (not into a slot), kept for good: the files read so far, by name
 * and language. Never freed: this file's statics are in the save state (port/src/aurora/savestate_aurora.c)
 * and the text is not, so a load puts back pointers to whatever was read before it (the one file kept
 * here before was freed when another was read, and a load could point back at the freed one).
 * Read straight from the disc (DVDOpen/DVDRead), not through DVDMgr's queue: msgSearch may run where
 * yielding to the disc thread can't (a menu's text).
 */
#define PORT_MSG_FILES 320
static struct {
    char name[16];
    s32 language;
    s32 length;
    char* text; /* NULL: not on the disc */
} sMsgFiles[PORT_MSG_FILES];
static s32 sMsgFileCount;

static const char* portMsgFile(const char* name, s32* length) {
    char path[160];
    const char* p;
    DVDFileInfo info;
    char* text = NULL;
    s32 i, len = 0;

    for (i = 0; i < sMsgFileCount; i++) {
        if (sMsgFiles[i].language == gp->language && !strcmp(sMsgFiles[i].name, name)) {
            *length = sMsgFiles[i].length;
            return sMsgFiles[i].text;
        }
    }
    snprintf(path, sizeof(path), "%s/./%s/%s.txt", getMarioStDvdRoot(), language_dir[gp->language], name);
    p = path;
#ifdef PORT_MODS
    p = portModsPathAlias(p); /* (as DVDMgrOpen: a mod's room reads another's files) */
#endif
    if (DVDConvertPathToEntrynum(p) != -1 && DVDOpen(p, &info)) {
        const s32 rounded = (s32)OSRoundUp32B(info.length);
        len = (s32)info.length;
        text = (char*)calloc(1, (size_t)rounded + 2);
        if (text != NULL && DVDReadPrio(&info, text, rounded, 0, 2) < len) {
            free(text);
            text = NULL;
        }
        DVDClose(&info);
    }
    if (text == NULL) {
        len = 0;
    }
    if (sMsgFileCount < PORT_MSG_FILES) {
        snprintf(sMsgFiles[sMsgFileCount].name, sizeof(sMsgFiles[0].name), "%s", name);
        sMsgFiles[sMsgFileCount].language = gp->language;
        sMsgFiles[sMsgFileCount].length = len;
        sMsgFiles[sMsgFileCount].text = text;
        sMsgFileCount++;
    }
    *length = len;
    return text;
}

/*
 * A partner's joining text (lecture_friends_NN, all_party_lecture) is only in the message file of the
 * map that partner joins in (tools/port/evt_lint.py: the same in every language). The open world has a
 * partner join in another partner's place, whose map file lacks it: the text comes from its own file.
 */
static const char* portMsgOtherFile(const char* tag) {
    static const struct {
        int last;          /* the partner's lecture_friends_NN, up to this NN */
        const char* file;  /* the map file that has them */
    } kFiles[] = {{3, "gor_01"}, {6, "hei_01"}, {8, "win_04"}, {11, "tou_10"}, {14, "usu_00"}, {16, "muj_05"},
                  {20, "gor_01"}};
    const char* sText;
    s32 sLength;
    const char* file = NULL;
    const char* s;
    int n, k;

    if (strncmp(tag, "lecture_friends_", 16) != 0 || tag[16] < '0' || tag[16] > '9' || tag[17] < '0' ||
        tag[17] > '9') {
        return NULL;
    }
    n = (tag[16] - '0') * 10 + (tag[17] - '0');
    for (k = 0; k < (int)(sizeof(kFiles) / sizeof(kFiles[0])) && file == NULL; k++) {
        if (n <= kFiles[k].last) {
            file = kFiles[k].file;
        }
    }
    if (file == NULL) {
        return NULL;
    }
    sText = portMsgFile(file, &sLength);
    if (sText == NULL) {
        return NULL;
    }
    /* tag, text, tag, text, ... each NUL-terminated, an empty tag at the end */
    for (s = sText; s < sText + sLength && *s; s += strlen(s) + 1) {
        const int match = strcmp(s, tag) == 0;
        s += strlen(s) + 1;
        if (s >= sText + sLength) {
            break;
        }
        if (match) {
            return s;
        }
    }
    return NULL;
}

/*
 * A tag the loaded files lack, from the current area's other rooms' files. The game loads one room's
 * messages (slot 0) and the global ones; a battle's or a scene's lines are in the room the story plays
 * it in (Hooktail's in gon_11, Bonetail's in jon_06, the Shadow Queen's in las_29). A fight met
 * anywhere else (a battle of the area's table started in another of its rooms, a mode's room, a warp
 * that skips the scene's room) printed the red "No messages" over the boss for each line. Each area's
 * files are read once (per language) into one index sorted by tag; nothing of it is ever freed (a
 * save state puts these statics back to an older index, which must still be there).
 */
typedef struct PortMsgPair {
    const char* tag;
    const char* text;
    s32 file; /* its room's file: sAreaFiles index */
} PortMsgPair;

#define PORT_MSG_AREAS 24
#define PORT_MSG_AREA_FILES 512
static struct {
    char area[4];
    s32 language;
    s32 count;
    PortMsgPair* pairs;
} sMsgAreas[PORT_MSG_AREAS];
static s32 sMsgAreaCount;
static char sAreaFiles[PORT_MSG_AREA_FILES][16];
static s32 sAreaFileCount;

static int portMsgPairCompare(const void* a, const void* b) {
    return strcmp(((const PortMsgPair*)a)->tag, ((const PortMsgPair*)b)->tag);
}

/* the pairs of one file, appended to *pairs (grown); the new count */
static s32 portMsgAreaAdd(PortMsgPair** pairs, s32 count, s32* cap, const char* name) {
    s32 length, fileIndex;
    const char* text = portMsgFile(name, &length);
    const char* s;
    if (text == NULL || sAreaFileCount >= PORT_MSG_AREA_FILES) {
        return count;
    }
    fileIndex = sAreaFileCount++;
    snprintf(sAreaFiles[fileIndex], sizeof(sAreaFiles[fileIndex]), "%s", name);
    for (s = text; s < text + length && *s; s += strlen(s) + 1) {
        const char* tag = s;
        s += strlen(s) + 1;
        if (s >= text + length) {
            break;
        }
        if (count >= *cap) {
            const s32 grown = *cap != 0 ? *cap * 2 : 1024;
            PortMsgPair* more = (PortMsgPair*)realloc(*pairs, sizeof(PortMsgPair) * (size_t)grown);
            if (more == NULL) {
                return count;
            }
            *pairs = more;
            *cap = grown;
        }
        (*pairs)[count].tag = tag;
        (*pairs)[count].text = s;
        (*pairs)[count].file = fileIndex;
        count++;
    }
    return count;
}

/* the text of tag in another room's file of the current area (*file: that file's name), or NULL */
static const char* portMsgArea(const char* tag, const char** file) {
    const char* map = gp != NULL ? gp->currentMapName : NULL;
    PortMsgPair key;
    const PortMsgPair* found;
    s32 i, a = -1;

    /* the room's own Tattle (Goombella's of the room, each room's own): never another room's */
    if (map == NULL || strlen(map) < 4 || map[3] != '_' || !strcmp(tag, "msg_kuri_map")) {
        return NULL;
    }
    for (i = 0; i < sMsgAreaCount; i++) {
        if (!strncmp(sMsgAreas[i].area, map, 3) && sMsgAreas[i].language == gp->language) {
            a = i;
            break;
        }
    }
    if (a < 0) {
        PortMsgPair* pairs = NULL;
        s32 count = 0, cap = 0;
        char name[16];
        if (sMsgAreaCount >= PORT_MSG_AREAS) {
            return NULL;
        }
        /* the disc's rooms: <area>_00 ... (tou_13, las_30, tik_21 the most), and gon_bt00, hei_bt00 */
        for (i = 0; i < 40; i++) {
            snprintf(name, sizeof(name), "%.3s_%02d", map, (int)i);
            count = portMsgAreaAdd(&pairs, count, &cap, name);
        }
        snprintf(name, sizeof(name), "%.3s_bt00", map);
        count = portMsgAreaAdd(&pairs, count, &cap, name);
        if (count > 1) {
            qsort(pairs, (size_t)count, sizeof(PortMsgPair), portMsgPairCompare);
        }
        a = sMsgAreaCount++;
        memcpy(sMsgAreas[a].area, map, 3);
        sMsgAreas[a].area[3] = '\0';
        sMsgAreas[a].language = gp->language;
        sMsgAreas[a].count = count;
        sMsgAreas[a].pairs = pairs;
    }
    if (sMsgAreas[a].count == 0) {
        return NULL;
    }
    key.tag = tag;
    found = (const PortMsgPair*)bsearch(&key, sMsgAreas[a].pairs, (size_t)sMsgAreas[a].count, sizeof(PortMsgPair),
                                        portMsgPairCompare);
    if (found == NULL) {
        return NULL;
    }
    *file = sAreaFiles[found->file];
    return found->text;
}

/* Goombella's Tattles of what has none: Paper Cut's own lines (npc_tattles_aurora.c sets them in the
 * player's language; these where nothing set them, the headless build) */
static const char* portMsgBuiltIn(const char* tag) {
    if (!strcmp(tag, "pc_tattle_npc")) {
        return "Hmm... I don't know anything about this one, Mario.<p>Not even my notes from the University of "
               "Goom have everybody in them!<k>";
    }
    if (!strcmp(tag, "pc_tattle_map")) {
        return "Hmm... I can't tell you much about this place, Mario.<p>Let's look around and see what we "
               "find!<k>";
    }
    return NULL;
}
#endif

/**
 * Returns the text of the message with this tag, looking in the area file then the global file;
 * if neither has it, a per-language error string.
 */
const char* PORT_HOOKABLE(msgSearch)(const char* tag) {
    MessageEntry* entry;
    MsgLookupEntry* lookup;
    smartEntry* smart;
    s32 i, max, j, index;
    s32 result;

#ifdef PORT_MODS /* a mod's message for this tag wins (port/mods/mods.cpp) */
    {
        static int sOriginal; /* looking up the game's own text for a mod's (below) */
        const char* modded = sOriginal ? NULL : portModMsgSearch(tag, language_dir[gp->language]);
        if (modded != NULL) {
            /* its % conversions fitted to the game's own message's, so a whole text pack keeps every
             * one the game's has (portModsFitMessage) */
            const char* original;
            sOriginal = 1;
#ifdef TARGET_PC
            sMsgQuiet++; /* (the mod's own tags have none: not a missing message) */
#endif
            original = msgSearch(tag);
#ifdef TARGET_PC
            sMsgQuiet--;
#endif
            sOriginal = 0;
            if (original == ErrMessage[gp->language]) {
                original = NULL;
            }
            /* Play as Luigi: his name for Mario's; Paper Cut's text, as the ones below
             * (port/include/port/port_text.h: the font puts the game's random numbers back after it) */
            return portTextOwn(portLuigiText(portModsFitMessage(modded, original)));
        }
    }
#endif
#ifdef TARGET_PC /* the open world's own messages (port/include/port/open_world.h) */
    {
        const char* pc = portOpenMsg(tag);
        if (pc == NULL) {
            pc = portDuoMsg(tag); /* duo moves' names and help (port/include/port/duo.h) */
        }
        if (pc == NULL) {
            pc = portRulesMsg(tag); /* the remake rules' own lines (port/include/port/rules.h) */
        }
        if (pc == NULL) {
            pc = portFireBreathMsg(tag); /* Bowser's Fire Breath (port/include/port/fire_breath.h) */
        }
        if (pc != NULL) {
            /* Paper Cut's text, in each player's language: the font puts the game's random
             * numbers back after it (port/include/port/port_text.h) */
            return portTextOwn(pc);
        }
    }
#endif
#ifdef TTYD_BACKEND_AURORA /* the pause menu's Settings tab's own messages (port/src/aurora/pause_settings_aurora.c) */
    {
        const char* pc = portPauseSettingsMsg(tag);
        const char* hint = pc == NULL ? portJournalHintMsg(tag) : NULL; /* the Journal's hint (port/src/aurora/journal_hint_aurora.c): the game's tab's */
        if (pc == NULL && hint == NULL) {
            pc = portPhotoJournalMsg(tag); /* the Journal's Photos (port/src/aurora/photo_gallery_aurora.c) */
        }
        if (pc == NULL) {
            pc = portStatsJournalMsg(tag); /* the Journal's Stats (port/src/aurora/stats_journal_aurora.c) */
        }
        if (pc == NULL) {
            pc = portTrackerJournalMsg(tag); /* the Journal's Tracker (port/src/aurora/tracker_journal_aurora.c) */
        }
        if (pc == NULL) {
            pc = portPm64JournalMsg(tag); /* the Journal's Paper Mario 64 Tattle Log (port/src/aurora/pm64_journal_aurora.c) */
        }
        if (pc != NULL) {
            /* Paper Cut's pages: their {A}, {B}, {X} and {Y} as the controller's buttons that do them
             * there (port/include/port/menu_buttons.h) */
            pc = portMenuText(pc);
        }
        if (pc == NULL) {
            pc = hint;
        }
        if (pc == NULL) {
            pc = portTravelJournalMsg(tag); /* fast travel from the Journal's map (port/include/port/warp_map.h) */
        }
        if (pc == NULL) {
            pc = portDirectorsCutMsg(tag); /* Director's Cut's restored badges (port/include/port/directors_cut.h) */
        }
        if (pc != NULL) {
            return portTextOwn(pc); /* (Paper Cut's text, as above) */
        }
    }
#elif defined(TARGET_PC) /* headless: the tags Aurora's sources answer above that the shared code asks for here */
    {
        const char* pc = portHeadlessMsg(tag);
        if (pc != NULL) {
            return portTextOwn(pc);
        }
    }
#endif
    for (i = 0; i < 2; i++) { // check both entries
        entry = &msgw->entries[i];
        smart = entry->lookup;
        if (smart) {
            lookup = smart->address;
            j = 0;
            max = entry->count;
            while (max >= j) {
                index = (j + max) / 2; // tag, message
                if (index >= entry->count) {
                    break;
                }
                result = strcmp(tag, msgEntryText(entry) + lookup[index].startaddr);
                if (!result) {
#ifdef TARGET_PC /* Play as Luigi: his name for Mario's (port/include/port/luigi.h) */
                    return portLuigiText(msgEntryText(entry) + lookup[index].endaddr);
#else
                    return msgEntryText(entry) + lookup[index].endaddr;
#endif
                }
                if (result < 0) {
                    max = index - 1;
                } else {
                    j = index + 1;
                }
            }
        }
    }
#ifdef TARGET_PC
    {
        /* a partner's joining text in another partner's place (the open world) */
        const char* other = portMsgOtherFile(tag);
        if (other != NULL) {
            return portLuigiText(other);
        }
    }
    {
        /* a line of another room of the area (a boss's, its fight met away from its room) */
        const char* file = NULL;
        const char* area = portMsgArea(tag, &file);
        if (area != NULL) {
            portTraceMsgElsewhere(tag, file, sMsgFile[0]);
            return portLuigiText(area);
        }
        area = portMsgBuiltIn(tag);
        if (area != NULL) {
            return portTextOwn(area);
        }
    }
    {
        /* a partner's line where the story never has that partner out (the
         * open world, chapter select: Yoshi in chapter 2): another
         * partner's line of the scene, Goombella's first, not the red "No
         * messages" */
        static const char* const kSuffix[] = {"_kur", "_nok", "_win", "_yos", "_viv", "_bom", "_chu"};
        static int sAlt;
        const size_t n = strlen(tag);
        int k, own = -1;
        if (sAlt) {
            return ErrMessage[gp->language];
        }
        for (k = 0; n > 4 && n < 0x40 && k < (int)(sizeof(kSuffix) / sizeof(kSuffix[0])); k++) {
            if (!strcmp(tag + n - 4, kSuffix[k])) {
                own = k;
            }
        }
        if (own >= 0) {
            char alt[0x40];
            const char* found = NULL;
            sAlt = 1;
            for (k = 0; k < (int)(sizeof(kSuffix) / sizeof(kSuffix[0])) && found == NULL; k++) {
                if (k != own) {
                    memcpy(alt, tag, n - 4);
                    strcpy(alt + n - 4, kSuffix[k]);
                    found = msgSearch(alt);
                    if (found == ErrMessage[gp->language]) {
                        found = NULL;
                    }
                }
            }
            sAlt = 0;
            if (found != NULL) {
                return found;
            }
        }
    }
    /* the log names every missing message once, with the files it was looked for in (sweeps grep for it) */
    if (sMsgQuiet == 0) {
        portTraceMsgMissing(tag, sMsgFile[0], sMsgCount[0], sMsgFile[1], sMsgCount[1]);
    }
#endif
    return ErrMessage[gp->language];
}

#ifdef TARGET_PC
/* whether a message with this tag is there (any of msgSearch's sources), without logging it as missing */
int portMsgHas(const char* tag) {
    const char* found;
    sMsgQuiet++;
    found = msgSearch(tag);
    sMsgQuiet--;
    return found != ErrMessage[gp->language];
}
#endif

/**
 * Returns TRUE if byte c starts a two-byte Shift-JIS character.
 * Later versions return FALSE for every language but Japanese.
 */
BOOL _ismbblead(u32 c) {
#if VERSION >= VERSION_US
    if ((u32)gp->language != 0) {
        return FALSE;
    }
#endif
    c = (u8)c;
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
        return TRUE;
    }
    return FALSE;
}

/**
 * Splits a markup tag at msg (after the '<') into its command name and argument string (up to '>'),
 * and returns the text after the tag.
 */
const char* PORT_HOOKABLE(msgGetCommand)(const char* msg, char* cmd, char* arg) {
    s32 i;
    BOOL inArg = FALSE;
    const char* m = msg;
    char* c = cmd;
    char* a = arg;

    for (i = 0; i < 0x80; i++, m++) {
        switch ((s8)*m) {
            case '>':
                if (!inArg) {
                    *c = 0;
                } else {
                    *a = 0;
                }
                return m + 1;
            case ' ':
                if (!inArg) {
                    *c = 0;
                    inArg = TRUE;
                } else {
                    *a++ = *m;
                }
                break;
            default:
                if (!inArg) {
                    *c++ = *m;
                } else {
                    *a++ = *m;
                }
                break;
        }
    }
    return NULL;
}

/** Maps an <icon> name in message text to its icon id, or -1. */
s32 PORT_HOOKABLE(msgIconStr2ID)(const char* str) {
    u32 i;

    for (i = 0; i < 18; i++) {
        if (!strcmp(msgIcon[i].name, str)) {
            return msgIcon[i].value;
        }
    }
    return -1;
}

/**
 * Opens a speech window for message text msg at the given priority (type and options come from the markup),
 * parses the text and sizes the window for its type; returns the window id.
 * callback is a function (s32 event, smartEntry*) the window notifies, 0 for none.
 */
s32 PORT_HOOKABLE(msgWindow_Entry)(const char* msg, EvtWord callback, u16 priority) {
    WindowEntry* window;
    char* copy;
    MsgWinData* data;
    smartEntry* smart;
    s32 id;

    // BUG: windowEntry returns -1 when all 7 window slots are in use; the id is not checked, so
    // windowGetPointer(-1) points before the window table and the writes below corrupt memory.
    id = windowEntry(priority);
    window = windowGetPointer(id);
    window->main = msgWindow_Main;
    window->delete = msgWindow_Delete;
    WIN_STATE(window) = 5;
    window->type = -1;
    window->flags |= WINDOW_FLAG_PRINTING;
    // BUG: strlen does not count the terminator, so strcpy writes one byte past the end of the block.
#ifdef TARGET_PC
    copy = __memAlloc(HEAP_DEFAULT, strlen(msg) + 1); /* PC port: the terminator too */
#else
    copy = __memAlloc(HEAP_DEFAULT, strlen(msg));
#endif
    strcpy(copy, msg);
    smart = smartAlloc(sizeof(MsgWinData), 1);
    data = smart->address;
    memset(data, 0, sizeof(MsgWinData));
    data->window = window;
    data->flags = MSG_WIN_FLAG_OPEN | MSG_WIN_FLAG_FIRST_FRAME;
    data->callback = callback;
    data->lastUpdateTime = gp->renderTime;
    data->openTime = gp->renderTime;
    data->talkSfxGroup = 0;
    data->talkVolume = 0x7F;
    data->parseTime = 0;
    data->parseX = 0;
    data->parseY = 0;
    data->parsePage = 0;
    data->parseScroll = 0;
    data->parseSpeed = 25;
    data->pageCount = 0;
#ifdef TARGET_PC
    data->portText = portTextIsOwn(msg); /* Paper Cut's text (msgDisp) */
#endif
    msgAnalize(smart, copy);
#ifdef TARGET_PC
    portSpeechMessage(window, copy); /* read aloud, page by page (port/include/port/speech.h) */
#endif
    if (window->type == 1) {
        data->flags |= MSG_WIN_FLAG_NO_PAGES;
    }
    __memFree(HEAP_DEFAULT, copy);
    window->userdata = smart;
    data = window->userdata->address;
    data->talkTimer = 0;
    data->talkPoseSet = 0;
    switch (window->type) {
        case 11:
            window->main = msgWindow_Clear_Main;
        case 0:
        case 2:
        case 3:
        case 4:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
            if (!(window->flags & WINDOW_FLAG_FIXED_POS)) {
                window->x = -280.0f;
                window->y = 225.0f;
                window->height = 560.0f;
                window->width = 176.0f;
            }
            break;
        case 5:
            window->main = selectWindow_Main;
            window->x = (s16)(-WIN_DATA(window)->selWidth / 2);
            window->y = WIN_DATA(window)->selY;
            window->height = WIN_DATA(window)->selWidth;
            window->width = (s16)(WIN_DATA(window)->numSel * 36 + 40);
            WIN_DATA(window)->sel = WIN_DATA(window)->defSel;
            data->cursorY = window->y - (data->sel * 31 + 58);
            break;
        case 1:
#if VERSION >= VERSION_US
            window->priority = priority + 10;
#else
            window->priority = 10;
#endif
            window->height = VERSION_PICK(0.7f, 0.9f) * GXGetTexObjWidth(getWakuTexObj(3));
            window->width = VERSION_PICK(0.7f, 0.9f) * GXGetTexObjHeight(getWakuTexObj(3));
            break;
    }
    return id;
}

#pragma optimize_for_size on
/** Window delete callback: frees the message window's parsed text. */
static void msgWindow_Delete(WindowEntry* window) {
    MsgWinData* data;
    s32 i;

#ifdef TARGET_PC
    portSpeechClosed(window);
#endif
    if (window->userdata) {
        data = window->userdata->address;
        for (i = 0; i < data->charCount; i++) {
        }
        smartFree(window->userdata);
        window->userdata = NULL;
    }
}

#pragma optimize_for_size reset

/**
 * Appends message msg to an open message window and resumes printing;
 * the script commands use it to continue a conversation in the same window.
 */
void PORT_HOOKABLE(msgWindow_Add)(const char* msg, s32 id) {
    WindowEntry* window = windowGetPointer(id);
    smartEntry* smart = window->userdata;

#ifdef TARGET_PC
    if (smart == NULL) {
        return; /* PC port: the window has closed; its data would be NULL */
    }
#endif
    ((MsgWinData*)smart->address)->lastUpdateTime = gp->renderTime;
#ifdef TARGET_PC
    ((MsgWinData*)smart->address)->portText |= portTextIsOwn(msg); /* Paper Cut's text (msgDisp) */
#endif
    msgAnalize(smart, msg);
#ifdef TARGET_PC
    portSpeechAppend(window, msg);
#endif
    WIN_STATE(window) = 1;
    window->flags |= WINDOW_FLAG_PRINTING;
}

/** Resumes printing in a message window that is waiting. */
void PORT_HOOKABLE(msgWindow_Continue)(s32 id) {
    WindowEntry* window = windowGetPointer(id);
    MsgWinData* data;

#ifdef TARGET_PC
    if (window->userdata == NULL) {
        return; /* PC port: the window has closed (its message read through by then); its data would be NULL */
    }
#endif
    WIN_STATE(window) = 1;
    window->flags |= WINDOW_FLAG_PRINTING;
    data = window->userdata->address;
    data->lastUpdateTime = gp->renderTime;
}

/** Restarts a message window's text from the beginning. */
void PORT_HOOKABLE(msgWindow_Repeat)(s32 id) {
    WindowEntry* window = windowGetPointer(id);
    MsgWinData* data;

#ifdef TARGET_PC
    if (window->userdata == NULL) {
        return; /* PC port: the window has closed (its message read through by then); its data would be NULL */
    }
#endif
    WIN_STATE(window) = 1;
    window->flags |= WINDOW_FLAG_PRINTING;
    data = window->userdata->address;
    data->lastUpdateTime = gp->renderTime;
    data->openTime = gp->renderTime;
    data->playTime = 0;
    data->scrollTarget = 0;
    data->scrollPos = 0;
    data->nextEntry = 0;
}

/** Closes every open message window whose speaker tag is name, putting its speaker back in the standing pose. */
void PORT_HOOKABLE(msgWindow_ForceClose)(const char* name) {
    WindowEntry* window;
    MsgWinData* data;
    s32 i;

#ifdef TARGET_PC
    /* PC port: every slot (the port has 16, windowdrv.h); a window in a slot past the 7th was never closed,
     * and the script waiting on it (a mutter closed when Mario talks, a scene's evt_msg_close) waited forever */
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
#else
    for (i = 0; i < 7; i++) {
#endif
        window = windowGetPointer(i);
        if (window->flags && WIN_STATE(window) && window->userdata &&
            ((MsgWinData*)window->userdata->address)->tag &&
            !strcmp(((MsgWinData*)window->userdata->address)->tag, name)) {
            WIN_STATE(window) = 7;
            data = window->userdata->address;
            if (data->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                BtlUnit_ChangeStayAnim(data->unitId);
            } else {
                npcSetStayPose(data->speakerName);
            }
        }
    }
}

/** Steps a message window back one page (Z at a key wait); FALSE if already on the first page. */
static inline BOOL msgWinPrevPage(WindowEntry* window) {
    MsgWinData* d = WIN_DATA(window);
    d->page--;
    if (d->page < 0) {
        d->page = 0;
        return FALSE;
    }
    return TRUE;
}

/** Steps a message window forward one page, stopping at the last. */
static inline void msgWinNextPage(WindowEntry* window) {
    MsgWinData* d = WIN_DATA(window);
    s32 max;
    d->page++;
    max = d->pageCount - 1;
    if (d->page > max) {
        d->page = max;
    }
}

// FAKE: #pragma opt_common_subs off: no common subexpression elimination in msgWindow_Main, so its fields are reloaded as in the target; without it 445 of its 492 instructions differ (same length: reloads and registers)
#pragma opt_common_subs off
/**
 * Per-frame window update for speech windows: times the speaker's talk animation,
 * then runs the window states (fade in, printing, key wait with A to continue and Z/B to page back and forward,
 * fade out and close), playing the page sounds, and queues msgWindow_Disp.
 */
static void msgWindow_Main(WindowEntry* window) {
    smartEntry* smart = window->userdata;
    MsgWinData* data = smart->address;
    s32 se;
    s32 target;
    u32 trg;

#ifdef TARGET_PC
    /* the page shown now, read once and logged; who says it (an NPC's name, not a battle unit's) */
    portSpeechPage(window, data->page, (data->flags & MSG_WIN_FLAG_BATTLE_UNIT) ? NULL : data->speakerName);
    /* a message up: B pages through it on every controller, as in the menus; its pictures of the game's
     * buttons stay the field's (a lesson's "press B" is the hammer's button: port/include/port/menu_buttons.h) */
    portMenuMarkText(1);
#endif
    if (data->talkTimer) {
        if (!data->talkPoseSet) {
            if (data->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                BtlUnit_ChangeTalkAnim(data->unitId);
            } else {
                npcSetTalkPose(data->speakerName);
            }
            data->talkPoseSet = 1;
        }
        data->talkTimer--;
        if (data->talkTimer == 0) {
            MsgWinData* d2 = smart->address;
            if (d2->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                BtlUnit_ChangeStayAnim(d2->unitId);
            } else {
                npcSetStayPose(d2->speakerName);
            }
            data->talkPoseSet = 0;
        }
    }

    switch (WIN_STATE(window)) {
        case 5:
            if (!(window->flags & WINDOW_FLAG_NO_SE)) {
                se = msgWindowOpen_SE_Data[window->type * 2];
                if (se >= 0) {
                    psndSFXOn(SFX_NAME_WORD(se | 0x20000));
                }
            }
            window->timestamp = gp->renderTime;
            WIN_STATE(window) = 6;
        case 6:
            window->flags |= WINDOW_FLAG_PRINTING;
            window->alpha = MSG_FADE_MS(gp->renderTime - window->timestamp) * 255 / 200;
            if (window->alpha > 255) {
                window->alpha = 255;
                WIN_STATE(window) = 1;
                data->openTime = gp->renderTime;
                data->lastUpdateTime = gp->renderTime;
            }
            break;
        case 7:
            if (!(window->flags & WINDOW_FLAG_NO_SE)) {
                se = msgWindowOpen_SE_Data[window->type * 2 + 1];
                if (se >= 0) {
                    psndSFXOn(SFX_NAME_WORD(se | 0x20000));
                }
            }
            window->timestamp = gp->renderTime;
            WIN_STATE(window) = 8;
        case 8:
            if (window->alpha == 0) {
                windowDelete(window);
                return;
            }
            window->alpha = 255 - MSG_FADE_MS(gp->renderTime - window->timestamp) * 255 / 200;
            if (window->alpha <= 0) {
                window->alpha = 0;
            }
            break;
        case 1:
            switch (msgMain(window->userdata)) {
                case 0:
                    WIN_STATE(window) = 7;
                    if (WIN_DATA(window)->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                        BtlUnit_ChangeStayAnim(WIN_DATA(window)->unitId);
                    } else {
                        npcSetStayPose(WIN_DATA(window)->speakerName);
                    }
                    break;
                case 1:
                    WIN_STATE(window) = 2;
                    window->flags &= ~WINDOW_FLAG_PRINTING;
                    if (WIN_DATA(window)->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                        BtlUnit_ChangeStayAnim(WIN_DATA(window)->unitId);
                    } else {
                        npcSetStayPose(WIN_DATA(window)->speakerName);
                    }
                    break;
                case 2:
                    WIN_STATE(window) = 3;
                    break;
                case 3:
                    break;
            }
            break;
        case 3:
            target = PAD_BUTTON_A;
            if (WIN_DATA(window)->flags & MSG_WIN_FLAG_KEY_X) {
                target = PAD_BUTTON_A | PAD_BUTTON_X;
            }
            trg = keyGetButtonTrg(0);
            if (trg & (target | PAD_BUTTON_B)) {
                WIN_STATE(window) = 1;
                data->pageState = 2;
                if (keyGetButtonTrg(0) & PAD_BUTTON_B) {
                    if (!(data->flags & MSG_WIN_FLAG_NO_FAST)) {
                        data->flags |= MSG_WIN_FLAG_FAST;
                    }
                }
            }
#if VERSION >= VERSION_US
            else // US: one key per frame
#endif
                if (keyGetButtonTrg(0) & PAD_TRIGGER_Z) {
                data->page = data->pageCount - 1;
                if (msgWinPrevPage(window) != 0U) {
                    WIN_STATE(window) = 9;
                    window->timestamp = gp->renderTime;
                    psndSFXOn(SFX_NAME_WORD(0x20041));
                    data->flags |= MSG_WIN_FLAG_PAGE_BACK;
                }
            }
            break;
        case 9:
            target = data->pageY[data->page];
            if (data->scrollPos >= target) {
                data->scrollPos = data->scrollPos - 0.6f * OSTicksToMilliseconds(gp->renderTime - window->timestamp);
                window->timestamp = gp->renderTime;
                if (data->scrollPos <= target) {
                    data->scrollPos = target;
                    WIN_STATE(window) = 11;
                }
            }
            break;
        case 10:
            target = data->pageY[data->page];
            if (data->scrollPos <= target) {
                data->scrollPos = data->scrollPos + 0.6f * OSTicksToMilliseconds(gp->renderTime - window->timestamp);
                window->timestamp = gp->renderTime;
                if (data->scrollPos >= target) {
                    data->scrollPos = target;
                    if (data->page == data->pageCount - 1) {
                        WIN_STATE(window) = 3;
                        data->flags &= ~MSG_WIN_FLAG_PAGE_BACK;
                    } else {
                        WIN_STATE(window) = 11;
                    }
                }
            }
            break;
        case 11:
            target = PAD_BUTTON_A;
            if (WIN_DATA(window)->flags & MSG_WIN_FLAG_KEY_X) {
                target = PAD_BUTTON_A | PAD_BUTTON_X;
            }
            trg = keyGetButtonTrg(0);
            if (trg & target) {
                WIN_STATE(window) = 10;
                window->timestamp = gp->renderTime;
                msgWinNextPage(window);
                psndSFXOn(SFX_NAME_WORD(0x20041));
            }
#if VERSION >= VERSION_US
            else // US: one key per frame
#endif
                if (keyGetButtonTrg(0) & PAD_BUTTON_B) {
                WIN_STATE(window) = 10;
                window->timestamp = gp->renderTime;
                data->page = data->pageCount - 1;
                psndSFXOn(SFX_NAME_WORD(0x20041));
            }
#if VERSION >= VERSION_US
            else // US: one key per frame
#endif
                if (keyGetButtonTrg(0) & PAD_TRIGGER_Z) {
                WIN_STATE(window) = 9;
                window->timestamp = gp->renderTime;
                if (msgWinPrevPage(window) != 0U) {
                    psndSFXOn(SFX_NAME_WORD(0x20041));
                }
            }
            break;
    }
    dispEntry(CAMERA_2D, 400.0f - window->priority, 0, msgWindow_Disp, window);
}

#pragma opt_common_subs reset
/**
 * Per-frame update for clear (type 11) windows, text with no frame:
 * prints, waits for A (or X) to page on and B to jump to the last page, then fades out; queues msgWindow_Disp.
 */
static void msgWindow_Clear_Main(WindowEntry* window) {
    MsgWinData* data = WIN_DATA(window);

    switch (WIN_STATE(window)) {
        case 5:
            WIN_STATE(window) = 1;
            window->timestamp = gp->renderTime;
            data->openTime = gp->renderTime;
            data->lastUpdateTime = gp->renderTime;
        case 1:
        case 6:
            window->flags |= WINDOW_FLAG_PRINTING;
            if (window->alpha < 255) {
                window->alpha = MSG_FADE_MS(gp->renderTime - window->timestamp) * 255 / 200;
                if (window->alpha > 255) {
                    window->alpha = 255;
                }
            }
            if (WIN_STATE(window) != 2) {
                switch (msgMain(window->userdata)) {
                    case 0:
                        WIN_STATE(window) = 7;
                        window->timestamp = gp->renderTime;
                        if (WIN_DATA(window)->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                            BtlUnit_ChangeStayAnim(WIN_DATA(window)->unitId);
                        } else {
                            npcSetStayPose(WIN_DATA(window)->speakerName);
                        }
                        break;
                    case 1:
                        WIN_STATE(window) = 2;
                        window->flags &= ~WINDOW_FLAG_PRINTING;
                        if (WIN_DATA(window)->flags & MSG_WIN_FLAG_BATTLE_UNIT) {
                            BtlUnit_ChangeStayAnim(WIN_DATA(window)->unitId);
                        } else {
                            npcSetStayPose(WIN_DATA(window)->speakerName);
                        }
                        break;
                    case 2:
                        WIN_STATE(window) = 3;
                        break;
                    case 3:
                        break;
                }
            }
            break;
        case 7:
            if (window->alpha == 0) {
                windowDelete(window);
                return;
            }
            window->alpha = 255 - MSG_FADE_MS(gp->renderTime - window->timestamp) * 255 / 200;
            if (window->alpha <= 0) {
                window->alpha = 0;
            }
            break;
    }
    dispEntry(CAMERA_2D, 400.0f - window->priority, 0, msgWindow_Disp, window);
}

/**
 * Draw callback for a message window: draws the frame for its type (speech balloon with tail toward the speaker,
 * sign plate, system box, ...) and the text with msgDisp.
 */
static void msgWindow_Disp(CameraId camId, void* param) {
    CameraEntry* camera = camGetCurPtr();
    BOOL ret = FALSE;
    WindowEntry* window = param;
    Vec pos;
    f32 w, h;
    f32 ex;
    f32 ey;
    s32 sy;
    static u16 active;

    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});

    switch (window->type) {
        case 0:
        case 1:
        case 6:
        case 8:
        case 9:
            if (window->tailMode != 0) {
                if (window->tailMode & 0x10) {
                    pos = window->speakerPos;
                } else {
                    getScreenPoint(&window->speakerPos, &pos);
#ifdef TARGET_PC
                    if (portViewMirrored()) {
                        pos.x = -pos.x; /* mirror mode: at the speaker as the flipped 3D shows them */
                    }
#endif
                }
                switch (window->type) {
                    case 0:
                    case 6:
                    case 8:
                    case 9:
                        ex = intplGetValue(0, pos.x, (window->height / 2.0f) + window->x, window->alpha, 255);
                        ey = intplGetValue(0, pos.y, window->y - (window->width / 2.0f), window->alpha, 255);
                        w = intplGetValue(0, 10.0f, window->height, window->alpha, 255);
                        h = intplGetValue(0, 10.0f, window->width, window->alpha, 255);
                        windowDispGX_Message(window->type, window->tailMode, window->alpha, ex - (w / 2.0f), ey + (h / 2.0f), w, h, pos.x, pos.y);
                        if (window->alpha == 255) {
                            GXSetScissor(camera->scissor[0], 240.0f - (window->y - 36.0f), camera->scissor[2], 100);
                            ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
                        }
                        break;
                    case 1:
                        window->x = pos.x - (window->height / 2.0f);
                        if (window->tailMode & 8) {
                            window->y = pos.y - 80.0f;
                        } else {
                            window->y = 60.0f + pos.y;
                        }
                        ex = intplGetValue(0, pos.x, (window->height / 2.0f) + window->x, window->alpha, 255);
                        ey = intplGetValue(0, pos.y, window->y - (window->width / 2.0f), window->alpha, 255);
                        w = intplGetValue(0, 10.0f, window->height, window->alpha, 255);
                        h = intplGetValue(0, 10.0f, window->width, window->alpha, 255);
                        windowDispGX_Message(window->type, window->tailMode, window->alpha, ex - (w / 2.0f), ey + (h / 2.0f), w, h, pos.x, pos.y);
                        if (window->alpha == 255) {
                            sy = 240 - (((s32)window->y) - 10);
                            if (sy < 0) {
                                sy = 0;
                            }
                            if (sy > 480) {
                                sy = 480;
                            }
                            GXSetScissor(camera->scissor[0], sy, camera->scissor[2], 0x7E);
                            msgDisp(window->userdata, window->alpha, VERSION_PICK(20.0f, 28.0f) + window->x, window->y - VERSION_PICK(7.0f, 6.0f));
                        }
                        break;
                }
            } else {
                windowDispGX_Message(window->type, 0, window->alpha, window->x, window->y, window->height, window->width, 0.0f, 0.0f);
                GXSetScissor(camera->scissor[0], (240.0f - (window->y - 36.0f)) - 8.0f, camera->scissor[2], 0x7E);
                ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
            }
            break;
#if VERSION >= VERSION_JP
        case 2:
        case 4:
        case 10:
            windowDispGX_Kanban(window->type, window->alpha, window->x, window->y, window->height, window->width);
            GXSetScissor(camera->scissor[0], (240.0f - (window->y - 36.0f)) - 2.0f, camera->scissor[2], 0x68);
            ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
            break;
#else
        // the demo draws every signboard window the same way
        case 2:
        case 4:
        case 10:
#endif
        case 7:
            windowDispGX_Kanban(window->type, window->alpha, window->x, window->y, window->height, window->width);
            GXSetScissor(camera->scissor[0], (240.0f - (window->y - 36.0f)) - 8.0f, camera->scissor[2], 0x7E);
            ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
            break;
        case 3:
            windowDispGX_System(window->type, window->alpha, window->x, window->y, window->height, window->width);
            GXSetScissor(camera->scissor[0], (240.0f - (window->y - 36.0f)) - 8.0f, camera->scissor[2], window->width - 50.0f);
            ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
            break;
        case 11:
            GXSetScissor(camera->scissor[0], (240.0f - (window->y - 36.0f)) - 8.0f, camera->scissor[2], window->width - 50.0f);
            ret = msgDisp(window->userdata, window->alpha, 62.0f + window->x, window->y - 36.0f);
            break;
        case 5:
            break;
    }

    GXSetScissor(camera->scissor[0], camera->scissor[1], camera->scissor[2], camera->scissor[3]);
    if (ret) {
        if (active == 0) {
            active = 1;
        }
        animPoseMain(msgw->poseId);
        animPoseDraw(msgw->poseId, (window->x + window->height) - 52.0f, 31.0f + (window->y - window->width), 0.0f, 180.0f, 5.0f, 2);
    }
}

/**
 * Per-frame update for selection lists (type 5):
 * fades in, moves the choice with the stick or D-pad (wrapping), confirms with A,
 * and on B cancels to the option given by the markup (or -1) unless cancelling is off; then fades out.
 */
static void selectWindow_Main(WindowEntry* window) {
    MsgWinData* data = window->userdata->address;
    s8 prev;

    msgMain(window->userdata);
    switch (WIN_STATE(window)) {
        case 5:
            window->alpha += 25;
            if (window->alpha > 255) {
                window->alpha = 255;
                WIN_STATE(window) = 1;
            }
            break;
        case 1:
#ifdef TARGET_PC
            portMenuMark(1); /* a choice up: B picks its cancel on every controller (port/include/port/menu_buttons.h) */
#endif
            prev = data->sel;
            if ((keyGetButtonTrg(0) & PAD_BUTTON_UP) || (keyGetDirTrg(0) & DEMO_STICK_UP)) {
                data->sel--;
            } else if ((keyGetButtonRep(0) & PAD_BUTTON_UP) || (keyGetDirRep(0) & DEMO_STICK_UP)) {
                if (data->sel) {
                    data->sel--;
                }
            }
            if ((keyGetButtonTrg(0) & PAD_BUTTON_DOWN) || (keyGetDirTrg(0) & DEMO_STICK_DOWN)) {
                data->sel++;
            } else if ((keyGetButtonRep(0) & PAD_BUTTON_DOWN) || (keyGetDirRep(0) & DEMO_STICK_DOWN)) {
                if (data->sel < data->numSel - 1) {
                    data->sel++;
                }
            }
            if (data->sel < 0) {
                data->sel = data->numSel - 1;
            }
            if (data->sel >= data->numSel) {
                data->sel = 0;
            }
            if (prev != data->sel) {
                psndSFXOn(SFX_NAME_WORD(0x20005));
            }
            if (keyGetButtonTrg(0) & PAD_BUTTON_B) {
                switch (data->cancel) {
                    case -1:
                        break;
                    case -2:
                        data->sel = -1;
                        WIN_STATE(window) = 7;
                        psndSFXOn(SFX_NAME_WORD(0x20013));
                        break;
                    default:
                        data->sel = data->cancel;
                        WIN_STATE(window) = 7;
                        psndSFXOn(SFX_NAME_WORD(0x20013));
                        break;
                }
            } else if (keyGetButtonTrg(0) & PAD_BUTTON_A) {
                WIN_STATE(window) = 7;
                psndSFXOn(SFX_NAME_WORD(0x20012));
            }
            break;
        case 7:
            if (window->alpha == 0) {
                WIN_STATE(window) = 4;
                window->flags &= ~WINDOW_FLAG_PRINTING;
                return;
            }
            window->alpha -= 25;
            if (window->alpha <= 0) {
                window->alpha = 0;
            }
            break;
        case 4:
            return;
    }
    dispEntry(CAMERA_2D, 400.0f - window->priority, 0, selectWindow_Disp, window);
}

/**
 * Draw callback for a selection list: the white frame, the options,
 * and the pointing-hand cursor gliding to the selected line.
 */
static void selectWindow_Disp(CameraId camId, void* param) {
    WindowEntry* window = param; // cast to correct type
    MsgWinData* data = window->userdata->address;
    Vec pos;

    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, window->alpha}, window->x, window->y, window->height, window->width, 30.0f);
    msgDisp(window->userdata, window->alpha, 40.0f + window->x, window->y - 25.0f);
    pos.x = 2.0f + window->x;
    data->cursorY += 0.3f * (window->y - (f32)(data->sel * 31 + 58) - data->cursorY);
    pos.y = data->cursorY;
    pos.z = 0.0f;
    iconDispGxAlpha(&pos, ICON_FLAG_2D | ICON_FLAG_FLIP_X, ICON_1F0, window->alpha, 1.0f);
}

// target pad words (G5: section sizes must match or later DOL addresses shift)

#ifdef TARGET_PC
/* PC port: HUD scale (port/hud.h) tells the message and choice windows' draws apart */
int portHudIsMessage(DispCallback callback) {
    return callback == msgWindow_Disp || callback == selectWindow_Disp;
}

/* PC port, tests (port/src/aurora/story_test_aurora.c's text advance): 2 when a
 * choice is up and taking keys (its cursor, count and the text's tag out),
 * 1 when a message waits for A (at a page's end), 3 when one is still printing,
 * else 0 */
int portMsgWaiting(int* sel, int* numSel, const char** tag) {
    int i, found = 0;
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) { /* (every slot: a window past the 7th waits for A too) */
        WindowEntry* w = windowGetPointer(i);
        MsgWinData* data;
        if (!(w->flags & WINDOW_FLAG_USED) || w->userdata == NULL) {
            continue;
        }
        data = WIN_DATA(w);
        if (w->main == selectWindow_Main && WIN_STATE(w) == 1) {
            *sel = data->sel;
            *numSel = data->numSel;
            *tag = data->tag;
            return 2;
        }
        if (w->main == msgWindow_Main && (WIN_STATE(w) == 3 || WIN_STATE(w) == 11)) {
            *tag = data->tag;
            found = 1;
        } else if ((w->main == msgWindow_Main || w->main == msgWindow_Clear_Main) && found == 0) {
            *tag = data->tag;
            found = 3;
        }
    }
    return found;
}

/* PC port, Skip any scene (port/src/common/scene_skip_pc.c): a choice window is up in any state (fading in
 * too: one press there could be read once it takes keys) */
int portMsgChoiceOpen(void) {
    int i;
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
        WindowEntry* w = windowGetPointer(i);
        if ((w->flags & WINDOW_FLAG_USED) && w->main == selectWindow_Main) {
            return 1;
        }
    }
    return 0;
}

/* PC port, tests: each window slot in use, for a script that waits on one (the story watch's dump) */
void portMsgDump(void) {
    int i;
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
        WindowEntry* w = windowGetPointer(i);
        if (!(w->flags & WINDOW_FLAG_USED)) {
            continue;
        }
        if (w->userdata == NULL || (w->main != msgWindow_Main && w->main != msgWindow_Clear_Main &&
                                    w->main != selectWindow_Main)) {
            printf("[story]   window %d: flags 0x%x (not a message)\n", i, (unsigned)w->flags);
            continue;
        }
        {
            MsgWinData* data = WIN_DATA(w);
            printf("[story]   window %d: %s '%s' flags 0x%x state %d alpha %d page %d/%d pageState %d play %d "
                   "entry %d/%d scroll %d/%d\n",
                   i, w->main == selectWindow_Main ? "choice" : "message", data->tag ? data->tag : "", (unsigned)w->flags,
                   (int)WIN_STATE(w), (int)w->alpha, (int)data->curPage, (int)data->pageCount, (int)data->pageState,
                   (int)data->playTime, (int)data->nextEntry, (int)data->charCount, (int)data->scrollPos,
                   (int)data->scrollTarget);
        }
    }
    fflush(stdout);
}
#endif

#ifdef PORT_MODS
#include "port/hooks/src_driver_msgdrv.inc"
#endif
