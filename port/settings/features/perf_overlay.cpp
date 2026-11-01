// The performance overlay (settings: perf_overlay; port/include/port/display.h):
// a frame-time graph in the frame-rate overlay's corner (top left without it),
// below or above that overlay. Bars: the time between presents (light) and
// the part of it spent computing and drawing (orange); lines at 60 and 120 Hz.
// Aurora times the GPU only in Tracy builds, so there is no GPU split.
#include "port/display.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" void portPerfOverlayDraw(void) {
  const int mode = portPerfOverlay();
  if (mode == 0) {
    return;
  }
  static float interval[PORT_PERF_SAMPLES], work[PORT_PERF_SAMPLES];
  const int n = portPerfSamples(interval, work, PORT_PERF_SAMPLES);
  if (n < 2) {
    return;
  }
  // the timings: the mean, the slowest 1% and the spread (jitter) of the frame times
  char text[160] = "";
  if (mode == 2) {
    float sum = 0.f, sumWork = 0.f;
    float sorted[PORT_PERF_SAMPLES];
    for (int i = 0; i < n; i++) {
      sum += interval[i];
      sumWork += work[i];
      sorted[i] = interval[i];
    }
    const float mean = sum / n;
    float var = 0.f;
    for (int i = 0; i < n; i++) {
      var += (interval[i] - mean) * (interval[i] - mean);
    }
    std::sort(sorted, sorted + n);
    const float p99 = sorted[std::min(n - 1, n * 99 / 100)];
    std::snprintf(text, sizeof(text), "%.2f ms  %.0f FPS  work %.2f ms\n1%% low %.1f ms  jitter %.2f ms", mean,
                  mean > 0.f ? 1000.f / mean : 0.f, sumWork / n, p99, std::sqrt(var / n));
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float scale = std::max(1.f, display.y / 720.f);
  const ImVec2 textSize = text[0] != '\0' ? ImGui::CalcTextSize(text) : ImVec2(0.f, 0.f);
  const float w = std::max(PORT_PERF_SAMPLES * scale, textSize.x), h = 64.f * scale, pad = 8.f;
  const float textH = text[0] != '\0' ? textSize.y + 4.f : 0.f;
  // the corner of the frame-rate overlay (1-4: TL TR BL BR), past its text
  const int corner = portSettings()->fps != 0 ? portSettings()->fps : 1;
  const bool right = corner == 2 || corner == 4, bottom = corner >= 3;
  const float clear = portSettings()->fps != 0 ? ImGui::GetTextLineHeight() + 12.f : 0.f;
  const float x = right ? display.x - w - pad : pad;
  const float y = bottom ? display.y - h - textH - pad - clear : pad + clear;

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(x - 4, y - 2), ImVec2(x + w + 4, y + h + textH + 2), IM_COL32(0, 0, 0, 140), 4.f);
  // 0-50 ms on the graph, the 60 and 120 Hz frame times marked
  const float top = 50.f;
  const auto yOf = [&](float ms) { return y + h - std::min(ms, top) / top * h; };
  for (const float ms : {1000.f / 60.f, 1000.f / 120.f}) {
    dl->AddLine(ImVec2(x, yOf(ms)), ImVec2(x + w, yOf(ms)), IM_COL32(255, 255, 255, 60));
  }
  const float bar = w / PORT_PERF_SAMPLES;
  const float x0 = x + w - n * bar;
  for (int i = 0; i < n; i++) {
    const float bx = x0 + i * bar;
    dl->AddRectFilled(ImVec2(bx, yOf(interval[i])), ImVec2(bx + bar, y + h), IM_COL32(200, 220, 255, 150));
    dl->AddRectFilled(ImVec2(bx, yOf(work[i])), ImVec2(bx + bar, y + h), IM_COL32(255, 150, 60, 210));
  }
  if (text[0] != '\0') {
    dl->AddText(ImVec2(x, y + h + 2.f), IM_COL32(255, 255, 255, 230), text);
  }
}
