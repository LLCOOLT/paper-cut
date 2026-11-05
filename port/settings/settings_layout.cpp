// Where every setting is shown, in one place: the settings page's tabs in
// order, and in each tab its groups and their settings, most used first
// (port/include/port/settings.h, portSettingsPlace). The full page, the
// launcher's Settings tab, the Start menu's Settings tab and the second
// screen's copy of it all show this order, under the groups' names.
//
// A feature still names a tab when it adds its control (portSettingsAdd*):
// that is where it goes when it isn't listed here (after the listed ones,
// under "More", and named in the log at start so it gets a place). A setting
// is found by that tab and its English label, so its key, which settings.txt,
// presets, codes, replays and online use, never changes with its place.
#include "port/settings.h"
#include <cstdio>
#include <cstring>

namespace {

enum Kind { kTabRow, kGroupRow, kItemRow, kNewRow };

struct Row {
  Kind kind;
  const char* text; // the tab's or group's name, or the setting's label
  const char* from; // a setting's: the tab its feature names, when not this one
};

#define TAB(name) {kTabRow, name, nullptr}
#define GROUP(name) {kGroupRow, name, nullptr}
#define ITEM(label) {kItemRow, label, nullptr}
#define FROM(label, tab) {kItemRow, label, tab}
// new in this version: a "new" marker beside it on the full page
#define NEW(label) {kNewRow, label, nullptr}
#define NEW_FROM(label, tab) {kNewRow, label, tab}

// The settings first, in the order most games list theirs (the game itself,
// what you see, hear and hold, then the helpers, the language, playing
// together, the saves and the program), then the modes and tools.
const Row kLayout[] = {
    TAB("Gameplay"),
    GROUP("Character"),
    NEW_FROM("Play as", "Enhancements"),
    GROUP("Difficulty"),
    ITEM("Difficulty"),
    ITEM("Enemy HP (custom)"),
    ITEM("Enemy attack (custom)"),
    NEW("Level sync"),
    GROUP("Speed"),
    ITEM("Text speed"),
    FROM("Faster text outside battle", "Enhancements"),
    FROM("Faster battles", "Enhancements"),
    FROM("Faster battle menus", "Enhancements"),
    ITEM("Run speed"),
    FROM("Skip repeated scenes", "Enhancements"),
    GROUP("Fast-forward and rewind"),
    ITEM("Fast-forward speed"),
    ITEM("Fast-forward toggles"),
    ITEM("Rewind"),
    ITEM("Rewind length"),
    ITEM("Rewind key"),
    GROUP("Camera"),
    ITEM("Free camera"),
    NEW("Mouse camera"),
    NEW("Mouse camera speed"),
    NEW("Invert mouse camera"),
    GROUP("As on the console"),
    NEW("Console-accurate mode"),

    TAB("Enhancements"),
    GROUP("Getting around"),
    ITEM("Fast travel"),
    ITEM("Travel to"),
    NEW_FROM("Area map", "Area map"),
    ITEM("Walk-speed boost"),
    ITEM("Boost speed"),
    ITEM("Boost key"),
    GROUP("Partners"),
    ITEM("Partner quick-swap"),
    NEW_FROM("Partner wheel", "Controls"),
    GROUP("Items and saving"),
    ITEM("Save Blocks save at once"),
    ITEM("Storage at every Save Block"),
    ITEM("Bigger bag"),
    ITEM("Junk-item helper in shops"),
    GROUP("Battles"),
    ITEM("Battle pop-ups"),
    NEW("Battle grades"),
    NEW("Battle recap"),
    ITEM("Tattle without a turn"),
    ITEM("Partners never at 0 HP"),
    ITEM("Healing items heal every partner"),
    ITEM("Level up with the D-pad"),
    ITEM("Retry on Game Over"),
    ITEM("Badge loadouts"),
    ITEM("Loadouts"),
    GROUP("Rewards and balance"),
    ITEM("Coins from battles"),
    ITEM("Star Points from battles"),
    ITEM("Remake balance"),
    ITEM("Pit of 100 Trials extras"),
    ITEM("Glitz Pit kindness"),
    GROUP("New things to find"),
    NEW("Paper Cut badge set"),
    NEW("Paper Cut badges"),
    NEW("Badge maker"),
    NEW("Zess T.'s new dishes"),
    GROUP("New Game+"),
    ITEM("New Game+"),
    ITEM("New Game+ from file"),
    GROUP("Collectibles"), // (the Journal, a list to read: last)
    ITEM("Star Piece and Shine Sprite tracker"),
    ITEM("On this map"),
    ITEM("Journal"),

    TAB("Display"),
    GROUP("Screen"),
    ITEM("Display mode"),
    ITEM("Window"),
    ITEM("Frame rate"),
    ITEM("VSync"),
    ITEM("Variable refresh rate (G-Sync, FreeSync)"),
    NEW_FROM("Battery saver", "Graphics"),
    ITEM("Run at 50 Hz (PAL)"),
    ITEM("Orientation"),
    ITEM("Keep clear of the notch"),
    GROUP("Picture shape"),
    ITEM("Widescreen"),
    ITEM("Aspect ratio"),
    ITEM("Widest picture"),
    ITEM("Bars where a scene ends"),
    ITEM("Blurred side bars"),
    GROUP("Other screens"),
    ITEM("HDR output"),
    ITEM("Second screen"),

    TAB("Graphics"),
    GROUP("Quality"),
    ITEM("Internal resolution"),
    ITEM("Anti-aliasing (4x MSAA)"),
    ITEM("Supersampling"),
    ITEM("Texture filtering"),
    ITEM("Anisotropic filtering"),
    ITEM("Texture detail"),
    ITEM("Texture upscaling"),
    ITEM("Never skip loading graphics"),
    NEW("Sharp text"),
    GROUP("Paper looks"),
    NEW("Paper shadows"),
    NEW("Paper physics"),
    NEW("Smooth paper edges"),
    NEW_FROM("Room transitions", "Display"),
    GROUP("Filter"),
    ITEM("Picture filter"),

    TAB("Audio"),
    GROUP("Volume"),
    ITEM("Volume"),
    ITEM("Music"),
    ITEM("Sound effects"),
    ITEM("Ambience"),
    GROUP("Output"),
    ITEM("Speakers"),
    NEW_FROM("Sound from the DualSense", "Controls"),
    ITEM("Mute in the background"),
    GROUP("Jukebox"),
    FROM("Jukebox", "Enhancements"),
    FROM("Tracks", "Enhancements"),
    GROUP("Music"),
    ITEM("Soundtrack"),
    ITEM("Music folder"),

    TAB("Controls"),
    GROUP("Controllers"),
    ITEM("Bindings"),
    ITEM("Controllers"),
    ITEM("Set up a controller"),
    ITEM("Button prompts"),
    ITEM("Controllers in the background"),
    GROUP("Rumble and light"),
    ITEM("Rumble"),
    ITEM("Rumble strength"),
    ITEM("Controller haptics"),
    ITEM("Controller light"),
    ITEM("The light's colour"),
    GROUP("Touch"),
    ITEM("Move the on-screen controls"),
    ITEM("On-screen controls' opacity"),
    ITEM("On-screen controls' size"),
    ITEM("Auto-run"),
    ITEM("Fixed stick"),
    ITEM("Tap the picture to press A"),
    ITEM("Haptic tap on button press"),
    ITEM("Vibrate without a controller"),
    GROUP("Other ways to play"),
    ITEM("One-handed controls"),
    ITEM("Mouse in menus"),

    TAB("Interface"),
    GROUP("On screen"),
    ITEM("HUD size"),
    ITEM("Widescreen HUD"),
    ITEM("Menu speed"),
    GROUP("Overlays"),
    ITEM("Overlay corner"),
    ITEM("Performance overlay"),
    ITEM("Input display"),

    TAB("Accessibility"),
    GROUP("Seeing"),
    ITEM("Message size"),
    ITEM("Colour-blind filter"),
    ITEM("Filter strength"),
    GROUP("Hearing and reading"),
    ITEM("Captions for sounds"),
    ITEM("Read text aloud"),
    ITEM("Reading speed"),
    ITEM("Message log"),
    GROUP("Timing"),
    ITEM("Action Command cue"),
    ITEM("Action commands: attacks"),
    ITEM("Action commands: guards"),
    NEW("Timing assist by command type"),
    NEW("Action command timing in every battle"),
    FROM("Hold instead of mashing", "Controls"),
    ITEM("Game speed"),
    ITEM("Slower only in battle"),

    TAB("Language"),
    ITEM("Text language"),
    ITEM("Menu language"),
    ITEM("Language packs"),

    TAB("Multiplayer"),
    GROUP("Co-op"),
    ITEM("Second player (co-op)"),
    ITEM("Players"),
    NEW("Brothers mode"),
    NEW("Duo moves"),
    NEW("Dungeon master"),
    GROUP("Together"),
    NEW("Co-op pings"),
    NEW("Paper notes"),

    TAB("Saves"),
    GROUP("Saving"),
    FROM("Autosave", "Gameplay"),
    NEW("Keep every save file"),
    NEW("Save files"),
    NEW("Settings per save file"),
    ITEM("Open the save folder"),
    ITEM("Saves"),
    ITEM("Sync saves through a folder"),
    ITEM("Sync saves with iCloud Drive"),
    GROUP("Backups"),
    ITEM("Backups kept"),
    ITEM("Restore a backup"),
    GROUP("Other devices"),
    ITEM("Send saves to another device"),
    ITEM("Receive saves"),
    ITEM("Copy a save to another release"),

    TAB("General"),
    GROUP("Starting"),
    FROM("Skip the intro", "Gameplay"),
    ITEM("Skip the launcher"),
    ITEM("Quick resume"),
    NEW("Pop-up launcher"),
    GROUP("In the background"),
    ITEM("Pause in the background"),
    GROUP("Pictures"),
    ITEM("Take a screenshot"),
    ITEM("Photo mode"),
    GROUP("Updates"),
    ITEM("Check for updates at start"),
    NEW("Install updates automatically"),
    ITEM("New version"),
    NEW("What's new"),
    GROUP("More"),
    ITEM("Settings presets"),
    NEW("Add to Steam"),
    ITEM("Discord status"),
    ITEM("Crash reports"),

    // the modes and tools
    TAB("Modes"),
    // the modes to start first (as the Start menu's page has them), the switches after, the
    // Director's Cut journal (a list to read) last
    GROUP("Party games"),
    NEW("Versus battle"),
    NEW("Raid boss"),
    NEW("Quiz party"),
    NEW("Bingo"),
    NEW("Tattle cards"),
    NEW("Pit of 100 Trials in co-op"),
    NEW("Dungeon master"), // (the Start menu's Modes hub: a row to Multiplayer's setting, modes_hub_links.cpp)
    GROUP("Battles"),
    ITEM("Boss rush"),
    ITEM("Boss refights"),
    ITEM("Endless Glitz Pit"),
    NEW("Battle puzzles"),
    NEW("Action-command dojo"),
    NEW("Leaderboards"),
    GROUP("After the story"),
    NEW("Chapter select"),
    NEW("The Paper Tower"),
    NEW("Paper Tower seed"),
    NEW("Trophy room"),
    GROUP("Every day"),
    NEW("Daily seed"),
    NEW("Daily shop specials"),
    NEW("Today's specials"),
    GROUP("New ways through the story"),
    FROM("Mirror mode", "Gameplay"),
    NEW("Hardcore"),
    NEW("Director's Cut"),
    NEW("Master Quest"),
    NEW("Master Quest set"),
    NEW("Remix: enemies"),
    NEW("Remix: badge draft"),
    NEW("Remix seed"),
    GROUP("Just for fun"),
    NEW("Big heads"),
    NEW("Moon gravity"),
    NEW("Paper-thin from every side"),
    NEW("Luigi's voice for Mario"),
    NEW("Spinning paper"),
    NEW("Chaos mode"),
    GROUP("Journal"),
    NEW("Director's Cut journal"),

    TAB("Challenges"),
    GROUP("Rules"),
    ITEM("Presets"),
    ITEM("One-hit KO"),
    ITEM("Guarding"),
    ITEM("Attack commands"),
    ITEM("No items in battle"),
    GROUP("Make your own"),
    NEW("Your challenges"),
    NEW("Master Quest sets"),
    NEW("Use my enemies"),
    NEW("Enemy maker"),

    TAB("Practice"),
    GROUP("Timer"),
    ITEM("Speedrun timer"),
    ITEM("Load removal"),
    ITEM("Start with a new file"),
    ITEM("Splits"),
    ITEM("Run"),
    ITEM("LiveSplit"),
    ITEM("LiveSplit connection"),
    GROUP("Save states and warps"),
    ITEM("Save states"),
    ITEM("Save warps"),
    ITEM("Replays"),
    NEW("Trick practice"),
    GROUP("Tools"),
    ITEM("Frame advance keys"),
    ITEM("Frame advance"),
    ITEM("Fixed RNG"),
    ITEM("RNG"),
    ITEM("Stats"),
    ITEM("Items"),
    ITEM("Story and flags"),
    ITEM("Memory watch corner"),
    ITEM("Memory watch"),

    TAB("Cosmetics"),
    ITEM("Theme"),
    ITEM("Looks and colour picker"),

    TAB("Randomizer"),
    ITEM("Built-in randomizer"),

    TAB("Archipelago"),
    ITEM("Archipelago"),

    TAB("Achievements"),
    ITEM("Achievements"),
    ITEM("This file"),

    TAB("Mods"),
    ITEM("Mods"),
    ITEM("Texture packs"),
    NEW("Lua mods"),
    ITEM("Export characters"),

    TAB("Streaming"),
    ITEM("Twitch chat drives the audience"),
    ITEM("Cooldown"),
    ITEM("Allow the harmful ones"),
    ITEM("Chat"),
};

// the row of the setting (tab, label) names, or -1
int find(const char* tab, const char* label) {
  const char* in = nullptr;
  for (int i = 0; i < static_cast<int>(sizeof(kLayout) / sizeof(kLayout[0])); i++) {
    const Row& r = kLayout[i];
    if (r.kind == kTabRow) {
      in = r.text;
    } else if (r.kind != kGroupRow && std::strcmp(r.text, label) == 0 &&
               std::strcmp(r.from != nullptr ? r.from : in, tab) == 0) {
      return i;
    }
  }
  return -1;
}

} // namespace

extern "C" int portSettingsPlace(const char* tab, const char* label, PortSettingsPlaceInfo* out) {
  const int at = tab != nullptr && label != nullptr ? find(tab, label) : -1;
  out->tab = tab;
  out->group = nullptr;
  out->order = 1 << 20;
  out->isNew = 0;
  if (at < 0) {
    return 0;
  }
  for (int i = at; i >= 0; i--) {
    if (kLayout[i].kind == kTabRow) {
      out->tab = kLayout[i].text;
      break;
    }
    if (kLayout[i].kind == kGroupRow && out->group == nullptr) {
      out->group = kLayout[i].text;
    }
  }
  out->order = at;
  out->isNew = kLayout[at].kind == kNewRow;
  return 1;
}

extern "C" int portSettingsTabOrder(const char** tabs, int max) {
  int n = 0;
  for (const Row& r : kLayout) {
    if (r.kind == kTabRow && n < max) {
      tabs[n++] = r.text;
    }
  }
  return n;
}
