/* upgrade_test: a player's user folder from an earlier release, read by this one.
 *
 *   upgrade_test <tests/port/upgrade> <port/src/aurora/settings_aurora.c>
 *
 * Settings (settings.txt, as 0.1.0, 0.2.0, 0.3.0 and 0.4.0 write it; fixtures from
 * make_fixtures.py): each is read the way settings_aurora.c reads it (its fixed fields,
 * listed from the source, then every other key through settings_extra.c) with this
 * release's settings declared before or after, then written back as portSettingsSave
 * writes it. Checked: nothing aborts; no value of the old file changes or is lost (a
 * setting keeps its value, or is left out at its default, which every release reads as
 * the same); keys no release knows are kept. And back: the file this release writes,
 * read by 0.3.0 (whose table held 256 feature settings and aborted past that), fits.
 *
 * Memory cards (<release>/card1.gci: files 1-3 saved by that release's headless build in
 * Rogueport, Petal Meadows and Glitzville, story 30, 40 and 165; 0.4.0's with blocks in the form its
 * Aurora build writes): every slot's save converts to the game's and back unchanged, a
 * slot written before 0.4.0 reads as having no Paper Cut block, and 0.4.0's blocks read.
 * Each of these saves was also loaded, played and saved again by 0.4.0, and 0.4.0's by
 * 0.1.0, 0.2.0 and 0.3.0, when they were made. */
#include "../../../port/src/common/settings_extra.c" /* its tables, reset per case */
#include "port/cardsave_pc.h"
#include "manager/cardmgr.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sFailures;
#define CHECK(c, ...)                                                                                      \
    do {                                                                                                   \
        if (!(c)) {                                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
            printf(__VA_ARGS__);                                                                           \
            printf("\n");                                                                                  \
            sFailures++;                                                                                   \
        }                                                                                                  \
    } while (0)

/* ---------------------------------------------------------------- key lists */

#define MAX_KEYS 4096
typedef struct {
    char key[64], value[32];
    int def, min, max;
} Key;
typedef struct {
    Key k[MAX_KEYS];
    int n;
} Keys;

static const Key* findKey(const Keys* ks, const char* key) {
    int i;
    for (i = 0; i < ks->n; i++) {
        if (strcmp(ks->k[i].key, key) == 0) {
            return &ks->k[i];
        }
    }
    return NULL;
}

static char* slurp(const char* path, long* size) {
    FILE* f = fopen(path, "rb");
    char* text;
    long n;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char*)malloc((size_t)n + 1);
    if (fread(text, 1, (size_t)n, f) != (size_t)n) {
        n = 0;
    }
    text[n] = '\0';
    fclose(f);
    if (size != NULL) {
        *size = n;
    }
    return text;
}

/* "key def min max" lines (keys_<release>.txt) */
static int readKeyList(const char* path, Keys* out) {
    FILE* f = fopen(path, "r");
    char line[256];
    out->n = 0;
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL && out->n < MAX_KEYS) {
        Key* k = &out->k[out->n];
        if (sscanf(line, "%63s %d %d %d", k->key, &k->def, &k->min, &k->max) == 4) {
            out->n++;
        }
    }
    fclose(f);
    return 1;
}

/* key=value lines (settings.txt) */
static int readSettings(const char* path, Keys* out) {
    char* text = slurp(path, NULL);
    char* line;
    out->n = 0;
    if (text == NULL) {
        return 0;
    }
    for (line = strtok(text, "\r\n"); line != NULL && out->n < MAX_KEYS; line = strtok(NULL, "\r\n")) {
        char* eq = strchr(line, '=');
        if (eq != NULL) {
            Key* k = &out->k[out->n++];
            *eq = '\0';
            snprintf(k->key, sizeof(k->key), "%s", line);
            snprintf(k->value, sizeof(k->value), "%s", eq + 1);
        }
    }
    free(text);
    return 1;
}

