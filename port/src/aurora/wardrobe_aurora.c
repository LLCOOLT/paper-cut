/* The pause menu's Wardrobe tab (port/include/port/pause_settings.h): the
 * cosmetics (port/settings/features/cosmetics.cpp) in the game's own menu, as
 * a game's character-customization screen. The one being dressed stands in
 * the middle of the page's left side, the game's own model in its idle
 * animation, turning slowly; the rows beside him: first whoever Play as picked
 * (docs/PLAY_AS.md), under their name and as they stand in on the Mario page,
 * with their own rows: Mario's looks (the game's own, Luigi, Wario, Waluigi,
 * Fire Mario), his cap and shirt, his overalls; Luigi's the same in his
 * colours; Peach's dress, crown, hair, gloves, earrings and shoes; Bowser's
 * shell, hair, skin, horns and spikes, and bracelets; each pick's colours its
 * own, kept apart, and the costume, fitted to whoever wears it. Then a
 * row for each part of each partner, in the order they join, under their
 * names, the partner of the selected row taking his place, met yet or not;
 * the status bar's and menus' theme, its banners (the bar's own icons take
 * it), the menus' colour (the page itself shows it) and "Colour picker...",
 * which opens the settings page's Cosmetics tab for the custom colours. The
 * rows scroll when they don't all fit. Left and right (A: the next) step
 * through a row's colours: the game's own, the 13 named ones, then the slot's
 * custom one. A colour shows at once: the models' textures are recoloured as
 * they're drawn, so what's on the page is what the field and battles will
 * show.
 *
 * Yoshi's row is the colour he hatched in itself (the game's, in the pouch:
 * pouchGetPartyColor, set by tou_10's setYoshiColor), stepped through the
 * game's seven, his model in the field taking it at once. That is the game's
 * state, which a replay's recording wouldn't have, so it can't change while
 * a replay runs (portReplayActive); the colour itself travels with the save, as every pouch
 * value does.
 *
 * Only how things look otherwise: cosmetics never change what the game reads
 * (the cosmetics feature's own note), so a game played together plays out the
 * same. The models, every partner's, are taken when the menu opens, as the
 * Party page's are, in every game of a session alike.
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the row
 * (before Settings), its label (pause_settings_aurora.c paints it from the
 * Journal's), its page (menu id 6, active 17) and its help text, which
 * msgSearch takes from portPauseWardrobeMsg. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/button_markup.h"
#include "port/text_fit.h"
#include "port/companion_menu.h"
#include "port/cosmetics.h"
#include "port/costume.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/luigi.h"
#include "port/menu_tap.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/replay.h"
#include "port/speech.h"
#include "port/tr.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/icondrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "pmario_sound.h"
#include "win/win_root.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


enum {
    KIND_HEADING, KIND_SUBHEADING, KIND_LOOKS, KIND_SLOT, KIND_YOSHI, KIND_PICKER, KIND_THEME,
    KIND_COSTUME, KIND_SKIN, KIND_STICKER_KIND, KIND_STICKER, KIND_STICK, KIND_PEEL,
    KIND_PM64 /* Mario's Paper Mario 64 look (port/include/port/pm64_media.h), with a ROM linked */
};
enum { SHOW_MARIO, SHOW_HUD, SHOW_PARTNER, SHOW_STICKER };

typedef struct Row {
    int kind;
    int slot; /* KIND_SLOT's (PORT_COSMETIC_*) */
    const char* label;
    const char* note;
    int show;  /* SHOW_*: who stands on the page while it is selected */
    int party; /* SHOW_PARTNER's: the partner (PartyId: 1 Goombella .. 7 Ms. Mowz) */
    int indent; /* a part under its partner's name */
} Row;

/* the Play as pick's own rows (the heading, its looks, then each of its
 * slots, portCosmeticsSlotPick, by their short labels), then these */
static const struct {
    int slot;
    const char* label;
} kPickParts[] = {
    {PORT_COSMETIC_CAP, "Cap and shirt"},
    {PORT_COSMETIC_OVERALLS, "Overalls"},
    {PORT_COSMETIC_LUIGI_CAP, "Cap and shirt"},
    {PORT_COSMETIC_LUIGI_OVERALLS, "Overalls"},
    {PORT_COSMETIC_PEACH_DRESS, "Dress"},
    {PORT_COSMETIC_PEACH_CROWN, "Crown"},
    {PORT_COSMETIC_PEACH_HAIR, "Hair"},
    {PORT_COSMETIC_PEACH_GLOVES, "Gloves"},
    {PORT_COSMETIC_PEACH_EARRINGS, "Earrings"},
    {PORT_COSMETIC_PEACH_SHOES, "Shoes"},
    {PORT_COSMETIC_BOWSER_SHELL, "Shell"},
    {PORT_COSMETIC_BOWSER_HAIR, "Hair"},
    {PORT_COSMETIC_BOWSER_SKIN, "Skin"},
    {PORT_COSMETIC_BOWSER_SPIKES, "Horns and spikes"},
    {PORT_COSMETIC_BOWSER_BRACELETS, "Bracelets"},
};
static const char* const kPickNames[PORT_PLAY_AS_COUNT] = {"Mario", "Luigi", "Peach", "Bowser"};
static const char* const kLooksNotes[PORT_PLAY_AS_COUNT] = {
    "Mario as his brother or his rivals, in one press.",
    "Luigi as his brother or his rivals, in one press.",
    NULL,
    NULL,
};

