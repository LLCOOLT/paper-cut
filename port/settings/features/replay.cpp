// Replays (port/include/port/replay.h, docs/REPLAYS.md): the Practice tab's
// Replays section, and the file.
//
// Record: the game starts again (Restart now's machinery) from quick resume's
// save, where the player is, or from the title, and plays in lockstep (the
// game clock virtual from a base the file keeps, as online co-op's); from its
// first pad read every frame's input is kept (replay_pc.c). Stop and save
// writes <user>/replays/<name> <date>.pcreplay. Play: the game starts again
// with the replay's settings for that run (portSettingsSetForRun, as online's
// guest), its memory card in a folder of its own, its start save, its clock,
// and its input; Esc or B leaves (the game starts again as it was before).
//
// The file, little-endian ("bytes": a u32 length, then them):
//   "PCRPLAY1"; bytes Paper Cut's version, the disc's ID, the mods, the name,
//   the date; u64 the clock's base; u8 the start (0 the title, 1 a save),
//   u8 its card slot; bytes the settings ("key=value" lines: every one but
//   this device's own); u32 card files, each bytes name, bytes data; bytes
//   the start save (a .gci, or none); u32 frames; u32 runs, each u32 frames
//   and 2 x 16 bytes (both players' controllers, as online co-op's); u32
//   checks, each u32 frame and u64 fingerprint (every 600 frames); then,
//   optional, the ghost track ("GHST...") and a leaderboard run's "MODE"
//   trailer (u32 6, u8 the board, u8 finished, u32 the run's frames), then
//   Master Quest's set ("MQST", bytes its text, "" the curated one: the
//   playback plays it whatever master_quest_sets.txt has), then the built-in
//   randomizer's settings ("RAND", bytes their JSON: the open world's new
//   files and the classic randomizer's start take them, whatever this
//   player's randomizer.json has).
#include "../../archipelago/ttyd_rando.h"
#include "port/boards.h"
#include "port/lua_mods.h"
#include "port/master_quest.h"
#include "port/online.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"

#include <SDL3/SDL.h>
#include <aurora/card.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

extern "C" {
void CARDSetBasePath(const char* path, int chan); // Aurora (dolphin/card.h)
void portLockstepBegin(long long base);             // os_time_aurora.c
long long portRealTime(void);
void portRestartForReplay(void); // restart.cpp
}

namespace {

constexpr const char kMagic[8] = {'P', 'C', 'R', 'P', 'L', 'A', 'Y', '1'};
// three or four players: a pad count after the frames, then that many pads a run (older builds
// don't open these; a replay with controllers 3 and 4 never there is still written as PCRPLAY1)
constexpr const char kMagic2[8] = {'P', 'C', 'R', 'P', 'L', 'A', 'Y', '2'};

// controllers 3 and 4 are there in some run (the replay needs PCRPLAY2)
bool usesMorePads(const std::vector<PortReplayRun>& runs) {
  for (const PortReplayRun& run : runs) {
    for (int i = PORT_REPLAY_PADS_OLD; i < PORT_REPLAY_PADS; i++) {
      if (static_cast<signed char>(run.pads[i][10]) != -1) { // (the pad's err: PAD_ERR_NO_CONTROLLER)
        return true;
      }
    }
  }
  return false;
}
constexpr const char* kExt = ".pcreplay";

// this device's own (prefixes), as settings presets' (presets.cpp), but the
// intro and PAL's 50 Hz, which change the frames: never in a replay, never
// changed by one
constexpr const char* kDevice[] = {
    "framerate", "vsync", "fullscreen", "resolution", "fps", "overlay_", "msaa", "tex_filter", "texture_upscale",
    "supersample", "post_filter", "vrr", "hdr", "aniso", "lod_bias", "no_pop_in", "aspect_", "side_bars", "volume",
    "mute", "music_", "sfx_", "ambience_", "background_input", "pause_unfocused", "cloud_sync", "sync_",
    "phone_rumble", "rumble", "touch_", "auto_run", "stick_fixed", "tap_a", "orientation", "safe_area", "ff_",
    "launcher_", "dump_textures", "key", "pad", "controller", "deadzone", "trigger", "second_player_port",
    "menu_language", "speedrun_timer", "timer_", "split_category", "livesplit", "memory_watch", "achievements",
    "button_prompts", "perf_overlay", "update_check", "window_", "quick_resume", "save_backups", "second_screen",
    "companion", "custom_music", "settings_hint", "online", "crowd_", "jukebox", "game_sound_mode", "speakers",
    "bind_", "mouse_camera", "dualsense", "update_auto", "save_library", "whats_new", "battery_saver",
};

bool device(const std::string& key) {
  for (const char* p : kDevice) {
    if (key.compare(0, std::strlen(p), p) == 0) {
      return true;
    }
  }
  return false;
}

std::string readFile(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::string out;
  if (data != nullptr) {
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
  }
  return out;
}

bool writeFile(const std::string& path, const std::string& data) {
  return SDL_SaveFile(path.c_str(), data.data(), data.size());
}

// "key=value" lines -> key -> value
std::map<std::string, std::string> parseSettings(const std::string& text) {
  std::map<std::string, std::string> out;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (eq != std::string::npos && eq > 0) {
      out[line.substr(0, eq)] = line.substr(eq + 1);
    }
  }
  return out;
}

// --- the file's bytes ---

struct Writer {
  std::string b;
  void u8(unsigned v) { b.push_back(static_cast<char>(v)); }
  void u32(unsigned v) {
    for (int i = 0; i < 4; i++) {
      u8((v >> (8 * i)) & 255);
    }
  }
  void u64(unsigned long long v) {
    u32(static_cast<unsigned>(v));
    u32(static_cast<unsigned>(v >> 32));
  }
  void bytes(const std::string& s) {
    u32(static_cast<unsigned>(s.size()));
    b += s;
  }
};

