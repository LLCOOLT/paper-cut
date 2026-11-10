/* Local achievements (port/include/port/practice.h): read from the save's own
 * flags and the pouch, so a save from anywhere counts what it has done. Kept
 * per save file beside the memory cards, not in the .gci:
 *   <user>/Achievements/<game id>/file<N>.txt  "name=<player>", then "<id> <date>"
 * Given only while the "achievements" setting is on and the practice tools
 * haven't changed the game since the file was loaded; what a file has already
 * done when it's loaded is recorded quietly, without a toast.
 *
 * 0.5.0: grouped on the page (a category each), each with an item's icon; Paper
 * Mario 64's listed only with a ROM linked; secrets named once earned. In co-op
 * every game present counts what the party does (its own record). */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "battle/battle.h"
#include "battle/battle_mario.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "port/file_write.h"
#include "port/achievements.h"
#include "port/area_maps.h"
#include "port/battlex.h"
#include "port/brothers.h"
#include "port/luigi.h"
#include "port/master_quest.h"
#include "port/player_menu.h"
#include "port/players.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_media.h"
#include "port/postgame_extra.h"
#include "port/practice.h"
#include "port/mode_lending.h"
int portPracticeInGame(void); /* (port/include/port/practice_tools.h, whose PORT_STAT_* differ from stats.h's) */
#include "port/settings.h"
#include "port/stats.h"
#include "port/together.h"
#include "port/toggles.h"
#include "port/tr.h"
#include "win/win_main.h"

typedef enum Kind {
    K_STORY,   /* GSW 0 reached a */
    K_FLAG,    /* GSWF a set */
    K_STAR_PIECES,
    K_SHINES,
    K_RECIPES,
    K_BADGES,
    K_TATTLES,
    K_TROUBLES, /* of the 30 (a: the goal) */
    K_PARTNERS,
    K_LEVEL,
    K_COINS,
    K_COUNTER, /* a counter of the modes' (port/include/port/achievements.h) reached a */
    K_ROOMS,   /* rooms visited on the area maps (a: the goal, 0 all) */
    K_AREA,    /* every room of one area visited (key: the area) */
    K_GOLD,    /* every Tattle, badge and recipe (the Gold Medal, port/include/port/postgame_extra.h) */
    /* 0.5.0 */
    K_BITS,      /* a counter used as a set (portAchievementCounterBit): a of its bits */
    K_BIT,       /* the same: bit b set */
    K_STAT,      /* the lifetime stat b (port/include/port/stats.h) reached a */
    K_PARTY,     /* each of the six party games won */
    K_PIPES,     /* the underground's warp pipes opened */
    K_EMAILS,    /* a Mailbox SP letters read */
    K_STAR_POWER, /* the Star Power gauge's maximum (100 a gauge) */
    K_FULL_BAG,  /* the bag's 20 places filled */
    K_WORN,      /* a different badges worn (the "worn" counters' bits) */
    K_SHOP_PRIZE, /* a shop-points prize claimed */
    K_BROKE,     /* no coins after chapter 1 */
    K_PM64_DISHES, /* Paper Mario 64 dishes cooked (a; 0 all) */
    K_PM64_FOES,   /* Paper Mario 64 enemies met (a; 0 all) */
} Kind;

/* the page's groups (portAchievementCategoryName) */
enum { C_STORY, C_EXPLORE, C_COLLECT, C_BATTLE, C_PLAYAS, C_MODES, C_MULTI, C_PM64, C_SECRET, C_COUNT };
static const char* const kCategories[C_COUNT] = {
    "Story", "Exploration", "Collection", "Battle", "Play as", "Modes", "Multiplayer", "Paper Mario 64", "Secrets",
};
#define F_PM64 1   /* listed only with a Paper Mario 64 ROM linked */
#define F_SECRET 2 /* named once earned */

typedef struct Achievement {
    const char* id; /* in the file: never renamed */
    const char* name;
    const char* note;
    Kind kind;
    int a;           /* the story value or flag; for counts, the goal (0: all there are) */
    const char* key; /* K_COUNTER's, K_BITS' and K_BIT's counter, K_AREA's area */
    int b;           /* K_BIT's bit, K_STAT's stat */
    int cat;         /* C_* */
    int flags;       /* F_* */
    int item;        /* the item whose icon is its picture */
} Achievement;

