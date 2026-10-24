/* Host unit test for map 'd' file decoding (port/src/common/map_pc.c) and
 * the PC-side accessors hitdrv uses for collision geometry. No disc data:
 * builds a tiny big-endian map file in memory -- a joint tree (root, child
 * with a packed mesh, sibling hit joint with an unpacked mesh), one material
 * with a sampler, the vcd table, texture / material-name / light / fog /
 * animation tables -- with a relocation table, decodes it and checks the
 * native graph, relocation coverage, the ARAM-style copy + rebase, and the
 * TPL detach/attach round trip mapdrv uses for the map texture file. */
#include "port/bedecode.h"
#include "port/gx_arrays.h"
#include "port/map_pc.h"
#include "port/tpl_pc.h"
#include "driver/mapfile.h"

/* MapAnimHeader's lists are the decomp's generic MapAnimList; view them per kind. */
#define JOINTS(h) ((MapAnimJointList*)(h)->jointAnims)
#define MATS(h) ((MapAnimMatList*)(h)->materialAnims)
#define LIGHTS(h) ((MapAnimLightList*)(h)->lightAnims)
#define LIGHTPARAMS(h) ((MapAnimLightParamList*)(h)->lightColorAnims)
#include "sdk/texPalette.h"

#include <math.h>
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

/* ------------------------------------------------- BE map file builder */
#define CAP 0x4000
typedef struct Builder {
    uint8_t* f;      /* whole file; data at +0x20 */
    uint32_t data;   /* bytes of data used */
    uint32_t rel[256];
    uint32_t relCount;
    struct { uint32_t off; const char* name; } chunks[16];
    uint32_t chunkCount;
} Builder;

static uint8_t* D(Builder* b, uint32_t off) { return b->f + 0x20 + off; }
static uint32_t alloc(Builder* b, uint32_t size) {
    uint32_t o = (b->data + 3) & ~3u;
    b->data = o + size;
    return o;
}
static void w32(Builder* b, uint32_t off, uint32_t v) { portPutBE32(D(b, off), v); }
static void w16(Builder* b, uint32_t off, uint16_t v) { portPutBE16(D(b, off), v); }
static void wf(Builder* b, uint32_t off, float v) {
    union { float f; uint32_t u; } x;
    x.f = v;
    w32(b, off, x.u);
}
/* pointer word at data offset 'at' -> data offset 'target' (relocated) */
static void ptr(Builder* b, uint32_t at, uint32_t target) {
    w32(b, at, target);
    b->rel[b->relCount++] = at;
}
static uint32_t str(Builder* b, const char* s) {
    uint32_t o = alloc(b, (uint32_t)strlen(s) + 1);
    memcpy(D(b, o), s, strlen(s) + 1);
    return o;
}
static void chunk(Builder* b, const char* name, uint32_t off) {
    b->chunks[b->chunkCount].off = off;
    b->chunks[b->chunkCount].name = name;
    b->chunkCount++;
}
static uint32_t finish(Builder* b) {
    uint32_t at, i, dataSize = (b->data + 3) & ~3u, strAt, s;
    at = 0x20 + dataSize;
    for (i = 0; i < b->relCount; i++) {
        portPutBE32(b->f + at + i * 4, b->rel[i]);
    }
    at += b->relCount * 4;
    strAt = at + b->chunkCount * 8;
    s = 0;
    for (i = 0; i < b->chunkCount; i++) {
        portPutBE32(b->f + at + i * 8, b->chunks[i].off);
        portPutBE32(b->f + at + i * 8 + 4, s);
        memcpy(b->f + strAt + s, b->chunks[i].name, strlen(b->chunks[i].name) + 1);
        s += (uint32_t)strlen(b->chunks[i].name) + 1;
    }
    at = strAt + s;
    portPutBE32(b->f + 0x0, at);
    portPutBE32(b->f + 0x4, dataSize);
    portPutBE32(b->f + 0x8, b->relCount);
    portPutBE32(b->f + 0xC, b->chunkCount);
    portPutBE32(b->f + 0x10, 0);
    return at;
}

