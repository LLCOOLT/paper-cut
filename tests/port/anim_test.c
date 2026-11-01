/* Host unit test for anim model decoding (filemgr type 5, port/anim_pc.h).
 * Builds a synthetic big-endian model in memory (one shape, textures, groups,
 * three anim table entries sharing two AnimData blocks whose sub-pointers are
 * relative to the AnimData), decodes it with portAnimDecode and checks the
 * native graph field by field, which parts stay big-endian in the blob, the
 * read patterns animdrv uses, idempotency/release, and the error paths. */
#include "port/anim_pc.h"
#include "port/bedecode.h"
#include "driver/animdrv.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sFailures, sChecks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        sChecks++;                                                                \
        if (!(c)) {                                                               \
            sFailures++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

/* ---------------------------------------------------- BE model layout */
enum {
    O_SHAPES = 0x200, O_DRAWCALLS = 0x2B0, O_POS = 0x2C0, O_POSIDX = 0x2E4, O_NRM = 0x2F0, O_NRMIDX = 0x308,
    O_CLR = 0x314, O_CLRIDX = 0x31C, O_TEXIDX0 = 0x328, O_TEXVTX = 0x334, O_TEXMTX = 0x344, O_TEXDATA = 0x35C,
    O_TEXBINDS = 0x364, O_DRAWS = 0x3A4, O_VIS = 0x410, O_NODE = 0x6A0, O_GROUPS = 0x424, O_ANIMS = 0x47C,
    O_ANIM_A = 0x540, O_ANIM_B = 0x630, MODEL_SIZE = 0x700,
    /* inside an AnimData (relative) */
    A_BASE = 0x60, A_KEYS = 0x6C, A_POSD = 0xC4, A_TEXD = 0xCC, A_VISD = 0xD8, A_NODED = 0xDC, A_MARKS = 0xE0
};

static void putF(uint8_t* p, float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    portPutBE32(p, v.u);
}

static void putAnim(uint8_t* m, uint32_t at, int full) {
    uint8_t* a = m + at;
    portPutBE32(a + 0x0, full ? 0xE8 : 0x6C);
    portPutBE32(a + 0x4, 1);
    portPutBE32(a + 0x24, A_BASE);
    putF(a + 0x44, -1.0f); putF(a + 0x48, -2.0f); putF(a + 0x4C, -3.0f); /* bbox */
    putF(a + 0x50, 4.0f); putF(a + 0x54, 25.0f); putF(a + 0x58, 6.0f);
    portPutBE32(a + A_BASE + 0, full ? 1 : 0); /* loop */
    putF(a + A_BASE + 4, 0.0f);
    putF(a + A_BASE + 8, full ? 30.0f : 10.0f);
    if (!full) {
        return;
    }
    portPutBE32(a + 0x08, 2); /* keyframes */
    portPutBE32(a + 0x0C, 2); /* pos deltas */
    portPutBE32(a + 0x14, 1); /* texmtx deltas */
    portPutBE32(a + 0x18, 1); /* vis deltas */
    portPutBE32(a + 0x1C, 1); /* node deltas */
    portPutBE32(a + 0x20, 1); /* frame marks */
    portPutBE32(a + 0x28, A_KEYS);
    portPutBE32(a + 0x2C, A_POSD);
    portPutBE32(a + 0x30, 0); /* no normal deltas */
    portPutBE32(a + 0x34, A_TEXD);
    portPutBE32(a + 0x38, A_VISD);
    portPutBE32(a + 0x3C, A_NODED);
    portPutBE32(a + 0x40, A_MARKS);
    /* key 0 at t=0: nothing; key 1 at t=15: pos deltas 0..1, texmtx 0, vis 0, node 0 */
    putF(a + A_KEYS + 0, 0.0f);
    putF(a + A_KEYS + 0x2C + 0x00, 15.0f);
    portPutBE32(a + A_KEYS + 0x2C + 0x04, 0);
    portPutBE32(a + A_KEYS + 0x2C + 0x08, 2);
    portPutBE32(a + A_KEYS + 0x2C + 0x14, 0);
    portPutBE32(a + A_KEYS + 0x2C + 0x18, 1);
    portPutBE32(a + A_KEYS + 0x2C + 0x1C, 0);
    portPutBE32(a + A_KEYS + 0x2C + 0x20, 1);
    portPutBE32(a + A_KEYS + 0x2C + 0x24, 0);
    portPutBE32(a + A_KEYS + 0x2C + 0x28, 1);
    a[A_POSD + 0] = 1; a[A_POSD + 1] = 16; a[A_POSD + 2] = (uint8_t)-32; a[A_POSD + 3] = 0;
    a[A_POSD + 4] = 1; a[A_POSD + 5] = 0; a[A_POSD + 6] = 0; a[A_POSD + 7] = 48;
    a[A_TEXD + 0] = 0; a[A_TEXD + 1] = 2;
    putF(a + A_TEXD + 4, 0.25f);
    putF(a + A_TEXD + 8, -0.5f);
    a[A_VISD + 0] = 1; a[A_VISD + 1] = 0xFF;
    a[A_NODED + 0] = 3; a[A_NODED + 1] = 8; a[A_NODED + 2] = 0; a[A_NODED + 3] = 0x60; /* step key */
    putF(a + A_MARKS + 0, 0.0f);
    portPutBE32(a + A_MARKS + 4, 7);
}

static uint8_t* buildModel(void) {
    uint8_t* m = (uint8_t*)calloc(1, MODEL_SIZE);
    int i;
    portPutBE32(m + 0x0, MODEL_SIZE);
    strcpy((char*)m + 0x04, "c_test");
    strcpy((char*)m + 0x44, "c_test_tex");
    strcpy((char*)m + 0x84, "2004/07/01");
    portPutBE32(m + 0xC4, 0x6);
    portPutBE32(m + 0xC8, 12);
    portPutBE32(m + 0xCC, -3);
    putF(m + 0xD0, -1.5f); putF(m + 0xD4, 0.0f); putF(m + 0xD8, -2.5f);
    putF(m + 0xDC, 1.5f); putF(m + 0xE0, 3.0f); putF(m + 0xE4, 2.5f);
    portPutBE32(m + 0xE8, 1);  /* shapes */
    portPutBE32(m + 0xEC, 2);  /* draw calls */
    portPutBE32(m + 0xF0, 3);  /* pos */
    portPutBE32(m + 0xF4, 3);
    portPutBE32(m + 0xF8, 2);  /* nrm */
    portPutBE32(m + 0xFC, 3);
    portPutBE32(m + 0x100, 2); /* clr */
    portPutBE32(m + 0x104, 3);
    portPutBE32(m + 0x108, 3); /* tex0 idx */
    portPutBE32(m + 0x128, 2); /* texcoords */
    portPutBE32(m + 0x12C, 1); /* tex mtx */
    portPutBE32(m + 0x130, 1); /* tex data */
    portPutBE32(m + 0x134, 1); /* tex binds */
    portPutBE32(m + 0x138, 1); /* draws */
    portPutBE32(m + 0x13C, 2); /* vis */
    portPutBE32(m + 0x140, 24); /* node floats: one AnimPoseNode */
    portPutBE32(m + 0x144, 1); /* groups */
    portPutBE32(m + 0x148, 3); /* anims */
    portPutBE32(m + 0x14C, O_SHAPES);
    portPutBE32(m + 0x150, O_DRAWCALLS);
    portPutBE32(m + 0x154, O_POS);
    portPutBE32(m + 0x158, O_POSIDX);
    portPutBE32(m + 0x15C, O_NRM);
    portPutBE32(m + 0x160, O_NRMIDX);
    portPutBE32(m + 0x164, O_CLR);
    portPutBE32(m + 0x168, O_CLRIDX);
    portPutBE32(m + 0x16C, O_TEXIDX0); /* texIndex[1..7] = 0 */
    portPutBE32(m + 0x18C, O_TEXVTX);
    portPutBE32(m + 0x190, O_TEXMTX);
    portPutBE32(m + 0x194, O_TEXDATA);
    portPutBE32(m + 0x198, O_TEXBINDS);
    portPutBE32(m + 0x19C, O_DRAWS);
    portPutBE32(m + 0x1A0, O_VIS);
    portPutBE32(m + 0x1A4, O_NODE);
    portPutBE32(m + 0x1A8, O_GROUPS);
    portPutBE32(m + 0x1AC, O_ANIMS);
    /* shape */
    strcpy((char*)m + O_SHAPES, "s_body");
    portPutBE32(m + O_SHAPES + 0x40, 0);
    portPutBE32(m + O_SHAPES + 0x44, 3);
    portPutBE32(m + O_SHAPES + 0x48, 1);
    portPutBE32(m + O_SHAPES + 0x58, 0); /* vtxTex[0].first */
    portPutBE32(m + O_SHAPES + 0x98, 0);
    portPutBE32(m + O_SHAPES + 0x9C, 1);
    portPutBE32(m + O_SHAPES + 0xA0, 2);
    portPutBE32(m + O_SHAPES + 0xA4, 3);
    /* draw calls */
    portPutBE32(m + O_DRAWCALLS + 0, 0); portPutBE32(m + O_DRAWCALLS + 4, 3);
    portPutBE32(m + O_DRAWCALLS + 8, 1); portPutBE32(m + O_DRAWCALLS + 12, 2);
    for (i = 0; i < 9; i++) putF(m + O_POS + i * 4, (float)i + 0.5f);
    for (i = 0; i < 6; i++) putF(m + O_NRM + i * 4, -(float)i);
    for (i = 0; i < 3; i++) {
        portPutBE32(m + O_POSIDX + i * 4, 2 - i);
        portPutBE32(m + O_NRMIDX + i * 4, i & 1);
        portPutBE32(m + O_CLRIDX + i * 4, 1);
        portPutBE32(m + O_TEXIDX0 + i * 4, 0x10000 + i); /* high half set: truncated to u16 */
    }
    m[O_CLR + 0] = 0x11; m[O_CLR + 1] = 0x22; m[O_CLR + 2] = 0x33; m[O_CLR + 3] = 0x44;
    for (i = 0; i < 4; i++) putF(m + O_TEXVTX + i * 4, 0.25f * i);
    /* tex mtx */
    m[O_TEXMTX + 0] = 1;
    putF(m + O_TEXMTX + 0x04, 0.5f); putF(m + O_TEXMTX + 0x08, 0.75f);
    putF(m + O_TEXMTX + 0x0C, 1.0f); putF(m + O_TEXMTX + 0x10, 2.0f); putF(m + O_TEXMTX + 0x14, 90.0f);
    portPutBE32(m + O_TEXDATA + 0, 0); portPutBE32(m + O_TEXDATA + 4, (uint32_t)-1);
    portPutBE32(m + O_TEXBINDS + 0, 0); portPutBE32(m + O_TEXBINDS + 4, 5); portPutBE32(m + O_TEXBINDS + 8, 2);
    strcpy((char*)m + O_TEXBINDS + 0xC, "tex_body");
    /* draw */
    portPutBE32(m + O_DRAWS + 0x00, 1);
    portPutBE32(m + O_DRAWS + 0x08, 4);
    portPutBE32(m + O_DRAWS + 0x10, 0);
    m[O_DRAWS + 0x30] = 0;
    portPutBE32(m + O_DRAWS + 0x38, 0);
    portPutBE32(m + O_DRAWS + 0x3C, 2);
    portPutBE32(m + O_DRAWS + 0x40, 0);
    portPutBE32(m + O_DRAWS + 0x4C, 0);
    m[O_VIS + 0] = 1; m[O_VIS + 1] = 0;
    for (i = 0; i < 24; i++) putF(m + O_NODE + i * 4, 10.0f * i);
    strcpy((char*)m + O_GROUPS, "g_root");
    portPutBE32(m + O_GROUPS + 0x40, (uint32_t)-1);
    portPutBE32(m + O_GROUPS + 0x44, (uint32_t)-1);
    portPutBE32(m + O_GROUPS + 0x48, 0);
    portPutBE32(m + O_GROUPS + 0x4C, 0);
    portPutBE32(m + O_GROUPS + 0x50, 0);
    portPutBE32(m + O_GROUPS + 0x54, 1);
    strcpy((char*)m + O_ANIMS + 0x00, "S_1");
    portPutBE32(m + O_ANIMS + 0x3C, O_ANIM_A);
    strcpy((char*)m + O_ANIMS + 0x40, "W_1");
    portPutBE32(m + O_ANIMS + 0x7C, O_ANIM_B);
    strcpy((char*)m + O_ANIMS + 0x80, "S_1_alias");
    portPutBE32(m + O_ANIMS + 0xBC, O_ANIM_A);
    putAnim(m, O_ANIM_A, 1);
    putAnim(m, O_ANIM_B, 0);
    return m;
}

/* ---------------------------------------------------- error capture */
static jmp_buf sJmp;
static int sExpectError;
static char sLastError[256];
static void onError(PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    snprintf(sLastError, sizeof(sLastError), "%s", msg);
    if (sExpectError) {
        longjmp(sJmp, 1);
    }
    fprintf(stderr, "unexpected decode error: %s\n", msg);
    sFailures++;
}

static int decodeFails(uint8_t* blob, uint32_t size) {
    AnimationPoseData* r;
    sExpectError = 1;
    if (setjmp(sJmp)) {
        sExpectError = 0;
        return 1;
    }
    r = portAnimDecode(blob, size, "bad");
    sExpectError = 0;
    return r == NULL;
}

static void testDecode(void) {
    uint8_t* m = buildModel();
    AnimationPoseData* r = portAnimDecode(m, MODEL_SIZE, "c_test");
    AnimData* a;
    AnimData* b;
    u16* idx;
    int i;

    CHECK(r != NULL);
    if (!r) {
        return;
    }
    CHECK(portAnimRoot(m) == r);
    CHECK(((PortAnimHeader*)m)->magic == PORT_ANIM_MAGIC);
    CHECK(portAnimLiveCount() == 1);
    CHECK((uint8_t*)r < m || (uint8_t*)r >= m + MODEL_SIZE);
    CHECK(r->size == MODEL_SIZE);
    CHECK(!strcmp(r->fileName, "c_test"));
    CHECK(!strcmp(r->textureName, "c_test_tex"));
    CHECK(r->flags == 6 && r->radius == 12 && r->height == -3);
    CHECK(r->bboxMin.x == -1.5f && r->bboxMin.z == -2.5f && r->bboxMax.y == 3.0f);
    CHECK(r->shapeCount == 1 && r->drawCallCount == 2 && r->bufferPosNum == 3 && r->bufferNrmNum == 2);
    CHECK(r->texIdxNum[0] == 3 && r->texVtxNum == 2 && r->bufferNodeNum == 24 && r->animCount == 3);
    /* the raw-offset readers elsewhere in the game (evt_mobj FLD 0xD0.., imgdrv +4) */
    CHECK(*(f32*)((u8*)r + 0xE0) == 3.0f);
    CHECK(!strcmp((char*)r + 4, "c_test"));

    CHECK(!strcmp(r->shapes[0].name, "s_body"));
    CHECK(r->shapes[0].vtxPos.count == 3 && r->shapes[0].vtxNrm.first == 1);
    CHECK(r->shapes[0].shapeDrawCount == 1 && r->shapes[0].dispMode == 2 && r->shapes[0].cullMode == 3);
    CHECK(r->drawCalls[1].first == 1 && r->drawCalls[1].count == 2);

    /* CPU-read base values: native, outside the blob */
    CHECK((uint8_t*)r->bufferVtxPos < m || (uint8_t*)r->bufferVtxPos >= m + MODEL_SIZE);
    CHECK(r->bufferVtxPos[0].x == 0.5f && r->bufferVtxPos[2].z == 8.5f);
    CHECK(r->nrmVtxBuffer[1].z == -5.0f);
    CHECK(((f32*)r->nodeData)[3] == 30.0f);
    CHECK(r->texAnimEntries[0].texBindIdx == 1 && r->texAnimEntries[0].transT == 0.75f &&
          r->texAnimEntries[0].rotate == 90.0f);

    /* GX arrays and index tables stay big-endian in the blob */
    CHECK((uint8_t*)r->clr0VtxBuffer == m + O_CLR && r->clr0VtxBuffer[0].r == 0x11 && r->clr0VtxBuffer[0].a == 0x44);
    CHECK((uint8_t*)r->texVtxBuffer == m + O_TEXVTX && portBEF32(&r->texVtxBuffer[3]) == 0.75f);
    CHECK((uint8_t*)r->posIndex == m + O_POSIDX && (uint8_t*)r->texIndex[0] == m + O_TEXIDX0);
    CHECK(r->texIndex[1] == NULL && r->texIndex[7] == NULL);
    CHECK((uint8_t*)r->groupVisibility == m + O_VIS && r->groupVisibility[0] == 1);
    /* renderProc's pattern: (u16*)((u32*)idx + i) + 1, read with portBE16 */
    for (i = 0; i < 3; i++) {
        idx = (u16*)((u32*)r->posIndex + i) + 1;
        CHECK(portBE16(idx) == 2 - i);
        CHECK((u16)portBE32((u32*)r->texIndex[0] + i) == i); /* multi-texture path truncates the u32 */
    }

    CHECK(r->texData[0].bindId == 0 && r->texData[0].wrapFlags == -1);
    CHECK(r->texBinds[0].texId == 5 && r->texBinds[0].texType == 2 && !strcmp((const char*)r->texBinds[0].pad_0C, "tex_body"));
    /* animSetMaterial_Texture reads texId/texType at +4/+8 */
    CHECK(*(u32*)((u8*)&r->texBinds[0] + 4) == 5 && *(s32*)((u8*)&r->texBinds[0] + 8) == 2);
    CHECK(r->draws[0].texCount == 1 && r->draws[0].drawMode == 4 && r->draws[0].drawCallCount == 2);
    CHECK(!strcmp(r->groups[0].name, "g_root") && r->groups[0].brotherGroupId == -1 && r->groups[0].useSSC == 1);

    /* anim table: entries 0 and 2 share AnimData A; pointers relative to it */
    a = r->anims[0].data;
    b = r->anims[1].data;
    CHECK(!strcmp(r->anims[2].name, "S_1_alias"));
    CHECK(a != NULL && b != NULL && a != b && r->anims[2].data == a);
    if (a && b) {
        CHECK(*(s32*)&a->loopInfo[0] == 1 && a->loopInfo[2] == 30.0f);
        CHECK(*(s32*)&b->loopInfo[0] == 0 && b->loopInfo[2] == 10.0f && b->keyframes == NULL);
        CHECK(a->keyframeCount == 2 && a->keyframes[1].time == 15.0f && a->keyframes[1].vtxPosCount == 2);
        CHECK(a->keyframes[1].nodeCount == 1 && a->keyframes[1].texStart == 0);
        CHECK((uint8_t*)a->vtxPosDeltas == m + O_ANIM_A + A_POSD);
        CHECK(a->vtxPosDeltas[0] == 1 && (s8)a->vtxPosDeltas[1] == 16 && (s8)a->vtxPosDeltas[2] == -32); /* index step, s8 x/y/z */
        CHECK(a->vtxNrmDeltas == NULL);
        CHECK(a->texDeltas[0].bindAdd == 2 && a->texDeltas[0].transS == 0.25f &&
              a->texDeltas[0].transT == -0.5f);
        CHECK(a->visDeltas[0] == 1 && a->visDeltas[1] == 0xFF); /* index step, add */
        CHECK(a->nodeDeltas[0] == 3 && (s8)a->nodeDeltas[1] == 8 && a->nodeDeltas[3] == 0x60); /* index step, value, tangents */
        CHECK(a->valueKeys[0].value == 7);
        CHECK(a->bboxMin.y == -2.0f && a->bboxMax.y == 25.0f);
        /* the external readers' view (win_*, eff_nice, shuryolight) */
        CHECK(a->loopInfo[2] - a->loopInfo[1] == 30.0f);
    }

    /* idempotent; release; a new model at the same address */
    CHECK(portAnimDecode(m, MODEL_SIZE, "again") == r);
    CHECK(portAnimLiveCount() == 1);
    {
        uint8_t* fresh = buildModel();
        memcpy(m, fresh, MODEL_SIZE);
        free(fresh);
        r = portAnimDecode(m, MODEL_SIZE, "reload");
        CHECK(r != NULL && portAnimLiveCount() == 1 && r->anims[1].data->loopInfo[2] == 10.0f);
    }
    portAnimRelease(m);
    CHECK(portAnimLiveCount() == 0);
    portAnimRelease(m); /* no-op */
    free(m);
}

static void testErrors(void) {
    uint8_t* m;

    m = buildModel();
    CHECK(decodeFails(m, 0x100)); /* smaller than the header */
    free(m);

    m = buildModel();
    portPutBE32(m + O_ANIM_A + A_KEYS + 0x2C + 0x08, 100); /* pos delta range past the file */
    CHECK(decodeFails(m, MODEL_SIZE));
    CHECK(strstr(sLastError, "vtxPosDeltas") != NULL);
    free(m);

    m = buildModel();
    portPutBE32(m + O_ANIM_A + A_KEYS + 0x2C + 0x18, 2); /* tex mtx range past its table */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    portPutBE32(m + 0x158, 0x8); /* index table inside the overwritten header bytes */
    CHECK(decodeFails(m, MODEL_SIZE));
    CHECK(strstr(sLastError, "posIndex") != NULL);
    free(m);

    m = buildModel();
    portPutBE32(m + 0x140, 0x1000); /* node buffer runs past the file */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    portPutBE32(m + O_ANIMS + 0x7C, 0x10); /* anim data inside the header */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    portPutBE32(m + 0x148, 1000); /* anim table runs past the file */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    portPutBE32(m + O_GROUPS + 0x50, 1); /* node 1 + 24 floats past the node buffer */
    CHECK(decodeFails(m, MODEL_SIZE));
    CHECK(strstr(sLastError, "g_root") != NULL);
    free(m);

    m = buildModel();
    portPutBE32(m + O_SHAPES + 0x9C, 2); /* shape uses draws 0..1 of 1 */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    portPutBE32(m + O_DRAWS + 0x3C, 3); /* draw uses draw calls 0..2 of 2 */
    CHECK(decodeFails(m, MODEL_SIZE));
    free(m);

    m = buildModel();
    m[O_DRAWS + 0x30] = 8; /* texture 0 reads texIndex[8] */
    CHECK(decodeFails(m, MODEL_SIZE));
    CHECK(strstr(sLastError, "vertex array") != NULL);
    free(m);

    m = buildModel();
    portPutBE32(m + 0x150, O_SHAPES + 0x68); /* pre-release 0x68-byte shapes (a/OFF_hei_01b) */
    CHECK(decodeFails(m, MODEL_SIZE));
    CHECK(strstr(sLastError, "pre-release") != NULL);
    free(m);

    /* tolerated, as on the GameCube: a zero-count range is never followed, and a
     * draw's texDataIds may name no texture (c_kamek_bb/be draw 34: -1) */
    m = buildModel();
    portPutBE32(m + O_SHAPES + 0x98, 95); /* a/OFF_hei_01b shape 0: draws 95+0 */
    portPutBE32(m + O_SHAPES + 0x9C, 0);
    portPutBE32(m + O_DRAWS + 0x38, 50); /* draw calls 50+0 */
    portPutBE32(m + O_DRAWS + 0x3C, 0);
    portPutBE32(m + O_DRAWS + 0x10, (uint32_t)-1);
    CHECK(portAnimDecode(m, MODEL_SIZE, "tolerated") != NULL);
    portAnimRelease(m);
    free(m);

    CHECK(portAnimLiveCount() == 0); /* failures register nothing */
}

int main(void) {
    portAnimErrorHook = onError;
    testDecode();
    testErrors();
    printf("anim_test: %d checks, %d failures\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
