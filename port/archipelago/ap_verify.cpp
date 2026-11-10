// $TTYD_AP_VERIFY (ttyd_ap.h's portApVerifyTargets), on every backend: every
// REL target holds what the same word of the disc's REL holds (the RELs'
// data, as linked into the executable, before a seed changes it): the
// translation of the apworld's offsets checked (tools/port/gc_offsets.py).
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"
#include "ap_log.h"

#include "port/mods.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

extern "C" int portApVerifyTargets(void) {
  int ok = 0, bad = 0;
  std::map<std::string, std::vector<uint8_t>> rels;
  for (const TtydApLocation& l : kTtydApLocations) {
    for (int i = l.first; i < l.first + l.count; i++) {
      const TtydApTarget& t = kTtydApTargets[i];
      if (t.kind == TTYD_AP_LOWMEM || t.name[0] == '\0') continue;
      std::vector<uint8_t>& rel = rels[t.area];
      if (rel.empty()) {
        int size = 0;
        if (void* data = portApReadDiscFile(("rel/" + std::string(t.area) + ".rel").c_str(), &size)) {
          rel.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
          free(data);
        }
      }
      if (t.gcOffset + 4 > rel.size()) continue;
      const uint8_t* g = rel.data() + t.gcOffset;
      const long long want = int32_t(uint32_t(g[0]) << 24 | uint32_t(g[1]) << 16 | uint32_t(g[2]) << 8 | g[3]);
      std::string why;
      const uint8_t* base = static_cast<const uint8_t*>(portModsGameAddress(t.name, why));
      const long long v = base == nullptr ? -1
                          : t.kind == TTYD_AP_SCRIPT ? (long long)*reinterpret_cast<const intptr_t*>(base + t.offset)
                                                     : (long long)*reinterpret_cast<const int32_t*>(base + t.offset);
      // (a script word that is a pointer differs by design; item ids and prices don't)
      if (v == want) {
        ok++;
      } else {
        bad++;
        portApLog("archipelago: verify: %s: %s+0x%X is %lld, the disc's rel/%s.rel:0x%X %lld%s", l.name, t.name,
                  t.offset, v, t.area, t.gcOffset, want, base == nullptr ? (" (" + why + ")").c_str() : "");
      }
    }
  }
  portApLog("archipelago: verify: %d targets as the disc has them, %d not", ok, bad);
  return bad;
}
