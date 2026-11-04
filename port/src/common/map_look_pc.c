/* Map looks (port/include/port/map_look.h): out = colour * mul + add after a
 * map object's material, per object or for the whole map. */
#include "port/map_look.h"
#include "driver/hitdrv.h"
#include "driver/mapdrv.h"
#include "driver/mobjdrv.h"
#include "mario/mariost.h"
#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOOKS 128

typedef struct Look {
    const MapObject* obj;
    GXColor mul, add;
} Look;

void* portMapLookObj;
static Look sLooks[LOOKS];
static int sCount;
static int sMapOn;
static GXColor sMapMul, sMapAdd;
static char sMap[16];

static GXColor rgb(unsigned int c) {
    return (GXColor){(u8)(c >> 16), (u8)(c >> 8), (u8)c, 0xFF};
}

static void checkMap(void) {
    if (gp != NULL && strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        sCount = 0;
        sMapOn = 0;
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
    }
}

void portMapLookClear(void) {
    sCount = 0;
    sMapOn = 0;
}

static void setOne(MapObject* obj, GXColor mul, GXColor add, int off) {
    obj->flags &= ~MAPOBJ_FLAG_DISPLAY_LIST; /* drawn through mapSetMaterialTev, not its recorded list */
    int i;
    for (i = 0; i < sCount; i++) {
        if (sLooks[i].obj == obj) {
            break;
        }
    }
    if (off) {
        if (i < sCount) {
            sLooks[i] = sLooks[--sCount];
        }
        return;
    }
    if (i == sCount) {
        if (sCount == LOOKS) {
            return;
        }
        sCount++;
    }
    sLooks[i].obj = obj;
    sLooks[i].mul = mul;
    sLooks[i].add = add;
}

static void setTree(MapObject* obj, GXColor mul, GXColor add, int off, int siblings) {
    for (; obj != NULL; obj = siblings ? obj->next : NULL) {
        setOne(obj, mul, add, off);
        setTree(obj->child, mul, add, off, 1);
    }
}

int portMapLookSet(const char* name, unsigned int mul, unsigned int add) {
    const int off = (mul & 0xFFFFFF) == 0xFFFFFF && (add & 0xFFFFFF) == 0;
    MapObject* obj;
    checkMap();
    if (name == NULL || name[0] == '\0') {
        MapWork* wp = mapGetWork();
        int i, j;
        for (i = 0; !off && i < wp->entries[0].count; i++) {
            for (j = 0; j < wp->entries[i].numJoints; j++) {
                wp->entries[i].objects[j].flags &= ~MAPOBJ_FLAG_DISPLAY_LIST;
            }
        }
        sMapOn = !off;
        sMapMul = rgb(mul);
        sMapAdd = rgb(add);
        return 1;
    }
    obj = mapGetMapObj(name);
    if (obj == NULL) {
        printf("[maplook] %s: no map object %s\n", gp->currentMapName, name);
        fflush(stdout);
        return 0;
    }
    setTree(obj, rgb(mul), rgb(add), off, 0);
    return 1;
}

void* portMapLookMobj;

int portMapLookSetMobj(const char* name, unsigned int mul, unsigned int add) {
    const int off = (mul & 0xFFFFFF) == 0xFFFFFF && (add & 0xFFFFFF) == 0;
    MapObjectEntry* entry;
    int i;
    checkMap();
    entry = name != NULL ? mobjNameToPtrNoAssert(name) : NULL;
    if (entry == NULL) {
        printf("[maplook] %s: no mobj %s\n", gp->currentMapName, name != NULL ? name : "(null)");
        fflush(stdout);
        return 0;
    }
    /* keyed by the entry's address, in the same table (it never equals a MapObject's) */
    for (i = 0; i < sCount; i++) {
        if (sLooks[i].obj == (const MapObject*)entry) {
            break;
        }
    }
    if (off) {
        if (i < sCount) {
            sLooks[i] = sLooks[--sCount];
        }
        return 1;
    }
    if (i == sCount) {
        if (sCount == LOOKS) {
            return 0;
        }
        sCount++;
    }
    sLooks[i].obj = (const MapObject*)entry;
    sLooks[i].mul = rgb(mul);
    sLooks[i].add = rgb(add);
    return 1;
}

