/* Settings a feature declares from its own file (port/include/port/settings.h:
 * portSettingsDeclare), kept by key beside PortSettings' fixed fields, so
 * features added side by side don't all edit one struct and one table. The
 * Aurora build reads and writes them in settings.txt with the others
 * (settings_aurora.c); any build takes $TTYD_SET_<key> for a run (tests). A
 * value read before its declaration waits, unclamped, until it comes. */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/settings.h"

/* pointers to the values are handed out, so the table never moves: room for
 * every feature's settings and many more */
#define EXTRA_MAX 2048
/* keys read before their declaration (settings.txt's, an online host's, a replay's) stop here, so the
   declarations always find room: a settings.txt grown over releases, damaged or crafted, or a host
   sending thousands of keys, can't fill the table */
#define UNDECLARED_MAX 512
/* of those, the ones that came only from an online host (portSettingsExtraForce): never saved */
#define RUN_ONLY_MAX 128

typedef struct Extra {
    char key[48];
    int value, min, max, def;
    int declared;
    int toDefault; /* at its default once declared (portSettingsExtraToDefault) */
    int savedAs, savedValue; /* settings.txt gets savedValue meanwhile (portSettingsExtraSaveAs) */
    int runOnly; /* only an online host's so far: the player has no value of their own, nothing saved */
} Extra;

static Extra sExtra[EXTRA_MAX];
static int sCount;
static int sUndeclared, sRunOnly;
/* a declaration with the table full anyway: a value of its own for the run, not saved */
#define SPARE_MAX 32
static Extra sSpare[SPARE_MAX];
static int sSpareCount;

/* Tests (tools/port/settings_fuzz.py): $TTYD_SETTINGS_DUMP=<file>: as the run
 * ends, every setting kept here, a line each, tab-separated:
 *   extra <key> <value> <min> <max> <def> <declared>
 * then the backend's fixed fields (Aurora: settings_aurora.c):
 *   field <key> <value> <min> <max>
 * (a value read but never declared has no range yet: declared 0) */
__attribute__((weak)) void portSettingsDumpFields(void* file) {
    (void)file;
}

static void dumpAtExit(void);

void portSettingsDumpNow(void) {
    const char* path = getenv("TTYD_SETTINGS_DUMP");
    FILE* f;
    int i;
    if (path == NULL || path[0] == '\0' || (f = portFopen(path, "w")) == NULL) {
        return;
    }
    for (i = 0; i < sCount; i++) {
        const Extra* e = &sExtra[i];
        fprintf(f, "extra\t%s\t%d\t%d\t%d\t%d\t%d\n", e->key, e->value, e->min, e->max, e->def, e->declared);
    }
    portSettingsDumpFields(f);
    fclose(f);
}

static void dumpAtExit(void) {
    portSettingsDumpNow();
}

/* Tests (the menu crawler, port/src/aurora/menu_crawl_aurora.c): how many
 * declared settings hold a value outside their range; the first in bad */
int portSettingsExtraCheck(char* bad, int size) {
    int i, n = 0;
    for (i = 0; i < sCount; i++) {
        const Extra* e = &sExtra[i];
        if (e->declared && (e->value < e->min || e->value > e->max) && n++ == 0 && bad != NULL && size > 0) {
            snprintf(bad, (size_t)size, "%s=%d (%d..%d)", e->key, e->value, e->min, e->max);
        }
    }
    return n;
}

static Extra* find(const char* key) {
    /* the entry last found for a key at this address (callers name settings with literals, and several look
     * theirs up every frame: one comparison instead of a walk past hundreds); checked by name, so a key in a
     * reused buffer still finds its own (entries never move or go) */
    static struct {
        const char* key;
        Extra* e;
    } sMemo[64];
    const unsigned slot = ((unsigned)((size_t)key >> 3) * 0x9E3779B1u) >> 26;
    int i;
    if (sMemo[slot].key == key && sMemo[slot].e != NULL && strcmp(sMemo[slot].e->key, key) == 0) {
        return sMemo[slot].e;
    }
    for (i = 0; i < sCount; i++) {
        if (strcmp(sExtra[i].key, key) == 0) {
            sMemo[slot].key = key;
            sMemo[slot].e = &sExtra[i];
            return &sExtra[i];
        }
    }
    for (i = 0; i < sSpareCount; i++) {
        if (strcmp(sSpare[i].key, key) == 0) {
            return &sSpare[i];
        }
    }
    return NULL;
}