struct Reader {
  const unsigned char* p;
  size_t left;
  bool ok = true;
  unsigned u8() {
    if (left < 1) {
      ok = false;
      return 0;
    }
    left--;
    return *p++;
  }
  unsigned u32() {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
      v |= u8() << (8 * i);
    }
    return v;
  }
  unsigned long long u64() {
    const unsigned long long lo = u32();
    return lo | (static_cast<unsigned long long>(u32()) << 32);
  }
  std::string bytes() {
    const unsigned n = u32();
    if (!ok || n > left) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p), n);
    p += n;
    left -= n;
    return s;
  }
};

struct Replay {
  std::string version, game, mods, name, date;
  unsigned long long base = 0;
  unsigned start = 0, slot = 3;
  std::string settings;
  std::vector<std::pair<std::string, std::string>> card;
  std::string save;
  unsigned frames = 0;
  std::vector<PortReplayRun> runs;
  std::vector<PortReplayCheck> checks;
  std::string ghost; // the ghost track ("GHST...", ghost_aurora.c), or none: after the checks, older builds stop before it
  // a leaderboard run's (port/include/port/boards.h): its board (-1 none), whether it finished, its time in frames
  int board = -1;
  unsigned boardDone = 0, boardFrames = 0;
  // Master Quest's set as the recording had it (portMasterQuestSelectedText); none in older replays
  bool hasMasterQuest = false;
  std::string masterQuest;
  // the built-in randomizer's settings (ttyd_rando.h: the open world's new files, the classic's start); none in older replays
  bool hasRando = false;
  std::string rando;
};

constexpr char kModeTag[4] = {'M', 'O', 'D', 'E'};
constexpr char kMasterQuestTag[4] = {'M', 'Q', 'S', 'T'};
constexpr char kRandoTag[4] = {'R', 'A', 'N', 'D'};

// the ghost track's length at p (ghost_aurora.c's layout), or left when it can't be told
size_t ghostLength(const unsigned char* p, size_t left) {
  auto at = [&](size_t o) -> unsigned {
    return o + 4 <= left ? unsigned(p[o]) | unsigned(p[o + 1]) << 8 | unsigned(p[o + 2]) << 16 | unsigned(p[o + 3]) << 24
                         : 0xFFFFFFFFu;
  };
  size_t o = 8;
  const unsigned maps = at(o);
  if (maps > 4096) {
    return left;
  }
  o += 4 + size_t(maps) * 16;
  const unsigned anims = at(o);
  if (anims > 4096) {
    return left;
  }
  o += 4 + size_t(anims) * 32;
  const unsigned samples = at(o);
  if (samples > 0x1000000) {
    return left;
  }
  o += 4 + size_t(samples) * 20;
  return o <= left ? o : left;
}

std::string encode(const Replay& r) {
  Writer w;
  const bool four = usesMorePads(r.runs);
  const int pads = four ? PORT_REPLAY_PADS : PORT_REPLAY_PADS_OLD;
  w.b.assign(four ? kMagic2 : kMagic, sizeof(kMagic));
  w.bytes(r.version);
  w.bytes(r.game);
  w.bytes(r.mods);
  w.bytes(r.name);
  w.bytes(r.date);
  w.u64(r.base);
  w.u8(r.start);
  w.u8(r.slot);
  w.bytes(r.settings);
  w.u32(static_cast<unsigned>(r.card.size()));
  for (const auto& [name, data] : r.card) {
    w.bytes(name);
    w.bytes(data);
  }
  w.bytes(r.save);
  w.u32(r.frames);
  if (four) {
    w.u8(static_cast<unsigned>(pads));
  }
  w.u32(static_cast<unsigned>(r.runs.size()));
  for (const PortReplayRun& run : r.runs) {
    w.u32(run.frames);
    w.b.append(reinterpret_cast<const char*>(run.pads), static_cast<size_t>(pads) * PORT_REPLAY_PAD_BYTES);
  }
  w.u32(static_cast<unsigned>(r.checks.size()));
  for (const PortReplayCheck& c : r.checks) {
    w.u32(c.frame);
    w.u64(c.hash);
  }
  w.b += r.ghost;
  if (r.board >= 0) {
    w.b.append(kModeTag, 4);
    w.u32(6);
    w.u8(static_cast<unsigned>(r.board));
    w.u8(r.boardDone);
    w.u32(r.boardFrames);
  }
  if (r.hasMasterQuest) {
    // (after MODE: builds before it stop at MODE, or at a tag that isn't MODE)
    w.b.append(kMasterQuestTag, 4);
    w.bytes(r.masterQuest);
  }
  if (r.hasRando) {
    // (after MQST: builds before it leave what follows MQST)
    w.b.append(kRandoTag, 4);
    w.bytes(r.rando);
  }
  return w.b;
}

