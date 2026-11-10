// Gameplay presets (Gameplay > Presets; the Start menu's Presets row, which opens a page of them,
// port/src/aurora/pause_presets_aurora.c; the launcher's first-run picker, port/settings/
// enhancement_presets.h). Each is shown as a tile: its item's icon, its name and a tagline of a few
// words; one sentence says what it is, and the page lists what it changes in full. A preset is made of parts (lists of
// key = value) over one set: every setting that changes play (kGameplay below, and every key a part
// names). Applying one puts each of those at the preset's value, or its default where the preset
// names none, so any preset can follow any other, both ways, and Vanilla is the way back from all.
// Controls, looks, sound, video and accessibility are outside that set: no preset touches them,
// except a preset's extras (Accessible's accessibility settings, Paper Mario 64's sounds), which only
// it sets and the others leave as they are. The list marks the preset the settings match now, or
// Custom with how many settings differ from the nearest one.
//
// Remake rules is remake_rules.inc: one REMAKE_RULE("key", value) line per remake change, appended
// to by each feature that adds one. The Remake page opens with the same list as one switch: Play it
// like the Switch remake. The Paper Mario 64 page has one too: Everything Paper Mario 64.
//
// Tests: $TTYD_PRESETS_TEST=1 applies each preset and then Vanilla once the game draws, logs what
// differs from the defaults after, the gameplay settings no preset resets, and the Paper Mario 64
// switch on and off ("[presets] ..."), then puts the settings back as they were.
#include "port/cosmetics.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../enhancement_presets.h"
#include "../launcher_look.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "port/gameplus.h"
#include "port/pm64.h"
#include "port/qol.h"
#include "port/toggles.h"
}

bool portCosmeticsDecode(const PortTexInfo& t, std::vector<uint8_t>& out); // cosmetics.cpp

