// Language > Game text packs (docs/DELOCALIZATION.md, docs/MODDING.md
// "messages/"): writes the open disc's own messages into the player's mods
// folder as a text pack's starting point, mods/text-pack-template/: a
// mod.json and messages/<LANG>/<file>.txt for every message file of every
// language this build shows, in the [tag] format mods' messages/ use (UTF-8;
// the JP build's in Shift-JIS, as its packs must be). The pack is turned off
// in the mod manager until the player turns it on, so a template left as it
// is changes nothing. Read from the player's disc at their request, written
// to their own folder; nothing of it is shipped.
#include "port/mods.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/text_language.h"
#include "port/tr.h"
#include "../settings_ui.h"
#include <SDL3/SDL.h>
#include <dolphin/dvd.h>
#include <imgui.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern "C" int portThreadIsMain(void); // os_thread_pc.c (port/platform.h, C only; BOOL is an int)

namespace {

constexpr const char* kFolder = "text-pack-template";

std::string sResult;
// the last result in English, for the Start menu's row (its help box; sResult is the full page's)
std::atomic<const char*> sResultEn{nullptr};

bool isJp() {
  return std::strcmp(PORT_GAME_ID, "G8MJ01") == 0;
}

// the message folders this build shows, and the pack folder each goes in (messages/<LANG>)
std::vector<std::pair<std::string, std::string>> folders() {
  if (isJp()) {
    return {{"/msg", "JP"}, {"/emsg", "EN"}};
  }
  if (std::strcmp(PORT_GAME_ID, "G8MP01") == 0) {
    return {{"/msg/US", "US"}, {"/msg/DE", "DE"}, {"/msg/FR", "FR"}, {"/msg/SP", "SP"}, {"/msg/IT", "IT"}};
  }
  // the US build: English, or the European disc's language in its place (Language > Text language)
  const char* chosen = portTextLanguageDir();
  return {{"/msg/US", chosen != nullptr ? chosen : "US"}};
}

// the game's Latin-1 as UTF-8 (the JP build's Shift-JIS stays as it is)
std::string toUtf8(const std::string& s) {
  if (isJp()) {
    return s;
  }
  std::string out;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x80) {
      out += ch;
    } else {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// one message file (tag, text, tag, text... each ending in a zero byte; an empty tag ends it) as a pack file
std::string packFile(const std::vector<uint8_t>& file, const std::string& name, int* count) {
  std::string out = "Messages of the game's " + name + ", from your disc: the [tag] format of docs/MODDING.md.\n"
                    "Keep only the messages you change (and their [tag] lines); a text you leave as it is\n"
                    "changes nothing, but a smaller pack is easier to share and to merge.\n\n";
  const char* p = reinterpret_cast<const char*>(file.data());
  const char* end = p + file.size();
  while (p < end && *p != '\0') {
    const char* text = static_cast<const char*>(std::memchr(p, '\0', end - p));
    if (text == nullptr || ++text >= end) {
      break;
    }
    const char* next = static_cast<const char*>(std::memchr(text, '\0', end - text));
    if (next == nullptr) {
      break;
    }
    out += "[" + std::string(p) + "]\n" + toUtf8(std::string(text, next)) + "\n";
    (*count)++;
    p = next + 1;
  }
  return out;
}

bool writeFile(const std::string& path, const std::string& text) {
  SDL_CreateDirectory(path.substr(0, path.rfind('/')).c_str());
  return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

// the dump; what it did, for the page
std::string dump() {
  const std::string root = std::string(portSettingsUserPath()) + "/mods/" + kFolder;
  int files = 0, messages = 0, failed = 0;
  for (const auto& [dir, lang] : folders()) {
    DVDDir d;
    DVDDirEntry e;
    if (!DVDOpenDir(dir.c_str(), &d)) {
      continue;
    }
    while (DVDReadDir(&d, &e)) {
      DVDFileInfo info;
      if (e.isDir || e.name == nullptr || !DVDOpen((dir + "/" + e.name).c_str(), &info)) {
        continue;
      }
      const s32 length = static_cast<s32>(info.length);
      std::vector<uint8_t> bytes((length + 31) & ~31);
      if (DVDReadPrio(&info, bytes.data(), static_cast<s32>(bytes.size()), 0, 2) >= length) {
        bytes.resize(length);
        if (writeFile(root + "/messages/" + lang + "/" + e.name, packFile(bytes, e.name, &messages))) {
          files++;
        } else {
          failed++;
        }
      }
      DVDClose(&info);
    }
    DVDCloseDir(&d);
  }
  if (files == 0) {
    sResultEn = "No message files could be read from the disc.";
    return portTr("No message files could be read from the disc.");
  }
  sResultEn = failed > 0 ? "Written, but some files could not be written."
                         : "Written to the mods folder. Turn it on in the mod manager.";
  const std::string json = std::string("{\n  \"id\": \"local.text-pack-template\",\n  \"name\": \"Text pack "
                                       "template\",\n  \"version\": \"1.0\",\n  \"author\": \"\",\n  \"description\": "
                                       "\"The game's messages from your disc, to change. Keep only what you change.\","
                                       "\n  \"games\": [\"") +
                           PORT_GAME_ID + "\"]\n}\n";
  writeFile(root + "/mod.json", json);
  portModsFolderOff(portSettingsUserPath(), kFolder);
  char line[600];
  std::snprintf(line, sizeof(line),
                portTr("Written: mods/%s, %d messages in %d files. It is off in the mod manager until you turn it on."),
                kFolder, messages, files);
  std::string out = line;
  if (failed > 0) {
    out += " ";
    out += portTr("Some files could not be written.");
  }
  return out;
}

// asked from the page's or the Start menu's thread, done on the game's frame (the disc is the game's)
std::atomic<bool> sAsked;
std::mutex sLock;

std::string result() {
  std::lock_guard lock(sLock);
  return sResult;
}

void ask() {
  {
    std::lock_guard lock(sLock);
    sResult = portTr("Writing the template...");
  }
  sAsked = true;
}

void section(int inGame) {
  portui::hint("A text pack changes the game's own messages (dialogue, names, items, menus) in any "
               "language: a mod with a messages folder. This writes every message of your disc into "
               "the mods folder as a pack's starting point; see docs/DELOCALIZATION.md.");
  if (!inGame) {
    portui::hint("Start the game to write the template: it reads the messages from your disc.");
    return;
  }
  // written again over a template there, a translation made in it would be lost: pressed twice then
  static bool sConfirm;
  const bool there =
      SDL_GetPathInfo((std::string(portSettingsUserPath()) + "/mods/" + kFolder + "/mod.json").c_str(), nullptr);
  if (ImGui::Button(portTrLabel(sConfirm ? "Write it again over the template (changes made in it are lost)"
                                         : "Write the game's messages as a text pack template"))) {
    if (there && !sConfirm) {
      sConfirm = true;
    } else {
      sConfirm = false;
      ask();
    }
  }
  if (sConfirm) {
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Cancel##msgdump"))) {
      sConfirm = false;
    }
  }
  const std::string now = result();
  if (!now.empty()) {
    portui::hint(now.c_str());
  }
}

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  // (a template there already: A twice writes over it, so the note says what is lost, as the full page's button)
  const bool there =
      SDL_GetPathInfo((std::string(portSettingsUserPath()) + "/mods/" + kFolder + "/mod.json").c_str(), nullptr);
  out[0] = portPauseRow("Write the game's messages as a text pack template",
                        there ? "A template is there already: writing it again loses the changes made in it."
                              : "Every message of your disc, into the mods folder (text-pack-template), off until "
                                "you turn it on.",
                        nullptr, 0, PORT_PAUSE_ROW_CONFIRM);
  return 1;
}

const char* formAct(int id, int dir) {
  if (dir != 0 || id != 0) {
    return nullptr;
  }
  if (!portThreadIsMain()) {
    ask(); // (written on the game's frame: the disc is the game's)
    return "Writing the template...";
  }
  // the Start menu runs on the game's frame: written now, and how it went said in the help box
  sAsked = false;
  const std::string done = dump();
  {
    std::lock_guard lock(sLock);
    sResult = done;
  }
  return sResultEn.load();
}

const PortPauseForm kForm = {"Language", "Game text packs", formRows, formAct};

struct Register {
  Register() {
    portPauseAddForm(&kForm);
    portSettingsAddSection("Language", "Game text packs", nullptr, 0, section,
                           "text pack messages dump template dialogue delocalization delocalisation translation mod");
  }
} sRegister;

} // namespace

// the game's frame (restorations_aurora.c): a template asked for is written
extern "C" void portMessageDumpFrame(void) {
  if (!sAsked.exchange(false)) {
    return;
  }
  const std::string done = dump();
  std::lock_guard lock(sLock);
  sResult = done;
}