/* a declaration's entry: never fatal. With the table full (it can't be, with the undeclared ones capped,
   but a feature declaring thousands could), a spare one for the run, then one shared scratch value */
static Extra* add(const char* key) {
    static Extra sScratch;
    Extra* e;
    if (sCount < EXTRA_MAX) {
        if (sCount == 0 && getenv("TTYD_SETTINGS_DUMP") != NULL) {
            atexit(dumpAtExit); /* (tests: registered with the first setting kept) */
        }
        e = &sExtra[sCount++];
    } else if (sSpareCount < SPARE_MAX) {
        fprintf(stderr, "settings: no room for %s (kept for this run, not saved)\n", key);
        e = &sSpare[sSpareCount++];
    } else {
        fprintf(stderr, "settings: no room for %s (not kept)\n", key);
        e = &sScratch;
    }
    memset(e, 0, sizeof(*e));
    snprintf(e->key, sizeof(e->key), "%s", key); /* (a key too long is cut: found again by its cut name) */
    return e;
}

/* a key read from a file or sent by another game (settings.txt, the online
 * host's, a replay's) before its declaration: one that can't be kept is left
 * out with a line in the log, not fatal */
static Extra* addRead(const char* key, int runOnly) {
    Extra* e;
    if (sUndeclared >= UNDECLARED_MAX || sCount >= EXTRA_MAX || strlen(key) >= sizeof(sExtra[0].key) ||
        (runOnly && sRunOnly >= RUN_ONLY_MAX)) {
        fprintf(stderr, "settings: %s left out (%s)\n", key,
                strlen(key) >= sizeof(sExtra[0].key) ? "key too long" : "too many settings not known here");
        return NULL;
    }
    e = add(key);
    sUndeclared++;
    if (runOnly) {
        e->runOnly = 1;
        sRunOnly++;
    }
    return e;
}

static void ownValue(Extra* e) {
    if (e->runOnly) {
        e->runOnly = 0;
        sRunOnly--;
    }
}

/* $TTYD_CLEAN_FRAME=1 (media captures: tools/port/media, the README shot plan's clean frame): the settings that
 * put something over the picture, off for the run (never saved): paper notes, What's new and the launcher's
 * news (seen), the update check, the settings hint (shown), the speedrun timer, the performance overlay, the
 * scenes' black edges (aspect_scene_limits), Discord, the quick resume offer. (The first-time tips, the graphics
 * watchdog and the achievements' toasts read it themselves: tips_aurora.c, gfx_guard_aurora.c,
 * achievements_pc.c. Achievements stay on, still earned and listed: the Achievements page's own shot needs
 * them.) A $TTYD_SET_<key> still wins over it. */
static const struct {
    const char* key;
    const char* value;
} kCleanFrame[] = {
    {"paper_notes", "0"},        {"whats_new_seen", "999999"},
    {"launcher_news_seen", "999999"}, {"update_check", "0"},  {"settings_hint_shown", "1"},
    {"speedrun_timer", "0"},     {"perf_overlay", "0"},       {"aspect_scene_limits", "0"},
    {"discord_status", "0"},     {"quick_resume", "0"},
};

int portCleanFrame(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* v = getenv("TTYD_CLEAN_FRAME");
        sOn = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    return sOn;
}

const char* portSettingsRunOverride(const char* key) {
    char env[80];
    const char* over;
    size_t i;
    snprintf(env, sizeof(env), "TTYD_SET_%s", key);
    if ((over = getenv(env)) != NULL) {
        return over;
    }
    if (portCleanFrame()) {
        for (i = 0; i < sizeof(kCleanFrame) / sizeof(kCleanFrame[0]); i++) {
            if (strcmp(kCleanFrame[i].key, key) == 0) {
                return kCleanFrame[i].value;
            }
        }
    }
    return NULL;
}

static int clamp(const Extra* e, int v) {
    return v < e->min ? e->min : v > e->max ? e->max : v;
}

