// The port's own menus in the player's language (port/include/port/tr.h): the
// tables from port/assets/lang (tools/port/gen_lang.py), the menu language
// (System > Menu language), and the lookups.
#include "port/tr.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

namespace {

struct TrEntry {
  const char* en;
  const char* tr;
  const char* game;
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

// the choice's names: Auto, then each language in its own words
constexpr const char* kChoices[] = {"Auto", "English", "Deutsch", "Français", "Español", "Italiano", "日本語"};

int* sChoice;

int* choice() {
  if (sChoice == nullptr) {
    sChoice = portSettingsDeclare("menu_language", 0, 0, kLangCount);
  }
  return sChoice;
}

// Auto: the JP release's Japanese, the EU release's text language (the
// computer's when that is), else the computer's (SDL's preferred locales)
int detect() {
  if (std::strcmp(PORT_GAME_ID, "G8MJ01") == 0) {
    return kJapanese;
  }
  if (std::strcmp(PORT_GAME_ID, "G8MP01") == 0) {
    const int text = portSettings()->language; // 0 the computer's, 1-5 English .. Italiano
    if (text >= 1 && text <= 5) {
      return text - 1;
    }
  }
  static int sLocale = -1;
  if (sLocale < 0) {
    sLocale = 0;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
      for (int i = 0; i < count && locales[i] != nullptr && sLocale == 0; i++) {
        for (int k = 1; k < kLangCount; k++) {
          if (locales[i]->language != nullptr && std::strcmp(locales[i]->language, kLangs[k].code) == 0) {
            sLocale = k;
            break;
          }
        }
        if (locales[i]->language != nullptr && std::strcmp(locales[i]->language, "en") == 0) {
          break; // English before any other: English
        }
      }
      SDL_free(locales);
    }
  }
  return sLocale;
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

std::mutex sMutex;

// $TTYD_TR_DUMP: each English text once, as asked for
void dump(const char* english) {
  static const char* sPath = SDL_getenv("TTYD_TR_DUMP");
  static std::set<std::string> sSeen;
  if (sPath == nullptr || english[0] == '\0') {
    return;
  }
  std::lock_guard<std::mutex> lock(sMutex);
  if (!sSeen.insert(english).second) {
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

// UTF-8 to the US and EU releases' Latin-1, or "" when a letter is outside it
std::string latin1(const char* text) {
  std::string out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0';) {
    unsigned cp = *p;
    int len = 1;
    if (cp >= 0xF0) {
      cp &= 0x07, len = 4;
    } else if (cp >= 0xE0) {
      cp &= 0x0F, len = 3;
    } else if (cp >= 0xC0) {
      cp &= 0x1F, len = 2;
    }
    for (int k = 1; k < len && p[k] != '\0'; k++) {
      cp = (cp << 6) | (p[k] & 0x3F);
    }
    p += len;
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

struct Register {
  Register() {
    choice();
    portSettingsAddChoice("System", "Menu language",
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

extern "C" int portTrLanguage(void) {
  const int c = *choice();
  return c == 0 ? detect() : c - 1;
}

extern "C" const char* portTr(const char* english) {
  if (english == nullptr) {
    return nullptr;
  }
  dump(english);
  const int lang = portTrLanguage();
  if (lang == 0 || (lang == kJapanese && !sJapaneseFont)) {
    return english;
  }
  const TrEntry* e = find(lang, english);
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
  const bool jp = std::strcmp(PORT_GAME_ID, "G8MJ01") == 0;
  const int lang = portTrLanguage();
  // the JP release's font: Shift-JIS (Japanese or plain English); the others': Latin-1
  if (lang != 0 && (lang == kJapanese) == jp) {
    if (const TrEntry* e = find(lang, english); e != nullptr && e->game != nullptr) {
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