namespace {

constexpr const char* kTab = "Enhancements";

struct Value {
  const char* key;
  int value;
};

#define REMAKE_RULE(key, value) {key, value},
const Value kRemake[] = {
#include "remake_rules.inc"
};
#undef REMAKE_RULE

// Every setting that changes play, by where it is shown: each preset puts all of these at its value or
// the default. (Also every key a preset's parts name.) Left out on purpose, as the player's own (kOwn
// below): who plays (Play as, co-op, Brothers mode), a setting's fine-tuning (custom difficulty, boost
// speed, seeds), the accessibility settings and the tools (rewind, fast-forward, save states, camera).
const char* const kGameplay[] = {
    // Gameplay
    "difficulty", "level_sync", "skip_any_scene", "text_hold_skip", "fast_field_text", "battle_speed",
    "fast_battle_menus", "run_speed",
    // Remake (remake_rules.inc's keys come with kRemake) and its Start in Rogueport
    "rogueport_start", "start_in_rogueport",
    // Enhancements: Field
    "fast_travel", "walk_boost", "save_no_prompt", "storage_anywhere", "collectible_tracker",
    // Battle
    "battle_popups", "battle_grades", "free_tattle", "levelup_dpad", "gameover_retry", "balanced_badges",
    "no_stage_hazards", "no_audience_throws", "auto_win_weak", "glitz_pit_kind", "coin_multiplier",
    "exp_multiplier", "pit_plus",
    // Characters
    "quick_swap", "partner_revive", "heal_all_partners",
    // Items and badges
    "sell_helper", "pc_recipes", "badge_loadouts", "pc_badges",
    // Story
    "skip_interludes", "pace_trims", "new_game_plus",
    // Restorations: the one that changes play (the others are looks and text)
    "pg_jp_recipes",
    // Paper Mario 64 (with a ROM linked)
    "pm64_partners", "pm64_met_all", "pm64_join", "pm64_field", "pm64_enemies", "pm64_story_enemies",
    "pm64_badges", "pm64_recipes", "pm64_bosses", "pm64_boss_scale",
    // Audio's Jukebox (a Paper Cut helper)
    "jukebox",
    // Multiplayer's rules (who plays is the players' own)
    "duo_moves", "team_bonus", "audience_players",
    // Modes: the switches that change the story's play
    "mirror_mode", "master_quest", "directors_cut", "remix_enemies", "remix_badge_draft", "chaos_mode",
    "fun_moon_gravity", "daily_specials", "pg_pit_whacka", "pg_second_tattles", "pg_boo_quiz", "pg_newsletter",
    "pg_crane_prizes", "pg_buy_tokens",
    // Challenges
    "challenge_ohko", "challenge_guard", "challenge_commands", "challenge_no_battle_items", "enemy_variants",
    // Practice and streaming
    "speedrun_timer", "timer_load_removal", "timer_autostart", "rng_fixed", "crowd_control",
};

// changes play, but the player's own: never reset by a preset (the test's check names any other)
const char* const kOwn[] = {
    "playable_luigi", "luigi_high_jump", "second_player", "brothers_mode", "dungeon_master",
    "difficulty_enemy_hp", "difficulty_enemy_attack", "walk_boost_speed", "new_game_plus_from", "remix_seed",
    "paper_tower_seed", "ac_attack_assist", "ac_guard_assist", "slow_speed", "slow_speed_battle", "hold_to_mash",
    "crowd_harmful", "page_turn", "settings_per_file", "console_accurate",
};

// comforts only: nothing a battle or the story feels
const Value kVanillaPlus[] = {
    {"quick_swap", 1},        {"save_no_prompt", 1},    {"levelup_dpad", 1},      {"skip_scenes", 1},
    {"skip_any_scene", 1},    {"text_hold_skip", 1},    {"rogueport_start", 1},   {"fast_battle_menus", 1},
    {"battle_popups", 1},     {"fast_field_text", 1},   {"partner_hint", 1},      {"talk_prompts", 1},
    {"mail_no_stop", 1},      {"list_lr_scroll", 1},    {"shop_plus", 1},         {"save_file_details", 1},
    {"battle_fp_preview", 1}, {"battle_sp_grey", 1},
};

// over Vanilla+ and the remake's: the port's own helpers
const Value kEnhanced[] = {
    {"bigger_bag", 1},  {"heal_all_partners", 1}, {"partner_revive", 1}, {"fast_travel", 1},
    {"storage_anywhere", 1}, {"walk_boost", 1},   {"free_tattle", 1},    {"gameover_retry", 1},
    {"sell_helper", 1}, {"jukebox", 1},
};

// over Vanilla+: easier battles, generous helpers
const Value kStory[] = {
    {"difficulty", 1},        {"partner_revive", 1},   {"heal_all_partners", 1}, {"levelup_heal_party", 1},
    {"gameover_retry", 1},    {"free_tattle", 1},      {"full_bag_use", 1},      {"bigger_bag", 1},
    {"battle_speed", 1},      {"auto_win_weak", 1},    {"rules_flee_coins", 1},  {"coin_hints", 1},
    {"skip_interludes", 1},
};

// over Vanilla+: harder rules, no helpers
const Value kChallenge[] = {
    {"difficulty", 2},          {"level_sync", 2},          {"challenge_commands", 1}, {"balanced_badges", 1},
    {"rules_hazard_pierce", 1}, {"rules_buzzy_blast", 1},   {"rules_bomb_no_sp", 1},   {"rules_retattle", 1},
};

// over Vanilla+: getting around
const Value kExplorer[] = {
    {"fast_travel", 1},          {"walk_boost", 1},            {"storage_anywhere", 1},
    {"collectible_tracker", 2},  {"remake_warp_pipes", 1},     {"remake_shortcut_pipes", 1},
    {"remake_chapter_shortcuts", 1}, {"remake_steeple_stairs", 1}, {"remake_pipe_a", 1},
};

// over Vanilla+: finding everything
const Value kCompletionist[] = {
    {"collectible_tracker", 2}, {"coin_hints", 1},  {"free_tattle", 1},   {"pg_second_tattles", 1},
    {"fast_travel", 1},         {"coin_cap", 1},    {"sell_helper", 1},   {"storage_anywhere", 1},
    {"bigger_bag", 1},          {"badge_loadouts", 1},
};

// over Vanilla+: playing together on one screen (the co-op rules at their own on)
const Value kCouch[] = {
    {"partner_revive", 1}, {"gameover_retry", 1}, {"battle_speed", 1},
    {"duo_moves", 1},      {"team_bonus", 1},     {"audience_players", 1},
};
const Value kCouchExtras[] = {{"second_player", 1}};

// over Vanilla+: fewer ways to fail
const Value kAccessible[] = {{"gameover_retry", 1}, {"partner_revive", 1}, {"free_tattle", 1}};
// the accessibility settings (outside the gameplay set: only this preset sets them, the others leave them)
const Value kAccessibleExtras[] = {
    {"less_flashing", 1},    {"less_shake", 1},      {"text_scale", 125}, {"captions", 1},
    {"ac_attack_assist", 1}, {"ac_guard_assist", 1}, {"hold_to_mash", 1}, {"slow_speed", 2},
    {"slow_speed_battle", 1},
};

// over Vanilla+: everything from the linked Paper Mario 64 (how its partners join, the boss list: their own
// pages; pm64_met_all and pm64_field at their defaults)
const Value kPm64[] = {
    {"pm64_partners", 1}, {"pm64_story_enemies", 1}, {"pm64_bosses", 1}, {"pm64_badges", 1}, {"pm64_recipes", 1},
};
// its music, sounds and Star Spirits (outside the gameplay set)
const Value kPm64Extras[] = {
    {"pm64_boss_music", 1}, {"pm64_sfx", 1}, {"pm64_sfx_general", 1}, {"pm64_star_spirits", 1},
};

// over Vanilla+: getting around a shuffled world quickly
const Value kRandomizer[] = {
    {"bigger_bag", 1}, {"fast_travel", 1}, {"storage_anywhere", 1}, {"walk_boost", 1}, {"gameover_retry", 1},
};

// nothing that changes play; the timer on
const Value kSpeedrun[] = {
    {"speedrun_timer", 2}, {"timer_load_removal", 1}, {"timer_autostart", 1},
};

struct Part {
  const Value* values;
  int count;
};
#define PART(a) Part{a, int(sizeof(a) / sizeof(a[0]))}

enum : unsigned {
  kPicker = 1, // in the first-run picker
  kNeedsRom = 2, // Paper Mario 64: only with a ROM linked
};

enum Group { kClassic, kHelpful, kHarder, kSpecial, kGroups };
const char* const kGroupNames[kGroups] = {"Classic", "Helpful", "Challenge", "Special"};

struct Preset {
  const char* name;
  const char* tagline; // a few words: its tile
  const char* about;   // one sentence: the detail line (what it changes in full: the page's expander)
  Part parts[3];       // in order: a later part's value wins
  Part extras;         // outside the gameplay set: set by this preset only
  unsigned flags;
  Group group;
  int item;         // its picture: this item's icon (the game's own; mario_pouch.h's ITEM_ numbers)
  const char* icon; // the same icon made for the launcher: art/launcher/presets/<icon>.png
};

// (the items by mario_pouch.h's numbers: the settings files are built without the game's headers)
const Preset kPresets[] = {
    // (no parts: every setting that changes play back to its default, the way back from the others)
    {"Vanilla", "The original game", "Every setting that changes play at its default, as on the console.",
     {}, {}, kPicker, kClassic, 0x94 /* Mushroom */, "vanilla"},
    {"Vanilla+", "Original, with comforts",
     "The original rules, with quick-swap, faster text and menus, and skippable scenes.",
     {PART(kVanillaPlus)}, {}, kPicker, kClassic, 0x95 /* Super Shroom */, "vanilla_plus"},
    {"Remake rules", "Play it like the Switch remake",
     "Every change the Switch remake made: bag sizes, coins, balance, hints and more.",
     {PART(kRemake)}, {}, kPicker, kClassic, 0x57 /* Shine Sprite */, "remake"},
    {"Enhanced", "Every helper on",
     "Vanilla+ and the remake's rules, plus fast travel, a bigger bag and retries.",
     {PART(kVanillaPlus), PART(kRemake), PART(kEnhanced)}, {}, kPicker, kHelpful, 0x96 /* Ultra Shroom */,
     "enhanced"},
    {"Story", "Relaxed, for the story",
     "Easy battles, partners never knocked out, healing for all, retry on Game Over.",
     {PART(kVanillaPlus), PART(kStory)}, {}, kPicker, kHelpful, 0x8A /* Sleepy Sheep */, "story"},
    {"Challenge", "Tougher fights",
     "Hard difficulty, enemies at Mario's level, narrower attack commands, no helpers.",
     {PART(kVanillaPlus), PART(kChallenge)}, {}, kPicker, kHarder, 0x8B /* POW Block */, "challenge"},
    {"Explorer", "Getting around faster",
     "Fast travel, a walking boost, the remake's pipes and storage at every Save Block.",
     {PART(kVanillaPlus), PART(kExplorer)}, {}, 0, kHelpful, 0x32 /* Magical Map */, "explorer"},
    {"Completionist", "Find everything",
     "Star Piece and Shine Sprite trackers, coin hints, free Tattles and fast travel.",
     {PART(kVanillaPlus), PART(kCompletionist)}, {}, 0, kHelpful, 0x7D /* Star Piece */, "completionist"},
    {"Couch party", "Together on one screen",
     "Second player on, duo moves, the team bonus and gentler battles.",
     {PART(kVanillaPlus), PART(kCouch)}, PART(kCouchExtras), 0, kSpecial, 0xDB /* Couple's Cake */, "couch"},
    {"Accessible", "Easier to see and play",
     "Retry on Game Over, bigger text, captions, wider timing, less flashing and shake.",
     {PART(kVanillaPlus), PART(kAccessible)}, PART(kAccessibleExtras), 0, kHelpful, 0x97 /* Life Shroom */,
     "accessible"},
    {"Paper Mario 64", "Both adventures in one",
     "Your linked Paper Mario 64's partners, enemies, bosses, badges, recipes, music and sounds.",
     {PART(kVanillaPlus), PART(kPm64)}, PART(kPm64Extras), kPicker | kNeedsRom, kSpecial, 0 /* Mario's head */,
     "pm64"},
    {"Randomizer", "Helpers for a shuffled world",
     "Vanilla+ with fast travel, a bigger bag and storage anywhere; the seed is on Randomizer.",
     {PART(kVanillaPlus), PART(kRandomizer)}, {}, 0, kSpecial, 0xA3 /* Mystery */, "randomizer"},
    {"Speedrun", "The timer, nothing else",
     "Nothing that changes play, and the speedrun timer with load removal.",
     {PART(kSpeedrun)}, {}, 0, kSpecial, 0x8C /* Stopwatch */, "speedrun"},
};
constexpr int kCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

// the presets as shown: by group, each group in the list's order
const std::vector<int>& shownOrder() {
  static std::vector<int> order;
  if (order.empty()) {
    for (int g = 0; g < kGroups; g++) {
      for (int p = 0; p < kCount; p++) {
        if (kPresets[p].group == g) {
          order.push_back(p);
        }
      }
    }
  }
  return order;
}

constexpr const char kNeedRom[] = "Link your Paper Mario ROM first: Enhancements, Paper Mario 64, Link Paper Mario 64.";
constexpr const char kLocked[] = "Console-accurate mode is on: these stay as on the console until it is off.";

// a setting's value by key: a declared one, or the backend's field (Run speed)
int* find(const char* key) {
  if (std::strcmp(key, "run_speed") == 0) {
    return &portSettings()->runSpeed;
  }
  portGameplusDeclare();
  portQolDeclare();
  return portSettingsFind(key);
}

int defaultOf(const char* key) {
  int v = 0;
  portSettingsDefault(key, &v);
  return v;
}

// --- The gameplay set, and each preset's value for each of its keys (made once)

struct Entry {
  const char* key;
  int* at; // nullptr: not declared (yet: looked for again)
  int def;
};

struct Table {
  std::vector<Entry> keys;               // kGameplay, then each part's keys not in it
  std::vector<std::vector<int>> wanted;  // [preset][key]
};

Table& table() {
  static Table t;
  if (t.keys.empty()) {
    std::set<std::string> seen;
    auto add = [&](const char* key) {
      if (seen.insert(key).second) {
        t.keys.push_back({key, nullptr, 0});
      }
    };
    for (const char* key : kGameplay) {
      add(key);
    }
    for (const Preset& p : kPresets) {
      for (const Part& part : p.parts) {
        for (int i = 0; i < part.count; i++) {
          add(part.values[i].key);
        }
      }
    }
    t.wanted.resize(kCount);
  }
  // resolve what isn't yet (a feature declares its keys on first use), with its default and each preset's value
  bool changed = false;
  for (Entry& e : t.keys) {
    if (e.at == nullptr && (e.at = find(e.key)) != nullptr) {
      e.def = defaultOf(e.key);
      changed = true;
    }
  }
  if (changed || t.wanted[0].size() != t.keys.size()) {
    for (int p = 0; p < kCount; p++) {
      std::vector<int>& w = t.wanted[p];
      w.assign(t.keys.size(), 0);
      for (size_t k = 0; k < t.keys.size(); k++) {
        w[k] = t.keys[k].def;
        for (const Part& part : kPresets[p].parts) {
          for (int i = 0; i < part.count; i++) {
            if (std::strcmp(part.values[i].key, t.keys[k].key) == 0) {
              w[k] = part.values[i].value;
            }
          }
        }
      }
    }
  }
  return t;
}

// how many settings differ from preset p (the gameplay set and its extras)
int differences(int p) {
  Table& t = table();
  int n = 0;
  for (size_t k = 0; k < t.keys.size(); k++) {
    if (t.keys[k].at != nullptr && *t.keys[k].at != t.wanted[p][k]) {
      n++;
    }
  }
  const Part& x = kPresets[p].extras;
  for (int i = 0; i < x.count; i++) {
    const int* at = find(x.values[i].key);
    n += at != nullptr && *at != x.values[i].value;
  }
  return n;
}

// the preset the settings match now (-1: Custom), and the nearest one with its count of differences
int current(int* nearest = nullptr, int* differ = nullptr) {
  int best = -1, bestN = 1 << 30;
  for (int p = 0; p < kCount; p++) {
    const int n = differences(p);
    if (n < bestN) {
      best = p;
      bestN = n;
    }
  }
  if (nearest != nullptr) {
    *nearest = best;
  }
  if (differ != nullptr) {
    *differ = bestN;
  }
  return bestN == 0 ? best : -1;
}

bool available(int p) {
  return portConsoleAccurate() == 0 && ((kPresets[p].flags & kNeedsRom) == 0 || portPm64Linked() != PM64_NONE);
}

void apply(int p) {
  Table& t = table();
  for (size_t k = 0; k < t.keys.size(); k++) {
    if (t.keys[k].at != nullptr) {
      *t.keys[k].at = t.wanted[p][k];
    }
  }
  const Part& x = kPresets[p].extras;
  for (int i = 0; i < x.count; i++) {
    if (int* at = find(x.values[i].key)) {
      *at = x.values[i].value;
    }
  }
  std::printf("[presets] %s applied\n", kPresets[p].name);
  std::fflush(stdout);
  portSettingsSave();
}

void muted(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// --- The presets' pictures (enhancement_presets.h's portPresetDrawIcon)

struct Icon {
  ImTextureID tex{};
  float w = 0.f, h = 0.f;
  int from = 0; // 0 none yet, 1 the game's icon.tpl, 2 the launcher's art cache, 3 the linked ROM's
};
Icon sIcons[kCount];
std::string sIconDir;          // the launcher's art cache folder for them, "" for none
std::string sIconTried[kCount]; // the folder each was last looked for in
unsigned sPm64Gen = 0;          // portPm64Generation() + 1 when Paper Mario 64's was made

void setIcon(Icon& ic, int w, int h, const void* rgba, int from) {
  ic.tex = launcherTexture(w, h, rgba); // (kept for the launcher's captures too)
  ic.w = float(w);
  ic.h = float(h);
  ic.from = from;
}

const Icon& iconOf(int p) {
  Icon& ic = sIcons[p];
  // Paper Mario 64's: Goombario, from the linked ROM
  if ((kPresets[p].flags & kNeedsRom) != 0 && portPm64Linked() != PM64_NONE) {
    if (ic.from != 3 || sPm64Gen != portPm64Generation() + 1) {
      static uint8_t rgba[PM64_CAST_FACE_MAX * PM64_CAST_FACE_MAX * 4];
      int w = 0, h = 0;
      if (portPm64CastFace(0, rgba, &w, &h) && w > 0 && h > 0) {
        setIcon(ic, w, h, rgba, 3);
        sPm64Gen = portPm64Generation() + 1;
      }
    }
    if (ic.from == 3) {
      return ic;
    }
  }
  // the game's own, once icon.tpl is in (in the game)
  if (ic.from == 0 || ic.from == 2) {
    PortTexInfo t{};
    std::vector<uint8_t> px;
    if (portIconTexture(portPresetItemIcon(kPresets[p].item), &t) && portCosmeticsDecode(t, px)) {
      setIcon(ic, t.width, t.height, px.data(), 1);
      return ic;
    }
  }
  // the launcher's, from the art cache (art/recipes/launcher_presets.recipe), looked for once per folder
  if (ic.from == 0 && !sIconDir.empty() && sIconTried[p] != sIconDir) {
    sIconTried[p] = sIconDir;
    const std::string path = sIconDir + kPresets[p].icon + ".png";
    if (SDL_Surface* s = SDL_LoadPNG(path.c_str())) {
      if (SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32)) {
        if (rgba->pitch == rgba->w * 4) {
          setIcon(ic, rgba->w, rgba->h, rgba->pixels, 2);
        }
        SDL_DestroySurface(rgba);
      }
      SDL_DestroySurface(s);
    }
  }
  return ic;
}

// a name's first letter (UTF-8 kept whole)
std::string initialOf(const char* text) {
  size_t n = 1;
  const unsigned char c = static_cast<unsigned char>(text[0]);
  if (c >= 0xF0) {
    n = 4;
  } else if (c >= 0xE0) {
    n = 3;
  } else if (c >= 0xC0) {
    n = 2;
  }
  return std::string(text, std::min(n, std::strlen(text)));
}

// --- What a preset changes: each setting it puts away from its default, as the page names it

std::string changesOf(int p) {
  static std::vector<PortSettingsControl> controls(1024);
  const int n = portSettingsControls(controls.data(), int(controls.size()));
  auto describe = [&](const int* at, int v) {
    for (int c = 0; c < n; c++) {
      const PortSettingsControl& s = controls[c];
      if (s.value != at || s.label == nullptr) {
        continue;
      }
      std::string out = portTr(s.label);
      if (s.kind == 1 && s.choices != nullptr && v >= 0 && v < s.count) {
        out += std::string(": ") + portTr(s.choices[v]);
      } else if (s.kind == 2) {
        out += ": " + std::to_string(v);
      } else if (v == 0) {
        out += std::string(": ") + portTr("Off");
      }
      return out;
    }
    return std::string();
  };
  std::string out;
  std::set<std::string> seen;
  auto add = [&](const std::string& s) {
    if (!s.empty() && seen.insert(s).second) {
      out += (out.empty() ? "" : ", ") + s;
    }
  };
  Table& t = table();
  for (size_t k = 0; k < t.keys.size(); k++) {
    if (t.keys[k].at != nullptr && t.wanted[p][k] != t.keys[k].def) {
      add(describe(t.keys[k].at, t.wanted[p][k]));
    }
  }
  const Part& x = kPresets[p].extras;
  for (int i = 0; i < x.count; i++) {
    if (const int* at = find(x.values[i].key)) {
      add(describe(at, x.values[i].value));
    }
  }
  return out;
}

// --- The settings page's Presets: a tile each (its picture, name and tagline), by group, the one in place
// marked; under them the one sentence of the tile the pointer or the focus is on (else the one in place),
// with what it changes in full behind an expander.

int sDetail = -1; // the tile last under the pointer or the focus

void section(int) {
  int nearest = 0, differ = 0;
  const int now = current(&nearest, &differ);
  if (now >= 0) {
    ImGui::Text(portTr("Now: %s"), portTr(kPresets[now].name));
  } else if (differ == 1) {
    ImGui::Text(portTr("Custom: 1 differs from %s"), portTr(kPresets[nearest].name));
  } else {
    ImGui::Text(portTr("Custom: %d differ from %s"), differ, portTr(kPresets[nearest].name));
  }

  const ImGuiStyle& style = ImGui::GetStyle();
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize(), tagSize = fs * 0.86f, capSize = fs * 0.8f;
  const float pad = fs * 0.22f, icon = fs * 1.8f, gap = style.ItemSpacing.x * 0.6f, rowGap = 3.f;
  const float avail = ImGui::GetContentRegionAvail().x;
  // as many across as fit (four at most): every name on one line, every tagline on two at most
  const std::vector<int>& order = shownOrder();
  int cols = 4;
  float cellW = 0.f, textW = 0.f, tagH = 0.f;
  for (; cols >= 1; cols--) {
    cellW = (avail - gap * float(cols - 1)) / float(cols);
    textW = cellW - pad * 3.f - icon;
    bool fits = textW > fs * 3.f;
    tagH = 0.f;
    for (int p = 0; p < kCount && fits; p++) {
      const float h = font->CalcTextSizeA(tagSize, FLT_MAX, textW, portTr(kPresets[p].tagline)).y;
      fits = ImGui::CalcTextSize(portTr(kPresets[p].name)).x <= textW && h <= tagSize * 2.05f;
      tagH = std::max(tagH, h);
    }
    if (fits || cols == 1) {
      break;
    }
  }
  const float cellH = std::max(icon, fs + tagH) + pad * 2.f;

  // the tiles in their groups' order, one after another; over each row a group's name above the tile
  // where that group starts
  int hovered = -1;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  float y = origin.y;
  const int n = int(order.size());
  for (int row = 0; row * cols < n; row++) {
    bool caption = false;
    for (int c = 0; c < cols && row * cols + c < n; c++) {
      const int k = row * cols + c;
      if (k == 0 || kPresets[order[k - 1]].group != kPresets[order[k]].group) {
        dl->AddText(font, capSize, ImVec2(origin.x + c * (cellW + gap) + pad, y),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), portTr(kGroupNames[kPresets[order[k]].group]));
        caption = true;
      }
    }
    if (caption) {
      y += capSize * 1.15f;
    }
    for (int c = 0; c < cols && row * cols + c < n; c++) {
      const int p = order[row * cols + c];
      ImGui::SetCursorScreenPos(ImVec2(origin.x + c * (cellW + gap), y));
      ImGui::PushID(p);
      const bool can = available(p);
      ImGui::BeginDisabled(!can);
      if (ImGui::Selectable("##preset", p == now, ImGuiSelectableFlags_None, ImVec2(cellW, cellH)) && p != now) {
        apply(p);
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused()) {
        hovered = p;
      }
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      dl->AddRect(a, b, ImGui::GetColorU32(p == now ? ImGuiCol_CheckMark : ImGuiCol_Border), fs * 0.3f, 0,
                  p == now ? 2.5f : 1.f);
      portPresetDrawIcon(dl, p, ImVec2(a.x + pad, (a.y + b.y - icon) * 0.5f), icon, !can);
      const ImVec2 at(a.x + pad * 2.f + icon, a.y + pad);
      dl->AddText(font, fs, at, ImGui::GetColorU32(can ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                  portTr(kPresets[p].name));
      dl->AddText(font, tagSize, ImVec2(at.x, at.y + fs), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                  portTr(kPresets[p].tagline), nullptr, textW);
      ImGui::PopID();
    }
    y += cellH + rowGap;
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
  ImGui::Dummy(ImVec2(avail, 0.f));
  if (hovered >= 0) {
    sDetail = hovered;
  }
  // the detail: its name and one sentence, why it can't be used now, and what it changes in full
  const int d = sDetail >= 0 ? sDetail : now >= 0 ? now : nearest;
  ImGui::TextWrapped("%s: %s", portTr(kPresets[d].name), portTr(kPresets[d].about));
  if (portConsoleAccurate() != 0) {
    muted(portTr(kLocked));
  } else if ((kPresets[d].flags & kNeedsRom) != 0 && portPm64Linked() == PM64_NONE) {
    muted(portTr(kNeedRom));
  }
  if (ImGui::TreeNode(portTrLabel("What it changes##preset_changes"))) {
    const std::string changes = changesOf(d);
    muted(changes.empty() ? portTr("Nothing: every setting that changes play at its default.") : changes.c_str());
    muted(portTr("Controls, looks, sound and video stay as they are. Change anything after: it shows as Custom."));
    ImGui::TreePop();
  }
}

// --- The Start menu's Presets: one row, the preset in place (or Custom); A opens the presets' page
// (port/src/aurora/pause_presets_aurora.c), a tile each

constexpr const char kPresetNote[] = "One choice sets every setting that changes play. {A}: see the presets.";

int presetRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  const int now = current();
  out[0] = portPauseRow("Presets", kPresetNote, now >= 0 ? kPresets[now].name : "Custom", 0);
  return 1;
}

const char* presetAct(int, int dir) {
  if (dir == 0) {
    portPresetsPageOpen();
  }
  return nullptr;
}

const PortPauseForm kPresetForm = {kTab, "Presets", presetRows, presetAct};

// --- A page's one switch: every key of a list at its value (on), or every one back to its default
// (off). On while all hold, off while none does, else how many do.

struct Switch {
  const Value* values;
  int count;
};

// how many of the switch's keys are at its value, of how many this build has
int switchOn(const Switch& s, int* total) {
  int on = 0;
  *total = 0;
  for (int i = 0; i < s.count; i++) {
    if (const int* at = find(s.values[i].key)) {
      (*total)++;
      on += *at == s.values[i].value;
    }
  }
  return on;
}

void setSwitch(const Switch& s, bool on) {
  for (int i = 0; i < s.count; i++) {
    if (int* at = find(s.values[i].key)) {
      *at = on ? s.values[i].value : defaultOf(s.values[i].key);
    }
  }
  portSettingsApply();
  portSettingsSave();
}

// --- The Remake page's: Play it like the Switch remake

constexpr const char* kRemakeTab = "Remake";
constexpr const char* kRemakeLabel = "Play it like the Switch remake";
constexpr const char kRemakeNote[] =
    "Every change the Switch remake made that Paper Cut has, on or off together: the remake's field moves, "
    "battle rules, maps, items and comforts. Each one is also below, to change on its own.";
const Switch kRemakeSwitch = {kRemake, int(sizeof(kRemake) / sizeof(kRemake[0]))};

void remakeSection(int) {
  int total = 0;
  const int on = switchOn(kRemakeSwitch, &total);
  bool all = total > 0 && on == total;
  ImGui::BeginDisabled(portConsoleAccurate() != 0);
  if (ImGui::Checkbox(portTrLabel("On##remake_switch"), &all)) {
    setSwitch(kRemakeSwitch, all);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::Text(portTr("%d of the remake's %d changes are on."), on, total);
  ImGui::TextWrapped("%s", portTr(portConsoleAccurate() != 0 ? kLocked : kRemakeNote)); // (greyed: why)
  ImGui::PopStyleColor();
}

// one switch's row in the Start menu: On, Off, or how many of how many
void switchRow(PortPauseRow* out, const Switch& s, const char* label, const char* note, unsigned flags) {
  int total = 0;
  const int on = switchOn(s, &total);
  *out = portPauseRow(label, note, on == total ? "On" : on == 0 ? "Off" : nullptr, 0, flags);
  if (on != total && on != 0) {
    std::snprintf(out->value, sizeof(out->value), "%d/%d", on, total); // (some: how many)
  }
}

int remakeRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  switchRow(out, kRemakeSwitch, kRemakeLabel, kRemakeNote, portConsoleAccurate() != 0 ? PORT_PAUSE_ROW_DIM : 0);
  return 1;
}

const char* remakeAct(int, int) {
  int total = 0;
  if (portConsoleAccurate() != 0) {
    return kLocked;
  }
  setSwitch(kRemakeSwitch, switchOn(kRemakeSwitch, &total) != total); // (A, left or right: all on, or all off once all are)
  return nullptr;
}

const PortPauseForm kRemakeForm = {kRemakeTab, kRemakeLabel, remakeRows, remakeAct};

// --- The Paper Mario 64 page's: Everything Paper Mario 64 (shown with a ROM linked). How its partners
// join and the boss list are their own settings, left as they are.

constexpr const char* kPm64Label = "Everything Paper Mario 64";
constexpr const char kPm64Note[] =
    "With your Paper Mario ROM linked, on or off together: its partners, its enemies in the story, its bosses "
    "with their music, Rowf's badges, Tayce T.'s recipes, its sounds and the Star Spirits. Off puts each back to "
    "its default. Each one is also below, to change on its own.";
const Value kPm64Switch[] = {
    {"pm64_partners", 1}, {"pm64_story_enemies", 1}, {"pm64_bosses", 1},     {"pm64_boss_music", 1},
    {"pm64_badges", 1},   {"pm64_recipes", 1},       {"pm64_sfx", 1},        {"pm64_sfx_general", 1},
    {"pm64_star_spirits", 1},
};
const Switch kPm64SwitchList = {kPm64Switch, int(sizeof(kPm64Switch) / sizeof(kPm64Switch[0]))};

bool sPm64Ask = false; // a ROM just linked: "Turn on all Paper Mario 64 content?" until answered

// the answer; why a Yes changed nothing (English), or NULL. (The question waits while console-accurate mode
// is on: portPresetPm64AskPending, pm64Rows; this is for one turned on with the question up.)
const char* pm64Answer(bool yes) {
  sPm64Ask = false;
  std::printf("[presets] all Paper Mario 64 content? %s\n", yes ? "yes" : "no");
  std::fflush(stdout);
  if (yes && portConsoleAccurate() != 0) {
    return kLocked;
  }
  if (yes) {
    setSwitch(kPm64SwitchList, true);
  }
  return nullptr;
}

void pm64Section(int) {
  if (sPm64Ask && portPm64Linked() != PM64_NONE && portConsoleAccurate() == 0) {
    ImGui::TextWrapped("%s", portTr(kPresetPm64Ask));
    muted(portTr(kPresetPm64AskNote));
    static int sFocus = 0;
    if (ImGui::Button(portTrLabel("Yes##pm64_ask_yes"))) {
      pm64Answer(true); // (asked only while console-accurate mode is off: the switch's note says why if on)
    }
    if (sFocus++ == 0) {
      ImGui::SetItemDefaultFocus(); // (Yes highlighted)
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("No##pm64_ask_no"))) {
      pm64Answer(false);
    }
    ImGui::Separator();
  }
  if (portPm64Linked() == PM64_NONE) {
    muted(portTr("Link your Paper Mario ROM first (Link Paper Mario 64, above): then one switch turns all of "
                 "its content on."));
    return;
  }
  int total = 0;
  const int on = switchOn(kPm64SwitchList, &total);
  bool all = total > 0 && on == total;
  ImGui::BeginDisabled(portConsoleAccurate() != 0);
  if (ImGui::Checkbox(portTrLabel("On##pm64_switch"), &all)) {
    setSwitch(kPm64SwitchList, all);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::Text(portTr("%d of these %d are on."), on, total);
  ImGui::TextWrapped("%s", portTr(portConsoleAccurate() != 0 ? kLocked : kPm64Note)); // (greyed: why)
  ImGui::PopStyleColor();
}

constexpr int kAskYes = 1, kAskNo = 2;

int pm64Rows(PortPauseRow* out, int max) {
  if (max < 3) {
    return 0;
  }
  const bool linked = portPm64Linked() != PM64_NONE;
  int n = 0;
  if (sPm64Ask && linked && portConsoleAccurate() == 0) { // (Yes first: the row the cursor comes to)
    out[n++] = portPauseRow(kPresetPm64Ask, kPresetPm64AskNote, "Yes", kAskYes);
    out[n++] = portPauseRow("No, leave them as they are", kPresetPm64AskNote, nullptr, kAskNo);
  }
  switchRow(&out[n++], kPm64SwitchList, kPm64Label, linked ? kPm64Note : kNeedRom,
            !linked || portConsoleAccurate() != 0 ? PORT_PAUSE_ROW_DIM : 0);
  return n;
}

const char* pm64Act(int id, int dir) {
  if ((id == kAskYes || id == kAskNo) && dir == 0) {
    return pm64Answer(id == kAskYes);
  }
  if (id != 0) {
    return nullptr;
  }
  int total = 0;
  if (portConsoleAccurate() != 0) {
    return kLocked;
  }
  if (portPm64Linked() == PM64_NONE) {
    return kNeedRom;
  }
  setSwitch(kPm64SwitchList, switchOn(kPm64SwitchList, &total) != total);
  return nullptr;
}

const PortPauseForm kPm64Form = {kTab, kPm64Label, pm64Rows, pm64Act};

// --- Settings from 0.4.1 and older
//
// Every key those releases wrote is still read as it was (none was renamed; Bag size and Skip scenes
// kept their old values' meanings and only gained new ones), so their choices all carry over. The presets
// came in 0.5.0 and name settings those releases didn't have, which would make an old setup show as Custom.
// The rule: the first start of 0.5.0 on a settings.txt from an older release (one found at start, not looked
// at before, and none of the settings below away from its default, which only 0.5.0 writes) compares it
// with each preset on the settings an older release had. When one matches there (the one naming the most
// of them, when several do), that preset's 0.5.0 version is put in place: only these new settings change.
// No match: everything as it was (Custom). Console-accurate mode on: nothing changes.
//
// Defaults changed in 0.5.0 (Skip any scene, Second player and, on a computer, Anisotropic filtering on)
// need no step here: 0.4.0 and 0.4.1 write a feature's setting only when it differs from its default, so
// one the player never changed isn't in their file and reads as the new default; one they changed keeps
// its value. (One set back to the old default by hand isn't in the file either, so it can't be told from
// untouched: it takes the new default too.) 0.1.0 to 0.3.0 wrote every setting, so there a value can't be
// told from untouched at all: their files keep what they hold.
const char* const kNewSince041[] = {
    "audience_players", "auto_win_weak", "balanced_badges", "battle_fp_preview", "battle_immune_cross",
    "battle_sp_grey", "bowser_retry", "coin_cap", "coin_hints", "full_bag_use", "jukebox_unlock", "less_flashing",
    "less_shake", "levelup_heal_party", "list_lr_scroll", "mail_no_stop", "no_audience_throws", "no_stage_hazards",
    "pace_trims", "partner_hint", "pg_boo_quiz", "pg_buy_tokens", "pg_crane_prizes", "pg_jp_recipes",
    "pg_newsletter", "pg_pit_whacka", "pg_second_tattles", "pm64_badges", "pm64_boss_music", "pm64_boss_scale",
    "pm64_bosses", "pm64_enemies", "pm64_field", "pm64_join", "pm64_met_all", "pm64_partners", "pm64_recipes", "pm64_sfx", "pm64_sfx_general",
    "pm64_star_spirits", "pm64_story_enemies", "remake_ambushes", "remake_blocks", "remake_chapter_shortcuts",
    "remake_express_skip", "remake_field_hammer", "remake_guard_stance", "remake_keelhaul", "remake_koops_aim",
    "remake_merchants", "remake_notice_range", "remake_pipe_a", "remake_plane_steady", "remake_projectile_hurt",
    "remake_shortcut_pipes", "remake_steeple_stairs", "remake_teeter", "remake_twilight", "remake_warp_pipes",
    "remake_yoshi", "rogueport_start", "rules_audience_pause", "rules_bandit_returns", "rules_bomb_no_sp",
    "rules_boss_coins", "rules_buzzy_blast", "rules_chomp_still", "rules_deep_sleep", "rules_doopliss",
    "rules_doopliss_shed", "rules_flee_coins", "rules_flower_fuzzy", "rules_glitz_conditions",
    "rules_hazard_pierce", "rules_lottery", "rules_mystery_box", "rules_pit_shop", "rules_power_lift",
    "rules_retattle", "rules_sleepy_sheep", "rules_sp_fill_wait", "rules_spider_strike", "rules_sweet_treat",
    "rules_swoopula_swoop", "rules_tremor_stagehand", "rules_trouble_coins", "save_file_details", "shop_plus",
    "skip_any_scene", "skip_interludes", "spring_jump_cue", "start_in_rogueport", "talk_prompts", "team_bonus",
    "text_hold_skip",
};

bool newSince041(const char* key) {
  for (const char* k : kNewSince041) {
    if (std::strcmp(k, key) == 0) {
      return true;
    }
  }
  return false;
}

void migrate() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  int* mark = portSettingsFind("preset_migrated");
  if (mark == nullptr || *mark != 0) {
    return;
  }
  *mark = 1; // (written with the next save: a new install's first is one too)
  if (!portSettingsFileFound()) {
    return;
  }
  Table& t = table();
  // (first: while it's on, the settings that change play read as the console's, not as the file has them)
  if (portConsoleAccurate() != 0) {
    std::printf("[presets] Console-accurate mode on: the settings left as they are\n");
    portSettingsSave();
    return;
  }
  for (const char* key : kNewSince041) {
    const int* at = find(key);
    if (at != nullptr && *at != defaultOf(key)) {
      std::printf("[presets] settings from 0.5.0 (%s set): left as they are\n", key);
      portSettingsSave();
      return;
    }
  }
  int best = -1, bestNamed = -1;
  for (int p = 0; p < kCount; p++) {
    bool same = true;
    int named = 0;
    for (size_t k = 0; k < t.keys.size() && same; k++) {
      const Entry& e = t.keys[k];
      if (e.at == nullptr || newSince041(e.key)) {
        continue;
      }
      same = *e.at == t.wanted[p][k];
      named += t.wanted[p][k] != e.def;
    }
    const Part& x = kPresets[p].extras;
    for (int i = 0; i < x.count && same; i++) {
      const int* at = find(x.values[i].key);
      if (at != nullptr && !newSince041(x.values[i].key)) {
        same = *at == x.values[i].value;
        named++;
      }
    }
    if (same && named > bestNamed) {
      best = p;
      bestNamed = named;
    }
  }
  if (best > 0) {
    std::printf("[presets] settings from an older release matched %s: its 0.5.0 version put in place\n",
                kPresets[best].name);
    apply(best);
  } else {
    std::printf("[presets] settings from an older release: %s, left as they are\n",
                best == 0 ? "Vanilla" : "Custom");
  }
  std::fflush(stdout);
  portSettingsSave();
}

