/* Test checkpoints (port/include/port/checkpoint.h): a run's game saved to
 * a file at a room change or a labelled step, and a later run started from
 * it. The file: a text header (key=value lines, then "--"), then the save
 * image (PORT_CARD_SLOT_SIZE bytes) and, for a chapter played from the inn,
 * Mario's game to go back to (as many again).
 *
 * A room change's checkpoint is made at the start of the frame the map change
 * begins (before seqMain leaves the old room), with the room and entrance it
 * goes to; a resume loads the save (as a continue does) and enters that room
 * by that entrance, with the clock and the random numbers as they were, so
 * the room comes up as it did in the run that saved it. */
#include "port/checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <dolphin/os.h>
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/platform.h"

BOOL portCardSnapshot(void* image); /* cardmgr.c */
BOOL portCardLoadSlotImage(s32 slot, const void* image);
void* portCardSlotPtr(s32 slot, u32* size);
void portContinueGame(s32 slot); /* seq_load.c */

#define MAGIC "TTYD checkpoint 1\n"
#define HEADER_MAX 8192

/* The headless build has neither Super Luigi nor $TTYD_FAST's clock functions */
__attribute__((weak)) void portSlCheckpointGet(PortSlCheckpoint* out) {
    out->mode = 0;
    out->haveReturn = 0;
}

/* a continue from the image, as file SLOT; the card's own copy of that file is left as it was */
__attribute__((weak)) void portSlCheckpointResume(const PortSlCheckpoint* ck, const uint8_t* image, int slot) {
    static uint8_t keep[0x8000];
    u32 size = 0;
    void* live = portCardSlotPtr(slot, &size);
    const int kept = live != NULL && size <= sizeof(keep);
    (void)ck;
    if (kept) {
        memcpy(keep, live, size);
    }
    if (portCardLoadSlotImage(slot, image)) {
        portContinueGame(slot);
    }
    if (kept) {
        memcpy(live, keep, size);
    }
}

__attribute__((weak)) s64 portFastClockGet(void) {
    return 0;
}

__attribute__((weak)) void portFastClockSet(s64 t) {
    (void)t;
}

/* the checkpoint being resumed ($TTYD_CHECKPOINT_LOAD) */
static struct {
    int state; /* 0 not read, 1 none, 2 loaded (waiting), 3 resumed */
    int inPlace;
    char map[32], bero[32];
    int slot, fadeIn, fadeOut;
    u32 rand;
    s64 clock;
    PortSlCheckpoint sl;
    uint8_t image[PORT_CARD_SLOT_SIZE];
    uint8_t ret[PORT_CARD_SLOT_SIZE];
    char text[HEADER_MAX];
} sCk;

const char* portCheckpointValue(const char* lines, const char* key, char* out, size_t size) {
    const size_t n = strlen(key);
    const char* p = lines;
    while (p != NULL && *p) {
        if (strncmp(p, key, n) == 0 && p[n] == '=') {
            const char* v = p + n + 1;
            const size_t len = strcspn(v, "\n");
            snprintf(out, size, "%.*s", (int)len, v);
            return out;
        }
        p = strchr(p, '\n');
        p = p != NULL ? p + 1 : NULL;
    }
    return NULL;
}

