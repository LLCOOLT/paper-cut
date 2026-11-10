// The built-in randomizer in the Start menu (port/include/port/pause_forms.h):
// the full page's "Built-in randomizer" section (randomizer.cpp) as a form on
// the Settings tab's Randomizer category, in the game's windows and font. The
// same settings (port/archipelago/ttyd_rando.h, <user>/randomizer.json): turn
// it on, the seed (typed on the text page, or a new one), what is shuffled,
// the spoiler log, and "Check this seed".
//
// Adding a row: an on/off is one line in kToggles (its field in
// PortRandoSettings, and when it is shown); anything else gets an id in the
// enum and a case in rows() and act().
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/companion_menu.h"
#include "../../archipelago/ttyd_ap.h"
#include "../../archipelago/ttyd_rando.h"
#include "../../archipelago/rando_gen.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

PortRandoSettings* settings() { return portRandoSettings(portSettingsUserPath()); }

// the on/off rows, in their order (ids from kToggleBase)
struct Toggle {
  const char* label;
  const char* note;
  int PortRandoSettings::*field;
  bool (*shown)(const PortRandoSettings& s); // nullptr: always
};
bool noOpenWorld(const PortRandoSettings& s) { return s.openWorld == 0 && portRandoApWorldSupported(); }
bool openWorldOn(const PortRandoSettings& s) { return s.openWorld != 0; }
const Toggle kToggles[] = {
    {"Badges", "Badges move among the places the game hands them out.", &PortRandoSettings::badges, nullptr},
    {"Items", "Items nothing needs move among their places.", &PortRandoSettings::items, nullptr},
    {"Star Pieces", "Star Pieces move among their places.", &PortRandoSettings::starPieces, nullptr},
    {"Shine Sprites", "Shine Sprites move among their places.", &PortRandoSettings::shineSprites, nullptr},
    {"Trouble rewards", "The troubles that hand over an item take part.", &PortRandoSettings::troubles, nullptr},
    {"Shop stock", "The shops' items swap shelves, each at its own price.", &PortRandoSettings::shops, nullptr},
    {"Mystery items", "An item lying in a place shows as a Mystery until picked up.", &PortRandoSettings::mystery,
     nullptr},
    {"Paper Cut's open world",
     "Chapters 1 to 8 in any order: key items and partners move too, placed so the goal's Crystal Stars can be "
     "reached. Each new file started while it is on takes the seed.",
     &PortRandoSettings::openWorld, nullptr},
    {"Shuffle the chapter gates",
     "The sewers' four chapter pipes lead to one another's chapters, both ways, by the seed.",
     &PortRandoSettings::gates, openWorldOn},
    {"Shuffle the room doors", "A town's or a field's doors lead to other rooms of the same area, both ways.",
     &PortRandoSettings::rooms, openWorldOn},
    {"Mix the ways between areas", "The doors from one area into the next lead into one another's areas.",
     &PortRandoSettings::mixWays, openWorldOn},
    {"Shuffle the blimp, the ship and the train", "Each takes Mario to another's destination.",
     &PortRandoSettings::transports, openWorldOn},
    {"Key items (needs the TTYD AP mod)",
     "Key items, partners, hammer and boots move too, by the TTYD apworld's logic: the TTYD AP mod must be installed "
     "as a code mod, or the game can't be finished.",
     &PortRandoSettings::keyItems, noOpenWorld},
    {"Music shuffle", "The areas' themes swap among themselves, the battle themes among theirs.",
     &PortRandoSettings::music, nullptr},
    {"Sound shuffle", "Each sound effect plays another of its own family.", &PortRandoSettings::sounds, nullptr},
    {"Spoiler log", "At the start: where every item went, in the randomizer folder in the user folder.",
     &PortRandoSettings::spoiler, nullptr},
};
constexpr int kToggleCount = sizeof(kToggles) / sizeof(kToggles[0]);

enum {
  kNotHere = 1, // the dimmed row: not on this version
  kUseBuiltIn,  // an Archipelago seed is chosen: back to this one
  kThisRun,
  kEnabled,
  kSeed,
  kNewSeed,
  kCheck,
  kGoal, // the open world's goal: its Crystal Stars (as the full page's slider)
  kLogic,     // glitchless, tricks, no logic (left and right; A goes round)
  kPreset,    // the presets (left and right choose, A applies)
  kPermalink, // the permalink shown; A types one in
  kTrickBase = 200,
  kToggleBase = 100,
};