// headerOnly: up to the settings (the list's)
bool decode(const std::string& data, Replay& r, bool headerOnly = false) {
  if (data.size() < sizeof(kMagic) || (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0 &&
                                       std::memcmp(data.data(), kMagic2, sizeof(kMagic2)) != 0)) {
    return false;
  }
  const bool four = std::memcmp(data.data(), kMagic2, sizeof(kMagic2)) == 0;
  Reader rd{reinterpret_cast<const unsigned char*>(data.data()) + sizeof(kMagic), data.size() - sizeof(kMagic)};
  r.version = rd.bytes();
  r.game = rd.bytes();
  r.mods = rd.bytes();
  r.name = rd.bytes();
  r.date = rd.bytes();
  r.base = rd.u64();
  r.start = rd.u8();
  r.slot = rd.u8();
  if (headerOnly || !rd.ok) {
    return rd.ok;
  }
  r.settings = rd.bytes();
  const unsigned files = rd.u32();
  for (unsigned i = 0; i < files && rd.ok; i++) {
    std::string name = rd.bytes();
    std::string bytes = rd.bytes();
    r.card.emplace_back(std::move(name), std::move(bytes));
  }
  r.save = rd.bytes();
  r.frames = rd.u32();
  const unsigned pads = four ? rd.u8() : PORT_REPLAY_PADS_OLD;
  if (pads < PORT_REPLAY_PADS_OLD || pads > PORT_REPLAY_PADS) {
    return false;
  }
  const size_t bytes = static_cast<size_t>(pads) * PORT_REPLAY_PAD_BYTES;
  const unsigned runs = rd.u32();
  for (unsigned i = 0; i < runs && rd.ok; i++) {
    PortReplayRun run{};
    run.frames = rd.u32();
    if (rd.left < bytes) {
      rd.ok = false;
      break;
    }
    std::memcpy(run.pads, rd.p, bytes);
    for (unsigned k = pads; k < PORT_REPLAY_PADS; k++) {
      run.pads[k][10] = 0xFF; // (not in the file: no controller there, PAD_ERR_NO_CONTROLLER)
    }
    rd.p += bytes;
    rd.left -= bytes;
    r.runs.push_back(run);
  }
  const unsigned checks = rd.u32();
  for (unsigned i = 0; i < checks && rd.ok; i++) {
    PortReplayCheck c{};
    c.frame = rd.u32();
    c.hash = rd.u64();
    r.checks.push_back(c);
  }
  if (rd.ok && portGhostHasTrack(rd.p, rd.left)) {
    const size_t n = ghostLength(rd.p, rd.left);
    r.ghost.assign(reinterpret_cast<const char*>(rd.p), n);
    rd.p += n;
    rd.left -= n;
  }
  if (rd.ok && rd.left >= 8 && std::memcmp(rd.p, kModeTag, 4) == 0) {
    rd.p += 4;
    rd.left -= 4;
    const unsigned size = rd.u32();
    if (size >= 6 && size <= rd.left) {
      const unsigned board = rd.u8();
      r.boardDone = rd.u8();
      r.boardFrames = rd.u32();
      r.board = board < PORT_BOARD_COUNT ? int(board) : -1;
      rd.p += size - 6; // (a longer trailer from a later build: the rest skipped)
      rd.left -= size - 6;
    }
  }
  if (rd.ok && rd.left >= 8 && std::memcmp(rd.p, kMasterQuestTag, 4) == 0) {
    rd.p += 4;
    rd.left -= 4;
    Reader mq = rd;
    std::string text = mq.bytes();
    if (mq.ok) {
      r.masterQuest = std::move(text);
      r.hasMasterQuest = true;
      rd = mq;
    }
  }
  if (rd.ok && rd.left >= 8 && std::memcmp(rd.p, kRandoTag, 4) == 0) {
    rd.p += 4;
    rd.left -= 4;
    Reader rr = rd;
    std::string text = rr.bytes();
    if (rr.ok) {
      r.rando = std::move(text);
      r.hasRando = true;
      rd = rr;
    }
  }
  return rd.ok;
}

// --- where things are ---

std::string replayDir() { return std::string(portSettingsUserPath()) + "/replays"; }
std::string playDir() { return replayDir() + "/.playback"; } // a playback's card and save

std::string gameId() {
  char id[8];
  std::snprintf(id, sizeof(id), "%.6s", portVersionSelf()->id);
  return id;
}

// the card's folder of this game (Aurora's GCI folder) under base, or Aurora's own (as online.cpp's)
std::string cardDir(const char* base) {
  char path[1024];
  char game[5];
  std::memcpy(game, portVersionSelf()->id, 4);
  game[4] = '\0';
  aurora_card_get_path(game, AURORA_CARD_GCI_DIRECTORY, 0, path, sizeof(path));
  std::string dir = path;
  if (base != nullptr) {
    const std::string user = portSettingsUserPath();
    if (dir.compare(0, user.size(), user) == 0) {
      dir = std::string(base) + dir.substr(user.size());
    }
  }
  return dir;
}

std::string modsList() {
  int count = 0;
  std::vector<std::string> names;
  if (char** found = SDL_GlobDirectory((std::string(portSettingsUserPath()) + "/mods").c_str(), "*", 0, &count)) {
    for (int i = 0; i < count; i++) {
      names.push_back(found[i]);
    }
    SDL_free(found);
  }
  std::sort(names.begin(), names.end());
  std::string list;
  for (const std::string& n : names) {
    list += (list.empty() ? "" : ",") + n;
  }
  // the Lua mods' files too (a replay plays the same only with the same Lua)
  if (const char* lua = portLuaHash(); lua[0] != '\0') {
    list += std::string("|lua:") + lua;
  }
  return list;
}

