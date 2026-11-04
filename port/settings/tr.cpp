// The port's own menus in the player's language (port/include/port/tr.h): the
// tables from port/assets/lang (tools/port/gen_lang.py), the language packs
// from the mods folder (<user>/mods/<pack>/lang/, docs/LANGUAGE_PACKS.md), the
// menu language (System > Menu language), and the lookups.
#include "port/tr.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct TrEntry {
  const char* en;
  const char* tr;
  const char* game;
};

struct TrJaFontChar {
  unsigned short cp, sjis; // a character of the JP release's font (gen_lang.py's JA_FONT) and its Shift-JIS
};

#include "lang_tables.inc"

struct Lang {
  const char* code;
  const TrEntry* table;
  size_t count;
};

const Lang kLangs[] = {
    {"en", nullptr, 0},           {"de", kTr_de, kTrCount_de}, {"fr", kTr_fr, kTrCount_fr},
    {"es", kTr_es, kTrCount_es}, {"it", kTr_it, kTrCount_it}, {"ja", kTr_ja, kTrCount_ja},
};
constexpr int kLangCount = 6;
constexpr int kJapanese = 5;
constexpr int kMaxPackLangs = 32; // languages packs can add

// the choice's names: Auto, then each language in its own words (then the packs')
constexpr const char* kChoices[] = {"Auto", "English", "Deutsch", "Français", "Español", "Italiano", "日本語"};

int* sChoice;
int* sPackKey; // the pack language chosen, by its code's hash (packs are numbered by folder: this keeps the choice)

int* choice() {
  if (sChoice == nullptr) {
    sChoice = portSettingsDeclare("menu_language", 0, 0, kLangCount + kMaxPackLangs);
    sPackKey = portSettingsDeclare("menu_language_pack", 0, 0, 0x7FFFFFFF);
  }
  return sChoice;
}

int codeKey(const std::string& code) { // FNV-1a, 31 bits, never 0
  uint32_t h = 2166136261u;
  for (unsigned char c : code) {
    h = (h ^ c) * 16777619u;
  }
  return static_cast<int>(h & 0x7FFFFFFF) | 1;
}

// a string key found by a const char* without making a std::string
struct SvHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
using TextMap = std::unordered_map<std::string, std::string, SvHash, std::equal_to<>>;

// --- UTF-8 and the game's fonts ---

// the next code point of UTF-8 text (p moves past it)
unsigned nextChar(const unsigned char*& p) {
  unsigned cp = *p;
  int len = 1;
  if (cp >= 0xF0) {
    cp &= 0x07, len = 4;
  } else if (cp >= 0xE0) {
    cp &= 0x0F, len = 3;
  } else if (cp >= 0xC0) {
    cp &= 0x1F, len = 2;
  }
  p++;
  for (int k = 1; k < len && *p != '\0'; k++, p++) {
    cp = (cp << 6) | (*p & 0x3F);
  }
  return cp;
}

// UTF-8 to the US and EU releases' Latin-1, or "" when a letter is outside it
std::string latin1(const char* text) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    const unsigned cp = nextChar(p);
    if (cp == 0x2026) { // the ellipsis: three dots
      out += "...";
    } else if (cp == 0x2013 || cp == 0x2014) {
      out += '-';
    } else if (cp > 0xFF) {
      return "";
    } else {
      out += static_cast<char>(cp);
    }
  }
  return out;
}

// UTF-8 to the JP release's Shift-JIS, its font's characters only
// (kTrJaFontMap, gen_lang.py's JA_FONT), or "" when one is missing
std::string jaFont(const char* text) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    const unsigned cp = nextChar(p);
    if (cp >= 0x20 && cp < 0x7F && std::strchr("<>\\`", static_cast<int>(cp)) == nullptr) {
      out += static_cast<char>(cp);
      continue;
    }
    const auto* end = kTrJaFontMap + sizeof(kTrJaFontMap) / sizeof(kTrJaFontMap[0]);
    const auto* it = std::lower_bound(kTrJaFontMap, end, cp, [](const TrJaFontChar& c, unsigned v) { return c.cp < v; });
    if (it == end || it->cp != cp) {
      return "";
    }
    out += static_cast<char>(it->sjis >> 8);
    out += static_cast<char>(it->sjis & 0xFF);
  }
  return out;
}