static const Achievement kList[] = {
    {"prologue", "A Paper Adventure", "Finish the prologue.", K_STORY, STORY_STG1_00_PETAL_MEADOWS, NULL, 0, C_STORY, 0,
     ITEM_MAGICAL_MAP},
    {"chapter1", "Castle Crasher", "Finish Chapter 1: Castle and Dragon.", K_STORY, STORY_STG2_00_BOGGLY_WOODS, NULL, 0,
     C_STORY, 0, ITEM_DIAMOND_STAR},
    {"chapter2", "Tree Hugger", "Finish Chapter 2: The Great Boggly Tree.", K_STORY,
     STORY_STG3_00_WELCOME_TO_GLITZVILLE, NULL, 0, C_STORY, 0, ITEM_EMERALD_STAR},
    {"chapter3", "Champ", "Finish Chapter 3: Of Glitz and Glory.", K_STORY, STORY_STG4_00_WELCOME_TO_STAGE_4, NULL, 0,
     C_STORY, 0, ITEM_GOLD_STAR},
    {"chapter4", "Name Recalled", "Finish Chapter 4: For Pigs the Bell Tolls.", K_STORY, STORY_STG5_00_LOGBOOK, NULL, 0,
     C_STORY, 0, ITEM_RUBY_STAR},
    {"chapter5", "Shipwrecked", "Finish Chapter 5: The Key to Pirates.", K_STORY, STORY_STG6_00_START, NULL, 0, C_STORY,
     0, ITEM_SAPPHIRE_STAR},
    {"chapter6", "All Aboard", "Finish Chapter 6: 3 Days of Excess.", K_STORY, STORY_STG7_00_START, NULL, 0, C_STORY, 0,
     ITEM_GARNET_STAR},
    {"chapter7", "Moonwalker", "Finish Chapter 7: Mario Shoots the Moon.", K_STORY, STORY_STG8_00_START, NULL, 0, C_STORY,
     0, ITEM_CRYSTAL_STAR},
    {"chapter8", "The Door Closes", "Defeat the Shadow Queen.", K_STORY, STORY_STG8_END_AFTER_THE_BATTLE, NULL, 0, C_STORY,
     0, ITEM_STAR_KEY},
    {"credits", "Happily Ever After", "See the end credits.", K_STORY, STORY_STG9_01_END_CREDITS, NULL, 0, C_STORY, 0,
     ITEM_PEACH_TART},
    {"star_pieces_half", "Shiny Things", "Find 50 Star Pieces.", K_STAR_PIECES, 50, NULL, 0, C_COLLECT, 0,
     ITEM_STAR_PIECE},
    {"star_pieces_all", "Star Piece Collector", "Find every Star Piece.", K_STAR_PIECES, 0, NULL, 0, C_COLLECT, 0,
     ITEM_STAR_PIECE},
    {"shine_sprites_all", "Sprite Hunter", "Find every Shine Sprite.", K_SHINES, 0, NULL, 0, C_COLLECT, 0,
     ITEM_SHINE_SPRITE},
    {"recipe_first", "First Course", "Have Zess T. cook a dish.", K_FLAG, GSWF_RECIPE_LOG_UNLOCKED, NULL, 0, C_COLLECT, 0,
     ITEM_COOKBOOK},
    {"recipes_all", "Master Chef", "Fill in every recipe in the Journal.", K_RECIPES, 0, NULL, 0, C_COLLECT, 0,
     ITEM_ZESS_DELUXE},
    {"badges_half", "Badge Fan", "Find 40 kinds of badge.", K_BADGES, 40, NULL, 0, C_COLLECT, 0, ITEM_POWER_PLUS},
    {"badges_all", "Badge Log Complete", "Find every kind of badge.", K_BADGES, 0, NULL, 0, C_COLLECT, 0,
     ITEM_POWER_PLUS_P},
    {"tattles_half", "Know Your Enemy", "Tattle 60 kinds of enemy.", K_TATTLES, 60, NULL, 0, C_COLLECT, 0,
     ITEM_CONTACT_LENS},
    {"tattles_all", "Tattle Log Complete", "Tattle every enemy in the Tattle Log.", K_TATTLES, 0, NULL, 0, C_COLLECT, 0,
     ITEM_CONTACT_LENS},
    {"pit_50", "Halfway Down", "Reach the 50th floor of the Pit of 100 Trials.", K_FLAG, GSWF_JON_FLOOR50_CHEST_OPENED,
     NULL, 0, C_BATTLE, 0, ITEM_STRANGE_SACK},
    {"pit_cleared", "Pit of 100 Trials", "Defeat Bonetail at the bottom of the Pit.", K_FLAG, GSWF_JON_BONETAIL_DEFEATED,
     NULL, 0, C_BATTLE, 0, ITEM_RETURN_POSTAGE},
    {"troubles_all", "Trouble Shooter", "Solve every trouble at the Trouble Center.", K_TROUBLES, 30, NULL, 0, C_EXPLORE,
     0, ITEM_ROUTING_SLIP},
    {"partners_all", "Full Party", "Have all seven partners join.", K_PARTNERS, 7, NULL, 0, C_STORY, 0,
     ITEM_HAPPY_HEART},
    {"level_20", "Seasoned", "Reach level 20.", K_LEVEL, 20, NULL, 0, C_COLLECT, 0, ITEM_UP_ARROW},
    {"coins_999", "Rich Uncle", "Carry 999 coins.", K_COINS, 999, NULL, 0, C_COLLECT, 0, ITEM_COIN},
    /* the post-game places and the modes. To add one: a row here
     * (a new id, never renamed) and, for a mode, a counter it counts with
     * portAchievementCounterAdd / portAchievementCounterMax
     * (port/include/port/achievements.h; most are watched from the modes' own
     * calls in port/src/aurora/achievements_modes_aurora.c). */
    {"maps_rogueport", "Know Your Way Around", "Visit every room of Rogueport on its area map.", K_AREA, 0, "gor", 0,
     C_EXPLORE, 0, ITEM_MAGICAL_MAP},
    {"maps_sewers", "Down Under", "Visit every room of Rogueport's underground.", K_AREA, 0, "tik", 0, C_EXPLORE, 0,
     ITEM_MAGICAL_MAP},
    {"maps_100", "Explorer", "Visit 100 rooms on the area maps.", K_ROOMS, 100, NULL, 0, C_EXPLORE, 0,
     ITEM_MAGICAL_MAP_LARGE},
    {"maps_all", "Cartographer", "Visit every room on the area maps.", K_ROOMS, 0, NULL, 0, C_EXPLORE, 0,
     ITEM_MAGICAL_MAP_LARGE},
    {"trophy_visit", "Hall of Fame", "Visit the trophy room.", K_COUNTER, 1, "trophy_visit", 0, C_EXPLORE, 0,
     ITEM_CHAMPS_BELT},
    {"trophy_full", "A Full Shelf", "Visit the trophy room with every Crystal Star and every boss in it.", K_COUNTER, 1,
     "trophy_full", 0, C_EXPLORE, 0, ITEM_CRYSTAL_STAR},
    {"tower_10", "Ten Floors Up", "Reach the 10th floor of the Paper Tower.", K_COUNTER, 10, "tower_floor", 0, C_MODES, 0,
     ITEM_UP_ARROW},
    {"tower_20", "Thin Air", "Reach the 20th floor of the Paper Tower.", K_COUNTER, 20, "tower_floor", 0, C_MODES, 0,
     ITEM_UP_ARROW},
    {"tower_top", "Top of the Tower", "Climb all 30 floors of the Paper Tower.", K_COUNTER, 1, "tower_cleared", 0,
     C_MODES, 0, ITEM_UP_ARROW},
    {"tower_fast", "Paper Airplane", "Climb the Paper Tower in under 30 minutes.", K_COUNTER, 1, "tower_fast", 0, C_MODES,
     0, ITEM_STOPWATCH},
    {"daily_first", "Bargain Hunter", "Buy a daily shop special.", K_COUNTER, 1, "daily_bought", 0, C_COLLECT, 0,
     ITEM_SILVER_CARD},
    {"daily_10", "Regular Customer", "Buy 10 daily shop specials.", K_COUNTER, 10, "daily_bought", 0, C_COLLECT, 0,
     ITEM_GOLD_CARD},
    {"versus_first", "Brotherly Rivalry", "Finish a versus battle.", K_COUNTER, 1, "versus_battles", 0, C_MULTI, 0,
     ITEM_L_EMBLEM},
    {"versus_10", "Old Rivals", "Finish 10 versus battles.", K_COUNTER, 10, "versus_battles", 0, C_MULTI, 0,
     ITEM_L_EMBLEM},
    {"rando_file", "Shuffled", "Play an open-world randomizer file.", K_COUNTER, 1, "rando_file", 0, C_MODES, 0,
     ITEM_MYSTERY},
    {"rando_3", "Stars Anywhere", "Hold three Crystal Stars on an open-world file.", K_COUNTER, 3, "rando_stars", 0,
     C_MODES, 0, ITEM_EMERALD_STAR},
    {"rando_7", "Seed Master", "Hold all seven Crystal Stars on an open-world file.", K_COUNTER, 7, "rando_stars", 0,
     C_MODES, 0, ITEM_CRYSTAL_STAR},
    {"ghost_race", "Racing a Ghost", "Race a ghost.", K_COUNTER, 1, "ghost_race", 0, C_MODES, 0, ITEM_BOOS_SHEET},
    {"ghost_ahead", "Faster Than Your Shadow", "Get 10 seconds ahead of a ghost.", K_COUNTER, 1, "ghost_ahead", 0,
     C_MODES, 0, ITEM_STOPWATCH},
    {"pitcoop_25", "Better Together", "Reach the 25th floor of the Pit in co-op.", K_COUNTER, 25, "pitcoop_floor", 0,
     C_MULTI, 0, ITEM_STRANGE_SACK},
    {"pitcoop_100", "Rock Bottom, Together", "Reach the Pit's 100th floor in co-op.", K_COUNTER, 100, "pitcoop_floor", 0,
     C_MULTI, 0, ITEM_RETURN_POSTAGE},
    {"bossrush_won", "Boss Rush", "Win a boss rush.", K_COUNTER, 1, "bossrush_won", 0, C_MODES, 0, ITEM_SKULL_GEM},
    {"glitz_10", "Crowd Favorite", "Win 10 rounds in one endless Glitz Pit run.", K_COUNTER, 10, "glitz_rounds", 0,
     C_MODES, 0, ITEM_CHAMPS_BELT},
    {"refight_won", "Rematch", "Win a boss refight.", K_COUNTER, 1, "refight_won", 0, C_MODES, 0, ITEM_QUAKE_HAMMER},
    {"chapter_replay", "Once More, With Feeling", "Finish a chapter again in chapter select.", K_COUNTER, 1,
     "chapter_replay", 0, C_MODES, 0, ITEM_RAGGED_DIARY},
    {"hardcore_ch1", "One Life", "Finish chapter 1 on a hardcore (one-life) file.", K_COUNTER, 1, "hardcore_ch1", 0,
     C_MODES, 0, ITEM_LAST_STAND},
    {"remix_chapter", "Remixed", "Finish a chapter with Remix: enemies on.", K_COUNTER, 1, "remix_chapter", 0, C_MODES, 0,
     ITEM_MYSTERY},
    {"luigi_chapter", "Green Thunder", "Finish a chapter playing as Luigi.", K_COUNTER, 1, "luigi_chapter", 0, C_PLAYAS,
     0, ITEM_L_EMBLEM},
    {"photo_first", "Say Cheese", "Take a picture in photo mode.", K_COUNTER, 1, "photos", 0, C_MODES, 0,
     ITEM_MARIO_POSTER},
    {"photo_25", "Photo Album", "Take 25 pictures in photo mode.", K_COUNTER, 25, "photos", 0, C_MODES, 0,
     ITEM_MARIO_POSTER},
    {"online_coop", "Two Plumbers, Two Places", "Play online co-op.", K_COUNTER, 1, "online_coop", 0, C_MULTI, 0,
     ITEM_MAILBOX_SP},
    {"raid_first", "Raid Party", "Beat a raid boss.", K_COUNTER, 1, "raid_wins", 0, C_MULTI, 0, ITEM_SUPERBOMBOMB},
    {"raid_5", "Weekly Regulars", "Beat five raid bosses.", K_COUNTER, 5, "raid_wins", 0, C_MULTI, 0, ITEM_SUPERBOMBOMB},
    {"raid_full", "Full House", "Beat a raid boss with four players.", K_COUNTER, 1, "raid_full", 0, C_MULTI, 0,
     ITEM_SUPERBOMBOMB},
    {"raid_all", "Every Boss in Turn", "Beat each of the raid rotation's six bosses.", K_COUNTER, 6, "raid_kinds", 0,
     C_MULTI, 0, ITEM_SKULL_GEM},
    /* 0.5.0: more after the story (port/include/port/postgame_extra.h) */
    {"gold_medal", "Gold Medal", "Tattle every enemy, find every badge and have every recipe cooked.", K_GOLD, 3, NULL, 0,
     C_COLLECT, 0, ITEM_GOLD_BAR},
    {"pit_whacka", "Bump in the Road", "Beat Whacka at the bottom of the Pit of 100 Trials.", K_FLAG,
     PORT_GSWF_PG_WHACKA_BEATEN, NULL, 0, C_BATTLE, 0, ITEM_WHACKA_BUMP},
    {"pg_champion", "Talon Tamer", "Beat the returning champion in the Glitz Pit.", K_FLAG, PORT_GSWF_PG_CHAMPION_BEATEN,
     NULL, 0, C_BATTLE, 0, ITEM_CHAMPS_BELT},
    {"pg_nighttail", "Lights Out", "Beat Nighttail in the Palace of Shadow.", K_FLAG, PORT_GSWF_PG_NIGHTTAIL_BEATEN, NULL,
     0, C_BATTLE, 0, ITEM_SKULL_GEM},
    {"pg_boo_quiz", "Ghost Scholar", "Answer every question of the Boo quiz in Creepy Steeple.", K_FLAG,
     PORT_GSWF_PG_QUIZ_WON, NULL, 0, C_EXPLORE, 0, ITEM_BOOS_SHEET},
    {"pg_newsletter", "Extra! Extra!", "Deliver every newsletter for the Rogueport paper.", K_FLAG, PORT_GSWF_PG_NEWS_DONE,
     NULL, 0, C_EXPLORE, 0, ITEM_DUBIOUS_PAPER},

    /* --- 0.5.0's batch ------------------------------------------------------------------------------------ */
    /* Paper Mario 64 (a ROM linked): the partners out with Mario (bit = PM64_PARTNER_*), then their field moves */
    {"pm64_goombario", "Goomba Scholar", "Have Goombario join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_GOOMBARIO,
     C_PM64, F_PM64, ITEM_CONTACT_LENS},
    {"pm64_kooper", "Shell Shocked", "Have Kooper join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_KOOPER, C_PM64, F_PM64,
     ITEM_COURAGE_SHELL},
    {"pm64_bombette", "Pink and Explosive", "Have Bombette join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_BOMBETTE,
     C_PM64, F_PM64, ITEM_FIRE_FLOWER},
    {"pm64_parakarry", "Special Delivery", "Have Parakarry join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_PARAKARRY,
     C_PM64, F_PM64, ITEM_PACKAGE},
    {"pm64_watt", "Bright Idea", "Have Watt join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_WATT, C_PM64, F_PM64,
     ITEM_THUNDER_BOLT},
    {"pm64_sushie", "Nanny on Board", "Have Sushie join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_SUSHIE, C_PM64, F_PM64,
     ITEM_SNOW_BUNNY},
    {"pm64_lakilester", "On Cloud Nine", "Have Lakilester join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_LAKILESTER,
     C_PM64, F_PM64, ITEM_SPIKE_SHIELD},
    {"pm64_bow", "Boo-tiful", "Have Bow join you.", K_BIT, 0, "pm64_party", PM64_PARTNER_BOW, C_PM64, F_PM64,
     ITEM_BOOS_SHEET},
    {"pm64_party_all", "Old Friends", "Have all eight Paper Mario 64 partners join you.", K_BITS, 8, "pm64_party", 0,
     C_PM64, F_PM64, ITEM_HAPPY_HEART},
    {"pm64_move_goombario", "Know-It-All", "Use Goombario's move in the field.", K_BIT, 0, "pm64_field",
     PM64_PARTNER_GOOMBARIO, C_PM64, F_PM64, ITEM_CONTACT_LENS},
    {"pm64_move_kooper", "Shell Game", "Toss Kooper's shell in the field.", K_BIT, 0, "pm64_field", PM64_PARTNER_KOOPER,
     C_PM64, F_PM64, ITEM_COURAGE_SHELL},
    {"pm64_move_bombette", "Blast Radius", "Set off Bombette in the field.", K_BIT, 0, "pm64_field",
     PM64_PARTNER_BOMBETTE, C_PM64, F_PM64, ITEM_FIRE_FLOWER},
    {"pm64_move_parakarry", "Airmail", "Fly with Parakarry.", K_BIT, 0, "pm64_field", PM64_PARTNER_PARAKARRY, C_PM64,
     F_PM64, ITEM_PACKAGE},
    {"pm64_move_watt", "Lights On", "Light the way with Watt.", K_BIT, 0, "pm64_field", PM64_PARTNER_WATT, C_PM64, F_PM64,
     ITEM_THUNDER_BOLT},
    {"pm64_move_sushie", "Smooth Sailing", "Swim with Sushie.", K_BIT, 0, "pm64_field", PM64_PARTNER_SUSHIE, C_PM64,
     F_PM64, ITEM_SNOW_BUNNY},
    {"pm64_move_lakilester", "Spike Strip", "Ride Lakilester.", K_BIT, 0, "pm64_field", PM64_PARTNER_LAKILESTER, C_PM64,
     F_PM64, ITEM_SPIKE_SHIELD},
    {"pm64_move_bow", "Now You See Me", "Hide with Bow.", K_BIT, 0, "pm64_field", PM64_PARTNER_BOW, C_PM64, F_PM64,
     ITEM_BOOS_SHEET},
    /* the bosses (pm64_bosses/registry.h's order), refought or in a boss rush */
    {"pm64_boss_jr_troopa", "Playground Bully", "Beat Jr. Troopa.", K_BIT, 0, "pm64_bosses", 0, C_PM64, F_PM64,
     ITEM_HAMMER},
    {"pm64_boss_koopa_bros", "Four Shells Down", "Beat the Koopa Bros.", K_BIT, 0, "pm64_bosses", 1, C_PM64, F_PM64,
     ITEM_COURAGE_SHELL},
    {"pm64_boss_tutankoopa", "Mummy's the Word", "Beat Tutankoopa.", K_BIT, 0, "pm64_bosses", 2, C_PM64, F_PM64,
     ITEM_RUIN_POWDER},
    {"pm64_boss_tubba", "Heart of the Matter", "Beat Tubba Blubba.", K_BIT, 0, "pm64_bosses", 3, C_PM64, F_PM64,
     ITEM_HAPPY_HEART},
    {"pm64_boss_general_guy", "Toy Box Shake-Up", "Beat General Guy.", K_BIT, 0, "pm64_bosses", 4, C_PM64, F_PM64,
     ITEM_BOX},
    {"pm64_boss_lava_piranha", "Hot Potted Plant", "Beat Lava Piranha.", K_BIT, 0, "pm64_bosses", 5, C_PM64, F_PM64,
     ITEM_FIRE_POP},
    {"pm64_boss_huff", "Blown Away", "Beat Huff N. Puff.", K_BIT, 0, "pm64_bosses", 6, C_PM64, F_PM64, ITEM_THUNDER_RAGE},
    {"pm64_boss_crystal_king", "Shattered Ice", "Beat the Crystal King.", K_BIT, 0, "pm64_bosses", 7, C_PM64, F_PM64,
     ITEM_ICE_STORM},
    {"pm64_boss_kammy", "Grounded Broom", "Beat Kammy Koopa.", K_BIT, 0, "pm64_bosses", 8, C_PM64, F_PM64,
     ITEM_SLOW_SHROOM},
    {"pm64_boss_bowser", "Koopa King, Again", "Beat Paper Mario 64's Bowser.", K_BIT, 0, "pm64_bosses", 9, C_PM64, F_PM64,
     ITEM_BOWSER_BROS_MEAT},
    {"pm64_bosses_all", "Seven Stars Saved", "Beat all ten Paper Mario 64 bosses.", K_BITS, 10, "pm64_bosses", 0, C_PM64,
     F_PM64, ITEM_SKULL_GEM},
    {"pm64_rush", "Two Adventures, One Rush", "Win a boss rush with the Paper Mario 64 bosses in it.", K_COUNTER, 1,
     "pm64_rush", 0, C_PM64, F_PM64, ITEM_SKULL_GEM},
    {"pm64_dish_first", "Tayce T.'s Apprentice", "Cook a Paper Mario 64 dish.", K_PM64_DISHES, 1, NULL, 0, C_PM64, F_PM64,
     ITEM_COOKBOOK},
    {"pm64_dishes_all", "Toad Town Gourmet", "Cook every Paper Mario 64 dish.", K_PM64_DISHES, 0, NULL, 0, C_PM64, F_PM64,
     ITEM_ZESS_DELUXE},
    {"pm64_rowf_all", "Rowf's Best Customer", "Own every badge from Rowf's shop.", K_BITS, 0, "pm64_rowf", 0, C_PM64,
     F_PM64, ITEM_POWER_PLUS},
    {"pm64_foes_20", "Mushroom Kingdom Field Guide", "Meet 20 kinds of Paper Mario 64 enemy.", K_PM64_FOES, 20, NULL, 0,
     C_PM64, F_PM64, ITEM_CONTACT_LENS},
    {"pm64_foes_all", "Know Your Goombas", "Meet every Paper Mario 64 enemy in its Tattle Log.", K_PM64_FOES, 0, NULL, 0,
     C_PM64, F_PM64, ITEM_CONTACT_LENS},
    {"pm64_spirit", "Star Spirit Sighting", "See a Star Spirit cheer on a Special move.", K_BITS, 1, "pm64_spirits", 0,
     C_PM64, F_PM64, ITEM_SHOOTING_STAR},
    {"pm64_spirits_all", "All Seven Stars", "See all seven Star Spirits cheer on Special moves.", K_BITS, 7,
     "pm64_spirits", 0, C_PM64, F_PM64, ITEM_SHOOTING_STAR},

    /* multiplayer: in co-op each game present counts */
    {"coop_chapter", "Two Heads Are Better", "Finish a chapter with two or more players.", K_COUNTER, 1, "coop_chapter",
     0, C_MULTI, 0, ITEM_HAPPY_HEART},
    {"coop_four", "Full Table", "Play with four players at once.", K_COUNTER, 1, "coop_four", 0, C_MULTI, 0,
     ITEM_COUPLES_CAKE},
    {"brothers_chapter", "Super Mario Bros.", "Finish a chapter in Brothers mode.", K_COUNTER, 1, "brothers_chapter", 0,
     C_MULTI, 0, ITEM_L_EMBLEM},
    {"lent_partner", "Borrowed Buddy", "Have a lent partner join another player.", K_COUNTER, 1, "lent_partner", 0,
     C_MULTI, 0, ITEM_PRESENT},
    {"crowd_throw", "From the Cheap Seats", "Land a throw from the audience.", K_COUNTER, 1, "crowd_throws", 0, C_MULTI, 0,
     ITEM_AUDIENCE_CAN},
    {"crowd_10", "Heckler", "Land 10 throws from the audience.", K_COUNTER, 10, "crowd_throws", 0, C_MULTI, 0,
     ITEM_AUDIENCE_ROCK},
    {"emotes_all", "Expressive", "Use all six emotes.", K_BITS, 6, "emotes", 0, C_MULTI, 0, ITEM_HAPPY_FLOWER},
    {"whos_who", "Musical Chairs", "Swap seats with Who's who.", K_COUNTER, 1, "seat_swaps", 0, C_MULTI, 0,
     ITEM_POINT_SWAP},
    {"team_bonus", "Teamwork!", "Earn a team bonus.", K_COUNTER, 1, "team_bonus", 0, C_MULTI, 0, ITEM_SUPER_APPEAL},
    {"team_bonus_10", "Dream Team", "Earn 10 team bonuses.", K_COUNTER, 10, "team_bonus", 0, C_MULTI, 0,
     ITEM_SUPER_APPEAL_P},
    {"versus_won", "Bragging Rights", "Win a versus battle.", K_COUNTER, 1, "versus_won", 0, C_MULTI, 0, ITEM_W_EMBLEM},
    {"quiz_won", "Quiz Whiz", "Win the party quiz.", K_COUNTER, 1, "quiz_won", 0, C_MULTI, 0, ITEM_DUBIOUS_PAPER},
    {"cards_won", "Card Shark", "Win a Tattle-card duel.", K_COUNTER, 1, "cards_won", 0, C_MULTI, 0, ITEM_SPECIAL_CARD},
    {"bingo_line", "Bingo!", "Complete a line in Bingo.", K_COUNTER, 1, "bingo_line", 0, C_MULTI, 0, ITEM_LOTTERY_PICK},
    {"party_all", "Party Animal", "Win each party game: Versus, Raid, Boss rush, Quiz, Tattle-card duel and Bingo.",
     K_PARTY, 6, NULL, 0, C_MULTI, 0, ITEM_CAKE},
    {"online_four", "Long-Distance Party", "Play online with four games linked.", K_COUNTER, 1, "online_four", 0, C_MULTI,
     0, ITEM_MAILBOX_SP},
    {"online_guest", "Plus One", "Join someone else's online game as a guest.", K_COUNTER, 1, "online_guest", 0, C_MULTI,
     0, ITEM_TRAIN_TICKET},
    {"coop_copy", "Souvenir", "As a guest, keep the co-op copy in a file.", K_COUNTER, 1, "coop_copy", 0, C_MULTI, 0,
     ITEM_PACKAGE},

    /* battle feats */
    {"superguards_100", "Iron Wall", "Superguard 100 times.", K_STAT, 100, NULL, PORT_STAT_SUPERGUARDS, C_BATTLE, 0,
     ITEM_DEFEND_PLUS},
    {"stylish_500", "Showboat", "Pull off 500 Stylish moves.", K_STAT, 500, NULL, PORT_STAT_STYLISH, C_BATTLE, 0,
     ITEM_SUPER_APPEAL},
    {"boss_flawless", "Untouchable", "Beat a chapter boss without Mario taking any damage.", K_COUNTER, 1,
     "boss_flawless", 0, C_BATTLE, 0, ITEM_DAMAGE_DODGE},
    {"win_1hp", "By a Thread", "Win a battle with Mario at 1 HP.", K_COUNTER, 1, "win_1hp", 0, C_BATTLE, 0,
     ITEM_LAST_STAND},
    {"win_one_turn", "One and Done", "Win a battle in a single turn.", K_COUNTER, 1, "win_one_turn", 0, C_BATTLE, 0,
     ITEM_STOPWATCH},
    {"win_danger", "Danger Zone", "Win a battle with Mario in Danger.", K_COUNTER, 1, "win_danger", 0, C_BATTLE, 0,
     ITEM_HP_DRAIN_BADGE},
    {"specials_all", "Star Studded", "Use all eight Special moves.", K_BITS, 8, "specials", 0, C_BATTLE, 0,
     ITEM_SHOOTING_STAR},
    {"hammer_only", "Hammer Time", "Win a battle with Mario using only his hammer.", K_COUNTER, 1, "hammer_only", 0,
     C_BATTLE, 0, ITEM_HAMMER},
    {"first_strike_50", "Strike First", "Strike first in the field 50 times.", K_COUNTER, 50, "first_strikes", 0,
     C_BATTLE, 0, ITEM_FIRST_ATTACK},
    {"battles_500", "Seasoned Brawler", "Win 500 battles.", K_STAT, 500, NULL, PORT_STAT_BATTLES_WON, C_BATTLE, 0,
     ITEM_POWER_PUNCH},
    {"enemies_1000", "Thousand Foes", "Defeat 1,000 enemies.", K_STAT, 1000, NULL, PORT_STAT_ENEMIES, C_BATTLE, 0,
     ITEM_POWER_SMASH},
    {"grade_s", "Top Marks", "Earn an S battle grade.", K_STAT, 1, NULL, PORT_STAT_GRADE_S, C_BATTLE, 0, ITEM_GOLD_STAR},
    {"appeals_100", "Crowd Pleaser", "Appeal to the audience 100 times.", K_STAT, 100, NULL, PORT_STAT_APPEALS, C_BATTLE,
     0, ITEM_SUPER_APPEAL_P},

    /* play as */
    {"peach_chapter", "Royal Rescue", "Finish a chapter playing as Peach.", K_BIT, 0, "playas", PORT_PLAY_AS_PEACH - 1,
     C_PLAYAS, 0, ITEM_PEACHY_PEACH},
    {"bowser_chapter", "King of the Koopas", "Finish a chapter playing as Bowser.", K_BIT, 0, "playas",
     PORT_PLAY_AS_BOWSER - 1, C_PLAYAS, 0, ITEM_BOWSER_BROS_MEAT},
    {"playas_all", "Change of Cast", "Finish a chapter as Luigi, as Peach and as Bowser.", K_BITS, 3, "playas", 0,
     C_PLAYAS, 0, ITEM_SUPER_LUIGI1},
    {"luigi_three", "Year of Luigi", "Finish three chapters playing as Luigi.", K_COUNTER, 3, "luigi_chapters", 0,
     C_PLAYAS, 0, ITEM_SUPER_LUIGI2},
    {"cool_chapter", "Too Cool", "Finish a chapter with Cool Mode on.", K_COUNTER, 1, "cool_chapter", 0, C_PLAYAS, 0,
     ITEM_ICICLE_POP},

    /* exploration */
    {"troubles_10", "Odd Jobs", "Solve 10 troubles at the Trouble Center.", K_TROUBLES, 10, NULL, 0, C_EXPLORE, 0,
     ITEM_ROUTING_SLIP},
    {"shines_early", "Early Bird", "Find 10 Shine Sprites before Chapter 3 is over.", K_COUNTER, 10, "shines_early", 0,
     C_EXPLORE, 0, ITEM_SHINE_SPRITE},
    {"shines_half", "Sprite Spotter", "Find 21 Shine Sprites.", K_SHINES, 21, NULL, 0, C_COLLECT, 0, ITEM_SHINE_SPRITE},
    {"sewer_pipes", "Plumbing Inspector", "Open every warp pipe in Rogueport's underground.", K_PIPES, 5, NULL, 0,
     C_EXPLORE, 0, ITEM_UP_ARROW},
    {"emails_30", "Pen Pal", "Read 30 letters on the Mailbox SP.", K_EMAILS, 30, NULL, 0, C_EXPLORE, 0, ITEM_MAILBOX_SP},
    {"maps_hei", "Stop and Smell the Petals", "Visit every room of Petal Meadows.", K_AREA, 0, "hei", 0, C_EXPLORE, 0,
     ITEM_FLOWER},
    {"maps_gon", "Castle Crawler", "Visit every room of Hooktail Castle.", K_AREA, 0, "gon", 0, C_EXPLORE, 0,
     ITEM_DIAMOND_STAR},
    {"maps_win", "Into the Woods", "Visit every room of Boggly Woods.", K_AREA, 0, "win", 0, C_EXPLORE, 0,
     ITEM_HORSETAIL},
    {"maps_mri", "Branching Out", "Visit every room of the Great Tree.", K_AREA, 0, "mri", 0, C_EXPLORE, 0,
     ITEM_PUNI_ORB},
    {"maps_tou", "Ringside Seat", "Visit every room of Glitzville.", K_AREA, 0, "tou", 0, C_EXPLORE, 0,
     ITEM_CHAMPS_BELT},
    {"maps_jin", "Steeple Chase", "Visit every room of Creepy Steeple.", K_AREA, 0, "jin", 0, C_EXPLORE, 0,
     ITEM_RUBY_STAR},
    {"maps_muj", "Castaway", "Visit every room of Keelhaul Key.", K_AREA, 0, "muj", 0, C_EXPLORE, 0, ITEM_COCONUT},
    {"maps_dou", "Grotto Spelunker", "Visit every room of the Pirate's Grotto.", K_AREA, 0, "dou", 0, C_EXPLORE, 0,
     ITEM_GROTTO_KEY},
    {"maps_rsh", "Every Car, Every Cabin", "Visit every room of the Excess Express.", K_AREA, 0, "rsh", 0, C_EXPLORE, 0,
     ITEM_TRAIN_TICKET},
    {"maps_bom", "Cold Feet", "Visit every room of Fahr Outpost.", K_AREA, 0, "bom", 0, C_EXPLORE, 0, ITEM_SNOW_BUNNY},
    {"maps_aji", "Infiltrator", "Visit every room of the X-Naut Fortress.", K_AREA, 0, "aji", 0, C_EXPLORE, 0,
     ITEM_DATA_DISK},
    {"maps_las", "Shadow Walker", "Visit every room of the Palace of Shadow.", K_AREA, 0, "las", 0, C_EXPLORE, 0,
     ITEM_SKULL_GEM},

    /* collection */
    {"full_bag", "Packed to the Brim", "Carry 20 items at once.", K_FULL_BAG, 20, NULL, 0, C_COLLECT, 0,
     ITEM_STRANGE_SACK},
    {"badges_worn_50", "Fashion Plate", "Wear 50 different badges.", K_WORN, 50, NULL, 0, C_COLLECT, 0,
     ITEM_DOUBLE_DIP},
    {"star_power_max", "Full Gauge", "Have all eight Star Power gauges.", K_STAR_POWER, 800, NULL, 0, C_COLLECT, 0,
     ITEM_SHOOTING_STAR},
    {"level_50", "Veteran", "Reach level 50.", K_LEVEL, 50, NULL, 0, C_COLLECT, 0, ITEM_UP_ARROW},
    {"level_99", "Maxed Out", "Reach level 99.", K_LEVEL, 99, NULL, 0, C_COLLECT, 0, ITEM_UP_ARROW},
    {"coins_spent", "Big Spender", "Spend 9,999 coins.", K_COUNTER, 9999, "coins_spent", 0, C_COLLECT, 0,
     ITEM_GOLD_BAR_X3},
    {"coins_10000", "Coin Magnet", "Collect 10,000 coins.", K_STAT, 10000, NULL, PORT_STAT_COINS, C_COLLECT, 0,
     ITEM_COIN},
    {"shop_prize", "Points Card", "Claim a shop-points prize.", K_SHOP_PRIZE, 1, NULL, 0, C_COLLECT, 0,
     ITEM_PLATINUM_CARD},

    /* modes */
    {"quick_start_ch1", "Fast Forward", "Start in Rogueport and finish Chapter 1.", K_COUNTER, 1, "quick_start_ch1", 0,
     C_MODES, 0, ITEM_STOPWATCH},
    {"skips_50", "Seen It Before", "Skip 50 scenes.", K_COUNTER, 50, "scene_skips", 0, C_MODES, 0, ITEM_SLOW_GO},
    {"mq_chapter", "Quest Master", "Finish a chapter in Master Quest.", K_COUNTER, 1, "mq_chapter", 0, C_MODES, 0,
     ITEM_SKULL_GEM},
    {"mq_done", "Master of the Door", "Defeat the Shadow Queen in Master Quest.", K_COUNTER, 1, "mq_done", 0, C_MODES, 0,
     ITEM_STAR_KEY},
    {"rando_everything", "Everything Shuffled", "Play an open-world file with every pool, option and entrance shuffle on.",
     K_COUNTER, 1, "rando_everything", 0, C_MODES, 0, ITEM_MYSTERY},
    {"daily_done", "Daily Grind", "Reach the goal of a daily seed.", K_COUNTER, 1, "daily_done", 0, C_MODES, 0,
     ITEM_LOTTERY_PICK},
    {"daily_7", "Seven-Day Streak", "Reach the daily seed's goal seven days in a row.", K_COUNTER, 7, "daily_streak", 0,
     C_MODES, 0, ITEM_LOTTERY_PICK},
    {"hardcore_game", "Iron Plumber", "Defeat the Shadow Queen on a hardcore (one-life) file.", K_COUNTER, 1,
     "hardcore_game", 0, C_MODES, 0, ITEM_LAST_STAND_P},

    /* secrets */
    {"secret_lost", "Better Luck Next Time", "Lose a battle.", K_STAT, 1, NULL, PORT_STAT_BATTLES_LOST, C_SECRET,
     F_SECRET, ITEM_DRIED_SHROOM},
    {"secret_flee_50", "Strategic Retreat", "Run away from 50 battles.", K_STAT, 50, NULL, PORT_STAT_BATTLES_FLED,
     C_SECRET, F_SECRET, ITEM_REPEL_CAPE},
    {"secret_spin_1000", "Dizzy Yet?", "Spin Jump 1,000 times in the field.", K_STAT, 1000, NULL, PORT_STAT_SPIN_JUMPS,
     C_SECRET, F_SECRET, ITEM_DIZZY_DIAL},
    {"secret_marathon", "Marathon", "Walk 42.195 kilometres.", K_STAT, 4219500, NULL, PORT_STAT_DISTANCE, C_SECRET,
     F_SECRET, ITEM_BOOTS},
    {"secret_hammer_1000", "Whack-a-Lot", "Swing the hammer 1,000 times in the field.", K_STAT, 1000, NULL,
     PORT_STAT_FIELD_HAMMERS, C_SECRET, F_SECRET, ITEM_HAMMER},
    {"secret_jumps_10000", "Jumpman", "Jump 10,000 times in the field.", K_STAT, 10000, NULL, PORT_STAT_FIELD_JUMPS,
     C_SECRET, F_SECRET, ITEM_JUMPMAN},
    {"secret_big_hit", "Fifty-Point Smash", "Deal 50 damage in a single hit.", K_STAT, 50, NULL, PORT_STAT_BEST_HIT,
     C_SECRET, F_SECRET, ITEM_POWER_RUSH},
    {"secret_100h", "Dedicated", "Play this file for 100 hours.", K_STAT, 100 * 3600 * 60, NULL, PORT_STAT_PLAY_FRAMES,
     C_SECRET, F_SECRET, ITEM_STOPWATCH},
    {"secret_broke", "Flat Broke", "Have no coins at all after Chapter 1.", K_BROKE, 1, NULL, 0, C_SECRET, F_SECRET,
     ITEM_COIN},
    {"secret_fp_1000", "Flower Power", "Spend 1,000 FP in battle.", K_STAT, 1000, NULL, PORT_STAT_FP_SPENT, C_SECRET,
     F_SECRET, ITEM_FP_PLUS},
};
#define COUNT ((int)(sizeof(kList) / sizeof(kList[0])))

