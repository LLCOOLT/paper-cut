/* Director's Cut (port/include/port/directors_cut.h): the game's own unused
 * content, put back while the setting is on.
 *
 * - Seven badges whose effects the battle code still has in full but which
 *   nothing in the game gives (src/data/item_data.c lists them hidden, named
 *   "in_unknown_item"): Mega Jump, Mega Smash, Mega Quake, Super Charge,
 *   Super Charge P, Triple Dip and Lucky Start P. Dazzle sells them for Star
 *   Pieces, with names and texts of ours (their own never shipped), sorted
 *   after the other badges.
 *   The Auto Command badge too (タツジーン, every action command done for
 *   you), which has no icon of its own: it borrows the Simplifier's.
 * - Boggly Woods' second set of enemy groups (win_02: Cleft and Pale Piranha
 *   mixes, rels/win/src/data/battle/battle_database_win.c), chosen by a flag
 *   nothing sets (GSWF_WIN02_ALT_BATTLES): taken as set, never written.
 * - Battles areas have but nothing starts (each REL's battle database hands
 *   them over from its _prolog, portDirectorsCutRel): Hooktail Castle's third
 *   Dull Bones, the Steeple's Swoopers on the stairs and Boos by the chest,
 *   Keelhaul Key's four Fuzzies. A cut field enemy is added to its map's setup
 *   as the disc's file lists the others, so the game's own code makes it.
 * - The level-up's confirmation box ("Is this OK?", on every disc), which
 *   nothing opens.
 * - The Red Buzzy family (kinds 172-174, units only in the European test
 *   module; CMakeLists.txt builds them into the Glitz Pit's REL): the Tiny
 *   Spinies' match, with Tattles of ours.
 * - The sewers' three cut pipe rooms, tik_09, tik_10 and tik_14: on every
 *   disc with their maps, text and scripts (tik.rel registers them), missing
 *   only from the area's map list (src/data/mapdata.c) and from a way in.
 *   Their MapData is the host's (mapDataPtr's hook), and tik_06's pipe Mario
 *   only ever comes out of (dokan_3, "You can't enter from here!") leads
 *   down into tik_09; tik_10 and tik_14 already lead back to it. Their two
 *   chests keep the game's own flags for them (GSWF_TIK10/14_CHEST_OPENED,
 *   1360 and 1361, nothing else uses).
 *
 * The item table's fields are changed in place as the setting turns on and
 * put back as it turns off, at the next message the game looks up. */
#include "port/directors_cut.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "evt/evt_badgeshop.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "driver/npcdrv.h"
#include "memory.h"
#include "port/rel_pc.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "data/mapdata.h"
#include "evt/evt_bero.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

typedef struct Badge {
    s32 id;
    s32 iconFrom; /* the badge whose icon it shows (0: its own) */
    const char* key;   /* our message keys: pc_dc_in_<key>, pc_dc_msg_<key>, pc_dc_menu_<key> */
    const char* name;  /* English */
    const char* text;  /* its description (the shop's and the badge list's) */
    const char* menu;  /* the short one under the menus */
    u16 starPieces;
} Badge;

static const Badge kBadges[] = {
    {ITEM_MEGA_JUMP, 0, "mega_jump", "Mega Jump",
     "Wear this to use Mega Jump, a mighty stomp. It costs 6 FP. A move cut from the original game.",
     "A mighty stomp. 6 FP.", 10},
    {ITEM_MEGA_SMASH, 0, "mega_smash", "Mega Smash",
     "Wear this to use Mega Smash, a heavy hammer blow. A move cut from the original game.",
     "A heavy hammer blow.", 10},
    {ITEM_MEGA_QUAKE, 0, "mega_quake", "Mega Quake",
     "Wear this to use Mega Quake, which shakes every enemy on the ground. It costs 7 FP. A move cut from the "
     "original game.",
     "Shakes every enemy on the ground. 7 FP.", 12},
    {ITEM_SUPER_CHARGE, 0, "super_charge", "Super Charge",
     "Wear this to use Super Charge, which builds up more attack power than Charge. A move cut from the original "
     "game.",
     "Charge up more than with Charge.", 6},
    {ITEM_SUPER_CHARGE_P, 0, "super_charge_p", "Super Charge P",
     "Wear this to let your partner use Super Charge, which builds up more attack power than Charge. A move cut "
     "from the original game.",
     "Your partner charges up more.", 6},
    {ITEM_TRIPLE_DIP, 0, "triple_dip", "Triple Dip",
     "Wear this to use three items in one turn. It costs 8 FP. Cut from the original game, where only two Double "
     "Dips gave it.",
     "Use three items in one turn. 8 FP.", 15},
    {ITEM_LUCKY_START_P, 0, "lucky_start_p", "Lucky Start P",
     "Wear this to give your partner a lucky bonus at the start of each battle. Cut from the original game.",
     "A lucky bonus for your partner at the start.", 8},
    {ITEM_AUTO_COMMAND_BADGE, ITEM_SIMPLIFIER, "auto_command", "Auto Command",
     "Wear this and every Action Command, Mario's and your partner's, does itself, perfectly. Cut from the "
     "original game.",
     "Every Action Command does itself.", 20},
};
#define BADGES ((int)(sizeof(kBadges) / sizeof(kBadges[0])))

