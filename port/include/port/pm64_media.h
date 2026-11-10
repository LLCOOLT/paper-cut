#ifndef PORT_PM64_MEDIA_H
#define PORT_PM64_MEDIA_H

/* More of the linked Paper Mario 64 ROM (docs/PM64_EXTRAS.md): the Journal's
 * Paper Mario 64 Tattle Log, its badges and the second merchant, the Star
 * Spirits' cameo and Mario's Paper Mario look in the Wardrobe. Everything read from the player's own ROM's cache while the
 * game runs; with no ROM linked none of it is there. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Paper Mario 64's badges (port/src/common/pm64_badges_pc.c): items PORT_PM64_BADGE_MIN + i
 * (port/include/port/battlex.h), their icons PORT_ICON_PM64_BASE + i (icondrv.c draws a badge's frame,
 * portIconTexOverride their picture). Setting "pm64_badges". */
#define PORT_ICON_PM64_BASE 0x7E00
enum {
    PORT_PM64_BADGE_DIZZY_STOMP = 0x1A0 + 5,
    PORT_PM64_BADGE_PEEKABOO = 0x1A0 + 12,
    PORT_PM64_BADGE_PAY_OFF = 0x1A0 + 13,
};
int portPm64BadgesActive(void);
int portPm64BadgeCount(void);
int portPm64BadgeGrants(int id, const short** grants);
const char* portPm64BadgeEnglish(int id);
int portPm64BadgeText(int id, char* name, size_t nameSize, char* desc, size_t descSize);
int portPm64BadgeWorn(int id);
int portPm64Peekaboo(void);
int portPm64PayOff(void);
/* the item table's entries (BP, price, icon), again after a link or unlink */
void portPm64BadgesFill(void);
/* Rowf's stock (gPortMerchantStockHook) */
int portPm64MerchantStock(int* out, int max);
/* An icon id's picture from the ROM (GX RGBA8, kept), NULL when it isn't one of these or there's none;
 * and the game's icon whose frame (icon.bin) a PM64 icon is drawn in (id itself for the rest). */
const void* portPm64BadgeIcon(int iconId, int* w, int* h);
unsigned portPm64IconBinId(unsigned iconId);
/* One of these icons' drawn size: its frame's (the game's icon's texture), not its picture's own (icondrv.c;
 * winIconSet sizes its quad by it, iconGX alike). 0, sizes left alone, for the game's own icons. */
int portIconFrameSize(unsigned iconId, unsigned short* width, unsigned short* height);
/* Each frame: Dizzy Stomp's weapon (Sleepy Stomp's, swapped and back). */
void portPm64BadgesBattleFrame(void);
/* Aurora, once (portPm64MediaInit): the texts, Rowf, the frame (pm64_badges_aurora.c). */
void portPm64BadgesInit(void);
/* Aurora, once (portPm64MediaInit): the Paper Mario 64 boss list's board at the Star Haven corner of Rogueport's
 * plaza, beside Rowf (pm64_board_aurora.c; with a ROM linked, not online or in a replay). */
void portPm64BoardInit(void);
/* Aurora, once: the Star Spirits over a Special move (pm64_touches_aurora.c, setting "pm64_star_spirits"). */
void portPm64TouchesInit(void);

/* --- Tayce T.'s recipes (port/src/common/pm64_food_pc.c): Paper Mario 64's dishes and Rowf's ingredients for
 * them, items PORT_PM64_FOOD_MIN + i (port/include/port/battlex.h), their icons PORT_ICON_PM64_FOOD_BASE + i
 * (drawn as the badges' are). Setting "pm64_recipes"; their texts and battle frame in pm64_badges_aurora.c. */
#define PORT_ICON_PM64_FOOD_BASE (PORT_ICON_PM64_BASE + 0x10)
int portPm64FoodActive(void);
int portPm64FoodCount(void); /* the items: dishes first */
int portPm64DishCount(void);
int portPm64FoodIs(int id);
const char* portPm64FoodEnglish(int id);
/* the name and description from the ROM (UTF-8); 0 without them */
int portPm64FoodText(int id, char* name, size_t nameSize, char* desc, size_t descSize);
/* what it restores; its kind (0 food, 1 Super Soda's cure, 2 thrown, 3 a random effect), -1 not one */
int portPm64FoodRestores(int id, int* hp, int* fp);
/* the item table's entries, again after a link or unlink (portItemsInit's too) */
void portPm64FoodFill(void);
/* Each frame: the Kooky Cookie's and Strange Cake's effect for the battle's turn. */
void portPm64FoodBattleFrame(void);
/* gor_01.c's cooking: a pair Paper Mario's recipe takes before the game's (cook_chk2's start), a pair with no
 * recipe of the game's or Paper Cut's (portRecipeCook), one ingredient with none (portRecipeCook1); 0 none */
int portPm64RecipeFirst(int a, int b);
int portPm64RecipeCook(int a, int b);
int portPm64RecipeCook1(int a);
/* the recipe book: a dish's first recipe (b 0: on its own), 0 none; cooked so far by this save file (the user
 * folder's Recipes/<game id>/file<n>.txt, read as a file is loaded), and marking one (gor_01.c cooking_flag) */