static char sEarned[COUNT][11]; /* "YYYY-MM-DD", or "" */
static char sPath[1024];        /* the file's record, "" with no save file */
static char sFile[128];          /* for the page: "File 1 (Mario)" */
static char sName[32];          /* the player's name the record is for */
static int sFirstCheck;         /* the first check after a load: quiet */
static int* sOn;
static int sGuest; /* the record is a co-op guest's (guestRecord) */

/* the modes' counters (port/include/port/achievements.h), kept in the record as "c <key> <value>" */
#define COUNTERS 128
static struct {
    char key[24];
    int value;
    int raised; /* raised since the record was loaded (portAchievementCounterAdd, _Max, _Bit) */
} sCounters[COUNTERS];
static int sCounterCount;
static int sCountersChanged;

static int counterIndex(const char* key, int make) {
    int i;
    for (i = 0; i < sCounterCount; i++) {
        if (strcmp(sCounters[i].key, key) == 0) {
            return i;
        }
    }
    if (!make || sCounterCount >= COUNTERS) {
        return -1;
    }
    snprintf(sCounters[sCounterCount].key, sizeof(sCounters[0].key), "%s", key);
    sCounters[sCounterCount].value = 0;
    sCounters[sCounterCount].raised = 0;
    return sCounterCount++;
}