// --- Tests ($TTYD_PRESETS_TEST=1): once the game draws; =show: the preset shown and every setting of the
// gameplay set (and the extras) away from its default, for the migration's checks

struct Saved {
  std::string key;
  int value;
};

std::vector<Saved> snapshot() {
  std::vector<Saved> out;
  const int n = portSettingsExtraCount();
  for (int i = 0; i < n; i++) {
    int v = 0;
    if (const char* key = portSettingsExtraAt(i, &v)) {
      out.push_back({key, v});
    }
  }
  out.push_back({"run_speed", portSettings()->runSpeed});
  return out;
}

void restore(const std::vector<Saved>& s) {
  for (const Saved& e : s) {
    if (int* at = find(e.key.c_str())) {
      *at = e.value;
    }
  }
  portSettingsSave();
}

// what differs from the snapshot now, as "key=value (was w)"
std::string diff(const std::vector<Saved>& before, int* count) {
  std::string out;
  *count = 0;
  for (const Saved& e : before) {
    const int* at = find(e.key.c_str());
    if (at != nullptr && *at != e.value) {
      (*count)++;
      char line[160];
      std::snprintf(line, sizeof(line), " %s=%d (was %d)", e.key.c_str(), *at, e.value);
      out += line;
    }
  }
  return out;
}