std::string now(bool forFile) {
  SDL_Time t = 0;
  SDL_DateTime dt{};
  SDL_GetCurrentTime(&t);
  SDL_TimeToDateTime(t, &dt, true);
  char text[40];
  std::snprintf(text, sizeof(text), forFile ? "%04d-%02d-%02d %02d.%02d.%02d" : "%04d-%02d-%02d %02d:%02d:%02d",
                dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  return text;
}

// a name as a file's: no path or reserved characters
std::string fileSafe(const std::string& name) {
  std::string out;
  for (char c : name) {
    out += std::strchr("\\/:*?\"<>|", c) != nullptr || static_cast<unsigned char>(c) < 32 ? '_' : c;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
    out.pop_back();
  }
  return out.empty() ? "Replay" : out;
}

std::string frameTime(unsigned frames) {
  const unsigned s = frames / 60;
  char text[32];
  std::snprintf(text, sizeof(text), "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
  return text;
}

// --- this run's replay ---

Replay sRec;          // recording: what the file will hold (the start)
Replay sPlay;         // playback: the file's
std::string sPlayPath;
bool sVersionDiffers, sModsDiffer;
char sName[64] = "Replay";
std::string sMessage;
std::string sSavedPath;
// the next start's environment (portReplayRestartEnv): name -> value
std::vector<std::pair<std::string, std::string>> sNextEnv;

std::string stopAndSave() {
  if (!portReplayRecording()) {
    return {};
  }
  portReplayRecordEnd();
  size_t runCount = 0, checkCount = 0;
  const PortReplayRun* runs = portReplayRuns(&runCount);
  const PortReplayCheck* checks = portReplayChecks(&checkCount);
  sRec.runs.assign(runs, runs + runCount);
  sRec.checks.assign(checks, checks + checkCount);
  sRec.frames = portReplayFrame();
  sRec.name = sName[0] != '\0' ? sName : "Replay";
  sRec.ghost.assign(portGhostBlob(nullptr, 0), '\0'); // where Mario was, for ghost races
  if (!sRec.ghost.empty()) {
    portGhostBlob(reinterpret_cast<unsigned char*>(sRec.ghost.data()), sRec.ghost.size());
  }
  std::string path;
  if (const char* out = SDL_getenv("TTYD_REPLAY_OUT")) { // tests: here
    path = out;
  } else {
    SDL_CreateDirectory(replayDir().c_str());
    path = replayDir() + "/" + fileSafe(sRec.name) + " " + now(true) + kExt;
  }
  if (!writeFile(path, encode(sRec))) {
    std::fprintf(stderr, "[replay] couldn't write %s: %s\n", path.c_str(), SDL_GetError());
    sMessage = portTr("The replay couldn't be saved.");
    return {};
  }
  std::printf("[replay] saved %s (%u frames)\n", path.c_str(), sRec.frames);
  std::fflush(stdout);
  sSavedPath = path;
  sMessage = std::string(portTr("Replay saved:")) + " " + path;
  return path;
}

// the recording's start, at start-up: what the playback needs to start the same
void beginRecording() {
  sRec = Replay{};
  sRec.version = PORT_VERSION;
  sRec.game = gameId();
  sRec.mods = modsList();
  sRec.date = now(false);
  if (const char* name = SDL_getenv("TTYD_REPLAY_NAME")) {
    std::snprintf(sName, sizeof(sName), "%s", name);
  }
  sRec.base = static_cast<unsigned long long>(portRealTime());
  portSettingsSave(); // settings.txt as they are now
  std::string settings;
  for (const auto& [key, value] : parseSettings(readFile(std::string(portSettingsUserPath()) + "/settings.txt"))) {
    if (!device(key)) {
      settings += key + "=" + value + "\n";
    }
  }
  sRec.settings = settings;
  // Master Quest's set as master_quest_sets.txt has it now (the settings name it only by its hash)
  sRec.hasMasterQuest = true;
  sRec.masterQuest = portMasterQuestSelectedText();
  // and this game on the same text for the run (an edit of the set meanwhile changes neither)
  portMasterQuestSetForRun(sRec.masterQuest.data(), sRec.masterQuest.size());
  // the built-in randomizer's as this start-up applied them (a seed made now when it is on with
  // none), and this game on the same for the run (the page changed meanwhile changes neither)
  sRec.hasRando = true;
  sRec.rando = portRandoSessionText(portSettingsUserPath());
  portRandoSettingsForRun(sRec.rando);
  const std::string dir = cardDir(nullptr);
  int count = 0;
  if (char** found = SDL_GlobDirectory(dir.c_str(), "*.gci", 0, &count)) {
    for (int i = 0; i < count; i++) {
      sRec.card.emplace_back(found[i], readFile(dir + "/" + found[i]));
    }
    SDL_free(found);
  }
  const char* save = SDL_getenv("TTYD_LOAD_SAVE");
  if (save != nullptr && save[0] != '\0') {
    sRec.save = readFile(save);
    const char* slot = SDL_getenv("TTYD_LOAD_SAVE_SLOT");
    sRec.slot = slot != nullptr && slot[0] >= '0' && slot[0] <= '3' ? static_cast<unsigned>(slot[0] - '0') : 3;
    sRec.start = sRec.save.empty() ? 0 : 1;
  }
  // a leaderboard run's (boards.cpp): armed now, so its start comes on the same frame in the playback
  if (const char* board = SDL_getenv("TTYD_BOARD_RUN"); board != nullptr && board[0] >= '0' &&
                                                        board[0] < '0' + PORT_BOARD_COUNT) {
    sRec.board = board[0] - '0';
    portBoardRunArm(sRec.board);
  }
  portLockstepBegin(static_cast<long long>(sRec.base));
  portReplayRecordBegin();
  std::printf("[replay] recording from %s\n", sRec.start ? "a save" : "the title");
  std::fflush(stdout);
}

// a playback's start, at start-up (race: a ghost race's, from the same start); false: the game starts as usual
bool beginPlayback(const char* path, bool race = false) {
  sPlayPath = path;
  if (!decode(readFile(path), sPlay)) {
    std::printf("[replay] %s isn't a replay (or isn't whole)\n", path);
    std::fflush(stdout);
    return false;
  }
  if (sPlay.game != gameId()) {
    std::printf("[replay] refused: made with the disc %s, this is %s\n", sPlay.game.c_str(), gameId().c_str());
    std::fflush(stdout);
    return false;
  }
  sVersionDiffers = sPlay.version != PORT_VERSION;
  sModsDiffer = sPlay.mods != modsList();
  if (sVersionDiffers) {
    std::printf("[replay] made with Paper Cut %s, this is %s: it may go out of step\n", sPlay.version.c_str(),
                PORT_VERSION);
  }
  if (sModsDiffer) {
    std::printf("[replay] made with other mods (%s): it may go out of step\n", sPlay.mods.c_str());
  }
  // the replay's settings for this run; this player's own that it doesn't have, at their defaults
  const auto wanted = parseSettings(sPlay.settings);
  portSettingsSave();
  for (const auto& [key, value] : parseSettings(readFile(std::string(portSettingsUserPath()) + "/settings.txt"))) {
    int def = 0;
    if (device(key) || wanted.find(key) != wanted.end()) {
      continue;
    }
    if (!portSettingsDefault(key.c_str(), &def)) {
      portSettingsExtraToDefault(key.c_str()); // declared on first use: at its default once it is
    } else if (std::to_string(def) != value) {
      portSettingsSetForRun(key.c_str(), std::to_string(def).c_str());
    }
  }
  for (const auto& [key, value] : wanted) {
    if (!device(key)) {
      portSettingsSetForRun(key.c_str(), value.c_str());
    }
  }
  if (sPlay.hasMasterQuest) {
    // the recording's set, whatever this player's master_quest_sets.txt has (online.cpp alike)
    portMasterQuestSetForRun(sPlay.masterQuest.data(), sPlay.masterQuest.size());
  }
  if (sPlay.hasRando) {
    // the recording's randomizer settings (portReplayEarly put them in place before the classic
    // randomizer applied: this only makes sure, for the open world's new files)
    portRandoSettingsForRun(sPlay.rando);
  }
  portSettingsHoldSaves(1); // nothing of it saved to this player's settings.txt
  portSettingsApply();
  // its card, in a folder of the playback's own
  const std::string base = playDir();
  const std::string dir = cardDir(base.c_str());
  SDL_CreateDirectory(dir.c_str());
  int count = 0;
  if (char** old = SDL_GlobDirectory(dir.c_str(), "*.gci", 0, &count)) {
    for (int i = 0; i < count; i++) {
      SDL_RemovePath((dir + "/" + old[i]).c_str());
    }
    SDL_free(old);
  }
  for (const auto& [name, data] : sPlay.card) {
    if (name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
        name.find("..") == std::string::npos) {
      writeFile(dir + "/" + name, data);
    }
  }
  CARDSetBasePath(base.c_str(), -1);
  if (sPlay.start == 1 && !sPlay.save.empty()) {
    const std::string save = base + "/start.gci";
    writeFile(save, sPlay.save);
    portWarpRequestResume(save.c_str(), static_cast<int>(sPlay.slot));
  }
  portLockstepBegin(static_cast<long long>(sPlay.base));
  if (sPlay.board >= 0) {
    portBoardRunArm(sPlay.board); // a leaderboard run's start, as in its recording
  }
  if (race) {
    // a ghost race: the same start, the player's own input, the replay's Mario drawn as a ghost
    if (!portGhostRaceBegin(reinterpret_cast<const unsigned char*>(sPlay.ghost.data()), sPlay.ghost.size(),
                            sPlay.name.c_str())) {
      std::printf("[replay] %s has no ghost to race\n", path);
    }
    std::fflush(stdout);
    return true;
  }
  portReplayPlayBegin(sPlay.runs.data(), sPlay.runs.size(), sPlay.checks.data(), sPlay.checks.size());
  std::printf("[replay] playing %s: '%s', %s, from %s\n", path, sPlay.name.c_str(), sPlay.date.c_str(),
              sPlay.start ? "a save" : "the title");
  std::fflush(stdout);
  return true;
}

// --- restarts ---

bool canRestart() { return portRestartSupported() && !portOnlineActive(); }

// start the game again recording: from quick resume's save (where the player is), or the title
void restartRecording(bool fromSave) {
  sNextEnv.clear();
  sNextEnv.emplace_back("TTYD_REPLAY_RECORD", "1");
  sNextEnv.emplace_back("TTYD_REPLAY_NAME", sName);
  if (fromSave) {
    portResumeWriteNow(); // the last clean save, with quick resume off too
    char path[1024], sidecar[1024];
    portResumePath(path, sizeof(path), portVersionSelf()->id, 0);
    portResumePath(sidecar, sizeof(sidecar), portVersionSelf()->id, 1);
    std::string slot = readFile(sidecar);
    slot = slot.empty() || slot[0] < '0' || slot[0] > '3' ? "3" : slot.substr(0, 1);
    sNextEnv.emplace_back("TTYD_LOAD_SAVE", path);
    sNextEnv.emplace_back("TTYD_LOAD_SAVE_SLOT", slot);
  }
  portRestartForReplay();
}

void restartPlaying(const std::string& path) {
  sNextEnv.clear();
  sNextEnv.emplace_back("TTYD_REPLAY_PLAY", path);
  portRestartForReplay();
}

void restartRacing(const std::string& path) {
  sNextEnv.clear();
  sNextEnv.emplace_back("TTYD_GHOST_RACE", path);
  portRestartForReplay();
}

// out of a playback: the game as it was before it (quick resume's save, when there's one)
void restartLeaving() {
  sNextEnv.clear();
  char path[1024], sidecar[1024];
  portResumePath(path, sizeof(path), portVersionSelf()->id, 0);
  portResumePath(sidecar, sizeof(sidecar), portVersionSelf()->id, 1);
  SDL_PathInfo info;
  if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
    std::string slot = readFile(sidecar);
    slot = slot.empty() || slot[0] < '0' || slot[0] > '3' ? "3" : slot.substr(0, 1);
    sNextEnv.emplace_back("TTYD_LOAD_SAVE", path);
    sNextEnv.emplace_back("TTYD_LOAD_SAVE_SLOT", slot);
  }
  portRestartForReplay();
}

bool quickResumeSave() {
  int slot = 0;
  size_t size = 0;
  if (portResumeImage(&slot, &size) != nullptr) {
    return true;
  }
  char path[1024];
  portResumePath(path, sizeof(path), portVersionSelf()->id, 0);
  SDL_PathInfo info;
  return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

// --- the list of replays ---

struct Entry {
  std::string path, file;
  Replay head;
  bool ok = false;
  bool ghost = false; // it keeps where Mario was: it can be raced
};
std::vector<Entry> sList;
int sPicked = -1;
bool sListed;

std::mutex sFileLock;
std::string sOpenPath; // from the file dialog's thread
std::atomic<bool> sPending{false};

void openChosen(void*, const char* const* files, int) {
  if (files != nullptr && files[0] != nullptr) {
    std::lock_guard<std::mutex> lock(sFileLock);
    sOpenPath = files[0];
    sPending = true;
  }
}

Entry entryFor(const std::string& path, const std::string& file) {
  Entry e;
  e.path = path;
  e.file = file;
  const std::string data = readFile(path);
  e.ok = decode(data, e.head, true);
  Replay whole;
  e.ghost = e.ok && decode(data, whole) && !whole.ghost.empty();
  return e;
}

void refresh() {
  sListed = true;
  sList.clear();
  sPicked = -1;
  const std::string dir = replayDir();
  int count = 0;
  if (char** found = SDL_GlobDirectory(dir.c_str(), (std::string("*") + kExt).c_str(), 0, &count)) {
    for (int i = 0; i < count; i++) {
      sList.push_back(entryFor(dir + "/" + found[i], found[i]));
    }
    SDL_free(found);
  }
  std::sort(sList.begin(), sList.end(), [](const Entry& a, const Entry& b) { return a.head.date > b.head.date; });
}

void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// what's to say about playing this one; false: it can't be played here
bool playable(const Entry& e, std::string& why) {
  if (!e.ok) {
    why = portTr("This file isn't a replay, or isn't whole.");
    return false;
  }
  if (e.head.game != gameId()) {
    char text[200];
    std::snprintf(text, sizeof(text), portTr("Made with another release's disc (%s): it can't be played here."),
                  e.head.game.c_str());
    why = text;
    return false;
  }
  if (e.head.version != PORT_VERSION) {
    char text[200];
    std::snprintf(text, sizeof(text),
                  portTr("Made with Paper Cut %s (this is %s): it may go out of step. You'll be told if it does."),
                  e.head.version.c_str(), PORT_VERSION);
    why = text;
  }
  return true;
}

void section(int inGame) {
  (void)inGame;
  if (sPending) {
    std::string open;
    {
      std::lock_guard<std::mutex> lock(sFileLock);
      open.swap(sOpenPath);
      sPending = false;
    }
    if (!open.empty()) {
      const size_t slash = open.find_last_of("/\\");
      sList.insert(sList.begin(), entryFor(open, slash == std::string::npos ? open : open.substr(slash + 1)));
      sPicked = 0;
    }
  }
  if (!sListed) {
    refresh();
  }
  if (portReplayRecording()) {
    ImGui::Text(portTr("Recording: %s (%u frames)."), frameTime(portReplayFrame()).c_str(), portReplayFrame());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    ImGui::InputText(portTrLabel("Name##replay_name"), sName, sizeof(sName));
    if (ImGui::Button(portTrLabel("Stop and save##replay_stop"))) {
      stopAndSave();
      refresh();
    }
  } else if (portGhostRacing()) {
    ImGui::Text(portTr("Racing the ghost of '%s'."), portGhostRaceName());
    if (ImGui::Button(portTrLabel("Stop the race##ghost_leave"))) {
      restartLeaving();
    }
    ImGui::SameLine();
    hint(portTr("The game starts again as it was before the race (also Shift+Esc)."));
  } else if (portReplayPlaying()) {
    ImGui::Text(portTr("Playing '%s': %u of %u frames."), sPlay.name.c_str(), portReplayFrame(), portReplayLength());
    if (ImGui::Button(portTrLabel("Stop the replay##replay_leave"))) {
      restartLeaving();
    }
    ImGui::SameLine();
    hint(portTr("The game starts again as it was before the replay."));
  } else {
    const bool restartable = canRestart();
    const bool fromSave = quickResumeSave();
    static int sFrom = 0; // 0 where you are (quick resume's save), 1 the title
    if (!fromSave) {
      sFrom = 1;
    }
    ImGui::TextUnformatted(portTr("Record"));
    ImGui::BeginDisabled(!fromSave);
    ImGui::RadioButton(portTrLabel("From where you are##replay_from"), &sFrom, 0);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::RadioButton(portTrLabel("From the title##replay_from"), &sFrom, 1);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    ImGui::InputText(portTrLabel("Name##replay_name"), sName, sizeof(sName));
    ImGui::BeginDisabled(!restartable);
    if (ImGui::Button(portTrLabel("Restart and record##replay_record"))) {
      restartRecording(sFrom == 0);
    }
    ImGui::EndDisabled();
    if (portOnlineActive()) {
      hint(portTr("Not during online co-op."));
    } else if (!portRestartSupported()) {
      hint(portTr("Close and reopen the app to apply it."));
    } else {
      hint(portTr("Recording starts the game again (as Restart now), from quick resume's save or the title, so that "
                  "the replay plays out exactly. Save states, warps, fast-forward and the Practice tab's edits are "
                  "off while recording."));
    }
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }

  ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
  ImGui::TextUnformatted(portTr("Your replays"));
  if (sList.empty()) {
    hint(portTr("None yet: they're kept in the replays folder of the user folder."));
  }
  if (ImGui::BeginListBox("##replays",
                          ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * std::clamp<float>(
                                                                                       float(sList.size()), 2.f, 8.f)))) {
    for (int i = 0; i < static_cast<int>(sList.size()); i++) {
      const Entry& e = sList[i];
      char label[300];
      if (e.ok) {
        std::snprintf(label, sizeof(label), "%s  (%s)##replay%d", e.head.name.c_str(), e.head.date.c_str(), i);
      } else {
        std::snprintf(label, sizeof(label), "%s##replay%d", e.file.c_str(), i);
      }
      if (ImGui::Selectable(label, sPicked == i)) {
        sPicked = i;
      }
    }
    ImGui::EndListBox();
  }
  if (ImGui::Button(portTrLabel("Refresh##replays"))) {
    refresh();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open a replay file...##replays"))) {
    static SDL_DialogFileFilter kFilter[] = {{nullptr, "pcreplay"}};
    kFilter[0].name = portTr("Paper Cut replay"); // (kept until the dialog answers)
    SDL_ShowOpenFileDialog(openChosen, nullptr, SDL_GetKeyboardFocus(), kFilter, 1, nullptr, false);
  }
#endif
  if (sPicked >= 0 && sPicked < static_cast<int>(sList.size())) {
    const Entry& e = sList[sPicked];
    std::string why;
    const bool ok = playable(e, why);
    if (!why.empty()) {
      ImGui::TextWrapped("%s", why.c_str());
    }
    ImGui::BeginDisabled(!ok || !canRestart() || portReplayRecording());
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Play##replay_play"))) {
      restartPlaying(e.path);
    }
    if (e.ghost) {
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Race its ghost##replay_race"))) {
        restartRacing(e.path);
      }
    }
    ImGui::EndDisabled();
    if (ok && e.ghost) {
      hint(portTr("Race its ghost: the game starts again from the replay's start with you in control, and the "
                  "replay's Mario runs beside you, see-through, in the same rooms. Each room says how far ahead or "
                  "behind you are."));
    }
    if (ok) {
      ImGui::SameLine();
      hint(portTr("The game starts again to play it; Esc or B stops it."));
    }
  }
}