static int* counterSlot(const char* key, int make) {
    const int i = counterIndex(key, make);
    return i >= 0 ? &sCounters[i].value : NULL;
}

/* a counter raised since the record was loaded: an achievement it reaches on the first check after the load was
   earned in play just now (a randomizer file's stamp as the map loads: Shuffled), not "already done" before */
static int counterRaisedSinceLoad(const char* key) {
    const int i = key != NULL ? counterIndex(key, 0) : -1;
    return i >= 0 && sCounters[i].raised;
}

/* counted only as the achievements would be given: on, a file loaded, the practice tools not used */
static int counting(void) {
    return sOn != NULL && *sOn && sPath[0] != '\0' && !portPracticeTainted();
}

/* A mode lends Mario a pouch, its maps or the story's words (port/include/port/mode_lending.h): the game state
 * isn't the file's, so nothing is read from it (the raid's level 30, its seven partners and 800 SP earned "Full
 * Party", "Seasoned" and "Full Gauge" on a fresh file, the dojo's badges "Fashion Plate"). The modes' own
 * achievements still count: they come from the modes' calls (the counters), not from the pouch. */
static int lending(void) {
#ifdef TTYD_BACKEND_AURORA
    return portModeLending() != 0;
#else
    return 0;
#endif
}

/* a lent pouch alone (a boss rush on Mario's own loadout is his: its battles count) */
static int lentPouch(void) {
#ifdef TTYD_BACKEND_AURORA
    return (portModeLending() & PORT_LEND_POUCH) != 0;
#else
    return 0;
#endif
}

