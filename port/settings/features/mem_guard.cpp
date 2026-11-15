// The memory guard on phones and tablets (port/include/port/mem_guard.h): graphics capped by the device's
// memory at start, stepped down when memory runs low, and a note at the next start when the system closed
// the game while it played (iOS and Android end such an app without a crash report: the player saw nothing).
// Off on computers.
#include "port/mem_guard.h"
#include "port/flight.h"
#include "port/display.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <mach/mach.h>
#include <os/proc.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // (std::min and std::max below)
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define MEM_GUARD_PHONE 1
#else
#define MEM_GUARD_PHONE 0
#endif

extern "C" void AuroraFlushStaticTextureCaches(void); // (dolphin/gx/GXAurora.h: at the next frame's start)
extern "C" unsigned int AuroraOutOfMemoryCount(void);  // (dolphin/gx/GXAurora.h: video memory ran out, never fatal)

namespace {

constexpr int kRing = 16;              // samples kept in the record
constexpr Uint64 kPollMs = 2000;       // the room left looked at (or the step gap, when shorter)
constexpr Uint64 kSampleMs = 10000;    // a sample written
constexpr Uint64 kStepGapMs = 10000;   // steps (and flushes) at most this often...
constexpr Uint64 kStepGapMaxMs = 300000; // ...the gap doubling once nothing is left to turn down

const char* const kUpscaleNames[] = {"off", "2x", "4x"};
const char* const kSuperNames[] = {"off", "2x", "3x", "4x"};
const char* const kPackNames[] = {"Auto", "Low", "Medium", "High", "Maximum"};

bool sOn;
std::string sRecord;     // memory_record.txt
unsigned sRamMiB;
std::string sHeader;     // the record's first lines
std::atomic<int> sBackground{0};
std::atomic<int> sWarnings{0};
std::atomic<int> sWarnPending{0};
std::atomic<int> sWritePending{0};
bool sEnded;             // a clean exit written: nothing after it
int sStep;
Uint64 sLastStep, sStepGap = kStepGapMs, sPollMs = kPollMs, sLastPoll, sLastSample;
unsigned long long sFrames;
int sWarnAt = -1, sKillAt = -1;
bool sKillBg;
bool sClosedOnScreen; // the last run's record: closed by the system while on screen (portMemGuardClosedOnScreen)

struct Sample {
  unsigned seconds, inUse, room;
  int background, warnings, step;
};
Sample sRing[kRing];
int sRingCount;

// the cards: short notes at the top of the screen, one after another
struct Card {
  std::string title, text;
};
std::vector<Card> sCards;
Uint64 sCardFrom; // when the first card was first drawn (0: not yet)
constexpr Uint64 kCardMs = 11000;

unsigned envNumber(const char* name, unsigned fallback) {
  const char* v = SDL_getenv(name);
  return v != nullptr && v[0] != '\0' ? static_cast<unsigned>(std::strtoul(v, nullptr, 10)) : fallback;
}

// /proc/<file>'s "<key>: <n> kB" line, in MiB (0: none)
#if defined(__linux__)
unsigned procMeminfo(const char* key) {
  FILE* f = std::fopen("/proc/meminfo", "r");
  char line[160];
  unsigned long kb = 0;
  const size_t n = std::strlen(key);
  if (f == nullptr) {
    return 0;
  }
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (std::strncmp(line, key, n) == 0 && line[n] == ':') {
      kb = std::strtoul(line + n + 1, nullptr, 10);
      break;
    }
  }
  std::fclose(f);
  return static_cast<unsigned>(kb >> 10);
}
#endif

// the device's memory, in MiB
unsigned ramMiB() {
  const int mib = SDL_GetSystemRAM();
  return envNumber("TTYD_MEM_GUARD_RAM", mib > 0 ? static_cast<unsigned>(mib) : 0);
}

// this app's memory, in MiB (0: unknown): what the system counts against it
unsigned inUseMiB() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  task_vm_info_data_t info;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    return static_cast<unsigned>(info.phys_footprint >> 20);
  }
  return 0;