bool isJpRelease() { return std::strcmp(PORT_GAME_ID, "G8MJ01") == 0; }

// the text in the game's encoding, or "" when the release's font can't show it
std::string gameForm(const char* text) { return isJpRelease() ? jaFont(text) : latin1(text); }

// --- The file format (tools/port/gen_lang.py's read(), the same rules) ---
// UTF-8 (a BOM is skipped); lines end in \n, \r\n or \r; a line starting with
// '#' is a comment, dropped first; then an English line, its translation on
// the next one, and blank lines between; \n inside a line is a line break.
// gen_lang.py stops on an English line with no translation after it; a pack
// keeps it as not translated yet (the template's empty lines), and keeps the
// first of two different translations of one English text.
struct Parsed {
  int strings = 0, untranslated = 0;
  std::vector<std::string> english; // every English line, translated or not (the template's update)
};

std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
      out += '\n';
      i++;
    } else {
      out += s[i];
    }
  }
  return out;
}

Parsed parseLang(const std::string& text, const std::string& name, TextMap& pairs, std::set<std::string>& mine,
                 std::string& problems) {
  Parsed r;
  std::vector<std::string> lines;
  size_t at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  while (at < text.size()) {
    size_t end = text.find_first_of("\r\n", at);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(at, end - at);
    if (line.empty() || line[0] != '#') {
      lines.push_back(std::move(line));
    }
    at = end < text.size() && text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n' ? end + 2 : end + 1;
  }
  size_t i = 0;
  while (i < lines.size()) {
    if (lines[i].empty()) {
      i++;
      continue;
    }
    const std::string en = unescape(lines[i]);
    r.english.push_back(en);
    if (i + 1 >= lines.size() || lines[i + 1].empty()) {
      r.untranslated++; // gen_lang.py: "no translation for"
      i++;
      continue;
    }
    const std::string tr = unescape(lines[i + 1]);
    i += 2;
    auto it = pairs.find(en);
    if (it == pairs.end()) {
      pairs.emplace(en, tr);
      mine.insert(en);
      r.strings++;
    } else if (mine.count(en) != 0 && it->second != tr) {
      problems += name + ": \"" + en + "\" translated twice (the first is used)\n";
    }
    // (an earlier pack's translation of it: that one wins)
  }
  return r;
}

std::string readText(const std::string& path, bool* ok = nullptr) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  if (ok != nullptr) {
    *ok = data != nullptr;
  }
  if (data == nullptr) {
    return {};
  }
  std::string text(static_cast<const char*>(data), size);
  SDL_free(data);
  return text;
}

std::vector<std::string> listDir(const std::string& dir, bool folders) {
  struct Ctx {
    std::string dir;
    bool folders;
    std::vector<std::string> out;
  } ctx{dir, folders, {}};
  SDL_EnumerateDirectory(
      dir.c_str(),
      [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
        Ctx* c = static_cast<Ctx*>(user);
        SDL_PathInfo info;
        if (name[0] != '.' && SDL_GetPathInfo((c->dir + "/" + name).c_str(), &info) &&
            (info.type == SDL_PATHTYPE_DIRECTORY) == c->folders) {
          c->out.emplace_back(name);
        }
        return SDL_ENUM_CONTINUE;
      },
      &ctx);
  std::sort(ctx.out.begin(), ctx.out.end());
  return ctx.out;
}

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t");
  const size_t b = s.find_last_not_of(" \t");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// --- Language packs ---

// one language's strings from the packs (those of each pack with its code, merged)
struct PackLang {
  std::string code, name, font; // font: the pack's .ttf, a full path, or ""
  int builtIn = -1;             // its index in kLangs, or -1: a language of its own
  int fallback = -1;            // a new language's fallback=, a built-in one's index
  TextMap tr;
  TextMap game;                   // the same in the game's encoding, where its font can show it
  std::set<std::string> owner;    // (while reading: the current pack's own texts)
  std::vector<ImWchar> ranges;    // its characters past Latin-1, as Dear ImGui ranges (0-ended), or empty
  std::set<ImWchar> chars;
  void* fontData = nullptr;       // the pack's font, read once (kept: the font atlas points at it)
  size_t fontSize = 0;
};