/* Layout bookkeeping the checks need. */
typedef struct Layout {
    uint32_t size;
    uint32_t packedDL, positions, posArray;
    uint32_t extraWord; /* relocated word no spec field reads (coverage test) */
} Layout;

static const float kPos[3][3] = {{1.0f, 2.0f, 3.0f}, {-4.5f, 0.25f, 8.0f}, {100.0f, -1.0f, 0.5f}};

static uint8_t* buildMap(Layout* L, int withExtraReloc) {
    Builder b;
    uint32_t ver, info, root, child, hit, dmRoot, dmChild, mat, samp, tex, mesh, rawMesh, vcd, dl, raw;
    uint32_t pos, nrm, clr, tc, tevb, texTbl, nameTbl, light, lightTbl, fog, aTbl, aHdr, jl, jt, ml, md, ll, ld,
        lcList, lc;
    uint32_t i;
    memset(&b, 0, sizeof(b));
    b.f = (uint8_t*)calloc(1, CAP);

    /* the version string sits at data+0: a relocated pointer whose value is 0 */
    ver = str(&b, "ver1.02");

    info = alloc(&b, 0x10);
    root = alloc(&b, 0x60);         /* "S", no parts */
    child = alloc(&b, 0x60 + 8);    /* "c1", one packed part */
    hit = alloc(&b, 0x60 + 8);      /* "A", one unpacked part (hit) */
    dmRoot = alloc(&b, 0x10);
    dmChild = alloc(&b, 0x10);
    mat = alloc(&b, 0x114);
    samp = alloc(&b, 0xC);
    tex = alloc(&b, 0x10);
    mesh = alloc(&b, 0x10 + 8);
    rawMesh = alloc(&b, 0x10 + 4);
    vcd = alloc(&b, 0x68);

    ptr(&b, info + 0x0, ver);
    ptr(&b, info + 0x4, root);
    ptr(&b, info + 0x8, str(&b, "S"));
    ptr(&b, info + 0xC, str(&b, "A"));

    /* joints */
    ptr(&b, root + 0x0, str(&b, "S"));
    ptr(&b, root + 0x4, str(&b, "null"));
    ptr(&b, root + 0xC, child);
    ptr(&b, root + 0x10, hit); /* sibling: next */
    for (i = 0; i < 3; i++) {
        wf(&b, root + 0x18 + i * 4, 1.0f);
    }
    ptr(&b, root + 0x58, dmRoot);
    w32(&b, root + 0x5C, 0);

    ptr(&b, child + 0x0, str(&b, "c1"));
    ptr(&b, child + 0x4, str(&b, "mesh"));
    ptr(&b, child + 0x8, root);
    for (i = 0; i < 3; i++) {
        wf(&b, child + 0x18 + i * 4, 2.0f);
        wf(&b, child + 0x24 + i * 4, 90.0f * (float)i);
        wf(&b, child + 0x30 + i * 4, -10.0f + (float)i);
        wf(&b, child + 0x3C + i * 4, -1.0f - (float)i);
        wf(&b, child + 0x48 + i * 4, 1.0f + (float)i);
    }
    ptr(&b, child + 0x58, dmChild);
    w32(&b, child + 0x5C, 1);
    ptr(&b, child + 0x60, mat);
    ptr(&b, child + 0x64, mesh);

    ptr(&b, hit + 0x0, str(&b, "A"));
    ptr(&b, hit + 0x4, str(&b, "mesh"));
    ptr(&b, hit + 0x14, root); /* prev */
    ptr(&b, hit + 0x58, dmRoot); /* shared draw mode */
    w32(&b, hit + 0x5C, 1);
    ptr(&b, hit + 0x60, mat);
    ptr(&b, hit + 0x64, rawMesh);

    D(&b, dmRoot)[1] = 1; /* cullMode */
    D(&b, dmChild)[0] = 3;
    D(&b, dmChild)[1] = 2;
    D(&b, dmChild)[2] = 0x09; /* flags */
    w32(&b, dmChild + 4, 0x80000001u); /* lightMask */
    w32(&b, dmChild + 8, 0x12345678u); /* hit attributes */

    /* material + sampler + texture info */
    ptr(&b, mat + 0x0, str(&b, "mat0"));
    D(&b, mat)[4] = 0x11; D(&b, mat)[5] = 0x22; D(&b, mat)[6] = 0x33; D(&b, mat)[7] = 0x44;
    D(&b, mat)[8] = 1;   /* lightMode */
    D(&b, mat)[0xA] = 2; /* blendMode */
    D(&b, mat)[0xB] = 1; /* samplerCount */
    ptr(&b, mat + 0xC, samp);
    w32(&b, mat + 0x10, 0xDEAD0000u); /* not relocated: samplers[1] must stay NULL */
    wf(&b, mat + 0x2C + 0x08, 1.0f);
    wf(&b, mat + 0x2C + 0x0C, 1.0f);
    wf(&b, mat + 0x2C + 0x14, 0.75f);
    D(&b, mat)[0x10C] = 0xFE; D(&b, mat)[0x10F] = 0x80;
    tevb = alloc(&b, 4);
    D(&b, tevb)[0] = 7;
    ptr(&b, mat + 0x110, tevb);
    ptr(&b, samp + 0x0, tex);
    w32(&b, samp + 0x4, 0xCAFEF00Du);
    D(&b, samp)[8] = 1; D(&b, samp)[9] = 2; D(&b, samp)[0xA] = 1;
    ptr(&b, tex + 0x0, str(&b, "tex0"));

    /* vertex arrays (count word + data, as the game skips 4 bytes) */
    pos = alloc(&b, 4 + 3 * 12);
    w32(&b, pos, 3);
    for (i = 0; i < 3; i++) {
        wf(&b, pos + 4 + i * 12 + 0, kPos[i][0]);
        wf(&b, pos + 4 + i * 12 + 4, kPos[i][1]);
        wf(&b, pos + 4 + i * 12 + 8, kPos[i][2]);
    }
    nrm = alloc(&b, 4 + 3);
    clr = alloc(&b, 4 + 4);
    tc = alloc(&b, 4 + 8);
    ptr(&b, vcd + 0x0, pos);
    ptr(&b, vcd + 0x4, nrm);
    w32(&b, vcd + 0x8, 1);
    ptr(&b, vcd + 0xC, clr);
    w32(&b, vcd + 0x14, 1);
    ptr(&b, vcd + 0x18, tc);
    w32(&b, vcd + 0x44, 2);
    w32(&b, vcd + 0x48, 5);

    /* packed mesh: one GX display list (pos + nrm u16 indices -> stride 4) */
    dl = alloc(&b, 3 + 3 * 4 + 1);
    D(&b, dl)[0] = 0x98;
    w16(&b, dl + 1, 3);
    for (i = 0; i < 3; i++) {
        w16(&b, dl + 3 + i * 4, (uint16_t)i);
        w16(&b, dl + 5 + i * 4, (uint16_t)(2 - i));
    }
    D(&b, mesh)[3] = 1;
    w32(&b, mesh + 4, 1);
    w32(&b, mesh + 8, 0x3);
    ptr(&b, mesh + 0xC, vcd);
    ptr(&b, mesh + 0x10, dl);
    w32(&b, mesh + 0x14, 16);

    /* unpacked mesh: MapRawDL with 3 vertex records */
    raw = alloc(&b, 4 + 3 * 0x18);
    w32(&b, raw, 3);
    for (i = 0; i < 3; i++) {
        w16(&b, raw + 4 + i * 0x18 + 0, (uint16_t)i);
        w16(&b, raw + 4 + i * 0x18 + 2, (uint16_t)(10 + i));
        w16(&b, raw + 4 + i * 0x18 + 8, 0xFFFF); /* tex[0] = -1 */
    }
    D(&b, rawMesh)[3] = 0;
    w32(&b, rawMesh + 4, 1);
    ptr(&b, rawMesh + 0xC, vcd);
    ptr(&b, rawMesh + 0x10, raw);

    /* texture_table */
    texTbl = alloc(&b, 8);
    w32(&b, texTbl, 1);
    ptr(&b, texTbl + 4, str(&b, "tex0"));
    /* material_name_table */
    nameTbl = alloc(&b, 4 + 8);
    w32(&b, nameTbl, 1);
    ptr(&b, nameTbl + 4, str(&b, "mat0"));
    ptr(&b, nameTbl + 8, mat);
    /* light_table */
    light = alloc(&b, 0x44);
    lightTbl = alloc(&b, 8);
    w32(&b, lightTbl, 1);
    ptr(&b, lightTbl + 4, light);
    ptr(&b, light + 0x0, str(&b, "L0"));
    ptr(&b, light + 0x4, str(&b, "pointLight"));
    wf(&b, light + 0x8, 1.0f); wf(&b, light + 0xC, 2.0f); wf(&b, light + 0x10, 3.0f);
    wf(&b, light + 0x14, 45.0f);
    D(&b, light)[0x2C] = 0xAA; D(&b, light)[0x2F] = 0xFF;
    wf(&b, light + 0x30, 30.0f);
    wf(&b, light + 0x34, 0.5f);
    w32(&b, light + 0x38, 2);
    w32(&b, light + 0x3C, 7);
    w32(&b, light + 0x40, 0x12D60D8);
    /* fog_table */
    fog = alloc(&b, 0x14);
    w32(&b, fog, 1); w32(&b, fog + 4, 2); wf(&b, fog + 8, 10.0f); wf(&b, fog + 0xC, 100.0f);
    D(&b, fog)[0x10] = 9; D(&b, fog)[0x13] = 0xFF;
    /* animation_table: joint, material and two light tracks; no alpha track */
    aHdr = alloc(&b, 0x20);
    aTbl = alloc(&b, 8);
    w32(&b, aTbl, 1);
    ptr(&b, aTbl + 4, aHdr);
    ptr(&b, aHdr + 0x0, str(&b, "anim0"));
    wf(&b, aHdr + 0x8, 30.0f);
    jt = alloc(&b, 0x5C + 2 * 0x1A8);
    jl = alloc(&b, 8);
    w32(&b, jl, 1);
    ptr(&b, jl + 4, jt);
    ptr(&b, aHdr + 0xC, jl);
    ptr(&b, jt + 0, str(&b, "c1"));
    wf(&b, jt + 4 + 6 * 4, 1.0f); /* base scale x */
    w32(&b, jt + 0x58, 2);
    wf(&b, jt + 0x5C + 0x1A8, 30.0f);                 /* keys[1].time */
    wf(&b, jt + 0x5C + 0x1A8 + 4 + 8 * 0x14, 3.5f);   /* keys[1].ch[8].value */
    w32(&b, jt + 0x5C + 4 + 0x10, 1);                  /* keys[0].ch[0].step */
    md = alloc(&b, 0x14 + 2 * 0x68);
    ml = alloc(&b, 8);
    w32(&b, ml, 1);
    ptr(&b, ml + 4, md);
    ptr(&b, aHdr + 0x10, ml);
    ptr(&b, md + 0, str(&b, "mat0"));
    w32(&b, md + 4, 0);
    wf(&b, md + 8, 0.5f);
    wf(&b, md + 0xC, 0.25f);
    w32(&b, md + 0x10, 2);
    wf(&b, md + 0x14 + 0x68 + 4 + 4 * 0x14, -2.0f); /* keys[1].ch[4].value */
    ld = alloc(&b, 8 + 0xB8);
    ll = alloc(&b, 8);
    w32(&b, ll, 1);
    ptr(&b, ll + 4, ld);
    ptr(&b, aHdr + 0x18, ll);
    ptr(&b, ld, str(&b, "L0"));
    w32(&b, ld + 4, 1);
    lc = alloc(&b, 8 + 0x68);
    lcList = alloc(&b, 8);
    w32(&b, lcList, 1);
    ptr(&b, lcList + 4, lc);
    ptr(&b, aHdr + 0x1C, lcList);
    ptr(&b, lc, str(&b, "L0"));
    w32(&b, lc + 4, 1);
    if (withExtraReloc) {
        L->extraWord = 0x20 + aHdr + 0x4; /* MapAnimHeader.unk4: not a pointer field in the spec */
        ptr(&b, aHdr + 0x4, ver);
    }

    chunk(&b, "information", info);
    chunk(&b, "texture_table", texTbl);
    chunk(&b, "material_name_table", nameTbl);
    chunk(&b, "light_table", lightTbl);
    chunk(&b, "fog_table", fog);
    chunk(&b, "vcd_table", vcd);
    chunk(&b, "animation_table", aTbl);
    chunk(&b, "curve_table", 0); /* unknown chunk: ignored */

    L->packedDL = 0x20 + dl;
    L->positions = 0x20 + pos + 4;
    L->posArray = 0x20 + pos;
    L->size = finish(&b);
    return b.f;
}