void portMapLookDump(void) {
    static char done[16];
    MapWork* wp;
    int i, j;
    if (getenv("TTYD_MAPOBJ_DUMP") == NULL || gp == NULL || strcmp(done, gp->currentMapName) == 0) {
        return;
    }
    snprintf(done, sizeof(done), "%s", gp->currentMapName);
    wp = mapGetWork();
    for (i = 0; i < wp->entries[0].count; i++) {
        MapEntry* entry = &wp->entries[i];
        for (j = 0; j < entry->numJoints; j++) {
            const MapObject* o = &entry->objects[j];
            /* its bounding box's centre, in the world (placing things in a borrowed room) */
            const MapFileJoint* jt = o->joints;
            const f32 cx = (jt->bboxMin.x + jt->bboxMax.x) * 0.5f, cy = (jt->bboxMin.y + jt->bboxMax.y) * 0.5f,
                      cz = (jt->bboxMin.z + jt->bboxMax.z) * 0.5f;
            const f32(*mw)[4] = o->modelWorldMtx;
            printf("[mapobj] %s %s%s%s flags %08x at %.0f %.0f %.0f size %.0f %.0f %.0f\n", gp->currentMapName,
                   o->joints->name, o->parent != NULL ? " in " : "", o->parent != NULL ? o->parent->joints->name : "",
                   (unsigned)o->flags, mw[0][0] * cx + mw[0][1] * cy + mw[0][2] * cz + mw[0][3],
                   mw[1][0] * cx + mw[1][1] * cy + mw[1][2] * cz + mw[1][3],
                   mw[2][0] * cx + mw[2][1] * cy + mw[2][2] * cz + mw[2][3], jt->bboxMax.x - jt->bboxMin.x,
                   jt->bboxMax.y - jt->bboxMin.y, jt->bboxMax.z - jt->bboxMin.z);
        }
        /* the collision objects, with their attributes (a wall only Tube Mode passes, say) */
        for (j = 0; j < entry->hitNumJoints; j++) {
            const HitEntry* h = &entry->hitEntries[j];
            if (h->joint != NULL && h->vectorCount > 0) {
                f32 lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                int v, k, d;
                for (v = 0; v < h->vectorCount; v++) {
                    const Vec* tri[3] = {&h->vectors[v].v0, &h->vectors[v].v1, &h->vectors[v].v2};
                    if (strcmp(getenv("TTYD_MAPOBJ_DUMP"), "tris") == 0) { /* every triangle: a room's real floors */
                        printf("[tri] %s %s %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f n %.3f\n", gp->currentMapName, h->joint->name,
                               tri[0]->x, tri[0]->y, tri[0]->z, tri[1]->x, tri[1]->y, tri[1]->z, tri[2]->x, tri[2]->y, tri[2]->z,
                               h->vectors[v].normal.y);
                    }
                    for (k = 0; k < 3; k++) {
                        const f32 c[3] = {tri[k]->x, tri[k]->y, tri[k]->z};
                        for (d = 0; d < 3; d++) {
                            lo[d] = c[d] < lo[d] ? c[d] : lo[d];
                            hi[d] = c[d] > hi[d] ? c[d] : hi[d];
                        }
                    }
                }
                printf("[hit] %s %s attr %08x flags %04x x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n", gp->currentMapName,
                       h->joint->name, (unsigned)h->attributes, (unsigned)h->flags, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
            }
        }
    }
    fflush(stdout);
}

int portMapLookTev(void* objp, int stage) {
    const MapObject* obj = (const MapObject*)objp;
    GXColor mul, add;
    int i, mobj = 0;
    if (obj == NULL && portMapLookMobj != NULL) { /* a mobj's model: its own look only */
        obj = (const MapObject*)portMapLookMobj;
        mobj = 1;
    }
    if (obj == NULL || (sCount == 0 && !sMapOn)) {
        return stage;
    }
    checkMap();
    for (i = 0; i < sCount; i++) {
        if (sLooks[i].obj == obj) {
            break;
        }
    }
    if (i < sCount) {
        mul = sLooks[i].mul;
        add = sLooks[i].add;
    } else if (sMapOn && !mobj) {
        mul = sMapMul;
        add = sMapAdd;
    } else {
        return stage;
    }
    if (stage > 14) {
        return stage;
    }
    if (mul.r != 0xFF || mul.g != 0xFF || mul.b != 0xFF) {
        /* colour * mul: d + (1 - c) a + c b with a 0, b the colour, c the konst */
        GXSetTevOrder((GXTevStageID)stage, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
        GXSetTevColorIn((GXTevStageID)stage, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
        GXSetTevColorOp((GXTevStageID)stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaIn((GXTevStageID)stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        GXSetTevAlphaOp((GXTevStageID)stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevSwapMode((GXTevStageID)stage, GX_TEV_SWAP0, GX_TEV_SWAP0);
        GXSetTevKColor(GX_KCOLOR2, mul);
        GXSetTevKColorSel((GXTevStageID)stage, GX_TEV_KCSEL_K2);
        stage++;
    }
    if (add.r != 0 || add.g != 0 || add.b != 0) {
        /* + add: d the colour, a the konst, c 0 */
        GXSetTevOrder((GXTevStageID)stage, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
        GXSetTevColorIn((GXTevStageID)stage, GX_CC_KONST, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
        GXSetTevColorOp((GXTevStageID)stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaIn((GXTevStageID)stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        GXSetTevAlphaOp((GXTevStageID)stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevSwapMode((GXTevStageID)stage, GX_TEV_SWAP0, GX_TEV_SWAP0);
        GXSetTevKColor(GX_KCOLOR3, add);
        GXSetTevKColorSel((GXTevStageID)stage, GX_TEV_KCSEL_K3);
        stage++;
    }
    return stage;
}