void showNow() {
  int nearest = 0, differ = 0;
  const int now = current(&nearest, &differ);
  std::printf("[presets] shown: %s (nearest %s, %d differ)\n", now >= 0 ? kPresets[now].name : "Custom",
              kPresets[nearest].name, differ);
  std::string line;
  for (const Saved& e : snapshot()) {
    int def = 0;
    if (portSettingsDefault(e.key.c_str(), &def) && e.value != def) {
      line += " " + e.key + "=" + std::to_string(e.value);
    }
  }
  std::printf("[presets] away from default:%s\n", line.c_str());
  std::fflush(stdout);
}

void selfTest(int) {
  static bool done = false;
  migrate(); // (a start straight into the game, past the launcher)
  static const char* const mode = SDL_getenv("TTYD_PRESETS_TEST");
  if (mode == nullptr || done) {
    return;
  }
  done = true;
  if (std::strcmp(mode, "show") == 0) {
    showNow();
    return;
  }
  if (mode[0] != '1') {
    return;
  }
  // the gameplay set's keys at their defaults first, so "Vanilla" is where every run comes back to
  const std::vector<Saved> before = snapshot();
  Table& t = table();
  int bad = 0;
  for (const Entry& e : t.keys) {
    if (e.at == nullptr) {
      std::printf("[presets] not in this build: %s\n", e.key);
    }
  }
  apply(0);
  const std::vector<Saved> vanilla = snapshot();
  int count = 0;
  const std::string start = diff(before, &count);
  std::printf("[presets] Vanilla from the start: %d changed:%s\n", count, start.c_str());
  // the action-command assists and Hold to mash: fresh, then as each preset leaves them from fresh (only
  // Accessible's extras set them, to 1)
  static const char* const kAssistKeys[] = {"ac_attack_assist", "ac_guard_assist", "hold_to_mash"};
  int fresh[3];
  for (int k = 0; k < 3; k++) {
    const int* at = portSettingsFind(kAssistKeys[k]);
    fresh[k] = at != nullptr ? *at : -1;
  }
  std::printf("[presets] assists fresh: attack %d, guard %d, hold to mash %d\n", fresh[0], fresh[1], fresh[2]);
  for (int p = 0; p < kCount; p++) {
    for (int k = 0; k < 3; k++) {
      if (int* at = portSettingsFind(kAssistKeys[k])) {
        *at = fresh[k];
      }
    }
    apply(p);
    {
      int v[3];
      for (int k = 0; k < 3; k++) {
        const int* at = portSettingsFind(kAssistKeys[k]);
        v[k] = at != nullptr ? *at : -1;
      }
      std::printf("[presets] assists after %s: attack %d, guard %d, hold to mash %d\n", kPresets[p].name, v[0],
                  v[1], v[2]);
    }
    const int now = current();
    std::printf("[presets] %s: shown as %s, %d differ from it\n", kPresets[p].name,
                now >= 0 ? kPresets[now].name : "Custom", differences(p));
    if (now != p) {
      bad++;
    }
    // and back
    apply(0);
    const std::string d = diff(vanilla, &count);
    std::printf("[presets] %s then Vanilla: %d settings differ from Vanilla's:%s\n", kPresets[p].name, count,
                d.c_str());
    // (only the preset's extras may stay: they are outside the gameplay set)
    if (count > kPresets[p].extras.count) {
      bad++;
    }
    restore(vanilla);
  }
  // every setting that changes play is in the gameplay set or the player's own
  {
    std::set<std::string> known;
    for (const Entry& e : t.keys) {
      known.insert(e.key);
    }
    for (const char* key : kOwn) {
      known.insert(key);
    }
    for (const Value& v : kAccessibleExtras) {
      known.insert(v.key);
    }
    std::map<const int*, const char*> keyOf; // a declared setting's value -> its key
    const int e = portSettingsExtraCount();
    for (int i = 0; i < e; i++) {
      int v = 0;
      if (const char* key = portSettingsExtraAt(i, &v)) {
        keyOf[portSettingsFind(key)] = key;
      }
    }
    std::vector<PortSettingsControl> controls(1024);
    const int n = portSettingsControls(controls.data(), int(controls.size()));
    for (int c = 0; c < n; c++) {
      if ((controls[c].flags & PORT_SETTING_CHANGES_PLAY) == 0 || controls[c].value == nullptr) {
        continue;
      }
      const auto it = keyOf.find(controls[c].value);
      if (it != keyOf.end() && known.count(it->second) == 0) {
        std::printf("[presets] changes play but in no preset's set: %s (%s)\n", it->second, controls[c].label);
      }
    }
  }
  // the Paper Mario 64 switch: all on, then all back to their defaults
  {
    int total = 0;
    setSwitch(kPm64SwitchList, true);
    const int on = switchOn(kPm64SwitchList, &total); // (first: a printf's arguments have no set order)
    std::printf("[presets] pm64 switch on: %d of %d (linked %d)\n", on, total, portPm64Linked());
    setSwitch(kPm64SwitchList, false);
    int defaults = 0;
    for (const Value& v : kPm64Switch) {
      const int* at = find(v.key);
      defaults += at != nullptr && *at == defaultOf(v.key);
    }
    const int stillOn = switchOn(kPm64SwitchList, &total);
    std::printf("[presets] pm64 switch off: %d of %d at their defaults, %d of %d on\n", defaults, total, stillOn,
                total);
  }
  restore(before);
  std::printf("[presets] test done: %d problems\n", bad);
  std::fflush(stdout);
}