/* the kinds read from the modes' counters and the lifetime stats, not from the game's state */
static int countersOnly(Kind k) {
    return k == K_COUNTER || k == K_BITS || k == K_BIT || k == K_PARTY || k == K_STAT;
}

static void counterChanged(const char* key) {
    const int i = counterIndex(key, 0);
    if (i >= 0) {
        sCounters[i].raised = 1;
    }
    sCountersChanged = 1;
}

void portAchievementCounterAdd(const char* key, int n) {
    int* v;
    if (key == NULL || n == 0 || !counting() || (v = counterSlot(key, 1)) == NULL) {
        return;
    }
    *v += n;
    counterChanged(key);
}

void portAchievementCounterMax(const char* key, int value) {
    int* v;
    if (key == NULL || !counting() || (v = counterSlot(key, 1)) == NULL || value <= *v) {
        return;
    }
    *v = value;
    counterChanged(key);
}

void portAchievementCounterBit(const char* key, int bit) {
    int* v;
    if (key == NULL || bit < 0 || bit > 30 || !counting() || (v = counterSlot(key, 1)) == NULL ||
        (*v & (1 << bit)) != 0) {
        return;
    }
    *v |= 1 << bit;
    counterChanged(key);
}

int portAchievementCounter(const char* key) {
    const int* v = key != NULL ? counterSlot(key, 0) : NULL;
    return v != NULL ? *v : 0;
}

#define TOASTS 8
static int sToasts[TOASTS];
static double sToastTimes[TOASTS];
static int sToastCount;

/* --- counting ---------------------------------------------------------------- */

static int bitCount(unsigned v) {
    int n = 0;
    for (; v != 0; v &= v - 1) {
        n++;
    }
    return n;
}

static int countNamed(const char* part, int lo, int hi, int* total) {
    int i, n = 0;
    *total = 0;
    for (i = 0; i < portGswfNamesCount; i++) {
        const PortNamedValue* v = &portGswfNames[i];
        if (v->value >= lo && v->value <= hi && strstr(v->name, part) != NULL) {
            (*total)++;
            n += swGet(v->value) ? 1 : 0;
        }
    }
    return n;
}

/* the Journal's own counts (win_log.c) */
static int countRecipes(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = WIN_RECIPE_MIN; i < ITEM_MAX; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            (*total)++;
            n += swGet(WIN_RECIPE_COOKED_SW(i - WIN_RECIPE_MIN)) ? 1 : 0;
        }
    }
    return n;
}

static int countBadges(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = BADGE_MIN; i < 0x153; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            (*total)++;
            n += swGet(i - BADGE_MIN + GSWF_BADGE_FOUND_BASE) ? 1 : 0;
        }
    }
    return n;
}

static int countTattles(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = 1; i < 0xD8; i++) {
        if (battleGetUnitMonosiriPtr(i)->tattleLogTag == NULL || i == UNIT_BELDAM_CHAPTER2 ||
            i == UNIT_MARILYN_CHAPTER2 || i == UNIT_DOOPLISS_CHAPTER4_FIGHT1 || i == UNIT_BOWSER_CHAPTER3) {
            continue;
        }
        (*total)++;
        n += swGet(i + GSWF_TATTLE_BASE) ? 1 : 0;
    }
    return n;
}