int portPm64RecipeHint(int dish, int* a, int* b);
void portPm64FoodFileLoaded(void);
int portPm64FoodCooked(int id);
void portPm64FoodMarkCooked(int id);
/* Rowf's ingredients while it's on (portPm64MerchantStock adds them) */
int portPm64FoodStock(int* out, int max);
/* a food icon id's picture in the icon archive and the game's item whose frame it's drawn in; 0 not one */
int portPm64FoodIcon(int iconId, unsigned* offset, int* looks);

/* --- other PM64 sprites in the game's models' places (pm64_enemy_pc.c; luigi_pc.c's draw) */
#define PM64_SPR_ROWF 166
extern int gPortPm64RowfPose; /* the second merchant's pose (drawn as Rowf), or -1 */
int portPm64MediaDrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale);
/* The check that a Paper Mario 64 look never shows its host's model (pm64_enemy_pc.c; luigi_pc.c's draw calls
 * them): a pose drawn as Paper Mario 64, a pose freed, and a pose about to be drawn as its own model (logged
 * "[pm64] host model shown: ..." when it was Paper Mario 64 just before, or is one of Paper Cut's pc_pm64_ NPCs) */
void portPm64NoteDrawn(int poseId);
void portPm64NoteReleased(int poseId);
void portPm64HostDrawCheck(int poseId, int xluMode);
/* Paper Mario 64's bosses standing in Rogueport (pm64_story_aurora.c): each one's NPC pose drawn as the boss's
 * sprite, set each frame (pose -1: none). sprite, palette, its row; layer: rows drawn over it (-1 none); vineA/B:
 * a vine's segment rows (-1 none) and its height in PM64 units; scale: the boss's own size. */
typedef struct Pm64FieldLook {
    int pose, sprite, palette, anim, layer[2], vineA, vineB;
    float vine, scale;
    float hover; /* how high it floats over the floor, the field's units (Huff N. Puff: as in its battle) */
} Pm64FieldLook;
#define PM64_FIELD_LOOKS 12
extern Pm64FieldLook gPortPm64FieldLooks[PM64_FIELD_LOOKS];
/* a Paper Mario 64 boss fight's own boss as it stands in the field (pm64_bosses_aurora.c): its look, pose -1;
 * talking: its talk row. 0 none (no ROM, no such fight). */
int portPm64BossFieldLook(int fight, int talking, Pm64FieldLook* out);
/* look i's head as last drawn (gPortPm64FieldLooks[i]): the top centre of its sprite's visible pixels, in the
 * world; 0 not drawn yet as it stands now (its pose) */
int portPm64FieldLookHead(int i, float out[3]);
/* look i as drawn (its visible pixels, its parts over it included): half its width, and its lowest and highest
 * visible pixel over its NPC's feet, the field's units; 0 not drawn yet as it stands now */
int portPm64FieldLookBounds(int i, float* halfWidth, float* bottom, float* top);
/* the visible pixels (alpha) of a GX RGBA8 texture texW wide, within its w x h: their bounds, rows from the top;
 * 0 none */
int portPm64TexOpaqueBounds(const void* tex, int texW, int w, int h, int* x0, int* y0, int* x1, int* y1);
/* The story's encounters with Paper Mario 64's bosses in Rogueport (pm64_story_aurora.c): its hooks. */
void portPm64StoryInit(void);

/* --- Mario's Paper Mario 64 look (pm64_player_pc.c; the Wardrobe's row, setting "pm64_mario_look") */
/* The player's sprites (0 Mario1, 1 its back, 2.. MarioB1.. as the decomp's player_sprite_names.yaml), made
 * on first use and kept; NULL without a ROM. */
struct Pm64Sprite;
const struct Pm64Sprite* portPm64PlayerSprite(int index);
int portPm64MarioLookOn(void);
void portPm64MarioLookSet(int on);
/* TTYD Mario's animation name to Mario1's animation */
int portPm64MarioAnim(const char* ttyd);

/* --- drawing (Aurora): an RGBA8 GX texture made from the ROM, on the 2D camera,
 * centred at (x, y), w x h; u, v the part of the texture used (its size over the
 * tiles' rounded-up size); tinted by rgba (0xRRGGBBAA). */
void portPm64Quad2D(const void* tex, int texW, int texH, float u, float v, float x, float y, float w, float h,
                    unsigned rgba);
/* A sprite's animation drawn on the 2D camera with its feet at (x, y), scale a
 * sprite pixel; anim stepped by the caller. */
struct Pm64Anim;
void portPm64AnimDraw2D(const struct Pm64Anim* a, int palette, float x, float y, float scale, unsigned rgba);

/* --- the Journal's Paper Mario 64 Tattle Log (pm64_journal_aurora.c) */

struct WindowWork;
int portPm64JournalEntry(void);
void portPm64JournalButton(struct WindowWork* wp, float x, float y, int selected); /* pause_settings_aurora.c */
void portPm64JournalOpen(struct WindowWork* wp);
int portPm64JournalMain(struct WindowWork* wp); /* 0 stay, -1 back to the entries, -2 the menu closed */
void portPm64JournalDisp(struct WindowWork* wp, float x, float y);
const char* portPm64JournalMsg(const char* tag);

#ifdef __cplusplus
}
#endif

#endif