struct Pack {
  std::string folder, name, code, problems;
  int adds = 0, strings = 0, untranslated = 0, files = 0;
  PortTrPackInfo info{};
};

struct Packs {
  std::vector<Pack> packs;
  std::vector<std::unique_ptr<PackLang>> langs;
  const PackLang* forLang[kLangCount + kMaxPackLangs] = {}; // by language index
  int newCount = 0;
  const char* names[1 + kLangCount + kMaxPackLangs] = {};
};

std::atomic<Packs*> sPacks{nullptr};
std::mutex sPackMutex;
std::vector<std::unique_ptr<Packs>> sGenerations; // the ones before stay: their texts may still be shown
std::set<ImWchar> sAtlasChars;                     // the characters the page's fonts were given
bool sFontsBuilt;

bool validCode(const std::string& code) {
  if (code.empty() || code.size() > 15) {
    return false;
  }
  for (char c : code) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
      return false;
    }
  }
  return true;
}

void readPack(Packs& ps, const std::string& folder, const std::string& dir) {
  Pack pack;
  pack.folder = folder;
  std::string forWhat = "menus", font, fallback;
  const std::string head = readText(dir + "/pack.txt");
  size_t at = head.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  while (at < head.size()) {
    size_t end = head.find_first_of("\r\n", at);
    if (end == std::string::npos) {
      end = head.size();
    }
    const std::string line = trim(head.substr(at, end - at));
    at = end + 1;
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    if (key == "name") {
      pack.name = value;
    } else if (key == "code") {
      pack.code = value;
      std::transform(pack.code.begin(), pack.code.end(), pack.code.begin(),
                     [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
    } else if (key == "for") {
      forWhat = value;
    } else if (key == "font") {
      font = value;
    } else if (key == "fallback") {
      fallback = value;
    } else {
      pack.problems += "pack.txt: \"" + key + "\" is not a key Paper Cut knows (name, code, for, font, fallback)\n";
    }
  }
  if (pack.name.empty()) {
    pack.name = folder;
  }
  if (forWhat != "menus") {
    pack.problems += "pack.txt: for=" + forWhat + ": only menus (Paper Cut's own menus) is supported; not loaded\n";
  } else if (!validCode(pack.code)) {
    pack.problems += "pack.txt: code=" + pack.code +
                     " is not a language code (lower-case letters, digits and -, e.g. pt or pt-br); not loaded\n";
  } else {
    PackLang* lang = nullptr;
    for (auto& l : ps.langs) {
      if (l->code == pack.code) {
        lang = l.get();
      }
    }
    if (lang == nullptr) {
      int builtIn = -1;
      for (int k = 0; k < kLangCount; k++) {
        if (pack.code == kLangs[k].code) {
          builtIn = k;
        }
      }
      if (builtIn < 0 && ps.newCount >= kMaxPackLangs) {
        pack.problems += "more than 32 new languages: not loaded\n";
      } else {
        auto l = std::make_unique<PackLang>();
        l->code = pack.code;
        l->name = pack.name;
        l->builtIn = builtIn;
        if (builtIn >= 0) {
          ps.forLang[builtIn] = l.get();
        } else {
          ps.forLang[kLangCount + ps.newCount++] = l.get();
        }
        lang = l.get();
        ps.langs.push_back(std::move(l));
      }
    }
    if (lang != nullptr) {
      pack.adds = lang->builtIn < 0 ? 1 : 0;
      if (!fallback.empty() && lang->builtIn < 0 && lang->fallback < 0) {
        for (int k = 0; k < kLangCount; k++) {
          if (fallback == kLangs[k].code) {
            lang->fallback = k;
          }
        }
        if (lang->fallback < 0) {
          pack.problems += "pack.txt: fallback=" + fallback + ": not one of en de fr es it ja\n";
        }
      }
      if (!font.empty() && lang->font.empty()) {
        const std::string path = dir + "/" + font;
        if (SDL_GetPathInfo(path.c_str(), nullptr)) {
          lang->font = path;
        } else {
          pack.problems += "pack.txt: font=" + font + ": no such file in lang/\n";
        }
      }
      lang->owner.clear();
      for (const std::string& file : listDir(dir, false)) {
        if (file == "pack.txt" || file.size() < 4 || file.compare(file.size() - 4, 4, ".txt") != 0) {
          continue;
        }
        const Parsed r = parseLang(readText(dir + "/" + file), file, lang->tr, lang->owner, pack.problems);
        pack.strings += r.strings;
        pack.untranslated += r.untranslated;
        pack.files++;
      }
      if (pack.files == 0) {
        pack.problems += "no .txt files of texts beside pack.txt\n";
      }
    }
  }
  if (!pack.problems.empty() && pack.problems.back() == '\n') {
    pack.problems.pop_back();
  }
  ps.packs.push_back(std::move(pack));
}

void finish(Packs& ps) {
  for (auto& l : ps.langs) {
    for (const auto& [en, tr] : l->tr) {
      if (std::string g = gameForm(tr.c_str()); !g.empty()) {
        l->game.emplace(en, std::move(g));
      }
      for (const unsigned char* p = reinterpret_cast<const unsigned char*>(tr.c_str()); *p != '\0';) {
        if (const unsigned cp = nextChar(p); cp > 0xFF && cp <= 0xFFFF) {
          l->chars.insert(static_cast<ImWchar>(cp));
        }
      }
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(l->name.c_str()); *p != '\0';) {
      if (const unsigned cp = nextChar(p); cp > 0xFF && cp <= 0xFFFF) {
        l->chars.insert(static_cast<ImWchar>(cp));
      }
    }
    for (ImWchar c : l->chars) {
      if (!l->ranges.empty() && l->ranges.back() == c - 1) {
        l->ranges.back() = c;
      } else {
        l->ranges.push_back(c);
        l->ranges.push_back(c);
      }
    }
    if (!l->ranges.empty()) {
      l->ranges.push_back(0);
    }
  }
  for (int i = 0; i < 1 + kLangCount; i++) {
    ps.names[i] = kChoices[i];
  }
  for (int j = 0; j < ps.newCount; j++) {
    ps.names[1 + kLangCount + j] = ps.forLang[kLangCount + j]->name.c_str();
  }
  for (Pack& p : ps.packs) {
    p.info = PortTrPackInfo{p.folder.c_str(), p.name.c_str(), p.code.c_str(), p.adds, p.strings, p.untranslated,
                            p.problems.c_str()};
  }
}

std::string sUserDir; // portTrInit's

std::string userDir() {
  const char* user = portSettingsUserPath();
  return user != nullptr && user[0] != '\0' ? std::string(user) : sUserDir;
}

std::unique_ptr<Packs> loadPacks() {
  auto ps = std::make_unique<Packs>();
  const std::string user = userDir();
  if (!user.empty() && SDL_getenv("TTYD_NO_MODS") == nullptr) {
    const std::string mods = user + "/mods";
    for (const std::string& folder : listDir(mods, true)) {
      const std::string dir = mods + "/" + folder + "/lang";
      if (SDL_GetPathInfo((dir + "/pack.txt").c_str(), nullptr)) {
        readPack(*ps, folder, dir);
      }
    }
  }
  finish(*ps);
  return ps;
}

int sLocale = -1; // Auto's language from the computer's (-1: not looked yet)

// the packs read now in use; the menu language kept across them (a pack's by its code)
// the menu language kept on a pack's language: by its code (wanted), or else by
// settings.txt's menu_language_pack; Auto when that pack is gone
void remap(const Packs* ps, const std::string& wanted) {
  int* c = choice();
  if (*c < 1 + kLangCount) {
    return;
  }
  int pick = -1;
  for (int j = 0; j < ps->newCount; j++) {
    const std::string& code = ps->forLang[kLangCount + j]->code;
    if (wanted.empty() ? (*sPackKey != 0 && codeKey(code) == *sPackKey) : code == wanted) {
      pick = j;
    }
  }
  if (pick >= 0) {
    *c = 1 + kLangCount + pick;
  } else if (!wanted.empty() || *sPackKey != 0 || *c >= 1 + kLangCount + ps->newCount) {
    *c = 0;
  }
}

void adopt(std::unique_ptr<Packs> ps) {
  int* c = choice();
  const Packs* old = sPacks.load();
  std::string wanted;
  if (*c >= 1 + kLangCount && old != nullptr && *c - 1 < kLangCount + old->newCount) {
    wanted = old->forLang[*c - 1]->code;
  }
  remap(ps.get(), wanted);
  portSettingsSetChoices("menu_language", ps->names, 1 + kLangCount + ps->newCount);
  sPacks.store(ps.get());
  sGenerations.push_back(std::move(ps));
  sLocale = -1;
}

bool settingsRead() {
  const char* user = portSettingsUserPath();
  return user != nullptr && user[0] != '\0';
}

std::atomic<bool> sSettingsSeen{false};

// The packs, read once the user folder is known: from main_aurora.c's
// portTrInit (before the fonts are made, so they get the packs' letters), or
// settings.txt's. Once settings.txt is read, the choice is matched to its pack.
Packs* packs() {
  Packs* p = sPacks.load(std::memory_order_acquire);
  if (p != nullptr && sSettingsSeen.load(std::memory_order_acquire)) {
    return p;
  }
  std::lock_guard<std::mutex> lock(sPackMutex);
  p = sPacks.load();
  if (p == nullptr) {
    if (userDir().empty()) {
      return nullptr; // before the user folder is known
    }
    adopt(loadPacks());
    p = sPacks.load();
  }
  if (!sSettingsSeen && settingsRead()) {
    remap(p, std::string());
    sSettingsSeen = true;
  }
  return p;
}

int languageCount(const Packs* p) { return kLangCount + (p != nullptr ? p->newCount : 0); }

const char* languageCode(const Packs* p, int lang) {
  return lang < kLangCount ? kLangs[lang].code : p->forLang[lang]->code.c_str();
}

// Auto: the JP release's Japanese, the EU release's text language (the
// computer's when that is), else the computer's (SDL's preferred locales;
// the packs' languages too, "pt-br" before "pt")
int detect(const Packs* p) {
  if (isJpRelease()) {
    return kJapanese;
  }
  if (std::strcmp(PORT_GAME_ID, "G8MP01") == 0) {
    const int text = portSettings()->language; // 0 the computer's, 1-5 English .. Italiano
    if (text >= 1 && text <= 5) {
      return text - 1;
    }
  }
  if (sLocale < 0) {
    int found = 0;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
      for (int i = 0; i < count && locales[i] != nullptr && found == 0; i++) {
        const char* language = locales[i]->language;
        if (language == nullptr) {
          continue;
        }
        if (std::strcmp(language, "en") == 0) {
          break; // English before any other: English
        }
        std::string full = language;
        if (locales[i]->country != nullptr) {
          full += std::string("-") + locales[i]->country;
        }
        std::transform(full.begin(), full.end(), full.begin(),
                       [](char ch) { return ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch; });
        for (const std::string& want : {full, std::string(language)}) {
          for (int k = 1; k < languageCount(p) && found == 0; k++) {
            if (want == languageCode(p, k)) {
              found = k;
            }
          }
        }
      }
      SDL_free(locales);
    }
    sLocale = found;
  }
  return sLocale < languageCount(p) ? sLocale : 0;
}

const TrEntry* find(int lang, const char* english) {
  const Lang& l = kLangs[lang];
  size_t lo = 0, hi = l.count;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = std::strcmp(english, l.table[mid].en);
    if (c == 0) {
      return &l.table[mid];
    }
    if (c < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return nullptr;
}

// the built-in table a language falls back to: its own, a pack language's fallback=, or none (English)
int baseOf(const Packs* p, int lang) {
  if (lang < kLangCount) {
    return lang;
  }
  return p->forLang[lang]->fallback > 0 ? p->forLang[lang]->fallback : 0;
}

std::mutex sMutex;
std::unordered_set<std::string, SvHash, std::equal_to<>> sSeen; // every English text asked for this run

// each English text once, as asked for: kept for the template, and with
// $TTYD_TR_DUMP added to that file
void dump(const char* english) {
  static const char* sPath = SDL_getenv("TTYD_TR_DUMP");
  if (english[0] == '\0') {
    return;
  }
  std::lock_guard<std::mutex> lock(sMutex);
  if (sSeen.find(std::string_view(english)) != sSeen.end()) {
    return;
  }
  sSeen.emplace(english);
  if (sPath == nullptr) {
    return;
  }
  if (FILE* f = std::fopen(sPath, "ab")) {
    std::string line;
    for (const char* p = english; *p != '\0'; p++) {
      line += *p == '\n' ? std::string("\\n") : std::string(1, *p);
    }
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
  }
}

struct Register {
  Register() {
    choice();
    portSettingsAddChoice("Language", "Menu language",
                          "Paper Cut's own menus: the settings page, the launcher's and the Start menu's Settings "
                          "tab. Auto: the Japanese release's Japanese, the European release's text language, "
                          "otherwise the computer's. The game's own text follows the disc.",
                          0, "menu_language", kChoices, 7, "language translate english deutsch french spanish italian japanese");
  }
} sRegister;

} // namespace

namespace {
bool sJapaneseFont; // a system font with Japanese was merged into the page's fonts
} // namespace

// The page's fonts (launcher_style.cpp): Japanese from the system's font, for
// the Japanese translations' characters only (kTrJaGlyphs); without one, the
// page stays English in Japanese (the game's own font has its own glyphs).
ImFont* portTrMergeJapaneseRanges(float px, const ImWchar* ranges);
ImFont* portTrMergeJapanese(float px) {
  return portTrMergeJapaneseRanges(px, kTrJaGlyphs);
}

// The same with ranges of its own (the second screen's: every Japanese
// character, for the JP release's own names, port/settings/features/companion.cpp)
ImFont* portTrMergeJapaneseRanges(float px, const ImWchar* ranges) {
  static const char* sPath;
  static bool sLooked;
  if (!sLooked) {
    sLooked = true;
    static const char* const kPaths[] = {
#if defined(_WIN32)
        "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc",
#elif defined(__APPLE__)
        "/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7"
        "\xE3\x83\x83\xE3\x82\xAF W4.ttc", // Hiragino Kaku Gothic W4
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
#elif defined(__ANDROID__)
        "/system/fonts/NotoSansCJK-Regular.ttc",
#else
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
#endif
    };
    for (const char* p : kPaths) {
      if (SDL_GetPathInfo(p, nullptr)) {
        sPath = p;
        break;
      }
    }
  }
  if (sPath == nullptr) {
    return nullptr;
  }
  ImFontConfig cfg;
  cfg.MergeMode = true;
  cfg.OversampleH = 1;
  cfg.OversampleV = 1;
  cfg.PixelSnapH = true;
  ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(sPath, px, &cfg, ranges);
  sJapaneseFont = sJapaneseFont || (f != nullptr && ranges == kTrJaGlyphs);
  return f;
}

// The page's fonts for the font just added (Fredoka, data): the system's
// Japanese (above), then each language pack's characters past Latin-1, from
// Fredoka where it has them, else the pack's own font= (that language's
// characters only), else the system's CJK font (which has Cyrillic and Greek too).
void portTrMergeFonts(const unsigned char* data, unsigned int size, float px) {
  portTrMergeJapanese(px);
  Packs* p = packs();
  sFontsBuilt = true;
  if (p == nullptr) {
    return;
  }
  for (auto& l : p->langs) {
    if (l->ranges.empty()) {
      continue;
    }
    sAtlasChars.insert(l->chars.begin(), l->chars.end());
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), px, &cfg,
                                               l->ranges.data());
    if (!l->font.empty() && l->fontData == nullptr) {
      l->fontData = SDL_LoadFile(l->font.c_str(), &l->fontSize);
    }
    if (l->fontData != nullptr) {
      ImFontConfig own;
      own.MergeMode = true;
      own.FontDataOwnedByAtlas = false;
      own.OversampleH = 1;
      own.OversampleV = 1;
      own.PixelSnapH = true;
      ImGui::GetIO().Fonts->AddFontFromMemoryTTF(l->fontData, static_cast<int>(l->fontSize), px, &own,
                                                 l->ranges.data());
    }
    portTrMergeJapaneseRanges(px, l->ranges.data());
  }
}