/* ------------------------------------------------------------- checks */
static char sLastError[256];
static int sErrors;
static void onError(PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    snprintf(sLastError, sizeof(sLastError), "%s", msg);
    sErrors++;
}

static int inFile(const void* p, const uint8_t* f, uint32_t size) {
    return (const uint8_t*)p >= f && (const uint8_t*)p < f + size;
}

static void checkGraph(PortMapData* d, const uint8_t* f, const Layout* L) {
    MapFileInfo* info = (MapFileInfo*)portMapChunk(d, "information");
    MapTexTable* tt = (MapTexTable*)portMapChunk(d, "texture_table");
    MapMaterialNameTable* nt = (MapMaterialNameTable*)portMapChunk(d, "material_name_table");
    MapFileLightTable* lt = (MapFileLightTable*)portMapChunk(d, "light_table");
    MapFileFog* fog = (MapFileFog*)portMapChunk(d, "fog_table");
    MapVcdTable* vcd = (MapVcdTable*)portMapChunk(d, "vcd_table");
    MapAnimTable* at = (MapAnimTable*)portMapChunk(d, "animation_table");
    MapFileJoint *root, *child, *hit;
    MapMaterial* mat;
    MapFileMesh *mesh, *raw;

    CHECK(info && tt && nt && lt && fog && vcd && at);
    CHECK(portMapChunk(d, "curve_table") == NULL);
    if (!info || !tt || !nt || !lt || !fog || !vcd || !at) {
        return;
    }
    /* a pointer to data+0 is a pointer, not NULL */
    CHECK(info->version == (const char*)f + 0x20);
    CHECK(!strcmp(info->version, "ver1.02"));
    CHECK(!strcmp(info->mapName, "S") && !strcmp(info->hitName, "A"));

    root = info->joint;
    CHECK(root && !strcmp(root->name, "S") && !strcmp(root->type, "null"));
    CHECK(root->parent == NULL && root->prev == NULL);
    CHECK(root->scale.x == 1.0f && root->scale.z == 1.0f && root->translation.y == 0.0f);
    CHECK(root->partCount == 0 && root->drawMode && root->drawMode->cullMode == 1);
    child = root->child;
    hit = root->next;
    CHECK(child && child->parent == root && !strcmp(child->name, "c1") && child->next == NULL);
    CHECK(hit && hit->prev == root && !strcmp(hit->name, "A") && hit->drawMode == root->drawMode);
    CHECK(child->rotation.y == 90.0f && child->rotation.z == 180.0f);
    CHECK(child->translation.x == -10.0f && child->translation.z == -8.0f);
    CHECK(child->bboxMin.y == -2.0f && child->bboxMax.z == 3.0f);
    CHECK(child->drawMode->blendType == 3 && child->drawMode->cullMode == 2 && child->drawMode->modeFlags == 9);
    CHECK(child->drawMode->lightMask == 0x80000001u && child->drawMode->hitAttributes == 0x12345678);
    CHECK(child->partCount == 1 && hit->partCount == 1);

    /* material shared between both joints and the name table */
    mat = (MapMaterial*)child->parts[0].material;
    CHECK(mat && mat == hit->parts[0].material && mat == nt->entries[0].data);
    CHECK(nt->count == 1 && !strcmp(nt->entries[0].name, "mat0"));
    CHECK(!strcmp(mat->name, "mat0") && mat->color.r == 0x11 && mat->color.a == 0x44);
    CHECK(mat->lightMode == 1 && mat->blendMode == 2 && mat->samplerCount == 1);
    CHECK(mat->samplers[0] && mat->samplers[1] == NULL && mat->samplers[7] == NULL);
    CHECK(mat->samplers[0]->wrapS == 1 && mat->samplers[0]->wrapT == 2 && mat->samplers[0]->blendMode == 1);
    CHECK(mat->samplers[0]->pad4[0] == 0xCA && mat->samplers[0]->pad4[3] == 0x0D); /* bytes, kept in file order */
    CHECK(mat->samplers[0]->tex && !strcmp(mat->samplers[0]->tex->name, "tex0") && mat->samplers[0]->tex->data == NULL);
    CHECK(mat->xforms[0].sx == 1.0f && mat->xforms[0].cx == 0.75f && mat->xforms[1].sx == 0.0f);
    CHECK(mat->blendColor.r == 0xFE && mat->blendColor.a == 0x80);
    CHECK(mat->tevMode && inFile(mat->tevMode, f, L->size) && *mat->tevMode == 7);
    CHECK(tt->count == 1 && !strcmp(tt->names[0], "tex0"));

    /* packed mesh: display list stays big-endian in the file */
    mesh = child->parts[0].mesh;
    CHECK(mesh && mesh->isPackedDisplay == 1 && mesh->displayListCount == 1 && mesh->elementMask == 3);
    CHECK(mesh->vcdTable == vcd); /* the vcd_table chunk and the mesh share one node */
    CHECK(mesh->displayLists.packedLists[0].offset == f + L->packedDL);
    CHECK(mesh->displayLists.packedLists[0].size == 16);
    CHECK(portMapLoadU16((uint8_t*)mesh->displayLists.packedLists[0].offset + 1, 1) == 3);
    /* unpacked mesh: vertex records decoded */
    raw = hit->parts[0].mesh;
    CHECK(raw && raw->isPackedDisplay == 0 && raw->displayListCount == 1 && raw->vcdTable == vcd);
    CHECK(raw->displayLists.normalLists[0] && raw->displayLists.normalLists[0]->count == 3);
    CHECK(raw->displayLists.normalLists[0]->vtx[2].pos == 2 && raw->displayLists.normalLists[0]->vtx[1].nrm == 11);
    CHECK(raw->displayLists.normalLists[0]->vtx[0].tex[0] == -1);
    /* hitdrv reads the raw list with GC offsets: count at 0, records of 0x18 from 4 */
    CHECK(*(s16*)((uint8_t*)raw->displayLists.normalLists[0] + 2 * 0x18 + 4) == 2);

    /* vcd table */
    CHECK(vcd->positions == f + L->posArray && vcd->normals && inFile(vcd->normals, f, L->size));
    CHECK(vcd->colors[0] && vcd->colors[1] == NULL);
    CHECK(vcd->texCount == 1 && vcd->texCoords[0] && vcd->texCoords[1] == NULL);
    CHECK(vcd->posFrac == 2 && vcd->texFrac[0] == 5);

    /* lights, fog */
    CHECK(lt->count == 1 && lt->lights[0] && !strcmp(lt->lights[0]->name, "L0"));
    CHECK(!strcmp(lt->lights[0]->type, "pointLight") && lt->lights[0]->position.z == 3.0f);
    CHECK(lt->lights[0]->rotation.x == 45.0f && lt->lights[0]->color.r == 0xAA && lt->lights[0]->color.a == 0xFF);
    CHECK(lt->lights[0]->spotAngle == 30.0f && lt->lights[0]->attenAngle == 0.5f && lt->lights[0]->attenType == 2);
    CHECK(lt->lights[0]->magic == 0x12D60D8 && lt->lights[0]->lflags == 7);
    CHECK(fog->enable == 1 && fog->type == 2 && fog->start == 10.0f && fog->end == 100.0f);
    CHECK(fog->color.r == 9 && fog->color.a == 0xFF);

    /* animation */
    CHECK(at->count == 1 && at->anims[0] && !strcmp(at->anims[0]->name, "anim0") && at->anims[0]->length == 30.0f);
    if (at->count == 1 && at->anims[0]) {
        MapAnimHeader* h = at->anims[0];
        CHECK(h->alphaAnims == NULL);
        CHECK(JOINTS(h) && JOINTS(h)->count == 1 && !strcmp(JOINTS(h)->items[0]->name, "c1"));
        CHECK(JOINTS(h)->items[0]->base[6] == 1.0f && JOINTS(h)->items[0]->keyCount == 2);
        CHECK(JOINTS(h)->items[0]->keys[1].time == 30.0f && JOINTS(h)->items[0]->keys[1].ch[8].value == 3.5f);
        CHECK(JOINTS(h)->items[0]->keys[0].ch[0].step == 1);
        CHECK(MATS(h) && MATS(h)->count == 1 && !strcmp(MATS(h)->items[0]->name, "mat0"));
        CHECK(MATS(h)->items[0]->u == 0.5f && MATS(h)->items[0]->v == 0.25f);
        CHECK(MATS(h)->items[0]->keyCount == 2 && MATS(h)->items[0]->keys[1].ch[4].value == -2.0f);
        CHECK(LIGHTS(h) && LIGHTS(h)->items[0]->keyCount == 1 && !strcmp(LIGHTS(h)->items[0]->name, "L0"));
        CHECK(LIGHTPARAMS(h) && LIGHTPARAMS(h)->items[0]->keyCount == 1);
    }
}