/* (port/include/port/postgame_extra.h) */
void portJournalCounts(int* tattles, int* tattleTotal, int* badges, int* badgeTotal, int* recipes, int* recipeTotal) {
    *tattles = countTattles(tattleTotal);
    *badges = countBadges(badgeTotal);
    *recipes = countRecipes(recipeTotal);
}

int portGoldMedal(void) {
    int t[6];
    if (gp == NULL) {
        return 0;
    }
    portJournalCounts(&t[0], &t[1], &t[2], &t[3], &t[4], &t[5]);
    return t[1] > 0 && t[0] >= t[1] && t[2] >= t[3] && t[4] >= t[5];
}

/* the badges worn, as bits of four counters ("worn0".."worn3", 30 badges each: BADGE_MIN + i) */
#define WORN_SETS 4
static int countWorn(void) {
    static const char* const kKeys[WORN_SETS] = {"worn0", "worn1", "worn2", "worn3"};
    int i, n = 0;
    for (i = 0; i < WORN_SETS; i++) {
        n += bitCount((unsigned)portAchievementCounter(kKeys[i]));
    }
    return n;
}

static void wornNow(const PouchData* pouch) {
    static const char* const kKeys[WORN_SETS] = {"worn0", "worn1", "worn2", "worn3"};
    int i;
    for (i = 0; i < MAX_BADGES; i++) {
        const int k = pouch->equippedBadges[i] - BADGE_MIN;
        if (pouch->equippedBadges[i] != 0 && k >= 0 && k < WORN_SETS * 30) {
            portAchievementCounterBit(kKeys[k / 30], k % 30);
        }
    }
}

/* the six party games, each won at least once */
static int partyGamesWon(void) {
    static const char* const kKeys[] = {"versus_won", "raid_wins", "bossrush_won", "quiz_won", "cards_won", "bingo_line"};
    int i, n = 0;
    for (i = 0; i < (int)(sizeof(kKeys) / sizeof(kKeys[0])); i++) {
        n += portAchievementCounter(kKeys[i]) > 0 ? 1 : 0;
    }
    return n;
}

static int progress(const Achievement* a, int* goal) {
    PouchData* pouch = pouchGetPtr();
    int total = 0, n = 0, i;
    switch (a->kind) {
    case K_STORY:
        *goal = 1;
        return swStoryPeek() >= a->a;
    case K_FLAG:
        *goal = 1;
        return swGet(a->a) ? 1 : 0;
    case K_STAR_PIECES:
        n = countNamed("STAR_PIECE", GSWF_GOR00_STAR_PIECE, GSWF_GOR00_STAR_PIECE + 99, &total);
        break;
    case K_SHINES:
        n = countNamed("SHINE_SPRITE", 0, 0x7FFFFFFF, &total);
        break;
    case K_RECIPES:
        n = countRecipes(&total);
        break;
    case K_BADGES:
        n = countBadges(&total);
        break;
    case K_TATTLES:
        n = countTattles(&total);
        break;
    case K_GOLD:
        *goal = 3;
        {
            int t[6];
            portJournalCounts(&t[0], &t[1], &t[2], &t[3], &t[4], &t[5]);
            return (t[0] >= t[1]) + (t[2] >= t[3]) + (t[4] >= t[5]);
        }
    case K_TROUBLES:
        total = 30;
        for (i = 1; i <= total; i++) {
            n += swGet(GSWF_TROUBLE_SOLVED_BASE + i) ? 1 : 0;
        }
        break;
    case K_PARTNERS:
        total = a->a;
        for (i = PARTNER_GOOMBELLA; i <= PARTNER_MSMOWZ; i++) {
            /* (Ms. Mowz's slot held by Paper Mario 64's Watt alone isn't her: port/include/port/pm64.h) */
            n += (pouch->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) && !portPm64SlotStandIn(i) ? 1 : 0;
        }
        break;
    case K_LEVEL:
        *goal = a->a;
        return pouch->level;
    case K_COINS:
        *goal = a->a;
        return pouch->coins;
    case K_COUNTER:
        *goal = a->a;
        return portAchievementCounter(a->key);
    case K_ROOMS:
        n = portAreaMapsVisited(&total);
        break;
    case K_AREA: {
        /* the game's thread and the page's both come here: one each (a shared one
         * filled by both at once would run past its tables) */
        PortAreaView view;
        if (!portAreaMapBuild(a->key, &view)) {
            *goal = 0;
            return 0;
        }
        *goal = view.roomCount;
        return view.visited;
    }
    case K_BITS:
        *goal = a->a != 0 ? a->a : portPm64BadgeCount(); /* (0: Rowf's, every badge of his) */
        if (strcmp(a->key, "pm64_bosses") == 0) {
            /* the ten bosses' bits only: a record from before 0.5.0 may hold bit 10, the Koopa Bros.' rematch (a
               refight of it set it), which counted towards "all ten" */
            return bitCount((unsigned)portAchievementCounter(a->key) & ((1u << PM64_BOSS_RUSH_FIGHTS) - 1u));
        }
        return bitCount((unsigned)portAchievementCounter(a->key));
    case K_BIT:
        *goal = 1;
        return (portAchievementCounter(a->key) >> a->b) & 1;
    case K_STAT: {
        const PortStats* s = portStatsFile();
        long long v = s != NULL && a->b >= 0 && a->b < PORT_STAT_COUNT ? s->v[a->b] : 0;
        *goal = a->a;
        return v > 0x7FFFFFFF ? 0x7FFFFFFF : (int)v;
    }
    case K_PARTY:
        *goal = a->a;
        return partyGamesWon();
    case K_PIPES:
        *goal = 5;
        return (swGet(GSWF_TIK16_PIPE_MRI_OPEN) ? 1 : 0) + (swGet(GSWF_TIK16_PIPE_NOK_OPEN) ? 1 : 0) +
               (swGet(GSWF_TIK17_PIPE_PIK_OPEN) ? 1 : 0) + (swGet(GSWF_TIK17_PIPE_MUJ_OPEN) ? 1 : 0) +
               (swGet(GSWF_TIK08_TWILIGHT_PIPE_REACHED) ? 1 : 0);
    case K_EMAILS:
        *goal = a->a;
        for (i = 0; i < 4; i++) {
            n += bitCount(pouch->emailReadFlags[i]);
        }
        return n;
    case K_STAR_POWER:
        *goal = 8;
        return pouch->maximumSP / 100;
    case K_FULL_BAG:
        *goal = a->a;
        for (i = 0; i < MAX_HELD_ITEMS; i++) {
            n += pouch->heldItems[i] != 0 ? 1 : 0;
        }
        return n;
    case K_WORN:
        *goal = a->a;
        return countWorn();
    case K_SHOP_PRIZE:
        *goal = 1;
        return pouch->shopPointRewardFlags != 0;
    case K_BROKE:
        *goal = 1;
        return swStoryPeek() >= STORY_STG2_00_BOGGLY_WOODS && pouch->coins == 0;
    case K_PM64_DISHES:
        total = portPm64DishCount();
        for (i = 0; i < total; i++) {
            n += portPm64FoodCooked(PORT_PM64_FOOD_MIN + i) ? 1 : 0;
        }
        break;
    case K_PM64_FOES:
        total = portPm64EnemyCount();
        for (i = 0; i < total; i++) {
            n += portPm64EnemySeen(i) ? 1 : 0;
        }
        break;
    }
    *goal = a->a != 0 && a->a < total ? a->a : total;
    return n;
}

static int pm64Shown(void) {
    return portPm64Linked() != PM64_NONE;
}

/* --- what the game does, watched (each poll; the battle's end and a Special move from the battle) --- */

/* the chapters' ends (the next one's start), the prologue's not counted */
static const int kChapterEnds[] = {
    STORY_STG2_00_BOGGLY_WOODS, STORY_STG3_00_WELCOME_TO_GLITZVILLE, STORY_STG4_00_WELCOME_TO_STAGE_4,
    STORY_STG5_00_LOGBOOK,      STORY_STG6_00_START,                 STORY_STG7_00_START,
    STORY_STG8_00_START,        STORY_STG8_END_AFTER_THE_BATTLE,
};
static int sPrevStory = -1;
static unsigned sTeamBonuses;
static int sBossHere; /* a chapter boss in this battle (seen while it was up) */

/* the chapter bosses (rules_pc.c's bounties) */
static int chapterBoss(int kind) {
    switch (kind) {
        case UNIT_BLOOPER:
        case UNIT_GOLD_FUZZY:
        case UNIT_HOOKTAIL:
        case UNIT_MAGNUS:
        case UNIT_BELDAM_CHAPTER2:
        case UNIT_MACHO_GRUBBA:
        case UNIT_RAWK_HAWK:
        case UNIT_DOOPLISS_CHAPTER4_FIGHT2:
        case UNIT_CORTEZ:
        case UNIT_LORD_CRUMP_CHAPTER5:
        case UNIT_SMORG:
        case UNIT_MAGNUS2_0:
        case UNIT_GLOOMTAIL:
        case UNIT_BELDAM_CHAPTER8:
        case UNIT_DOOPLISS_CHAPTER8:
        case UNIT_BOWSER_CHAPTER8:
        case UNIT_GRODUS:
        case UNIT_SHADOW_QUEEN_DEMON:
        case UNIT_BONETAIL:
            return 1;
    }
    return 0;
}

static void bossLook(BattleWork* wp) {
    int i;
    for (i = 0; wp != NULL && i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = wp->units[i];
        if (u != NULL && u->alliance == 1 && chapterBoss(u->currentKind)) {
            sBossHere = 1;
        }
    }
}

