// The port's own menus in the player's language (port/include/port/tr.h): the
// tables from port/assets/lang (tools/port/gen_lang.py), the language packs
// from the mods folder (<user>/mods/<pack>/lang/, docs/LANGUAGE_PACKS.md), the
// menu language (Language > Menu language), and the lookups.
#include "port/file_pc.h"
#include "port/tr.h"
#include "port/button_markup.h"
#include "port/settings.h"
#include "port/mods.h" // portModsFolderOff: a pack turned off in the mod manager
#include "port/port_text.h"
#include "port/text_fit.h"
#include "file_util.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
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

// The game's fonts have no '<' or '>' (a message tag's marks, in the US/EU font and the JP one): a tag
// ("<p>", "<col c00000ff>", "</col>", "<!STG1_07>": '<', not a space, and its '>' before another '<') is
// kept as it is; any other '<' or '>' (a menu path's "Settings > Audio", the ">>" button) is a stray mark, drawn as
// the font's look-alike: « » on the US and EU releases, ← → on the JP one (tools/port/gen_lang.py's
// fold_marks the same; tools/port/font_chars.py checks the port's text against the fonts).
// The length of the tag at p ('<'), 0 when it's a stray '<'
size_t tagLen(const unsigned char* p) {
  if (p[0] != '<' || p[1] == '\0' || p[1] == ' ' || p[1] == '\t' || p[1] == '\n' || p[1] == '\r' || p[1] == '<' ||
      p[1] == '>') {
    return 0;
  }
  for (size_t n = 1; p[n] != '\0'; n++) {
    if (p[n] == '>') {
      return n + 1;
    }
    if (p[n] == '<' || p[n] == '\n') {
      return 0;
    }
  }
  return 0;
}

// the Latin-1 letters the US and EU releases' font lacks (papermarioset_US/_EU.bfn's MAP1 blocks), as
// it can show them (gen_lang.py's LATIN_LACKS the same)
const char* latinLack(unsigned cp) {
  switch (cp) {
    case 0xA0: return " ";
    case 0xA2: return "c";
    case 0xA3: return "L";
    case 0xA5: return "Y";
    case 0xA6: return "|";
    case 0xA7: return "S";
    case 0xA8: return "\"";
    case 0xA9: return "(c)";
    case 0xAC: return "-";
    case 0xAD: return "";
    case 0xAE: return "(R)";
    case 0xAF: return "-";
    case 0xB0: return "\xBA"; // the degree sign: the ordinal's º (Italian's "50°")
    case 0xB1: return "+/-";
    case 0xB4: return "'";
    case 0xB5: return "u";
    case 0xB6: return "P";
    case 0xB7: return "-";
    case 0xB8: return ",";
    case 0xB9: return "1";
    case 0xC3: return "A";
    case 0xC5: return "A";
    case 0xC6: return "AE";
    case 0xD5: return "O";
    case 0xDD: return "Y";
    case 0xE3: return "a";
    case 0xE5: return "a";
    case 0xE6: return "ae";
    case 0xF0: return "d";
    case 0xF5: return "o";
    case 0xF7: return "/";
    case 0xF8: return "o";
    case 0xFD: return "y";
    case 0xFE: return "th";
    case 0xFF: return "y";
    default: return cp >= 0x7F && cp < 0xA0 ? "" : nullptr; // (control codes: none)
  }
}

// UTF-8 to the US and EU releases' Latin-1, or "" when a letter is outside it; what the font lacks as
// it can show it (above)
std::string latin1(const char* text) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    if (const size_t tag = tagLen(p); tag > 0) {
      out.append(reinterpret_cast<const char*>(p), tag);
      p += tag;
      continue;
    }
    if (*p == '<' || *p == '>') {
      out += static_cast<char>(*p == '<' ? 0xAB : 0xBB);
      p++;
      continue;
    }
    const unsigned cp = nextChar(p);
    if (const char* fold = cp <= 0xFF ? latinLack(cp) : nullptr; fold != nullptr) {
      out += fold;
    } else if (cp == 0x2026) { // the ellipsis: three dots
      out += "...";
    } else if (cp == 0x2013 || cp == 0x2014) {
      out += '-';
    } else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A) { // typographic quotes (gen_lang.py's LATIN_FOLD)
      out += '\'';
    } else if (cp == 0x201C || cp == 0x201D || cp == 0x201E) {
      out += '"';
    } else if (cp == 0x0152 || cp == 0x0153) { // the oe ligature
      out += cp == 0x0152 ? "OE" : "oe";
    } else if (cp > 0xFF) {
      return "";
    } else {
      out += static_cast<char>(cp);
    }
  }
  return out;
}