static const Row kRest[] = {
    {KIND_HEADING, 0, "Partners", NULL, SHOW_PARTNER, 1, 0},
    {KIND_SUBHEADING, 0, "Goombella", NULL, SHOW_PARTNER, 1, 0},
    {KIND_SLOT, PORT_COSMETIC_PARTNER, "Hair", NULL, SHOW_PARTNER, 1, 1},
    {KIND_SLOT, PORT_COSMETIC_GOOMBELLA_HELMET, "Helmet", NULL, SHOW_PARTNER, 1, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 1, 1},
    {KIND_SUBHEADING, 0, "Koops", NULL, SHOW_PARTNER, 2, 0},
    {KIND_SLOT, PORT_COSMETIC_KOOPS_SHELL, "Shell", NULL, SHOW_PARTNER, 2, 1},
    {KIND_SLOT, PORT_COSMETIC_KOOPS_HOODIE, "Hoodie", NULL, SHOW_PARTNER, 2, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 2, 1},
    {KIND_SUBHEADING, 0, "Flurrie", NULL, SHOW_PARTNER, 5, 0},
    {KIND_SLOT, PORT_COSMETIC_FLURRIE_HAIR, "Hair", NULL, SHOW_PARTNER, 5, 1},
    {KIND_SLOT, PORT_COSMETIC_FLURRIE_DRESS, "Dress", NULL, SHOW_PARTNER, 5, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 5, 1},
    {KIND_YOSHI, 0, "Yoshi's colour", NULL, SHOW_PARTNER, 4, 0},
    {KIND_SKIN, 0, "Yoshi's skin", NULL, SHOW_PARTNER, 4, 0},
    {KIND_SUBHEADING, 0, "Vivian", NULL, SHOW_PARTNER, 6, 0},
    {KIND_SLOT, PORT_COSMETIC_VIVIAN, "Body", NULL, SHOW_PARTNER, 6, 1},
    {KIND_SLOT, PORT_COSMETIC_VIVIAN_HAT, "Hat", NULL, SHOW_PARTNER, 6, 1},
    {KIND_SLOT, PORT_COSMETIC_VIVIAN_HAIR, "Hair", NULL, SHOW_PARTNER, 6, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 6, 1},
    {KIND_SUBHEADING, 0, "Bobbery", NULL, SHOW_PARTNER, 3, 0},
    {KIND_SLOT, PORT_COSMETIC_BOBBERY_BODY, "Body", NULL, SHOW_PARTNER, 3, 1},
    {KIND_SLOT, PORT_COSMETIC_BOBBERY_FUSE, "Fuse", NULL, SHOW_PARTNER, 3, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 3, 1},
    {KIND_SUBHEADING, 0, "Ms. Mowz", NULL, SHOW_PARTNER, 7, 0},
    {KIND_SLOT, PORT_COSMETIC_MOWZ_MASK, "Mask", NULL, SHOW_PARTNER, 7, 1},
    {KIND_SLOT, PORT_COSMETIC_MOWZ_CAPE, "Cape", NULL, SHOW_PARTNER, 7, 1},
    {KIND_SKIN, 0, "Skin", NULL, SHOW_PARTNER, 7, 1},
    {KIND_HEADING, 0, "Stickers", NULL, SHOW_STICKER, 0, 0},
    {KIND_STICKER_KIND, 0, "Kind", "Partners' faces, items and enemies, unlocked as this file meets them.", SHOW_STICKER,
     0, 0},
    {KIND_STICKER, 0, "Sticker", "Left and right: the ones of this kind unlocked so far.", SHOW_STICKER, 0, 0},
    {KIND_STICK, 0, "Stick it here", "Sticks it where Mario stands, facing the camera. Kept with this save file.",
     SHOW_STICKER, 0, 0},
    {KIND_PEEL, 0, "Peel the nearest", "Takes the sticker nearest Mario off.", SHOW_STICKER, 0, 0},
    {KIND_HEADING, 0, "Status bar", NULL, SHOW_HUD, 0, 0},
    {KIND_THEME, 0, "Theme", "The banners and the menus in colours that go together. A part with its own keeps it.",
     SHOW_HUD, 0, 0},
    {KIND_SLOT, PORT_COSMETIC_HUD_HP, "HP banner", NULL, SHOW_HUD, 0, 0},
    {KIND_SLOT, PORT_COSMETIC_HUD_FP, "FP banner", NULL, SHOW_HUD, 0, 0},
    {KIND_SLOT, PORT_COSMETIC_HUD_SP, "Star Power banner", NULL, SHOW_HUD, 0, 0},
    {KIND_SLOT, PORT_COSMETIC_HUD_MENU, "Menus", NULL, SHOW_HUD, 0, 0},
    {KIND_PICKER, 0, "Colour picker...", "Each part's own colour: a swatch, or red, green and blue.", SHOW_MARIO, 0, 0},
};
#define REST ((s32)(sizeof(kRest) / sizeof(kRest[0])))
#define MAX_ROWS (REST + 5 + (s32)(sizeof(kPickParts) / sizeof(kPickParts[0])))

