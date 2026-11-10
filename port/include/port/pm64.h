#ifndef PORT_PM64_H
#define PORT_PM64_H

/* Link your Paper Mario 64 ROM (docs/PM64_LINK.md): the player points Paper
 * Cut at their own Paper Mario ROM (US, JP, PAL or iQue, known by its SHA-1;
 * .z64, .v64 or .n64 byte order). Linking reads the parts used below out of it
 * into a cache in the user folder (<user>/pm64/rom_cache.bin) and nothing else:
 * nothing from the ROM is in Paper Cut, its repository or its downloads, and
 * the ROM itself is not kept. Unlinking deletes the cache.
 *
 * port/src/common/pm64_pc.c reads the cache: its segments, Yay0, the NPC sprite
 * archive (rasters, palettes, animation component trees), the message archive
 * and the move table. The formats follow the PM64 decomp's documentation
 * (pmret/papermario); the code is Paper Cut's own.
 *
 * Usage:
 *   if (portPm64Linked()) {
 *       const Pm64Sprite* s = portPm64NpcSprite(PM64_SPR_WORLD_GOOMBARIO);
 *       Pm64Anim a;
 *       portPm64AnimStart(&a, s, 2);            // its third animation (Walk)
 *       portPm64AnimStep(&a, 1.0f);             // each 60 Hz frame
 *       for each part a.part[i] with raster >= 0:
 *           portPm64RasterTexture(s, raster, palette) -> GX RGBA8 texture, size
 *       char name[64]; portPm64MessageText(PM64_MSG(0x1D, 0x07), name, sizeof name); // "Goombario"
 *   }
 * Segments not parsed here (the audio's SBN, for the soundtrack) are in the
 * cache whole: portPm64Segment(PM64_SEG_AUDIO, &size). A new segment is a row
 * in pm64_pc.c's kSegs and a bump of its PM64_CACHE_FORMAT (old caches are
 * then read again from the ROM: the player links it once more). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM64_NONE = 0,
    PM64_US = 1,
    PM64_JP = 2,
    PM64_PAL = 3,
    PM64_IQUE = 4,
};

/* --- linking */

/* The linked ROM's version (PM64_US..PM64_IQUE), or PM64_NONE. Reads the
 * cache's index from the user folder on first call. */
int portPm64Linked(void);
/* "US", "Japan", "Europe", "iQue" (English, for the settings page), or "" */
const char* portPm64VersionName(int version);
/* Links the ROM in rom (size bytes, any byte order; the buffer is the
 * caller's and may be reordered in place): checks it, writes the cache.
 * Returns the version, or PM64_NONE with a reason (English) in err. */
int portPm64LinkRom(void* rom, size_t size, char* err, size_t errSize);
/* As above from a file path (stdio; the settings page reads picked files
 * through SDL so Android's content:// ones work too). */
int portPm64LinkFile(const char* path, char* err, size_t errSize);
/* Deletes the cache and forgets everything read from it. */
void portPm64Unlink(void);
/* Bumped by link and unlink, for users that keep things made from the cache. */
unsigned portPm64Generation(void);

/* --- linking from the launcher (port/settings/features/pm64_link.cpp, the settings page's own code,
 * for port/settings/launcher_ui.cpp's Paper Mario 64 entry; Aurora builds only) */

/* Nonzero if the file at path is an N64 ROM: a .z64/.v64/.n64 name, or its first bytes (any byte
 * order: Android's content:// URIs often have no name). A disc image picked or dropped is not one. */
int portPm64LinkIsRom(const char* path);
/* The ROM at path (picked or dropped) linked at the next portPm64LinkPoll. */
void portPm64LinkQueue(const char* path);
/* The system's file picker for a ROM; its answer is linked at the next portPm64LinkPoll. */
void portPm64LinkPick(void);
/* Each frame: a ROM picked or dropped is linked. Returns how the last try went, translated and
 * short, for the launcher ("" none yet). */