static int* sOn;
static int sApplied = -1;
static ItemData sOriginal[BADGES];
static char sKeys[BADGES][3][40];

int portDirectorsCut(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("directors_cut", 0, 0, 1);
    }
    return *sOn;
}

/* the item table as the setting says */
static void sync(void) {
    const int on = portDirectorsCut();
    int i;
    if (on == sApplied) {
        return;
    }
    if (sApplied < 0) {
        for (i = 0; i < BADGES; i++) {
            sOriginal[i] = itemDataTable[kBadges[i].id];
        }
    }
    sApplied = on;
    for (i = 0; i < BADGES; i++) {
        ItemData* d = &itemDataTable[kBadges[i].id];
        if (!on) {
            *d = sOriginal[i];
            continue;
        }
        snprintf(sKeys[i][0], sizeof(sKeys[i][0]), "pc_dc_in_%s", kBadges[i].key);
        snprintf(sKeys[i][1], sizeof(sKeys[i][1]), "pc_dc_msg_%s", kBadges[i].key);
        snprintf(sKeys[i][2], sizeof(sKeys[i][2]), "pc_dc_menu_%s", kBadges[i].key);
        d->name = sKeys[i][0];
        d->description = sKeys[i][1];
        d->menu_description = sKeys[i][2];
        d->type_sort_order = (s16)(0x7000 + i); /* after the other badges */
        d->star_piece_price = kBadges[i].starPieces;
        if (kBadges[i].iconFrom != 0) {
            d->icon_id = itemDataTable[kBadges[i].iconFrom].icon_id;
        }
    }
}

int portDirectorsCutFlag(int gswf) {
    return gswf == GSWF_WIN02_ALT_BATTLES && portDirectorsCut();
}

int portDirectorsCutLevelUpConfirm(void) {
    return portDirectorsCut();
}

/* --- the areas' cut battles --- */

typedef struct Rel {
    char name[8];
    const PortDirectorsCutSelect* selects;
    const PortDirectorsCutEnemy* enemies;
} Rel;
static Rel sRels[8];
#define RELS ((int)(sizeof(sRels) / sizeof(sRels[0])))

void portDirectorsCutRel(const char* rel, const PortDirectorsCutSelect* selects,
                         const PortDirectorsCutEnemy* enemies) {
    int i;
    for (i = 0; i < RELS; i++) {
        if (sRels[i].name[0] == '\0' || strcmp(sRels[i].name, rel) == 0) {
            snprintf(sRels[i].name, sizeof(sRels[i].name), "%s", rel);
            sRels[i].selects = selects;
            sRels[i].enemies = enemies;
            return;
        }
    }
}

/* the linked REL's (a REL's pointers mean nothing while another is linked) */
static const Rel* linkedRel(void) {
    const char* name = portRelCurrentName();
    int i;
    for (i = 0; name != NULL && i < RELS && sRels[i].name[0] != '\0'; i++) {
        if (strcmp(sRels[i].name, name) == 0) {
            return &sRels[i];
        }
    }
    return NULL;
}

void* portDirectorsCutSelect(void* select) {
    const Rel* rel = portDirectorsCut() ? linkedRel() : NULL;
    const PortDirectorsCutSelect* s;
    for (s = rel != NULL ? rel->selects : NULL; s != NULL && s->select != NULL; s++) {
        if (s->select == select) {
            return s->restored;
        }
    }
    return select;
}

/* The map's cut enemies, as setup-file entries in the file's own format, so
 * setupDataBase makes them as it does the others; their battles come after
 * (no BTLNO names them). */
static void* sSetupMade; /* the setup with them added, this map load's */

