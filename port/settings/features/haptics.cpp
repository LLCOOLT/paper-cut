// Controller haptics (Controls > Controller haptics): a short pulse on the
// player's controller as a timed press's window opens, so the timing can be
// felt (port/src/common/ac_cue_pc.c finds the moment from the game's own state:
// an attack's landing, an enemy's jump onto Mario or the partner, an enemy's
// other attacks once their timing is learned), a pulse where a gauge or stick
// command succeeds and a tick as it steps on, and on a
// DualSense the adaptive triggers resist while a command holds R (or L and R).
// Output only: the game reads nothing of it, so play and online stay the same.
// The pulses follow Controls > Rumble and Rumble strength, and the game's own
// Rumble option; another controller gets the pulses as plain rumble.
#include "port/ac_cue.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int* sOn;
SDL_Gamepad* sSent[PAD_CHANMAX]; // the controller each port's trigger effect went to
int sMask[PAD_CHANMAX];          // and what it was (-1: none sent)
unsigned sSeenAt;                // gPortHapticsTriggersAt last tick
int sStale;                      // ticks it has not moved
int sLog = -1;

bool logging() {
  if (sLog < 0) {
    sLog = std::getenv("TTYD_HAPTICS_LOG") != nullptr || std::getenv("TTYD_HAPTICS_PRESS") != nullptr;
  }
  return sLog != 0;
}

SDL_Gamepad* gamepadFor(int port) {
  const s32 index = PADGetIndexForPort(u32(port));
  return index >= 0 ? PADGetSDLGamepadForIndex(u32(index)) : nullptr;
}

bool isDualSense(SDL_Gamepad* g) { return g != nullptr && SDL_GetGamepadType(g) == SDL_GAMEPAD_TYPE_PS5; }

// The DualSense's output report, as SDL's HIDAPI driver takes it through
// SDL_SendGamepadEffect (DS5EffectsState_t, src/joystick/hidapi/SDL_hidapi_ps5.c)
struct DS5Effects {
  uint8_t enableBits1;          // 0: 0x04 the right trigger's effect, 0x08 the left's
  uint8_t enableBits2;          // 1
  uint8_t rumbleRight;          // 2
  uint8_t rumbleLeft;           // 3
  uint8_t headphoneVolume;      // 4
  uint8_t speakerVolume;        // 5
  uint8_t microphoneVolume;     // 6
  uint8_t audioEnableBits;      // 7
  uint8_t micLightMode;         // 8
  uint8_t audioMuteBits;        // 9
  uint8_t rightTrigger[11];     // 10
  uint8_t leftTrigger[11];      // 21
  uint8_t unknown1[6];          // 32
  uint8_t enableBits3;          // 38
  uint8_t unknown2[2];          // 39
  uint8_t ledAnim;              // 41
  uint8_t ledBrightness;        // 42
  uint8_t padLights;            // 43
  uint8_t ledRed, ledGreen, ledBlue; // 44
};
static_assert(sizeof(DS5Effects) == 47, "DS5EffectsState_t is 47 bytes");

// A trigger effect: off (0x05), or a light wall partway down the pull (mode
// 0x02, section resistance: start, end, force), the GameCube trigger's click
// before the digital press
void triggerEffect(uint8_t out[11], bool resist) {
  std::memset(out, 0, 11);
  if (resist) {
    out[0] = 0x02;
    out[1] = 0x50; // from a third of the way down
    out[2] = 0xA0; // to two thirds
    out[3] = 0x60; // modest
  } else {
    out[0] = 0x05;
  }
}

void sendTriggers(SDL_Gamepad* g, int mask) {
  DS5Effects e{};
  e.enableBits1 = 0x04 | 0x08;
  triggerEffect(e.rightTrigger, (mask & 2) != 0);
  triggerEffect(e.leftTrigger, (mask & 1) != 0);
  SDL_SendGamepadEffect(g, &e, sizeof(e));
}

// The pulse (ac_cue_pc.c calls it at the frame before the window opens)
void cue(int port, int kind) {
  const PortSettings* s = portSettings();
  if (!*sOn || !s->rumble || s->rumbleStrength <= 0) {
    return;
  }
  SDL_Gamepad* g = gamepadFor(port);
  if (g == nullptr) {
    return;
  }
  const float k = float(s->rumbleStrength) / 100.f;
  // an attack, or a gauge or stick command where it succeeds: short and sharp, both motors;
  // a guard: crisper, the small motor alone, shorter; a gauge's or stick's step: a faint tick
  Uint16 low = 0;
  Uint16 high = Uint16(0xFFFF * k);
  Uint32 ms = 30;
  const char* name = "guard";
  switch (kind) {
  case PORT_HAPTICS_ATTACK:
  case PORT_HAPTICS_ZONE:
    low = Uint16(0x8000 * k);
    ms = 45;
    name = kind == PORT_HAPTICS_ATTACK ? "attack" : "zone";
    break;
  case PORT_HAPTICS_TICK:
    high = Uint16(0x6000 * k);
    ms = 18;
    name = "tick";
    break;
  default:
    break;
  }
  const bool ok = SDL_RumbleGamepad(g, low, high, ms);
  if (logging()) {
    std::printf("[haptics] port %d: %s pulse %ums (low %u, high %u)%s\n", port, name, unsigned(ms), unsigned(low),
                unsigned(high), ok ? "" : " (the controller has no rumble)");
    std::fflush(stdout);
  }
}

// each presented frame: the trigger effects where they changed (and off when
// the setting is, or out of battle, where the action commands stop updating)
void tick(int pageOpen) {
  (void)pageOpen;
  if (gPortHapticsTriggersAt != sSeenAt) {
    sSeenAt = gPortHapticsTriggersAt;
    sStale = 0;
  } else if (sStale < 1000) {
    sStale++;
  }
  for (int port = 0; port < PAD_CHANMAX; port++) {
    SDL_Gamepad* g = gamepadFor(port);
    if (!isDualSense(g)) {
      sSent[port] = nullptr;
      continue;
    }
    const int want = *sOn && sStale < 10 ? gPortHapticsTriggers[port] : 0;
    if (g == sSent[port] && want == sMask[port]) {
      continue;
    }
    sendTriggers(g, want);
    sSent[port] = g;
    sMask[port] = want;
  }
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("controller_haptics", 1, 0, 1);
  gPortHapticsCue = cue;
  portSettingsAddToggle("Controls", "Controller haptics",
                        "A short pulse as an action command's or a guard's timing window opens, and as a gauge or "
                        "stick command reaches where it succeeds, with a tick as it steps on. Guards against "
                        "attacks other than jumps are felt once that attack has been seen twice. On a DualSense "
                        "the triggers also resist while a command holds R. Follows Rumble and Rumble strength.",
                        0, "controller_haptics",
                        "haptics rumble vibration dualsense adaptive triggers action command guard superguard timing "
                        "gauge stick");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