const char* portPm64LinkPoll(void);
/* The i-th ROM file found in the user folder, its pm64 folder or Downloads (NULL past the last). */
const char* portPm64LinkFound(int i);

/* --- segments of the cache */

#define PM64_TAG(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define PM64_SEG_SPRITES PM64_TAG('S', 'P', 'R', 'T') /* the sprite section (player and NPC sprites) */
#define PM64_SEG_MESSAGES PM64_TAG('M', 'S', 'G', '0') /* the message archive (PAL: English) */
#define PM64_SEG_MESSAGES_DE PM64_TAG('M', 'S', 'G', 'D') /* PAL only */
#define PM64_SEG_MESSAGES_FR PM64_TAG('M', 'S', 'G', 'F')
#define PM64_SEG_MESSAGES_ES PM64_TAG('M', 'S', 'G', 'S')
#define PM64_SEG_AUDIO PM64_TAG('S', 'B', 'N', ' ') /* the audio's SBN archive, for the soundtrack */
#define PM64_SEG_MOVES PM64_TAG('M', 'O', 'V', 'E') /* gMoveTable, found by its contents */
#define PM64_SEG_ICONS PM64_TAG('I', 'C', 'O', 'N') /* the item icon archive (port/include/port/pm64_media.h) */
#define PM64_SEG_ITEMS PM64_TAG('I', 'T', 'E', 'M') /* gItemTable, found by its contents */

/* A segment's bytes as in the ROM (big-endian), loaded on first use and kept;
 * NULL if the cache hasn't got it. */
const uint8_t* portPm64Segment(uint32_t tag, size_t* size);
/* A song playing from PM64_SEG_AUDIO holds it (+1 as it opens, -1 as it closes, on any thread), counted per
 * segment: an unlink or a new link meanwhile frees that segment once nothing holds it. 0 when a +1 was refused
 * (too many segments held at once: don't use it then). */
int portPm64AudioHold(const void* segment, int delta);

/* --- Yay0 */

/* The decoded size of Yay0 data, or 0 if src isn't Yay0. */
size_t portPm64Yay0Size(const void* src, size_t srcSize);
/* Decodes src into dst (dstSize >= its decoded size, any bytes past it zeroed); 1 if it all fit. */
int portPm64Yay0Decode(const void* src, size_t srcSize, void* dst, size_t dstSize);

/* --- NPC sprites */

/* The NPC sprite archive's indices (the decomp's SPR_ ids minus 1) of the
 * partners' sprites: the field's, then the battle's. */
enum {
    PM64_SPR_WORLD_GOOMBARIO = 0,
    PM64_SPR_WORLD_KOOPER = 1,
    PM64_SPR_WORLD_BOMBETTE = 2,
    PM64_SPR_WORLD_PARAKARRY = 3,
    PM64_SPR_WORLD_BOW = 4,
    PM64_SPR_WORLD_WATT = 5,
    PM64_SPR_WORLD_SUSHIE = 6,
    PM64_SPR_WORLD_LAKILESTER = 7,
    PM64_SPR_BATTLE_GOOMBARIO = 8, /* ... the battle's in the same order, to 15 */
    PM64_SPR_BATTLE_LAKILESTER = 15,
};

typedef struct Pm64Sprite Pm64Sprite;

/* How many sprites the archive has (0 when not linked). */
int portPm64NpcSpriteCount(void);
/* Sprite index, decoded on first use and kept; NULL if there's none. */
const Pm64Sprite* portPm64NpcSprite(int index);
/* PM64's sizes beside Mario (port/src/common/pm64_partner_pc.c): the factor on a PM64 sprite (drawn at half a unit a
 * pixel) that makes it stand beside TTYD Mario as it stood beside PM64's Mario (about 0.855; the partners, the
 * enemies and the bosses all use it); an NPC sprite's row's drawn height in pixels (0: none); PM64 Mario's (Mario1
 * Idle, about 51). */
