// portfile::isLink (port/settings/file_util.h): a symbolic link, or on Windows a junction, which
// the folder copies and removals of port/settings/features never follow (a link to a parent
// folder would recurse without end; a removal through one would empty what it points to).
#include "../file_util.h"

#ifdef __ANDROID__
#include "port/android_pc.h" // the file picker's content:// URIs
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace portfile {

bool isLink(const std::string& path) {
#ifdef _WIN32
  const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (n <= 0) {
    return false;
  }
  std::wstring wide(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
  WIN32_FIND_DATAW found;
  const HANDLE h = FindFirstFileW(wide.c_str(), &found);
  if (h == INVALID_HANDLE_VALUE) {
    return false;
  }
  FindClose(h);
  // (only links and junctions: a cloud folder's placeholders are reparse points too)
  return (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
         (found.dwReserved0 == IO_REPARSE_TAG_SYMLINK || found.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
#else
  struct stat st;
  return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
#endif
}

bool pickedInfo(const std::string& path, uint64_t* size) {
  if (size != nullptr) {
    *size = 0;
  }
#ifdef __ANDROID__
  if (PORT_ANDROID_IS_CONTENT_URI(path.c_str())) {
    unsigned long long n = 0;
    if (!portAndroidContentInfo(path.c_str(), nullptr, 0, &n)) {
      return false;
    }
    if (size != nullptr) {
      *size = n;
    }
    return true;
  }
#endif
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
    return false;
  }
  if (size != nullptr) {
    *size = info.size;
  }
  return true;
}

bool readPicked(const std::string& path, uint64_t maxBytes, std::vector<uint8_t>& out) {
  out.clear();
  uint64_t size = 0;
  if (!pickedInfo(path, &size) || size > maxBytes) {
    return false;
  }
  SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
  if (io == nullptr) {
    return false;
  }
  // (in steps, stopping past the cap: a size the system didn't tell, or a file grown since the check,
  // is still never read whole)
  bool ok = true;
  std::vector<uint8_t> step(64 * 1024);
  for (;;) {
    const size_t got = SDL_ReadIO(io, step.data(), step.size());
    if (got == 0) {
      ok = SDL_GetIOStatus(io) == SDL_IO_STATUS_EOF;
      break;
    }
    if (got > maxBytes - out.size()) {
      ok = false;
      break;
    }
    out.insert(out.end(), step.begin(), step.begin() + static_cast<std::ptrdiff_t>(got));
  }
  SDL_CloseIO(io);
  if (!ok) {
    out.clear();
  }
  return ok;
}

} // namespace portfile
