// Settings presets (General > Settings presets): this player's setup as a code
// to share (copied, pasted) or a file (saved, opened), and someone else's put
// in place. A preset holds the settings that differ from the defaults among
// those that change the game: gameplay, challenges, accessibility, cosmetics,
// mirror mode... not this device's own (its window and graphics, its keys and
// controllers, its paths, online and sync). Putting one in place first puts
// those settings back to their defaults, so the result is the sharer's setup.
//
//   PCSET1:<base64url of "key=value;key=value...">
#include "port/settings.h"
#include "port/pause_forms.h"
#include "port/tr.h"
#include "../device_keys.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr const char* kPrefix = "PCSET1:";

// this device's own (device_keys.h), and the intro and PAL's 50 Hz: never in a preset, never changed by one
bool device(const std::string& key) {
  return portdevice::owns(key) || key.compare(0, 10, "skip_intro") == 0 || key.compare(0, 8, "pal_50hz") == 0;
}

// settings.txt as it is now: key -> value
std::map<std::string, std::string> current() {
  std::map<std::string, std::string> out;
  portSettingsSave();
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile((std::string(portSettingsUserPath()) + "/settings.txt").c_str(), &size));
  if (text == nullptr) {
    return out;
  }
  std::string all(text, size);
  SDL_free(text);
  size_t at = 0;
  while (at < all.size()) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) {
      end = all.size();
    }
    const std::string line = all.substr(at, end - at);
    at = end + 1;
    const size_t eq = line.find('=');
    if (eq != std::string::npos) {
      out[line.substr(0, eq)] = line.substr(eq + 1);
    }
  }
  return out;
}

// --- base64url, no padding ---
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode(const std::string& in) {
  std::string out;
  unsigned v = 0;
  int bits = 0;
  for (unsigned char c : in) {
    v = (v << 8) | c;
    bits += 8;
    while (bits >= 6) {
      out += kB64[(v >> (bits - 6)) & 63];
      bits -= 6;
    }
  }
  if (bits > 0) {
    out += kB64[(v << (6 - bits)) & 63];
  }
  return out;
}

bool decode(const std::string& in, std::string& out) {
  unsigned v = 0;
  int bits = 0;
  out.clear();
  for (char c : in) {
    const char* at = std::strchr(kB64, c);
    if (at == nullptr || c == '\0') {
      return false;
    }
    v = (v << 6) | unsigned(at - kB64);
    bits += 6;
    if (bits >= 8) {
      out += char((v >> (bits - 8)) & 0xFF);
      bits -= 8;
    }
  }
  return true;
}

// this player's preset: the game's settings that differ from their defaults
std::string makeCode() {
  std::string body;
  for (const auto& [key, value] : current()) {
    int def = 0;
    if (device(key) || !portSettingsDefault(key.c_str(), &def) || std::atoi(value.c_str()) == def) {
      continue;
    }
    body += (body.empty() ? "" : ";") + key + "=" + value;
  }
  return std::string(kPrefix) + encode(body);
}

// someone's preset put in place: the game's settings back to their defaults, then the preset's.
// The count it changed, or -1 when it isn't a preset.
int apply(const std::string& typed) {
  std::string code;
  for (char c : typed) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      code += c;
    }
  }
  const size_t start = code.find(kPrefix);
  std::string body;
  if (start == std::string::npos || !decode(code.substr(start + std::strlen(kPrefix)), body)) {
    return -1;
  }
  std::map<std::string, std::string> wanted;
  size_t at = 0;
  while (at < body.size()) {
    size_t end = body.find(';', at);
    if (end == std::string::npos) {
      end = body.size();
    }
    const std::string pair = body.substr(at, end - at);
    at = end + 1;
    const size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0) {
      return -1;
    }
    const std::string key = pair.substr(0, eq), value = pair.substr(eq + 1);
    if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos) {
      return -1; // numbers only: every preset setting is one
    }
    if (!device(key)) {
      wanted[key] = value;
    }
  }
  int changed = 0;
  for (const auto& [key, value] : current()) {
    int def = 0;
    if (device(key) || !portSettingsDefault(key.c_str(), &def)) {
      continue;
    }
    const auto w = wanted.find(key);
    const std::string to = w != wanted.end() ? w->second : std::to_string(def);
    if (to != value) {
      portSettingsSetKey(key.c_str(), to.c_str());
      changed++;
    }
  }
  portSettingsSave(); // (and applied)
  return changed;
}

// --- the page ---

char sTyped[4096];
std::string sMessage;
std::mutex sFileLock;
std::string sSavePath, sOpenPath; // from the file dialogs' threads
std::atomic<bool> sPending{false};

void saveChosen(void*, const char* const* files, int) {
  if (files != nullptr && files[0] != nullptr) {
    std::lock_guard<std::mutex> lock(sFileLock);
    sSavePath = files[0];
    sPending = true;
  }
}

void openChosen(void*, const char* const* files, int) {
  if (files != nullptr && files[0] != nullptr) {
    std::lock_guard<std::mutex> lock(sFileLock);
    sOpenPath = files[0];
    sPending = true;
  }
}