float portPm64Size(void);
float portPm64NpcSpriteHeight(int sprite, int anim);
float portPm64MarioHeightPx(void);
int portPm64SpriteAnimCount(const Pm64Sprite* s);
/* A sprite in the NPC sprites' layout made from decoded data of one's own (malloc'd, taken; its rasters'
 * pixel offsets into itself), kept for good; NULL if it doesn't parse. The player's sprites:
 * port/include/port/pm64_media.h. */
const Pm64Sprite* portPm64SpriteAdopt(uint8_t* data, size_t size);
int portPm64SpriteRasterCount(const Pm64Sprite* s);
int portPm64SpritePaletteCount(const Pm64Sprite* s);
/* Raster i's size and the palette it was drawn with; 0 if there's no raster i. */
int portPm64SpriteRaster(const Pm64Sprite* s, int i, int* width, int* height, int* palette);
/* Raster i in palette pal (-1: its own) as RGBA8, 4 bytes a pixel, rows of
 * width*4 bytes (out: width*height*4). 1 on success. */
int portPm64SpriteRGBA(const Pm64Sprite* s, int i, int pal, uint8_t* out);
/* Cool Mode: the eyes found on raster i (kept per raster): their middle as shares of its width and height
 * (from the top left), the glasses' width as a share of its width, and how sure (higher: surer). 0: none. */
int portPm64SpriteEyes(const Pm64Sprite* s, int i, float* x, float* y, float* width, float* score);
/* The same as a GameCube GX_TF_RGBA8 texture (4x4 tiles; the size rounded up
 * to whole tiles: *texW, *texH), made on first use and kept with the sprite
 * (the bytes never change once made, so a queued draw may point at them);
 * NULL if there's no such raster. */
const void* portPm64RasterTexture(const Pm64Sprite* s, int i, int pal, int* texW, int* texH);

/* An animation playing: the sprite's components (parts) and where each is. */
#define PM64_MAX_PARTS 16
typedef struct Pm64Part {
    int raster;     /* -1: nothing drawn */
    int palette;    /* -1: the raster's own */
    float x, y, z;  /* the part's place, its parent's added: sprite pixels, y up, from the sprite's feet */
    float rx, ry, rz;
    float sx, sy, sz;
    /* the command interpreter's state */
    int pc;
    float wait;
    int loops;
    int parent;
    float ox, oy, oz; /* the part's own SetPosition offset */
    float bx, by, bz; /* the component's base position */
} Pm64Part;
typedef struct Pm64Anim {
    const Pm64Sprite* sprite;
    int anim;
    int count;
    Pm64Part part[PM64_MAX_PARTS];
} Pm64Anim;

/* Starts animation anim (0 .. count-1) of s from its first frame. */
void portPm64AnimStart(Pm64Anim* a, const Pm64Sprite* s, int anim);
/* Steps it by frames 60 Hz frames (the waits are in 60ths of a second). */
void portPm64AnimStep(Pm64Anim* a, float frames);

/* --- messages */

/* The decomp's message ids: section << 16 | index. */
#define PM64_MSG(section, index) ((uint32_t)(section) << 16 | (uint32_t)(index))
/* The message's bytes in the ROM's own encoding up to its end byte (0xFD,
 * not counted); NULL if there's no such message. lang: 0 English, 1 German,
 * 2 French, 3 Spanish (PAL; other versions have one). */
const uint8_t* portPm64MessageRaw(uint32_t id, int lang, size_t* len);
/* The message as plain UTF-8 text (line breaks as '\n', commands left out):
 * its length, or -1 (no such message, or a JP/iQue ROM's text, which isn't
 * decoded here). lang as above, falling back to English. */
int portPm64MessageText(uint32_t id, int lang, char* out, size_t outSize);

/* --- moves (gMoveTable) */

typedef struct Pm64Move {
    uint32_t nameMsg;
    uint32_t flags;
    uint32_t shortDescMsg;
    uint32_t fullDescMsg;
    int category;
    int costFP;
    int costBP;
    int actionTip;
} Pm64Move;
int portPm64MoveCount(void);
int portPm64Move(int index, Pm64Move* out);
/* The move whose full description is fullDescMsg (unique per move); its index or -1. */
int portPm64MoveByDesc(uint32_t fullDescMsg, Pm64Move* out);

/* --- items (gItemTable: the decomp's item enum order) */

typedef struct Pm64Item {
    uint32_t nameMsg;
    uint32_t fullDescMsg;
    uint32_t shortDescMsg;
    int sellValue;
    int typeFlags;
    int potencyA; /* HP restored (a thrown item's damage) */
    int potencyB; /* FP restored */
} Pm64Item;
/* Item index of the ROM's table; 0 when the cache hasn't got the table (a ROM linked before it was read). */
int portPm64Item(int index, Pm64Item* out);

/* --- the eight partners (port/src/common/pm64_partner_pc.c) */

/* PM64 partners 1 Goombario .. 7 Lakilester, then 8 Bow (added after the
 * seven, so the numbers saved and sent online stay), and the TTYD partner
 * (PartyId 1 Goombella .. 7 Ms. Mowz) whose place, abilities and battle unit
 * each takes. */
enum {
    PM64_PARTNER_NONE = 0,
    PM64_PARTNER_GOOMBARIO = 1,
    PM64_PARTNER_KOOPER = 2,
    PM64_PARTNER_BOMBETTE = 3,
    PM64_PARTNER_PARAKARRY = 4,
    PM64_PARTNER_WATT = 5,
    PM64_PARTNER_SUSHIE = 6,
    PM64_PARTNER_LAKILESTER = 7,
    PM64_PARTNER_BOW = 8,
    PM64_PARTNER_COUNT = 9,
};
int portPm64PartnerHost(int pm64Partner);
/* Its name in the game's language: the ROM's ("Goombario"; a PAL ROM's in the
 * European release's text language), the Japanese release's own names on the
 * JP disc, else the English one. UTF-8. */
const char* portPm64PartnerName(int pm64Partner);
/* The same in the game's own text encoding (Latin-1; the Japanese release's Shift-JIS), for its menus. */
const char* portPm64PartnerGameName(int pm64Partner);
/* The Start menu's Party tab (win_party.c): the menu model poseId of TTYD partner party drawn as the PM64 partner
 * in its place (walking while selected), scale a sprite pixel in the model's units; 1 if it was, 0: draw the model. */
int portPm64PartyMenuDraw(int poseId, int party, float (*mtx)[4], int selected, float scale);
/* The setting "pm64_partners" is on, the ROM is linked and (online) every
 * player has it linked. */
int portPm64PartnersActive(void);
/* The PM64 partner standing in TTYD partner party's place (0: none). */
int portPm64PartnerOn(int party);
/* One character's own look (second_player_pc.c: a co-op player's pick), by party slot (src/party.c's
 * partyPtrTbl, 0 .. PORT_PARTY_SLOTS - 1): pm64Partner drawn on that entry when it is its host, -1 the TTYD
 * partner's own look, 0 the party's (portPm64PartnerOn). Field draws only; battles keep the party's. */
void portPm64SlotLook(int slot, int pm64Partner);
/* The players 2-4 cards' Character strip: partner pm64Partner's face from the ROM, centred, as a GX RGBA8
 * texture *size square; NULL none. */
const void* portPm64MenuFace(int pm64Partner, int* size);
/* Each pair (a TTYD partner and the PM64 partners on it): the look after host's current one (TTYD -> each
 * PM64 partner met on it -> TTYD): a PM64 partner, 0 the TTYD partner, -1 no other (the Party tab's Y). */