int sPreset = 0; // the preset row's choice
const char* const kLevels[] = {"Glitchless", "Tricks", "No logic"};

std::string sCheck; // the last "Check this seed"

// text for the game's font as it is: plain letters (a '<' would start a tag)
void plainCopy(char* out, size_t size, const char* in) {
  size_t n = 0;
  for (const char* p = in; p != nullptr && *p != '\0' && n + 1 < size; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    out[n++] = c >= 0x20 && c < 0x7F && c != '<' && c != '>' ? static_cast<char>(c) : '?';
  }
  out[n] = '\0';
}

const char* onOff(int v) { return v ? "On" : "Off"; }

int rows(PortPauseRow* out, int max) {
  int n = 0;
  auto add = [&](const PortPauseRow& r) {
    if (n < max) {
      out[n++] = r;
    }
  };
  const PortRandoSettings& s = *settings();
  if (portApSeedPath()[0] != '\0') {
    add(portPauseRow("Use the built-in randomizer instead",
                     "An Archipelago seed is chosen; {A} uses the built-in one.", nullptr,
                     kUseBuiltIn));
  }
  {
    const char* active = portRandoActive();
    PortPauseRow r = portPauseRow("This run", "What this run's game was given.", active[0] == '\0' ? "Not randomized" : nullptr,
                                  kThisRun);
    plainCopy(r.value, sizeof(r.value), active);
    add(r);
  }
  add(portPauseRow("Randomize from the next start",
                   "Every file played while it is on gets this seed's items: start a new file for it, and turn it "
                   "off again for your other files.",
                   onOff(s.enabled), kEnabled));
  {
    PortPauseRow r = portPauseRow("Seed", "{A}: type a seed (any text). Empty: a random one at start.",
                                  s.seed[0] == '\0' ? "(random)" : nullptr, kSeed);
    plainCopy(r.value, sizeof(r.value), s.seed);
    add(r);
  }
  add(portPauseRow("New seed", "{A}: a new random seed.", nullptr, kNewSeed));
  add(portPauseRow("What is shuffled", nullptr, nullptr, 0, PORT_PAUSE_ROW_HEADING));
  for (int i = 0; i < kToggleCount; i++) {
    const Toggle& t = kToggles[i];
    if (t.shown == nullptr || t.shown(s)) {
      add(portPauseRow(t.label, t.note, onOff(s.*(t.field)), kToggleBase + i));
    }
    if (t.field == &PortRandoSettings::openWorld && s.openWorld) {
      PortPauseRow r = portPauseRow("Crystal Stars for the goal",
                                    "The goal: this many Crystal Stars, from any chapters. The logic makes sure that "
                                    "many can be reached; a notice says when they are. The others may need items "
                                    "that lie past the goal.",
                                    nullptr, kGoal);
      std::snprintf(r.value, sizeof(r.value), "%d", s.openGoal > 0 && s.openGoal < 7 ? s.openGoal : 7);
      add(r);
    }
  }
  if (s.openWorld || (s.keyItems && portRandoApWorldSupported())) {
    add(portPauseRow("Logic", nullptr, nullptr, 0, PORT_PAUSE_ROW_HEADING));
    const int level = s.logicLevel >= 0 && s.logicLevel <= 2 ? s.logicLevel : 0;
    add(portPauseRow("Logic",
                     "Glitchless: every item where the game can be played to it. Tricks: the chosen tricks count too. "
                     "No logic: items anywhere; a seed may not be finishable.",
                     kLevels[level], kLogic));
    if (level == 1) {
      const std::vector<ttyd_rando::TrickInfo> tricks = ttyd_rando::tricks();
      for (size_t i = 0; i < tricks.size(); i++) {
        add(portPauseRow(tricks[i].name, "Counted on by the logic when on (Paper Cut's open world).",
                         onOff((s.tricks & int(tricks[i].bit)) != 0), kTrickBase + int(i)));
      }
    }
  }
  {
    PortPauseRow r = portPauseRow("Check this seed", "{A}: the seed made now, without playing it: what it would do.",
                                  nullptr, kCheck);
    plainCopy(r.value, sizeof(r.value), sCheck.c_str());
    add(r);
  }
  add(portPauseRow("Presets", nullptr, nullptr, 0, PORT_PAUSE_ROW_HEADING));
  {
    if (sPreset >= portRandoPresetCount()) sPreset = 0;
    PortPauseRow r = portPauseRow("Preset", "Left and right choose a preset; {A} uses it (the seed stays).",
                                  portRandoPresetBuiltIn(sPreset) ? portRandoPresetName(sPreset) : nullptr, kPreset);
    if (!portRandoPresetBuiltIn(sPreset)) plainCopy(r.value, sizeof(r.value), portRandoPresetName(sPreset));
    add(r);
  }
  {
    PortPauseRow r = portPauseRow("Permalink", "The seed and every choice in one text. {A}: type one in.", nullptr,
                                  kPermalink);
    plainCopy(r.value, sizeof(r.value), portRandoPermalink());
    add(r);
  }
  return n;
}

