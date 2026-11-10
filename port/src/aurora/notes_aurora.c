/* Paper notes in the room (port/include/port/notes.h, docs/PAPER_NOTES.md):
 * each note the relay kept for this map stands on the ground as a written
 * sheet, as the game draws an item lying in the field: the game's own Vital
 * Paper icon (portNoteIcon), lifted a little off the ground and leaning back
 * toward the camera, bobbing and swaying gently, with a soft shadow under it
 * and now and then a glint at its corner. Gold once rated +3 or more, blue for
 * this device's own. No mark over it: a "!" over Mario is the game's "press A
 * here", and a note's is not one.
 *
 * Standing by one (a reach measured across the screen and into it, wider for
 * a bigger pick: Bowser's), its text comes up over it in the game's speech
 * balloon (windowdrv.c's), its tail at the note, with "{NOTE}: Read" under
 * it: the notes' own button (Controls > Bindings' "Read a paper note": C on
 * the keyboard, a controller's left shoulder, LB or L1, and the game's L,
 * which Mario doesn't use in the field: a GameCube controller's), drawn as
 * the controller in hand shows it. A is never a note's: by a note it jumps
 * (or talks, checks, opens a door) as anywhere else. On a touch screen (and
 * with the mouse) a tap on the sheet or its balloon opens it. Opened, the
 * balloon grows its rating and "Rate up / Rate down / Close" (the Start
 * menu's rating, for that one note: A or a tap chooses, B or the notes'
 * button again closes), Mario and the room's NPCs held meanwhile.
 *
 * It never clips into a fence or a wall it was left against: it stands a
 * little toward the camera from where it was left, and is drawn pulled
 * toward the camera along its line of sight (pullMtx: the same on the
 * screen, nearer in depth), so only what stands well in front of it hides it.
 * Nor behind the one playing (Bowser is wide): a note right behind them
 * slides out to their side while they stand there.
 *
 * Drawn only, and opened by a button the game has no use for there: nothing
 * else of the game is read but where Mario and the camera are, and the
 * font's random numbers are put back after the text (as second_player_pc.c's
 * tags). So online co-op shows the notes too, each game its own, and there
 * none opens (no "Read" under it: the games stay the same); a replay shows
 * none (portNotesOn). The near one's balloon makes way while the game holds
 * Mario (a talk, a sign, a scene). */
#define PORT_NOTES_DRAWING
#include "port/notes.h"
#include "port/bindings.h"
#include "port/button_markup.h"
#include "port/luigi.h"
#include "port/menu_tap.h"
#include "port/player_menu.h"
#include "port/platform.h"
#include "port/statesync.h"
#include "port/tips.h"
#include "port/tr.h"
#include "port/view.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/windowdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include "pmario_sound.h"
#include "system.h"
#include "wide_x.h"
#include "port/menu_tap.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTES_MAX 16
#define NOTE_SEEN 700.0f   /* drawn this near Mario */
#define NOTE_READ_X 30.0f  /* its text shown this far across the screen from the one playing's edge */
#define NOTE_READ_Z 56.0f  /* and this far into it (the eye judges depth worst) */
#define NOTE_READ_Y 80.0f  /* and this far up or down */
#define NOTE_LIFT 6.0f     /* the letter's foot this far off the ground */
#define NOTE_TOWARD 14.0f  /* and this far toward the camera from where it was left (off a fence or wall) */
#define NOTE_SCALE 0.9f    /* the icon's size (1: a field item's) */
#define NOTE_PULL 40.0f    /* drawn this much nearer the camera (pullMtx) */
#define NOTE_LEAN 18.0f    /* degrees leaning back, its face up toward the camera */
#define NOTE_HIDDEN 30.0f  /* a note this far or more behind the one playing (and across them) slides out */
#define GLINT_EVERY 170    /* frames between a note's glints */
#define GLINT_LEN 26       /* a glint's frames */

extern BinaryFontHeader* pfh; /* fontmgr.c: the font, NULL until the logos load it */

static PortNote sNotes[NOTES_MAX];
static Vec sAt[NOTES_MAX]; /* where each is drawn this frame (standAt, slid out from behind the one playing) */
static int sSlidOut[NOTES_MAX]; /* (slid out of the way more than a little: for the tests' log) */
static int sCount;
static int sNear = -1; /* the note Mario stands by, or -1 */
static int sAge;       /* frames the near one has been near (its text fades in) */
static int sBusy;      /* the game holds Mario (a talk, an event): the near one's balloon stays away */
static char sNearId[16];
static unsigned sTick; /* frames drawn (the bob, the sway, the glint) */

/* a note slid out from behind the one playing: by id (the list is made again each frame), eased */
static struct {
    char id[16];
    float shift;
    unsigned seen;
} sSlide[NOTES_MAX];