int portPm64PairNext(int host);
/* The battle's partner change (battle_seq_command.c's list, port builds only): a row's kind with the look its
 * pick puts on the host (look 1-8 a PM64 partner, PM64_LOOK_TTYD the TTYD partner, 0 no change). It rides
 * commandMenu.partyKind, so online and in replays every game wears the same; BattleChangeParty takes it
 * (portPm64BattleChangeKind: the look worn, as the Partner wheel does; the unit kind back). */
#define PM64_LOOK_TTYD 15
#define PM64_BTL_KIND(kind, look) ((kind) | ((look) << 8))
#define PM64_BTL_KIND_OF(k) ((k) & 0xFF)
#define PM64_BTL_LOOK_OF(k) (((k) >> 8) & 0xF)
int portPm64BattleChangeKind(int partyKind);
/* The battle's partner list draws a PM64 row's icon (a party menu icon, ICON_155..ICON_161) as that partner's
 * face (portIconTexOverride, through portPm64HudFace) while this is set; 0 off. */
void portPm64IconFace(int pm64Partner);
/* The HUD's partner face iconId (statuswindow.c's party_icon) as the PM64 partner's in that host's place:
 * the head of his sprite from the ROM framed as the game's icon (gameTex: the icon's own texture, GX format
 * gameFmt, gameW x gameH, whose HP heart is kept), a w x h GX RGBA8 texture; NULL: the game's own. */
const void* portPm64HudFace(unsigned iconId, const void* gameTex, int gameFmt, int gameW, int gameH, int* w, int* h);
/* The launcher's cast (port/settings/launcher_popup.cpp): how many PM64 partners stand with TTYD's (0 when not
 * linked), and the i-th's face in PM64's own order (Goombario .. Lakilester): his sprite's head from the ROM
 * with the sticker edge, as the HUD's, centred and cropped to it; straight RGBA8, top row first, *w x *h, at
 * most PM64_CAST_FACE_MAX square. 0: none. */
#define PM64_CAST_FACE_MAX 64
int portPm64CastCount(void);
int portPm64CastFace(int i, uint8_t* rgba, int* w, int* h);
/* Puts pm64Partner in its host's place (0 with party: the TTYD partner back). */
void portPm64PartnerWear(int party, int pm64Partner);
/* Which have joined: each with its host (its TTYD counterpart), once the
 * setting is on and the host has joined. */
int portPm64PartnerAvailable(int pm64Partner);
/* Each field frame (portPm64FieldFrame): the ones whose host has joined since
 * marked joined (GSW 2011, Bow 2019), and told once Mario is free; the overlay
 * (pm64_link.cpp) takes each to tell in turn ("Kooper joined too!"): its
 * PM64 partner, or 0 none waiting. */
void portPm64JoinFrame(void);
int portPm64JoinNotice(void);
/* "How Paper Mario partners join" ("pm64_join"): 0 with their TTYD partner, 1 once their TTYD partner has joined
 * and their chapter's Paper Mario boss is beaten (a partner who joined before stays). "From the start"
 * ("pm64_met_all") overrides it: then 0, all of them with their TTYD partners, without the notices. */
int portPm64JoinByBoss(void);
/* Paper Mario 64's boss fights, each beaten or not on this file (GSW 2024..2025, a bit per fight: 2024's 0..7,
 * 2025's 0..2). The first ten are the boss rush's, in its order: Jr. Troopa, the Koopa Bros., Tutankoopa, Tubba
 * Blubba, General Guy, Lava Piranha, Huff N. Puff, the Crystal King, Kammy Koopa, Bowser. The eleventh is the Koopa
 * Bros.' rematch (the same boss and stage, its own beaten mark: in the story once Bobbery has joined; it brings
 * Bombette, the first Koopa Bros. fight Kooper alone); it isn't in the rush. The boss list and the board show it
 * after the first Koopa Bros. fight (portPm64FightAt). */
#define PM64_BOSS_FIGHTS 11
#define PM64_BOSS_RUSH_FIGHTS 10
#define PM64_FIGHT_KOOPA_BROS 1
#define PM64_FIGHT_KOOPA_BROS_REMATCH 10
int portPm64BossBeaten(int fight);
void portPm64BossSetBeaten(int fight);
/* The boss (pm64_bosses_aurora.c's, 0..9: its sprite, battle and portPm64BossEnglish) that fight is: the rematch
 * is the Koopa Bros. (1); the rest are their own. -1 none. */
int portPm64FightBoss(int fight);
/* A fight's name in English (portTr it): the boss's, the rematch "Koopa Bros. (rematch)". */
const char* portPm64FightEnglish(int fight);
/* The lists' order (the boss list, the board): the fight shown i-th (0..PM64_BOSS_FIGHTS-1), the rematch after
 * the first Koopa Bros. fight. */
int portPm64FightAt(int i);
/* The PM64 partners fight's boss brings in "Beat their boss" (Jr. Troopa: Goombario; the Koopa Bros.: Kooper; the
 * Koopa Bros.' rematch: Bombette ...; none for the last three): up to 2 into out, how many. */
int portPm64BossPartners(int fight, int out[2]);
/* Of those, the ones beating it would bring now (that mode, the partners on, their TTYD partner joined and they
 * not yet): the boss list's "brings Kooper". */
int portPm64BossBrings(int fight, int out[2]);
/* While a mode lends its loadout (every TTYD partner joined for the run: modes_aurora.c), no PM64 partner joins
 * and none is told. */
void portPm64JoinHold(int on);
/* The ROM not linked while a file made with it plays (its PM64 partners met or worn, PM64 bosses beaten, PM64
 * badges or dishes in the bag, the storage or worn): 1 once per launch, Mario free in the field, for the overlay's
 * note (pm64_link.cpp). Its partners stand as their TTYD counterparts meanwhile, its badges and dishes keep
 * working under stand-in names and icons; linking again brings everything back (nothing in the save changes). */
int portPm64UnlinkedNotice(void);
/* luigi_pc.c's draw: pose poseId drawn as a PM64 partner's sprite instead.
 * 1 if it was (the pose's own animation and paper turn stepped, nothing of
 * its model drawn). */
struct AnimationPose;
int portPm64DrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale);
/* The same for another player's ghost (statesync_ghost_pc.c): drawn as that
 * player's PM64 partner p, whatever this game's own choice is. */
int portPm64DrawPoseAs(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int pm64Partner);
/* Ms. Mowz's slot (party 7) held by Watt alone: he joined there (after chapter 4, his boss beaten) with Ms. Mowz
 * herself not recruited. The slot is marked joined in the pouch while the PM64 partners are on, always wearing
 * Watt; she isn't on the wheel, and the partner counts and the open world's checks leave her out (partyChkJoin(7)
 * stays true: the game's party switch and menus go by it, for him). Her own
 * recruitment (partyJoin 7: her quest, the open world's item) ends it: then both are there, as for any host.
 * party: 1..7 (only 7 can be). */
int portPm64SlotStandIn(int party);
/* mario_party.c's partyJoin: the story (or an item) recruited TTYD partner party */
void portPm64PartyJoined(int party);
/* Their own field abilities (port/src/common/pm64_field_pc.c): each frame
 * (the frame hook) Watt's light and sparks, Sushie's swim and Lakilester's
 * carry; the PM64 partner out now in the field (0 none); Mario swimming on
 * Sushie now; Mario carried on Lakilester's cloud now.
 * The decomp's own files call the rest (mario_motion.c, mario_hit.c,
 * mot_hip.c, mot_roll.c: portPm64MarioAttr; party_yoshi.c:
 * portPm64FlutterFrames). */
