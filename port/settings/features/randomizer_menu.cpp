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

#include <cstdio>
#include <cstring>
#include <string>

namespace {

PortRandoSettings* settings() { return portRandoSettings(portSettingsUserPath()); }

// the on/off rows, in their order (ids from kToggleBase)
struct Toggle {
  const char* label;
  const char* note;
  int PortRandoSettings::*field;
  bool (*shown)(const PortRandoSettings& s); // nullptr: always
};
bool noOpenWorld(const PortRandoSettings& s) { return s.openWorld == 0; }
const Toggle kToggles[] = {
    {"Badges", "Badges move among the places the game hands them out.", &PortRandoSettings::badges, nullptr},
    {"Items", "Items nothing needs move among their places.", &PortRandoSettings::items, nullptr},
    {"Star Pieces", "Star Pieces move among their places.", &PortRandoSettings::starPieces, nullptr},
    {"Shine Sprites", "Shine Sprites move among their places.", &PortRandoSettings::shineSprites, nullptr},
    {"Paper Cut's open world",
     "Chapters 1 to 8 in any order: key items and partners move too, placed so the goal's Crystal Stars can be "
     "reached. Each new file started while it is on takes the seed.",
     &PortRandoSettings::openWorld, nullptr},
    {"Key items (needs the TTYD AP mod)",
     "Key items, partners, hammer and boots move too, by the TTYD apworld's logic: the TTYD AP mod must be installed "
     "as a code mod, or the game can't be finished.",
     &PortRandoSettings::keyItems, noOpenWorld},
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
  kToggleBase = 100,
};

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
  if (!portRandoSupported()) {
    add(portPauseRow("The randomizer: the US version only",
                     "It knows where the items are in the US version only, so it is off on this one.", nullptr,
                     kNotHere, PORT_PAUSE_ROW_DIM));
    return n;
  }
  const PortRandoSettings& s = *settings();
  if (portApSeedPath()[0] != '\0') {
    add(portPauseRow("Use the built-in randomizer instead",
                     "An Archipelago seed is chosen on the Archipelago tab; it is used instead of this one.", nullptr,
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
    PortPauseRow r = portPauseRow("Seed", "A: type a seed. Any text; empty: a random one is made at the start.",
                                  s.seed[0] == '\0' ? "(random)" : nullptr, kSeed);
    plainCopy(r.value, sizeof(r.value), s.seed);
    add(r);
  }
  add(portPauseRow("New seed", "A: a new random seed.", nullptr, kNewSeed));
  add(portPauseRow("What is shuffled", nullptr, nullptr, 0, PORT_PAUSE_ROW_HEADING));
  for (int i = 0; i < kToggleCount; i++) {
    const Toggle& t = kToggles[i];
    if (t.shown == nullptr || t.shown(s)) {
      add(portPauseRow(t.label, t.note, onOff(s.*(t.field)), kToggleBase + i));
    }
  }
  {
    PortPauseRow r = portPauseRow("Check this seed", "A: the seed made now, without playing it: what it would do.",
                                  nullptr, kCheck);
    plainCopy(r.value, sizeof(r.value), sCheck.c_str());
    add(r);
  }
  return n;
}

void seedTyped(const char* text) {
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
  if (!portRandoSupported()) {
    return "It knows where the items are in the US version only, so it is off on this one.";
  }
  PortRandoSettings* s = settings();
  if (id >= kToggleBase && id < kToggleBase + kToggleCount) {
    int& v = s->*(kToggles[id - kToggleBase].field);
    v = !v;
    return changed();
  }
  switch (id) {
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
  default: return nullptr;
  }
}

const PortPauseForm kForm = {"Randomizer", "Built-in randomizer", rows, act};

const bool sAdded = [] {
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace
