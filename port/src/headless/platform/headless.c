/* Headless platform backend: no window, no input. Lets the game run (and
 * be tested) before a windowed backend exists. */
#include "port/file_pc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dolphin/gx.h>
#include "wide_x.h"
#include "port/platform.h"
#include "port/settings.h"
#include "battle/battle_unit.h"
#include "port/directors_cut.h"
#include "port/dungeon_master.h"
#include "port/open_world.h"

void portPlatformInit(void) {}

BOOL portPlatformPoll(void) {
    return TRUE;
}

void portPlatformPresent(void) {}

BOOL portPlatformReadPad(s32 chan, PADStatus* status) {
    return chan == 0; /* a controller in port 1 with nothing pressed */
}

/* Frame captures: nothing is drawn headless. A capture is "pending" for as
 * many retraces as Aurora's takes (the frame drawn, then its readback), so a
 * script's end after a shot step leaves at the same frame on both backends. */
#define SHOT_FRAMES 3
static int sShotLeft;

void portShotRequest(const char* name) {
    (void)name;
    sShotLeft = SHOT_FRAMES;
}

void portShotRequestIn(const char* name, int frames) {
    (void)name, (void)frames;
}

BOOL portShotPending(void) {
    return sShotLeft > 0;
}

void portShotFrame(void) {
    if (sShotLeft > 0) {
        sShotLeft--;
    }
}

void portInterpSceneDrawn(void) {}
/* text to speech and the message log (port/include/port/speech.h): the settings page's, not here */
void portSpeechMessage(const void* window, const char* text) {
    (void)window;
    (void)text;
}
void portSpeechAppend(const void* window, const char* text) {
    (void)window;
    (void)text;
}
void portSpeechPage(const void* window, int page, const char* speaker) {
    (void)window;
    (void)page;
    (void)speaker;
}
void portSpeechClosed(const void* window) {
    (void)window;
}
void portSpeechMenu(const char* text) {
    (void)text;
}
/* captions for sound cues (port/include/port/captions.h): the settings page's, not here */
void portCaptionSfx(const char* name, const float* pos) {
    (void)name;
    (void)pos;
}

void portCaptionPan(int pan) {
    (void)pan;
}

BOOL portInterpInBetween(void) {
    return FALSE;
}

BOOL portInterpStepsHere(void) {
    return TRUE;
}

void* portInterpScratchAlloc(u32 size) {
    (void)size;
    return NULL;
}

void portInterpKeep(void* p, unsigned int n) {
    (void)p;
    (void)n;
}

/* Play as Luigi's faces (port/src/aurora/luigi_faces_aurora.c): none headless, the game's own icons */
int portIconTexOverride(unsigned int iconId, void* texObj) {
    (void)iconId;
    (void)texObj;
    return 0;
}

void portLuigiFaceForce(int luigi) {
    (void)luigi;
}

/* Cool Mode's sunglasses (luigi_faces_aurora.c): none headless */
int portFunCoolTexture(void* texObj) {
    (void)texObj;
    return 0;
}

/* settings.txt (port/src/aurora/settings_aurora.c): none headless; the
 * costumes' and skins' choices (costume_pc.c, skin_pc.c) stay for the run */
void portSettingsSave(void) {
}
/* a setting changed by key (the dojo's suggestions, dojo_pc.c): the declared ones only */
int portSettingsSetKey(const char* key, const char* value) {
    return portSettingsExtraSet(key, value);
}

/* Button tokens in the game's text (port/settings/features/button_markup.cpp): left as written headless */
const char* portButtonsGame(const char* text) {
    return text;
}

/* Menu speed (port/settings/features/menu_speed.cpp): the game's own slides headless */
int portMenuFrames(int frames) {
    return frames;
}

/* Confirm and back in Paper Cut's menus (port/settings/features/menu_buttons.cpp): the game's
 * buttons as they are headless (no controller of a kind) */
unsigned portMenuButtons(int port, unsigned buttons) {
    (void)port;
    return buttons;
}

unsigned portMenuButtonsAsOwn(int port, unsigned buttons) {
    (void)port;
    return buttons;
}

/* Paper Cut's open world (port/include/port/open_world.h): its seed side
 * (port/archipelago/open_world.cpp) is built headless too; a new file is
 * armed by the settings (Aurora), a test's by $TTYD_OPEN_SEED */
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools, int* goal) {
    (void)seed;
    (void)seedMax;
    (void)pools;
    (void)goal;
    return 0;
}
void portOpenArmRules(PortOpenRules* out) {
    out->level = out->tricks = out->extra = out->entrances = 0;
}
int portRandoRunMystery(void) {
    return 0;
}
/* The port's translations (port/settings/tr.cpp): none headless, its English */
const char* portTr(const char* english) {
    return english;
}
const char* portTrGame(const char* english) {
    return english;
}
void portTextForGame(const char* utf8, char* out, size_t size) { /* its ASCII */
    size_t n = 0;
    for (; utf8 != NULL && *utf8 != '\0' && n + 1 < size; utf8++) {
        if ((unsigned char)*utf8 < 0x80) {
            out[n++] = *utf8 == '<' ? '(' : *utf8 == '>' ? ')' : *utf8;
        }
    }
    if (size != 0) {
        out[n] = '\0';
    }
}
int portTrLanguage(void) { /* English */
    return 0;
}

/* Lua mods (port/include/port/lua_mods.h): none headless */
int portLuaRule(const char* key, int* value) {
    (void)key;
    (void)value;
    return 0;
}
void portLuaGuard(void* unit, int super) {
    (void)unit;
    (void)super;
}
int portLuaGuardResult(int result) {
    return result;
}
int portLuaCommandLevel(int level) {
    return level;
}
int portLuaSkinChoice(int party) {
    (void)party;
    return -2;
}
int portLuaCostumeChoice(void) {
    return -1;
}

/* Director's Cut (port/src/aurora/directors_cut_aurora.c): off headless */
int portDirectorsCut(void) {
    return 0;
}
int portDirectorsCutFlag(int gswf) {
    (void)gswf;
    return 0;
}
int portDirectorsCutDazzle(void) {
    return 0;
}
int portDirectorsCutDazzleCount(void) {
    return 0;
}
int portDirectorsCutBadge(int item) {
    (void)item;
    return 0;
}
/* Tattle cards (port/src/aurora/cards_aurora.c): gor_02's card players only talk headless */
struct EventEntry;
int portEvtCardsTalk(struct EventEntry* event, int isFirstCall) {
    (void)event;
    (void)isFirstCall;
    return 2; /* EVT_RETURN_DONE */
}
const char* portDirectorsCutMsg(const char* tag) {
    (void)tag;
    return 0;
}
void portDirectorsCutRel(const char* rel, const PortDirectorsCutSelect* selects,
                         const PortDirectorsCutEnemy* enemies) {
    (void)rel;
    (void)selects;
    (void)enemies;
}
void portRemakeAmbushRel(const char* rel, const PortDirectorsCutEnemy* ambushes) {
    (void)rel;
    (void)ambushes;
}
void* portDirectorsCutSelect(void* select) {
    return select;
}
int portBoardRunState(void) {
    return 0; /* (no leaderboard runs in the headless build) */
}
void portDirectorsCutSetup(const char* map, void** data, unsigned int* size) {
    (void)map;
    (void)data;
    (void)size;
}
void portDirectorsCutSetupDone(const char* map) {
    (void)map;
}
int portDirectorsCutLevelUpConfirm(void) {
    return 0;
}
int portEvtDirectorsCutBero(struct EventEntry* event, int isFirstCall) {
    (void)event;
    (void)isFirstCall;
    return 2; /* EVT_RETURN_DONE */
}

/* Versus battles (port/src/aurora/versus_aurora.c): none headless */
int portVersusOn(void) {
    return 0;
}

int portVersusPlayerUnit(const void* unit) {
    return unit != NULL && ((const BattleUnit*)unit)->alliance == 0;
}