int portPm64Carried(void);
/* Lakilester's own field ability (pm64_aurora.c's hook on Flurrie's cloud_use, "Full"): the button mounts his cloud
 * or gets off it (refused over spikes or lava, or in the air), instead of Gale Force. 1 if it was his (party: a
 * PartyEntry*); 0, Flurrie's use as ever. */
int portPm64LakilesterUse(void* party);
/* After each frame of Bobbery's use (bomhei_use) with Bombette out: dropping back in after her blast she faces
 * where Mario faces, as PM64's Bombette comes back (party: a PartyEntry*) */
void portPm64BombetteUsed(void* party);
void portPm64FieldFrame(void);
/* Bow's Outta Sight in the field (Vivian's Veil underneath, its rules unchanged): pm64_aurora.c's hooks on
 * vivian_use say each frame whether Mario is hidden now (on); Mario stays standing, see-through, with Bow
 * over him, fading in and out (pm64_field_pc.c). portPm64BowVeil: how far (0 none .. 1 hidden). */
void portPm64BowVeilSet(int on);
float portPm64BowVeil(void);
int portPm64BowVeilAlpha(void); /* Mario's and Bow's alpha while hidden: 255 .. 128 */
/* luigi_pc.c's portLuigiDrawMtx (every draw of a pose through Mario's path): 1 if this draw of Mario is held back
 * and drawn with the translucent things instead (see-through under Bow); mtx is a Mtx */
int portPm64BowVeilDefer(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale);
/* battle_disp.c: a veiled unit's body is drawn anyway (Bow after Outta Sight: there, see-through, over Mario;
 * Vivian's veiled body sinks out of sight). unit: a BattleUnit. */
int portPm64DrawVeiled(const void* unit);
int portPm64PartnerOut(void);
/* Watt in the field as in PM64 (pm64_field_pc.c; pm64_aurora.c's hooks on Ms. Mowz's chuchu_use and chuchu_move):
 * the partner button picks her up into Mario's hands and puts her down again (no sniff, no freeze); held, she
 * shows the hidden blocks near her; out, she floats close behind him. party: a PartyEntry. */
int portPm64WattHeld(void);
void portPm64WattUse(void* party);
void portPm64WattFollow(void* party);
/* Goombario's field Tattle (party_kuribo.c): the message he says for Goombella's tag (an NPC's, npc its name; or
 * the room's, npc NULL): his own Paper Mario line from the ROM where one fits, else tag (pm64_field_pc.c) */
const char* portPm64FieldTattle(const char* npc, const char* tag);
int portPm64Swimming(void);
/* mot_jump.c: a jump off Sushie's back, higher (to reach the shore from the water) */
void portPm64SwimJump(void);
struct HitEntry;
int portPm64MarioAttr(struct HitEntry* hit);
int portPm64FlutterFrames(int frames);
/* party_yoshi.c: the button a flutter is held on (A; X for Parakarry's carry). pm64_aurora.c's partyDoWork
 * hook: 1 when the partner out refuses X here (the buzzer played): Parakarry under a low ceiling, Sushie. */
int portPm64FlutterButton(void);
int portPm64FieldRefuse(void);
/* pm64_aurora.c's marioChgPose hook: on Sushie's back a stand, walk or run is the ride pose (returns 0) */
int portPm64RidePose(const char** name);
/* Once at start (playmodes_aurora.c): the frame hook, the battle moves'
 * texts. */
void portPm64Init(void);
/* Each frame (the frame hook): the battle moves, the field. */
void portPm64Frame(void);
/* The partner wheel's choice of a PM64 partner: its tap's argument
 * (PM64_WHEEL_BASE + pm64Partner), taken by portQuickSwapPoll. */
#define PM64_WHEEL_BASE 8
/* portQuickSwapPoll: a wheel tap for a PM64 partner (arg) put on its host;
 * the host's id to switch to, or -1 (current: the partner out now). */
