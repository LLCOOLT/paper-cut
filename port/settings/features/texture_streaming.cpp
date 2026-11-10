// Texture pack memory (setting texture_pack_memory, Graphics tab): how much of the GPU a texture pack's pictures
// may take and the largest size they load at, for Aurora's streaming of them (aurora/texture.hpp,
// set_streaming_options; port/patches/aurora/0031). A pack's pictures always load on worker threads, the game's own
// drawn until each is ready; this sets the budget the least recently drawn are let go past, the largest side a
// picture loads at (its top mip levels dropped), and how much is uploaded in a frame. Auto picks for the device:
// Low on phones, tablets, Linux handhelds and machines short of memory, Medium on a Steam Deck, Maximum elsewhere (Aurora's own defaults, as before).
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>

#include "port/handheld.h"
#include "port/settings.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace {

const char* const kLevels[] = {"Auto", "Low", "Medium", "High", "Maximum"};

struct Level {
  uint64_t budgetMiB;
  uint32_t maxDimension;
  uint64_t uploadMiB;
};
// Low, Medium, High, Maximum
constexpr Level kLevelValues[] = {
    {512, 1024, 6},
    {1024, 2048, 8},
    {2048, 0, 12},
    {4096, 0, 12},
};

int* sLevel;
int sApplied = -1;

int autoLevel() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return 1;
#else
  // Linux handhelds and machines short of memory (the smaller graphics buffers chosen): 1-2 GB shared between
  // the CPU and the GPU, so Low as on phones
  if (portHandheld() || portLowMemoryGfx()) {
    return 1;
  }
  return portSteamDeck() ? 2 : 4;
#endif
}

void tick(int /*pageOpen*/) {
  const int level = *sLevel == 0 ? autoLevel() : *sLevel;
  if (level == sApplied || level < 1 || level > 4) {
    return;
  }
  sApplied = level;
  const Level& l = kLevelValues[level - 1];
  std::printf("[texpack] memory level %d: %u MiB, textures up to %u px\n", level, unsigned(l.budgetMiB),
              unsigned(l.maxDimension));
  aurora::texture::set_streaming_options({
      .memoryBudgetBytes = l.budgetMiB << 20,
      .maxDimension = l.maxDimension,
      .uploadBytesPerFrame = l.uploadMiB << 20,
  });
}

const bool sAdded = [] {
  sLevel = portSettingsDeclare("texture_pack_memory", 0, 0, 4);
  portSettingsAddChoice("Graphics", "Texture pack memory",
                        "How much graphics memory a texture pack's pictures may use, and how large they load. Low "
                        "keeps them to 512 MB at up to 1024 pixels, Medium 1 GB at up to 2048, High 2 GB and "
                        "Maximum 4 GB at full size. Auto: Low on phones, tablets, handhelds and machines with "
                        "little memory, Medium on a Steam Deck, Maximum elsewhere. They load in the background; the game's own pictures show until "
                        "they are ready.",
                        0, "texture_pack_memory", kLevels, 5, "texture pack memory vram streaming hd budget");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