/* the note opened (the notes' button, a tap): its own copy, kept while the view is up */
enum { OPT_UP, OPT_DOWN, OPT_CLOSE, OPT_COUNT };
static struct {
    int open;
    PortNote note;
    int age;         /* frames open */
    int cursor;      /* OPT_* */
    int asked;       /* a rating sent, waiting for its answer */
    int serial;      /* portNotesAnswer's serial when it was sent */
    const char* answer; /* the answer (English, translated when drawn) */
    int held;        /* Mario and the NPCs held: given back a frame after it closes (the closing A is no jump) */
    float optX0[OPT_COUNT], optX1[OPT_COUNT], optY; /* where each choice was drawn (taps; x1 <= x0: not shown) */
    int release;     /* frames until they are */
    char heldMap[16];
} sView;

/* an A at a note, the game's as anywhere (a jump, a talk): logged once Mario's motion shows what it did */
static struct {
    int frames;
    char id[16];
} sGameA;

/* Tests ($TTYD_NOTES_TEST): what the notes drew in a frame, logged as "[notes] drawn: ..." each time it
   changes: each sheet ("sheet t0"), the balloon ("balloon t0" / "opened t0"), each text ("'Close'") and the
   arrow; so a scenario sees there is no "!" (or anything else) drawn over a note */
static char sDrawn[640], sDrawnLast[640];
static int sDrawLog = -1;

static void drawn(const char* what, const char* text) {
    size_t n;
    if (sDrawLog < 0) {
        sDrawLog = getenv("TTYD_NOTES_TEST") != NULL;
    }
    n = strlen(sDrawn);
    if (!sDrawLog || n + 4 >= sizeof(sDrawn)) {
        return;
    }
    snprintf(sDrawn + n, sizeof(sDrawn) - n, text != NULL ? "%s%s '%s'" : "%s%s", n > 0 ? "; " : "", what,
             text != NULL ? text : "");
}

/* a note's part drawn: "sheet t0" */
static void drawnOf(const char* what, const char* id) {
    char one[48];
    snprintf(one, sizeof(one), "%s %s", what, id);
    drawn(one, NULL);
}

static void drawnFlush(void) {
    if (sDrawLog > 0 && strcmp(sDrawn, sDrawnLast) != 0) {
        printf("[notes] drawn: %s\n", sDrawn[0] != '\0' ? sDrawn : "nothing");
        fflush(stdout);
        memcpy(sDrawnLast, sDrawn, sizeof(sDrawn));
    }
    sDrawn[0] = '\0';
}

/* the font's text, logged for the tests */
static void say(float x, float y, const char* text) {
    FontDrawString(x, y, text);
    drawn("text", text);
}

/* the one playing's half width on the screen, in the field's units (Bowser is broad) */
static float bodyHalf(void) {
    switch (portPlayAs()) {
        case PORT_PLAY_AS_BOWSER: return 32.0f;
        case PORT_PLAY_AS_PEACH: return 16.0f;
        default: return 14.0f;
    }
}

/* a note's own phase, so a room's notes don't bob together */
static float phaseOf(const PortNote* n) {
    unsigned h = 2166136261u;
    const char* c;
    for (c = n->id; *c != '\0'; c++) {
        h = (h ^ (unsigned char)*c) * 16777619u;
    }
    return (float)(h % 628u) * 0.01f;
}

/* across the screen (right) and into it (forward), on the ground, from the 3D camera toward p */
static void camAxes(const Vec* p, float* rx, float* rz, float* fx, float* fz) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    float x = p->x - cam->cameraPos.x, z = p->z - cam->cameraPos.z;
    const float d = sqrtf(x * x + z * z);
    if (d > 1.0f) {
        x /= d;
        z /= d;
    } else {
        x = 0.0f;
        z = -1.0f;
    }
    *fx = x;
    *fz = z;
    *rx = -z;
    *rz = x;
}

/* where it stands: where it was left, lifted, a little toward the camera on the ground */
static Vec standAt(const PortNote* n) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    Vec at;
    float tx = cam->cameraPos.x - (float)n->x, tz = cam->cameraPos.z - (float)n->z;
    const float d = sqrtf(tx * tx + tz * tz);
    at.x = (float)n->x;
    at.y = (float)n->y + NOTE_LIFT;
    at.z = (float)n->z;
    if (d > 1.0f) {
        at.x += tx / d * NOTE_TOWARD;
        at.z += tz / d * NOTE_TOWARD;
    }
    return at;
}

u16 portNoteIcon(void) {
    return itemDataTable[ITEM_VITAL_PAPER].icon_id; /* a written sheet, lined */
}

