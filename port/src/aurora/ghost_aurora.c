/* Ghost races (port/include/port/playmodes.h, docs/REPLAYS.md): a recording
 * also keeps where Mario was, every other frame (the room, his place, his
 * facing, his animation), in the replay file after its input. Racing a
 * replay starts the game again from the replay's start, as a playback does,
 * but with the player in control; the replay's Mario is drawn translucent in
 * the same room at the same time from the start, and each room the player
 * enters says how far ahead or behind the ghost was there.
 *
 * Recording only reads Mario; the race's ghost is a pose of the game's own
 * Mario animations (type 0: let go at each map change and made again), drawn
 * with the blur's translucent colour. Neither is in online co-op's
 * fingerprint, and nothing happens without a recording or a race. */
#include "port/playmodes.h"
#include "port/replay.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"

#include <dolphin/mtx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char* marioAnimeGroupData[4][3];

#define GHOST_EVERY 2 /* frames per sample */
#define NAME_MAX 512

typedef struct Sample {
    u16 map;   /* index into the maps' names; 0xFFFF: not in the field (a battle, a map change) */
    s16 anim;  /* index into the animations' names, -1 none */
    u8 group;  /* the pose group (marioAnimeId) */
    u8 mirror; /* drawn mirrored */
    s16 yaw;   /* the turn, tenths of a degree */
    f32 x, y, z;
} Sample;

typedef struct Track {
    Sample* samples;
    u32 count, cap;
    char (*maps)[16];
    u32 mapCount;
    char (*anims)[32];
    u32 animCount;
} Track;

static Track sRec, sRace;
static int sRecording;
static u32 sFrame;       /* frames since the recording or the race began */
static int sRacing;
static char sRaceName[64];
static int sPose[3] = {-1, -1, -1};
static char sPoseMap[16];
static s16 sPoseAnim[3] = {-1, -1, -1};
static Sample sDraw;     /* the ghost this frame */
static int sDrawOn;
static u32 sLastMatched; /* the ghost's sample the player's last room was matched at */
static char sLastRoom[16];
static float sDelta;     /* the player's time in the last room entered minus the ghost's, seconds */
static int sDeltaShown;  /* a delta to show (a count that changes each room) */

static void trackClear(Track* t) {
    free(t->samples);
    free(t->maps);
    free(t->anims);
    memset(t, 0, sizeof(*t));
}

static int nameIndex(char (**names)[16], u32* count, const char* name, size_t width) {
    u32 i;
    char* base;
    for (i = 0; i < *count; i++) {
        if (strncmp((char*)*names + i * width, name, width - 1) == 0) {
            return (int)i;
        }
    }
    if (*count >= NAME_MAX) {
        return -1;
    }
    *names = realloc(*names, (*count + 1) * width);
    base = (char*)*names + *count * width;
    memset(base, 0, width);
    strncpy(base, name, width - 1);
    return (int)(*count)++;
}

static void sample(void) {
    Sample s;
    const MarioWork* wp = marioGetPtr();
    memset(&s, 0, sizeof(s));
    s.map = 0xFFFF;
    s.anim = -1;
    if (seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && wp != NULL &&
        !(wp->dispFlags & MARIO_DISP_FLAG_HIDDEN)) {
        int m = nameIndex(&sRec.maps, &sRec.mapCount, gp->currentMapName, 16);
        int a = wp->animName != NULL ? nameIndex((char(**)[16])&sRec.anims, &sRec.animCount, wp->animName, 32) : -1;
        s.map = m >= 0 ? (u16)m : 0xFFFF;
        s.anim = (s16)a;
        s.group = (u8)marioAnimeId();
        s.mirror = (wp->dispFlags & MARIO_DISP_FLAG_MIRROR) ||
                   (!(wp->dispFlags & MARIO_DISP_FLAG_NO_MIRROR) && revise360(wp->displayDirection) >= 90.0f &&
                    revise360(wp->displayDirection) <= 270.0f);
        s.yaw = (s16)(revise360(wp->displayDirection - wp->targetYaw) * 10.0f);
        s.x = wp->position.x + wp->dispOfs.x;
        s.y = wp->position.y + wp->dispOfs.y;
        s.z = wp->position.z + wp->dispOfs.z;
    }
    if (sRec.count == sRec.cap) {
        sRec.cap = sRec.cap ? sRec.cap * 2 : 4096;
        sRec.samples = realloc(sRec.samples, sRec.cap * sizeof(Sample));
    }
    sRec.samples[sRec.count++] = s;
}

/* --- the file's section: "GHST", u32 every, u32 maps + 16 bytes each,
 * u32 animations + 32 bytes each, u32 samples + 20 bytes each (u16 map,
 * s16 anim, u8 group, u8 mirror, s16 yaw, f32 x, y, z), little-endian --- */