#elif defined(__linux__)
  // resident pages (/proc/self/statm's second number)
  FILE* f = std::fopen("/proc/self/statm", "r");
  unsigned long size = 0, resident = 0;
  if (f == nullptr) {
    return 0;
  }
  const int got = std::fscanf(f, "%lu %lu", &size, &resident);
  std::fclose(f);
  const long page = sysconf(_SC_PAGESIZE);
  return got == 2 && page > 0 ? static_cast<unsigned>((static_cast<unsigned long long>(resident) * page) >> 20) : 0;
#else
  return 0;
#endif
}

// the room left before the system steps in, in MiB (0: unknown)
unsigned roomMiB() {
  unsigned room = 0;
#if defined(__APPLE__) && TARGET_OS_IPHONE
  room = static_cast<unsigned>(os_proc_available_memory() >> 20); // (this app's own limit, less its use)
#elif defined(__linux__)
  room = procMeminfo("MemAvailable"); // (the whole system's: Android's low-memory killer watches it)
#elif defined(_WIN32)
  MEMORYSTATUSEX m;
  m.dwLength = sizeof(m);
  if (GlobalMemoryStatusEx(&m)) {
    room = static_cast<unsigned>(m.ullAvailPhys >> 20);
  }
#endif
  return envNumber("TTYD_MEM_GUARD_ROOM", room);
}

// room under this is low: iOS's figure is the app's own, Android's the system's (a twentieth of it, 256 MB at least)
unsigned lowRoomMiB() {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  return 256;
#else
  return std::max(256u, sRamMiB / 20);
#endif
}

const char* nameOf(const char* const* names, int count, const int* v) {
  return v != nullptr && *v >= 0 && *v < count ? names[*v] : "?";
}

std::string settingsLine() {
  char text[200];
  std::snprintf(text, sizeof(text), "texture upscaling %s, supersampling %s, texture pack memory %s",
                nameOf(kUpscaleNames, 3, portSettingsFind("texture_upscale")),
                nameOf(kSuperNames, 4, portSettingsFind("supersample")),
                nameOf(kPackNames, 5, portSettingsFind("texture_pack_memory")));
  return text;
}

std::string sampleLine(const Sample& s) {
  char line[160];
  std::snprintf(line, sizeof(line), "%u s %s: %u MB in use, %u MB left, %d warnings, step %d", s.seconds,
                s.background ? "in the background" : "on screen", s.inUse, s.room, s.warnings, s.step);
  return line;
}

// the record written whole (a small file), through a new file put in place: a run ended while writing it
// leaves the last one whole
void writeRecord(const char* end) {
  if (sRecord.empty() || sEnded) {
    return;
  }
  std::string text = sHeader;
  for (int i = 0; i < sRingCount; i++) {
    text += sampleLine(sRing[i]) + "\n";
  }
  if (end != nullptr) {
    text += std::string("end: ") + end + "\n";
    sEnded = true;
  }
  const std::string fresh = sRecord + ".new";
  if (SDL_SaveFile(fresh.c_str(), text.data(), text.size())) {
    SDL_RenamePath(fresh.c_str(), sRecord.c_str());
  }
}

void sample() {
  Sample s;
  s.seconds = static_cast<unsigned>(SDL_GetTicks() / 1000);
  s.inUse = inUseMiB();
  s.room = roomMiB();
  s.background = sBackground.load();
  s.warnings = sWarnings.load();
  s.step = sStep;
  if (sRingCount == kRing) {
    std::memmove(sRing, sRing + 1, sizeof(Sample) * (kRing - 1));
    sRingCount--;
  }
  sRing[sRingCount++] = s;
  sLastSample = SDL_GetTicks();
  writeRecord(nullptr);
}

void card(const char* title, const std::string& text) {
  sCards.push_back(Card{title != nullptr ? title : "", text});
}