// --- over the game ---

void drawLabel() {
  if (!portReplayActive()) {
    return;
  }
  char text[256];
  if (portReplayRecording()) {
    std::snprintf(text, sizeof(text), "%s %s", portTr("Recording"), frameTime(portReplayFrame()).c_str());
  } else if (portReplayFinished()) {
    std::snprintf(text, sizeof(text), "%s", portTr("Replay finished: Esc or B to leave"));
  } else {
    std::snprintf(text, sizeof(text), "%s %u / %u", portTr("Replay"), portReplayFrame(), portReplayLength());
  }
  std::string extra;
  if (portReplayPlaying() && portReplayOutOfStep() >= 0) {
    char line[160];
    std::snprintf(line, sizeof(line), portTr("Out of step since frame %ld"), portReplayOutOfStep());
    extra = line;
  } else if (portReplayPlaying() && sVersionDiffers) {
    char line[160];
    std::snprintf(line, sizeof(line), portTr("Made with Paper Cut %s"), sPlay.version.c_str());
    extra = line;
  }
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  const float size = ImGui::GetFontSize();
  const ImVec2 at(size * 0.6f, size * 0.6f);
  const ImVec2 box = ImGui::CalcTextSize(text);
  const ImVec2 box2 = extra.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(extra.c_str());
  const float w = std::max(box.x, box2.x);
  const float h = box.y + (extra.empty() ? 0.f : box2.y + size * 0.2f);
  draw->AddRectFilled(ImVec2(at.x - size * 0.3f, at.y - size * 0.2f), ImVec2(at.x + w + size * 0.3f, at.y + h + size * 0.2f),
                      IM_COL32(0, 0, 0, 150), size * 0.3f);
  const ImU32 dot = portReplayRecording() ? IM_COL32(235, 60, 60, 255) : IM_COL32(255, 255, 255, 255);
  draw->AddText(at, dot, text);
  if (!extra.empty()) {
    draw->AddText(ImVec2(at.x, at.y + box.y + size * 0.2f), IM_COL32(255, 200, 90, 255), extra.c_str());
  }
}