/* the rows now: the pick's, then the rest (made again when the pick changes) */
static Row kRows[MAX_ROWS];
static s32 sRows;
static s32 sRowsPick = -1;
#define ROWS sRows
#define SHOWN 10 /* the rows the page has room for; the others scroll into view */

/* the page: the stage at the left (its middle STAGE_X), the rows at the right */
#define STAGE_X (-165.0f)
#define FEET_Y (-95.0f)
#define ROW_X (-30.0f)
#define VALUE_X 258.0f
#define ROW_TOP 132.0f
#define ROW_STEP 24.0f
#define HELP_COLS 30

static s32 sRow = 1;
static s32 sTop = 0; /* the first row shown */
static char sHelp[512];
/* why the last Stick or Peel couldn't happen (English, portStickerWhyNot), said in the
 * help box in place of the row's note until another row is selected */
static const char* sRefusal;
static s32 sRefusalRow = -1;
static s32 sPartner[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; /* each partner's pose, by PartyId (-1: none) */
static s32 sYoshiColour = -1;                               /* the colour sPartner[4] was taken in */

/* each partner's field model, its idle animation and its size on the stage
 * (they are of all sizes: Flurrie fills it, Yoshi is small) */
static const struct {
    const char* model; /* NULL: Yoshi's, by his colour (yoshigroup) */
    const char* idle;
    f32 scale;
} kPartnerModel[8] = {
    {NULL, NULL, 0.0f},
    {"c_pkr", "PKR_S_1", 2.2f},
    {"c_pnk", "PNK_S_1", 2.1f},
    {"c_bomt_n", "S_1", 1.9f},
    {NULL, "PYS_S_1", 2.3f},
    {"c_windy", "PWD_S_1", 1.6f},
    {"c_vivian", "PTR_S_1", 1.9f},
    {"c_tyutyu", "PCH_S_1", 2.2f},
};

/* Yoshi's seven, by the colour number the game gives him (pouchGetPartyColor) */
static const char* const kYoshiColours[7] = {"Green", "Red", "Blue", "Orange", "Pink", "Black", "White"};
static const unsigned kYoshiRgb[7] = {0x3CB43C, 0xE63228, 0x3C64E6, 0xF58C1E, 0xF58CC8, 0x3C3C46, 0xF0F0F0};
static const char* const kYoshiNote = "The colour he hatched in. This changes the game itself.";
static const char* const kYoshiLocked = "Not during online play or a replay.";
static const char* const kStepNote = "Left and right: the game's own, 13 colours, or Custom.";
/* a costume chosen that the pick can't wear (costume_pc.c's fits): not shown on them */
static const char* const kCostumeNoFit[PORT_PLAY_AS_COUNT] = {
    "This outfit doesn't fit Mario, so he doesn't wear it.",
    "This outfit doesn't fit Luigi, so he doesn't wear it.",
    "This outfit doesn't fit Peach, so she doesn't wear it. Left and right: the ones that do.",
    "This outfit doesn't fit Bowser, so he doesn't wear it. Left and right: the ones that do.",
};

static void makeRows(void) {
    const int pick = portPlayAs();
    size_t i;
    s32 n = 0;
    if (pick == sRowsPick && sRows > 0) {
        return;
    }
    kRows[n++] = (Row){KIND_HEADING, 0, kPickNames[pick], NULL, SHOW_MARIO, 0, 0};
    if (portCosmeticsLookCount() > 0) {
        kRows[n++] = (Row){KIND_LOOKS, 0, "Looks", kLooksNotes[pick], SHOW_MARIO, 0, 0};
    }
    for (i = 0; i < sizeof(kPickParts) / sizeof(kPickParts[0]); i++) {
        if (portCosmeticsSlotPick(kPickParts[i].slot) == pick) {
            kRows[n++] = (Row){KIND_SLOT, kPickParts[i].slot, kPickParts[i].label, NULL, SHOW_MARIO, 0, 0};
        }
    }
    kRows[n++] = (Row){KIND_COSTUME, 0, "Costume", NULL, SHOW_MARIO, 0, 0};
    if (pick == PORT_PLAY_AS_MARIO && portPm64Linked() != PM64_NONE) {
        kRows[n++] = (Row){KIND_PM64, 0, "Paper Mario 64 look",
                           "Mario as his Paper Mario sprite, from your linked ROM: in the field and in battle.",
                           SHOW_MARIO, 0, 0};
    }
    for (i = 0; i < (size_t)REST; i++) {
        kRows[n++] = kRest[i];
    }
    sRows = n;
    if (sRowsPick >= 0) { /* (the pick changed while the menu was open: back to the top) */
        sRow = 1;
        sTop = 0;
    }
    sRowsPick = pick;
}

static int yoshiLocked(void) {
    return portReplayActive();
}

static s32 yoshiColour(void) {
    return pouchGetPartyColor(PARTNER_YOSHI) % 7;
}

static int heading(const Row* r) {
    return r->kind == KIND_HEADING || r->kind == KIND_SUBHEADING;
}

/* the row's note: its own, or its slot's, or how the colours go */
static const char* rowNote(const Row* r) {
    if (r->kind == KIND_YOSHI) {
        return yoshiLocked() ? kYoshiLocked : kYoshiNote;
    }
    if (r->kind == KIND_COSTUME) {
        const int c = portCostumeCurrent();
        if (c > 0 && !portCostumeFits(c - 1)) {
            return kCostumeNoFit[portPlayAs()];
        }
        return "An enemy's outfit, cut from its own paper: one more each time an enemy joins the Tattle Log.";
    }
    if (r->kind == KIND_SKIN) {
        return portSkinCount() == 0 ? "Put a character pack in the skins folder to dress them as someone else."
                                    : "Someone else in their place: the character packs in the skins folder.";
    }
    if (r->note != NULL) {
        return r->note;
    }
    if (r->kind == KIND_SLOT && portCosmeticsSlotNote(r->slot) != NULL) {
        return portCosmeticsSlotNote(r->slot);
    }
    return kStepNote;
}

/* the row's value in the game's encoding ("" for none) */
static const char* rowValue(const Row* r) {
    if (r->kind == KIND_LOOKS) {
        const int look = portCosmeticsLook();
        return portTrGame(look >= 0 ? portCosmeticsLookName(look) : "Your own");
    }
    if (r->kind == KIND_THEME) {
        return portTrGame(portCosmeticsThemeName(portCosmeticsTheme()));
    }
    if (r->kind == KIND_SLOT) {
        /* (a part of the status bar or menus with no colour of its own: the theme's) */
        return portTrGame(portCosmeticsSlotThemed(r->slot) ? "Theme"
                                                           : portCosmeticsColourName(portCosmeticsChoice(r->slot)));
    }
    if (r->kind == KIND_YOSHI) {
        return portTrGame(kYoshiColours[yoshiColour()]);
    }
    if (r->kind == KIND_COSTUME) {
        const int c = portCostumeCurrent();
        return portTrGame(c > 0 ? portCostumeName(c - 1) : "None");
    }
    if (r->kind == KIND_PM64) {
        return portTrGame(portPm64MarioLookOn() ? "On" : "Off");
    }
    if (r->kind == KIND_SKIN) {
        const int k = portSkinOf(r->party);
        return k >= 0 ? portSkinName(k) : portTrGame("Their own");
    }
    if (r->kind == KIND_STICKER_KIND) {
        static const char* const kKinds[PORT_STICKER_KINDS] = {"Faces", "Items", "Enemies"};
        static char sKindText[64];
        const int k = portStickerKindChosen();
        snprintf(sKindText, sizeof(sKindText), "%s (%d)", portTrGame(kKinds[k]), portStickerUnlockedCount(k));
        return sKindText;
    }
    if (r->kind == KIND_STICKER) {
        const int c = portStickerChosen();
        return c >= 0 ? portStickerName(c) : portTrGame("None yet");
    }
    if (r->kind == KIND_STICK) {
        static char sHereText[48];
        snprintf(sHereText, sizeof(sHereText), portTrGame("%d here"), portStickersHere());
        return sHereText;
    }
    return "";
}

/* Yoshi's model on the stage, in the colour he has now */
static void takeYoshi(void) {
    const s32 c = yoshiColour();
    s32 pose;
    if (sPartner[4] >= 0 && c == sYoshiColour) {
        return;
    }
    pose = animPoseEntry(yoshigroup[c], 0);
    if (pose < 0) {
        return; /* (the pose pool full: the old colour stays on the stage, tried again next frame) */
    }
    if (sPartner[4] >= 0) {
        animPoseRelease(sPartner[4]);
    }
    sPartner[4] = pose;
    animPoseSetAnim(sPartner[4], kPartnerModel[4].idle, 1);
    animPoseSetMaterialFlagOn(sPartner[4], ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2);
    sYoshiColour = c;
}

/* Yoshi's colour stepped (pouchSetPartyColor), and Yoshi in the field, if
 * he's out, in it: his model taken again as partyPoseEntry takes it, lit as
 * the old one was (his 8-bit self keeps d_mario's) */
static void stepYoshi(int dir) {
    const s32 c = ((yoshiColour() + dir) % 7 + 7) % 7;
    s32 i;
    pouchSetPartyColor(PARTNER_YOSHI, c);
    for (i = 0; i < 2; i++) {
        PartyEntry* p = partyGetPtr(i);
        s32 old, pose;
        if (p == NULL || p->currentMemberId != PARTY_MEMBER_YOSHI || (p->flags2 & PARTY_DISP_FLAG_FAMICOM) ||
            p->paperAnimGroup < 0) {
            continue;
        }
        old = p->paperAnimGroup;
        if ((pose = animPoseEntry(yoshigroup[c], 2)) < 0) {
            continue; /* (the pose pool full: the field Yoshi keeps his old model, never one of -1) */
        }
        p->paperAnimGroup = pose;
        animPoseSetMaterialFlagOn(p->paperAnimGroup, animPoseGetMaterialFlag(old));
        animPoseSetMaterialLightFlagOn(p->paperAnimGroup, animPoseGetMaterialLightFlag(old));
        animPoseRelease(old);
        p->trigFlags |= PARTY_TRIG_FLAG_CHG_ANIM; /* (partyPreDisp sets his pose on the new one) */
    }
    takeYoshi();
}

static int change(const Row* r, int dir) {
    if (r->kind == KIND_LOOKS) {
        const int n = portCosmeticsLookCount();
        const int look = portCosmeticsLook();
        /* from his own colours, the first look one way or the last the other */
        portCosmeticsSetLook(look < 0 ? (dir > 0 ? 0 : n - 1) : (look + dir + n) % n);
        return 1;
    }
    if (r->kind == KIND_SLOT) {
        portCosmeticsStep(r->slot, dir);
        return 1;
    }
    if (r->kind == KIND_THEME) {
        portCosmeticsStepTheme(dir);
        return 1;
    }
    if (r->kind == KIND_YOSHI && !yoshiLocked()) {
        stepYoshi(dir);
        return 1;
    }
    if (r->kind == KIND_COSTUME) {
        portCostumeStep(dir);
        return 1;
    }
    if (r->kind == KIND_PM64) {
        portPm64MarioLookSet(!portPm64MarioLookOn());
        return 1;
    }
    if (r->kind == KIND_SKIN) {
        portSkinStep(r->party, dir);
        return portSkinCount() > 0;
    }
    if (r->kind == KIND_STICKER_KIND) {
        portStickerStepKind(dir);
        return 1;
    }
    if (r->kind == KIND_STICKER) {
        portStickerStep(dir);
        return 1;
    }
    return 0;
}

/* the rows shown: the selected one in view, with its heading if it has one */
static void scroll(void) {
    s32 first = sRow;
    while (first > 0 && heading(&kRows[first - 1]) && sRow - first < 2) {
        first--;
    }
    if (first < sTop) {
        sTop = first;
    }
    if (sRow >= sTop + SHOWN) {
        sTop = sRow - SHOWN + 1;
    }
    if (sTop > ROWS - SHOWN) {
        sTop = ROWS - SHOWN;
    }
    if (sTop < 0) {
        sTop = 0;
    }
}

/* row i's place on the page, among the rows shown */
static f32 rowY(s32 i) {
    return ROW_TOP - (i - sTop) * ROW_STEP;
}

static void pointAt(WindowWork* wp) {
    static char tags[MAX_ROWS][24];
    char text[512];
    if (tags[sRow][0] == '\0') {
        snprintf(tags[sRow], sizeof(tags[sRow]), "pc_menu_wardrobe_%d", (int)sRow);
    }
    scroll();
    wp->helpTgtX = ROW_X - 30.0f;
    wp->helpTgtY = rowY(sRow) - 8.0f;
    if (sRefusalRow != sRow) {
        sRefusal = NULL;
        sRefusalRow = -1;
    }
    snprintf(text, sizeof(text), "%s", portTrGame(sRefusal != NULL ? sRefusal : rowNote(&kRows[sRow])));
    portPauseWrap(text, sHelp, sizeof(sHelp));
    winMsgEntry(wp, 0, tags[sRow], 0);
}

static void move(int dir) {
    do {
        sRow = (sRow + dir + ROWS) % ROWS;
    } while (heading(&kRows[sRow]));
}

/* every partner's field model, met yet or not, as the Party page takes its
 * own (the one the colours recolour; the Party page's is another), Yoshi's in
 * the colour he has */
void portPauseWardrobeInit(WindowWork* wp) {
    s32 i;
    (void)wp;
    for (i = 1; i < 8; i++) {
        if (kPartnerModel[i].model == NULL) {
            sPartner[i] = -1;
            takeYoshi();
            continue;
        }
        sPartner[i] = animPoseEntry(kPartnerModel[i].model, 0);
        if (sPartner[i] < 0) {
            continue; /* (the pose pool full: no model on the stage for this one, stand() draws none) */
        }
        animPoseSetAnim(sPartner[i], kPartnerModel[i].idle, 1);
        animPoseSetMaterialFlagOn(sPartner[i], ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2);
    }
}

void portPauseWardrobeExit(WindowWork* wp) {
    s32 i;
    (void)wp;
    for (i = 1; i < 8; i++) {
        if (sPartner[i] >= 0) {
            animPoseRelease(sPartner[i]);
        }
        sPartner[i] = -1;
    }
    sYoshiColour = -1;
}

void portPauseWardrobeInit2(WindowWork* wp) {
    wp->flags |= WIN_FLAG_SUBMENU;
    sRefusal = NULL;
    sRefusalRow = -1;
    makeRows();
    if (sRow <= 0 || sRow >= ROWS || heading(&kRows[sRow])) {
        sRow = 1;
    }
    pointAt(wp);
}

/* the mouse and touch: the row under the pointer is selected; a click on the
 * selected one is A */
static int pointer(WindowWork* wp) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    s32 i;
    (void)wp;
    if (kind == 0 || gx < ROW_X - 20.0f || gx > VALUE_X + 10.0f) {
        return 0;
    }
    for (i = sTop; i < ROWS && i < sTop + SHOWN; i++) {
        const f32 y = rowY(i) - 8.0f; /* (the row's middle: the text hangs from its y) */
        if (!heading(&kRows[i]) && gy <= y + ROW_STEP / 2 && gy > y - ROW_STEP / 2) {
            if (i == sRow) {
                return kind == 1;
            }
            sRow = i;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

/* the colour picker open (port/src/aurora/pause_picker_aurora.c): the stage
 * shows its part, as if that part's row were selected */
static void followPicker(void) {
    s32 i;
    for (i = 0; i < ROWS; i++) {
        if (kRows[i].kind == KIND_SLOT && kRows[i].slot == portPausePickerSlot()) {
            sRow = i;
            return;
        }
    }
}

s32 portPauseWardrobeMain(WindowWork* wp) {
    const Row* r;
    int a;
    makeRows();
    if (kRows[sRow].show == SHOW_STICKER && !gPortCompanionMenu) {
        /* the stage's sticker: its pose wanted from the menu's frame (lockstep online), not its draw */
        portStickerPoseWant(portStickerChosen());
    }
    if (portPausePage() != NULL) {
        portPausePageMain(wp);
        followPicker();
        if (portPausePage() == NULL) {
            pointAt(wp);
        }
        return 0;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    a = pointer(wp) || (wp->buttonTrg & PAD_BUTTON_A);
    r = &kRows[sRow];
    if (a) {
        if (r->kind == KIND_PICKER) {
            psndSFXOn((const char*)0x20012);
            /* (in the game's windows, on the pick's first part) */
            portPauseOpenPage(&gPortPausePicker, kRows[1].kind == KIND_SLOT ? kRows[1].slot : kRows[2].slot);
            followPicker();
        } else if (r->kind == KIND_STICK || r->kind == KIND_PEEL) {
            /* (A only: left and right never stick one) */
            const int peel = r->kind == KIND_PEEL;
            const char* why = portStickerWhyNot(peel);
            const int done = why == NULL && (peel ? portStickerPeel() : portStickerStick());
            psndSFXOn((const char*)(done ? 0x20012 : 0x20013));
            sRefusal = done ? NULL : (why != NULL ? why : "Can't do that now.");
            sRefusalRow = done ? -1 : sRow;
        } else if (change(r, 1)) {
            psndSFXOn((const char*)0x20005);
        }
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        if (change(r, (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1)) {
            psndSFXOn((const char*)0x20005);
        }
    }
    pointAt(wp);
    { /* read aloud: the row, its value and note, when either changes */
        static char sSaid[600];
        char text[600];
        const char* value;
        r = &kRows[sRow];
        value = rowValue(r);
        snprintf(text, sizeof(text), "%s%s%s. %s", portTrGame(r->label), value[0] != '\0' ? ", " : "", value,
                 portTrGame(rowNote(r)));
        if (strcmp(text, sSaid) != 0) {
            snprintf(sSaid, sizeof(sSaid), "%s", text);
            portSpeechMenu(text);
        }
    }
    return 0;
}

/* --- drawing */

/* text at (x, y) no wider than w: scaled down from s to fit */
static f32 fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    portTextFit(right ? "wardrobe-value" : "wardrobe-label", text, full * s, w, 1);
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", text);
    portButtonsGameTextScale(textWas);
    return full * k;
}

/* a filled box of colour c, its top-left at (x, y) */
static void box(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXConcat(camGetPtr(CAMERA_2D)->view, m, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, -h, 0.0f);
    GXPosition3f32(w, -h, 0.0f);
    GXPosition3f32(w, 0.0f, 0.0f);
}

/* a colour's swatch: the colour in a dark frame, as the game's icons are outlined */
static void swatch(f32 x, f32 y, unsigned rgb, u8 alpha) {
    box(x, y, 18.0f, 16.0f, (GXColor){0x30, 0x28, 0x20, alpha});
    box(x + 2.0f, y - 2.0f, 14.0f, 12.0f, (GXColor){(u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, alpha});
}

/* a small triangle pointing up (dir 1) or down (-1): more rows that way */
static void more(f32 x, f32 y, int dir, GXColor c) {
    s32 i;
    for (i = 0; i < 5; i++) {
        const f32 w = 2.0f + i * 2.0f;
        box(x - w / 2, dir > 0 ? y - i * 1.6f : y + i * 1.6f - 6.4f, w, 1.6f, c);
    }
}

/* the slow turn of the one on the stage: to and fro, never edge-on (they're paper) */
static f32 turn(void) {
    const f32 t = (f32)(gp->retraceCount % 600) / 600.0f;
    return sinf(t * 6.2831853f) * 0.6f; /* radians, about 35 degrees either way */
}

/* a pose stood at (x, y), s times its size, turned by the stage's turn; in
 * its animation's own time, as the Party and Mario pages keep theirs */
static void stand(WindowWork* wp, s32 pose, f32 x, f32 y, f32 s, int mario, int vivian) {
    AnimData* anim;
    Mtx m, sc, ry;
    if (pose < 0) {
        return;
    }
    anim = animPoseGetAnimDataPtr(pose);
    if (anim != NULL && anim->loopInfo[2] > 0) {
        animPoseSetLocalTime(pose, (f32)(gp->retraceCount % (s32)anim->loopInfo[2]));
    }
    animPoseMain(pose);
    if (mario) {
        portLuigiUpdate(PORT_LUIGI_MENU, pose, TRUE); /* Play as Luigi: his stand-in, as on the Mario page */
    }
    if (vivian) {
        Vec vp = {x, y, 0.0f};
        animPoseVivianMain(pose, &vp);
    }
    winKageGX(wp, (GXColor){0x00, 0x00, 0x00, 0x80}, x, y, 0.0f, s);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXScale(sc, s, s, s);
    PSMTXRotRad(ry, 'y', turn());
    PSMTXConcat(m, sc, m);
    PSMTXConcat(m, ry, m);
    winZClear();
    /* (Luigi standing in, his costume, a partner's skin) */
    portLuigiDrawMtx(pose, m, 2, 0.0f, 5.0f);
}

/* the status bar's own icons, as it draws them: Mario's face and the HP
 * banner, the partner's and theirs, the flower and the FP banner, the star
 * and the Star Power banner */
static void statusBar(f32 x, f32 y) {
    /* (each banner, then what sits on its left end, 70 apart as on the bar) */
    static const struct {
        f32 dx, dy;
        int icon;
    } kIcons[] = {
        {0.0f, 0.0f, 1}, {-70.0f, 0.0f, 0}, {0.0f, -52.0f, 1}, {-70.0f, -52.0f, 2},
        {0.0f, -104.0f, 4}, {-70.0f, -104.0f, 3}, {0.0f, -156.0f, 6}, {-70.0f, -156.0f, 5},
    };
    const u16 ids[] = {ICON_1A3, ICON_1D0, ICON_1A4, ICON_1B1, ICON_1D1, ICON_1B2, ICON_1D2};
    const f32 k = 0.85f;
    s32 i;
    for (i = 0; i < (s32)(sizeof(kIcons) / sizeof(kIcons[0])); i++) {
        Vec v = {x + kIcons[i].dx * k, y + kIcons[i].dy, 0.0f};
        iconDispGx(&v, 0x10, ids[kIcons[i].icon], k);
    }
}

void portPauseWardrobeDisp(s32 camId, WindowWork* wp, s32 idx) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor teal = {0x1E, 0x6A, 0x62, 0xFF};
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    const int inside = wp->active == PORT_PAUSE_WARDROBE_TAB + 11;
    int show;
    s32 i;
    (void)camId;
    makeRows();
    show = kRows[sRow].show;

    winBgGX(wp, PORT_PAUSE_WARDROBE_TAB, x, y);
    /* the stage: who the selected row dresses */
    if (show == SHOW_HUD) {
        statusBar(x + STAGE_X + 25.0f, y + 85.0f);
    } else if (show == SHOW_STICKER) {
        const int c = portStickerChosen();
        const int icon = portStickerIcon(c);
        if (icon >= 0) {
            Vec v = {x + STAGE_X, y + FEET_Y + 60.0f, 0.0f};
            iconDispGx(&v, 0x10, (u16)icon, 3.0f);
        } else {
            stand(wp, portStickerPose(c), x + STAGE_X, y + FEET_Y, 2.0f, 0, 0);
        }
    } else if (show == SHOW_PARTNER) {
        const s32 p = kRows[sRow].party;
        if (p == 4) {
            takeYoshi(); /* (his colour changed: by the row, or by the game) */
        }
        stand(wp, sPartner[p], x + STAGE_X, y + FEET_Y, kPartnerModel[p].scale, 0, p == 6);
    } else {
        /* (Peach, a head taller, and Bowser, twice as wide, a little smaller:
         * her crown clear of the tabs, his snout on the page) */
        static const f32 kStage[PORT_PLAY_AS_COUNT] = {2.2f, 2.2f, 1.8f, 1.6f};
        stand(wp, wp->mario.poseId, x + STAGE_X, y + FEET_Y, kStage[portPlayAs()], 1, 0);
    }

    if (portPausePage() != NULL) { /* the colour picker beside the stage */
        portPausePageDisp(wp, x, y);
        return;
    }
    /* the rows that fit, scrolled to the selected one */
    winKirinukiGX(wp, 0, x + ROW_X - 28.0f, y + ROW_TOP + 12.0f, VALUE_X - ROW_X + 40.0f, SHOWN * ROW_STEP + 10.0f);
    for (i = sTop; i < ROWS && i < sTop + SHOWN; i++) {
        const Row* r = &kRows[i];
        const f32 ry = y + rowY(i);
        const GXColor c = !inside || i == sRow ? black : grey;
        const char* value;
        winFontInit();
        if (r->kind == KIND_HEADING) {
            /* (Mario's rows: the pick's, by name; Play as, port/include/port/luigi.h) */
            fitText(x + ROW_X - 18.0f, ry, 280.0f, 0.75f, teal,
                    portTrGame(strcmp(r->label, "Mario") == 0 ? portPlayAsName() : r->label), 0);
            continue;
        }
        if (r->kind == KIND_SUBHEADING) { /* a partner's name, over the rows of his parts */
            fitText(x + ROW_X, ry, 140.0f, 0.75f, teal, portTrGame(r->label), 0);
            continue;
        }
        fitText(x + ROW_X + (r->indent ? 14.0f : 0.0f), ry, r->indent ? 126.0f : 140.0f, 0.8f, c,
                portTrGame(r->label), 0);
        value = rowValue(r);
        if (value[0] != '\0') {
            const f32 w = fitText(x + VALUE_X, ry, 104.0f, 0.8f, c, value, 1);
            if (r->kind == KIND_YOSHI) {
                swatch(x + VALUE_X - w - 24.0f, ry - 2.0f, kYoshiRgb[yoshiColour()],
                       c.r == 0 && !yoshiLocked() ? 0xFF : 0xA0);
            } else if (r->kind == KIND_SLOT) {
                swatch(x + VALUE_X - w - 24.0f, ry - 2.0f, portCosmeticsSlotRgb(r->slot), c.r == 0 ? 0xFF : 0xA0);
            }
        }
    }
    /* more above or below */
    if (sTop > 0) {
        more(x + VALUE_X + 2.0f, y + ROW_TOP + 8.0f, 1, teal);
    }
    if (sTop + SHOWN < ROWS) {
        more(x + VALUE_X + 2.0f, y + ROW_TOP - SHOWN * ROW_STEP + 6.0f, -1, teal);
    }
}

const char* portPauseWardrobeMsg(const char* tag) {
    if (strcmp(tag, "pc_menu_wardrobe") == 0) {
        static char sTab[256];
        static const char* const kTab[PORT_PLAY_AS_COUNT] = {
            "Change how Mario, his partners and the status bar look.",
            "Change how Luigi, his partners and the status bar look.",
            "Change how Peach, her partners and the status bar look.",
            "Change how Bowser, his partners and the status bar look.",
        };
        portPauseWrap(portTrGame(kTab[portPlayAs()]), sTab, sizeof(sTab));
        return sTab;
    }
    if (strncmp(tag, "pc_menu_wardrobe_", 17) == 0) {
        return sHelp; /* the selected row's (pointAt) */
    }
    return NULL;
}