static void testDecode(void) {
    Layout L;
    uint8_t* f = buildMap(&L, 0);
    uint8_t* copy;
    PortMapData *d, *d2;
    uint32_t uncovered[4];
    MapFileInfo* info;

    portMapErrorHook = onError;
    sErrors = 0;
    CHECK(portMapDecode(f, L.size + 4, "size-mismatch") == NULL && sErrors == 0); /* GC skips the header too */
    d = portMapDecode(f, L.size, "synthetic.map");
    CHECK(d != NULL && sErrors == 0);
    if (!d) {
        fprintf(stderr, "decode failed: %s\n", sLastError);
        free(f);
        return;
    }
    CHECK(portBE32(f + 0x14) == PORT_MAP_RELOC_MARK);
    CHECK(portMapUncoveredRelocs(d, uncovered, 4) == 0);
    CHECK(portMapRelocCount(d) > 50);
    checkGraph(d, f, &L);

    /* Idempotent: decoding the already relocated file (archive-resident maps
     * are loaded again) does not relocate twice. */
    d2 = portMapDecode(f, L.size, "again.map");
    CHECK(d2 != NULL);
    if (d2) {
        checkGraph(d2, f, &L);
        portMapRelease(d2);
    }

    /* hitdrv accessors: positions in the file are big-endian */
    {
        float v[3];
        int16_t s = 0x1234;
        portGXRegisterDiscBlob(f, L.size);
        CHECK(portMapIsDiscData(f + L.positions));
        CHECK(!portMapIsDiscData(&s));
        portMapLoadVec(f + L.positions, 1, portMapIsDiscData(f + L.positions), v);
        CHECK(v[0] == kPos[1][0] && v[1] == kPos[1][1] && v[2] == kPos[1][2]);
        portMapLoadVec(&kPos[0][0], 2, 0, v);
        CHECK(v[0] == 100.0f && v[2] == 0.5f);
        CHECK(portMapLoadS16(&s, 0) == 0x1234);
        CHECK(portMapLoadU16(f + L.packedDL + 3 + 4, 1) == 1);
        portGXUnregisterDiscBlob(f);
        CHECK(!portMapIsDiscData(f + L.positions));
    }

    /* ARAM round trip: the file is copied elsewhere; the native graph stays
     * and only pointers into the file move. */
    info = (MapFileInfo*)portMapChunk(d, "information");
    copy = (uint8_t*)malloc(CAP);
    memcpy(copy, f, CAP);
    memset(f, 0xDD, CAP); /* the old buffer is reused by the battle map */
    portMapRebase(d, copy);
    CHECK(portMapFile(d) == copy);
    CHECK(portMapChunk(d, "information") == info);
    checkGraph(d, copy, &L);
    portMapRelease(d);
    free(copy);
    free(f);
}