void permalinkTyped(const char* text) {
  const char* why = portRandoApplyPermalink(text);
  sCheck = why != nullptr ? why : "";
}

void seedTyped(const char* text) {
  if (portRandoShared()) return; // (the session's: the host's, see act)
  PortRandoSettings* s = settings();
  std::snprintf(s->seed, sizeof(s->seed), "%s", text != nullptr ? text : "");
  sCheck.clear();
  portRandoSaveSettings();
}

const char* changed() {
  sCheck.clear();
  portRandoSaveSettings();
  return "Changes apply the next time the game starts.";
}

const char* act(int id, int dir) {
  PortRandoSettings* s = settings();
  // Online (and in a replay) the settings are the session's, the host's: a
  // change here would stamp another seed on this side's next new file. Only
  // looking stays (the presets' notes, the permalink, "Check this seed").
  const bool looks = id == kCheck || id == kThisRun || id == kUseBuiltIn || id == kPermalink ||
                     (id == kPreset && dir != 0);
  if (portRandoShared() && !looks) return "The session's settings can't be changed here.";
  if (id >= kToggleBase && id < kToggleBase + kToggleCount) {
    int& v = s->*(kToggles[id - kToggleBase].field);
    v = !v;
    return changed();
  }
  if (id >= kTrickBase) {
    const std::vector<ttyd_rando::TrickInfo> tricks = ttyd_rando::tricks();
    const size_t i = size_t(id - kTrickBase);
    if (i < tricks.size()) {
      s->tricks ^= int(tricks[i].bit);
      return changed();
    }
    return nullptr;
  }
  switch (id) {
  case kLogic: {
    const int level = s->logicLevel >= 0 && s->logicLevel <= 2 ? s->logicLevel : 0;
    s->logicLevel = dir < 0 ? (level + 2) % 3 : (level + 1) % 3;
    return changed();
  }
  case kPreset: {
    const int count = portRandoPresetCount();
    if (dir != 0) {
      sPreset = (sPreset + (dir < 0 ? count - 1 : 1)) % count;
      return portRandoPresetNote(sPreset)[0] != '\0' ? portRandoPresetNote(sPreset) : nullptr;
    }
    portRandoApplyPreset(sPreset);
    return changed();
  }
  case kPermalink:
    if (dir == 0) {
      portPauseTextOpen("Permalink", portRandoPermalink(), 90, PORT_PAUSE_CHARS_NAME, permalinkTyped);
    }
    return nullptr;
  case kUseBuiltIn:
    if (dir == 0) {
      portApSetSeed("");
      return "The built-in randomizer's seed is used from the next start.";
    }
    return nullptr;
  case kEnabled:
    s->enabled = !s->enabled;
    return changed();
  case kSeed:
    if (dir == 0) {
      portPauseTextOpen("Seed", s->seed, static_cast<int>(sizeof(s->seed)) - 1, PORT_PAUSE_CHARS_NAME, seedTyped);
    }
    return nullptr;
  case kNewSeed:
    if (dir == 0) {
      portRandoNewSeed();
      sCheck.clear();
      return "A new seed: from the next start.";
    }
    return nullptr;
  case kCheck:
    if (dir == 0) {
      sCheck = portRandoCheck();
    }
    return nullptr;
  case kGoal: {
    // 1 to 5 (left and right; A goes round), 5 kept as 0: all the chapters
    const int goal = s->openGoal > 0 && s->openGoal < 7 ? s->openGoal : 7;
    const int next = dir < 0 ? (goal > 1 ? goal - 1 : 1) : dir > 0 ? (goal < 7 ? goal + 1 : 7) : goal % 7 + 1;
    s->openGoal = next >= 7 ? 0 : next;
    return changed();
  }
  default: return nullptr;
  }
}

const PortPauseForm kForm = {"Randomizer", "Built-in randomizer", rows, act, PORT_PAUSE_FORM_OWN};

const bool sAdded = [] {
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