int* portSettingsDeclare(const char* key, int def, int min, int max) {
    char env[80];
    const char* over;
    Extra* e = find(key);
    if (e != NULL && e->declared) {
        return &e->value; /* declared twice (two files sharing a key): one value */
    }
    if (e == NULL) {
        e = add(key);
        e->value = def;
    } else {
        sUndeclared--; /* (one read before: room for another) */
    }
    e->min = min;
    e->max = max;
    e->def = def;
    e->declared = 1;
    if (e->toDefault) {
        e->value = def; /* (this run's set doesn't have it: online's guest, a playback) */
        e->toDefault = 0;
        ownValue(e); /* (a host's value that ended with its session: the player's own default from here) */
    }
    (void)env;
    if ((over = portSettingsRunOverride(key)) != NULL) {
        e->value = atoi(over);
    }
    e->value = clamp(e, e->value);
    return &e->value;
}

int* portSettingsFind(const char* key) {
    Extra* e = find(key);
    return e != NULL && e->declared ? &e->value : NULL;
}

int portSettingsUpgradeOnce(const char* mark, int fileFound) {
    int* m = portSettingsDeclare(mark, 0, 0, 1);
    const int now = fileFound && *m == 0;
    *m = 1;
    return now;
}

/* a setting's value, declared or only read so far (settings.txt's, a feature that declares it on first use):
   1 when there is one */
int portSettingsExtraRaw(const char* key, int* value) {
    const Extra* e = find(key);
    if (e == NULL || e->toDefault) {
        return 0;
    }
    *value = e->value;
    return 1;
}

/* a declared setting's default (settings presets); 0 when it isn't declared */
int portSettingsExtraDefault(const char* key, int* value) {
    const Extra* e = find(key);
    if (e == NULL || !e->declared) {
        return 0;
    }
    *value = e->def;
    return 1;
}

int portSettingsExtraSet(const char* key, const char* value) {
    char env[80];
    Extra* e = find(key);
    if (e == NULL && (e = addRead(key, 0)) == NULL) {
        return 0;
    }
    (void)env;
    ownValue(e); /* (the player's own now: saved) */
    e->toDefault = 0; /* (a value set since: it wins) */
    if (portSettingsRunOverride(key) == NULL) { /* the run's override stays */
        e->value = atoi(value);
        if (e->declared) {
            e->value = clamp(e, e->value);
        }
    }
    return 1;
}

/* a value for this run, over the run's own override too (online co-op's
 * guest: the host's settings, port/settings/online_lobby.cpp). A key not
 * known here yet (declared on first use) is kept for its declaration, but as
 * the host's only: never written to this player's settings.txt, and only so
 * many (a host sending thousands is refused past RUN_ONLY_MAX) */
void portSettingsExtraForce(const char* key, const char* value) {
    Extra* e = find(key);
    if (e == NULL && (e = addRead(key, 1)) == NULL) {
        return;
    }
    e->toDefault = 0;
    e->value = atoi(value);
    if (e->declared) {
        e->value = clamp(e, e->value);
    }
}

int portSettingsExtraToDefault(const char* key) {
    Extra* e = find(key);
    if (e == NULL) {
        return 0;
    }
    if (e->declared) {
        e->value = e->def;
        ownValue(e); /* (the host's value is gone: what the player changes it to from here is saved) */
    } else {
        e->toDefault = 1; /* (an online host's only: its declaration makes it the player's, above) */
    }
    return 1;
}

void portSettingsExtraSaveAs(const char* key, int on, int value) {
    Extra* e = find(key);
    if (e != NULL) {
        e->savedAs = on;
        e->savedValue = value;
    }
}

int portSettingsExtraCount(void) {
    return sCount;
}

const char* portSettingsExtraAt(int i, int* value) {
    if (i < 0 || i >= sCount) {
        return NULL;
    }
    *value = sExtra[i].savedAs ? sExtra[i].savedValue : sExtra[i].value;
    return sExtra[i].key;
}

int portSettingsExtraWritten(int i) {
    const Extra* e;
    if (i < 0 || i >= sCount) {
        return 0;
    }
    e = &sExtra[i];
    if (e->savedAs == 2) {
        return 0; /* (held for a session, and the player has no value of their own: left out, so their default) */
    }
    if (e->runOnly && !e->savedAs) {
        return 0; /* only an online host's: the player never had one (left at its default in the file) */
    }
    /* one read but not declared yet (declared on first use, or a later
     * release's) is kept as it was read */
    return !e->declared || (e->savedAs ? e->savedValue : e->value) != e->def;
}