extern "C" int portTrLanguage(void) {
  const int c = *choice();
  const Packs* p = packs();
  if (c == 0) {
    return detect(p);
  }
  return c - 1 < languageCount(p) ? c - 1 : 0;
}

extern "C" const char* portTr(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  dump(english);
  const int lang = portTrLanguage();
  const Packs* p = sPacks.load();
  if (p != nullptr && p->forLang[lang] != nullptr) {
    const TextMap& tr = p->forLang[lang]->tr;
    if (auto it = tr.find(std::string_view(english)); it != tr.end()) {
      return it->second.c_str(); // the pack's: over the built-in one
    }
  }
  const int base = p != nullptr ? baseOf(p, lang) : lang;
  if (base == 0 || (base == kJapanese && !sJapaneseFont)) {
    return english;
  }
  const TrEntry* e = find(base, english);
  return e != nullptr ? e->tr : english;
}

extern "C" const char* portTrLabel(const char* label) {
  static thread_local std::string sRing[16];
  static thread_local int sAt;
  if (label == nullptr) {
    return nullptr;
  }
  const char* id = std::strstr(label, "##");
  const std::string shown = id != nullptr ? std::string(label, id - label) : std::string(label);
  const char* t = shown.empty() ? "" : portTr(shown.c_str());
  if (!shown.empty() && t == shown.c_str()) {
    return label; // no translation: as it is
  }
  std::string& out = sRing[sAt++ % 16];
  out = std::string(t) + "###" + (id != nullptr ? std::string(id + 2) : shown);
  return out.c_str();
}