void portAchievementsBattleOver(BattleWork* wp, int result) {
    const BattleActRecord* r;
    const BattleUnit* m;
    if (wp == NULL || !counting() || lentPouch()) {
        sBossHere = 0; /* (a lent loadout's battle: its level 30 and 800 SP aren't the file's) */
        return;
    }
    r = &wp->actRecord;
    m = BattleGetMarioPtr(wp);
    bossLook(wp);
    /* Mario's first strike (the field's hit into the battle: 1-6 his jumps and hammers) */
    if (wp->fbatInfo != NULL && wp->fbatInfo->partyId >= 1 && wp->fbatInfo->partyId <= 6) {
        portAchievementCounterAdd("first_strikes", 1);
    }
    if (result == 1 && m != NULL) {
        if (sBossHere && r->marioDamageTaken == 0) {
            portAchievementCounterMax("boss_flawless", 1);
        }
        if (m->currentHp == 1) {
            portAchievementCounterMax("win_1hp", 1);
        }
        if (m->statusFlags & (STATUS_FLAG_DANGER | STATUS_FLAG_PERIL)) {
            portAchievementCounterMax("win_danger", 1);
        }
        if (r->turnCount <= 1) {
            portAchievementCounterMax("win_one_turn", 1);
        }
        if (r->marioHammerMoves > 0 && r->marioJumpMoves == 0 && r->marioSpecialAttacks + r->marioSpecialSupports == 0 &&
            r->marioAttackItems + r->marioSupportItems == 0) {
            portAchievementCounterMax("hammer_only", 1);
        }
    }
    sBossHere = 0;
}

void portAchievementsSpecialUsed(const void* weapon) {
    int i;
    if (lentPouch()) {
        return; /* (a loadout lends every Star Power: "Star Studded" isn't the file's) */
    }
    for (i = 0; i < 8; i++) {
        if (weapon != NULL && weapon == (const void*)superActionTable[i]) {
            portAchievementCounterBit("specials", i);
        }
    }
}

/* each poll, a file being counted: what the game state says now */
static void watch(const PouchData* pouch) {
    const int story = swStoryPeek();
    int i;
    /* in a battle: a chapter boss among the enemies (it may be gone by the end) */
    if (seqGetSeq() == SEQ_BATTLE) {
        bossLook(_battleWorkPointer);
    }
    /* co-op: four players; a lent partner come; team bonuses; an online guest; four games linked */
    if (portCoopPlayers() >= 4 || (portStateSyncCoopOn() && portStateSyncPlayersNow() >= 4)) {
        portAchievementCounterMax("coop_four", 1);
    }
    if (portStateSyncLinked() && portStateSyncPlayersNow() >= 4) {
        portAchievementCounterMax("online_four", 1);
    }
    if (portStateSyncLinked() && portStateSyncMe() >= 2) {
        portAchievementCounterMax("online_guest", 1);
    }
    for (i = 1; i <= 3; i++) {
        const int c = portCoopCharacterOf(i);
        if (c >= 1 && c <= 7 && !partyChkJoin((PartyId)c)) {
            portAchievementCounterMax("lent_partner", 1);
        }
    }
    if (portTeamBonusShown() > sTeamBonuses) {
        portAchievementCounterAdd("team_bonus", (int)(portTeamBonusShown() - sTeamBonuses));
    }
    sTeamBonuses = portTeamBonusShown();
    /* the badges worn; Shine Sprites found early */
    wornNow(pouch);
    if (story < STORY_STG4_00_WELCOME_TO_STAGE_4) {
        int total;
        portAchievementCounterMax("shines_early", countNamed("SHINE_SPRITE", 0, 0x7FFFFFFF, &total));
    }
    /* Paper Mario 64: the partner out, Rowf's badges owned */
    if (pm64Shown()) {
        const int out = portPm64PartnerOut();
        if (out >= PM64_PARTNER_GOOMBARIO && out <= PM64_PARTNER_BOW) {
            portAchievementCounterBit("pm64_party", out);
        }
        for (i = 0; i < MAX_BADGES; i++) {
            const int k = pouch->badges[i] - PORT_PM64_BADGE_MIN;
            if (k >= 0 && k < portPm64BadgeCount()) {
                portAchievementCounterBit("pm64_rowf", k);
            }
        }
    }
}

/* each frame: a chapter over, and how it was played (as the modes' watch: the story's rise seen in the field or a
 * battle, the story set by a load not counted) */
static void watchChapter(void) {
    const int story = swStoryPeek();
    int i;
    if (sPrevStory >= 0 && story > sPrevStory && seqGetSeq() == SEQ_GAME) {
        for (i = 0; i < (int)(sizeof(kChapterEnds) / sizeof(kChapterEnds[0])); i++) {
            if (sPrevStory < kChapterEnds[i] && story >= kChapterEnds[i]) {
                const int as = portPlayAs();
                if (portCoopPlayers() >= 2 || portStateSyncCoopOn()) {
                    portAchievementCounterMax("coop_chapter", 1);
                }
                if (portBrothersOn()) {
                    portAchievementCounterMax("brothers_chapter", 1);
                }
                if (as == PORT_PLAY_AS_LUIGI || as == PORT_PLAY_AS_PEACH || as == PORT_PLAY_AS_BOWSER) {
                    portAchievementCounterBit("playas", as - 1); /* (bits 0-2: Luigi, Peach, Bowser) */
                }
                if (as == PORT_PLAY_AS_LUIGI) {
                    portAchievementCounterAdd("luigi_chapters", 1);
                }
                if (portFunOn(PORT_FUN_COOL)) {
                    portAchievementCounterMax("cool_chapter", 1);
                }
                if (i == 0 && portAchievementCounter("quick_start") > 0) {
                    portAchievementCounterMax("quick_start_ch1", 1);
                }
                if (portMasterQuestActive() != NULL) {
                    portAchievementCounterMax("mq_chapter", 1);
                    if (i == 7) {
                        portAchievementCounterMax("mq_done", 1);
                    }
                }
            }
        }
    }
    sPrevStory = story;
}

/* each frame: Paper Mario 64's partners' field moves (short, so not left to the poll) */
static void watchFrame(void) {
    const MarioWork* mario;
    const PartyEntry* party;
    int out;
    if (seqGetSeq() != SEQ_GAME || !pm64Shown() || (out = portPm64PartnerOut()) <= 0) {
        return;
    }
    mario = marioGetPtr();
    party = mario != NULL && mario->currPartySlotId[0] >= 0 ? partyGetPtr(mario->currPartySlotId[0]) : NULL;
    if ((party != NULL && party->runMode == 3) /* (its field move: partyDoWork) */ ||
        (out == PM64_PARTNER_SUSHIE && portPm64Swimming()) || (out == PM64_PARTNER_BOW && portPm64BowVeil() > 0)) {
        portAchievementCounterBit("pm64_field", out);
    }
}

/* --- the record -------------------------------------------------------------- */

static void save(void) {
    FILE* f;
    int i;
    if (sPath[0] == '\0') {
        return;
    }
    portPracticeGameDir("Achievements");
    if (sGuest) {
        portPracticeGameDir("Achievements/coop");
    }
    f = portFileWriteOpen(sPath, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "name=%s\n", sName);
    for (i = 0; i < COUNT; i++) {
        if (sEarned[i][0] != '\0') {
            fprintf(f, "%s %s\n", kList[i].id, sEarned[i]);
        }
    }
    for (i = 0; i < sCounterCount; i++) {
        fprintf(f, "c %s %d\n", sCounters[i].key, sCounters[i].value);
    }
    portFileWriteClose(f, sPath);
}

/* a co-op guest (no save file of its own): its record is its copy of the host's adventure's,
 * <user>/Achievements/coop/<game id>/<host>_<room>.txt (coop_save_pc.c's name for the copy) */

static int sAdopted; /* the record was a guest's, kept as this file ("name=*"): it takes the file's name */

static void loadRecord(void);

static void guestRecord(void) {
    char key[96], path[1024];
    const int guest = portStateSyncCoopGuest();
    if (!guest) {
        if (sGuest) { /* the session over: nothing kept until a file is loaded */
            save();
            sGuest = 0;
            sPath[0] = '\0';
        }
        return;
    }
    portCoopCopyKey(portStateSyncPlayerName(1), portStateSyncCode(), key, sizeof(key));
    snprintf(path, sizeof(path), "%s/Achievements/coop/%s/%s.txt", portPracticeUserDir(), PORT_GAME_ID, key);
    if (sGuest && strcmp(path, sPath) == 0) {
        return;
    }
    if (sPath[0] != '\0') {
        save(); /* (the file played before joining: its record as it stood) */
    }
    sGuest = 1;
    snprintf(sPath, sizeof(sPath), "%s", path);
    snprintf(sName, sizeof(sName), "%s", key);
    snprintf(sFile, sizeof(sFile), portTr("Co-op (%.40s)"), portStateSyncPlayerName(1));
    loadRecord();
    printf("[achievement] a co-op guest: kept as %s\n", key);
    fflush(stdout);
}

/* the guest's copy kept in file slot (coop_copy_pc.c, the launcher): its record goes with it, to be that file's */
void portAchievementsCoopCopyKept(int slot) {
    char key[96], from[1024], to[1024], line[128];
    FILE *in, *out;
    if (slot < 0 || slot > 3 || !portCoopCopyPendingKey(key, sizeof(key))) {
        return;
    }
    snprintf(from, sizeof(from), "%s/Achievements/coop/%s/%s.txt", portPracticeUserDir(), PORT_GAME_ID, key);
    snprintf(to, sizeof(to), "%s/Achievements/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID, slot + 1);
    if (sGuest && strcmp(sPath, from) == 0) {
        save();
    }
    portPracticeGameDir("Achievements");
    if ((out = portFileWriteOpen(to, "w")) == NULL) {
        return;
    }
    fprintf(out, "name=*\n"); /* (the file's player name, taken when it's loaded: the save's own encoding) */
    if ((in = portFopen(from, "r")) != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            if (strncmp(line, "name=", 5) != 0 && strncmp(line, "c coop_copy ", 12) != 0) {
                fputs(line, out);
            }
        }
        fclose(in);
    }
    fprintf(out, "c coop_copy 1\n"); /* (Souvenir) */
    if (!portFileWriteClose(out, to)) {
        printf("[achievement] the co-op copy's record couldn't be kept for file %d\n", slot + 1);
        fflush(stdout);
        return; /* (the guest's record stays where it was) */
    }
    portRemove(from); /* (only once the file's is in place) */
    printf("[achievement] the co-op copy's record kept for file %d\n", slot + 1);
    fflush(stdout);
}

