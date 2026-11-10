/* upgrade_test: a player's user folder from an earlier release, read by this one.
 *
 *   upgrade_test <tests/port/upgrade> <port/src/aurora/settings_aurora.c>
 *
 * Settings (settings.txt, as 0.1.0, 0.2.0, 0.3.0, 0.4.0 and 0.4.1 write it; fixtures from
 * make_fixtures.py): each is read the way settings_aurora.c reads it (its fixed fields,
 * listed from the source, then every other key through settings_extra.c) with this
 * release's settings (keys_current.txt) declared before or after, then written back as
 * portSettingsSave writes it. Checked: nothing aborts; no value of the old file changes or
 * is lost but wide_hud, turned on once (WIDE_HUD_EXEMPT, below) (a setting keeps its value, a
 * fixed field its text, or is left out at its default, which every release
 * reads as the same); keys no release knows are kept; no default of a 0.4.x setting has
 * changed (from 0.4.0 on a file leaves those out). And back: the file this release writes,
 * read by 0.3.0 (whose table held 256 feature settings and aborted past that), fits.
 *
 * Memory cards (<release>/card1.gci: files 1-3 saved by that release's headless build in
 * Rogueport, Petal Meadows and Glitzville, story 30, 40 and 165; 0.4.0's with blocks in the form its
 * Aurora build writes; 0.4.1's file 1 only, a new file saved at Rogueport's plaza Save Block
 * by its Aurora build: the testpass scenarios olddata_make_1 and _2): every slot's save
 * converts to the game's and back unchanged, a slot written before 0.4.0 reads as having no
 * Paper Cut block, and 0.4.0's and 0.4.1's blocks read. Each of these saves was also loaded,
 * played and saved again by 0.4.0, and 0.4.0's by 0.1.0, 0.2.0 and 0.3.0, when they were
 * made; 0.4.1's by 0.5.0 (olddata_load), and that one by 0.4.1 again (olddata_back). */
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

/* The exemption from "no value of the old file changes": wide_hud. 0.4.x wrote wide_hud=0 (its default then)
 * into every settings.txt, so a 0 there is almost never the player's choice; 0.5.0's default is on, and the
 * player approved turning it on once for everyone upgrading. A file without the mark (every release before
 * 0.5.0's) comes back with wide_hud=1 and the mark; a file with it keeps its wide_hud, 0 included. */
#define WIDE_HUD_MARK "wide_hud_050"
static int wideHudExempt(const char* key, const Keys* file) {
    const Key* mark = findKey(file, WIDE_HUD_MARK);
    return strcmp(key, "wide_hud") == 0 && (mark == NULL || atoi(mark->value) == 0);
}
#define WIDE_HUD_EXEMPT(key, file) wideHudExempt((key), (file))

/* Defaults 0.5.0 changed on purpose, so a 0.4.x file that left the setting out reads the new one (the
 * gameplay presets' change, which updated keys_current.txt with them): aniso 0 -> 4 (16x on a computer;
 * phones and tablets keep 0, port/include/port/display.h), second_player 0 -> 1 (on, but nothing changes
 * until a second controller joins, second_player.cpp). Any other default changed still fails. */
