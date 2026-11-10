// This build's version for replays and online co-op (port/include/port/build_id.h).
#include "port/build_id.h"

#include <cstdlib>
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

#ifdef PORT_BUILD_HEADLESS
// The headless build plays the same code but not quite the same game as Aurora's (the button names'
// widths in the text, only a few settings, no PM64 bosses or Director's Cut groups, no point clamp in
// the projection): it says so in its version, so a room or a replay refuses a mix of the two with the
// version in its note. $TTYD_MIX_BACKENDS=1 (tests: tests/port/testpass/walk_xplat_3p_online.json)
// leaves the tag off, so headless and Aurora games play together anyway.
bool mixAllowed() {
  const char* env = std::getenv("TTYD_MIX_BACKENDS");
  return env != nullptr && env[0] == '1';
}
#endif

} // namespace

extern "C" const char* portBuildVersion(void) {
  static const std::string version = [] {
    std::string v = release(PORT_VERSION) || portBuildIdText[0] == '\0'
                        ? std::string(PORT_VERSION)
                        : std::string(PORT_VERSION) + "+" + portBuildIdText;
#ifdef PORT_BUILD_HEADLESS
    if (!mixAllowed()) {
      v += " (headless build)";
    }
#endif
    return v;
  }();
  return version.c_str();
}
