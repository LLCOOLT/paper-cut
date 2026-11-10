// The mod manager's installs (port/settings/mod_manager.h): a file or folder
// the player dropped on the window or picked, copied into the mods folder on
// a worker thread, as what it is. Only the player's own files: nothing is
// downloaded. And the drop events, and safe mode's "the game got going".
#include "../mod_manager.h"

#include "port/detach.h"
#include "port/offscreen.h"
#include "port/mods.h"
#include "port/path_link.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h" // gPortLauncherShown
#include "../file_util.h"    // portfile::isLink

#include <SDL3/SDL.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include "port/ios_pc.h" // portIosPickedDone
#endif
#endif
#ifdef __ANDROID__
#include "port/android_pc.h" // the file picker's content:// URIs
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" void portSettingsOpenAt(const char* tab); // settings_ui.cpp: the page open on a tab
bool portSaveManagerTakesDrop(const char* path);       // save_manager.cpp
extern "C" int portPm64LinkIsRom(const char* path);   // pm64_link.cpp (port/include/port/pm64.h)

namespace {

std::mutex sLock;
std::deque<std::string> sQueue;       // waiting
modmgr::Progress sProgress;           // the one being installed
std::vector<modmgr::Result> sResults; // newest last
bool sWorker;                         // the thread runs
std::atomic<int> sFinished{0};        // installs done, not yet seen by poll()
std::atomic<unsigned> sFinishedKinds{0};
std::atomic<bool> sDragging{false};
std::atomic<uint64_t> sDroppedAt{0};

std::string userDir() { return portSettingsUserPath() != nullptr ? portSettingsUserPath() : "."; }
std::string modsDir() { return userDir() + "/mods"; }

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}

bool endsWith(const std::string& s, const char* suffix) {
  const std::string l = lower(s);
  const size_t n = std::strlen(suffix);
  return l.size() >= n && l.compare(l.size() - n, n, suffix) == 0;
}