void portAchievementsFileLoaded(void) {
    sPrevStory = -1;
    sBossHere = 0;
    sTeamBonuses = portTeamBonusShown();
    sGuest = 0;
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        sPath[0] = '\0';
        return;
    }
    snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
    snprintf(sPath, sizeof(sPath), "%s/Achievements/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
             (int)gp->lastSlot + 1);
    snprintf(sFile, sizeof(sFile), portTr("File %d (%s)"), (int)gp->lastSlot + 1, sName);
    loadRecord();
}

/* sPath's record, for sName */
static void loadRecord(void) {
    char line[128];
    FILE* f;
    int i, sameName = 0;
    memset(sEarned, 0, sizeof(sEarned));
    sCounterCount = 0;
    sCountersChanged = 0;
    sAdopted = 0;
    f = portFopen(sPath, "r");
    if (f != NULL) {
        int named = 0, cut = 0;
        while (fgets(line, sizeof(line), f) != NULL) {
            char id[64], date[16];
            const size_t len = strlen(line);
            if (len > 0 && line[len - 1] != '\n' && feof(f)) {
                cut = 1; /* (every line is written with its end: one without is a file cut short, not read) */
                break;
            }
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "name=", 5) == 0) {
                named = 1;
                sameName = strcmp(line + 5, sName) == 0 || strcmp(line + 5, "*") == 0;
                sAdopted |= strcmp(line + 5, "*") == 0;
                continue;
            }
            if (!sameName) {
                continue; /* another game begun in the file: its record starts again */
            }
            if (line[0] == 'c' && line[1] == ' ') {
                char key[24];
                int value;
                int* v;
                if (sscanf(line + 2, "%23s %d", key, &value) == 2 && (v = counterSlot(key, 1)) != NULL) {
                    *v = value;
                }
                continue;
            }
            if (sscanf(line, "%63s %15s", id, date) != 2) {
                continue;
            }
            for (i = 0; i < COUNT; i++) {
                if (strcmp(kList[i].id, id) == 0) {
                    snprintf(sEarned[i], sizeof(sEarned[i]), "%s", date);
                }
            }
        }
        fclose(f);
        if (!named) {
            /* no name line: a record cut short (a crash mid-write before 0.5.0's whole writes) or emptied. Not
               taken for a new game's and written over: kept aside as it is */
            char aside[1040];
            snprintf(aside, sizeof(aside), "%s.damaged", sPath);
            portRename(sPath, aside);
            printf("[achievement] %s had no name line%s: kept as %s\n", sPath, cut ? " (cut short)" : "", aside);
            fflush(stdout);
        }
    }
    sCountersChanged = sAdopted; /* (a kept co-op copy's: written again under the file's name) */
    sFirstCheck = 1;
    {
        /* $TTYD_ACHIEVEMENT_TOAST=<n> (tests): achievement n's toast shown as the file loads */
        const char* test = getenv("TTYD_ACHIEVEMENT_TOAST");
        if (test != NULL && atoi(test) >= 0 && atoi(test) < COUNT && sToastCount < TOASTS) {
            sToasts[sToastCount] = atoi(test);
            sToastTimes[sToastCount++] = 0.0;
        }
    }
}

void portAchievementsFrame(void) {
    static unsigned int sTick;
    static int sLentSaid;
    int i, changed = 0, lend;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("achievements", 1, 0, 1);
    }
    if (*sOn && gp != NULL) {
        guestRecord(); /* (a co-op guest's record: its copy of the host's adventure) */
        if (sPath[0] == '\0' && !sGuest && seqGetSeq() == SEQ_GAME) {
            /* $TTYD_ACHIEVEMENT_FILE=<1-4> (tests): a game booted into a map counted as that file, as if loaded */
            static int sTestDone;
            const char* test = sTestDone ? NULL : getenv("TTYD_ACHIEVEMENT_FILE");
            sTestDone = 1;
            if (test != NULL && atoi(test) >= 1 && atoi(test) <= 4) {
                snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
                snprintf(sPath, sizeof(sPath), "%s/Achievements/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
                         atoi(test));
                if (sName[0] != '\0') {
                    snprintf(sFile, sizeof(sFile), portTr("File %d (%s)"), atoi(test), sName);
                } else {
                    snprintf(sFile, sizeof(sFile), portTr("File %d"), atoi(test)); /* (a booted map's: no name) */
                }
                loadRecord();
            }
        }
    }
    if (!*sOn || sPath[0] == '\0' || gp == NULL || portPracticeTainted()) {
        return;
    }
    if ((seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE) || pouchGetPtr() == NULL) {
        sPrevStory = -1;
        return;
    }
    lend = lending();
    if (lend != sLentSaid) {
        printf("[achievement] %s\n", lend ? "a mode lends: only the modes' own count meanwhile"
                                          : "the game is the file's own again: everything counts");
        fflush(stdout);
        sLentSaid = lend;
    }
    if (lend) {
        sPrevStory = -1; /* (versus's words put back aren't a story step; the first look after starts afresh) */
    } else {
        watchChapter();
        watchFrame();
    }
    if (++sTick % 30 != 0) {
        return;
    }
    if (!lend) {
        watch(pouchGetPtr());
    }
    for (i = 0; i < COUNT; i++) {
        int goal = 1;
        int done;
        if (sEarned[i][0] != '\0' && !sFirstCheck) {
            continue; /* (earned for good: its progress isn't counted again) */
        }
        if (lend && !countersOnly(kList[i].kind)) {
            continue; /* (a mode lends: see lending) */
        }
        if ((kList[i].flags & F_PM64) && !pm64Shown()) {
            continue; /* (none to earn without the ROM) */
        }
        done = progress(&kList[i], &goal) >= goal && goal > 0;
        if (done && sEarned[i][0] == '\0') {
            /* (the first check's quiet "already done" is for what the loaded file had reached before; a counter
               raised since the load was earned now: its toast and its plain line) */
            const int before = sFirstCheck && !((kList[i].kind == K_COUNTER || kList[i].kind == K_BITS ||
                                                 kList[i].kind == K_BIT) &&
                                                counterRaisedSinceLoad(kList[i].key));
            const time_t t = time(NULL);
            struct tm tm;
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            strftime(sEarned[i], sizeof(sEarned[i]), "%Y-%m-%d", &tm);
            changed = 1;
            if (!before && sToastCount < TOASTS) {
                sToasts[sToastCount] = i;
                sToastTimes[sToastCount++] = 0.0; /* timed from when it shows */
            }
            printf("[achievement] %s%s\n", kList[i].name, before ? " (already done)" : "");
            fflush(stdout);
        } else if (sFirstCheck && !done) {
            printf("[achievement] %s: %d of %d\n", kList[i].name, progress(&kList[i], &goal), goal);
            fflush(stdout);
        }
    }
    if (!lend) {
        sFirstCheck = 0; /* (the first full look, with every kind) */
    }
    if (changed || sCountersChanged) {
        sCountersChanged = 0;
        save();
    }
}

/* --- for the page ------------------------------------------------------------ */

int portAchievementCount(void) {
    return COUNT;
}

const char* portAchievementName(int i) {
    return i >= 0 && i < COUNT ? kList[i].name : "";
}

const char* portAchievementNote(int i) {
    return i >= 0 && i < COUNT ? kList[i].note : "";
}

int portAchievementCategory(int i) {
    return i >= 0 && i < COUNT ? kList[i].cat : 0;
}

int portAchievementCategoryCount(void) {
    return C_COUNT;
}

const char* portAchievementCategoryName(int category) {
    return category >= 0 && category < C_COUNT ? kCategories[category] : "";
}

int portAchievementHidden(int i) {
    return i < 0 || i >= COUNT || ((kList[i].flags & F_PM64) && !pm64Shown() && portAchievementEarned(i) == NULL);
}

int portAchievementSecret(int i) {
    return i >= 0 && i < COUNT && (kList[i].flags & F_SECRET) != 0;
}

int portAchievementIcon(int i) {
    const int item = i >= 0 && i < COUNT ? kList[i].item : 0;
    return item > 0 && item < ITEM_MAX ? (int)itemDataTable[item].icon_id : -1;
}

int portAchievementProgress(int i, int* goal) {
    *goal = 0;
    if (i < 0 || i >= COUNT || !portPracticeInGame() || pouchGetPtr() == NULL) {
        return 0;
    }
    return progress(&kList[i], goal);
}

const char* portAchievementEarned(int i) {
    return i >= 0 && i < COUNT && sPath[0] != '\0' && sEarned[i][0] != '\0' ? sEarned[i] : NULL;
}

const char* portAchievementFile(void) {
    return sPath[0] != '\0' ? sFile : "";
}

int portAchievementToast(double* age) {
    const double now = portHostSeconds();
    if (portCleanFrame() && getenv("TTYD_ACHIEVEMENT_TOAST") == NULL) {
        /* $TTYD_CLEAN_FRAME (media captures): earned and listed, no toast over the picture (settings_extra.c);
           a toast asked for by a test still shows */
        sToastCount = 0;
        return -1;
    }
    while (sToastCount > 0) {
        if (sToastTimes[0] == 0.0) {
            if (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE) {
                return -1; /* shown once the room or battle is up, not over a load */
            }
            sToastTimes[0] = now;
            printf("[achievement] toast shown: %s\n", sToasts[0] >= 0 && sToasts[0] < COUNT ? kList[sToasts[0]].name : "?");
            fflush(stdout);
        }
        *age = now - sToastTimes[0];
        if (*age < 5.0) {
            return sToasts[0];
        }
        memmove(sToasts, sToasts + 1, sizeof(int) * (TOASTS - 1));
        memmove(sToastTimes, sToastTimes + 1, sizeof(double) * (TOASTS - 1));
        sToastCount--;
    }
    return -1;
}