void report(int changed) {
  if (changed < 0) {
    sMessage = portTr("That isn't a settings preset.");
  } else {
    char text[160];
    std::snprintf(text, sizeof(text), portTr("Preset put in place: %d settings changed. Some apply from the next start."),
                  changed);
    sMessage = text;
  }
}

// the dialogs' results, on the page's own thread
void fileWork() {
  if (!sPending) {
    return;
  }
  std::string save, open;
  {
    std::lock_guard<std::mutex> lock(sFileLock);
    save.swap(sSavePath);
    open.swap(sOpenPath);
    sPending = false;
  }
  if (!save.empty()) {
    const std::string text = "# Paper Cut settings preset: General > Settings presets > Open a preset file\n" + makeCode() +
                             "\n";
    sMessage = SDL_SaveFile(save.c_str(), text.data(), text.size()) ? portTr("Preset saved.")
                                                                    : portTr("The preset couldn't be saved there.");
  }
  if (!open.empty()) {
    size_t size = 0;
    char* text = static_cast<char*>(SDL_LoadFile(open.c_str(), &size));
    if (text == nullptr) {
      sMessage = portTr("That file couldn't be read.");
    } else {
      report(apply(std::string(text, size)));
      SDL_free(text);
    }
  }
}

void section(int inGame) {
  (void)inGame;
  fileWork();
  if (ImGui::Button(portTrLabel("Copy this setup's code"))) {
    SDL_SetClipboardText(makeCode().c_str());
    sMessage = portTr("Code copied: paste it wherever you share it.");
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Save as a file..."))) {
    static SDL_DialogFileFilter kFilter[] = {{nullptr, "txt"}};
    kFilter[0].name = portTr("Paper Cut settings preset"); // (kept until the dialog answers)
    SDL_ShowSaveFileDialog(saveChosen, nullptr, SDL_GetKeyboardFocus(), kFilter, 1, "paper-cut-preset.txt");
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open a preset file..."))) {
    static SDL_DialogFileFilter kFilter[] = {{nullptr, "txt"}};
    kFilter[0].name = portTr("Paper Cut settings preset");
    SDL_ShowOpenFileDialog(openChosen, nullptr, SDL_GetKeyboardFocus(), kFilter, 1, nullptr, false);
  }
#endif
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  ImGui::InputText(portTrLabel("A preset's code##preset_code"), sTyped, sizeof(sTyped));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Paste##preset_paste"))) {
    if (char* clip = SDL_GetClipboardText()) {
      std::snprintf(sTyped, sizeof(sTyped), "%s", clip);
      SDL_free(clip);
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(sTyped[0] == '\0');
  if (ImGui::Button(portTrLabel("Put in place##preset_apply"))) {
    report(apply(sTyped));
  }
  ImGui::EndDisabled();
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// The Start menu's form (port/include/port/pause_forms.h): the code copied, a copied one put in place
int formRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  out[0] = PortPauseRow{"Copy this setup's code", "Your settings as a code, copied to paste wherever you share it.",
                        {}, {}, nullptr, 0, 0};
  out[1] = PortPauseRow{"Put the copied code in place",
                        "Someone's preset code, copied: the settings that change the game put in place.", {}, {},
                        nullptr, 1, PORT_PAUSE_ROW_CONFIRM};
  return 2;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id == 0) {
    SDL_SetClipboardText(makeCode().c_str());
    return "Code copied: paste it wherever you share it.";
  }
  std::string clip;
  if (char* text = SDL_GetClipboardText()) {
    clip = text;
    SDL_free(text);
  }
  const int changed = apply(clip);
  report(changed);
  return changed < 0 ? "That isn't a settings preset." : "Preset put in place. Some settings apply from the next start.";
}

const PortPauseForm kForm = {"General", "Settings presets", formRows, formAct, PORT_PAUSE_FORM_OWN};

const bool sAdded = [] {
  portSettingsAddSection("General", "Settings presets",
                         "Your setup as a code or a file to share, or someone else's put in place: the settings that "
                         "change the game (gameplay, challenges, accessibility, cosmetics, mirror mode...), not this "
                         "device's own (window, graphics, keys, controllers).",
                         0, section, "preset presets share code export import setup file copy paste");
  portPauseAddForm(&kForm);
  return true;
}();

} // namespace

// Tests ($TTYD_PRESET_TEST=<file>): this setup's code written to the file, then the
// code in $TTYD_PRESET_APPLY put in place (main_aurora.c doesn't call it: the
// first settings page frame does, via the overlay below)
namespace {
void testOverlay(int pageOpen) {
  (void)pageOpen;
  static bool done;
  if (done) {
    return;
  }
  done = true;
  if (const char* out = SDL_getenv("TTYD_PRESET_TEST")) {
    const std::string code = makeCode() + "\n";
    SDL_SaveFile(out, code.data(), code.size());
  }
  if (const char* in = SDL_getenv("TTYD_PRESET_APPLY")) {
    std::printf("[presets] put in place: %d settings changed\n", apply(in));
    std::fflush(stdout);
  }
}
const bool sTest = [] {
  if (SDL_getenv("TTYD_PRESET_TEST") != nullptr || SDL_getenv("TTYD_PRESET_APPLY") != nullptr) {
    portSettingsAddOverlay(testOverlay, nullptr);
  }
  return true;
}();
} // namespace
