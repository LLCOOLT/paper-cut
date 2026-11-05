/* Headless platform backend: no window, no input. Lets the game run (and
 * be tested) before a windowed backend exists. */
#include <stdlib.h>
#include "port/platform.h"
#include "battle/battle_unit.h"
#include "port/directors_cut.h"
#include "port/dungeon_master.h"

void portPlatformInit(void) {}

BOOL portPlatformPoll(void) {
    return TRUE;
}

void portPlatformPresent(void) {}

BOOL portPlatformReadPad(s32 chan, PADStatus* status) {
    return chan == 0; /* a controller in port 1 with nothing pressed */
}

/* Frame captures: nothing is drawn headless. */
void portShotRequest(const char* name) {
    (void)name;
}

BOOL portShotPending(void) {
    return FALSE;
}

void portShotFrame(void) {}

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

BOOL portInterpInBetween(void) {
    return FALSE;
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

/* settings.txt (port/src/aurora/settings_aurora.c): none headless; the
 * costumes' and skins' choices (costume_pc.c, skin_pc.c) stay for the run */
void portSettingsSave(void) {
}
/* a setting changed by key (the dojo's suggestions, dojo_pc.c): the declared ones only */
int portSettingsSetKey(const char* key, const char* value) {
    return portSettingsExtraSet(key, value);
}

/* Menu speed (port/settings/features/menu_speed.cpp): the game's own slides headless */
int portMenuFrames(int frames) {
    return frames;
}

/* Paper Cut's open world (port/include/port/open_world.h): its seed side is
 * the Archipelago support's (Aurora); headless never stamps a file */
int portOpenPartnerReward(int partner) {
    (void)partner;
    return 0;
}
int portOpenChestReward(int curse) {
    (void)curse;
    return 0;
}
void portOpenAreaLinked(const char* area) {
    (void)area;
}
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools, int* goal) {
    (void)seed;
    (void)seedMax;
    (void)pools;
    (void)goal;
    return 0;
}
const char* portOpenMsg(const char* tag) {
    (void)tag;
    return 0;
}
const char* portOpenText(const char* english) {
    return english;
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
void* portDirectorsCutSelect(void* select) {
    return select;
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

/* Photo mode's depth of field (view_pc.c) asks the frame's shape: 4:3 here. */
int portWidescreenWide(void) {
    return 0;
}

float portWidescreenAspect(void) {
    return 4.0f / 3.0f;
}

int portVersusAutoCommand(const void* unit) {
    (void)unit;
    return -1;
}

/* paper looks (port/include/port/paper.h): nothing is drawn here */
void portPaperGxShadow(int on) {
    (void)on;
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
void portBattlexVariantTexts(int kind, const char* name, const char* tattle) {
    (void)kind;
    (void)name;
    (void)tattle;
}

/* the user folder (settings_aurora.c on Aurora): $TTYD_USER_DIR, else here */
const char* portSettingsUserPath(void) {
    const char* dir = getenv("TTYD_USER_DIR");
    return dir != NULL && dir[0] != '\0' ? dir : ".";
}