/* settings_aurora.c's fixed fields: the {"key", &sSettings.x, ...} rows of kFields */
static void readFixed(const char* source, Keys* out) {
    char* text = slurp(source, NULL);
    const char* at;
    const char* tableEnd;
    out->n = 0;
    if (text == NULL) {
        return;
    }
    at = strstr(text, "kFields[] = {");
    tableEnd = at != NULL ? strstr(at, "\n};") : NULL;
    while (tableEnd != NULL && (at = strstr(at, "{\"")) != NULL && at < tableEnd && out->n < MAX_KEYS) {
        const char* end = strchr(at + 2, '"');
        Key* k = &out->k[out->n++];
        snprintf(k->key, sizeof(k->key), "%.*s", (int)(end - at - 2), at + 2);
        /* the bounds after the value's pointer (the frame rate: NULL, a string) */
        if (sscanf(end + 1, ", %*[^,], %d, %d", &k->min, &k->max) != 2) {
            k->min = k->max = 0;
        }
        at = end;
    }
    free(text);
}

/* ------------------------------------------------------------- settings */

static void resetExtras(void) {
    memset(sExtra, 0, sizeof(sExtra));
    sCount = 0;
}

static Keys sFixed, sNow, sOld, sWritten, s030;

/* the file read as portSettingsLoad reads it, this release's settings declared before
 * (declareFirst: at start-up) or after (on first use), then written as portSettingsSave
 * writes it: the fixed fields, then the feature settings that differ from their defaults */
static void loadAndSave(const Keys* file, int declareFirst, Keys* written) {
    int i, j;
    resetExtras();
    for (j = 0; declareFirst && j < sNow.n; j++) {
        if (findKey(&sFixed, sNow.k[j].key) == NULL) {
            portSettingsDeclare(sNow.k[j].key, sNow.k[j].def, sNow.k[j].min, sNow.k[j].max);
        }
    }
    written->n = 0;
    for (i = 0; i < file->n; i++) {
        if (findKey(&sFixed, file->k[i].key) != NULL) {
            Key* w = &written->k[written->n++]; /* (clamped to the field's range: checked below) */
            *w = file->k[i];
        } else {
            portSettingsExtraSet(file->k[i].key, file->k[i].value);
        }
    }
    for (j = 0; !declareFirst && j < sNow.n; j++) {
        if (findKey(&sFixed, sNow.k[j].key) == NULL) {
            portSettingsDeclare(sNow.k[j].key, sNow.k[j].def, sNow.k[j].min, sNow.k[j].max);
        }
    }
    for (i = 0; i < portSettingsExtraCount(); i++) {
        int value;
        const char* key = portSettingsExtraAt(i, &value);
        if (portSettingsExtraWritten(i)) {
            Key* w = &written->k[written->n++];
            snprintf(w->key, sizeof(w->key), "%s", key);
            snprintf(w->value, sizeof(w->value), "%d", value);
        }
    }
}

static int isNumber(const char* s) {
    if (*s == '-') {
        s++;
    }
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
    }
    return 1;
}

/* every value of the old file comes through: the same, or left out at the default */
static void checkKept(const char* release, const Keys* file, const Keys* written, int* migrated, int* kept) {
    int i;
    for (i = 0; i < file->n; i++) {
        const Key* old = &file->k[i];
        const Key* now = findKey(&sNow, old->key);
        const Key* w = findKey(written, old->key);
        const Key* fixed = findKey(&sFixed, old->key);
        if (fixed != NULL) {
            CHECK(w != NULL, "%s: fixed field %s not written back", release, old->key);
            if (isNumber(old->value) && !(fixed->min == 0 && fixed->max == 0)) {
                const int v = atoi(old->value);
                CHECK(v >= fixed->min && v <= fixed->max, "%s: %s=%d is outside this release's %d..%d", release,
                      old->key, v, fixed->min, fixed->max);
            }
            (*migrated)++;
            continue;
        }
        if (now == NULL) {
            /* a key this release doesn't declare: kept as it was */
            CHECK(w != NULL && strcmp(w->value, old->value) == 0, "%s: unknown key %s=%s lost (got %s)", release,
                  old->key, old->value, w != NULL ? w->value : "nothing");
            (*kept)++;
            continue;
        }
        {
            const int v = atoi(old->value);
            CHECK(v >= now->min && v <= now->max, "%s: %s=%d is outside this release's %d..%d (it would change)",
                  release, old->key, v, now->min, now->max);
            if (w != NULL) {
                CHECK(atoi(w->value) == v, "%s: %s=%d came back as %s", release, old->key, v, w->value);
            } else {
                CHECK(v == now->def, "%s: %s=%d was left out but isn't the default (%d)", release, old->key, v,
                      now->def);
            }
            (*migrated)++;
        }
    }
}

