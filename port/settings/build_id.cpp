// This build's version for replays and online co-op (port/include/port/build_id.h).
#include "port/build_id.h"

#include <cstring>
#include <string>

extern "C" const char portBuildIdText[]; // generated/build_id.c (cmake/build_id.cmake)

namespace {

// a release's version: digits and dots only ("0.4.0")
bool release(const char* v) {
  if (v[0] == '\0') {
    return false;
  }
  for (const char* p = v; *p != '\0'; p++) {
    if ((*p < '0' || *p > '9') && *p != '.') {
      return false;
    }
  }
  return true;
}

} // namespace

extern "C" const char* portBuildVersion(void) {
  static const std::string version = release(PORT_VERSION) || portBuildIdText[0] == '\0'
                                         ? std::string(PORT_VERSION)
                                         : std::string(PORT_VERSION) + "+" + portBuildIdText;
  return version.c_str();
}