GXColor portNoteTint(int mine, int score) {
    return mine ? (GXColor){0x9C, 0xC8, 0xFF, 0xFF}
           : score >= 3 ? (GXColor){0xFF, 0xD8, 0x60, 0xFF}
                        : (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
}

static GXColor tintOf(const PortNote* n) {
    return portNoteTint(n->mine, n->score);
}

/* The note drawn toward the camera along its line of sight, smaller by as much: it looks the same, at the
 * same place on the screen, but a fence or a wall it was left right against (NOTE_PULL units or less in
 * front of it) no longer cuts into it; what stands well in front of it still hides it. P = the pull. */
static void pullMtx(Mtx p, const Vec* at) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    const Vec eye = cam->cameraPos;
    const float dx = at->x - eye.x, dy = at->y - eye.y, dz = at->z - eye.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float k = d > 1.0f ? (d - NOTE_PULL) / d : 1.0f;
    Mtx t;
    if (k < 0.3f) {
        k = 0.3f;
    }
    PSMTXTrans(p, -eye.x, -eye.y, -eye.z);
    PSMTXScale(t, k, k, k);
    PSMTXConcat(t, p, p);
    PSMTXTrans(t, eye.x, eye.y, eye.z);
    PSMTXConcat(t, p, p);
}

/* the GX state for the untextured shapes (the shadow, the glint): vertex colours, the depth tested, not written */
static void flatState(int additive) {
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, additive ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});
    GXSetCullMode(GX_CULL_NONE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetCurrentMtx(0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

/* the soft shadow on the ground: an ellipse, dark in the middle, gone at its edge */
static void shadowDraw(const PortNote* n, const Vec* at, float bob, Mtx pull) {
    const CameraEntry* cam = camGetCurPtr();
    const float k = 1.0f - bob * 0.04f;
    const float rx = 14.0f * k * NOTE_SCALE, rz = 8.0f * k * NOTE_SCALE;
    const float y = (float)n->y + 0.6f;
    const u8 a = (u8)(0x78 * k);
    Mtx m;
    int i;
    flatState(0);
    PSMTXConcat((MtxPtr)cam->view, pull, m);
    GXLoadPosMtxImm(m, 0);
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 18);
    GXPosition3f32(at->x, y, at->z);
    GXColor4u8(0x10, 0x0C, 0x08, a);
    for (i = 0; i <= 16; i++) {
        const float t = (float)i * (6.2831853f / 16.0f);
        GXPosition3f32(at->x + cosf(t) * rx, y, at->z + sinf(t) * rz);
        GXColor4u8(0x10, 0x0C, 0x08, 0);
    }
}

/* now and then a glint at the sheet's top corner: a small four-pointed star in the sheet's own plane (m: the
   sheet's, the icon's units: its foot's middle at 0, about 32 tall), added on, white fading out at its points */
static void glintDraw(Mtx m, float ph) {
    const CameraEntry* cam = camGetCurPtr();
    const unsigned t = (sTick + (unsigned)(ph * 27.0f)) % GLINT_EVERY;
    const float cx = 9.0f, cy = 25.0f, cz = 0.5f;
    float s, len, w;
    u8 a;
    Mtx v;
    int k;
    if (t >= GLINT_LEN) {
        return;
    }
    s = sinf((float)t * (3.14159265f / (float)GLINT_LEN));
    len = 4.0f + 8.0f * s;
    w = 1.4f + 0.8f * s;
    a = (u8)(0xF0 * s);
    flatState(1);
    PSMTXConcat((MtxPtr)cam->view, m, v);
    GXLoadPosMtxImm(v, 0);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 12);
    for (k = 0; k < 2; k++) { /* the across arm, then the up one: two thin diamonds */
        const float ax = k == 0 ? len : 0.0f, ay = k == 0 ? 0.0f : len;
        const float bx = k == 0 ? 0.0f : w, by = k == 0 ? w : 0.0f;
        int side;
        for (side = -1; side <= 1; side += 2) {
            GXPosition3f32(cx - ax, cy - ay, cz);
            GXColor4u8(0xFF, 0xFF, 0xF0, 0);
            GXPosition3f32(cx + bx * side, cy + by * side, cz);
            GXColor4u8(0xFF, 0xFF, 0xF0, a);
            GXPosition3f32(cx + ax, cy + ay, cz);
            GXColor4u8(0xFF, 0xFF, 0xF0, 0);
        }
    }
}

/* one note: its shadow, then the letter (the game's icon) standing on it, then its glint */
static void noteDisp(CameraId camId, void* param) {
    const int i = (int)(intptr_t)param;
    const CameraEntry* cam = camGetPtr(camId);
    const PortNote* n;
    float ph, bob, sway;
    Vec at;
    Mtx m, t, pull;
    if (i < 0 || i >= sCount) {
        return;
    }
    n = &sNotes[i];
    ph = phaseOf(n);
    at = sAt[i];
    bob = 1.0f + sinf((float)sTick * 0.06f + ph) * (i == sNear ? 2.2f : 1.4f);
    sway = sinf((float)sTick * 0.035f + ph * 1.7f) * 6.0f;
    pullMtx(pull, &at);
    shadowDraw(n, &at, bob, pull);
    PSMTXTrans(m, at.x, at.y + bob, at.z);
    PSMTXRotRad(t, 'y', -cam->viewYaw * 0.017453292f);
    PSMTXConcat(m, t, m);
    PSMTXRotRad(t, 'x', -NOTE_LEAN * 0.017453292f);
    PSMTXConcat(m, t, m);
    PSMTXRotRad(t, 'z', sway * 0.017453292f);
    PSMTXConcat(m, t, m);
    PSMTXScale(t, NOTE_SCALE, NOTE_SCALE, NOTE_SCALE);
    PSMTXConcat(m, t, m);
    PSMTXConcat(pull, m, m);
    iconDispGxCol(m, ICON_FLAG_ITEM_NO_BLEND, portNoteIcon(), tintOf(n));
    glintDraw(m, ph);
    drawnOf(sSlidOut[i] ? "sheet (slid out)" : "sheet", n->id);
}

/* a spot over a note on the screen: the 2D camera's units, as the speech balloons use */
static void screenOf(const Vec* where, float up, Vec* out) {
    Vec at = *where;
    at.y += up;
    getScreenPoint(&at, out);
    if (portViewMirrored()) {
        out->x = -out->x;
    }
}