// a declared setting at value for the run, settings.txt keeping the player's (held once: a second change
// for the run keeps the first hold's, the player's own)
bool setForRun(const char* key, int value) {
  int* v = portSettingsFind(key);
  if (v == nullptr || *v == value) {
    return false;
  }
  if (!portSettingsExtraHeld(key)) {
    portSettingsExtraSaveAs(key, 1, *v);
  }
  *v = value;
  return true;
}

void add(std::string& list, const char* what) {
  list += list.empty() ? "" : ", ";
  list += what;
}

// --- at start ---

// what the last run's record says (memory_record.txt, then kept as memory_record_last.txt)
void lookAtLastRun() {
  size_t size = 0;
  char* data = static_cast<char*>(SDL_LoadFile(sRecord.c_str(), &size));
  if (data == nullptr) {
    return; // (a first start, or the guard was off)
  }
  const std::string text(data, size);
  SDL_free(data);
  const std::string last = sRecord.substr(0, sRecord.size() - 4) + "_last.txt";
  SDL_RemovePath(last.c_str());
  SDL_RenamePath(sRecord.c_str(), last.c_str());
  std::vector<std::string> lines;
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    end = end == std::string::npos ? text.size() : end;
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty() && line[0] != '#') {
      lines.push_back(line);
    }
    at = end + 1;
  }
  const std::string tail = lines.empty() ? std::string() : lines.back();
  if (tail.rfind("end:", 0) == 0) {
    std::printf("[memory] the last run ended cleanly\n");
    return;
  }
  if (tail.find(" in the background:") != std::string::npos) {
    std::printf("[memory] the last run ended in the background (the system's own business): no note\n");
    return;
  }
  if (tail.find(" on screen:") == std::string::npos) {
    return; // (no sample: ended before its first)
  }
  if (portCrashReportFound()) {
    std::printf("[memory] the last run crashed: its report is the crash notice's\n");
    return;
  }
  sClosedOnScreen = true;
  std::printf("[memory] the last run was closed by the system while on screen (no clean exit, no crash report):\n");
  for (const std::string& line : lines) {
    std::printf("[memory] record: %s\n", line.c_str());
  }
  std::fflush(stdout);
  unsigned inUse = 0, room = 0;
  const size_t colon = tail.find(": ");
  if (colon != std::string::npos) {
    std::sscanf(tail.c_str() + colon + 2, "%u MB in use, %u MB left", &inUse, &room);
  }
  char body[600];
  if (inUse != 0) {
    std::snprintf(body, sizeof(body),
                  portTr("Last time, your device ended the game while you played, most likely to free memory "
                         "(%d MB in use, %d MB left)."),
                  static_cast<int>(inUse), static_cast<int>(room));
  } else {
    std::snprintf(body, sizeof(body), "%s",
                  portTr("Last time, your device ended the game while you played, most likely to free memory."));
  }
  card(portTr("The system closed Paper Cut"),
       std::string(body) + " " +
           portTr("If it happens again, lower Texture upscaling or Supersampling in Settings > Graphics."));
}

// the device's tier by its memory: 0 under 4 GB, 1 under 8 GB, 2 more (or unknown)
int tier() { return sRamMiB == 0 ? 2 : sRamMiB < 3500 ? 0 : sRamMiB < 7000 ? 1 : 2; }

// the capped settings: their keys, the highest value each tier allows (texture pack memory: Auto 0, then Low..
// Maximum), and the note's line for each, "<setting> <capped> on this device (<chosen> chosen)"
struct Capped {
  const char* key;
  int extra; // the cap: the tier plus this
  const char* label;
  const char* const* names;
  int count;
  const char* line; // (portTr: the names in it translated too)
};
const Capped kCapped[] = {
    {"texture_upscale", 0, "texture upscaling", kUpscaleNames, 3, "Texture upscaling %s on this device (%s chosen)"},
    {"supersample", 0, "supersampling", kSuperNames, 4, "Supersampling %s on this device (%s chosen)"},
    {"texture_pack_memory", 1, "texture pack memory", kPackNames, 5,
     "Texture pack memory %s on this device (%s chosen)"},
};