int portCheckpointSave(const char* name, const char* map, const char* bero, const char* driver) {
    static uint8_t image[PORT_CARD_SLOT_SIZE];
    static uint8_t ret[PORT_CARD_SLOT_SIZE];
    const char* dir = getenv("TTYD_CHECKPOINT_DIR");
    PortSlCheckpoint sl;
    char path[1024];
    FILE* f;
    if (dir == NULL || *dir == '\0' || gp == NULL) {
        printf("[checkpoint] %s: not saved ($TTYD_CHECKPOINT_DIR not set)\n", name);
        fflush(stdout);
        return 0;
    }
    if (!portCardSnapshot(image)) {
        printf("[checkpoint] %s: not saved (no save image)\n", name);
        fflush(stdout);
        return 0;
    }
    memset(&sl, 0, sizeof(sl));
    sl.returnImage = ret;
    portSlCheckpointGet(&sl);
#if defined(_WIN32)
    _mkdir(dir);
#else
    mkdir(dir, 0777);
#endif
    snprintf(path, sizeof(path), "%s/%s.ckpt", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) {
        printf("[checkpoint] %s: cannot write %s\n", name, path);
        fflush(stdout);
        return 0;
    }
    fputs(MAGIC, f);
    fprintf(f, "game=%s\nname=%s\nkind=%s\nfrom_map=%s\nmap=%s\nbero=%s\nslot=%d\n", PORT_GAME_ID, name,
            map != NULL ? "room" : "here", gp->currentMapName, map != NULL ? map : gp->currentMapName,
            bero != NULL ? bero : "", (int)gp->saveSlot);
    fprintf(f, "story=%d\nrand=%u\nclock=%lld\nfade_in=%d\nfade_out=%d\n", (int)swByteGet(0),
            (unsigned)portRandState(), (long long)portFastClockGet(), (int)gp->areaFadeInType, (int)gp->areaFadeOutType);
    fprintf(f, "sl_mode=%d\nsl_chapter=%d\nsl_return=%d\nsl_return_slot=%d\nsl_kept_map=%s\nsl_kept_pos=%f %f %f\n",
            sl.mode, sl.chapter, sl.haveReturn, sl.returnSlot, sl.keptMap, sl.keptPos[0], sl.keptPos[1], sl.keptPos[2]);
    fputs(driver != NULL ? driver : "", f);
    fputs("--\n", f);
    fwrite(image, 1, sizeof(image), f);
    if (sl.haveReturn) {
        fwrite(ret, 1, sizeof(ret), f);
    }
    fclose(f);
    printf("[checkpoint] saved %s: %s, entering %s%s%s, story %d\n", path, gp->currentMapName,
           map != NULL ? map : "(here)", bero != NULL && *bero ? " by " : "", bero != NULL ? bero : "",
           (int)swByteGet(0));
    fflush(stdout);
    return 1;
}

