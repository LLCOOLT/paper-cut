// Whole-file reads and writes the settings page's features share (port/settings/features/*.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace portfile {

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

// the file written whole or not at all: to path.tmp, then renamed over path
inline bool writeAll(const std::string& path, const void* data, size_t size) {
  SDL_IOStream* f = SDL_IOFromFile((path + ".tmp").c_str(), "wb");
  const bool ok = f != nullptr && SDL_WriteIO(f, data, size) == size;
  if (f != nullptr) {
    SDL_CloseIO(f);
  }
  if (ok && SDL_RenamePath((path + ".tmp").c_str(), path.c_str())) {
    return true;
  }
  SDL_RemovePath((path + ".tmp").c_str());
  return false;
}

} // namespace portfile
