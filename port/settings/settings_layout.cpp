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
//
// A tab can hold pages (PAGE lines after its TAB line: Enhancements' Field,
// Battle...). Each page is a tab of its own on the full page (listed under
// its tab's name) and a button on its tab's own buttons in the Start menu; the
// tab itself has no settings. A setting listed on a page was added to the tab
// the page is under, unless FROM names another.
#include "port/settings.h"
#include <cstdio>
#include <cstring>

namespace {

enum Kind { kTabRow, kPageRow, kGroupRow, kItemRow, kNewRow };

struct Row {
  Kind kind;
  const char* text; // the tab's, page's or group's name, or the setting's label
  const char* from; // a setting's: the tab its feature names, when not this one
};

#define TAB(name) {kTabRow, name, nullptr}
#define PAGE(name) {kPageRow, name, nullptr}
#define GROUP(name) {kGroupRow, name, nullptr}
#define ITEM(label) {kItemRow, label, nullptr}
#define FROM(label, tab) {kItemRow, label, tab}
// new in this version: a "new" marker beside it on the full page
#define NEW(label) {kNewRow, label, nullptr}
#define NEW_FROM(label, tab) {kNewRow, label, tab}
// the Remake tab's: the remake's changes, each added to Enhancements by its feature
#define REMAKE(label) FROM(label, "Enhancements")
#define REMAKE_NEW(label) NEW_FROM(label, "Enhancements")

// The settings first, in the order most games list theirs (the game itself,
// what you see, hear and hold, then the helpers, the language, playing
// together, the saves and the program), then the modes and tools.
const Row kLayout[] = {
    TAB("Gameplay"),
    NEW_FROM("Presets", "Enhancements"), // one choice for many settings (enhancement_presets.cpp)
    GROUP("Difficulty"),
    ITEM("Difficulty"),
    ITEM("Enemy HP (custom)"),
    ITEM("Enemy attack (custom)"),
    NEW("Level sync"),
    GROUP("Speed"),
    ITEM("Text speed"),
    NEW("Fly through text"), // (scene_skip.cpp)
    NEW("Skip any scene"),
    FROM("Faster text outside battle", "Enhancements"),
    FROM("Faster battles", "Enhancements"),
    FROM("Faster battle menus", "Enhancements"),
    ITEM("Run speed"),
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

    // Every change the Switch remake (2024) made that Paper Cut has, here and nowhere else
    // (remake_rules.inc lists their keys): the one switch for them all first, then each on its own.
    TAB("Remake"),
    NEW("Play it like the Switch remake"), // (enhancement_presets.cpp)
    GROUP("Field"), // (field_remake.cpp)
    REMAKE_NEW("Shorter enemy notice range"),
    REMAKE_NEW("Field projectiles hurt"),
    REMAKE_NEW("Pipes with A"),
    REMAKE_NEW("Remake field hammer"),
    REMAKE_NEW("Koops' aim"),
    REMAKE_NEW("Steadier Paper Plane"),
    REMAKE_NEW("Remake Yoshi rides"),
    REMAKE_NEW("Ledge teetering"),
    REMAKE_NEW("Remake: First Strike on spiders"),
    GROUP("Battle"),
    REMAKE("Remake balance"),
    REMAKE_NEW("Guard stances"),
    REMAKE_NEW("FP cost preview"),
    REMAKE_NEW("Grey out Special"),
    REMAKE_NEW("Immunity crossed out"),
    REMAKE_NEW("Spring Jump cue"),
    REMAKE_NEW("Remake: menu waits for throws"),
    REMAKE_NEW("Remake: wait for Star Power"),
    REMAKE_NEW("Remake: hazards pierce"),
    REMAKE_NEW("Remake: deep sleep"),
    REMAKE_NEW("Remake: stagehands"),
    GROUP("Enemies and bosses"),
    REMAKE_NEW("Remake: Bob-omb blasts"),
    REMAKE_NEW("Remake: Buzzies resist blasts"),
    REMAKE_NEW("Remake: Flower Fuzzies"),
    REMAKE_NEW("Remake: Bandit loot"),
    REMAKE_NEW("Remake: Swoopula Swoop"),
    REMAKE_NEW("Remake: still Chain Chomps"),
    REMAKE_NEW("Remake: retattle second fights"),
    REMAKE_NEW("Remake: Doopliss"),
    REMAKE_NEW("Remake: Doopliss's surprise attack"),
    REMAKE_NEW("Remake: Glitz Pit conditions"),
    GROUP("Maps and places"), // (remake_maps.cpp)
    REMAKE_NEW("Warp pipes in the sewers"),
    REMAKE_NEW("The remake's shortcut pipes"),
    REMAKE_NEW("Excess Express skips Riverside"),
    REMAKE_NEW("Chapter shortcuts"),
    REMAKE_NEW("Keelhaul Key and the Grotto"),
    REMAKE_NEW("Jump down the Steeple's stairs"),
    REMAKE_NEW("A shorter hunt for General White"),
    REMAKE_NEW("New Save and Heart Blocks"),
    REMAKE_NEW("New ambush battles"),
    REMAKE_NEW("Charlieton and a Grotto salesman"),
    GROUP("Items and coins"),
    REMAKE("Bag size"),
    REMAKE_NEW("Use healing items when the bag is full"),
    REMAKE_NEW("Coins up to 9,999"),
    REMAKE_NEW("Remake: Sleepy Sheep"),
    REMAKE_NEW("Remake: Power Lift"),
    REMAKE_NEW("Remake: Sweet Treat"),
    REMAKE_NEW("Remake: Mystery"),
    REMAKE_NEW("Remake: lottery prizes"),
    REMAKE_NEW("Remake: Pit shop"),
    REMAKE_NEW("Remake: Trouble rewards"),
    REMAKE_NEW("Remake: boss coins"),
    REMAKE_NEW("Remake: keep coins when fleeing"),
    REMAKE_NEW("Shop improvements"),
    REMAKE_NEW("Hints for coins"),
    GROUP("Quality of life"),
    REMAKE_NEW("Level-ups heal every partner"),
    REMAKE_NEW("Left and Right page through lists"),
    REMAKE_NEW("Save-file details"),
    REMAKE_NEW("Retry in Bowser's stages"),
    REMAKE_NEW("Jukebox tracks"),
    GROUP("Scenes and text"),
    REMAKE("Skip scenes"),
    REMAKE_NEW("Start in Rogueport"),
    REMAKE_NEW("Mail without stopping"),
    REMAKE_NEW("Partner hint"),
    REMAKE_NEW("Prompts over things to check"),

    // Paper Cut's own changes, a page each by what they're about (the Start menu: a button each
    // under Enhancements). A setting here was added to Enhancements: its pages are only its places.
    TAB("Enhancements"),
    PAGE("Field"),
    GROUP("Getting around"),
    ITEM("Fast travel"),
    ITEM("Travel to"),
    NEW_FROM("Area map", "Area map"),
    NEW("Warp to any room"),
    ITEM("Walk-speed boost"),
    ITEM("Boost speed"),
    ITEM("Boost key"),
    GROUP("Saving"),
    ITEM("Save Blocks save at once"),
    ITEM("Storage at every Save Block"),
    GROUP("Collectibles"),
    ITEM("Star Piece and Shine Sprite tracker"),
    ITEM("On this map"),

    PAGE("Battle"),
    GROUP("On screen"),
    ITEM("Battle pop-ups"),
    NEW("Battle grades"),
    NEW("Battle recap"),
    GROUP("Help in battle"),
    ITEM("Tattle without a turn"),
    ITEM("Level up with the D-pad"),
    ITEM("Retry on Game Over"),
    GROUP("Battle rules"), // the players' rule wishes (rules.cpp) and the port's own
    NEW("Balanced badges"),
    NEW("No stage hazards"),
    NEW("No audience throws"),
    NEW("Auto-win weak fights"),
    ITEM("Glitz Pit kindness"),
    GROUP("Rewards"),
    ITEM("Coins from battles"),
    ITEM("Star Points from battles"),
    ITEM("Pit of 100 Trials extras"),

    PAGE("Characters"), // Play as and the partners
    GROUP("Play as"),
    NEW("Play as"),
    NEW("Luigi's higher jump"),
    GROUP("Partners"),
    ITEM("Partner quick-swap"),
    NEW_FROM("Partner wheel", "Controls"),
    ITEM("Partners never at 0 HP"),
    ITEM("Healing items heal every partner"),

    PAGE("Items and badges"),
    GROUP("Items"),
    ITEM("Junk-item helper in shops"),
    NEW("Zess T.'s new dishes"),
    GROUP("Badges"),
    ITEM("Badge loadouts"),
    ITEM("Loadouts"),
    NEW("Paper Cut badge set"),
    NEW("Paper Cut badges"),
    NEW("Badge maker"),

    PAGE("Story"),
    GROUP("Pace"),
    NEW("Skip Peach's and Bowser's interludes"),
    NEW("Glitz Pit and chapter 6 pace trims"),
    GROUP("New Game+"),
    ITEM("New Game+"),
    ITEM("New Game+ from file"),

    PAGE("Lists"), // this file's lists, to read (long: a page of their own)
    ITEM("Journal"),
    NEW("Notes"),
    NEW("Recipe book"),
    NEW("Glitz Pit roster"),

    PAGE("Restorations"), // what the Japanese release had (restorations.cpp; three of postgame_extra.cpp's)
    NEW("Restorations"),
    NEW("Rogueport's chalk outline"),
    NEW("The Parlor Boo's bunny ears"),
    NEW("Japanese screen flashes"),
    NEW("1000 X-Nauts in the prologue"),
    NEW_FROM("Japanese bad ending", "Gameplay"),
    NEW_FROM("Hooktail fears frogs", "Gameplay"),
    NEW_FROM("Japanese recipes", "Gameplay"),

    PAGE("Paper Mario 64"), // with the player's own Paper Mario 64 linked (pm64_link.cpp, pm64_media.cpp)
    NEW("Link Paper Mario 64"),
    NEW("Everything Paper Mario 64"), // the one switch (enhancement_presets.cpp)
    NEW("Paper Mario 64 partners"),
    NEW("Paper Mario 64 partners from the start"),
    NEW("Win Paper Mario partners from their bosses"),
    NEW("Paper Mario 64 boss list"), // (pm64_media.cpp: each boss on its own; the Star Haven corner's board too)
    NEW("Paper Mario 64 bosses: Scale to your level"),
    NEW("Paper Mario field abilities"),
    NEW("Paper Mario 64 enemies"),
    NEW("Paper Mario 64 enemies in the story"),
    NEW("Paper Mario 64 badges"),
    NEW("Tayce T.'s recipes"), // (pm64_media.cpp)
    NEW("Tattle Log: Paper Mario 64"),
    NEW_FROM("Paper Mario 64 Star Spirits", "Cosmetics"),

    TAB("Display"),
    GROUP("Screen"),
    ITEM("Display mode"),
    ITEM("Window"),
    ITEM("Frame rate"),
    ITEM("VSync"),
    ITEM("Variable refresh rate (G-Sync, FreeSync)"),
    NEW_FROM("Battery saver", "Graphics"),
    NEW_FROM("Keep game speed when the GPU falls behind", "Graphics"),
    ITEM("Run at 50 Hz (PAL)"),
    ITEM("Orientation"),
    ITEM("Keep clear of the notch"),
    NEW("Show the system bars"),
    GROUP("Picture shape"),
    ITEM("Widescreen"),
    ITEM("Aspect ratio"),
    ITEM("Widescreen HUD at the edges"),
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
    NEW("Texture pack memory"),
    ITEM("Never skip loading graphics"),
    NEW("Sharp text"),
    GROUP("Light"),
    NEW("Bloom"),
    NEW("Ray-traced lighting"),
    GROUP("Paper looks"),
    NEW("Paper shadows"),
    NEW("Paper physics"),
    NEW("Smooth paper edges"),
    NEW_FROM("Room transitions", "Display"),
    GROUP("Filter"),
    ITEM("Picture filter"),
    GROUP("Stereoscopic 3D"), // (stereo.cpp)
    NEW("Stereoscopic 3D"),
    NEW("3D depth"),
    NEW("Screen distance"),

    TAB("Audio"),
    GROUP("Volume"),
    ITEM("Volume"),
    ITEM("Music"),
    ITEM("Sound effects"),
    ITEM("Ambience"),
    NEW("Talk blips"),
    GROUP("Output"),
    ITEM("Speakers"),
    NEW_FROM("Sound from the DualSense", "Controls"),
    ITEM("Mute in the background"),
    ITEM("Output rate"),
    GROUP("Paper Mario 64"), // with the player's own Paper Mario 64 linked (pm64_media.cpp)
    NEW("Paper Mario sound effects"),
    NEW("Paper Mario sounds for everyday actions"),
    GROUP("Jukebox"),
    FROM("Jukebox", "Enhancements"),
    FROM("Tracks", "Enhancements"),
    GROUP("Music"),
    ITEM("Soundtrack"),
    ITEM("Music folder"),
    GROUP("Battle music"),
    ITEM("Battle music: Rogueport and its underground"),
    ITEM("Battle music: Petal Meadows and Hooktail Castle"),
    ITEM("Battle music: Boggly Woods and the Great Tree"),
    ITEM("Battle music: Glitzville"),
    ITEM("Battle music: Twilight Town and Creepy Steeple"),
    ITEM("Battle music: Keelhaul Key and Pirate's Grotto"),
    ITEM("Battle music: The Excess Express, Riverside and Poshley Heights"),
    ITEM("Battle music: Fahr Outpost, the moon and the X-Naut Fortress"),
    ITEM("Battle music: The Palace of Shadow"),
    ITEM("Battle music: The Pit of 100 Trials"),

    TAB("Controls"),
    GROUP("Controllers"),
    ITEM("Bindings"),
    NEW("Controls at a glance"),
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
    ITEM("Touchpad scrolls menus"),

    TAB("Interface"),
    GROUP("On screen"),
    ITEM("HUD size"),
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
    NEW_FROM("Less screen shake", "Enhancements"),
    NEW_FROM("Less flashing", "Enhancements"),
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
    NEW("Game text packs"),

    TAB("Multiplayer"),
    GROUP("Co-op"),
    ITEM("Co-op (up to 4 players)"),
    ITEM("Players"),
    NEW("Player colours"),
    NEW("Camera"), // (coop_camera.cpp)
    NEW("Brothers mode"),
    NEW("Duo moves"),
    NEW("Dungeon master"),
    NEW("Team bonus"),
    NEW("Idle players join the audience"),
    GROUP("Together"),
    NEW("Co-op pings"),
    NEW("Paper notes"),

    TAB("Saves"),
    GROUP("Saving"),
    FROM("Autosave", "Gameplay"),
    NEW("Keep every save file"),
    NEW("Save before sleep"),
    NEW("Save files"),
    NEW("Co-op copies"), // (save_files.cpp: a guest's copies of the host's adventure)
    NEW("Settings per save file"),
    ITEM("Open the save folder"),
    ITEM("Saves"),
    ITEM("Sync saves through a folder"),
    ITEM("Sync saves with iCloud Drive"),
    NEW("Portable mode"),
    GROUP("Backups"),
    ITEM("Backups kept"),
    ITEM("Restore a backup"),
    NEW("Back up everything"),
    GROUP("Other devices"),
    ITEM("Send saves to another device"),
    ITEM("Receive saves"),
    ITEM("Copy a save to another release"),
    ITEM("A copy for Dolphin or a GameCube"),
    NEW("Import saves"),
    NEW("Export saves"),

    TAB("General"),
    GROUP("Starting"),
    FROM("Skip the intro", "Gameplay"),
    ITEM("Skip the launcher"),
    ITEM("Quick resume"),
    NEW("Title menu"),
    NEW("Pop-up launcher"),
    ITEM("Launcher look"),
    ITEM("Launcher sounds"),
    NEW("Back to the launcher"), // from the game (launcher_back.cpp; port/include/port/launcher_back.h)
    NEW("Online room"),          // Leave the room from anywhere (online_leave.cpp)
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
    NEW("Tips"), // Show tips again (launcher_onboard.cpp; port/include/port/tips.h)
    GROUP("More"),
    ITEM("Settings presets"),
    NEW("Add to Steam"),
    ITEM("Discord status"),
    ITEM("Crash reports"),
    NEW("Crash notice at start"),

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
    NEW("Paper Mario 64 bosses"), // (pm64_media.cpp)
    NEW("Paper Mario 64 bosses in the story"), // (pm64_media.cpp: each in Rogueport, pm64_story_aurora.c)
    NEW("Paper Mario 64 bosses play their own music"), // (pm64_media.cpp)
    ITEM("Endless Glitz Pit"),
    NEW("Battle puzzles"),
    NEW("Action-command dojo"),
    NEW("Leaderboards"),
    GROUP("After the story"),
    NEW("Chapter select"),
    NEW("The Paper Tower"),
    NEW("Paper Tower seed"),
    NEW("Trophy room"),
    NEW("Gold Medal"), // (postgame_extra.cpp)
    NEW("Gallery"),
    NEW("Whacka in the Pit"),
    NEW("Pit Mover"),
    NEW("Exhibition matches"),
    NEW("Second-fight Tattles"),
    NEW("Boo quiz in Creepy Steeple"),
    NEW("Newsletter Trouble"),
    NEW("Crane game prizes"),
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
    NEW("Cool Mode"),
    NEW("Moon gravity"),
    NEW("Paper-thin from every side"),
    NEW("Luigi's voice for Mario"),
    NEW("Spinning paper"),
    NEW("Chaos mode"),
    NEW("Buy Pianta tokens"), // (postgame_extra.cpp)
    NEW("Map editor"), // (map_editor.cpp)
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
    NEW("Warp to a room"), // (practice_more.cpp)
    NEW("Jump to chapter"), // (practice.cpp)
    ITEM("Replays"),
    NEW("Trick practice"),
    GROUP("Tools"),
    ITEM("Frame advance keys"),
    ITEM("Frame advance"),
    ITEM("Fixed RNG"),
    ITEM("RNG"),
    ITEM("Stats"),
    ITEM("Items"),
    NEW("Badges in battle"), // (practice_more.cpp)
    NEW("Party"),
    ITEM("Story and flags"),
    ITEM("Memory watch corner"),
    ITEM("Memory watch"),

    TAB("Cosmetics"),
    ITEM("Theme"),
    ITEM("Looks and colour picker"),
    GROUP("Play as"), // the colours (cosmetics.cpp)
    ITEM("Mario's cap and shirt"),
    ITEM("Mario's overalls"),
    ITEM("Luigi's cap and shirt"),
    ITEM("Luigi's overalls"),
    ITEM("Peach's dress"),
    ITEM("Peach's crown"),
    ITEM("Peach's hair"),
    ITEM("Peach's gloves"),
    ITEM("Peach's earrings"),
    ITEM("Peach's shoes"),
    ITEM("Bowser's shell"),
    ITEM("Bowser's hair"),
    ITEM("Bowser's skin"),
    ITEM("Bowser's horns and spikes"),
    ITEM("Bowser's bracelets"),
    GROUP("Partners"),
    ITEM("Goombella's hair"),
    ITEM("Goombella's helmet"),
    ITEM("Koops's shell"),
    ITEM("Koops's hoodie"),
    ITEM("Flurrie's hair"),
    ITEM("Flurrie's dress"),
    ITEM("Bobbery's body"),
    ITEM("Bobbery's fuse"),
    ITEM("Ms. Mowz's mask"),
    ITEM("Ms. Mowz's cape"),
    ITEM("Vivian's body"),
    ITEM("Vivian's hat"),
    ITEM("Vivian's hair"),
    GROUP("On screen"),
    ITEM("HP banner"),
    ITEM("FP banner"),
    ITEM("Star Power banner"),
    ITEM("Menus"),

    TAB("Randomizer"),
    ITEM("Built-in randomizer"),

    TAB("Archipelago"),
    ITEM("Archipelago"),

    TAB("Achievements"),
    ITEM("Achievements"),
    NEW("This file"), // 0.5.0: grouped by category, with icons and progress bars

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

constexpr int kRows = static_cast<int>(sizeof(kLayout) / sizeof(kLayout[0]));

// the row of the setting (tab, label) names, or -1. On a page, a setting with no FROM was
// added to the page's tab (or names the page itself).
int find(const char* tab, const char* label) {
  const char* in = nullptr;   // the tab listed last
  const char* page = nullptr; // and its page, if any
  for (int i = 0; i < kRows; i++) {
    const Row& r = kLayout[i];
    if (r.kind == kTabRow) {
      in = r.text;
      page = nullptr;
    } else if (r.kind == kPageRow) {
      page = r.text;
    } else if (r.kind != kGroupRow && std::strcmp(r.text, label) == 0 &&
               (r.from != nullptr ? std::strcmp(r.from, tab) == 0
                                  : std::strcmp(in, tab) == 0 || (page != nullptr && std::strcmp(page, tab) == 0))) {
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
    if (kLayout[i].kind == kTabRow || kLayout[i].kind == kPageRow) {
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
  for (int i = 0; i < kRows; i++) {
    const Row& r = kLayout[i];
    const bool hasPages = r.kind == kTabRow && i + 1 < kRows && kLayout[i + 1].kind == kPageRow;
    if (((r.kind == kTabRow && !hasPages) || r.kind == kPageRow) && n < max) {
      tabs[n++] = r.text;
    }
  }
  return n;
}

extern "C" const char* portSettingsTabParent(const char* tab) {
  const char* in = nullptr;
  for (const Row& r : kLayout) {
    if (r.kind == kTabRow) {
      in = r.text;
    } else if (r.kind == kPageRow && tab != nullptr && std::strcmp(r.text, tab) == 0) {
      return in;
    }
  }
  return nullptr;
}