static int defaultChangedOn050(const char* key) {
    return strcmp(key, "aniso") == 0 || strcmp(key, "second_player") == 0;
}

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
    /* the one upgrade step that changes a value, as portSettingsLoad takes it after the file's lines: the
     * widescreen HUD on once for a file from before 0.5.0 (WIDE_HUD_EXEMPT) */
    if (portSettingsUpgradeOnce(WIDE_HUD_MARK, 1)) {
        for (i = 0; i < written->n; i++) {
            if (strcmp(written->k[i].key, "wide_hud") == 0) {
                snprintf(written->k[i].value, sizeof(written->k[i].value), "1");
            }
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
            if (w != NULL) {
                const char* want = WIDE_HUD_EXEMPT(old->key, file) ? "1" : old->value;
                CHECK(strcmp(w->value, want) == 0, "%s: fixed field %s=%s came back as %s (wanted %s)", release,
                      old->key, old->value, w->value, want);
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
    static const char* const kReleases[] = {"0.1.0", "0.2.0", "0.3.0", "0.4.0", "0.4.1"};
    char path[1024];
    int r, order;
    snprintf(path, sizeof(path), "%s/keys_current.txt", dir);
    CHECK(readKeyList(path, &sNow), "no %s", path);
    snprintf(path, sizeof(path), "%s/keys_0.3.0.txt", dir);
    CHECK(readKeyList(path, &s030), "no %s", path);
    for (r = 0; r < (int)(sizeof(kReleases) / sizeof(kReleases[0])); r++) {
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
    /* from 0.4.0 on, a setting at its default is left out of the file: one whose default this
     * release changed would come back changed (a feature turned on, or off, by the update alone) */
    for (r = 3; r < (int)(sizeof(kReleases) / sizeof(kReleases[0])); r++) {
        static Keys old;
        int j, changed = 0;
        snprintf(path, sizeof(path), "%s/keys_%s.txt", dir, kReleases[r]);
        CHECK(readKeyList(path, &old), "no %s", path);
        for (j = 0; j < old.n; j++) {
            const Key* now = findKey(&sNow, old.k[j].key);
            if (now != NULL && findKey(&sFixed, old.k[j].key) == NULL && now->def != old.k[j].def &&
                !defaultChangedOn050(old.k[j].key)) {
                CHECK(0, "%s: %s's default was %d, now %d: a file that left it out reads it changed", kReleases[r],
                      old.k[j].key, old.k[j].def, now->def);
                changed++;
            }
        }
        printf("settings %s: %d settings, %d defaults changed since\n", kReleases[r], old.n, changed);
    }
    /* the widescreen HUD's one-time step (WIDE_HUD_EXEMPT): a 0.4.x file comes back with it on and the
     * mark; a file of this release's, with the mark, keeps the player's choice (off stays off, on stays on) */
    {
        static Keys file, out;
        int value;
        for (value = 0; value <= 1; value++) {
            const Key* hud;
            const Key* mark;
            file.n = 0;
            snprintf(file.k[file.n].key, sizeof(file.k[0].key), "wide_hud");
            snprintf(file.k[file.n++].value, sizeof(file.k[0].value), "%d", value);
            loadAndSave(&file, 1, &out);
            hud = findKey(&out, "wide_hud");
            mark = findKey(&out, WIDE_HUD_MARK);
            CHECK(hud != NULL && strcmp(hud->value, "1") == 0, "wide_hud=%d from before 0.5.0 came back as %s",
                  value, hud != NULL ? hud->value : "nothing");
            CHECK(mark != NULL && strcmp(mark->value, "1") == 0, "the %s mark wasn't written", WIDE_HUD_MARK);
            snprintf(file.k[file.n].key, sizeof(file.k[0].key), WIDE_HUD_MARK);
            snprintf(file.k[file.n++].value, sizeof(file.k[0].value), "1");
            loadAndSave(&file, 0, &out);
            hud = findKey(&out, "wide_hud");
            CHECK(hud != NULL && atoi(hud->value) == value, "wide_hud=%d with the mark came back as %s", value,
                  hud != NULL ? hud->value : "nothing");
        }
        printf("settings: wide_hud on once for a file from before 0.5.0, the player's choice kept after\n");
    }
    /* the guard itself: what 0.4.0 first wrote (every declared setting) is what aborted 0.3.0 */
    {
        int j;
        sWritten.n = 0;
        for (j = 0; j < sNow.n; j++) {
            Key* w = &sWritten.k[sWritten.n++];
            *w = sNow.k[j];
        }
        CHECK(countIn030(&sWritten) > 256, "the 0.3.0 check can't tell: every key of this release gives only %d",
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
            /* (a file never played: the game's own empty file, its flags word 1; no block) */
            empty = empty || (at[0] == 0x00 && at[1] == 0x01);
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
    static const char* const kReleases[] = {"0.1.0", "0.2.0", "0.3.0", "0.4.0", "0.4.1"};
    int r, i;
    for (r = 0; r < (int)(sizeof(kReleases) / sizeof(kReleases[0])); r++) {
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