void portDirectorsCutSetup(const char* map, void** data, unsigned int* size) {
    const Rel* rel = portDirectorsCut() ? linkedRel() : NULL;
    const PortDirectorsCutEnemy* e;
    char text[4096];
    int n = 0;
    char* out;
    if (rel == NULL || rel->enemies == NULL || (*data != NULL && *data == sSetupMade)) {
        return;
    }
    for (e = rel->enemies; e->map != NULL; e++) {
        if (strcmp(e->map, map) != 0) {
            continue;
        }
        n += snprintf(text + n, sizeof(text) - (size_t)n,
                      "\r\n<Npc>\r\n<name>\r\n\"%s\"\r\n</>\r\n<tribe>\r\n\"%s\"\r\n</>\r\n"
                      "<position>\r\n%d %d %d \r\n</>\r\n<dir>\r\n270 \r\n</>\r\n"
                      "<aitype>\r\n\"%s\"\r\n</>\r\n"
                      "<territory_type>\r\n\"NPC_TERRITORY_TYPE_SQUARE\"\r\n</>\r\n"
                      "<territory_base>\r\n%d %d %d \r\n</>\r\n"
                      "<territory_loiter>\r\n%d %d %d \r\n</>\r\n"
                      "<territory_homing>\r\n%d %d %d \r\n</>\r\n"
                      "<search_range>\r\n200 \r\n</>\r\n<search_angle>\r\n300 \r\n</>\r\n"
                      "<homing_range>\r\n1000 \r\n</>\r\n<homing_angle>\r\n360 \r\n</>\r\n"
                      "<btl_setup_no>\r\n\"BTLNO_INVALID\"\r\n</>\r\n</>\r\n",
                      e->name, e->tribe, (int)e->pos[0], (int)e->pos[1], (int)e->pos[2], e->ai, (int)e->pos[0],
                      (int)e->pos[1], (int)e->pos[2], (int)e->loiter[0], (int)e->loiter[1], (int)e->loiter[2],
                      (int)e->homing[0], (int)e->homing[1], (int)e->homing[2]);
        if (n >= (int)sizeof(text)) {
            return;
        }
    }
    if (n == 0) {
        return;
    }
    if (*data == NULL) {
        *size = 0;
    }
    out = (char*)_mapAlloc((u32)(*size + (unsigned int)n + 1));
    if (out == NULL) {
        return;
    }
    if (*size > 0) {
        memcpy(out, *data, *size);
    }
    memcpy(out + *size, text, (size_t)n + 1);
    *data = out;
    *size += (unsigned int)n;
    sSetupMade = out;
}

void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup);

void portDirectorsCutSetupDone(const char* map) {
    const Rel* rel = portDirectorsCut() ? linkedRel() : NULL;
    const PortDirectorsCutEnemy* e;
    sSetupMade = NULL; /* (the map heap may hand the next map's file the same place) */
    for (e = rel != NULL ? rel->enemies : NULL; e != NULL && e->map != NULL; e++) {
        NpcEntry* npc;
        int i;
        if (strcmp(e->map, map) != 0 || (npc = npcNameToPtr_NoAssert(e->name)) == NULL) {
            continue; /* another map's, or beaten */
        }
        npcSetupBattleInfo(npc, (NpcSetupBattleInfo*)e->battle);
        for (i = 0; i < 4; i++) {
            if (npc->slaves[i]) {
                npc->slaves[i]->battleInfo = npc->battleInfo;
            }
        }
    }
}

int portDirectorsCutDazzle(void) {
    const int on = portDirectorsCut();
    int i;
    sync();
    if (bdsw == NULL) {
        return 0;
    }
    for (i = 0; i < BADGES; i++) {
        const s16 id = (s16)kBadges[i].id;
        const s32 stock = badgeShop_get(bdsw->starmaniac_table, id);
        if (on && stock <= 0 && !pouchCheckItem((u32)id)) {
            badgeShop_add(bdsw->starmaniac_table, id, 1 - stock);
        } else if (!on && stock > 0) {
            badgeShop_add(bdsw->starmaniac_table, id, -stock);
        }
    }
    return on ? BADGES : 0;
}

int portDirectorsCutDazzleCount(void) {
    sync();
    return portDirectorsCut() && bdsw != NULL ? BADGES : 0;
}

int portDirectorsCutBadge(int item) {
    int i;
    sync();
    for (i = 0; i < BADGES; i++) {
        if (kBadges[i].id == item) {
            return 1;
        }
    }
    return 0;
}

