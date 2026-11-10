// Whole-file reads and writes the settings page's features share (port/settings/features/*.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include "port/ios_pc.h" // portIosStorePath / portIosResolvePath
#endif

namespace portfile {

// A remembered path (the launcher's library.txt and last_disc.txt, and whatever reads them), as it is kept
// and back. iOS: relative to the app's container, which moves with a reinstall or an update (ios_pc.h; an
// older build's absolute path is found under today's container, or by its file name in Documents);
// elsewhere as it is.
inline std::string storePath(const std::string& path) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  char out[4096];
  portIosStorePath(path.c_str(), out, sizeof(out));
  return out;
#else
  return path;
#endif
}

inline std::string resolvePath(const std::string& stored) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  if (stored.empty()) {
    return stored;
  }
  char out[4096];
  portIosResolvePath(stored.c_str(), out, sizeof(out));
  return out;
#else
  return stored;
#endif
}

inline bool isSeparator(char c) { return c == '/' || c == '\\'; }

// path without its trailing separators (a root, "/" or "C:/", keeps its own)
inline std::string trimTrailing(std::string path) {
  while (path.size() > 1 && isSeparator(path.back()) && path[path.size() - 2] != ':') {
    path.pop_back();
  }
  return path;
}

// a symbolic link or a junction (features/file_links.cpp): never followed by a folder copy or removal
bool isLink(const std::string& path);

// A file the player picked (the system's file picker) or dropped (features/file_links.cpp). Android's
// picker gives content:// URIs, which SDL_GetPathInfo and stat() can't see though SDL_IOFromFile opens
// them (port/include/port/android_pc.h): every size or kind check on a picked or dropped path goes
// through these, never SDL_GetPathInfo. pickedInfo: true when it is a file that can be read, with its
// size (size may be null; 0 when the system doesn't tell it, which only a content URI can do).
bool pickedInfo(const std::string& path, uint64_t* size);
// its bytes, when it is a readable file of at most maxBytes; false otherwise (too big, a folder, gone).
// Never reads more than maxBytes + one step, even when the system didn't tell the size.
bool readPicked(const std::string& path, uint64_t maxBytes, std::vector<uint8_t>& out);

// how deep a folder copy goes at most (a mod's, the user folder's)
constexpr int kMaxTreeDepth = 32;

// dir and name joined by exactly one separator, whatever either has at the seam
inline std::string join(const std::string& dir, const std::string& name) {
  size_t from = 0;
  while (from < name.size() && isSeparator(name[from])) {
    from++;
  }
  if (dir.empty()) {
    return name.substr(from);
  }
  std::string out = trimTrailing(dir);
  if (!isSeparator(out.back())) {
    out += '/';
  }
  return out + name.substr(from);
}

// the file's bytes as a string ("" when it can't be read)
inline std::string readFile(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::string out;
  if (data != nullptr) {
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
  }
  return out;
}

// the file's bytes (none when it can't be read)
inline std::vector<uint8_t> readAll(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::vector<uint8_t> bytes;
  if (data != nullptr) {
    bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    SDL_free(data);
  }
  return bytes;
}

// How writeAll leaves the file. kSync (saves, cards, lists, settings: anything written on a
// player's action): on the disk before the rename, so a power cut just after can't leave an empty
// file in its place (as file_write_pc.c does on the C side). kNoSync, for small files written often
// or worth little (a text field's value): the rename alone keeps them whole against a crash; a
// physical flush each time would stall the caller on a slow disk or SD card.
enum Sync { kSync, kNoSync };

// f's bytes on the disk: Windows' SDL_FlushIO is already FlushFileBuffers; elsewhere SDL's stdio
// backend only flushes to the OS, so the descriptor is synced here
inline bool syncToDisk(SDL_IOStream* f) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
  (void)f;
  return true;
#else
  const SDL_PropertiesID props = SDL_GetIOProperties(f);
  int fd = static_cast<int>(SDL_GetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1));
  if (fd < 0) {
    if (FILE* fp = static_cast<FILE*>(SDL_GetPointerProperty(props, SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, nullptr))) {
      fd = fileno(fp);
    }
  }
  if (fd >= 0 && fsync(fd) != 0) {
    SDL_SetError("the file couldn't be synced to the disk");
    return false;
  }
  return true; // (no descriptor: a backend with nothing to sync)
#endif
}

// the file written whole or not at all: to path.tmp, then renamed over path (see Sync)
inline bool writeAll(const std::string& path, const void* data, size_t size, Sync sync = kSync) {
  SDL_IOStream* f = SDL_IOFromFile((path + ".tmp").c_str(), "wb");
  bool ok = f != nullptr && (size == 0 || SDL_WriteIO(f, data, size) == size) &&
            (sync == kNoSync || (SDL_FlushIO(f) && syncToDisk(f)));
  if (f != nullptr) {
    // (a full disk or a removed SD card shows up only here on the stdio-buffered backends: never renamed in)
    ok = SDL_CloseIO(f) && ok;
  }
  if (ok && SDL_RenamePath((path + ".tmp").c_str(), path.c_str())) {
    return true;
  }
  const std::string why = SDL_GetError(); // (the callers show it: not the clean-up's)
  SDL_RemovePath((path + ".tmp").c_str());
  SDL_SetError("%s", why.c_str());
  return false;
}

} // namespace portfile