void* portVersusGuardPad(const void* unit, int* handled) {
    (void)unit;
    *handled = 0;
    return NULL;
}

int portVersusRoute(void) {
    return 0;
}

/* Dungeon master (port/src/aurora/dungeon_master_aurora.c): none here. */
int gPortDmLive;

int portDmUserFunc(struct EventEntry* entry, int first, int* result) {
    (void)entry;
    (void)first;
    (void)result;
    return 0;
}

int portDmHold(struct EventEntry* entry) {
    (void)entry;
    return 0;
}

int portDmPlayer(void) {
    return 0;
}

int portDmPlaysEnemies(int player) {
    (void)player;
    return 0;
}

void portDmRegister(void) {
}

int portVersusShowstopperSoft(void) {
    return 0;
}

int portSacFoeAlliance(void) {
    return 1;
}

int portVersusStopTime(const void* attacker, int time) {
    (void)attacker;
    return time;
}

/* Photo mode's depth of field (view_pc.c) asks the frame's shape: 4:3 here. */
int portWidescreenWide(void) {
    return 0;
}

float portWidescreenAspect(void) {
    return 4.0f / 3.0f;
}

/* EFB copies of the frame (include/wide_x.h): the game's own 4:3 frame here */
float portWidescreenScaleX(void) {
    return 1.0f;
}

void portScreenSpaceBegin(void) {
}

void portScreenSpaceEnd(void) {
}

float portGameEfbX(float x) {
    return x;
}

void portTexCopySrcGame(u16 left, u16 top, u16 wd, u16 ht) {
    GXSetTexCopySrc(left, top, wd, ht);
}

int portMirrorReadable(float (*model)[4]) {
    (void)model;
    return 0;
}

void portMirrorReadableEnd(int on) {
    (void)on;
}

int portMirrorHold(void) {
    return 0;
}

int portMirrorAs3D(void) {
    return 0;
}

/* a 3D view inside a picture (include/wide_x.h): the game's own 4:3 frame here */
float portWideFrameStretch(void) {
    return 1.0f;
}

void portWideView3DStretch(float stretch) {
    (void)stretch;
}

void portWideSqueezeBegin(void) {
}

void portWideSqueezeEnd(void) {
}

/* the inns' bed shot's bars (widescreen_aurora.c): nothing is drawn here */
void portWideBedShot(int on) {
    (void)on;
}

int portVersusAutoCommand(const void* unit) {
    (void)unit;
    return -1;
}

/* paper looks (port/include/port/paper.h): nothing is drawn here */
void portPaperGxShadow(float strength, float soft, const float* feetView, float length) {
    (void)strength;
    (void)soft;
    (void)feetView;
    (void)length;
}

void portPaperGxEdges(int on) {
    (void)on;
}

/* Battery saver (port/src/aurora/battery_aurora.c): headless draws every frame */
int portBatterySkipDraw(void) {
    return 0;
}
void portBatteryFrameUndrawn(void) {
}
int portBatteryUndrawn(void) {
    return 0;
}
int portDrawFrames(void) {
    return 1;
}

/* Battle extras (port/include/port/battlex.h): the enemy variants' texts are
 * game messages set on Aurora; headless keeps the game's */
/* Paper Mario 64's bosses (pm64_bosses_aurora.c): none headless */
struct BattleUnit;
int portPm64BossDraw(const struct BattleUnit* unit, int* sprite, int* palette, int* anim, float* scale) {
    (void)unit;
    (void)sprite;
    (void)palette;
    (void)anim;
    (void)scale;
    return 0;
}
int portPm64BossCardRow(const struct BattleUnit* unit, int* sprite, int* palette, int* row) {
    (void)unit;
    (void)sprite;
    (void)palette;
    (void)row;
    return 0;
}
int portPm64BossLayer(const struct BattleUnit* unit, int row, int n, int* anim) {
    (void)unit;
    (void)row;
    (void)n;
    (void)anim;
    return 0;
}
int portPm64BossVine(const struct BattleUnit* unit, int* rowA, int* rowB, float* height) {
    (void)unit;
    (void)rowA;
    (void)rowB;
    (void)height;
    return 0;
}
int portPm64BossFx(const struct BattleUnit* unit, int* glow, float from[3], float to[3], int* age) {
    (void)unit;
    (void)from;
    (void)to;
    (void)age;
    *glow = 0;
    return 0;
}

