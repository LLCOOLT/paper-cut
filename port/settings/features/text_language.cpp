// The US build's text in German, French, Spanish or Italian, from the
// European disc (port/include/port/text_language.h). The European release is
// built from the US code and its message files (msg/<DE|FR|SP|IT>/*.txt) have
// the US files' names and tags: once the US disc is open, its msg/US files
// are read, the European disc from the launcher's library is opened in its
// place for a moment for the chosen language's, and the US disc again. Each
// file becomes a host file (port/include/port/mods.h) under msg/US: the US
// file's tags, each with the European text where that has the tag (English
// where it hasn't), then the European file's own tags.
#include "port/mods.h"
#include "port/savestate.h" // portSaveStateNotice
#include "port/settings.h"
#include "port/text_language.h"
#include "../file_util.h" // portfile::resolvePath
#include <SDL3/SDL.h>
#include <aurora/dvd.h>
#include <dolphin/dvd.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* kChoices[] = {"English", "German (from the European disc)", "French (from the European disc)",
                                    "Spanish (from the European disc)", "Italian (from the European disc)"};
constexpr const char* kDirs[] = {nullptr, "DE", "FR", "SP", "IT"}; // the European disc's msg/<dir>

int* sLanguage;
const char* sLoadedDir; // the European folder whose text is in use (portTextLanguageDir)
const char* sWhy;       // why the text stayed English (English), said once the picture is up
int sWhyFrames;

// the reason as a notice over the picture, a moment after the game's window shows (the load is before it)
void sayWhy(int pageOpen) {
  (void)pageOpen;
  if (sWhy != nullptr && ++sWhyFrames == 90) {
    portSaveStateNotice(sWhy);
    sWhy = nullptr;
  }
}

using Files = std::map<std::string, std::vector<uint8_t>>;

// every file of the open disc's folder dir ("/msg/US"), by name
Files readFolder(const std::string& dir) {
  Files files;
  DVDDir d;
  DVDDirEntry e;
  if (!DVDOpenDir(dir.c_str(), &d)) {
    return files;
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
      files[e.name] = std::move(bytes);
    }
    DVDClose(&info);
  }
  return files;
}

// a message file's tag/text pairs, in order (they end at an empty tag)
std::vector<std::pair<std::string, std::string>> pairs(const std::vector<uint8_t>& file) {
  std::vector<std::pair<std::string, std::string>> out;
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
    out.emplace_back(std::string(p), std::string(text, next));
    p = next + 1;
  }
  return out;
}

std::vector<uint8_t> merge(const std::vector<uint8_t>& us, const std::vector<uint8_t>& eu) {
  const auto usPairs = pairs(us), euPairs = pairs(eu);
  std::unordered_map<std::string, const std::string*> euText;
  for (const auto& [tag, text] : euPairs) {
    euText.emplace(tag, &text);
  }
  std::vector<uint8_t> out;
  auto put = [&out](const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
  };
  std::unordered_map<std::string, bool> done;
  for (const auto& [tag, text] : usPairs) {
    const auto it = euText.find(tag);
    put(tag);
    put(it != euText.end() ? *it->second : text);
    done[tag] = true;
  }
  for (const auto& [tag, text] : euPairs) {
    if (!done.count(tag)) {
      put(tag);
      put(text);
    }
  }
  out.push_back(0); // the empty tag that ends the file
  return out;
}

// the launcher's disc images (library.txt in the user folder)
std::vector<std::string> library() {
  std::vector<std::string> paths;
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile((std::string(portSettingsUserPath()) + "/library.txt").c_str(), &size));
  if (text == nullptr) {
    return paths;
  }
  std::string line;
  for (size_t i = 0; i <= size; i++) {
    if (i == size || text[i] == '\n') {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
      }
      if (!line.empty()) {
        paths.push_back(portfile::resolvePath(line)); // (iOS: kept relative to the app's container)
      }
      line.clear();
    } else {
      line += text[i];
    }
  }
  SDL_free(text);
  return paths;
}

struct TextLanguageSettings {
  TextLanguageSettings() {
    sLanguage = portSettingsDeclare("text_language", 0, 0, 4);
    if (std::strcmp(PORT_GAME_ID, "G8ME01") == 0) {
      portSettingsAddChoice("Language", "Text language",
                            "The European release's text, read from its disc image in the launcher's library.",
                            PORT_SETTING_NEXT_START, "text_language", kChoices, 5,
                            "language german deutsch french francais spanish espanol italian italiano text pal");
      portSettingsAddOverlay(sayWhy, nullptr);
    }
  }
} sTextLanguage;

} // namespace

extern "C" int portTextLanguageLoad(const char* path) {
  const int language = *sLanguage;
  if (std::strcmp(PORT_GAME_ID, "G8ME01") != 0 || language <= 0 || language > 4) {
    return 1;
  }
  const Files us = readFolder("/msg/US");
  if (us.empty()) {
    return 1;
  }
  Files eu;
  aurora_dvd_close();
  for (const std::string& disc : library()) {
    if (!aurora_dvd_open(disc.c_str())) {
      continue;
    }
    if (std::memcmp(DVDGetCurrentDiskID()->gameName, "G8MP", 4) == 0) {
      eu = readFolder(std::string("/msg/") + kDirs[language]);
    }
    aurora_dvd_close();
    if (!eu.empty()) {
      break;
    }
  }
  if (!aurora_dvd_open(path)) { // the disc that was open a moment ago
    std::fprintf(stderr, "text language: could not reopen %s\n", path);
    return 0;
  }
  int count = 0;
  for (const auto& [name, bytes] : us) {
    const auto it = eu.find(name);
    if (it != eu.end()) {
      const std::vector<uint8_t> merged = merge(bytes, it->second);
      portModsSetHostFile(("/msg/US/" + name).c_str(), merged.data(), merged.size());
      count++;
    }
  }
  if (count > 0) {
    sLoadedDir = kDirs[language];
  } else {
    sWhy = eu.empty() ? "Text language: no European disc in the launcher's library, so the text is English."
                      : "Text language: the European disc's text couldn't be read, so the text is English.";
  }
  std::fprintf(stderr, "text language: %s, %d message files from the European disc\n", kDirs[language], count);
  return 1;
}

extern "C" const char* portTextLanguageDir(void) {
  return sLoadedDir;
}