int portPm64WheelChoice(int arg, int current);

/* Online: the GSW words that are each player's own (the PM64 partner on each
 * host, 2012..2018), not merged between games; and all seven hosts' (8 bytes, the
 * last 0) taken and put back, for a shared battle's player B playing A's. */
int portPm64GswOwn(int gswIndex);
void portPm64WornGet(unsigned char out[8]);
void portPm64WornSet(const unsigned char in[8]);

/* Partner p's joining was told on this file (GSW 2011 bit p, Bow GSW 2019
 * bit 0; 2011 bit 0: all, from 0.5.0's first builds), and its telling. */
int portPm64PartnerMet(int pm64Partner);
void portPm64PartnerSetMet(int pm64Partner);
/* The PM64 message language for the game's own (portPm64MessageText's lang:
 * the European release's text language where a PAL ROM has it, else 0). */
int portPm64GameLanguage(void);
/* The PM64 message language that is the game's own text language, -1 none (a Japanese game, an Italian one, or
 * German, French or Spanish without a European ROM): the ROM's text stands in for the game's only then (the
 * battle's move texts, the Party tab, Goombario's Tattle); otherwise the game's own stays. */
int portPm64TextLanguage(void);
/* The Start menu's Party tab (win_party.c) with a PM64 partner out on host: the message tag of his own
 * description from the ROM (PM64's partner menu text, laid out as the game's help), NULL for the game's own
 * (none on host, the ROM's text not in the game's language, or no mods). The moves' names, descriptions and FP
 * costs need nothing: they're the host weapons' own, which the battle's mapping already makes PM64's. */
const char* portPm64PartyHelp(int host);

/* --- battle: the hosts' moves that stand for PM64 moves */

/* How many (host move, PM64 move) pairs there are; pair which's PM64 name
 * and battle description from the ROM (0 without text: JP, iQue); the host
 * weapon's name or description message tag (may be NULL); whether its PM64
 * partner is the one out on its host now. */
int portPm64MoveCountMapped(void);
int portPm64MoveText(int which, char* name, size_t nameSize, char* desc, size_t descSize);
const char* portPm64MoveTag(int which, int description);
int portPm64MoveWorn(int which);
/* Each frame: the worn partners' moves take PM64's FP cost (the move table),
 * damage by rank and effects (Electro Dash's shock through defense, Belly
 * Flop's and Tidal Wave's water, Mega Shock's paralysis as stop); the rest
 * the game's own. */
void portPm64BattleCosts(void);
/* battle_event_cmd.c btlevtcmd_CheckDamage / CommandCheckDamage, after the hit (tests, TTYD_PM64_LOG): a worn
 * PM64 move's hit on target logged with the damage it did ("[pm64] Kooper's move 3 hit ...: 5 damage, HP 3->0")
 * and the statuses it left; hpBefore and totalBefore its HP and totalHpDamageTaken before the hit. */
struct BattleUnit;
void portPm64HitLog(struct BattleUnit* attacker, struct BattleUnit* target, int hpBefore, int totalBefore, int result);
/* battle_event_cmd.c btlevtcmd_CheckDamage / CommandCheckDamage, when the hit ended the attacker's event (a
 * counter): a PM64 move's leftovers (its forced row, held effects and damage, offset, turn, size, fade) undone,
 * since its own end never runs. */
void portPm64MoveCut(struct BattleUnit* attacker);
/* unit_party_chuchurina.c: Tease's weapon as it's made (Mega Shock's). */
struct BattleWeapon;
void portPm64MadowaseWeapon(struct BattleWeapon* weapon);

/* --- online (port/include/port/statesync.h): "pm64" in the hello's mods list
 * when this game has the partners ("" when not); every player needs the same.
 * The value sent holds while connected (portPm64PartnersActive follows it). */
const char* portPm64OnlineToken(void);

#ifdef __cplusplus
}
#endif

#endif