/* the game's list arrow turned to point right, (x, y) its middle (pause_notes_aurora.c's) */
static void arrowRight(float x, float y, float scale) {
    Mtx m, t;
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXRotRad(t, 'z', -1.57079633f);
    PSMTXConcat(m, t, m);
    PSMTXScale(t, scale, scale, 1.0f);
    PSMTXConcat(m, t, m);
    PSMTXTrans(t, 0.0f, -12.0f, 0.0f);
    PSMTXConcat(m, t, m);
    iconDispGxCol(m, ICON_DISP_2D, ICON_1B5, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    drawn("arrow", NULL);
}

static const char* const kOptText[OPT_COUNT] = {"Rate up", "Rate down", "Close"};

static int sCanRead; /* a note can be opened now (portNotesFrame: canRead): "{NOTE}: Read" under the near one's text */
static struct {
    float x0, x1, y0, y1; /* the near one's balloon as drawn (taps: x1 <= x0 none) */
} sBalloon;

/* what the near one's balloon says opens it: the notes' button as the controller in hand shows it ({NOTE},
   button_markup.h); a tap on a touch screen */
static const char* readPrompt(void) {
#ifdef PORT_TOUCH_OVERLAY
    if (portTouchPlayerOne()) {
        return portTrGame("Tap: Read");
    }
#endif
    return portTrGame("{NOTE}: Read");
}

/* can this note still be rated from here (not one's own, not rated already) */
static int rateable(const PortNote* n) {
    return !n->mine && n->rated == 0;
}

/* The balloon: the near one's text, its tail at the note; opened, its rating and the choice under it */
static void balloonDraw(const PortNote* n, const Vec* where, int age, int opened) {
    const float fade = opened ? 1.0f : age < 12 ? (float)age / 12.0f : 1.0f;
    const u8 a = (u8)(0xFF * fade);
    const GXColor ink = {0x28, 0x20, 0x18, a};
    char text[160], score[16];
    const char* opt[OPT_COUNT];
    const char* prompt = !opened && sCanRead ? readPrompt() : NULL;
    float ow[OPT_COUNT], pw = 0.0f;
    float tw, sw, w, h = opened ? 104.0f : prompt != NULL ? 84.0f : 62.0f, x, y, scale = 0.8f, rowW = 0.0f;
    int k, count = 0, ids[OPT_COUNT];
    Vec s;
    portNoteText(n->phrase, n->word, 1, text, sizeof(text));
    snprintf(score, sizeof(score), "%+d", n->score);
    tw = FontGetMessageWidth(text) * scale;
    if (tw > 400.0f) {
        scale *= 400.0f / tw;
        tw = 400.0f;
    }
    sw = FontGetMessageWidth(score) * 0.72f;
    w = tw + sw + 70.0f;
    if (opened) { /* the choice: Rate up, Rate down, Close (Close alone for one's own or one rated) */
        for (k = 0; k < OPT_COUNT; k++) {
            if (k != OPT_CLOSE && (!rateable(n) || sView.asked || sView.answer != NULL)) {
                continue;
            }
            ids[count] = k;
            opt[count] = portTrGame(kOptText[k]);
            ow[count] = FontGetMessageWidth(opt[count]) * 0.72f;
            rowW += ow[count] + (count > 0 ? 34.0f : 0.0f);
            count++;
        }
        if (rowW + 96.0f > w) {
            w = rowW + 96.0f;
        }
    }
    if (prompt != NULL) { /* (its icon measured at the size it is drawn: button_markup.h) */
        const float was = portButtonsGameTextScale(0.62f);
        pw = FontGetMessageWidth(prompt) * 0.62f;
        portButtonsGameTextScale(was);
        if (pw + 60.0f > w) {
            w = pw + 60.0f;
        }
    }
    if (w < 150.0f) {
        w = 150.0f;
    }
    screenOf(where, 20.0f, &s);
    x = s.x - w * 0.38f; /* (the tail leaves from 0.38 of the way along) */
    y = s.y + 32.0f + h + 6.0f * (1.0f - fade);
    {
        const float edge = 300.0f * portWidescreenEdgeScale() - 8.0f;
        if (x < -edge) {
            x = -edge;
        }
        if (x + w > edge) {
            x = edge - w;
        }
    }
    if (y > 220.0f) {
        y = 220.0f;
    }
    windowDispGX_Message(WINDOW_TYPE_NORMAL, 1, a, x, y, w, h, s.x, s.y);
    drawnOf(opened ? "opened" : "balloon", n->id);
    sBalloon.x0 = x;
    sBalloon.x1 = x + w;
    sBalloon.y0 = y - h;
    sBalloon.y1 = y;
    FontDrawStart_alpha(a);
    FontDrawEdgeOff();
    FontDrawColor(ink);
    FontDrawScale(scale);
    say(x + 26.0f, y - 18.0f, text);
    FontDrawColor(n->mine ? (GXColor){0x30, 0x70, 0xD0, a}
                  : n->score > 0 ? (GXColor){0xC0, 0x80, 0x00, a}
                                 : (GXColor){0x70, 0x70, 0x70, a});
    FontDrawScale(0.72f);
    say(x + w - (opened ? 40.0f : 24.0f) - sw, y - 20.0f, score); /* (the taller balloon rounds off more) */
    if (prompt != NULL) { /* "{NOTE}: Read", the button as the controller in hand shows it */
        const float was = portButtonsGameTextScale(0.62f);
        FontDrawColor((GXColor){0x70, 0x60, 0x50, a});
        FontDrawScale(0.62f);
        FontDrawString(x + 26.0f, y - 52.0f, prompt);
        portButtonsGameTextScale(was);
        drawn("prompt", portButtonsWords(prompt, 1));
    }
    if (!opened) {
        return;
    }
    { /* the second line: whose, or how it was rated, or the answer to the rating */
        const char* line = sView.asked                                   ? portTrGame("Rating it...")
                           : sView.answer != NULL                        ? portTrGame(sView.answer)
                           : n->mine                                     ? portTrGame("yours")
                           : n->rated > 0                                ? portTrGame("rated up")
                           : n->rated < 0                                ? portTrGame("rated down")
                                                                         : NULL;
        if (line != NULL) {
            float lw = FontGetMessageWidth(line) * 0.66f, ls = 0.66f;
            if (lw > w - 52.0f) {
                ls *= (w - 52.0f) / lw;
            }
            FontDrawColor((GXColor){0x70, 0x60, 0x50, a});
            FontDrawScale(ls);
            say(x + 26.0f, y - 50.0f, line);
        }
    }
    { /* the choice, the chosen one with the game's arrow at its left */
        float ox = x + (w - rowW) * 0.5f;
        int chosen = 0, j = 0;
        for (k = 0; k < OPT_COUNT; k++) { /* (the cursor's place among those shown) */
            if (k != OPT_CLOSE && count < OPT_COUNT) {
                continue;
            }
            if (k == sView.cursor) {
                chosen = j;
            }
            j++;
        }
        for (k = 0; k < OPT_COUNT; k++) {
            sView.optX0[k] = sView.optX1[k] = 0.0f;
        }
        sView.optY = y - 76.0f;
        for (k = 0; k < count; k++) {
            const int on = k == chosen;
            sView.optX0[ids[k]] = ox - 22.0f; /* (the arrow's place too) */
            sView.optX1[ids[k]] = ox + ow[k] + 10.0f;
            if (on) {
                arrowRight(ox - 16.0f, y - 86.0f, 0.42f);
            }
            FontDrawStart_alpha(a);
            FontDrawEdgeOff();
            FontDrawColor(on ? (GXColor){0x20, 0x50, 0xB0, a} : ink);
            FontDrawScale(0.72f);
            say(ox, y - 76.0f, opt[k]);
            ox += ow[k] + 34.0f;
        }
    }
}

static void textDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    (void)camId;
    (void)param;
    sBalloon.x0 = sBalloon.x1 = 0.0f; /* (until drawn again) */
    if (pfh == NULL) {
        return;
    }
    if (sView.open) {
        int i;
        Vec where = standAt(&sView.note);
        for (i = 0; i < sCount; i++) { /* (where it is drawn now, when still in the list) */
            if (strcmp(sNotes[i].id, sView.note.id) == 0) {
                where = sAt[i];
            }
        }
        balloonDraw(&sView.note, &where, 99, 1);
    } else if (sNear >= 0 && !sBusy) { /* (not over the game's own talk, a sign, a cutscene: Mario held) */
        balloonDraw(&sNotes[sNear], &sAt[sNear], sAge, 0);
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

/* how far into the reach of the one playing: under 1 near enough to read */
static float reachOf(const PortNote* n, const MarioWork* mario) {
    const float half = bodyHalf();
    const float dx = (float)n->x - mario->position.x, dz = (float)n->z - mario->position.z;
    float rx, rz, fx, fz, across, into;
    camAxes(&mario->position, &rx, &rz, &fx, &fz);
    across = (dx * rx + dz * rz) / (NOTE_READ_X + half);
    into = (dx * fx + dz * fz) / (NOTE_READ_Z + half);
    if (fabsf((float)n->y - mario->position.y) >= NOTE_READ_Y) {
        return 99.0f;
    }
    return across * across + into * into;
}

/* where note i is drawn: where it stands, slid out to the side of the one playing while it would be behind them */
static Vec slideAt(int i, const MarioWork* mario) {
    const PortNote* n = &sNotes[i];
    const float half = bodyHalf() + 12.0f;
    Vec at = standAt(n);
    float rx, rz, fx, fz, across, into, want = 0.0f;
    int k, slot = -1, oldest = 0;
    camAxes(&mario->position, &rx, &rz, &fx, &fz);
    across = (at.x - mario->position.x) * rx + (at.z - mario->position.z) * rz;
    into = (at.x - mario->position.x) * fx + (at.z - mario->position.z) * fz;
    if (into > NOTE_HIDDEN - NOTE_TOWARD && into < 260.0f && fabsf(across) < half &&
        fabsf(at.y - mario->position.y) < NOTE_READ_Y) {
        want = (across >= 0.0f ? half : -half) - across; /* (to the nearer side) */
    }
    for (k = 0; k < NOTES_MAX; k++) {
        if (strcmp(sSlide[k].id, n->id) == 0) {
            slot = k;
            break;
        }
        if (sSlide[k].seen < sSlide[oldest].seen) {
            oldest = k;
        }
    }
    if (slot < 0) {
        slot = oldest;
        snprintf(sSlide[slot].id, sizeof(sSlide[slot].id), "%s", n->id);
        sSlide[slot].shift = want;
    }
    sSlide[slot].shift += (want - sSlide[slot].shift) * 0.18f;
    sSlide[slot].seen = sTick + 1;
    sSlidOut[i] = fabsf(sSlide[slot].shift) > 8.0f;
    at.x += rx * sSlide[slot].shift;
    at.z += rz * sSlide[slot].shift;
    return at;
}

/* Mario and the NPCs held while a note is open; given back where they were held, else left */
static void hold(int on) {
    if (on && !sView.held) {
        marioKeyOff();
        npcStopForEvent();
        snprintf(sView.heldMap, sizeof(sView.heldMap), "%s", gp->currentMapName);
        sView.held = 1;
    } else if (!on && sView.held) {
        MarioWork* mario = marioGetPtr();
        sView.held = 0;
        if (gp != NULL && !gp->inBattle && strcmp(sView.heldMap, gp->currentMapName) == 0) {
            if (mario != NULL && mario->keyOffCount > 0) {
                marioKeyOn();
            }
            npcStartForEvent();
            printf("[notes] Mario and the NPCs go on (keys held %d)\n", mario != NULL ? (int)mario->keyOffCount : -1);
            fflush(stdout);
        }
    }
}

static void viewClose(const char* why) {
    printf("[notes] closed %s (%s)\n", sView.note.id, why);
    fflush(stdout);
    sView.open = 0;
    sView.asked = 0;
    sView.answer = NULL;
    sView.release = 1; /* (the next frame: the A or B that closed it is no jump or hammer) */
}

/* a tap waiting on one of the open note's choices: the choice (OPT_*), taken; -1 none (a tap elsewhere is left) */
static int optionTapped(void) {
    float gx, gy;
    int k;
    if (!portMenuTapPeek(&gx, &gy) || gy > sView.optY + 6.0f || gy < sView.optY - 28.0f) {
        return -1;
    }
    for (k = 0; k < OPT_COUNT; k++) {
        if (sView.optX1[k] > sView.optX0[k] && gx >= sView.optX0[k] && gx <= sView.optX1[k]) {
            portMenuTapTake();
            return k;
        }
    }
    return -1;
}

/* the open note's frame: its copy kept fresh, the answer to its rating, the choice; read: the notes' button
   pressed this frame (it closes it, as B does) */
static void viewFrame(int read) {
    const u32 trg = keyGetButtonTrg(0), dir = keyGetDirTrg(0);
    int i, serial, tapped;
    const char* answer;
    portMenuMark(1); /* B closes it on every controller (port/include/port/menu_buttons.h) */
    for (i = 0; i < sCount; i++) {
        if (strcmp(sNotes[i].id, sView.note.id) == 0) {
            sView.note = sNotes[i];
        }
    }
    answer = portNotesAnswer(&serial);
    if (sView.asked && answer != NULL && serial != sView.serial) {
        sView.asked = 0;
        sView.answer = answer;
        sView.cursor = OPT_CLOSE;
        printf("[notes] rated %s: %s\n", sView.note.id, answer);
        fflush(stdout);
    }
    if (sView.age++ == 0) {
        return; /* (the press or tap that opened it) */
    }
    if (trg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        viewClose("B");
        return;
    }
    if (read) {
        psndSFXOn((const char*)0x20013);
        viewClose("the notes' button");
        return;
    }
    tapped = sView.asked ? -1 : optionTapped();
    if (!rateable(&sView.note) || sView.asked || sView.answer != NULL) {
        sView.cursor = OPT_CLOSE;
    } else if (dir & DEMO_STICK_LEFT) {
        if (sView.cursor > 0) {
            sView.cursor--;
            psndSFXOn((const char*)0x20005);
        }
    } else if (dir & DEMO_STICK_RIGHT) {
        if (sView.cursor < OPT_COUNT - 1) {
            sView.cursor++;
            psndSFXOn((const char*)0x20005);
        }
    }
    if (tapped >= 0) {
        sView.cursor = tapped;
    } else if (!(trg & PAD_BUTTON_A) || sView.asked) {
        return;
    }
    if (sView.cursor == OPT_CLOSE) {
        psndSFXOn((const char*)0x20013);
        viewClose(tapped >= 0 ? "a tap" : "A");
        return;
    }
    psndSFXOn((const char*)0x20012);
    portNotesAnswer(&sView.serial);
    printf("[notes] rate %s %s\n", sView.note.id, sView.cursor == OPT_UP ? "up" : "down");
    fflush(stdout);
    sView.asked = 1;
    portNotesRate(sView.note.map, sView.note.id, sView.cursor == OPT_UP ? 1 : -1);
}

/* A note can be opened now: Mario in the field, his controls his and on his feet (standing, walking or running:
   the notes' button is no move of his), no menu, card, talk or scene up; not online (the games stay the same: none
   opens, as the page's rating waits) */
static int canRead(const MarioWork* mario) {
    const WindowWork* win = winGetPtr();
    if (mario == NULL || gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || !portNotesOn()) {
        return 0;
    }
    if (portStateSyncOn() && portStateSyncPhase() != PORT_SYNC_OFF) {
        return 0;
    }
    if (mario->currMotionId != MARIO_MOTION_STAY && mario->currMotionId != MARIO_MOTION_WALK &&
        mario->currMotionId != MARIO_MOTION_DASH) {
        return 0;
    }
    if (mario->keyOffCount > 0 || !marioChkKey() || !marioChkCtrl() || marioStGetSystemLevel() != 0) {
        return 0;
    }
    return (win == NULL || win->menuState == 0) && !portPlayerMenuOpen(0) && !portMenuOpen();
}

/* The notes' button pressed this frame (player 1's, as Mario reads them): the game's L, which Mario doesn't use in
   the field (a GameCube controller's, the touch controls'), or the "Read a paper note" binding (C, a controller's
   left shoulder: Controls > Bindings), each as it went down. *how: which, for the log. */
static int readPressed(const char** how) {
    static int sHeld = 1; /* (held as the game starts: not a press until let go) */
    const int held = portBindingsHeldOn(PORT_BIND_NOTE_READ, 0);
    const int bound = held && !sHeld;
    sHeld = held;
    if (keyGetButtonTrg(0) & PAD_TRIGGER_L) {
        *how = "L";
        return 1;
    }
    *how = "the binding";
    return bound;
}

/* a tap (or a click) waiting on a drawn note's sheet, or on the near one's balloon: that note, the tap taken; -1 */
static int noteTapped(void) {
    float gx, gy;
    int i;
    if (!portMenuTapPeek(&gx, &gy)) {
        return -1;
    }
    for (i = 0; i < sCount; i++) {
        Vec at;
        screenOf(&sAt[i], 14.0f, &at); /* (the sheet's middle: it stands about 30 tall) */
        if (fabsf(gx - at.x) <= 26.0f && fabsf(gy - at.y) <= 28.0f) {
            portMenuTapTake();
            return i;
        }
    }
    if (sNear >= 0 && sBalloon.x1 > sBalloon.x0 && gx >= sBalloon.x0 && gx <= sBalloon.x1 && gy >= sBalloon.y0 &&
        gy <= sBalloon.y1) {
        portMenuTapTake();
        return sNear;
    }
    return -1;
}

/* note i opened: its copy, Mario and the NPCs held */
static void viewOpen(int i, const char* how) {
    memset(&sView.note, 0, sizeof(sView.note));
    sView.note = sNotes[i];
    sView.open = 1;
    sView.age = 0;
    sView.asked = 0;
    sView.answer = NULL;
    sView.release = 0;
    sView.cursor = rateable(&sView.note) ? OPT_UP : OPT_CLOSE;
    hold(1);
    printf("[notes] read %s by %s\n", sView.note.id, how);
    printf("[notes] open %s (%+d%s)\n", sView.note.id, sView.note.score,
           sView.note.mine ? ", yours" : sView.note.rated != 0 ? ", rated" : "");
    fflush(stdout);
    sGameA.frames = 0;
}

void portNotesFrame(void) {
    MarioWork* mario;
    const char* readHow = "";
    int i, total, near = -1, readOn;
    float best = 1.0f, dist[NOTES_MAX];
    drawnFlush(); /* (the last frame's drawing, for the tests) */
    if (sView.release > 0 && --sView.release == 0) {
        hold(0);
    }
    if (gp == NULL || !portNotesOn() || seqGetSeq() != SEQ_GAME || gp->inBattle || (mario = marioGetPtr()) == NULL) {
        if (sView.open) {
            viewClose("left the field");
        }
        sView.release = 0;
        hold(0);
        sCount = 0;
        sNear = -1;
        return;
    }
    {
        /* tests: the test's notes placed round Mario once the field has run $TTYD_NOTES_TEST_AFTER frames (an
           entrance's walk in done first) */
        static int sAfter = -1, sFrames;
        if (sAfter < 0) {
            const char* e = getenv("TTYD_NOTES_TEST_AFTER");
            sAfter = e != NULL ? atoi(e) : 0;
        }
        if (++sFrames > sAfter) {
            portNotesTestAnchor(gp->currentMapName, mario->position.x, mario->position.y, mario->position.z);
        }
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
    for (i = 0; i < sCount; i++) { /* the one most within reach */
        const float r = reachOf(&sNotes[i], mario);
        if (r < best) {
            best = r;
            near = i;
        }
    }
    if (near >= 0 && strcmp(sNotes[near].id, sNearId) == 0) {
        sAge++;
    } else {
        sAge = 0;
    }
    if (near >= 0 && sAge == 0) {
        char text[160];
        portNoteText(sNotes[near].phrase, sNotes[near].word, 0, text, sizeof(text));
        printf("[notes] near %s: %s (%+d)\n", sNotes[near].id, text, sNotes[near].score);
        fflush(stdout);
    }
    snprintf(sNearId, sizeof(sNearId), "%s", near >= 0 ? sNotes[near].id : "");
    sNear = near;
    sBusy = !sView.held && mario->keyOffCount > 0;
    sTick++;
    readOn = readPressed(&readHow);
    for (i = 0; i < sCount; i++) {
        sAt[i] = slideAt(i, mario);
    }
    if (near >= 0 && sAge == 0 && getenv("TTYD_NOTES_TEST") != NULL) { /* tests: where its sheet is, as a tap's place (0-1 across) */
        Vec at;
        screenOf(&sAt[near], 14.0f, &at);
        printf("[notes] %s on screen at %.3f %.3f\n", sNotes[near].id,
               at.x / (608.0f * portWidescreenEdgeScale()) + 0.5f, 0.5f - at.y / 480.0f);
        fflush(stdout);
    }
    /* the notes' button or a tap opens one (the near one / the one tapped); A is the game's, as anywhere (a jump, a
       talk: logged once Mario's motion shows what it did) */
    sCanRead = canRead(mario);
    if (sView.open) {
        viewFrame(readOn);
    } else if (sView.release == 0 && sCanRead) {
        const int tapped = noteTapped();
        if (tapped >= 0) {
            viewOpen(tapped, "a tap");
        } else if (readOn && near >= 0) {
            viewOpen(near, readHow);
        }
    }
    if (!sView.open && near >= 0 && sView.release == 0 && (keyGetButtonTrg(0) & PAD_BUTTON_A) &&
        !mario->keyOffCount) {
        sGameA.frames = 8;
        snprintf(sGameA.id, sizeof(sGameA.id), "%s", sNotes[near].id);
    } else if (sGameA.frames > 0 && --sGameA.frames == 0) {
        printf("[notes] A by %s was the game's: motion %d\n", sGameA.id, (int)mario->currMotionId);
        fflush(stdout);
    }
    for (i = 0; i < sCount; i++) { /* each in the room's depth order, after the room itself (translucent) */
        dispEntry(CAMERA_3D, dispCalcZ(&sAt[i]), 1, noteDisp, (void*)(intptr_t)i);
    }
    if (sCount > 0 || sView.open) {
        dispEntry(CAMERA_2D, 900.0f, 1, textDisp, NULL);
        /* the first note this install shows: the tip (once; not in a replay, which shows none anyway) */
        if (sCount > 0 && !portTipSeen(PORT_TIP_NOTES)) {
            portTipOffer(PORT_TIP_NOTES);
        }
    }
}