static void testCoverageAndErrors(void) {
    Layout L;
    uint8_t* f = buildMap(&L, 1);
    PortMapData* d;
    uint32_t uncovered[4] = {0};

    portMapErrorHook = onError;
    sErrors = 0;
    d = portMapDecode(f, L.size, "extra-reloc.map");
    CHECK(d != NULL && sErrors == 0);
    CHECK(portMapUncoveredRelocs(d, uncovered, 4) == 1 && uncovered[0] == L.extraWord);
    portMapRelease(d);
    free(f);

    /* a relocation outside the data is an error */
    f = buildMap(&L, 0);
    {
        uint32_t dataSize = portBE32(f + 4);
        portPutBE32(f + 0x20 + dataSize, dataSize + 0x100);
    }
    sErrors = 0;
    CHECK(portMapDecode(f, L.size, "bad-reloc.map") == NULL && sErrors == 1);
    CHECK(strstr(sLastError, "relocation") != NULL);
    free(f);

    /* a pointer into nowhere is an error */
    f = buildMap(&L, 0);
    portPutBE32(f + 0x14, PORT_MAP_RELOC_MARK); /* treat as relocated: values are used as is */
    portPutBE32(f + 0x20 + 0x8 + 0x4, 0x7FFFFFF0u); /* information.joint */
    sErrors = 0;
    CHECK(portMapDecode(f, L.size, "bad-ptr.map") == NULL && sErrors == 1);
    free(f);
    portMapErrorHook = NULL;
}