static void loadFile(void) {
    const char* path = getenv("TTYD_CHECKPOINT_LOAD");
    char v[64];
    FILE* f;
    long size;
    char* data;
    char* end;
    size_t hdr;
    sCk.state = 1;
    if (path == NULL || *path == '\0') {
        return;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        printf("[checkpoint] FAIL: cannot open %s\n", path);
        fflush(stdout);
        _Exit(2);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (char*)malloc((size_t)size + 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        printf("[checkpoint] FAIL: cannot read %s\n", path);
        fflush(stdout);
        _Exit(2);
    }
    fclose(f);
    data[size] = '\0';
    end = strstr(data, "\n--\n");
    if (strncmp(data, MAGIC, strlen(MAGIC)) != 0 || end == NULL) {
        printf("[checkpoint] FAIL: %s is not a checkpoint\n", path);
        fflush(stdout);
        _Exit(2);
    }
    hdr = (size_t)(end - data) + 4;
    snprintf(sCk.text, sizeof(sCk.text), "%.*s", (int)(end + 1 - data), data);
    if (portCheckpointValue(sCk.text, "game", v, sizeof(v)) == NULL || strcmp(v, PORT_GAME_ID) != 0) {
        printf("[checkpoint] FAIL: %s is from game %s, this is %s\n", path, v, PORT_GAME_ID);
        fflush(stdout);
        _Exit(2);
    }
    sCk.sl.returnImage = sCk.ret;
    if ((size_t)size < hdr + PORT_CARD_SLOT_SIZE) {
        printf("[checkpoint] FAIL: %s is cut short\n", path);
        fflush(stdout);
        _Exit(2);
    }
    memcpy(sCk.image, data + hdr, PORT_CARD_SLOT_SIZE);
#define NUM(key, def) (portCheckpointValue(sCk.text, key, v, sizeof(v)) != NULL ? strtoll(v, NULL, 10) : (def))
    sCk.sl.haveReturn = (int)NUM("sl_return", 0);
    if (sCk.sl.haveReturn) {
        if ((size_t)size < hdr + 2 * PORT_CARD_SLOT_SIZE) {
            printf("[checkpoint] FAIL: %s is cut short\n", path);
            fflush(stdout);
            _Exit(2);
        }
        memcpy(sCk.ret, data + hdr + PORT_CARD_SLOT_SIZE, PORT_CARD_SLOT_SIZE);
    }
    free(data);
    sCk.inPlace = portCheckpointValue(sCk.text, "kind", v, sizeof(v)) != NULL && strcmp(v, "here") == 0;
    portCheckpointValue(sCk.text, "map", sCk.map, sizeof(sCk.map));
    portCheckpointValue(sCk.text, "bero", sCk.bero, sizeof(sCk.bero));
    sCk.slot = (int)NUM("slot", 0);
    sCk.rand = (u32)NUM("rand", 0);
    sCk.clock = (s64)NUM("clock", 0);
    sCk.fadeIn = (int)NUM("fade_in", 0);
    sCk.fadeOut = (int)NUM("fade_out", 0);
    sCk.sl.mode = (int)NUM("sl_mode", 0);
    sCk.sl.chapter = (int)NUM("sl_chapter", 0);
    sCk.sl.returnSlot = (int)NUM("sl_return_slot", 0);
    portCheckpointValue(sCk.text, "sl_kept_map", sCk.sl.keptMap, sizeof(sCk.sl.keptMap));
    if (portCheckpointValue(sCk.text, "sl_kept_pos", v, sizeof(v)) != NULL) {
        sscanf(v, "%f %f %f", &sCk.sl.keptPos[0], &sCk.sl.keptPos[1], &sCk.sl.keptPos[2]);
    }
#undef NUM
    if (sCk.slot < 0 || sCk.slot > 3) {
        sCk.slot = 0;
    }
    printf("[checkpoint] loading %s: %s %s%s%s\n", path, sCk.inPlace ? "here in" : "entering", sCk.map,
           sCk.bero[0] ? " by " : "", sCk.bero);
    fflush(stdout);
    sCk.state = 2;
}

const char* portCheckpointPending(int* inPlace) {
    if (sCk.state == 0) {
        loadFile();
    }
    if (inPlace != NULL) {
        *inPlace = sCk.inPlace;
    }
    return sCk.state >= 2 ? sCk.text : NULL;
}

int portCheckpointResumeFrame(void) {
    static int sFrames;
    s32 seq;
    if (sCk.state == 0) {
        loadFile();
    }
    if (sCk.state != 2) {
        return 0;
    }
    seq = seqGetSeq();
    /* the title (or the field) up and settled, nothing else on its way */
    if (gp == NULL || (seq != SEQ_TITLE && seq != SEQ_GAME) || gp->inBattle || seqCheckSeq()) {
        sFrames = 0;
        return 0;
    }
    if (++sFrames < 120) {
        return 0;
    }
    portSlCheckpointResume(&sCk.sl, sCk.image, sCk.slot);
    if (!sCk.inPlace) {
        /* into the room by the entrance the run took, not at the save's spot */
        gp->flags &= ~GLOBAL_FLAG_LOADED;
        gp->areaFadeInType = (FadeType)sCk.fadeIn;
        gp->areaFadeOutType = (FadeType)sCk.fadeOut;
        seqSetSeq(SEQ_MAP_CHANGE, sCk.map, sCk.bero[0] ? sCk.bero : NULL);
    }
    portRandSetState(sCk.rand);
    portFastClockSet(sCk.clock);
    sCk.state = 3;
    printf("[checkpoint] resumed: %s %s%s%s\n", sCk.inPlace ? "loaded in" : "entering", sCk.map,
           sCk.bero[0] ? " by " : "", sCk.bero);
    fflush(stdout);
    return 1;
}