bool isJpRelease();

// UTF-8 to plain ASCII for the JP release's font (it has no accented letters): Latin letters
// without their accents (Francais, Espanol), any other letter a '?'. On the JP release, a stray '<' or
// '>' (tagLen) as its font's ← → (Shift-JIS) and \ ` (which it lacks too) as / '; elsewhere ( )
std::string asciiFold(const char* text) {
  static const char kFold[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty"; // U+00C0..U+00FF
  const bool jp = isJpRelease();
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    if (const size_t tag = tagLen(p); tag > 0) {
      out.append(reinterpret_cast<const char*>(p), tag);
      p += tag;
      continue;
    }
    if (*p == '<' || *p == '>') {
      out += jp ? (*p == '<' ? "\x81\xA9" : "\x81\xA8") : (*p == '<' ? "(" : ")");
      p++;
      continue;
    }
    if (jp && (*p == '\\' || *p == '`')) {
      out += *p == '\\' ? '/' : '\'';
      p++;
      continue;
    }
    const unsigned cp = nextChar(p);
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp == 0xDF) {
      out += "ss";
    } else if (cp >= 0xC0 && cp <= 0xFF) {
      out += kFold[cp - 0xC0];
    } else if (cp == 0x2026) {
      out += "...";
    } else if (cp == 0x2013 || cp == 0x2014) {
      out += '-';
    } else {
      out += '?';
    }
  }
  return out;
}

// UTF-8 to the JP release's Shift-JIS, its font's characters only
// (kTrJaFontMap, gen_lang.py's JA_FONT), or "" when one is missing. A message
// tag (<p>, <k>, <col ...>) and a line break are copied as they are: they aren't
// drawn as letters (gen_lang.py checks the same text with its tags left out); a
// stray '<' or '>' (tagLen) is the font's ← or →
std::string jaFont(const char* text) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    if (const size_t tag = tagLen(p); tag > 0) {
      out.append(reinterpret_cast<const char*>(p), tag);
      p += tag;
      continue;
    }
    if (*p == '<' || *p == '>') {
      out += *p == '<' ? "\x81\xA9" : "\x81\xA8";
      p++;
      continue;
    }
    if (*p == '\n') {
      out += '\n';
      p++;
      continue;
    }
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