// the graphics capped by the device's memory, for the run: the note says each one, as capped and as chosen
// (it once said "Graphics lightened for this device's memory, this session only" and no more: an iPhone 17 Pro
// with Supersampling 4x chosen saw it every start, nothing saying what or why)
void capForDevice() {
  std::string changed, note;
  char what[160];
  for (const Capped& c : kCapped) {
    const int max = tier() + c.extra;
    int* v = portSettingsFind(c.key);
    if (v == nullptr || *v <= max) {
      continue;
    }
    const int was = *v;
    setForRun(c.key, max);
    const char* to = c.names[std::min(max, c.count - 1)];
    const char* from = c.names[std::min(was, c.count - 1)];
    std::snprintf(what, sizeof(what), "%s %s (from %s)", c.label, to, from);
    add(changed, what);
    std::snprintf(what, sizeof(what), portTr(c.line), portTr(to), portTr(from));
    note += note.empty() ? "" : "\n";
    note += what;
    if (std::strcmp(c.key, "texture_upscale") == 0) {
      portTextureUpscaleSet(max);
    }
  }
  std::printf("[memory] a device with %u MB: %s%s\n", sRamMiB, changed.empty() ? "the graphics as chosen" : "for this run ",
              changed.c_str());
  std::fflush(stdout);
  if (!note.empty()) {
    card(nullptr, note);
  }
}

// --- while playing ---

void stepDown(const char* why) {
  const Uint64 now = SDL_GetTicks();
  if (sLastStep != 0 && now - sLastStep < sStepGap) {
    return;
  }
  sLastStep = now;
  sStep++;
  std::string changed;
  PortSettings* ps = portSettings();
  if (sStep == 1) {
    if (setForRun("supersample", 0)) {
      add(changed, "supersampling off");
    }
    if (const int* up = portSettingsFind("texture_upscale"); up != nullptr && *up > 0) {
      const int to = *up - 1;
      setForRun("texture_upscale", to);
      portTextureUpscaleSet(to);
      add(changed, to == 0 ? "texture upscaling off" : "texture upscaling 2x");
    }
    if (const int* pack = portSettingsFind("texture_pack_memory"); pack != nullptr && *pack > 1) {
      setForRun("texture_pack_memory", 1);
      add(changed, "texture pack memory Low");
    }
  } else if (sStep == 2) {
    if (setForRun("texture_upscale", 0)) {
      portTextureUpscaleSet(0);
      add(changed, "texture upscaling off");
    }
    if (ps->resolution != 1) {
      portSettingsSetForRun("resolution", "1");
      add(changed, "the picture at 480 lines");
    }
  }
  if (changed.empty() && sStepGap < kStepGapMaxMs) {
    sStepGap = std::min(sStepGap * 2, kStepGapMaxMs); // (only the textures to let go: less often)
  }
  AuroraFlushStaticTextureCaches();
  const unsigned inUse = inUseMiB(), room = roomMiB();
  portFlightMark("memory guard: %s (%u MB in use, %u MB left): step %d", why, inUse, room, sStep);
  std::printf("[memory] %s (%u MB in use, %u MB left): step %d, for this run: %s%s\n", why, inUse, room, sStep,
              changed.c_str(), changed.empty() ? "the textures let go" : ", the textures let go");
  std::fflush(stdout);
  if (!changed.empty()) {
    portSettingsApply();
    card(nullptr, portTr("Memory running low: lighter graphics for this session"));
  }
  sample();
}