extern "C" const char* portTrGame(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  dump(english);
  const bool jp = isJpRelease();
  const int lang = portTrLanguage();
  const Packs* p = sPacks.load();
  if (p != nullptr && p->forLang[lang] != nullptr) {
    // a pack's text, where the release's font has every character of it
    const TextMap& game = p->forLang[lang]->game;
    if (auto it = game.find(std::string_view(english)); it != game.end()) {
      return it->second.c_str();
    }
  }
  const int base = p != nullptr ? baseOf(p, lang) : lang;
  // the JP release's font: Shift-JIS (Japanese or plain English); the others': Latin-1
  if (base != 0 && (base == kJapanese) == jp) {
    if (const TrEntry* e = find(base, english); e != nullptr && e->game != nullptr) {
      return e->game;
    }
  }
  if (jp) {
    return english; // the English is ASCII
  }
  // the English (or a text with no table entry), made Latin-1 once
  std::lock_guard<std::mutex> lock(sMutex);
  static std::unordered_map<const char*, std::string> sCache;
  auto it = sCache.find(english);
  if (it == sCache.end()) {
    std::string l = latin1(english);
    it = sCache.emplace(english, l.empty() ? std::string(english) : l).first;
  }
  return it->second.c_str();
}

// --- Language packs: the Mods tab's part (port/settings/features/lang_packs.cpp) ---

