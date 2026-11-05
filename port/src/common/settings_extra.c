/* Settings a feature declares from its own file (port/include/port/settings.h:
 * portSettingsDeclare), kept by key beside PortSettings' fixed fields, so
 * features added side by side don't all edit one struct and one table. The
 * Aurora build reads and writes them in settings.txt with the others
 * (settings_aurora.c); any build takes $TTYD_SET_<key> for a run (tests). A
 * value read before its declaration waits, unclamped, until it comes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/settings.h"

/* pointers to the values are handed out, so the table never moves: room for
 * every feature's settings and many more */
#define EXTRA_MAX 2048

typedef struct Extra {
    char key[48];
    int value, min, max, def;
    int declared;
    int toDefault; /* at its default once declared (portSettingsExtraToDefault) */
    int savedAs, savedValue; /* settings.txt gets savedValue meanwhile (portSettingsExtraSaveAs) */
} Extra;

static Extra sExtra[EXTRA_MAX];
static int sCount;

static Extra* find(const char* key) {
    int i;
    for (i = 0; i < sCount; i++) {
        if (strcmp(sExtra[i].key, key) == 0) {
            return &sExtra[i];
        }
    }
    return NULL;
}

static Extra* add(const char* key) {
    Extra* e;
    if (sCount == EXTRA_MAX || strlen(key) >= sizeof(e->key)) {
        fprintf(stderr, "settings: no room for %s\n", key);
        abort();
    }
    e = &sExtra[sCount++];
    memset(e, 0, sizeof(*e));
    snprintf(e->key, sizeof(e->key), "%s", key);
    return e;
}

/* a key read from a file or sent by another game (settings.txt, the online
 * host's, a replay's): one that can't be kept is left out, not fatal */
static Extra* addRead(const char* key) {
    if (sCount == EXTRA_MAX || strlen(key) >= sizeof(sExtra[0].key)) {
        fprintf(stderr, "settings: %s left out (no room)\n", key);
        return NULL;
    }
    return add(key);
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
    }
    e->min = min;
    e->max = max;
    e->def = def;
    e->declared = 1;
    if (e->toDefault) {
        e->value = def; /* (this run's set doesn't have it: online's guest, a playback) */
        e->toDefault = 0;
    }
    snprintf(env, sizeof(env), "TTYD_SET_%s", key);
    if ((over = getenv(env)) != NULL) {
        e->value = atoi(over);
    }
    e->value = clamp(e, e->value);
    return &e->value;
}

int* portSettingsFind(const char* key) {
    Extra* e = find(key);
    return e != NULL && e->declared ? &e->value : NULL;
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
    if (e == NULL && (e = addRead(key)) == NULL) {
        return 0;
    }
    snprintf(env, sizeof(env), "TTYD_SET_%s", key);
    e->toDefault = 0; /* (a value set since: it wins) */
    if (getenv(env) == NULL) { /* the run's override stays */
        e->value = atoi(value);
        if (e->declared) {
            e->value = clamp(e, e->value);
        }
    }
    return 1;
}

/* a value for this run, over the run's own override too (online co-op's
 * guest: the host's settings, port/settings/online_lobby.cpp) */
void portSettingsExtraForce(const char* key, const char* value) {
    Extra* e = find(key);
    if (e == NULL && (e = addRead(key)) == NULL) {
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
    } else {
        e->toDefault = 1;
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
