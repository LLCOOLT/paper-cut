#pragma once
/* Paper Cut's own words in the save: every GSW (byte), GSWF (flag), LSW or LSWF a port feature keeps in
 * GlobalWork past what the game uses, in one table, so two features never share one.
 *
 * The game names GSWs up to 1474 and GSWFs up to 5666 (include/evt/evt_story.h); the words below are past
 * those, or (Archipelago) where the TTYD AP mod has always kept them. A feature takes its words from here
 * (PORT_<KIND>_<NAME>_FIRST/_LAST), and asserts its own layout fits (_Static_assert / static_assert).
 * tools/port/save_words.py --check (local CI's audits) fails when two ranges overlap, a range leaves its kind's
 * bounds or the game's, or a word's number turns up in port code outside this table.
 *
 * X(kind, name, first, last, what): inclusive; kind GSW, GSWF, LSW or LSWF. Keep it sorted by kind and first. */
#define PORT_SAVE_WORDS(X)                                                                                   \
    X(GSW, AP_DIRTY, 1696, 1696, "Archipelago: the TTYD AP mod's dirty bits (0x10: replay received items)")   \
    X(GSW, AP_LOCATIONS, 1700, 1789, "Archipelago: the AP mod's location checks (troubles, presents)")        \
    X(GSW, AP_RECEIVED, 1792, 1795, "Archipelago: the received items' index (u32)")                           \
    X(GSW, AP_COUNTERS, 1801, 1805, "Archipelago: items ever received, counted (5 kinds)")                    \
    X(GSW, OPEN, 1920, 2010, "open world (randomizer): magic, stage, pools, goal, seed, tracks, order, rules") \
    X(GSW, PM64, 2011, 2019, "PM64 link: met (2011, Bow 2019 b0), worn per host (2012..2018), Watt alone (2019 b7)") \
    X(GSW, PM64_BOSSES, 2024, 2025, "PM64 bosses: each fight beaten on this file (11: 2024 b0..7, 2025 b0..2)") \
    X(GSW, KEELHAUL, 2030, 2030, "remake maps: Keelhaul Key's jump stands switch")                            \
    X(GSW, HARDCORE, 2040, 2042, "remix modes: the hardcore mark (0xC5) and its id (since 0.4.0)")            \
    X(GSWF, AP_LOCATIONS, 6002, 6456, "Archipelago: the AP mod's location checks (recipes 6400..6456)")      \
    X(GSWF, AP_EVER, 6600, 6855, "Archipelago: item ever received (6600 + its id 1..255)")                    \
    X(GSWF, POSTGAME, 8100, 8131, "postgame extras: Whacka, newsletter, quiz, crane, champion, Nighttail")

/* The game's own last words, and each kind's size */
#define PORT_GAME_GSW_LAST 1474
#define PORT_GAME_GSWF_LAST 5666
#define PORT_GSW_COUNT 2048
#define PORT_GSWF_COUNT 8192

#define PORT_SAVE_WORD_ENUM_(kind, name, first, last, what) \
    PORT_##kind##_##name##_FIRST = (first), PORT_##kind##_##name##_LAST = (last),
enum { PORT_SAVE_WORDS(PORT_SAVE_WORD_ENUM_) PORT_SAVE_WORDS_END_ };
#undef PORT_SAVE_WORD_ENUM_