std::string slashes(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  while (s.size() > 1 && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

// a path's last part; an Android content URI's name as the file manager shows it (the URI itself
// often holds none: ".../document/msf%3A1234"), else what follows its last / or %2F
std::string baseName(const std::string& path) {
  std::string p = slashes(path);
#ifdef __ANDROID__
  char shown[512];
  if (PORT_ANDROID_IS_CONTENT_URI(path.c_str()) && portAndroidContentInfo(path.c_str(), shown, sizeof(shown), nullptr) &&
      shown[0] != '\0') {
    p = shown;
  }
#endif
  size_t cut = p.find_last_of('/');
  const size_t enc = lower(p).rfind("%2f");
  if (enc != std::string::npos && (cut == std::string::npos || enc > cut)) {
    cut = enc + 2;
  }
  std::string name = cut == std::string::npos ? p : p.substr(cut + 1);
  // what no file name may hold
  for (char& c : name) {
    if (std::strchr("<>:\"/\\|?*", c) != nullptr || static_cast<unsigned char>(c) < 32) {
      c = '_';
    }
  }
  return name.empty() ? std::string("mod") : name;
}

std::string stem(const std::string& name) {
  const size_t dot = name.rfind('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool isDir(const std::string& path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

bool exists(const std::string& path) { return SDL_GetPathInfo(path.c_str(), nullptr); }

std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> names;
  SDL_EnumerateDirectory(
      dir.c_str(),
      [](void* user, const char*, const char* name) -> SDL_EnumerationResult {
        static_cast<std::vector<std::string>*>(user)->emplace_back(name);
        return SDL_ENUM_CONTINUE;
      },
      &names);
  std::sort(names.begin(), names.end());
  return names;
}

// a Dolphin texture pack's file name (tex1_...png or .dds)
bool textureName(const std::string& name) {
  const std::string l = lower(baseName(name));
  return l.rfind("tex1_", 0) == 0 && (endsWith(l, ".png") || endsWith(l, ".dds"));
}

std::string jsonText(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += static_cast<unsigned char>(c) < 32 ? ' ' : c;
  }
  return out;
}

std::string slug(const std::string& name) {
  std::string out;
  for (char c : lower(name)) {
    out += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
  }
  return out;
}

std::string format(const char* english, ...) {
  char text[600];
  va_list args;
  va_start(args, english);
  std::vsnprintf(text, sizeof(text), portTr(english), args);
  va_end(args);
  return text;
}

void report(bool ok, const std::string& text, unsigned kinds = 0) {
  std::lock_guard lock(sLock);
  sResults.push_back({ok, text, SDL_GetTicks()});
  if (sResults.size() > 8) {
    sResults.erase(sResults.begin());
  }
  if (ok) {
    sFinishedKinds |= kinds;
    sFinished++;
  }
}

void setProgress(uint64_t done, uint64_t total) {
  std::lock_guard lock(sLock);
  sProgress.done = done;
  if (total != 0) {
    sProgress.total = total;
  }
}

// --- copying, with progress ---

// (links and junctions inside a dropped folder are left out, not followed: one to a parent
// folder would recurse until the stack or the disk gave out; and no deeper than kMaxTreeDepth)
uint64_t treeSize(const std::string& dir, int depth = 0) {
  uint64_t size = 0;
  if (depth > portfile::kMaxTreeDepth) {
    return size;
  }
  for (const std::string& name : listDir(dir)) {
    const std::string path = dir + "/" + name;
    SDL_PathInfo info; // (a folder's entry, not a picked path: testpass.py lint)
    if (!portfile::isLink(path) && SDL_GetPathInfo(path.c_str(), &info)) {
      size += info.type == SDL_PATHTYPE_DIRECTORY ? treeSize(path, depth + 1) : info.size;
    }
  }
  return size;
}

bool copyFile(const std::string& from, const std::string& to, uint64_t& done, std::string& why) {
  SDL_IOStream* in = SDL_IOFromFile(from.c_str(), "rb");
  if (in == nullptr) {
    why = format("can't read %s: %s", baseName(from).c_str(), SDL_GetError());
    return false;
  }
  SDL_IOStream* out = SDL_IOFromFile(to.c_str(), "wb");
  if (out == nullptr) {
    why = format("can't write %s: %s", to.c_str(), SDL_GetError());
    SDL_CloseIO(in);
    return false;
  }
  std::vector<uint8_t> buf(1 << 20);
  bool ok = true;
  for (;;) {
    const size_t n = SDL_ReadIO(in, buf.data(), buf.size());
    if (n == 0) {
      ok = SDL_GetIOStatus(in) == SDL_IO_STATUS_EOF;
      break;
    }
    if (SDL_WriteIO(out, buf.data(), n) != n) {
      ok = false;
      break;
    }
    done += n;
    setProgress(done, 0);
  }
  ok = SDL_CloseIO(out) && ok;
  SDL_CloseIO(in);
  if (!ok) {
    why = format("can't copy %s: %s", baseName(from).c_str(), SDL_GetError());
    SDL_RemovePath(to.c_str());
  }
  return ok;
}

bool copyTree(const std::string& from, const std::string& to, uint64_t& done, std::string& why, int depth = 0) {
  if (depth > portfile::kMaxTreeDepth) {
    why = format("%s has folders too deep inside it", baseName(from).c_str());
    return false;
  }
  if (!SDL_CreateDirectory(to.c_str())) {
    why = format("can't make the folder %s: %s", to.c_str(), SDL_GetError());
    return false;
  }
  for (const std::string& name : listDir(from)) {
    const std::string a = from + "/" + name, b = to + "/" + name;
    if (portfile::isLink(a)) {
      continue; // (a link or junction: left out, never followed)
    }
    if (isDir(a) ? !copyTree(a, b, done, why, depth + 1) : !copyFile(a, b, done, why)) {
      return false;
    }
  }
  return true;
}

// (a link or junction inside is removed itself, never emptied through: what it points to stays)
bool removeTree(const std::string& path) {
  if (isDir(path) && !portfile::isLink(path)) {
    for (const std::string& name : listDir(path)) {
      removeTree(path + "/" + name);
    }
  }
  return SDL_RemovePath(path.c_str());
}

// --- where things go ---

// A name in dir that isn't taken: name, else "stem (2).ext"...
std::string freeName(const std::string& dir, const std::string& name, bool file) {
  if (!exists(dir + "/" + name)) {
    return name;
  }
  const size_t dot = file ? name.rfind('.') : std::string::npos;
  const std::string base = dot == std::string::npos ? name : name.substr(0, dot);
  const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
  for (int n = 2;; n++) {
    const std::string next = base + " (" + std::to_string(n) + ")" + ext;
    if (!exists(dir + "/" + next)) {
      return next;
    }
  }
}

// The installed mod (folder or file in the mods folder) with this id, or "".
std::string installedWithId(const std::string& id, std::string* version) {
  const std::string dir = modsDir();
  for (const std::string& name : listDir(dir)) {
    const std::string path = dir + "/" + name;
    const bool zip = endsWith(name, ".ttydmod") || endsWith(name, ".zip");
    if (!zip && !isDir(path)) {
      continue;
    }
    const PortModProbe probe = portModsProbe(path, zip);
    if ((probe.mod && probe.id == id) || (probe.pack && !id.empty() && id == "pack:" + name)) {
      if (version != nullptr) {
        *version = probe.version;
      }
      return path;
    }
  }
  return {};
}

// An installed one being replaced: out of the mods folder into mods_old (kept, not deleted).
// the installed one moved to mods_old (*where: where it went)
bool retire(const std::string& path, std::string& why, std::string* where = nullptr) {
  const std::string old = userDir() + "/mods_old";
  SDL_CreateDirectory(old.c_str());
  const std::string to = old + "/" + freeName(old, baseName(path), !isDir(path));
  if (!SDL_RenamePath(path.c_str(), to.c_str())) {
    why = format("the one installed can't be moved aside (is it in use?): %s", SDL_GetError());
    return false;
  }
  if (where != nullptr) {
    *where = to;
  }
  return true;
}

// The staged copy (in mods_incoming) put in the mods folder as name; one with
// the same id first moved aside. The result's line.
void place(const std::string& staged, std::string name, const std::string& id, const std::string& shown,
           const std::string& version, unsigned kinds) {
  const std::string dir = modsDir();
  SDL_CreateDirectory(dir.c_str());
  std::string why, oldVersion;
  const std::string old = id.empty() ? std::string() : installedWithId(id, &oldVersion);
  std::string retired;
  if (!old.empty()) {
    if (!retire(old, why, &retired)) {
      removeTree(staged);
      report(false, format("%s: %s", shown.c_str(), why.c_str()));
      return;
    }
  }
  name = freeName(dir, name, !isDir(staged));
  if (!SDL_RenamePath(staged.c_str(), (dir + "/" + name).c_str())) {
    const std::string error = SDL_GetError();
    // the one installed put back where it was (it stays in the list as it was)
    const bool back = retired.empty() || SDL_RenamePath(retired.c_str(), old.c_str());
    report(false, format("%s: can't be put in the mods folder: %s%s", shown.c_str(), error.c_str(),
                         back ? "" : " (the one installed is in mods_old)"));
    removeTree(staged);
    return;
  }
  const std::string label = version.empty() ? shown : shown + " " + version;
  if (old.empty()) {
    report(true, format("Installed %s.", label.c_str()), kinds);
  } else if (!oldVersion.empty() && oldVersion != version) {
    report(true, format("Updated %s from %s to %s (the old one is in mods_old).", shown.c_str(), oldVersion.c_str(),
                        version.c_str()),
           kinds);
  } else {
    report(true, format("Replaced %s (the old one is in mods_old).", label.c_str()), kinds);
  }
}

std::string staging() {
  const std::string dir = userDir() + "/mods_incoming";
  SDL_CreateDirectory(dir.c_str());
  return dir;
}

// A folder made into a mod around its files: mod.json and what goes in sub
void writeManifest(const std::string& dir, const std::string& id, const std::string& name, const char* description) {
  const std::string json = "{\n  \"id\": \"" + jsonText(id) + "\",\n  \"name\": \"" + jsonText(name) +
                           "\",\n  \"description\": \"" + jsonText(description) + "\"\n}\n";
  SDL_SaveFile((dir + "/mod.json").c_str(), json.data(), json.size());
}

bool hasTextures(const std::string& dir, int depth = 0) {
  for (const std::string& name : listDir(dir)) {
    const std::string path = dir + "/" + name;
    if (portfile::isLink(path)) {
      continue; // (never walked through, as treeSize/copyTree/removeTree don't: a junction to a drive root)
    }
    if (isDir(path) ? depth < 6 && hasTextures(path, depth + 1) : textureName(name)) {
      return true;
    }
  }
  return false;
}

void installPath(const std::string& path, int depth);

// a folder: a mod, a language pack, a folder of mods, or a texture pack
void installFolder(const std::string& path, int depth) {
  const std::string name = baseName(path);
  const PortModProbe probe = portModsProbe(path, false);
  uint64_t done = 0;
  std::string why;
  if (probe.mod || probe.pack) {
    if (!probe.error.empty()) {
      report(false, format("%s: %s", name.c_str(), probe.error.c_str()));
      return;
    }
    setProgress(0, treeSize(path));
    const std::string staged = staging() + "/" + freeName(staging(), name, false);
    if (!copyTree(path, staged, done, why)) {
      removeTree(staged);
      report(false, format("%s: %s", name.c_str(), why.c_str()));
      return;
    }
    place(staged, name, probe.mod ? probe.id : "pack:" + name, probe.name.empty() ? name : probe.name, probe.version,
          probe.pack ? PORT_MOD_KIND_LANG : 0);
    return;
  }
  // a folder of mods: each one in it
  bool any = false;
  if (depth == 0) {
    for (const std::string& entry : listDir(path)) {
      const std::string sub = path + "/" + entry;
      const bool zip = endsWith(entry, ".ttydmod") || endsWith(entry, ".zip");
      if (zip || (isDir(sub) && (exists(sub + "/mod.json") || exists(sub + "/lang/pack.txt")))) {
        any = true;
        installPath(sub, depth + 1);
      }
    }
  }
  if (any) {
    return;
  }
  if (hasTextures(path)) {
    // a Dolphin texture pack's folder: a mod of its own, so it can be turned off and ordered
    setProgress(0, treeSize(path));
    const std::string staged = staging() + "/" + freeName(staging(), name, false);
    if (!SDL_CreateDirectory(staged.c_str()) || !copyTree(path, staged + "/textures", done, why)) {
      removeTree(staged);
      report(false, format("%s: %s", name.c_str(), why.c_str()));
      return;
    }
    writeManifest(staged, "local.textures." + slug(name), name, "A texture pack.");
    place(staged, name, "local.textures." + slug(name), name, "", PORT_MOD_KIND_TEXTURES);
    return;
  }
  report(false, format("%s: no mod here (no mod.json, language pack or texture pack).", name.c_str()));
}

// a .ttydmod or .zip (staged: a copy in mods_incoming): a mod, a language
// pack or a texture pack
void installZip(const std::string& staged, const std::string& name) {
  const PortModProbe probe = portModsProbe(staged, true);
  if (probe.mod) {
    if (!probe.error.empty()) {
      SDL_RemovePath(staged.c_str());
      report(false, format("%s: %s", name.c_str(), probe.error.c_str()));
      return;
    }
    place(staged, endsWith(name, ".zip") || endsWith(name, ".ttydmod") ? name : slug(probe.id) + ".ttydmod", probe.id,
          probe.name.empty() ? probe.id : probe.name, probe.version, 0);
    return;
  }
  std::vector<std::pair<std::string, uint32_t>> files;
  std::string why;
  if (!portModsZipList(staged, files, why)) {
    SDL_RemovePath(staged.c_str());
    report(false, format("%s: %s", name.c_str(), why.c_str()));
    return;
  }
  uint64_t total = 0;
  bool textures = false;
  for (const auto& [file, size] : files) {
    total += size;
    textures = textures || textureName(file);
  }
  const std::string folder = stem(name);
  const std::string out = staging() + "/" + freeName(staging(), folder, false);
  setProgress(0, total);
  const auto step = [](uint64_t done) {
    setProgress(done, 0);
    return true;
  };
  bool ok = false;
  std::string id;
  unsigned kinds = 0;
  if (probe.pack) {
    // language packs are read from folders: unpacked
    ok = portModsZipExtract(staged, probe.prefix, out, step, why);
    id = "pack:" + folder;
    kinds = PORT_MOD_KIND_LANG;
  } else if (textures) {
    ok = SDL_CreateDirectory(out.c_str()) && portModsZipExtract(staged, probe.prefix, out + "/textures", step, why);
    if (ok) {
      id = "local.textures." + slug(folder);
      writeManifest(out, id, folder, "A texture pack.");
    }
    kinds = PORT_MOD_KIND_TEXTURES;
  } else {
    SDL_RemovePath(staged.c_str());
    report(false, format("%s: no mod in this zip (no mod.json, language pack or texture pack).", name.c_str()));
    return;
  }
  SDL_RemovePath(staged.c_str());
  if (!ok) {
    removeTree(out);
    report(false, format("%s: %s", name.c_str(), why.c_str()));
    return;
  }
  place(out, folder, id, folder, "", kinds);
}

void installPath(const std::string& given, int depth) {
  const std::string path = slashes(given);
  const std::string name = baseName(path);
  {
    std::lock_guard lock(sLock);
    sProgress.item = name;
    sProgress.done = sProgress.total = 0;
  }
  const std::string mods = slashes(modsDir());
  if (lower(path).rfind(lower(mods) + "/", 0) == 0) {
    report(false, format("%s is in the mods folder already.", name.c_str()));
    return;
  }
  if (isDir(path)) {
    installFolder(path, depth);
    return;
  }
  uint64_t done = 0;
  std::string why;
  if (endsWith(name, ".lua")) {
    // one Lua file: a Lua mod around it
    const std::string staged = staging() + "/" + freeName(staging(), stem(name), false);
    const std::string id = "local.lua." + slug(stem(name));
    if (!SDL_CreateDirectory((staged + "/lua").c_str()) || !copyFile(path, staged + "/lua/main.lua", done, why)) {
      removeTree(staged);
      report(false, format("%s: %s", name.c_str(), why.c_str()));
      return;
    }
    writeManifest(staged, id, stem(name), "A Lua mod.");
    place(staged, stem(name), id, stem(name), "", 0);
    return;
  }
  if (textureName(name)) {
    const std::string dir = userDir() + "/textures";
    SDL_CreateDirectory(dir.c_str());
    if (!copyFile(path, dir + "/" + name, done, why)) {
      report(false, format("%s: %s", name.c_str(), why.c_str()));
      return;
    }
    report(true, format("%s is in the textures folder: it shows at once.", name.c_str()), PORT_MOD_KIND_TEXTURES);
    return;
  }
  static const char* const kDiscs[] = {".iso", ".gcm", ".ciso", ".gcz", ".rvz", ".wia", ".wbfs", ".nfs", ".tgc"};
  for (const char* ext : kDiscs) {
    if (endsWith(name, ext)) {
      report(false, format("%s is a disc image, not a mod: add it on the Play tab.", name.c_str()));
      return;
    }
  }
  // a zip, or (Android) a content URI that may be one: copied first, then read
  uint64_t size = 0;
  if (portfile::pickedInfo(path, &size)) {
    setProgress(0, size);
  }
  const std::string staged = staging() + "/" + freeName(staging(), name, true);
  if (!copyFile(path, staged, done, why)) {
    report(false, format("%s: %s", name.c_str(), why.c_str()));
    return;
  }
  installZip(staged, name);
}

void worker() {
  for (;;) {
    std::string next;
    {
      std::lock_guard lock(sLock);
      if (sQueue.empty()) {
        sProgress = {};
        sWorker = false;
        return;
      }
      next = sQueue.front();
      sQueue.pop_front();
      sProgress.busy = true;
      sProgress.waiting = static_cast<int>(sQueue.size());
    }
    installPath(next, 0);
    portReleasePickedFolder(next.c_str()); // (iOS: a picked folder's access, kept until its copy is done)
#if defined(__APPLE__) && TARGET_OS_IPHONE
    portIosPickedDone(next.c_str()); // (a picked file's copy in tmp/: copied in, so gone)
#endif
  }
}

void SDLCALL picked(void*, const char* const* files, int) {
  if (files == nullptr) { // (the dialog couldn't show: said in the results, with the mods folder)
    char why[1200];
    report(false, portPickerError(why, sizeof(why), modsDir().c_str()));
    return;
  }
  for (int i = 0; files[i] != nullptr; i++) {
    modmgr::install(files[i]);
  }
}

// SDL's drop events: in the launcher and over the game. An invite link
// (papercut:) is online play's (invite.cpp).
int dropEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  switch (e->type) {
  case SDL_EVENT_DROP_BEGIN:
  case SDL_EVENT_DROP_POSITION:
    sDragging = true;
    return 0;
  case SDL_EVENT_DROP_COMPLETE:
    sDragging = false;
    return 0;
  case SDL_EVENT_DROP_FILE:
    if (e->drop.data == nullptr || SDL_strncasecmp(e->drop.data, "papercut:", 9) == 0 ||
        portSaveManagerTakesDrop(e->drop.data) || // a save or memory card: the Saves tab's (save_manager.cpp)
        portPm64LinkIsRom(e->drop.data)) {         // a Paper Mario ROM: linked (pm64_link.cpp)
      return 0;
    }
    sDragging = false;
    sDroppedAt = SDL_GetTicks();
    modmgr::install(e->drop.data);
    if (!gPortLauncherShown) {
      portSettingsOpenAt("Mods"); // over the game: the manager shows what came in
    }
    return 1;
  default:
    return 0;
  }
}

// Each frame over the game: a minute into play, safe mode's "this start is
// under way" file goes (port/include/port/mods.h).
void frame(int) {
  static Uint64 sStart;
  static bool sDone;
  if (sDone || gPortLauncherShown) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (sStart == 0) {
    sStart = now;
  } else if (now - sStart > 60000) {
    sDone = true;
    portModsStartedOk();
  }
}

struct Register {
  Register() { portSettingsAddOverlay(frame, dropEvent); }
} sRegister;

} // namespace