// a text's printf conversions in order ("d s" for "%d of %s"; %% left out): some
// translations are formats, so a pack's must have the English's, or it isn't used.
// Parsed the way printf parses: an "n$" position, the flags " -+#0'", a width and
// precision (digits, "*" or "*n$"), a length, then the conversion. Positions are
// part of the code (MSVC's printf doesn't take them, so they never match English),
// and a % that doesn't start a whole conversion (a lone or trailing %, "%y") is a
// code of its own, "?", so it can't slip past either.
std::string formatCodes(const std::string& s) {
  std::string codes;
  const size_t n = s.size();
  auto in = [&](const char* set, size_t j) { return j < n && s[j] != '\0' && std::strchr(set, s[j]) != nullptr; };
  auto digits = [&](size_t& j) {
    const size_t from = j;
    while (j < n && s[j] >= '0' && s[j] <= '9') {
      j++;
    }
    return j > from;
  };
  auto position = [&](size_t& j, std::string& code) { // "n$", kept in the code
    size_t k = j;
    if (digits(k) && k < n && s[k] == '$') {
      code += s.substr(j, k + 1 - j);
      j = k + 1;
    }
  };
  for (size_t i = 0; i < n; i++) {
    if (s[i] != '%') {
      continue;
    }
    size_t j = i + 1;
    if (j < n && s[j] == '%') {
      i = j;
      continue;
    }
    std::string code;
    position(j, code);
    while (in(" -+#0'", j)) {
      j++; // (flags may differ)
    }
    if (j < n && s[j] == '*') {
      code += '*'; // (an argument: widths themselves may differ)
      j++;
      position(j, code);
    } else {
      digits(j);
    }
    if (j < n && s[j] == '.') {
      j++;
      if (j < n && s[j] == '*') {
        code += '*';
        j++;
        position(j, code);
      } else {
        digits(j);
      }
    }
    while (in("hlLqjztIw", j)) {
      code += s[j]; // (its size: I32/I64 keep their digits too)
      j++;
      if (s[j - 1] == 'I') {
        while (j < n && s[j] >= '0' && s[j] <= '9') {
          code += s[j++];
        }
      }
    }
    if (in("diouxXeEfFgGaAcspnCSZ", j)) {
      codes += code + s[j] + ' ';
      i = j;
    } else {
      codes += "? "; // (not a whole conversion: printf would misread it or abort)
    }
  }
  return codes;
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
    if (formatCodes(tr) != formatCodes(en)) {
      problems += name + ": \"" + en + "\": its % codes differ from the English (not used)\n";
      continue;
    }
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
      if (SDL_GetPathInfo((dir + "/pack.txt").c_str(), nullptr) && !portModsFolderOff(user.c_str(), folder.c_str())) {
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
  const PackLang* l = p != nullptr ? p->forLang[lang] : nullptr;
  return l != nullptr && l->fallback > 0 ? l->fallback : 0; // (none: English)
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
  // (texts made as they are shown, a status with a count, are new every time:
  // past this many the template has every real one, and the set stops growing)
  constexpr size_t kSeenCap = 16384;
  if (sPath == nullptr && sSeen.size() >= kSeenCap) {
    return;
  }
  sSeen.emplace(english);
  if (sPath == nullptr) {
    return;
  }
  if (FILE* f = portFopen(sPath, "ab")) {
    std::string line;
    for (const char* p = english; *p != '\0'; p++) {
      line += *p == '\n' ? std::string("\\n") : std::string(1, *p);
    }
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
  }
}

// $TTYD_TR_MISS=<file>: each text a built-in language has no translation for, once a language and
// kind: "<code>\t<kind>\t<English>" (kind: page, the settings page's; game, the game font's; font,
// translated but not in the game font's letters). The list to translate (tests: tools/port/testpass.py).
void miss(int lang, const char* kind, const char* english) {
  static const char* sPath = SDL_getenv("TTYD_TR_MISS");
  static std::unordered_set<std::string> sMissed;
  if (sPath == nullptr || english[0] == '\0' || lang <= 0 || lang >= kLangCount) {
    return;
  }
  std::string line = std::string(kLangs[lang].code) + "\t" + kind + "\t";
  for (const char* p = english; *p != '\0'; p++) {
    line += *p == '\n' ? std::string("\\n") : std::string(1, *p);
  }
  std::lock_guard<std::mutex> lock(sMutex);
  if (!sMissed.insert(line).second) {
    return;
  }
  if (FILE* f = std::fopen(sPath, "ab")) {
    std::fprintf(f, "%s\n", line.c_str());
    std::fclose(f);
  }
}

struct Register {
  Register() {
    choice();
    portSettingsAddChoice("Language", "Menu language",
                          "Paper Cut's own menus. Auto: the Japanese release's Japanese, the European release's "
                          "text language, otherwise the computer's. The game's own text follows the disc.",
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

namespace {
// the menu language within one generation of the packs (a reload can swap them
// between two loads: a lookup reads them once and works out its language from that)
int languageIn(const Packs* p) {
  const int c = *choice();
  if (c == 0) {
    return detect(p);
  }
  return c >= 1 && c - 1 < languageCount(p) ? c - 1 : 0;
}
} // namespace

extern "C" int portTrLanguage(void) { return languageIn(packs()); }

// --- Play as (docs/PLAY_AS.md): the pick's name for Mario's in these texts too ---

extern "C" {
int portPlayAsText(void);                                              // luigi_pc.c
int portPlayAsHasName(const char* text, int chr);                      // luigi_pc.c
void portPlayAsSwapText(const char* text, char* out, int chr, int enc); // luigi_pc.c
}

namespace {

constexpr int kEncSjis = 1, kEncLatin1 = 2, kEncUtf8 = 3; // luigi.h's PORT_TEXT_*

// Texts that keep Mario's name whoever the pick is (by their English): a choice
// that is a name itself (the Play as choice, a side, a sticker), Play as's and
// the voice toggle's own help (and an achievement's "playing as Luigi"), the
// looks and colours named after the brothers, versus (always the
// brothers: Mario's side against Luigi's), Brothers mode (Luigi with Mario,
// and it turns Play as off), and the model files' names.
bool keepsMario(const char* english) {
  static const char* const kWhole[] = {
      "Mario", "Luigi", "Peach", "Bowser", "Fire Mario", "Luigi green",
      // the colour slots, each named after the one it dresses (cosmetics.cpp: all four's listed together)
      "Mario's cap and shirt", "Mario's overalls", "Luigi's cap and shirt", "Luigi's overalls", "Peach's dress",
      "Peach's crown", "Peach's hair", "Peach's gloves", "Peach's earrings", "Peach's shoes", "Bowser's shell",
      "Bowser's hair", "Bowser's skin", "Bowser's horns and spikes", "Bowser's bracelets"};
  for (const char* w : kWhole) {
    if (std::strcmp(english, w) == 0) {
      return true;
    }
  }
  static const char* const kPart[] = {
      "Mario's place", "Luigi's voice for Mario", "sound as Luigi's", "as his brother or his rivals",
      "Mario's side", "Luigi's side", "Mario's player", "against Luigi", "against the computer's Luigi",
      "Mario's badges", "Mario's items", "Luigi's badges", "Luigi's items", "Luigi joins Mario",
      "Luigi follows Mario", "walks Luigi", "Brothers mode's Luigi", "Mario is a_mario", "Play as", "playing as",
      "Luigi's higher jump", "While Luigi is picked",
      // texts written for each pick already (the Wardrobe's tab and costume notes, wardrobe_aurora.c):
      // swapped again, Peach's would read "Mario, her partners" (the pick and Mario trade places)
      "partners and the status bar look", "This outfit doesn't fit",
  };
  for (const char* p : kPart) {
    if (std::strstr(english, p) != nullptr) {
      return true;
    }
  }
  return false;
}

// shown (english in the menu language) with the pick's name for Mario's, kept
// for as long as the game runs
const char* playAs(const char* shown, const char* english, int enc) {
  const int chr = portPlayAsText();
  if (chr <= 0 || chr > 3 || shown == nullptr || !portPlayAsHasName(shown, chr) || keepsMario(english)) {
    return shown;
  }
  static std::mutex sSwapMutex;
  static std::unordered_map<std::string, std::string> sSwapped[4][4];
  std::lock_guard<std::mutex> lock(sSwapMutex);
  auto& cache = sSwapped[chr][enc];
  auto it = cache.find(shown);
  if (it != cache.end()) {
    return it->second.c_str();
  }
  std::string out(std::strlen(shown) * 2 + 1, '\0');
  portPlayAsSwapText(shown, out.data(), chr, enc);
  out.resize(std::strlen(out.c_str()));
  // a table's text is kept for good (callers may hold it); a text made as it is
  // shown (a count, a status) only while the cache is small, then in a ring, so
  // the cache can't grow for as long as the page is open
  constexpr size_t kSwapCap = 4096;
  bool known = cache.size() < kSwapCap;
  for (int k = 1; k < kLangCount && !known; k++) {
    known = find(k, english) != nullptr;
  }
  if (known) {
    return cache.emplace(shown, std::move(out)).first->second.c_str();
  }
  // (each thread's own, as portTrf's and portTrList's: returned after the lock is let go, another
  // thread's 64 calls can't write over a text this one still uses)
  thread_local std::string sRing[64];
  thread_local unsigned sAt;
  std::string& slot = sRing[sAt++ % 64];
  slot = std::move(out);
  return slot.c_str();
}

} // namespace

static const char* trNoPlayAs(const char* english);

extern "C" const char* portTr(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  return portButtonsUi(playAs(trNoPlayAs(english), english, kEncUtf8));
}

extern "C" const char* portTrNoPlayAs(const char* english) { return portButtonsUi(trNoPlayAs(english)); }

// (portTr's, before the text's buttons are made icons: port/include/port/button_markup.h)
static const char* trNoPlayAs(const char* english) {
  if (english == nullptr || english[0] == '\0') {
    return english; // (nothing to look up)
  }
  dump(english);
  const Packs* p = packs();
  const int lang = languageIn(p);
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
  if (e == nullptr) {
    miss(base, "page", english);
  }
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

extern "C" const char* const* portTrList(const char* const* english, int count) {
  static thread_local std::vector<const char*> sRing[8];
  static thread_local int sAt;
  std::vector<const char*>& out = sRing[sAt++ % 8];
  out.assign(count > 0 ? static_cast<size_t>(count) : 0, "");
  for (int i = 0; i < count; i++) {
    out[i] = english[i] != nullptr ? portTr(english[i]) : "";
  }
  return out.data();
}

extern "C" const char* portTrf(const char* english, ...) {
  static thread_local std::string sRing[16];
  static thread_local int sAt;
  if (english == nullptr) {
    return nullptr;
  }
  const char* format = portTr(english);
  std::string& out = sRing[sAt++ % 16];
  va_list args;
  va_start(args, english);
  va_list again;
  va_copy(again, args);
  const int n = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  out.assign(n > 0 ? static_cast<size_t>(n) : 0, '\0');
  if (n > 0) {
    std::vsnprintf(out.data(), static_cast<size_t>(n) + 1, format, again);
  }
  va_end(again);
  return out.c_str();
}

extern "C" const char* portTrGame(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  const char* shown = portTextOwn(playAs(portTrGameNoPlayAs(english), english, isJpRelease() ? kEncSjis : kEncLatin1));
  portTextFitKey(english, shown); // (tests: $TTYD_TEXT_FIT's keys)
  return shown;
}

static const char* trGame(const char* english);

// Paper Cut's text, wherever the game's font draws it: the game's random numbers put back after
// (port/include/port/port_text.h)
extern "C" void portTextForGame(const char* utf8, char* out, size_t size) {
  if (out == nullptr || size == 0) {
    return;
  }
  const char* text = utf8 != nullptr ? utf8 : "";
  std::string shown = gameForm(text);
  if (shown.empty()) {
    shown = asciiFold(text);
  }
  size_t n = 0;
  for (size_t i = 0; i < shown.size() && n + 1 < size; i++) {
    const char c = shown[i];
    // (a Shift-JIS pair is never cut in half)
    if (isJpRelease() && (static_cast<unsigned char>(c) >= 0x81) && i + 1 < shown.size()) {
      if (n + 2 >= size) {
        break;
      }
      out[n++] = c;
      out[n++] = shown[++i];
      continue;
    }
    out[n++] = c == '<' ? '(' : c == '>' ? ')' : c;
  }
  out[n] = '\0';
}

extern "C" void portTextFromGame(const char* game, char* out, size_t size) {
  if (out == nullptr || size == 0) {
    return;
  }
  std::string utf8;
  const bool jp = isJpRelease();
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(game != nullptr ? game : ""); *p != '\0'; p++) {
    const unsigned c = *p;
    unsigned cp = c;
    if (c >= 0x80 && jp) {
      cp = '?';
      if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && p[1] != '\0') {
        const unsigned sjis = c << 8 | p[1];
        p++;
        for (const TrJaFontChar& f : kTrJaFontMap) { // (the font's few hundred: by its Shift-JIS)
          if (f.sjis == sjis) {
            cp = f.cp;
            break;
          }
        }
      }
    }
    // (otherwise the US and EU releases' Latin-1: the byte is the code point)
    char enc[4];
    size_t n = 0;
    if (cp < 0x80) {
      enc[n++] = char(cp);
    } else if (cp < 0x800) {
      enc[n++] = char(0xC0 | (cp >> 6));
      enc[n++] = char(0x80 | (cp & 0x3F));
    } else {
      enc[n++] = char(0xE0 | (cp >> 12));
      enc[n++] = char(0x80 | ((cp >> 6) & 0x3F));
      enc[n++] = char(0x80 | (cp & 0x3F));
    }
    if (utf8.size() + n + 1 > size) {
      break; // (never a letter cut in half)
    }
    utf8.append(enc, n);
  }
  std::memcpy(out, utf8.c_str(), utf8.size() + 1);
}

extern "C" const char* portTrGameNoPlayAs(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  const char* shown = portTextOwn(trGame(english));
  portTextFitKey(english, shown);
  return shown;
}

static const char* trGame(const char* english) {
  dump(english);
  const bool jp = isJpRelease();
  const Packs* p = packs();
  const int lang = languageIn(p);
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
    const bool translated = find(base, english) != nullptr;
    portTextFitEnglish(english, translated); // (tests: no translation, or none its font shows)
    if (std::strcmp(english, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") != 0) { // (made the font's below)
      miss(base, translated ? "font" : "game", english);
    }
  }
  // the English (or a text with no table entry), made the font's once: Latin-1, or on the JP release
  // ASCII (its font has no accented letters: the language names' are written without, as "Francais")
  std::lock_guard<std::mutex> lock(sMutex);
  static std::unordered_map<const char*, std::string> sCache;
  auto it = sCache.find(english);
  if (it == sCache.end()) {
    std::string l;
    if (std::strcmp(english, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") == 0) { // "Japanese" in Japanese (the language choice)
      l = jp ? "\x82\xC9\x82\xD9\x82\xF1\x82\xB2" : "Japanese"; // (its third kanji isn't in the JP font: in kana)
    } else if (jp) {
      l = asciiFold(english);
    } else {
      l = latin1(english);
    }
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
  // (whole or not at all: a full disk mustn't cut a translator's own strings.txt short)
  if (!portfile::writeAll(path, out.data(), out.size())) {
    return nullptr;
  }
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