/* the menu language's, but English online and in replays: the game's font
 * takes random numbers per letter, and each player has a menu language of
 * their own (battlex_aurora.c's tr() alike) */
static const char* dcText(const char* english) {
    return portOnlineActive() || portReplayActive() ? english : portTrGame(english);
}

/* Goombella's Tattles of the Red Buzzy family (their own are a Japanese
 * placeholder, the name and nothing else, on every disc): a page each, the
 * game's page breaks between them, so the translations hold no tags. */
typedef struct Tattle {
    const char* tag;
    const char* pages[4];
} Tattle;

static const Tattle kTattles[] = {
    {"btl_hlp_crimson_met",
     {"That's a Red Buzzy.\nIt's a Buzzy Beetle with a\nshell that's way tougher.",
      "Max HP is 5, Attack is 3, and\nDefense is 4.",
      "Fire and explosions don't hurt\nit at all, but if we flip it\nover, its Defense drops to 0.",
      "Funny... I've never read about\nthese anywhere. Where have\nthey been hiding?"}},
    {"btl_hlp_crimson_patamet",
     {"That's a Red Parabuzzy.\nIt's a Red Buzzy with wings.",
      "Max HP is 5, Attack is 3, and\nDefense is 4.",
      "Fire and explosions don't hurt\nit. Knock it out of the air,\nand it's a Red Buzzy again."}},
    {"btl_hlp_crimson_patatogemet",
     {"That's a Red Spiky Parabuzzy.\nIt's a Red Spike Top with\nwings. Ugh!",
      "Max HP is 5, Attack is 3, and\nDefense is 4.",
      "Don't jump on that spike!\nKnock it down first, then\nflip it over."}},
};
#define TATTLES ((int)(sizeof(kTattles) / sizeof(kTattles[0])))
static char sTattle[TATTLES][1024];

static const char* tattle(int i) {
    const Tattle* t = &kTattles[i];
    int n = 0;
    int p;
    for (p = 0; p < 4 && t->pages[p] != NULL; p++) {
        const char* page = dcText(t->pages[p]);
        n += snprintf(sTattle[i] + n, sizeof(sTattle[i]) - (size_t)n, "%s%s\n<k>", p > 0 ? "\n<p>\n" : "",
                      page != NULL ? page : t->pages[p]);
        if (n >= (int)sizeof(sTattle[i])) {
            break;
        }
    }
    return sTattle[i];
}

const char* portDirectorsCutMsg(const char* tag) {
    int i;
    sync();
    if (sApplied && tag != NULL && tag[0] == 'b' && strncmp(tag, "btl_hlp_crimson_", 16) == 0) {
        for (i = 0; i < TATTLES; i++) {
            if (strcmp(tag, kTattles[i].tag) == 0) {
                return tattle(i);
            }
        }
    }
    if (tag == NULL || tag[0] != 'p' || strncmp(tag, "pc_dc_", 6) != 0 || !sApplied) {
        if (sApplied && tag != NULL && strcmp(tag, "btl_wn_mario_super_charge") == 0) {
            return dcText("Super Charge");
        }
        if (sApplied && tag != NULL && strcmp(tag, "btl_hlp_cmd_operation_super_charge") == 0) {
            return dcText("Builds up more attack power than Charge.");
        }
        return NULL;
    }
    for (i = 0; i < BADGES; i++) {
        if (strcmp(tag, sKeys[i][0]) == 0) {
            return dcText(kBadges[i].name);
        }
        if (strcmp(tag, sKeys[i][1]) == 0) {
            /* broken into lines as the game's own descriptions are: the help boxes (Dazzle's
             * list, a badge got) draw a line as it is, and scroll two lines at a time */
            static char sDesc[BADGES][512];
            const char* text = dcText(kBadges[i].text);
            portPauseWrapCols(text != NULL ? text : kBadges[i].text, sDesc[i], sizeof(sDesc[i]), 30);
            return sDesc[i];
        }
        if (strcmp(tag, sKeys[i][2]) == 0) {
            return dcText(kBadges[i].menu);
        }
    }
    return NULL;
}

/* --- the sewers' cut pipe rooms --- */

static const char* const kRooms[] = {"tik_09", "tik_10", "tik_14"};
#define ROOMS ((int)(sizeof(kRooms) / sizeof(kRooms[0])))
static MapData sRoomMaps[ROOMS];

/* mapDataPtr(name), after: the rooms' MapData, always (tik.rel's _prolog
 * gives them their init scripts through it, setting or not; nothing leads
 * there with Director's Cut off) */