namespace modmgr {

void install(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::lock_guard lock(sLock);
  sQueue.push_back(path);
  if (!sWorker) {
    sWorker = true;
    sProgress.busy = true;
    portDetach(worker);
  } else {
    sProgress.waiting = static_cast<int>(sQueue.size());
  }
}

bool canPickFiles() {
  return true; // (iOS: the system's document picker, portShowOpenFileDialog; each mod copied in)
}

bool canPickFolder() {
#if defined(__ANDROID__) // (iOS: the document picker's folder, opened in place: portShowOpenFolderDialog)
  return false;
#else
  return true;
#endif
}

void pickFiles() {
  static const SDL_DialogFileFilter kFilters[] = {
      {"Mods (.ttydmod, .zip, .lua)", "ttydmod;zip;lua"},
      {"All files", "*"},
  };
  portShowOpenFileDialog(picked, nullptr, SDL_GetKeyboardFocus(), kFilters, 2, nullptr, true);
}

void pickFolder() { portShowOpenFolderDialog(picked, nullptr, SDL_GetKeyboardFocus(), nullptr, false); }

Progress progress() {
  std::lock_guard lock(sLock);
  return sProgress;
}

std::vector<Result> results() {
  std::lock_guard lock(sLock);
  return sResults;
}

void clearResults() {
  std::lock_guard lock(sLock);
  sResults.clear();
}

bool poll() {
  if (sFinished.exchange(0) == 0) {
    return false;
  }
  listChanged(sFinishedKinds.exchange(0) | PORT_MOD_KIND_LANG); // (a mod may carry a lang/ too)
  portModsReload();
  return true;
}

bool dragging() { return sDragging; }

uint64_t droppedAt() { return sDroppedAt; }

void listChanged(unsigned kinds) {
  if (kinds & PORT_MOD_KIND_LANG) {
    portTrReloadPacks();
  }
}

} // namespace modmgr
