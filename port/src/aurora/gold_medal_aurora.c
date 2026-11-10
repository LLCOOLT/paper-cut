/* The Gallery (port/include/port/postgame_extra.h): the disc's own enemy
 * and boss models, turning round on a page of the Start menu, chapter by
 * chapter. Each model is the one the Tattle Log shows (the game's Tattle
 * table: its model and pose), grouped by where the Log says it lives, and
 * named in the game's own text. A chapter opens once its story is done and
 * half the Star Pieces of its areas are found (the last, the Palace and the
 * Pit, once the story is over). Left and right: the next model; up and
 * down: nearer and farther; B: back. The Gold Medal's own pieces (its
 * achievement and the gold look) are in achievements_pc.c and cosmetics.cpp. */
#include "port/postgame_extra.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/practice.h"
#include "port/tr.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/msgdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include "win/win_root.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int portCosmeticPoolRoom(void); /* costume_pc.c: room in the pose pool for one more */

#define CHAPTERS 9

static const struct {
    const char* name;
    const char* areas[5]; /* the Tattle Log's places ("menu_monosiri_<area>") and the Star Pieces' maps */
    int storyDone;        /* the story's progress (GSW 0) from which it is done */
} kChapters[CHAPTERS] = {
    {"Rogueport", {"gor", "tik"}, STORY_STG1_00_PETAL_MEADOWS},
    {"Chapter 1: Castle and Dragon", {"hei", "nok", "gon"}, STORY_STG2_00_BOGGLY_WOODS},
    {"Chapter 2: The Great Boggly Tree", {"win", "mri"}, STORY_STG3_00_WELCOME_TO_GLITZVILLE},
    {"Chapter 3: Of Glitz and Glory", {"tou"}, STORY_STG4_00_WELCOME_TO_STAGE_4},
    {"Chapter 4: For Pigs the Bell Tolls", {"usu", "gra", "jin"}, STORY_STG5_00_LOGBOOK},
    {"Chapter 5: The Key to Pirates", {"muj", "dou"}, STORY_STG6_00_START},
    {"Chapter 6: 3 Days of Excess", {"rsh", "eki", "pik", "hom"}, STORY_STG7_00_START},
    {"Chapter 7: Mario Shoots the Moon", {"bom", "moo", "aji"}, STORY_STG8_00_START},
    {"The Palace of Shadow and the Pit", {"las", "jon"}, STORY_STG8_END_AFTER_THE_BATTLE},
};

/* the chapter a Tattle entry belongs to (its place), or the last for the rest */
static int chapterOf(const TattleEntry* t) {
    int c, a;
    const char* place = t->locationTag != NULL && strncmp(t->locationTag, "menu_monosiri_", 14) == 0
                            ? t->locationTag + 14
                            : "";
    for (c = 0; c < CHAPTERS; c++) {
        for (a = 0; a < 5 && kChapters[c].areas[a] != NULL; a++) {
            if (strcmp(place, kChapters[c].areas[a]) == 0) {
                return c;
            }
        }
    }
    return CHAPTERS - 1;
}

/* the Tattle entries with a Log page and a model: the Gallery's */
static int inGallery(int kind) {
    const TattleEntry* t = battleGetUnitMonosiriPtr(kind);
    return t != NULL && t->tattleLogTag != NULL && t->modelName != NULL && t->poseName != NULL && t->enemyTag != NULL;
}

static int modelKind(int chapter, int i) {
    int k;
    for (k = 1; k < 0xD8; k++) {
        if (inGallery(k) && chapterOf(battleGetUnitMonosiriPtr(k)) == chapter && i-- == 0) {
            return k;
        }
    }
    return -1;
}

int portGalleryChapters(void) {
    return CHAPTERS;
}

const char* portGalleryChapterName(int chapter) {
    return chapter >= 0 && chapter < CHAPTERS ? kChapters[chapter].name : "";
}

int portGalleryStarPieces(int chapter, int* total) {
    int i, a, n = 0;
    *total = 0;
    if (chapter < 0 || chapter >= CHAPTERS) {
        return 0;
    }
    for (i = 0; i < portGswfNamesCount; i++) {
        const PortNamedValue* v = &portGswfNames[i];
        if (strstr(v->name, "STAR_PIECE") == NULL || strncmp(v->name, "GSWF_", 5) != 0) {
            continue;
        }
        for (a = 0; a < 5 && kChapters[chapter].areas[a] != NULL; a++) {
            char upper[4];
            int k;
            for (k = 0; k < 3; k++) {
                upper[k] = (char)(kChapters[chapter].areas[a][k] - 'a' + 'A');
            }
            upper[3] = '\0';
            if (strncmp(v->name + 5, upper, 3) == 0) {
                (*total)++;
                n += gp != NULL && swGet(v->value) ? 1 : 0;
                break;
            }
        }
    }
    return n;
}

int portGalleryOpen(int chapter) {
    int total, found;
    if (gp == NULL || chapter < 0 || chapter >= CHAPTERS || swByteGet(0) < kChapters[chapter].storyDone) {
        return 0;
    }
    found = portGalleryStarPieces(chapter, &total);
    return found * 2 >= total;
}

