// "Preparing graphics": before the game starts, wait until Aurora has built
// every pipeline it knows of (the bundled seed, data/initial_pipeline_cache.db,
// tools/port/pipeline_cache.py; and the player's own cache), on a stage in the
// launcher's style with how far it has got. Without it, a model drawn for the
// first time before its pipeline is ready is skipped, and a scene waiting on a
// character it can't see stalls (the guards in Rogueport's square, on a fresh
// install). Built once: later starts find them in the cache and go straight on.
//
// The log says how long it took and how many it built ("[warmup] ..."), and
// Aurora logs each pipeline a draw still has to build afterwards ("Pipeline
// built on demand", port/patches/aurora/0081): one the seed misses.
// TTYD_WARMUP=1 waits in scripted runs too (tests/port/testpass/
// gfx_seed_no_on_demand.json; a run's own empty cache, TTYD_CACHE_DIR).
#include "../launcher_style.h"
#include "../settings_ui.h"
#include "port/aurora_pc.h"
#include "port/gpu_start.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

extern "C" void portWatchdogFrame(void); // crash_pc.c (port/platform.h, C only)

extern "C" void portPipelineWarmupWait(void) {
  // scripted runs (tests, sweeps) have fresh caches each time: no waiting there, unless TTYD_WARMUP=1 asks
  const char* forced = SDL_getenv("TTYD_WARMUP");
  if ((forced == nullptr || std::strcmp(forced, "1") != 0) &&
      (SDL_getenv("TTYD_FAST") != nullptr || SDL_getenv("TTYD_INPUT") != nullptr ||
       SDL_getenv("TTYD_MAP") != nullptr)) {
    return;
  }
  const AuroraStats* stats = aurora_get_stats();
  if (stats == nullptr) {
    return;
  }
  if (stats->queuedPipelines == 0) {
    std::printf("[warmup] nothing to prepare: %u pipelines built, %u on demand so far\n", stats->createdPipelines,
                stats->onDemandPipelines);
    std::fflush(stdout);
    return;
  }
  uint32_t total = stats->queuedPipelines;
  const uint32_t createdBefore = stats->createdPipelines;
  const Uint64 start = SDL_GetTicks();
  const char* how = "done";
  gPortLauncherShown = true; // a tap reaches ImGui here as on the launcher (settings_ui.cpp)
  // at most a minute and a half ($TTYD_WARMUP_SECONDS: a test timing the whole
  // warmup), and any key, click, tap or button starts the game at once: Aurora
  // goes on building in the background either way
  const char* limitEnv = SDL_getenv("TTYD_WARMUP_SECONDS");
  const int limitS = limitEnv != nullptr && SDL_atoi(limitEnv) > 0 ? SDL_atoi(limitEnv) : 90;
  while (stats->queuedPipelines > 0) {
    if (SDL_GetTicks() - start >= static_cast<Uint64>(limitS) * 1000) {
      how = "stopped at its time limit";
      break;
    }
    portAuroraBeginFrame(); // exits the program if the window is closed
    portWatchdogFrame();    // a frame drawn ($TTYD_WATCHDOG: the wait is no hang)
    bool skip = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !skip; k++) {
      skip = ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false);
    }
    if (skip) {
      how = "skipped by the player";
      break;
    }
    total = std::max(total, stats->queuedPipelines);
    const float scale = launcherUiScale(1.f);
    launcherStyleInit(scale);
    ImVec2 stageMin, stageMax;
    launcherBackdrop(static_cast<float>(SDL_GetTicks() - start) / 1000.f, scale, &stageMin, &stageMax);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float fontScale = ImGui::GetIO().FontGlobalScale;
    const float cx = (stageMin.x + stageMax.x) * 0.5f;
    const float cy = (stageMin.y + stageMax.y) * 0.5f;
    const float headSize = gLauncherFonts.title->FontSize * fontScale * 1.25f;
    const float bodySize = gLauncherFonts.heading->FontSize * fontScale * 1.2f;
    const char* head = portTr("Preparing graphics");
    char line[64];
    const int done = static_cast<int>(100.f * static_cast<float>(total - stats->queuedPipelines) / total);
    std::snprintf(line, sizeof(line), "%d%%", done);
    const char* note = portTr("Only the first time. Press any button to start now.");
    const ImVec2 hs = gLauncherFonts.title->CalcTextSizeA(headSize, FLT_MAX, 0.f, head);
    const ImVec2 ls = gLauncherFonts.heading->CalcTextSizeA(bodySize * 1.4f, FLT_MAX, 0.f, line);
    const ImVec2 ns = gLauncherFonts.heading->CalcTextSizeA(bodySize * 0.8f, FLT_MAX, 0.f, note);
    float y = cy - (hs.y + ls.y + ns.y + 40.f * scale) * 0.5f;
    launcherOutlinedText(dl, gLauncherFonts.title, headSize, ImVec2(cx - hs.x * 0.5f, y), head, kPaper, kCurtain,
                         4.f * scale);
    y += hs.y + 6.f * scale;
    launcherOutlinedText(dl, gLauncherFonts.heading, bodySize * 1.4f, ImVec2(cx - ls.x * 0.5f, y), line, kGold,
                         IM_COL32(60, 20, 30, 255), 3.f * scale);
    y += ls.y + 8.f * scale;
    // a paper progress bar: cream track, gold fill
    const float barW = std::min(stageMax.x - stageMin.x - 40.f * scale, 420.f * scale);
    const float barH = 14.f * scale;
    const ImVec2 b0(cx - barW * 0.5f, y), b1(cx + barW * 0.5f, y + barH);
    dl->AddRectFilled(ImVec2(b0.x + 3.f * scale, b0.y + 4.f * scale), ImVec2(b1.x + 3.f * scale, b1.y + 4.f * scale),
                      IM_COL32(20, 10, 30, 110), barH * 0.5f);
    dl->AddRectFilled(b0, b1, kPaper, barH * 0.5f);
    if (done > 0) {
      dl->AddRectFilled(ImVec2(b0.x + 2.f * scale, b0.y + 2.f * scale),
                        ImVec2(b0.x + 2.f * scale + (barW - 4.f * scale) * done / 100.f, b1.y - 2.f * scale), kGold,
                        barH * 0.5f);
    }
    y += barH + 12.f * scale;
    launcherOutlinedText(dl, gLauncherFonts.heading, bodySize * 0.8f, ImVec2(cx - ns.x * 0.5f, y), note, kPaper,
                         IM_COL32(60, 20, 30, 255), 2.f * scale);
    launcherStyleEnd();
    aurora_end_frame();
    portGpuStartFrame(); // the first frame drawn: the graphics API started (port/include/port/gpu_start.h)
  }
  gPortLauncherShown = false;
  portUiReleaseInput(); // the game gets the keyboard and controller
  // (a line the tests and a player's log both read: how long the first start's wait was, and what it built)
  std::printf("[warmup] Preparing graphics %s: %u pipelines built in %llu ms (%u queued at the start, %u left, "
              "%u on demand so far)\n",
              how, stats->createdPipelines - createdBefore, static_cast<unsigned long long>(SDL_GetTicks() - start),
              total, stats->queuedPipelines, stats->onDemandPipelines);
  std::fflush(stdout);
}
