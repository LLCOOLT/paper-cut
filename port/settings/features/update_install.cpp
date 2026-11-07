// port/include/port/update_install.h: a downloaded release put in place.
//
//   <install>/update/new/...   the release's files, unpacked (its one top
//                              folder dropped), for the next start
//   <install>/update/ready     the version, once new/ is complete
//   <install>/update/old/...   what they replaced, removed the start after
//
// <install> is the executable's folder (Windows, the Linux zip); on macOS the
// app bundle is what's replaced and update/ is ".papercut-update" beside it.
// The running program can't overwrite itself on Windows, but it can be moved
// aside: at the next start, before anything else, each of new/'s entries
// moves the install's own of that name to old/ and takes its place, then the
// program starts again from the new files. A move that fails puts back the
// ones done, so the install is never half one version and half another.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include "port/update_install.h"
#include "port/mods.h"
#include <SDL3/SDL.h>
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#endif
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__EMSCRIPTEN__)
#define UPDATE_NONE 1
#endif

namespace port_update {

namespace {

bool exists(const std::string& path, SDL_PathType* type = nullptr) {
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path.c_str(), &info)) {
    return false;
  }
  if (type != nullptr) {
    *type = info.type;
  }
  return true;
}

std::string trimSlash(std::string path) {
  while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
    path.pop_back();
  }
  return path;
}

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// What an update replaces: the executable's folder, or on macOS the app bundle
std::string installRoot() {
  const char* base = SDL_GetBasePath();
  std::string root = base != nullptr ? trimSlash(base) : std::string();
#ifdef __APPLE__
  const size_t app = root.rfind(".app/Contents");
  root = app == std::string::npos ? std::string() : root.substr(0, app + 4);
#endif
  return root;
}

std::string readText(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::string text = data != nullptr ? std::string(static_cast<char*>(data), size) : std::string();
  SDL_free(data);
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
    text.pop_back();
  }
  return text;
}

bool writeText(const std::string& path, const std::string& text) {
  return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

std::vector<std::string> entries(const std::string& dir) {
  std::vector<std::string> names;
  SDL_EnumerateDirectory(
      dir.c_str(),
      [](void* user, const char*, const char* name) {
        static_cast<std::vector<std::string>*>(user)->push_back(name);
        return SDL_ENUM_CONTINUE;
      },
      &names);
  std::sort(names.begin(), names.end());
  return names;
}

void removeTree(const std::string& path) {
  SDL_PathType type = SDL_PATHTYPE_NONE;
  if (!exists(path, &type)) {
    return;
  }
  if (type == SDL_PATHTYPE_DIRECTORY) {
    for (const std::string& name : entries(path)) {
      removeTree(path + "/" + name);
    }
  }
  SDL_RemovePath(path.c_str());
}

// --- SHA-256 (FIPS 180-4) ---

struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64];
  size_t used = 0;
  uint64_t bits = 0;

  static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 |
             block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
  }

  void add(const uint8_t* data, size_t size) {
    bits += uint64_t(size) * 8;
    while (size > 0) {
      const size_t n = std::min(size, 64 - used);
      std::memcpy(block + used, data, n);
      used += n;
      data += n;
      size -= n;
      if (used == 64) {
        compress();
        used = 0;
      }
    }
  }

  std::string hex() {
    const uint64_t total = bits;
    const uint8_t one = 0x80, zero = 0;
    add(&one, 1);
    while (used != 56) {
      add(&zero, 1);
    }
    uint8_t length[8];
    for (int i = 0; i < 8; i++) {
      length[i] = uint8_t(total >> (56 - 8 * i));
    }
    add(length, 8);
    char out[65];
    for (int i = 0; i < 8; i++) {
      std::snprintf(out + i * 8, 9, "%08x", h[i]);
    }
    return out;
  }
};

// --- the zip (stored or deflated entries, no zip64) ---

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16)) | (uint32_t(p[3]) << 24); }