extern "C" void portTrInit(const char* userPath) {
  if (userPath != nullptr) {
    sUserDir = userPath;
  }
}

extern "C" int portTrPackCount(void) {
  const Packs* p = packs();
  return p != nullptr ? static_cast<int>(p->packs.size()) : 0;
}

extern "C" const PortTrPackInfo* portTrPack(int i) {
  const Packs* p = packs();
  return p != nullptr && i >= 0 && i < static_cast<int>(p->packs.size()) ? &p->packs[i].info : nullptr;
}

extern "C" void portTrReloadPacks(void) {
  std::lock_guard<std::mutex> lock(sPackMutex);
  adopt(loadPacks());
}

extern "C" int portTrPacksNeedRestart(void) {
  const Packs* p = sPacks.load();
  if (p == nullptr || !sFontsBuilt) {
    return 0;
  }
  for (const auto& l : p->langs) {
    for (ImWchar c : l->chars) {
      if (sAtlasChars.count(c) == 0) {
        return 1; // letters the page's fonts weren't given: they are made at the start
      }
    }
  }
  return 0;
}

extern "C" void portTrSettingsSync(void) {
  if (sChoice == nullptr) {
    return;
  }
  const Packs* p = packs(); // (matched to settings.txt's choice first)
  const int c = *sChoice;
  int key = 0;
  if (p != nullptr && c >= 1 + kLangCount && c - 1 < languageCount(p)) {
    key = codeKey(p->forLang[c - 1]->code);
  } else if (p == nullptr && c >= 1 + kLangCount) {
    key = *sPackKey; // packs not read yet: as it was
  }
  *sPackKey = key;
}