static void put32(unsigned char** p, u32 v) {
    (*p)[0] = (unsigned char)v;
    (*p)[1] = (unsigned char)(v >> 8);
    (*p)[2] = (unsigned char)(v >> 16);
    (*p)[3] = (unsigned char)(v >> 24);
    *p += 4;
}

static u32 get32(const unsigned char* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

size_t portGhostBlob(unsigned char* out, size_t room) {
    const size_t size = 4 + 4 + 4 + sRec.mapCount * 16 + 4 + sRec.animCount * 32 + 4 + (size_t)sRec.count * 20;
    unsigned char* p = out;
    u32 i;
    if (sRec.count == 0) {
        return 0;
    }
    if (out == NULL || room < size) {
        return size;
    }
    memcpy(p, "GHST", 4);
    p += 4;
    put32(&p, GHOST_EVERY);
    put32(&p, sRec.mapCount);
    memcpy(p, sRec.maps, sRec.mapCount * 16);
    p += sRec.mapCount * 16;
    put32(&p, sRec.animCount);
    memcpy(p, sRec.anims, sRec.animCount * 32);
    p += sRec.animCount * 32;
    put32(&p, sRec.count);
    for (i = 0; i < sRec.count; i++) {
        const Sample* s = &sRec.samples[i];
        u32 f;
        p[0] = (unsigned char)s->map;
        p[1] = (unsigned char)(s->map >> 8);
        p[2] = (unsigned char)s->anim;
        p[3] = (unsigned char)((u16)s->anim >> 8);
        p[4] = s->group;
        p[5] = s->mirror;
        p[6] = (unsigned char)s->yaw;
        p[7] = (unsigned char)((u16)s->yaw >> 8);
        p += 8;
        memcpy(&f, &s->x, 4);
        put32(&p, f);
        memcpy(&f, &s->y, 4);
        put32(&p, f);
        memcpy(&f, &s->z, 4);
        put32(&p, f);
    }
    return size;
}

int portGhostHasTrack(const unsigned char* data, size_t size) {
    return data != NULL && size >= 8 && memcmp(data, "GHST", 4) == 0;
}

static int parse(const unsigned char* data, size_t size, Track* t) {
    const unsigned char* p = data + 8;
    const unsigned char* end = data + size;
    u32 i;
    memset(t, 0, sizeof(*t));
    if (!portGhostHasTrack(data, size) || get32(data + 4) != GHOST_EVERY) {
        return 0;
    }
#define NEED(n)                                                                                                    \
    if ((size_t)(end - p) < (size_t)(n)) {                                                                         \
        trackClear(t);                                                                                             \
        return 0;                                                                                                  \
    }
    NEED(4);
    t->mapCount = get32(p);
    p += 4;
    if (t->mapCount > NAME_MAX) {
        return 0;
    }
    NEED(t->mapCount * 16);
    t->maps = malloc(t->mapCount * 16 + 1);
    memcpy(t->maps, p, t->mapCount * 16);
    p += t->mapCount * 16;
    NEED(4);
    t->animCount = get32(p);
    p += 4;
    if (t->animCount > NAME_MAX) {
        trackClear(t);
        return 0;
    }
    NEED(t->animCount * 32);
    t->anims = malloc(t->animCount * 32 + 1);
    memcpy(t->anims, p, t->animCount * 32);
    p += t->animCount * 32;
    NEED(4);
    t->count = get32(p);
    p += 4;
    NEED((size_t)t->count * 20);
    t->samples = malloc((size_t)t->count * sizeof(Sample) + 1);
    for (i = 0; i < t->count; i++, p += 20) {
        Sample* s = &t->samples[i];
        u32 f;
        s->map = (u16)(p[0] | (p[1] << 8));
        s->anim = (s16)(p[2] | (p[3] << 8));
        s->group = p[4] < 3 ? p[4] : 0;
        s->mirror = p[5];
        s->yaw = (s16)(p[6] | (p[7] << 8));
        f = get32(p + 8);
        memcpy(&s->x, &f, 4);
        f = get32(p + 12);
        memcpy(&s->y, &f, 4);
        f = get32(p + 16);
        memcpy(&s->z, &f, 4);
        if (s->map != 0xFFFF && s->map >= t->mapCount) {
            s->map = 0xFFFF;
        }
        if (s->anim >= (s16)t->animCount) {
            s->anim = -1;
        }
    }
#undef NEED
    for (i = 0; i < t->mapCount; i++) {
        t->maps[i][15] = '\0';
    }
    for (i = 0; i < t->animCount; i++) {
        t->anims[i][31] = '\0';
    }
    return 1;
}

int portGhostRaceBegin(const unsigned char* data, size_t size, const char* name) {
    trackClear(&sRace);
    if (!parse(data, size, &sRace)) {
        return 0;
    }
    snprintf(sRaceName, sizeof(sRaceName), "%s", name != NULL ? name : "");
    sRacing = 1;
    sFrame = 0;
    sLastMatched = 0;
    sLastRoom[0] = '\0';
    printf("[ghost] racing '%s': %u samples\n", sRaceName, (unsigned)sRace.count);
    fflush(stdout);
    return 1;
}

int portGhostRacing(void) {
    return sRacing;
}

const char* portGhostRaceName(void) {
    return sRaceName;
}

double portGhostRaceSeconds(void) {
    return (double)sFrame / 60.0;
}

int portGhostDelta(float* seconds) {
    *seconds = sDelta;
    return sDeltaShown;
}

/* --- drawing --- */

static void ghostDisp(CameraId cam, void* param) {
    Mtx m, r;
    const int pose = sPose[sDraw.group];
    const f32 s = marioGetScale();
    (void)cam;
    (void)param;
    if (pose < 0) {
        return;
    }
    MTXScale(m, s, s, sDraw.mirror ? -s : s);
    MTXRotRad(r, 'y', 0.017453292f * (sDraw.yaw / 10.0f));
    MTXConcat(r, m, m);
    MTXTrans(r, sDraw.x, sDraw.y, sDraw.z);
    MTXConcat(r, m, m);
    animPoseSetMaterialFlagOn(pose, ANIM_MATERIAL_FLAG_EVT_COLOR);
    animPoseSetMaterialEvtColor(pose, (GXColor){0xC0, 0xE0, 0xFF, 0x78});
    animPoseDrawMtx(pose, m, 1, 0.0f, 1.0f);
    animPoseDrawMtx(pose, m, 2, 0.0f, 1.0f);
    animPoseDrawMtx(pose, m, 3, 0.0f, 1.0f);
}

static void raceFrame(void) {
    const u32 at = sFrame / GHOST_EVERY;
    const SequenceType seq = seqGetSeq();
    int i;
    sDrawOn = 0;
    if (seq == SEQ_MAP_CHANGE) {
        /* the type 0 poses go at every map change, the same room again too (a Game
         * Over's continue, a door back into it): taken again once it's in */
        sPoseMap[0] = '\0';
    }
    if (seq != SEQ_GAME || gp == NULL || gp->inBattle) {
        return;
    }
    if (strcmp(sPoseMap, gp->currentMapName) != 0) {
        /* a new room: type 0 poses were let go at the map change */
        snprintf(sPoseMap, sizeof(sPoseMap), "%s", gp->currentMapName);
        for (i = 0; i < 3; i++) {
            sPose[i] = -1;
            sPoseAnim[i] = -1;
        }
        /* the ghost's time in this room: its first sample here since the last room matched */
        if (strcmp(sLastRoom, gp->currentMapName) != 0) {
            u32 k;
            snprintf(sLastRoom, sizeof(sLastRoom), "%s", gp->currentMapName);
            for (k = sLastMatched; k < sRace.count; k++) {
                const Sample* g = &sRace.samples[k];
                if (g->map != 0xFFFF && strcmp(sRace.maps[g->map], gp->currentMapName) == 0) {
                    sLastMatched = k;
                    sDelta = (float)((double)sFrame - (double)k * GHOST_EVERY) / 60.0f;
                    sDeltaShown++;
                    break;
                }
            }
        }
    }
    if (at >= sRace.count) {
        return;
    }
    sDraw = sRace.samples[at];
    if (sDraw.map == 0xFFFF || strcmp(sRace.maps[sDraw.map], gp->currentMapName) != 0) {
        return;
    }
    if (sPose[sDraw.group] < 0) {
        sPose[sDraw.group] = animPoseEntry(marioAnimeGroupData[0][sDraw.group], 0);
    }
    if (sPose[sDraw.group] < 0) {
        return;
    }
    if (sDraw.anim >= 0 && sPoseAnim[sDraw.group] != sDraw.anim) {
        sPoseAnim[sDraw.group] = sDraw.anim;
        animPoseSetAnim(sPose[sDraw.group], sRace.anims[sDraw.anim], TRUE);
    }
    animPoseMain(sPose[sDraw.group]);
    {
        Vec pos = {sDraw.x, sDraw.y, sDraw.z};
        f32 z;
        PSMTX44MultVec(camGetPtr(CAMERA_3D)->projection, &pos, &pos);
        z = 5000.0f * pos.z + 5000.0f;
        z = z < 0.0f ? 0.0f : z > 10000.0f ? 10000.0f : z;
        dispEntry(CAMERA_3D, -z, 2, ghostDisp, NULL);
    }
    sDrawOn = 1;
}

void portGhostFrame(void) {
    if (portReplayRecording()) {
        if (!sRecording) {
            sRecording = 1;
            trackClear(&sRec);
            sFrame = 0;
        }
        if (sFrame % GHOST_EVERY == 0) {
            sample();
        }
        sFrame++;
        return;
    }
    sRecording = 0;
    if (sRacing) {
        raceFrame();
        sFrame++;
    }
}
