/* Paper notes in the room (port/include/port/notes.h): each note the relay
 * kept for this map drawn on the ground as a small folded sheet, and the
 * nearest one's text over it once Mario stands by it. Only drawn: nothing
 * of the game is read but where Mario and the camera are, nothing changed,
 * and the font's random numbers are put back after the text (as
 * second_player_pc.c's tags). */
#include "port/notes.h"
#include "port/platform.h"
#include "port/view.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NOTES_MAX 16
#define NOTE_SEEN 700.0f /* drawn this near Mario */
#define NOTE_READ 70.0f  /* its text shown this near */

extern BinaryFontHeader* pfh; /* fontmgr.c: the font, NULL until the logos load it */

static PortNote sNotes[NOTES_MAX];
static int sCount;
static int sNear = -1; /* the note Mario stands by, or -1 */
static int sAge;       /* frames the near one has been near (its text fades in) */
static char sNearId[16];

static void flatStart(void) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXCopy(camGetPtr(CAMERA_2D)->view, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static void quad(const Vec* p) {
    int i;
    const float m = portViewMirrored() ? -1.0f : 1.0f;
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    for (i = 0; i < 4; i++) {
        GXPosition3f32(p[i].x * m, p[i].y, 0.0f);
    }
}

static void tev(GXColor c) {
    GXSetTevColor(GX_TEVREG0, c);
}

/* a sheet lying on the ground at a world spot: its corners through the 3D
 * camera, so it lies flat in the room; a shadow, an edge, the paper, a fold */
static void sheetDraw(const PortNote* n, float lift) {
    static const float kCorner[4][2] = {{-11.0f, -8.0f}, {11.0f, -8.0f}, {11.0f, 8.0f}, {-11.0f, 8.0f}};
    Vec at, s[4], e[4], shadow[4];
    const GXColor edge = n->mine ? (GXColor){0x50, 0x90, 0xE0, 0xFF}
                         : n->score >= 3 ? (GXColor){0xE0, 0xB0, 0x30, 0xFF}
                                         : (GXColor){0xB0, 0x90, 0x70, 0xFF};
    int i;
    for (i = 0; i < 4; i++) {
        at.x = (float)n->x + kCorner[i][0] * 1.25f;
        at.y = (float)n->y + 0.5f;
        at.z = (float)n->z + kCorner[i][1] * 1.25f;
        getScreenPoint(&at, &e[i]);
        at.x = (float)n->x + kCorner[i][0];
        at.y = (float)n->y + 0.5f + lift;
        at.z = (float)n->z + kCorner[i][1];
        getScreenPoint(&at, &s[i]);
        shadow[i] = e[i];
        shadow[i].x += 2.0f;
        shadow[i].y -= 2.0f;
    }
    tev((GXColor){0x20, 0x18, 0x10, 0x50});
    quad(shadow);
    tev(edge);
    quad(e);
    tev((GXColor){0xFF, 0xFC, 0xF0, 0xFF});
    quad(s);
    /* the fold across the middle */
    {
        Vec fold[4];
        fold[0] = s[0];
        fold[1] = s[1];
        fold[2].x = (s[1].x + s[2].x) * 0.5f;
        fold[2].y = (s[1].y + s[2].y) * 0.5f;
        fold[3].x = (s[0].x + s[3].x) * 0.5f;
        fold[3].y = (s[0].y + s[3].y) * 0.5f;
        tev((GXColor){0xE4, 0xDC, 0xCC, 0xFF});
        quad(fold);
    }
}

static void textDraw(const PortNote* n, float alpha) {
    char text[160], score[16];
    Vec at, screen;
    float w, scale = 0.7f;
    const u8 a = (u8)(0xFF * alpha);
    portNoteText(n->phrase, n->word, 1, text, sizeof(text));
    snprintf(score, sizeof(score), "%+d", n->score);
    at.x = (float)n->x;
    at.y = (float)n->y + 34.0f;
    at.z = (float)n->z;
    getScreenPoint(&at, &screen);
    if (portViewMirrored()) {
        screen.x = -screen.x;
    }
    w = FontGetMessageWidth(text) * scale;
    if (w > 420.0f) {
        scale *= 420.0f / w;
        w = 420.0f;
    }
    FontDrawStart_alpha(a);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xF4, 0xD8, a});
    FontDrawScale(scale);
    FontDrawString(screen.x - w * 0.5f, screen.y + 22.0f, text);
    FontDrawColor(n->score > 0 ? (GXColor){0xF0, 0xC8, 0x50, a} : (GXColor){0xC0, 0xC0, 0xC0, a});
    FontDrawScale(0.55f);
    w = FontGetMessageWidth(score) * 0.55f;
    FontDrawString(screen.x - w * 0.5f, screen.y + 2.0f, score);
}

static void notesDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    int i;
    (void)camId;
    (void)param;
    flatStart();
    for (i = 0; i < sCount; i++) {
        sheetDraw(&sNotes[i], i == sNear ? 2.0f + sinf((float)sAge * 0.1f) * 1.5f : 0.0f);
    }
    if (sNear >= 0 && pfh != NULL) {
        textDraw(&sNotes[sNear], sAge < 15 ? (float)sAge / 15.0f : 1.0f);
    }
    portRandSetState(seed);
}

/* in front of the 3D camera */
static int inView(const PortNote* n) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    const float z = cam->view[2][0] * (float)n->x + cam->view[2][1] * (float)n->y + cam->view[2][2] * (float)n->z +
                    cam->view[2][3];
    return z < -1.0f;
}

void portNotesFrame(void) {
    MarioWork* mario;
    int i, total, near = -1;
    float best = NOTE_READ * NOTE_READ, dist[NOTES_MAX];
    if (gp == NULL || !portNotesOn() || seqGetSeq() != SEQ_GAME || gp->inBattle || (mario = marioGetPtr()) == NULL) {
        sCount = 0;
        sNear = -1;
        return;
    }
    portNotesWant(gp->currentMapName);
    total = portNotesCount(gp->currentMapName);
    sCount = 0;
    for (i = 0; i < total; i++) { /* the nearest NOTES_MAX in view of all the room's */
        PortNote n;
        float dx, dz, d;
        int k, at;
        if (!portNotesGet(gp->currentMapName, i, &n)) {
            break;
        }
        dx = (float)n.x - mario->position.x;
        dz = (float)n.z - mario->position.z;
        d = dx * dx + dz * dz;
        if (d > NOTE_SEEN * NOTE_SEEN || !inView(&n)) {
            continue;
        }
        at = sCount;
        if (sCount == NOTES_MAX) { /* full: in the farthest one's place, when nearer */
            for (at = 0, k = 1; k < NOTES_MAX; k++) {
                if (dist[k] > dist[at]) {
                    at = k;
                }
            }
            if (dist[at] <= d) {
                continue;
            }
        } else {
            sCount++;
        }
        sNotes[at] = n;
        dist[at] = d;
    }
    for (i = 0; i < sCount; i++) {
        if (dist[i] < best && fabsf((float)sNotes[i].y - mario->position.y) < 80.0f) {
            best = dist[i];
            near = i;
        }
    }
    if (near >= 0 && strcmp(sNotes[near].id, sNearId) == 0) {
        sAge++;
    } else {
        sAge = 0;
    }
    snprintf(sNearId, sizeof(sNearId), "%s", near >= 0 ? sNotes[near].id : "");
    sNear = near;
    if (sCount > 0) {
        dispEntry(CAMERA_2D, 900.0f, 1, notesDisp, NULL);
    }
}