void portBattlexVariantTexts(int kind, const char* name, const char* tattle) {
    (void)kind;
    (void)name;
    (void)tattle;
}

/* the user folder (settings_aurora.c on Aurora): $TTYD_USER_DIR, else here */
const char* portSettingsUserPath(void) {
    /* without a trailing separator, as on Aurora: "<user>/<name>" joins have exactly one */
    static char sDir[1024];
    const char* dir = getenv("TTYD_USER_DIR");
    size_t n;
    if (dir == NULL || dir[0] == '\0') {
        return ".";
    }
    snprintf(sDir, sizeof(sDir), "%s", dir);
    n = strlen(sDir);
    while (n > 1 && (sDir[n - 1] == '/' || sDir[n - 1] == '\\') && sDir[n - 2] != ':') {
        sDir[--n] = '\0';
    }
    return sDir;
}

/* settings.txt in the user folder ($TTYD_USER_DIR), as Aurora reads it at
 * start (settings_aurora.c): the settings that change how the game plays
 * (the others are the window's, the picture's and the sound device's), and
 * every feature's own (settings_extra.c); $TTYD_SET_<key> over them for the
 * run. The same file gives the same game on both backends. */
#include <stdio.h>
#include <string.h>
#include "port/gameplay.h"
#include "port/ac_cue.h"
#include "port/widescreen.h"
#include "port/pause_settings.h"
#include "port/pm64_media.h"
#include "port/tr.h"
#include "driver/msgdrv.h"

typedef struct {
    const char* key;
    int value, min, max;
} HeadlessField;

static HeadlessField sFields[] = {
    {"skip_intro", 0, 0, 1}, {"text_speed", 0, 0, 3}, {"run_speed", 100, 100, 150},
    {"ac_cue", 0, 0, 2},     {"wide_hud", 1, 0, 1},
};
#define HEADLESS_FIELDS (int)(sizeof(sFields) / sizeof(sFields[0]))

/* the fixed fields one by one ("sync what matters" online's battle settings, statesync_battle_pc.c):
 * key i and its value now, "" for one with no number, NULL past the last */
const char* portSettingsFieldAt(int i, int* value) {
    if (i < 0 || i >= HEADLESS_FIELDS) {
        return NULL;
    }
    *value = sFields[i].value;
    return sFields[i].key;
}

/* tests: $TTYD_SETTINGS_DUMP's fixed fields (port/src/common/settings_extra.c) */
void portSettingsDumpFields(void* file) {
    int i;
    for (i = 0; i < HEADLESS_FIELDS; i++) {
        fprintf((FILE*)file, "field\t%s\t%d\t%d\t%d\n", sFields[i].key, sFields[i].value, sFields[i].min,
                sFields[i].max);
    }
}

static void headlessSet(const char* key, const char* value) {
    int i;
    for (i = 0; i < HEADLESS_FIELDS; i++) {
        if (strcmp(key, sFields[i].key) == 0) {
            const int v = atoi(value);
            sFields[i].value = v < sFields[i].min ? sFields[i].min : v > sFields[i].max ? sFields[i].max : v;
            return;
        }
    }
    portSettingsExtraSet(key, value); /* a feature's own */
}

/* the fields into the game's globals */
static void headlessGlobals(void) {
    static const int kTextSpeeds[] = {100, 200, 400, 10000};
    gPortSkipIntro = sFields[0].value;
    gPortTextSpeed = kTextSpeeds[sFields[1].value];
    gPortRunSpeed = sFields[2].value / 100.0f;
    gPortAcCue = sFields[3].value;
    gPortWideHud = sFields[4].value;
}

/* a setting's value for this run ("sync what matters" online's shared battle: settings.h);
 * nothing is saved headless, so holding saves is nothing here */