struct Register {
  Register() {
    portSettingsDeclare("preset_picker_seen", 0, 0, 1);
    portSettingsDeclare("preset_migrated", 0, 0, 1);
    portSettingsAddSection(kTab, "Presets", nullptr, 0, section,
                           "preset vanilla remake rules enhanced story relaxed easy challenge hard explorer "
                           "completionist couch party accessible paper mario 64 randomizer speedrun one click");
    portSettingsAddSection(kRemakeTab, kRemakeLabel, nullptr, 0, remakeSection,
                           "remake switch 2024 nintendo switch all rules preset one");
    portSettingsAddSection(kTab, kPm64Label, nullptr, 0, pm64Section,
                           "paper mario 64 pm64 n64 everything all switch one partners enemies bosses badges "
                           "recipes sounds star spirits");
    portPauseAddForm(&kPresetForm);
    portPauseAddForm(&kRemakeForm);
    portPauseAddForm(&kPm64Form);
    portSettingsAddOverlay(selfTest, nullptr);
  }
} sRegister;

} // namespace

// --- The launcher's first-run picker and the Paper Mario 64 question (port/settings/enhancement_presets.h)

const char* const kPresetPm64Ask = "Turn on all Paper Mario 64 content?";
const char* const kPresetPm64AskNote =
    "Its partners, its enemies in the story, its bosses with their music, Rowf's badges, Tayce T.'s recipes, its "
    "sounds and the Star Spirits: the Everything Paper Mario 64 switch (Enhancements, Paper Mario 64). Each can "
    "be changed on its own after.";

