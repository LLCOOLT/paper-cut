// The message windows' text page by page (port/settings/message_text.h):
// msgdrv.c's hooks (port/include/port/speech.h) land here, each window's text
// is split at the game's own page breaks (<p>) with its tags taken out, and
// each page goes to the features listening (text to speech, the message log)
// as the window shows it.
#include "../message_text.h"
#include "port/companion.h"
#include "port/speech.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace msgtext {

bool japanese() { return std::strncmp(PORT_GAME_ID, "G8MJ", 4) == 0; }

std::string plain(const std::string& raw) {
  const bool jp = japanese();
  std::string out;
  bool inTag = false;
  for (char c : raw) {
    if (c == '<') {
      inTag = true;
    } else if (c == '>' && inTag) {
      inTag = false;
      out += ' ';
    } else if (!inTag) {
      if (c == '\n' && jp) {
        continue; // the box's line breaks: Japanese runs on without a space
      }
      out += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    }
  }
  std::string tidy;
  for (char c : out) {
    if (c != ' ' || (!tidy.empty() && tidy.back() != ' ')) {
      tidy += c;
    }
  }
  while (!tidy.empty() && tidy.back() == ' ') {
    tidy.pop_back();
  }
  return tidy;
}

std::string utf8(const char* game) {
  // portCompanionUtf8 (port/src/common/companion_pc.c): Latin-1 or Shift-JIS by the release, on every platform
  char out[2048];
  portCompanionUtf8(game, out, sizeof(out), 0);
  return out;
}

namespace {

struct Listener {
  PageFn page;
  ClosedFn closed;
};
std::vector<Listener>& listeners() {
  static std::vector<Listener> list;
  return list;
}

struct Window {
  std::vector<std::string> pages;
  int shown = -1; // the page shown last
  int seen = -1;  // the furthest page shown
};
std::map<const void*, Window> sWindows;

// a message's pages, split at the game's <p>
void addPages(Window& w, const char* text) {
  const std::string raw = text != nullptr ? text : "";
  size_t at = 0;
  for (;;) {
    const size_t p = raw.find("<p>", at);
    w.pages.push_back(plain(raw.substr(at, p == std::string::npos ? std::string::npos : p - at)));
    if (p == std::string::npos) {
      break;
    }
    at = p + 3;
  }
}

} // namespace

void listen(PageFn page, ClosedFn closed) { listeners().push_back(Listener{page, closed}); }

} // namespace msgtext

using namespace msgtext;

extern "C" void portSpeechMessage(const void* window, const char* text) {
  Window w;
  addPages(w, text);
  sWindows[window] = std::move(w);
}

extern "C" void portSpeechAppend(const void* window, const char* text) {
  addPages(sWindows[window], text);
}

extern "C" void portSpeechPage(const void* window, int page, const char* speaker) {
  auto it = sWindows.find(window);
  if (it == sWindows.end() || page == it->second.shown || page < 0 || page >= int(it->second.pages.size())) {
    return;
  }
  Window& w = it->second;
  w.shown = page;
  const bool fresh = page > w.seen;
  w.seen = std::max(w.seen, page);
  const std::string& words = w.pages[size_t(page)];
  if (words.empty()) {
    return;
  }
  for (const Listener& l : listeners()) {
    if (l.page != nullptr) {
      l.page(window, words, speaker != nullptr ? speaker : "", fresh);
    }
  }
}

extern "C" void portSpeechClosed(const void* window) {
  if (sWindows.erase(window) == 0) {
    return;
  }
  for (const Listener& l : listeners()) {
    if (l.closed != nullptr) {
      l.closed(window);
    }
  }
}