void drawCards() {
  if (sCards.empty()) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (sCardFrom == 0) {
    sCardFrom = now;
    std::printf("[memory] note shown: %s%s%s\n", sCards.front().title.c_str(),
                sCards.front().title.empty() ? "" : ": ", sCards.front().text.c_str());
    std::fflush(stdout);
  }
  if (now - sCardFrom >= kCardMs) {
    sCards.erase(sCards.begin());
    sCardFrom = 0;
    return;
  }
  if (portCleanFrame()) {
    return; // (media captures: nothing over the picture)
  }
  const Card& c = sCards.front();
  const float left = static_cast<float>(kCardMs - (now - sCardFrom)) / 1000.f;
  const float alpha = std::min(1.f, left);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize() * (MEM_GUARD_PHONE ? 1.2f : 1.f);
  const float pad = size * 0.6f;
  const float wrap = std::min(display.x - 32.f - pad * 2, size * 28.f);
  float top = 12.f; // below the notch or Dynamic Island
  SDL_Rect safe{};
  if (SDL_Window* win = aurora_get_window(); win != nullptr && SDL_GetWindowSafeArea(win, &safe) && safe.h > 0) {
    top += static_cast<float>(safe.y);
  }
  const ImVec2 titleSize = c.title.empty() ? ImVec2(0, 0) : font->CalcTextSizeA(size, FLT_MAX, wrap, c.title.c_str());
  const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, wrap, c.text.c_str());
  const float gap = c.title.empty() ? 0.f : size * 0.3f;
  const float w = std::max(titleSize.x, textSize.x) + pad * 2;
  const float h = titleSize.y + gap + textSize.y + pad * 2;
  const ImVec2 at((display.x - w) * 0.5f, top);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(0, 0, 0, static_cast<int>(200 * alpha)), size * 0.4f);
  if (!c.title.empty()) {
    dl->AddText(font, size, ImVec2(at.x + pad, at.y + pad), IM_COL32(255, 220, 120, static_cast<int>(255 * alpha)),
                c.title.c_str(), nullptr, wrap);
  }
  dl->AddText(font, size, ImVec2(at.x + pad, at.y + pad + titleSize.y + gap),
              IM_COL32(255, 255, 255, static_cast<int>(240 * alpha)), c.text.c_str(), nullptr, wrap);
}

void onExit() {
  writeRecord("a clean exit");
}

bool SDLCALL onEvent(void*, SDL_Event* e) {
  switch (e->type) {
  case SDL_EVENT_WILL_ENTER_BACKGROUND: // (iOS: the app switcher, a call, the lock; Android: paused)
  case SDL_EVENT_DID_ENTER_BACKGROUND:
    if (sBackground.exchange(1) == 0) {
      sWritePending = 1;
    }
    break;
  case SDL_EVENT_DID_ENTER_FOREGROUND:
    if (sBackground.exchange(0) != 0) {
      sWritePending = 1;
    }
    break;
  case SDL_EVENT_LOW_MEMORY:
    // (on screen only: Android trims a put-away app's memory, as a matter of course)
    if (sBackground.load() == 0) {
      sWarnings++;
      sWarnPending = 1;
    }
    break;
  case SDL_EVENT_TERMINATING:
    if (SDL_IsMainThread()) {
      writeRecord("the system ended it as asked");
    }
    break;
  default:
    break;
  }
  // a change of state written now on the game's thread (iOS may suspend it right after), else at the next frame
  if (sWritePending.load() && SDL_IsMainThread() && !sEnded) {
    sWritePending = 0;
    sample();
  }
  return true;
}

const bool sAdded = [] {
  portSettingsAddOverlay([](int) { portMemGuardFrame(); }, nullptr);
  return true;
}();

} // namespace