void frame(int pageOpen) {
  (void)pageOpen;
  static bool sLeaving;
  // tests: $TTYD_REPLAY_FRAMES=N, the recording saved after N frames;
  // $TTYD_REPLAY_EXIT, the program ends once it's saved, or the playback is over
  static const char* sStopAt = SDL_getenv("TTYD_REPLAY_FRAMES");
  static const bool sExit = SDL_getenv("TTYD_REPLAY_EXIT") != nullptr;
  if (portReplayRecording() && sStopAt != nullptr && portReplayFrame() >= static_cast<unsigned>(std::atoi(sStopAt))) {
    stopAndSave();
    if (sExit) {
      std::_Exit(0);
    }
  }
  // a leaderboard run's recording: saved as it ends (its time in the trailer)
  if (portReplayRecording() && sRec.board >= 0) {
    const int state = portBoardRunState();
    if (state == PORT_BOARD_RUN_DONE || state == PORT_BOARD_RUN_FAILED) {
      sRec.boardDone = state == PORT_BOARD_RUN_DONE ? 1 : 0;
      sRec.boardFrames = portBoardRunFrames();
      const std::string path = stopAndSave();
      if (!path.empty()) {
        sMessage = std::string(portTr(state == PORT_BOARD_RUN_DONE
                                          ? "Run finished and saved: check and upload it in Modes > Leaderboards."
                                          : "The run ended before its end: saved as a replay, not for the board.")) +
                   " " + path;
      }
      if (sExit) {
        std::_Exit(0);
      }
    }
  }
  if (portReplayPlaying() && sExit && portReplayFinished()) {
    std::printf("[replay] test: done\n");
    std::fflush(stdout);
    std::_Exit(portReplayOutOfStep() >= 0 ? 3 : 0);
  }
  if (portReplayPlaying() && portReplayStopAsked() && !sLeaving) {
    sLeaving = true;
    restartLeaving();
  }
  drawLabel();
}

