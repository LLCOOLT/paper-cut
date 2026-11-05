/* Director's Cut (port/include/port/directors_cut.h): the game's own unused
 * content, put back while the setting is on.
 *
 * - Seven badges whose effects the battle code still has in full but which
 *   nothing in the game gives (src/data/item_data.c lists them hidden, named
 *   "in_unknown_item"): Mega Jump, Mega Smash, Mega Quake, Super Charge,
 *   Super Charge P, Triple Dip and Lucky Start P. Dazzle sells them for Star
 *   Pieces, with names and texts of ours (their own never shipped), sorted
 *   after the other badges.
 * - Boggly Woods' second set of enemy groups (win_02: Cleft and Pale Piranha
 *   mixes, rels/win/src/data/battle/battle_database_win.c), chosen by a flag
 *   nothing sets (GSWF_WIN02_ALT_BATTLES): taken as set, never written.
 *
 * The item table's fields are changed in place as the setting turns on and
 * put back as it turns off, at the next message the game looks up. */
#include "port/directors_cut.h"
#include "port/settings.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "evt/evt_badgeshop.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include <stdio.h>
#include <string.h>

typedef struct Badge {
    s32 id;
    const char* key;   /* our message keys: pc_dc_in_<key>, pc_dc_msg_<key>, pc_dc_menu_<key> */
    const char* name;  /* English */
    const char* text;  /* its description (the shop's and the badge list's) */
    const char* menu;  /* the short one under the menus */
    u16 starPieces;
} Badge;

static const Badge kBadges[] = {
    {ITEM_MEGA_JUMP, "mega_jump", "Mega Jump",
     "Wear this to use Mega Jump, a mighty stomp. It costs 6 FP. A move cut from the original game.",
     "A mighty stomp. 6 FP.", 10},
    {ITEM_MEGA_SMASH, "mega_smash", "Mega Smash",
     "Wear this to use Mega Smash, a heavy hammer blow. A move cut from the original game.",
     "A heavy hammer blow.", 10},
    {ITEM_MEGA_QUAKE, "mega_quake", "Mega Quake",
     "Wear this to use Mega Quake, which shakes every enemy on the ground. It costs 7 FP. A move cut from the "
     "original game.",
     "Shakes every enemy on the ground. 7 FP.", 12},
    {ITEM_SUPER_CHARGE, "super_charge", "Super Charge",
     "Wear this to use Super Charge, which builds up more attack power than Charge. A move cut from the original "
     "game.",
     "Charge up more than with Charge.", 6},
    {ITEM_SUPER_CHARGE_P, "super_charge_p", "Super Charge P",
     "Wear this to let your partner use Super Charge, which builds up more attack power than Charge. A move cut "
     "from the original game.",
     "Your partner charges up more.", 6},
    {ITEM_TRIPLE_DIP, "triple_dip", "Triple Dip",
     "Wear this to use three items in one turn. It costs 8 FP. Cut from the original game, where only two Double "
     "Dips gave it.",
     "Use three items in one turn. 8 FP.", 15},
    {ITEM_LUCKY_START_P, "lucky_start_p", "Lucky Start P",
     "Wear this to give your partner a lucky bonus at the start of each battle. Cut from the original game.",
     "A lucky bonus for your partner at the start.", 8},
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
    }
}

int portDirectorsCutFlag(int gswf) {
    return gswf == GSWF_WIN02_ALT_BATTLES && portDirectorsCut();
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

const char* portDirectorsCutMsg(const char* tag) {
    int i;
    sync();
    if (tag == NULL || tag[0] != 'p' || strncmp(tag, "pc_dc_", 6) != 0 || !sApplied) {
        if (sApplied && tag != NULL && strcmp(tag, "btl_wn_mario_super_charge") == 0) {
            return portTrGame("Super Charge");
        }
        if (sApplied && tag != NULL && strcmp(tag, "btl_hlp_cmd_operation_super_charge") == 0) {
            return portTrGame("Builds up more attack power than Charge.");
        }
        return NULL;
    }
    for (i = 0; i < BADGES; i++) {
        if (strcmp(tag, sKeys[i][0]) == 0) {
            return portTrGame(kBadges[i].name);
        }
        if (strcmp(tag, sKeys[i][1]) == 0) {
            return portTrGame(kBadges[i].text);
        }
        if (strcmp(tag, sKeys[i][2]) == 0) {
            return portTrGame(kBadges[i].menu);
        }
    }
    return NULL;
}

/* --- the Journal's pages --- */

static const char* const kExtraTitles[] = {"Boggly Woods' other enemies"};
static const char* const kExtraTexts[] = {
    "Boggly Woods has a second set of enemy groups in its data, Clefts with Pale Piranhas among others, chosen by "
    "a flag nothing in the game ever sets. With Director's Cut its field enemies draw from that set."};

int portDirectorsCutPages(void) {
    return BADGES + (int)(sizeof(kExtraTitles) / sizeof(kExtraTitles[0]));
}

const char* portDirectorsCutPageTitle(int i) {
    return i < BADGES ? kBadges[i].name : i < portDirectorsCutPages() ? kExtraTitles[i - BADGES] : "";
}

const char* portDirectorsCutPageText(int i) {
    return i < BADGES ? kBadges[i].text : i < portDirectorsCutPages() ? kExtraTexts[i - BADGES] : "";
}
