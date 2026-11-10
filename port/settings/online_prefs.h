// Online's preferences, <user>/online.txt (port/settings/features/online_prefs.cpp): the
// player's name (the boards' uploader too), what was typed last to join, the port to host
// on, and "List my room". Kept apart from the lobby, which sync what matters doesn't use.
#pragma once

#include "port/online.h"

#include <string>

namespace portonline {

struct Prefs {
  bool loaded = false;
  char name[32] = "";
  char address[128] = "";
  int port = PORT_ONLINE_DEFAULT_PORT;
  bool listMine = false; // the Start menu's "List my room" (the room list)
};

extern Prefs gPrefs;

// read once (later calls do nothing), and written whole after any change
void loadPrefs();
void savePrefs();

// a room's code exactly (PORT_ONLINE_CODE_LENGTH of PORT_ONLINE_CODE_CHARS, a dash
// allowed, spaces around), upper-cased without the dash, or "": the clipboard's test
std::string strictRoomCode(const char* text);

} // namespace portonline