int event(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type == SDL_EVENT_QUIT || e->type == SDL_EVENT_TERMINATING) {
    stopAndSave(); // a recording under way kept
    return 0;
  }
  if ((portReplayPlaying() || portGhostRacing()) && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat &&
      e->key.key == SDLK_ESCAPE && (portReplayPlaying() || (e->key.mod & SDL_KMOD_SHIFT))) { // a race: Shift+Esc
    restartLeaving();
    return 1;
  }
  return 0;
}

const bool sAdded = [] {
  portSettingsAddSection("Practice", "Replays",
                         "Record a run's input to a small file, and play it back exactly: from where you are or from "
                         "the title.",
                         0, section, "replay replays record recording playback input movie tas demo");
  portSettingsAddOverlay(frame, event);
  return true;
}();

} // namespace

// At start-up before the mods (main_aurora.c): a playback's randomizer settings, so the
// classic randomizer applies the recording's at the start (portApInit) as it did there
extern "C" void portReplayEarly(void) {
  const char* play = SDL_getenv("TTYD_REPLAY_PLAY");
  if (play == nullptr || play[0] == '\0') {
    play = SDL_getenv("TTYD_GHOST_RACE");
  }
  if (play == nullptr || play[0] == '\0') {
    return;
  }
  Replay r;
  if (decode(readFile(play), r) && r.hasRando) {
    portRandoSettingsForRun(r.rando);
  }
}