struct Entry {
  std::string name;
  uint16_t method, madeBy;
  uint32_t crc, compressed, size, local, attributes;
};

bool readDirectory(SDL_IOStream* io, std::vector<Entry>& out, std::string& why) {
  const Sint64 fileSize = SDL_GetIOSize(io);
  const Sint64 tail = std::min<Sint64>(fileSize, 22 + 65535);
  std::vector<uint8_t> buf(size_t(tail > 0 ? tail : 0));
  if (tail < 22 || SDL_SeekIO(io, fileSize - tail, SDL_IO_SEEK_SET) < 0 ||
      SDL_ReadIO(io, buf.data(), buf.size()) != buf.size()) {
    why = "the download isn't a zip";
    return false;
  }
  Sint64 eocd = -1;
  for (Sint64 i = Sint64(buf.size()) - 22; i >= 0; i--) {
    if (le32(&buf[size_t(i)]) == 0x06054B50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) {
    why = "the download isn't a zip";
    return false;
  }
  const uint8_t* e = &buf[size_t(eocd)];
  const uint32_t count = le16(e + 10), cdSize = le32(e + 12), cdOffset = le32(e + 16);
  std::vector<uint8_t> cd(cdSize);
  if (cdOffset == 0xFFFFFFFF || SDL_SeekIO(io, cdOffset, SDL_IO_SEEK_SET) < 0 ||
      SDL_ReadIO(io, cd.data(), cd.size()) != cd.size()) {
    why = "the zip is damaged";
    return false;
  }
  size_t at = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (at + 46 > cd.size() || le32(&cd[at]) != 0x02014B50) {
      why = "the zip is damaged";
      return false;
    }
    const uint8_t* h = &cd[at];
    Entry entry{std::string(), le16(h + 10), le16(h + 4), le32(h + 16), le32(h + 20), le32(h + 24), le32(h + 42),
                le32(h + 38)};
    const uint16_t nameLen = le16(h + 28);
    const size_t next = at + 46 + nameLen + le16(h + 30) + le16(h + 32);
    if (next > cd.size()) {
      why = "the zip is damaged";
      return false;
    }
    entry.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
    std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
    at = next;
    if ((le16(h + 8) & 1) != 0 || (entry.method != 0 && entry.method != 8) || entry.size == 0xFFFFFFFF) {
      why = "the zip has an entry this can't unpack";
      return false;
    }
    out.push_back(std::move(entry));
  }
  return true;
}

bool safeName(const std::string& name) {
  if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos) {
    return false;
  }
  size_t at = 0;
  while (at <= name.size()) {
    const size_t slash = std::min(name.find('/', at), name.size());
    const std::string part = name.substr(at, slash - at);
    if (part == "..") {
      return false;
    }
    at = slash + 1;
  }
  return true;
}

bool extract(SDL_IOStream* io, const Entry& entry, const std::string& to, std::string& why) {
  uint8_t header[30];
  std::vector<uint8_t> data(entry.compressed);
  if (SDL_SeekIO(io, entry.local, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, header, 30) != 30 ||
      le32(header) != 0x04034B50 ||
      SDL_SeekIO(io, Sint64(entry.local) + 30 + le16(header + 26) + le16(header + 28), SDL_IO_SEEK_SET) < 0 ||
      SDL_ReadIO(io, data.data(), data.size()) != data.size()) {
    why = "the zip is damaged";
    return false;
  }
  std::vector<uint8_t> out;
  if (entry.method == 0) {
    out = std::move(data);
  } else {
    out.resize(entry.size);
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
      why = "out of memory";
      return false;
    }
    z.next_in = data.data();
    z.avail_in = uInt(data.size());
    z.next_out = out.data();
    z.avail_out = uInt(out.size());
    const int result = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (result != Z_STREAM_END || z.total_out != entry.size) {
      why = "the zip is damaged";
      return false;
    }
  }
  if (crc32(0, out.data(), uInt(out.size())) != entry.crc) {
    why = "the zip is damaged (a file's checksum)";
    return false;
  }
  SDL_CreateDirectory(parentOf(to).c_str());