/* 0.3.0's settings_extra.c held 256 keys (its own declared ones and every other key
 * read) and aborted past that: the file written here, read by 0.3.0, must fit */
static int countIn030(const Keys* written) {
    int i, n = 0;
    for (i = 0; i < s030.n; i++) {
        if (findKey(&sFixed, s030.k[i].key) == NULL) {
            n++; /* declared by 0.3.0 */
        }
    }
    for (i = 0; i < written->n; i++) {
        if (findKey(&sFixed, written->k[i].key) == NULL && findKey(&s030, written->k[i].key) == NULL) {
            n++;
        }
    }
    return n;
}

static void testSettings(const char* dir) {
    static const char* const kReleases[] = {"0.1.0", "0.2.0", "0.3.0", "0.4.0"};
    char path[1024];
    int r, order;
    snprintf(path, sizeof(path), "%s/keys_0.4.0.txt", dir);
    CHECK(readKeyList(path, &sNow), "no %s", path);
    snprintf(path, sizeof(path), "%s/keys_0.3.0.txt", dir);
    CHECK(readKeyList(path, &s030), "no %s", path);
    for (r = 0; r < 4; r++) {
        snprintf(path, sizeof(path), "%s/%s/settings.txt", dir, kReleases[r]);
        CHECK(readSettings(path, &sOld), "no %s", path);
        for (order = 0; order < 2; order++) {
            int migrated = 0, kept = 0, in030;
            loadAndSave(&sOld, order, &sWritten);
            checkKept(kReleases[r], &sOld, &sWritten, &migrated, &kept);
            /* and the file written read again: the same */
            {
                static Keys again;
                int i;
                loadAndSave(&sWritten, !order, &again);
                CHECK(again.n == sWritten.n, "%s: written again, %d lines instead of %d", kReleases[r], again.n,
                      sWritten.n);
                for (i = 0; i < sWritten.n; i++) {
                    const Key* a = findKey(&again, sWritten.k[i].key);
                    CHECK(a != NULL && strcmp(a->value, sWritten.k[i].value) == 0, "%s: %s changed on a second save",
                          kReleases[r], sWritten.k[i].key);
                }
            }
            in030 = countIn030(&sWritten);
            CHECK(in030 <= 256, "%s: 0.3.0 would read %d feature settings from this release's file (max 256)",
                  kReleases[r], in030);
            if (order == 0) {
                printf("settings %s: %d lines read, %d settings carried over, %d unknown keys kept, %d lines "
                       "written (0.3.0 would hold %d of 256)\n",
                       kReleases[r], sOld.n, migrated, kept, sWritten.n, in030);
            }
        }
    }
    /* the guard itself: what 0.4.0 first wrote (every declared setting) is what aborted 0.3.0 */
    {
        int j;
        sWritten.n = 0;
        for (j = 0; j < sNow.n; j++) {
            Key* w = &sWritten.k[sWritten.n++];
            *w = sNow.k[j];
        }
        CHECK(countIn030(&sWritten) > 256, "the 0.3.0 check can't tell: every 0.4.0 key gives only %d",
              countIn030(&sWritten));
    }
    /* odd lines: none fatal (no '=', an empty key, a long key, a value that isn't a number) */
    {
        char longKey[200];
        memset(longKey, 'k', sizeof(longKey) - 1);
        longKey[sizeof(longKey) - 1] = '\0';
        resetExtras();
        CHECK(portSettingsExtraSet(longKey, "1") == 0, "a key too long to keep is kept");
        CHECK(portSettingsExtraSet("", "1") == 1, "an empty key");
        CHECK(portSettingsExtraSet("framerate_text", "display") == 1, "a word for a value");
    }
}