extern "C" void portMemGuardStart(const char* userPath) {
  const char* env = SDL_getenv("TTYD_MEM_GUARD");
  sOn = env != nullptr && env[0] != '\0' ? env[0] != '0' : MEM_GUARD_PHONE;
  if (!sOn || userPath == nullptr || userPath[0] == '\0') {
    sOn = false;
    return;
  }
  sRecord = std::string(userPath);
  while (sRecord.size() > 1 && (sRecord.back() == '/' || sRecord.back() == '\\')) {
    sRecord.pop_back();
  }
  sRecord += "/memory_record.txt";
  sRamMiB = ramMiB();
  sWarnAt = static_cast<int>(envNumber("TTYD_MEM_GUARD_WARN_AT", 0));
  sWarnAt = sWarnAt > 0 ? sWarnAt : -1;
  sKillAt = static_cast<int>(envNumber("TTYD_MEM_GUARD_KILL_AT", 0));
  sKillAt = sKillAt > 0 ? sKillAt : -1;
  sKillBg = envNumber("TTYD_MEM_GUARD_KILL_BG", 0) != 0;
  sStepGap = std::max<Uint64>(100, envNumber("TTYD_MEM_GUARD_GAP_MS", static_cast<unsigned>(kStepGapMs)));
  sPollMs = std::min(kPollMs, sStepGap);
  lookAtLastRun();
  capForDevice();
  char header[400];
  std::snprintf(header, sizeof(header),
                "# Paper Cut's memory record: the last minutes of the run, newest last (port/include/port/mem_guard.h)\n"
                "device %u MB, Paper Cut %s, %s\n",
                sRamMiB,
#ifdef PORT_VERSION
                PORT_VERSION,
#else
                "?",
#endif
                settingsLine().c_str());
  sHeader = header;
  if (!SDL_AddEventWatch(onEvent, nullptr)) {
    std::printf("[memory] no event watch: %s\n", SDL_GetError());
  }
  std::atexit(onExit);
  sample();
  sLastPoll = SDL_GetTicks();
  std::fflush(stdout);
}

extern "C" void portMemGuardFrame(void) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  {
    // Video memory ran out (a texture or an EFB copy the GPU couldn't make: GitHub #56's "Copy Conv Texture" on a
    // 6 GB card, an iGPU's shared memory): on every device, computers too, the same lighter graphics for the run.
    // Aurora never stops for it any more: the work is dropped, its texture caches let go (GXAurora.h).
    static unsigned sOutOfMemorySeen;
    const unsigned outOfMemory = AuroraOutOfMemoryCount();
    if (outOfMemory != sOutOfMemorySeen) {
      sOutOfMemorySeen = outOfMemory;
      std::printf("[memory] out of video memory (%u so far)\n", outOfMemory);
      stepDown("out of video memory");
    }
  }
  if (!sOn) {
    drawCards(); // (a computer: the note above only)
    return;
  }
  sFrames++;
  const Uint64 now = SDL_GetTicks();
  if (sWarnAt > 0 && sFrames == static_cast<unsigned long long>(sWarnAt)) {
    std::printf("[memory] test: a memory warning at frame %llu\n", sFrames);
    sWarnings++;
    sWarnPending = 1;
  }
  if (sKillAt > 0 && sFrames == static_cast<unsigned long long>(sKillAt)) {
    if (sKillBg) {
      sBackground = 1;
    }
    sample();
    std::printf("[memory] test: ended at frame %llu as the system would%s\n", sFrames,
                sKillBg ? ", in the background" : ", on screen");
    std::fflush(stdout);
    std::_Exit(9); // (no exit handlers: as a phone's system ends it)
  }
  if (sWarnPending.exchange(0) != 0) {
    stepDown("the system's memory warning");
  } else if (now - sLastPoll >= sPollMs) {
    sLastPoll = now;
    const unsigned room = roomMiB();
    if (room != 0 && room < lowRoomMiB()) {
      stepDown("little memory left");
    }
  }
  if (sWritePending.exchange(0) != 0 || now - sLastSample >= kSampleMs) {
    sample();
  }
  drawCards();
}

extern "C" int portSettingChoiceMax(const int* value, const char** why) {
  if (!sOn || value == nullptr) {
    return -1; // (a computer: every choice)
  }
  for (const Capped& c : kCapped) {
    if (value == portSettingsFind(c.key)) {
      const int max = tier() + c.extra;
      if (max >= c.count - 1) {
        return -1;
      }
      if (why != nullptr) {
        *why = "Higher choices need more memory than this device has.";
      }
      return max;
    }
  }
  return -1;
}

extern "C" int portMemGuardClosedOnScreen(void) {
  return sClosedOnScreen ? 1 : 0;
}