#if !defined(_WIN32)
  const unsigned mode = (entry.madeBy >> 8) == 3 ? entry.attributes >> 16 : 0;
  if ((mode & 0170000) == 0120000) { // a symbolic link (a macOS framework's), its target the contents
    const std::string target(out.begin(), out.end());
    if (target.empty() || target[0] == '/' || symlink(target.c_str(), to.c_str()) != 0) {
      why = "a link in the zip can't be made";
      return false;
    }
    return true;
  }
#endif
  if (!SDL_SaveFile(to.c_str(), out.data(), out.size())) {
    why = std::string("can't write beside the install (") + SDL_GetError() + ")";
    return false;
  }
#if !defined(_WIN32)
  if ((mode & 0777) != 0) {
    chmod(to.c_str(), mode & 0777);
  }
#endif
  return true;
}

// This program's own executable
std::string selfExe() {
#ifdef _WIN32
  static wchar_t path[32768];
  const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(sizeof(path) / sizeof(path[0])));
  if (n == 0 || n >= sizeof(path) / sizeof(path[0])) {
    return std::string();
  }
  const int len = WideCharToMultiByte(CP_UTF8, 0, path, int(n), nullptr, 0, nullptr, nullptr);
  std::string out(size_t(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, path, int(n), out.data(), len, nullptr, nullptr);
  return out;
#elif defined(__APPLE__)
  char path[4096];
  uint32_t size = sizeof(path);
  char real[PATH_MAX];
  return _NSGetExecutablePath(path, &size) == 0 && realpath(path, real) != nullptr ? std::string(real) : std::string();
#else
  char path[4096];
  const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
  return n > 0 ? std::string(path, size_t(n)) : std::string();
#endif
}

