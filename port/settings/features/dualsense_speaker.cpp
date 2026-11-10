// Sound from the DualSense's speaker (Controls > Sound from the DualSense):
// the action commands' blips and ratings, the Superguard's clink and coins
// also play from the controller's own speaker, as a DualSense game does.
//
// A DualSense on USB is also a sound device (four channels: the first pair to
// its speaker or headset jack, the second pair its haptic actuators), which
// SDL lists like any other; over Bluetooth it isn't, so this works on a cable.
// The controller is told (its output report, through SDL_SendGamepadEffect)
// to play that pair from its speaker, at a set volume. The sounds are made
// here (short synthesised tones, nothing of the game's), started by the game's
// own cues as they come (gPortSfxHeard: port/include/port/captions.h). Output
// only: the game reads nothing of it, so play and online stay the same.
#include "port/captions.h"
#include "port/settings.h"
#include <SDL3/SDL.h>
#include <dolphin/pad.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
float salPcOutputGain(void);     // hw_pc.c: the master volume with its mutes and rewind's
float portVolumeGain(int percent); // audio_mix_pc.c
BOOL portIsPaused(void);         // port/platform.h
}

namespace {

constexpr int kRate = 48000;

int* sOn;
SDL_AudioStream* sStream;
SDL_AudioDeviceID sDevice;
SDL_JoystickID sPad; // the DualSense told to use its speaker (by id: its SDL_Gamepad is gone once unplugged)
int* sSfxVolume;
int* sSfxMute;
Uint64 sNextLook;    // when to look for the device again

// the cue heard this frame (the highest kind wins: a Superguard's Great is the clink)
enum Kind { NONE, BLIP, RATING, COIN, CLINK };
std::atomic<int> sHeard{NONE};

void heard(const char* name) {
  int kind = NONE;
  if (std::strcmp(name, "PORT_SUPERGUARD") == 0) {
    kind = CLINK;
  } else if (std::strcmp(name, "SFX_COIN_GET1") == 0 || std::strcmp(name, "SFX_COUNT_COIN1") == 0 ||
             std::strcmp(name, "SFX_MOBJ_TEN_COUNT_COIN1") == 0) {
    kind = COIN;
  } else if (std::strncmp(name, "SFX_SYSTEM_", 11) == 0 &&
             (std::strstr(name, "NICE") != nullptr || std::strstr(name, "GOOD") != nullptr ||
              std::strstr(name, "GREAT") != nullptr || std::strstr(name, "WONDERFUL") != nullptr ||
              std::strstr(name, "EXCELLENT") != nullptr)) {
    kind = RATING;
  } else if (std::strcmp(name, "SFX_AC_PI1") == 0 || std::strcmp(name, "SFX_AC_PONE1") == 0 ||
             std::strcmp(name, "SFX_AC_BUTTON_PUSH1") == 0 || std::strcmp(name, "SFX_AC_COMMAND_OK1") == 0 ||
             std::strcmp(name, "SFX_AC_OK1") == 0) {
    kind = BLIP;
  }
  int was = sHeard.load();
  while (kind > was && !sHeard.compare_exchange_weak(was, kind)) {
  }
}

// --- the sounds ---

void tone(std::vector<float>& out, float at, float len, float freq, float amp, float decay, bool square) {
  const size_t from = size_t(at * kRate), n = size_t(len * kRate);
  if (out.size() < from + n) {
    out.resize(from + n, 0.f);
  }
  for (size_t i = 0; i < n; i++) {
    const float t = float(i) / kRate;
    const float ph = std::fmod(t * freq, 1.f);
    float v = square ? (ph < 0.5f ? 1.f : -1.f) * 0.5f + std::sin(6.2831853f * ph) * 0.5f // softened square
                     : std::sin(6.2831853f * ph);
    const float attack = std::min(1.f, t / 0.003f);
    out[from + i] += v * amp * attack * std::exp(-t * decay);
  }
}

std::vector<float> make(int kind) {
  std::vector<float> s;
  switch (kind) {
  case BLIP:
    tone(s, 0.f, 0.06f, 1320.f, 0.5f, 60.f, false);
    break;
  case RATING:
    tone(s, 0.f, 0.07f, 880.f, 0.45f, 30.f, false);
    tone(s, 0.06f, 0.14f, 1320.f, 0.5f, 22.f, false);
    break;
  case COIN:
    tone(s, 0.f, 0.07f, 987.8f, 0.4f, 8.f, true);
    tone(s, 0.065f, 0.3f, 1318.5f, 0.4f, 9.f, true);
    break;
  case CLINK:
    // a bright metal strike: inharmonic partials that die away fast
    tone(s, 0.f, 0.35f, 2093.f, 0.35f, 14.f, false);
    tone(s, 0.f, 0.3f, 3349.f, 0.25f, 18.f, false);
    tone(s, 0.f, 0.25f, 5233.f, 0.18f, 25.f, false);
    tone(s, 0.f, 0.2f, 7100.f, 0.1f, 35.f, false);
    break;
  default:
    break;
  }
  return s;
}

// --- the controller ---

// The DualSense's output report as SDL's HIDAPI driver takes it (as haptics.cpp)
struct DS5Effects {
  uint8_t enableBits1;     // 0x20 the speaker's volume, 0x80 the audio control below
  uint8_t enableBits2;
  uint8_t rumbleRight, rumbleLeft;
  uint8_t headphoneVolume;
  uint8_t speakerVolume;   // 0x3D-0x64 audible
  uint8_t microphoneVolume;
  uint8_t audioEnableBits; // bits 4-5: the output path, 3 the speaker alone
  uint8_t micLightMode;
  uint8_t audioMuteBits;
  uint8_t rightTrigger[11], leftTrigger[11];
  uint8_t unknown1[6];
  uint8_t enableBits3;
  uint8_t unknown2[2];
  uint8_t ledAnim, ledBrightness, padLights, ledRed, ledGreen, ledBlue;
};
static_assert(sizeof(DS5Effects) == 47, "DS5EffectsState_t is 47 bytes");

void routeToSpeaker(SDL_Gamepad* g, bool on) {
  DS5Effects e{};
  e.enableBits1 = 0x20 | 0x80;
  e.speakerVolume = on ? 0x64 : 0x00;
  e.audioEnableBits = on ? 0x30 : 0x00;
  SDL_SendGamepadEffect(g, &e, sizeof(e));
}

SDL_Gamepad* dualSense() {
  for (u32 i = 0; i < PAD_CHANMAX; i++) {
    const s32 index = PADGetIndexForPort(i);
    SDL_Gamepad* g = index >= 0 ? PADGetSDLGamepadForIndex(u32(index)) : nullptr;
    if (g != nullptr && SDL_GetGamepadType(g) == SDL_GAMEPAD_TYPE_PS5 &&
        SDL_GetGamepadConnectionState(g) == SDL_JOYSTICK_CONNECTION_WIRED) {
      return g;
    }
  }
  return nullptr;
}

// the controller's sound device, by its name ("Wireless Controller", "DualSense")
SDL_AudioDeviceID findDevice() {
  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  SDL_AudioDeviceID found = 0;
  for (int i = 0; ids != nullptr && i < count && found == 0; i++) {
    const char* name = SDL_GetAudioDeviceName(ids[i]);
    if (name != nullptr && (SDL_strcasestr(name, "Wireless Controller") != nullptr ||
                            SDL_strcasestr(name, "DualSense") != nullptr)) {
      found = ids[i];
    }
  }
  SDL_free(ids);
  return found;
}

void closeDevice() {
  if (sStream != nullptr) {
    SDL_DestroyAudioStream(sStream);
    sStream = nullptr;
  }
  if (sPad != 0) {
    // (still connected: told to stop; unplugged, there's nothing to tell, and a pad opened since isn't it)
    if (SDL_Gamepad* g = SDL_GetGamepadFromID(sPad)) {
      routeToSpeaker(g, false);
    }
    sPad = 0;
  }
  sDevice = 0;
}

// each presented frame: the device kept open while there's a DualSense on a
// cable, and the cue heard played
void tick(int pageOpen) {
  (void)pageOpen;
  const int kind = sHeard.exchange(NONE);
  SDL_Gamepad* g = *sOn ? dualSense() : nullptr;
  if ((g != nullptr ? SDL_GetGamepadID(g) : 0) != sPad) {
    closeDevice();
    sNextLook = 0;
  }
  if (g == nullptr || !SDL_WasInit(SDL_INIT_AUDIO)) {
    return;
  }
  if (sStream == nullptr) {
    if (SDL_GetTicks() < sNextLook) {
      return;
    }
    sNextLook = SDL_GetTicks() + 3000;
    sDevice = findDevice();
    if (sDevice == 0) {
      return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 4, kRate};
    sStream = SDL_OpenAudioDeviceStream(sDevice, &spec, nullptr, nullptr);
    if (sStream == nullptr) {
      return;
    }
    SDL_ResumeAudioStreamDevice(sStream);
    routeToSpeaker(g, true);
    sPad = SDL_GetGamepadID(g);
  }
  // the game's sound effects: their volume and mute, and the device's (the master volume, its mutes,
  // rewind's); nothing while paused
  const float gain = salPcOutputGain() * portVolumeGain(*sSfxMute ? 0 : *sSfxVolume);
  if (kind == NONE || gain <= 0.f || portIsPaused()) {
    return;
  }
  static std::vector<float> sSounds[CLINK + 1];
  if (sSounds[kind].empty()) {
    sSounds[kind] = make(kind);
  }
  const std::vector<float>& mono = sSounds[kind];
  std::vector<float> frames(mono.size() * 4, 0.f);
  for (size_t i = 0; i < mono.size(); i++) {
    frames[i * 4] = frames[i * 4 + 1] = mono[i] * gain; // the speaker's pair; the actuators' pair silent
  }
  SDL_ClearAudioStream(sStream); // the newest cue cuts the one before
  SDL_PutAudioStreamData(sStream, frames.data(), int(frames.size() * sizeof(float)));
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("dualsense_speaker", 1, 0, 1);
  sSfxVolume = portSettingsDeclare("sfx_volume", 100, 0, 100);
  sSfxMute = portSettingsDeclare("sfx_mute", 0, 0, 1);
  gPortSfxHeard = heard;
  portSettingsAddToggle("Controls", "Sound from the DualSense",
                        "On a DualSense connected with a cable: the action commands' blips, the Superguard's clink "
                        "and coins also sound from the controller's own speaker.",
                        0, "dualsense_speaker", "dualsense ps5 controller speaker sound audio playstation");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