void portSettingsSetForRun(const char* key, const char* value) {
    int i;
    for (i = 0; i < HEADLESS_FIELDS; i++) {
        if (strcmp(key, sFields[i].key) == 0) {
            headlessSet(key, value);
            return;
        }
    }
    portSettingsExtraForce(key, value);
}

void portSettingsHoldSaves(int hold) {
    (void)hold;
}

void portSettingsApply(void) {
    headlessGlobals();
}

void portHeadlessSettingsLoad(void) {
    char path[1100], line[512];
    FILE* f;
    int i;
    snprintf(path, sizeof(path), "%s/settings.txt", portSettingsUserPath());
    f = portFopen(path, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            char* eq = strchr(line, '=');
            line[strcspn(line, "\r\n")] = '\0';
            if (eq != NULL) {
                *eq = '\0';
                headlessSet(line, eq + 1);
            }
        }
        fclose(f);
    }
    for (i = 0; i < HEADLESS_FIELDS; i++) {
        char env[64];
        const char* over;
        snprintf(env, sizeof(env), "TTYD_SET_%s", sFields[i].key);
        if ((over = getenv(env)) != NULL) {
            headlessSet(sFields[i].key, over);
        }
    }
    headlessGlobals();
}

/* the pause menu's tab row (port/src/aurora/pause_settings_aurora.c): Aurora's places, Paper Cut's
 * three tabs included (src/window/win_root.c), so the menu moves as on Aurora */
float portPauseTabX(int n, int i) {
    return n >= 8 ? i * 70.0f - 245.0f : n >= 7 ? i * 78.0f - 234.0f : n >= 6 ? i * 90.0f - 225.0f : i * 105.0f - 205.0f;
}

float portPauseTabScale(int n) {
    return n >= 8 ? 0.7f : n >= 7 ? 0.76f : n >= 6 ? 0.86f : 1.0f;
}

/* Paper Cut's tags its shared code asks for here that only Aurora's sources answer
 * (tools/port/msg_tags.py): the tab row's help (the Journal's is the game's own, d-pad down opening
 * the menu on it) and Dizzy Stomp's name and description (pm64_badges_aurora.c), in Aurora's words.
 * Nothing is drawn; a message that is there keeps the "missing message" lines out of the log. */
const char* portHeadlessMsg(const char* tag) {
    static char sDesc[256];
    char name[64];
    if (strcmp(tag, "pc_menu_kiroku") == 0) {
        return msgSearch("msg_menu_kiroku");
    }
    if (strcmp(tag, "pc_menu_settings") == 0) {
        return portTrGame("Change Paper Cut's settings here.");
    }
    if (strcmp(tag, "pc_menu_wardrobe") == 0) {
        return portTrGame("Change how Mario, his partners and the status bar look.");
    }
    if (strcmp(tag, "pc_menu_online") == 0) {
        return portTrGame("Play together, on this screen or online: the adventure, party games, who's who.");
    }
    if (strcmp(tag, "pc_pm64_dizzy_stomp") == 0) {
        return portPm64BadgeEnglish(PORT_PM64_BADGE_DIZZY_STOMP);
    }
    if (strcmp(tag, "pc_pm64_dizzy_stomp_desc") == 0) {
        if (!portPm64BadgeText(PORT_PM64_BADGE_DIZZY_STOMP, name, sizeof(name), sDesc, sizeof(sDesc))) {
            snprintf(sDesc, sizeof(sDesc), "%s", portPm64BadgeEnglish(PORT_PM64_BADGE_DIZZY_STOMP));
        }
        return sDesc;
    }
    return NULL;
}

/* $TTYD_OFFSCREEN: only Aurora has a window to keep hidden (main_aurora.c) */
int portOffscreen(void) {
    return 0;
}

/* first-time tips (port/include/port/tips.h): Aurora's only */
int portTipOffer(int tip) {
    (void)tip;
    return 0;
}

int portTipsAllowed(void) {
    return 0;
}

int portTipSeen(int tip) {
    (void)tip;
    return 1;
}