bool samePath(std::string a, std::string b) {
  a = trimSlash(a);
  b = trimSlash(b);
  std::replace(a.begin(), a.end(), '\\', '/');
  std::replace(b.begin(), b.end(), '\\', '/');
#ifdef _WIN32
  for (std::string* s : {&a, &b}) {
    for (char& c : *s) {
      c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
  }
#endif
  return a == b;
}

// path is the user folder or holds it (saves, settings, mods: never moved)
bool holdsUserFolder(const std::string& path) {
  std::vector<std::string> users;
  if (const char* dir = SDL_getenv("TTYD_USER_DIR")) {
    users.push_back(dir);
  }
#ifndef UPDATE_NONE
  if (char* pref = SDL_GetPrefPath(nullptr, "ttyd")) {
    users.push_back(pref);
    SDL_free(pref);
  }
#endif
  for (std::string user : users) {
    user = trimSlash(user);
    for (std::string at = user; !at.empty() && at != parentOf(at); at = parentOf(at)) {
      if (samePath(at, path)) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

std::string assetName(const std::string& version) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  // one app per game version (tools/port/release.py's names)
  const std::string id = PORT_GAME_ID;
  const char* game = id == "G8MJ01" ? "jp" : id == "G8MP01" ? "eu" : "us";
#ifdef __ANDROID__
  return "papercut-" + version + "-" + game + "-android-arm64.apk";
#else
  return "papercut-" + version + "-" + game + "-ios-unsigned.ipa";
#endif
#endif
#if defined(_WIN32)
  const char* os = "windows";
#elif defined(__APPLE__)
  const char* os = "macos";
#else
  const char* os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  const char* arch = "arm64";
#else
  const char* arch = "x64";
#endif
  return "papercut-" + version + "-" + os + "-" + arch + ".zip";
}

std::string stageDir() {
  const std::string root = installRoot();
  if (root.empty()) {
    return std::string();
  }
#ifdef __APPLE__
  return parentOf(root) + "/.papercut-update";
#else
  return root + "/update";
#endif
}

Mode mode(std::string* why) {
#ifdef UPDATE_NONE
#if defined(__ANDROID__)
  (void)why;
  return Mode::Android; // (update_check.cpp: the APK to Android's installer)
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  (void)why;
  return Mode::AltStore; // an app can't install itself: AltStore's or SideStore's source
#else
  if (why != nullptr) {
    *why = "the browser build is always the newest";
  }
  return Mode::Elsewhere;
#endif
#else
#if defined(__linux__)
  if (exists("/.flatpak-info")) {
    return Mode::Flatpak;
  }
  if (SDL_getenv("APPIMAGE") != nullptr) {
    return Mode::AppImage;
  }
#endif
  static int sChecked = -1; // the install's folder can be written: checked once
  static std::string sWhy;
  if (sChecked < 0) {
    const std::string root = installRoot(), stage = stageDir();
#if defined(_WIN32)
    const bool release = !root.empty() && exists(root + "/ttyd-us.exe");
#elif defined(__APPLE__)
    const bool release = !root.empty();
#else
    const bool release = !root.empty() && exists(root + "/ttyd-us");
#endif
    const std::string probe = stage + "/probe";
    if (!release) {
      sWhy = "this isn't a release's install";
      sChecked = 0;
    } else if (!SDL_CreateDirectory(stage.c_str()) || !writeText(probe, "ok")) {
      sWhy = "the install's folder can't be written";
      sChecked = 0;
    } else {
      SDL_RemovePath(probe.c_str());
      sChecked = 1;
    }
  }
  if (why != nullptr) {
    *why = sWhy;
  }
  return sChecked == 1 ? Mode::Install : Mode::Elsewhere;
#endif
}

std::string sha256File(const std::string& path) {
  SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
  if (io == nullptr) {
    return std::string();
  }
  Sha256 sha;
  static thread_local uint8_t chunk[65536];
  size_t n;
  while ((n = SDL_ReadIO(io, chunk, sizeof(chunk))) > 0) {
    sha.add(chunk, n);
  }
  SDL_CloseIO(io);
  return sha.hex();
}

bool stage(const std::string& zip, const std::string& version, std::string& why) {
  const std::string dir = stageDir();
  if (dir.empty()) {
    why = "this isn't a release's install";
    return false;
  }
  SDL_RemovePath((dir + "/ready").c_str());
  removeTree(dir + "/new");
  SDL_IOStream* io = SDL_IOFromFile(zip.c_str(), "rb");
  if (io == nullptr) {
    why = "the download can't be read";
    return false;
  }
  std::vector<Entry> list;
  bool ok = readDirectory(io, list, why);
  // the zip's one top folder ("papercut-0.3.0-windows-x64/") is dropped
  std::string top;
  for (const Entry& e : list) {
    const size_t slash = e.name.find('/');
    const std::string first = slash == std::string::npos ? std::string() : e.name.substr(0, slash + 1);
    if (first.empty() || (!top.empty() && first != top)) {
      top.clear();
      break;
    }
    top = first;
  }
  int files = 0;
  for (size_t i = 0; ok && i < list.size(); i++) {
    const std::string name = list[i].name.substr(top.size());
    if (name.empty() || name.back() == '/') {
      continue; // a folder: made with its files
    }
    if (!safeName(name)) {
      why = "the zip has a file outside its folder";
      ok = false;
      break;
    }
    ok = extract(io, list[i], dir + "/new/" + name, why);
    files++;
  }
  SDL_CloseIO(io);
  if (ok && files == 0) {
    why = "the zip is empty";
    ok = false;
  }
  if (ok && !writeText(dir + "/ready", version)) {
    why = "can't write beside the install";
    ok = false;
  }
  if (!ok) {
    removeTree(dir + "/new");
  }
  return ok;
}

std::string staged() {
  const std::string dir = stageDir();
  return dir.empty() ? std::string() : readText(dir + "/ready");
}

} // namespace port_update

extern "C" void portUpdateApplyStaged(int argc, char** argv) {
#ifdef UPDATE_NONE
  (void)argc;
  (void)argv;
#else
  using namespace port_update;
  const std::string dir = stageDir();
  if (dir.empty()) {
    return;
  }
  removeTree(dir + "/old"); // the last update's (on Windows, once that program has ended)
  const std::string version = readText(dir + "/ready");
  if (version.empty()) {
    return;
  }
  SDL_RemovePath((dir + "/ready").c_str()); // tried once: a failure isn't tried at every start
  const std::string root = installRoot();
  // (new name, the install's path it replaces)
  std::vector<std::pair<std::string, std::string>> moves;
  for (const std::string& name : entries(dir + "/new")) {
#ifdef __APPLE__
    // the app alone (the zip's notes beside it stay out of /Applications), at the bundle's own path
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".app") == 0) {
      moves.emplace_back(name, root);
    }
#else
    moves.emplace_back(name, root + "/" + name);
#endif
  }
  std::vector<std::pair<std::string, bool>> done; // (index's target, had an old one)
  bool ok = !moves.empty();
  SDL_CreateDirectory((dir + "/old").c_str());
  for (size_t i = 0; ok && i < moves.size(); i++) {
    const std::string& to = moves[i].second;
    const std::string from = dir + "/new/" + moves[i].first, aside = dir + "/old/" + moves[i].first;
    if (samePath(to, dir) || holdsUserFolder(to)) {
      std::printf("[update] %s left as it is (the update's or the user's folder)\n", to.c_str());
      continue;
    }
    const bool had = exists(to);
    if (had && !SDL_RenamePath(to.c_str(), aside.c_str())) {
      std::fprintf(stderr, "[update] can't move %s aside: %s\n", to.c_str(), SDL_GetError());
      ok = false;
      break;
    }
    if (!SDL_RenamePath(from.c_str(), to.c_str())) {
      std::fprintf(stderr, "[update] can't move %s into place: %s\n", from.c_str(), SDL_GetError());
      if (had) {
        SDL_RenamePath(aside.c_str(), to.c_str());
      }
      ok = false;
      break;
    }
    done.emplace_back(moves[i].first, had);
  }
  if (!ok) {
    // put back what was moved: the install stays the version it was
    for (size_t i = done.size(); i-- > 0;) {
      const std::string to = std::find_if(moves.begin(), moves.end(), [&](const auto& m) {
                               return m.first == done[i].first;
                             })->second;
      SDL_RenamePath(to.c_str(), (dir + "/new/" + done[i].first).c_str());
      if (done[i].second) {
        SDL_RenamePath((dir + "/old/" + done[i].first).c_str(), to.c_str());
      }
    }
    std::fprintf(stderr, "[update] Paper Cut %s was not installed; this version goes on\n", version.c_str());
    writeText(dir + "/failed", version);
    return;
  }
  removeTree(dir + "/new");
  SDL_RemovePath((dir + "/failed").c_str());
  std::printf("[update] Paper Cut %s is in place (%zu entries); starting it\n", version.c_str(), done.size());
  std::fflush(stdout);
  // the new program, from the same path, with the same arguments
  const std::string exe = selfExe();
  std::vector<const char*> args{exe.c_str()};
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }
  args.push_back(nullptr);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_UPDATE_APPLIED", version.c_str(), true);
  SDL_Process* process = exe.empty() ? nullptr : SDL_CreateProcess(args.data(), false);
  if (process == nullptr) {
    std::fprintf(stderr, "[update] can't start the new version (%s): this one goes on\n", SDL_GetError());
    return;
  }
  SDL_DestroyProcess(process); // it runs on; this one ends
  portModsStartedOk();         // an update's restart isn't a failed start (safe mode, port/include/port/mods.h)
  std::_Exit(0);
#endif
}