/* ------------------------------------------- TPL detach / attach (bmapLoad) */
static void testTplRoundTrip(void) {
    enum { SZ = 0x80, DESC = 0xC, TEX = 0x20, DATA = 0x60 };
    uint8_t* t = (uint8_t*)calloc(1, SZ);
    uint8_t* copy = (uint8_t*)calloc(1, SZ);
    uint8_t* other = NULL;
    TEXPalette* pal;
    void* h;
    portPutBE32(t, PORT_TPL_VERSION);
    portPutBE32(t + 4, 1);
    portPutBE32(t + 8, DESC);
    portPutBE32(t + DESC, TEX);
    portPutBE16(t + TEX, 4);
    portPutBE16(t + TEX + 2, 8);
    portPutBE32(t + TEX + 4, 1);
    portPutBE32(t + TEX + 8, DATA);
    t[DATA] = 0x5A;
    pal = portTplDecode(t, SZ, "map.tpl");
    CHECK(pal == (TEXPalette*)t && pal->descriptorArray[0].textureHeader->data == t + DATA);
    /* bmapLoad: park the graph, copy the blob out, free it */
    h = portTplDetach(t);
    CHECK(h != NULL);
    memcpy(copy, t, SZ);
    /* a battle TPL decoded at the same address must not free the parked graph */
    memset(t, 0, SZ);
    portPutBE32(t, PORT_TPL_VERSION);
    portPutBE32(t + 4, 0);
    portPutBE32(t + 8, 0);
    other = (uint8_t*)portTplDecode(t, SZ, "battle.tpl");
    CHECK(other == t);
    portTplRelease(t);
    /* bmapUnLoad: copy back somewhere else, reattach */
    portTplAttach(h, copy);
    pal = (TEXPalette*)copy;
    CHECK(pal->numDescriptors == 1 && pal->descriptorArray[0].textureHeader->data == copy + DATA);
    CHECK(*(uint8_t*)pal->descriptorArray[0].textureHeader->data == 0x5A);
    CHECK(portTplDecode(copy, SZ, "map.tpl") == pal); /* registered, native root */
    portTplRelease(copy);
    portTplAttach(NULL, copy); /* no-op */
    free(t);
    free(copy);
}

int main(void) {
    testDecode();
    testCoverageAndErrors();
    testTplRoundTrip();
    printf("map_test: %d checks, %d failures\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