/* ------------------------------------------------------------- memory cards */

#define GCI_HEADER 0x40

static void testCard(const char* release, const char* path) {
    long size = 0;
    uint8_t* gci = (uint8_t*)slurp(path, &size);
    CardSaveSlot* native = (CardSaveSlot*)calloc(1, sizeof(CardSaveSlot));
    uint8_t* image = (uint8_t*)calloc(1, PORT_CARD_SLOT_SIZE);
    int slot, saves = 0;
    if (gci == NULL) {
        CHECK(0, "%s: can't read %s", release, path);
        return;
    }
    CHECK(size == GCI_HEADER + PORT_CARD_META_SIZE + 8 * PORT_CARD_SLOT_SIZE, "%s: %s is %ld bytes", release, path,
          size);
    if (size >= (long)(GCI_HEADER + PORT_CARD_META_SIZE + 8 * PORT_CARD_SLOT_SIZE)) {
        for (slot = 0; slot < 8; slot++) {
            const uint8_t* at = gci + GCI_HEADER + PORT_CARD_META_SIZE + slot * PORT_CARD_SLOT_SIZE;
            const char* extra = portCardImageExtra(at);
            int empty = 1, i;
            for (i = 0; i < 0x40 && empty; i++) {
                empty = at[i] == 0;
            }
            if (strcmp(release, "0.4.0") < 0) {
                CHECK(extra == NULL, "%s: slot %d has a Paper Cut block before 0.4.0", release, slot);
            } else if (!empty) {
                /* the block as save_files.cpp writes it: its id, who was played, its settings */
                CHECK(extra != NULL && strstr(extra, "\nid=") != NULL && strstr(extra, "\nchar=") != NULL,
                      "%s: slot %d's Paper Cut block can't be read", release, slot);
            }
            if (portCardSlotFromImage(at, native)) {
                CHECK(portCardSlotToImage(native, image), "%s: slot %d doesn't convert back", release, slot);
                CHECK(memcmp(image, at, PORT_CARD_GC_DATA_SIZE) == 0 &&
                          memcmp(image + 0x3FF0, at + 0x3FF0, 0x10) == 0,
                      "%s: slot %d changed on the way through", release, slot);
                saves++;
            } else {
                CHECK(empty, "%s: slot %d holds something that isn't a valid save", release, slot);
            }
        }
    }
    printf("card %s: %s, %d valid saves (main and backup)\n", release, path, saves);
    CHECK(saves > 0, "%s: no save in %s", release, path);
    free(gci);
    free(native);
    free(image);
}

static void testCards(const char* dir) {
    static const char* const kReleases[] = {"0.1.0", "0.2.0", "0.3.0", "0.4.0"};
    int r, i;
    for (r = 0; r < 4; r++) {
        for (i = 1; i <= 9; i++) {
            char path[1024];
            FILE* f;
            snprintf(path, sizeof(path), "%s/%s/card%d.gci", dir, kReleases[r], i);
            if ((f = fopen(path, "rb")) == NULL) {
                continue;
            }
            fclose(f);
            testCard(kReleases[r], path);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: upgrade_test <tests/port/upgrade> <port/src/aurora/settings_aurora.c>\n");
        return 2;
    }
    readFixed(argv[2], &sFixed);
    CHECK(sFixed.n >= 30, "only %d fixed fields found in %s", sFixed.n, argv[2]);
    testSettings(argv[1]);
    testCards(argv[1]);
    if (sFailures != 0) {
        printf("upgrade_test: %d failures\n", sFailures);
        return 1;
    }
    printf("upgrade_test: ok\n");
    return 0;
}