namespace {
std::string escapeLine(const std::string& s) {
  std::string out;
  for (char c : s) {
    out += c == '\n' ? std::string("\\n") : std::string(1, c);
  }
  return out;
}
} // namespace

extern "C" const char* portTrWriteTemplate(int* added) {
  static std::string sPath;
  if (added != nullptr) {
    *added = 0;
  }
  const std::string user = userDir();
  if (user.empty()) {
    return nullptr;
  }
  const std::string mods = user + "/mods";
  const std::string pack = mods + "/my-language";
  const std::string dir = pack + "/lang";
  SDL_CreateDirectory(mods.c_str());
  SDL_CreateDirectory(pack.c_str());
  SDL_CreateDirectory(dir.c_str());

  // every English text: the built-in tables' (what TTYD_TR_DUMP found) and those asked for this run
  std::set<std::string> all;
  for (int k = 1; k < kLangCount; k++) {
    for (size_t i = 0; i < kLangs[k].count; i++) {
      all.insert(kLangs[k].table[i].en);
    }
  }
  {
    std::lock_guard<std::mutex> lock(sMutex);
    all.insert(sSeen.begin(), sSeen.end());
  }
  const std::string path = dir + "/strings.txt";
  bool exists = false;
  const std::string have = readText(path, &exists);
  std::string out;
  if (exists) {
    // already there: only the texts it lacks, at its end
    TextMap pairs;
    std::set<std::string> mine;
    std::string problems;
    const Parsed r = parseLang(have, "strings.txt", pairs, mine, problems);
    for (const std::string& en : r.english) {
      all.erase(en);
    }
    if (all.empty()) {
      sPath = path;
      return sPath.c_str();
    }
    out = have;
    if (!out.empty() && out.back() != '\n') {
      out += '\n';
    }
    out += "\n# New texts to translate\n\n";
  } else {
    out = "# Paper Cut's own menus: each English text, then its translation on the empty line under it\n"
          "# (docs/LANGUAGE_PACKS.md). A text left empty stays English. Keep %s, %d and \\n as they are.\n\n";
  }
  for (const std::string& en : all) {
    if (en.empty() || en[0] == '#' || en.find('\r') != std::string::npos) {
      continue; // (a line the format can't hold)
    }
    out += escapeLine(en) + "\n\n\n";
    if (added != nullptr) {
      (*added)++;
    }
  }
  SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
  if (io == nullptr) {
    return nullptr;
  }
  SDL_WriteIO(io, out.data(), out.size());
  SDL_CloseIO(io);
  const std::string head = dir + "/pack.txt";
  if (!SDL_GetPathInfo(head.c_str(), nullptr)) {
    if (SDL_IOStream* h = SDL_IOFromFile(head.c_str(), "wb")) {
      const char* text = "# A language pack (docs/LANGUAGE_PACKS.md)\n"
                         "# name: the language in its own words, as the Menu language list shows it\n"
                         "name=My language\n"
                         "# code: the language's code (pt, pt-br, ko...); a built-in one (de, fr, es, it, ja, en)\n"
                         "# changes that language's texts instead\n"
                         "code=xx\n"
                         "for=menus\n"
                         "# fallback=es: a text this pack lacks shown in that built-in language, not English\n"
                         "# font=myfont.ttf: a font in this folder for letters Fredoka lacks (Cyrillic, Greek, Korean...)\n";
      SDL_WriteIO(h, text, std::strlen(text));
      SDL_CloseIO(h);
    }
  }
  sPath = path;
  return sPath.c_str();
}