static void mapDataPost(MapData** result, const char* name) {
    int i;
    if (*result != NULL || name == NULL || strncmp(name, "tik_", 4) != 0) {
        return;
    }
    for (i = 0; i < ROOMS; i++) {
        if (strcmp(name, kRooms[i]) == 0) {
            sRoomMaps[i].mapName = kRooms[i];
            *result = &sRoomMaps[i];
            return;
        }
    }
}

static EvtWord* sTik06Exit;     /* dokan_3's own exit script ("You can't enter from here!") */
static const char* sTik06Bero; /* and the pipe it names */

int portEvtDirectorsCutBero(struct EventEntry* event, int isFirstCall) {
    BeroEntry* e;
    (void)isFirstCall;
    for (e = (BeroEntry*)event->lwData[0]; e != NULL && e->hitName != NULL; e++) {
        if (strcmp(e->hitName, "dokan_3") != 0) {
            continue;
        }
        if (e->nextMapName == NULL) { /* as the REL has it */
            sTik06Exit = e->exitEvent;
            sTik06Bero = e->nextBeroName;
        }
        if (portDirectorsCut()) {
            e->exitEvent = NULL;
            e->nextMapName = "tik_09";
            e->nextBeroName = "dokan_2";
        } else if (e->nextMapName != NULL) {
            e->exitEvent = sTik06Exit;
            e->nextMapName = NULL;
            e->nextBeroName = sTik06Bero;
        }
    }
    return EVT_RETURN_DONE;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portDirectorsCutInit(void) {
    static int sDone PORT_HOST_DATA; /* (the hook: not game state) */
    if (sDone) {
        return;
    }
    sDone = 1;
    portModHostAddHook(1, kSlot_mapDataPtr, (void*)mapDataPost);
}

/* --- the Journal's pages --- */

static const char* const kExtraTitles[] = {
    "Boggly Woods' other enemies",
    "Hooktail Castle's third Dull Bones",
    "The Creepy Steeple's lost Swoopers and Boos",
    "Four Fuzzies on Keelhaul Key",
    "\"Is this OK?\" at a level up",
    "The Red Buzzy family",
    "Three lost pipe rooms in the sewers",
};
static const char* const kExtraTexts[] = {
    "Boggly Woods has a second set of enemy groups in its data, Clefts with Pale Piranhas among others, chosen by "
    "a flag nothing in the game ever sets. With Director's Cut its field enemies draw from that set.",
    "The room with the floating blocks names its two Dull Bones \"2\" and \"3\", and the castle's data still has "
    "the battle a first one would fight: two Dull Bones. With Director's Cut he walks the room again.",
    "The Steeple's data has battles for two Swoopers on the stairs and two Boos in the room with the big chest, "
    "but nothing ever placed them there. With Director's Cut they haunt those rooms.",
    "The jungle's Green Fuzzies come in ones, twos and threes, and the island's data has a group of four after "
    "them that no fight ever picks. With Director's Cut a Fuzzy may bring three friends.",
    "After a level up's choice of HP, FP or BP, the game has a box asking \"Is this OK?\", its text on every disc, "
    "but it never opens. With Director's Cut it does, so a slip of the stick can be taken back.",
    "The Glitz Pit's Tiny Spinies are Red Spike Tops, and the game has three more red Buzzies that never fight: "
    "the Red Buzzy, the Red Parabuzzy and the Red Spiky Parabuzzy, their battle code left behind in a test "
    "module. With Director's Cut the Tiny Spinies' match is the whole family, and Goombella has a Tattle for each "
    "(they stay out of the Tattle Log, which never had pages for them).",
    "In the room before the Pit of 100 Trials is a pipe Mario only ever comes out of (\"You can't enter from "
    "here!\"). It was the way into three small pipe rooms, each still on the disc with its map and script but left "
    "out of the game. With Director's Cut it takes Mario down into them, two chests and all, and back out.",
};

int portDirectorsCutPages(void) {
    return BADGES + (int)(sizeof(kExtraTitles) / sizeof(kExtraTitles[0]));
}

const char* portDirectorsCutPageTitle(int i) {
    return i < BADGES ? kBadges[i].name : i < portDirectorsCutPages() ? kExtraTitles[i - BADGES] : "";
}

const char* portDirectorsCutPageText(int i) {
    return i < BADGES ? kBadges[i].text : i < portDirectorsCutPages() ? kExtraTexts[i - BADGES] : "";
}