int portGalleryModels(int chapter) {
    int k, n = 0;
    for (k = 1; k < 0xD8; k++) {
        n += inGallery(k) && chapterOf(battleGetUnitMonosiriPtr(k)) == chapter;
    }
    return n;
}

const char* portGalleryModelName(int chapter, int i) {
    const int k = modelKind(chapter, i);
    const char* name = k > 0 && gp != NULL ? msgSearch(battleGetUnitMonosiriPtr(k)->enemyTag) : NULL;
    return name != NULL ? name : "?";
}

/* --- the page ---------------------------------------------------------------- */

static int sChapter, sModel, sPose = -1, sPoseKind = -1;
static float sScale = 1.6f;
static char sHelp[256];

/* the pose was taken while the page was up: the menu closed another way than B (Start, online, a state
   load) leaves it, and a map change since freed it and may have given its slot to an NPC or Mario. Not
   seen for a few frames: forgotten, never released (a pose still ours goes with the map's) */
static unsigned sPoseSeen;

static void stillOurs(void) {
    if (sPose >= 0 && gp != NULL && (unsigned)gp->retraceCount - sPoseSeen > 2) {
        sPose = -1;
        sPoseKind = -1;
    }
    if (gp != NULL) {
        sPoseSeen = (unsigned)gp->retraceCount;
    }
}

static void letGo(void) {
    stillOurs();
    if (sPose >= 0) {
        animPoseRelease(sPose);
    }
    sPose = -1;
    sPoseKind = -1;
}

static void take(void) {
    const int k = modelKind(sChapter, sModel);
    const TattleEntry* t;
    stillOurs();
    if (k == sPoseKind) {
        return;
    }
    letGo();
    if (k <= 0 || !portCosmeticPoolRoom()) {
        return;
    }
    t = battleGetUnitMonosiriPtr(k);
    sPose = animPoseEntry(t->modelName, 0);
    if (sPose >= 0) {
        animPoseSetAnim(sPose, t->poseName, 1);
        animPoseSetMaterialFlagOn(sPose, ANIM_MATERIAL_FLAG_NO_BLEND | ANIM_MATERIAL_FLAG_NO_BLEND2);
        sPoseKind = k;
    }
}

static void pageOpen(int chapter) {
    sChapter = chapter;
    sModel = 0;
    sScale = 1.6f;
    letGo();
}

static int pageMain(WindowWork* wp) {
    const int n = portGalleryModels(sChapter);
    if (wp->buttonTrg & PAD_BUTTON_B) {
        letGo();
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (n > 0 && (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT))) {
        sModel = (sModel + ((wp->dirRep & DEMO_STICK_LEFT) ? n - 1 : 1)) % n;
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_UP) {
        sScale = sScale < 3.0f ? sScale + 0.2f : sScale;
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        sScale = sScale > 0.6f ? sScale - 0.2f : sScale;
    }
    take();
    return 0;
}

static void text(float x, float y, float s, GXColor c, const char* t) {
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){s, s, s}, c, "%s", t);
}

static void pageDisp(WindowWork* wp, float x, float y) {
    const GXColor heading = {0x5A, 0x46, 0x9C, 0xFF}, black = {0x00, 0x00, 0x00, 0xFF};
    const int n = portGalleryModels(sChapter);
    char line[96];
    winKirinukiGX(wp, 0, x - 270.0f, y + 144.0f, 540.0f, 270.0f);
    text(x - 250.0f, y + 132.0f, 0.75f, heading, portTrGame(portGalleryChapterName(sChapter)));
    if (n == 0) {
        return;
    }
    snprintf(line, sizeof(line), "%s  (%d/%d)", portGalleryModelName(sChapter, sModel), sModel + 1, n);
    text(x - 250.0f, y + 104.0f, 0.7f, black, line);
    stillOurs();
    if (sPose >= 0) {
        AnimData* anim = animPoseGetAnimDataPtr(sPose);
        Mtx m, sc, ry;
        /* a full turn every ten seconds: the turnaround */
        const float angle = (float)(gp->retraceCount % 600) / 600.0f * 6.2831853f;
        if (anim != NULL && anim->loopInfo[2] > 0) {
            animPoseSetLocalTime(sPose, (float)(gp->retraceCount % (s32)anim->loopInfo[2]));
        }
        animPoseMain(sPose);
        winKageGX(wp, (GXColor){0x00, 0x00, 0x00, 0x80}, x, y - 100.0f, 0.0f, sScale);
        PSMTXTrans(m, x, y - 100.0f, 0.0f);
        PSMTXScale(sc, sScale, sScale, sScale);
        PSMTXRotRad(ry, 'y', angle);
        PSMTXConcat(m, sc, m);
        PSMTXConcat(m, ry, m);
        winZClear();
        animPoseDrawMtx(sPose, m, 2, 0.0f, 5.0f);
    }
}

static const char* pageHelp(void) {
    portPauseWrap(portTrGame("Left and right: the next model. Up and down: nearer and farther. {B}: back."), sHelp,
                  sizeof(sHelp));
    return sHelp;
}

static const PortPausePage kGalleryPage = {pageOpen, pageMain, pageDisp, pageHelp};

void portGalleryOpenPage(int chapter) {
    portPauseOpenPage(&kGalleryPage, chapter);
}
