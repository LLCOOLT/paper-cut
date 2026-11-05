// The modes' session data (port/include/port/online_session.h): the named
// blocks the host's setup carries (online.cpp's extension block, flag 8).
#include "port/online_session.h"

namespace {

struct Block {
  const char* name;
  int (*get)(char* out, int size);
  void (*put)(const char* data, int len);
};

const Block kBlocks[] = {
    {"enemies", portBattlexSessionGet, portBattlexSessionPut},
    {"cards", portCardsSessionGet, portCardsSessionPut},
    {"stickers", portStickersSessionGet, portStickersSessionPut},
};
constexpr int kCount = static_cast<int>(sizeof(kBlocks) / sizeof(kBlocks[0]));

} // namespace

extern "C" int portOnlineSessionCount(void) { return kCount; }

extern "C" const char* portOnlineSessionName(int i) { return i >= 0 && i < kCount ? kBlocks[i].name : ""; }

extern "C" int portOnlineSessionGet(int i, char* out, int size) {
  if (i < 0 || i >= kCount || out == nullptr || size <= 0) {
    return -1;
  }
  return kBlocks[i].get(out, size);
}

extern "C" void portOnlineSessionPut(int i, const char* data, int len) {
  if (i >= 0 && i < kCount) {
    kBlocks[i].put(data, data != nullptr && len > 0 ? len : 0);
  }
}