extern "C" void portReplayStartup(void) {
  if (const char* play = SDL_getenv("TTYD_REPLAY_PLAY"); play != nullptr && play[0] != '\0') {
    if (portOnlineActive()) {
      return;
    }
    beginPlayback(play);
  } else if (const char* race = SDL_getenv("TTYD_GHOST_RACE"); race != nullptr && race[0] != '\0') {
    if (!portOnlineActive()) {
      beginPlayback(race, true);
    }
  } else if (SDL_getenv("TTYD_REPLAY_RECORD") != nullptr && !portOnlineActive()) {
    beginRecording();
  }
}

extern "C" void portReplaySaveIfRecording(void) { stopAndSave(); }

extern "C" int portReplayRestartRecording(const char* name, int fromSave) {
  if (!canRestart() || portReplayActive() || (fromSave && !quickResumeSave())) {
    return 0;
  }
  std::snprintf(sName, sizeof(sName), "%s", name != nullptr && name[0] != 0 ? name : "Replay");
  restartRecording(fromSave != 0);
  return 1;
}

extern "C" int portReplayCanRestart(void) { return canRestart() && !portReplayActive() ? 1 : 0; }

extern "C" int portReplayCanRecordHere(void) { return portReplayCanRestart() && quickResumeSave() ? 1 : 0; }

extern "C" int portReplayRestartForBoard(int board, const char* name) {
  if (board < 0 || board >= PORT_BOARD_COUNT || !portReplayCanRecordHere()) {
    return 0;
  }
  std::snprintf(sName, sizeof(sName), "%s", name != nullptr && name[0] != 0 ? name : portBoardTitle(board));
  restartRecording(true);
  sNextEnv.emplace_back("TTYD_BOARD_RUN", std::to_string(board));
  return 1;
}

extern "C" int portReplayRestartPlay(const char* path, const char* env, const char* value) {
  if (!portReplayCanRestart()) {
    return 0;
  }
  restartPlaying(path);
  if (env != nullptr) {
    sNextEnv.emplace_back(env, value != nullptr ? value : "1");
  }
  return 1;
}

extern "C" int portReplayPlayingBoard(int* board, unsigned* frames, int* done) {
  if (!portReplayPlaying() || sPlay.board < 0) {
    return 0;
  }
  *board = sPlay.board;
  *frames = sPlay.boardFrames;
  *done = static_cast<int>(sPlay.boardDone);
  return 1;
}

extern "C" const char* portReplayPlayingPath(void) { return sPlayPath.c_str(); }

namespace port_replay {

// a replay file's leaderboard run (boards.cpp's list): false when it's none
bool boardRun(const std::string& path, int& board, unsigned& frames, bool& done, std::string& name, std::string& date,
              std::string& game, std::string& version) {
  Replay r;
  if (!decode(readFile(path), r) || r.board < 0) {
    return false;
  }
  board = r.board;
  frames = r.boardFrames;
  done = r.boardDone != 0;
  name = r.name;
  date = r.date;
  game = r.game;
  version = r.version;
  return true;
}

// the bytes a board keeps of a replay: all of it but the ghost track (only for races here, and large)
bool forUpload(const std::string& path, std::string& bytes) {
  Replay r;
  if (!decode(readFile(path), r) || r.board < 0) {
    return false;
  }
  r.ghost.clear();
  bytes = encode(r);
  return true;
}

} // namespace port_replay

extern "C" void portReplayRestartEnv(void) {
  SDL_Environment* env = SDL_GetEnvironment();
  for (const auto& [name, value] : sNextEnv) {
    SDL_SetEnvironmentVariable(env, name.c_str(), value.c_str(), true);
  }
}