void portPresetPm64Linked() {
  int total = 0;
  sPm64Ask = switchOn(kPm64SwitchList, &total) != total && portConsoleAccurate() == 0;
}
// (not while console-accurate mode is on: a Yes then could change nothing; it waits until it is off)
bool portPresetPm64AskPending() { return sPm64Ask && portPm64Linked() != PM64_NONE && portConsoleAccurate() == 0; }
void portPresetPm64Answer(bool yes) { pm64Answer(yes); }

int portPresetCount() { return kCount; }
const char* portPresetName(int i) { return i >= 0 && i < kCount ? kPresets[i].name : ""; }
const char* portPresetTagline(int i) { return i >= 0 && i < kCount ? kPresets[i].tagline : ""; }
const char* portPresetAbout(int i) { return i >= 0 && i < kCount ? kPresets[i].about : ""; }
int portPresetItem(int i) { return i >= 0 && i < kCount ? kPresets[i].item : 0; }
int portPresetGroup(int i) { return i >= 0 && i < kCount ? int(kPresets[i].group) : 0; }
const char* portPresetGroupName(int group) { return group >= 0 && group < kGroups ? kGroupNames[group] : ""; }
int portPresetShown(int k) { return k >= 0 && k < int(shownOrder().size()) ? shownOrder()[k] : -1; }
int portPresetInPicker(int i) { return i >= 0 && i < kCount && (kPresets[i].flags & kPicker) != 0; }
int portPresetAvailable(int i) { return i >= 0 && i < kCount && available(i); }
int portPresetNow(int* nearest, int* differ) { return current(nearest, differ); }

