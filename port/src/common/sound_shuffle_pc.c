/* The randomizer's music and sound shuffle, and the sound editor (port/include/port/sound_shuffle.h). */
#include "port/sound_shuffle.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BGM 0x200
#define MAX_SFX 0x2000

static short sBgm[MAX_BGM]; /* index -> index, 0: as it is */
static short sSfx[MAX_SFX];
static short sEditBgm[MAX_BGM]; /* the editor's, which win */
static short sEditSfx[MAX_SFX];

static unsigned long long sState;

static unsigned next(void) {
    /* xorshift64*: its own numbers, never the game's */
    sState ^= sState >> 12;
    sState ^= sState << 25;
    sState ^= sState >> 27;
    return (unsigned)((sState * 0x2545F4914F6CDD1DULL) >> 32);
}

/* a family's members in table order, shuffled among themselves into map */
static void shuffleFamily(short* map, const int* members, int n) {
    int order[MAX_SFX];
    int k;
    if (n < 2) {
        return;
    }
    for (k = 0; k < n; k++) {
        order[k] = members[k];
    }
    for (k = n - 1; k > 0; k--) {
        const int j = (int)(next() % (unsigned)(k + 1));
        const int t = order[k];
        order[k] = order[j];
        order[j] = t;
    }
    for (k = 0; k < n; k++) {
        map[members[k]] = (short)order[k];
    }
}

static int prefixed(const char* name, const char* prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

static int bgmClass(const char* name) {
    if (name[0] == '\0') {
        return 0;
    }
    if (prefixed(name, "BGM_STG")) {
        return 1; /* the areas' themes */
    }
    if (prefixed(name, "BGM_BATTLE") || prefixed(name, "BGM_BOSS")) {
        return 2; /* battles and bosses */
    }
    return 0;
}

/* the family of a sound: its name's first two words (SFX_ENM_...), or 0 where it stays */
static int sameFamily(const char* a, const char* b) {
    const char* ea = strchr(a + 4, '_');
    const char* eb = strchr(b + 4, '_');
    const size_t la = ea != NULL ? (size_t)(ea - a) : strlen(a);
    const size_t lb = eb != NULL ? (size_t)(eb - b) : strlen(b);
    return la == lb && strncmp(a, b, la) == 0;
}

static int sfxShuffled(const char* name) {
    if (name[0] == '\0' || !prefixed(name, "SFX_")) {
        return 0;
    }
    /* ambient loops, the menus' sounds and the silences stay */
    return !prefixed(name, "SFX_AMB") && !prefixed(name, "SFX_OFF") && !prefixed(name, "SFX_CURSOR") &&
           !prefixed(name, "SFX_SYS") && !prefixed(name, "SFX_MENU") && strstr(name, "LOOP") == NULL;
}

void portSoundShuffleSet(unsigned long long seed, int music, int sounds) {
    static int members[MAX_SFX];
    static char done[MAX_SFX];
    int i, n;
    memset(sBgm, 0, sizeof(sBgm));
    memset(sSfx, 0, sizeof(sSfx));
    if (seed == 0 || (!music && !sounds)) {
        return;
    }
    sState = seed ^ 0x9E3779B97F4A7C15ULL;
    if (sState == 0) {
        sState = 1;
    }
    if (music) {
        int cls;
        const int count = portPsndBgmCount() < MAX_BGM ? portPsndBgmCount() : MAX_BGM;
        for (cls = 1; cls <= 2; cls++) {
            n = 0;
            for (i = 0; i < count; i++) {
                if (bgmClass(portPsndBgmName(i)) == cls) {
                    members[n++] = i;
                }
            }
            shuffleFamily(sBgm, members, n);
        }
    }
    if (sounds) {
        const int count = portPsndSfxCount() < MAX_SFX ? portPsndSfxCount() : MAX_SFX;
        memset(done, 0, sizeof(done));
        for (i = 0; i < count; i++) {
            const char* name = portPsndSfxName(i);
            int j;
            if (done[i] || !sfxShuffled(name)) {
                continue;
            }
            n = 0;
            for (j = i; j < count; j++) {
                const char* other = portPsndSfxName(j);
                if (!done[j] && sfxShuffled(other) && sameFamily(name, other)) {
                    members[n++] = j;
                    done[j] = 1;
                }
            }
            shuffleFamily(sSfx, members, n);
        }
    }
}

void portSoundEditorClear(void) {
    memset(sEditBgm, 0, sizeof(sEditBgm));
    memset(sEditSfx, 0, sizeof(sEditSfx));
}

static int findName(int sfx, const char* name) {
    const int count = sfx ? portPsndSfxCount() : portPsndBgmCount();
    int i;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 1; i < count; i++) {
        if (strcmp(sfx ? portPsndSfxName(i) : portPsndBgmName(i), name) == 0) {
            return i;
        }
    }
    return 0;
}

void portSoundEditorSet(int sfx, const char* from, const char* to) {
    const int a = findName(sfx, from), b = findName(sfx, to);
    if (a <= 0) {
        return;
    }
    if (sfx) {
        if (a < MAX_SFX) {
            sEditSfx[a] = (short)b;
        }
    } else if (a < MAX_BGM) {
        sEditBgm[a] = (short)b;
    }
}

int portSoundRemapBgm(int index) {
    if (index <= 0 || index >= MAX_BGM) {
        return index;
    }
    if (sEditBgm[index] > 0) {
        return sEditBgm[index];
    }
    return sBgm[index] > 0 ? sBgm[index] : index;
}

int portSoundRemapSfx(int id) {
    const int sid = id & 0x1FFF;
    int to;
    if (sid <= 0 || sid >= MAX_SFX) {
        return id;
    }
    to = sEditSfx[sid] > 0 ? sEditSfx[sid] : sSfx[sid];
    return to > 0 ? (id & ~0x1FFF) | to : id;
}