void portPresetApply(int i) {
  if (portPresetAvailable(i)) {
    apply(i);
  }
}

const char* portPresetUse(int i) {
  if (i < 0 || i >= kCount) {
    return nullptr;
  }
  if (portConsoleAccurate() != 0) {
    return kLocked;
  }
  if (!available(i)) {
    return kNeedRom;
  }
  apply(i);
  portSettingsApply();
  return nullptr;
}

void portPresetIconDir(const char* dir) { sIconDir = dir != nullptr ? dir : ""; }

void portPresetDrawIcon(ImDrawList* dl, int i, ImVec2 at, float size, bool dim) {
  if (i < 0 || i >= kCount) {
    return;
  }
  const ImU32 tint = dim ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 255);
  const Icon& ic = iconOf(i);
  if (ic.tex != ImTextureID{} && ic.w > 0.f && ic.h > 0.f) {
    // (as large as fits, its shape kept)
    const float k = size / std::max(ic.w, ic.h);
    const ImVec2 sz(ic.w * k, ic.h * k);
    const ImVec2 a(at.x + (size - sz.x) * 0.5f, at.y + (size - sz.y) * 0.5f);
    dl->AddImage(ic.tex, a, ImVec2(a.x + sz.x, a.y + sz.y), ImVec2(0, 0), ImVec2(1, 1), tint);
    return;
  }
  // none yet (no disc in the launcher): the name's initial on a paper token
  const ImVec2 c(at.x + size * 0.5f, at.y + size * 0.5f);
  const float r = size * 0.42f;
  dl->AddCircleFilled(ImVec2(c.x + size * 0.03f, c.y + size * 0.05f), r, IM_COL32(20, 10, 30, dim ? 30 : 60));
  dl->AddCircleFilled(c, r, dim ? IM_COL32(236, 226, 206, 255) : IM_COL32(248, 226, 170, 255));
  dl->AddCircle(c, r, dim ? IM_COL32(150, 136, 120, 255) : IM_COL32(86, 40, 30, 255), 0, std::max(1.f, size * 0.05f));
  const std::string letter = initialOf(portTr(kPresets[i].name));
  ImFont* font = ImGui::GetFont();
  const float fs = size * 0.5f;
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, letter.c_str());
  dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
              dim ? IM_COL32(150, 136, 120, 255) : IM_COL32(86, 40, 30, 255), letter.c_str());
}

namespace {
bool sPickerAnswered = false; // (this run: $TTYD_PRESET_PICKER shows it until then)
}

bool portPresetPickerDue() {
  migrate(); // (the launcher's first frame: before anything is shown)
  if (sPickerAnswered) {
    return false;
  }
  static const bool forced = [] {
    const char* v = SDL_getenv("TTYD_PRESET_PICKER");
    return v != nullptr && v[0] == '1';
  }();
  if (forced) {
    return true;
  }
  const int* seen = portSettingsFind("preset_picker_seen");
  return !portSettingsFileFound() && (seen == nullptr || *seen == 0) && portConsoleAccurate() == 0;
}

void portPresetPickerDone() {
  sPickerAnswered = true;
  if (int* seen = portSettingsFind("preset_picker_seen")) {
    *seen = 1;
  }
  portSettingsSave();
}
