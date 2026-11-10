// Stereoscopic 3D (settings stereo_3d, stereo_depth, stereo_screen; Graphics tab): the GPU's side and the settings.
// port/src/aurora/stereo_aurora.c draws the cameras twice a frame, the left eye first; here, after the left eye,
// the frame is kept (Aurora's resolve_pass) and cleared by a custom draw (the game's own clear: black, the far
// depth); at the frame's end the right eye is kept too and the two are drawn into the frame in the chosen layout
// (a custom draw, a full-screen triangle), under the overlays drawn after it. Draw-only.
#include <aurora/gfx.hpp>
#include <webgpu/webgpu_cpp.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "port/savestate.h"
#include "port/settings.h"

extern "C" int portStereoMode(void); // stereo_aurora.c

namespace {

const char* const kModes[] = {
    "Off",
    "Side by side",
    "Side by side (half)",
    "Top and bottom",
    "Top and bottom (half)",
    "Anaglyph (red-cyan)",
    "Anaglyph (green-magenta)",
};
constexpr int kModeCount = sizeof(kModes) / sizeof(kModes[0]);

struct ClearPayload {
  uint32_t width, height;
};

struct Uniforms {
  float size[4]; // width, height, mode, 0
};
static_assert(sizeof(Uniforms) % 16 == 0);

struct ComposePayload {
  aurora::gfx::Range uniforms;
  WGPUTextureView left; // referenced (AddRef'd); released by the draw
  WGPUTextureView right;
  uint32_t width, height;
};

constexpr const char* kVertex = R"(
struct VsOut {
  @builtin(position) pos: vec4f,
};

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
  let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  var o: VsOut;
  o.pos = vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, DEPTH, 1.0);
  return o;
}
)";

constexpr const char* kClear = R"(
@fragment
fn fs_main(in: VsOut) -> @location(0) vec4f {
  return vec4f(0.0, 0.0, 0.0, 0.0);
}
)";

constexpr const char* kCompose = R"(
struct U {
  size: vec4f,
};
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var left_tex: texture_2d<f32>;
@group(0) @binding(2) var right_tex: texture_2d<f32>;

fn load(t: texture_2d<f32>, p: vec2f) -> vec4f {
  let d = vec2i(textureDimensions(t));
  return textureLoad(t, clamp(vec2i(floor(p)), vec2i(0), d - vec2i(1)), 0);
}

// the eye's picture at uv (0-1), averaged over foot texels a pixel (a box filter: 1 or 2 each way)
fn eye(t: texture_2d<f32>, uv: vec2f, foot: vec2f) -> vec4f {
  let p = uv * vec2f(textureDimensions(t));
  let o = foot * 0.25;
  return (load(t, p + vec2f(-o.x, -o.y)) + load(t, p + vec2f(o.x, -o.y)) + load(t, p + vec2f(-o.x, o.y)) +
          load(t, p + o)) * 0.25;
}

fn lin(c: vec3f) -> vec3f {
  return pow(max(c, vec3f(0.0)), vec3f(2.2));
}

fn enc(c: vec3f) -> vec3f {
  return pow(clamp(c, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / 2.2));
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4f {
  let size = u.size.xy;
  let mode = i32(u.size.z + 0.5);
  let p = in.pos.xy;
  let black = vec4f(0.0, 0.0, 0.0, 1.0);
  if (mode == 1 || mode == 2) { // side by side: left eye on the left
    let half = size.x * 0.5;
    let right = p.x >= half;
    let x = select(p.x, p.x - half, right) / half;
    var uv = vec2f(x, p.y / size.y);
    var foot = vec2f(2.0, 1.0);
    if (mode == 1) { // each eye its own shape: half the height, in the middle
      uv.y = (p.y - size.y * 0.25) / (size.y * 0.5);
      foot = vec2f(2.0, 2.0);
      if (uv.y < 0.0 || uv.y >= 1.0) {
        return black;
      }
    }
    if (right) {
      return eye(right_tex, uv, foot);
    }
    return eye(left_tex, uv, foot);
  }
  if (mode == 3 || mode == 4) { // top and bottom: left eye on top
    let half = size.y * 0.5;
    let bottom = p.y >= half;
    let y = select(p.y, p.y - half, bottom) / half;
    var uv = vec2f(p.x / size.x, y);
    var foot = vec2f(1.0, 2.0);
    if (mode == 3) { // each eye its own shape: half the width, in the middle
      uv.x = (p.x - size.x * 0.25) / (size.x * 0.5);
      foot = vec2f(2.0, 2.0);
      if (uv.x < 0.0 || uv.x >= 1.0) {
        return black;
      }
    }
    if (bottom) {
      return eye(right_tex, uv, foot);
    }
    return eye(left_tex, uv, foot);
  }
  let uv = p / size;
  let l = eye(left_tex, uv, vec2f(1.0)).rgb;
  let r = eye(right_tex, uv, vec2f(1.0)).rgb;
  if (mode == 6) { // green-magenta: the left eye's brightness in green, the right eye's red and blue
    let y = dot(l, vec3f(0.299, 0.587, 0.114));
    return vec4f(r.r, y, r.b, 1.0);
  }
  // red-cyan: Dubois's least-squares anaglyph (less ghosting and colour clash than taking channels)
  let ll = lin(l);
  let rl = lin(r);
  let c = vec3f(dot(ll, vec3f(0.456100, 0.500484, 0.176381)) + dot(rl, vec3f(-0.043400, -0.087951, -0.001553)),
                dot(ll, vec3f(-0.040094, -0.037842, -0.015770)) + dot(rl, vec3f(0.378476, 0.733640, 0.018407)),
                dot(ll, vec3f(-0.015218, -0.020592, -0.005974)) + dot(rl, vec3f(-0.072149, -0.112846, 1.226097)));
  return vec4f(enc(c), 1.0);
}
)";

bool sUnsupported = false;
aurora::gfx::DrawTypeId sClearType = aurora::gfx::InvalidDrawType;
aurora::gfx::DrawTypeId sComposeType = aurora::gfx::InvalidDrawType;
wgpu::TextureView sLeft;
uint32_t sLeftFrame = UINT32_MAX;
uint32_t sComposedFrame = UINT32_MAX;

// (render worker)
std::mutex sPipelineMutex;
wgpu::BindGroupLayout sLayout;
wgpu::ShaderModule sClearModule, sComposeModule;
std::unordered_map<uint64_t, wgpu::RenderPipeline> sClearPipelines, sComposePipelines;

std::string vertexSource() {
  std::string v = kVertex;
  v.replace(v.find("DEPTH"), 5, aurora::gfx::uses_reversed_z() ? "0.0" : "1.0"); // the far depth
  return v;
}

wgpu::ShaderModule makeModule(const wgpu::Device& device, const std::string& code, const char* label) {
  wgpu::ShaderSourceWGSL source{};
  source.code = code.c_str();
  const wgpu::ShaderModuleDescriptor desc{.nextInChain = &source, .label = label};
  return device.CreateShaderModule(&desc);
}

wgpu::RenderPipeline pipelineFor(const aurora::gfx::DrawContext& ctx, bool clear) {
  std::lock_guard lock{sPipelineMutex};
  auto& cache = clear ? sClearPipelines : sComposePipelines;
  if (const auto it = cache.find(ctx.layout.key); it != cache.end()) {
    return it->second;
  }
  if (!sLayout) {
    const wgpu::BindGroupLayoutEntry entries[] = {
        {.binding = 0,
         .visibility = wgpu::ShaderStage::Fragment,
         .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(Uniforms)}},
        {.binding = 1,
         .visibility = wgpu::ShaderStage::Fragment,
         .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                     .viewDimension = wgpu::TextureViewDimension::e2D}},
        {.binding = 2,
         .visibility = wgpu::ShaderStage::Fragment,
         .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                     .viewDimension = wgpu::TextureViewDimension::e2D}},
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Stereoscopic 3D bind group layout", .entryCount = 3, .entries = entries};
    sLayout = ctx.device.CreateBindGroupLayout(&layoutDesc);
    const std::string vs = vertexSource();
    sClearModule = makeModule(ctx.device, vs + kClear, "Stereoscopic 3D clear");
    sComposeModule = makeModule(ctx.device, vs + kCompose, "Stereoscopic 3D compose");
  }
  wgpu::ColorTargetState targets[aurora::gfx::MaxColorAttachments]{};
  const uint32_t count = std::max<uint32_t>(ctx.layout.colorAttachmentCount, 1);
  for (uint32_t i = 0; i < count && i < aurora::gfx::MaxColorAttachments; ++i) {
    targets[i].format = ctx.layout.colorAttachments[i].format;
    if (i != aurora::gfx::SceneColorAttachmentIndex) {
      targets[i].writeMask = wgpu::ColorWriteMask::None;
    } else {
      // the clear: colour and alpha (the game's destination alpha); the eyes put together: the colour
      targets[i].writeMask = clear ? wgpu::ColorWriteMask::All
                                   : wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green |
                                         wgpu::ColorWriteMask::Blue;
    }
  }
  const auto module = clear ? sClearModule : sComposeModule;
  const wgpu::FragmentState fragment{.module = module, .entryPoint = "fs_main", .targetCount = count, .targets = targets};
  wgpu::PipelineLayout pipelineLayout;
  if (!clear) {
    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &sLayout};
    pipelineLayout = ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);
  } else {
    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{.bindGroupLayoutCount = 0, .bindGroupLayouts = nullptr};
    pipelineLayout = ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);
  }
  wgpu::DepthStencilState depth{
      .format = ctx.layout.depthStencilFormat,
      .depthWriteEnabled = clear ? wgpu::OptionalBool::True : wgpu::OptionalBool::False,
      .depthCompare = wgpu::CompareFunction::Always,
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = clear ? "Stereoscopic 3D clear" : "Stereoscopic 3D compose",
      .layout = pipelineLayout,
      .vertex = {.module = module, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = ctx.layout.depthStencilFormat != wgpu::TextureFormat::Undefined ? &depth : nullptr,
      .multisample = {.count = std::max<uint32_t>(ctx.layout.sampleCount, 1), .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  auto pipeline = ctx.device.CreateRenderPipeline(&desc);
  cache.emplace(ctx.layout.key, pipeline);
  return pipeline;
}

void fullTarget(const wgpu::RenderPassEncoder& pass, uint32_t width, uint32_t height) {
  pass.SetViewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f);
  pass.SetScissorRect(0, 0, width, height);
}

// (render worker) the frame cleared for the right eye
void drawClear(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void* data,
               size_t size, void*) {
  if (size != sizeof(ClearPayload)) {
    return;
  }
  ClearPayload payload;
  std::memcpy(&payload, data, sizeof(payload));
  const auto pipeline = pipelineFor(ctx, true);
  if (!pipeline || payload.width == 0 || payload.height == 0) {
    return;
  }
  pass.SetPipeline(pipeline);
  fullTarget(pass, payload.width, payload.height);
  pass.Draw(3);
}

// (render worker) the two eyes into the frame
void drawCompose(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void* data,
                 size_t size, void*) {
  if (size != sizeof(ComposePayload)) {
    return;
  }
  ComposePayload payload;
  std::memcpy(&payload, data, sizeof(payload));
  const auto left = wgpu::TextureView::Acquire(payload.left);
  const auto right = wgpu::TextureView::Acquire(payload.right);
  const auto pipeline = pipelineFor(ctx, false);
  if (!pipeline || !left || !right || payload.width == 0 || payload.height == 0) {
    return;
  }
  const wgpu::BindGroupEntry entries[] = {
      {.binding = 0, .buffer = ctx.uniformBuffer, .offset = payload.uniforms.offset, .size = sizeof(Uniforms)},
      {.binding = 1, .textureView = left},
      {.binding = 2, .textureView = right},
  };
  const wgpu::BindGroupDescriptor bindDesc{.layout = sLayout, .entryCount = 3, .entries = entries};
  const auto bindGroup = ctx.device.CreateBindGroup(&bindDesc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  fullTarget(pass, payload.width, payload.height);
  pass.Draw(3);
}

bool registered() {
  if (sUnsupported) {
    return false;
  }
  if (sClearType == aurora::gfx::InvalidDrawType) {
    sClearType = aurora::gfx::register_draw_type({.label = "Stereoscopic 3D clear", .draw = drawClear});
    sComposeType = aurora::gfx::register_draw_type({.label = "Stereoscopic 3D compose", .draw = drawCompose});
    if (sClearType == aurora::gfx::InvalidDrawType || sComposeType == aurora::gfx::InvalidDrawType) {
      SDL_Log("stereo: no custom draws here: off for this run");
      sUnsupported = true;
      portSaveStateNotice("Stereoscopic 3D can't be drawn on this device: off for this run.");
      return false;
    }
  }
  return true;
}

const bool sAdded = [] {
  portSettingsDeclare("stereo_3d", 0, 0, kModeCount - 1);
  portSettingsDeclare("stereo_depth", 50, 0, 100);
  portSettingsDeclare("stereo_screen", 100, 50, 200);
  portSettingsAddChoice(
      "Graphics", "Stereoscopic 3D",
      "Draws the game for two eyes: for a 3D TV or monitor, a phone in a VR viewer, a headset's virtual screen or "
      "coloured 3D glasses. Side by side and Top and bottom keep each eye's shape; the (half) modes squeeze each eye "
      "into half the screen, as 3D TVs expect (set the TV to the same 3D mode). The HUD and menus stay at screen "
      "depth. The scene is drawn twice, about doubling the graphics card's work; ray-traced lighting is off while "
      "it is on.",
      0, "stereo_3d", kModes, kModeCount,
      "stereoscopic 3d stereo sbs side by side top bottom tab over under anaglyph red cyan glasses vr 3dtv");
  portSettingsAddSlider("Graphics", "3D depth",
                        "How far apart the two eyes are: more is deeper. Lower it if the picture is hard to see as "
                        "one.",
                        0, "stereo_depth", 0, 100, "%d%%", "stereoscopic 3d depth separation strength");
  portSettingsAddSlider("Graphics", "Screen distance",
                        "Where the screen sits in the scene, against the distance to what the camera follows: at "
                        "100% Mario is at the screen, nearer things come out of it and farther ones go behind. More "
                        "brings more of the scene out of the screen.",
                        0, "stereo_screen", 50, 200, "%d%%", "stereoscopic 3d convergence screen distance pop out");
  portSettingsSetShown("stereo_depth", [] { return portStereoMode() != 0 ? 1 : 0; });
  portSettingsSetShown("stereo_screen", [] { return portStereoMode() != 0 ? 1 : 0; });
  return true;
}();

} // namespace

extern "C" int portStereoGpuLeftDone(void) {
  if (!registered()) {
    return 0;
  }
  aurora::gfx::ResolvedTargets targets;
  if (!aurora::gfx::resolve_pass({.color = true, .depth = false, .normal = false}, targets) || !targets.color) {
    return 0;
  }
  sLeft = targets.color;
  sLeftFrame = aurora::gfx::current_frame();
  const ClearPayload clear{targets.width, targets.height};
  aurora::gfx::push_custom_draw(sClearType, &clear, sizeof(clear));
  return 1;
}

extern "C" void portStereoGpuCompose(int mode) {
  if (!registered() || sComposedFrame == aurora::gfx::current_frame()) {
    return; // (once a frame)
  }
  sComposedFrame = aurora::gfx::current_frame();
  aurora::gfx::ResolvedTargets targets;
  if (!aurora::gfx::resolve_pass({.color = true, .depth = false, .normal = false}, targets) || !targets.color) {
    sLeft = nullptr;
    return;
  }
  // no left eye this frame (a frame drawn once: its own picture for both eyes, flat)
  wgpu::TextureView left = sLeft && sLeftFrame == aurora::gfx::current_frame() ? sLeft : targets.color;
  sLeft = nullptr;
  Uniforms u{};
  u.size[0] = static_cast<float>(targets.width);
  u.size[1] = static_cast<float>(targets.height);
  u.size[2] = static_cast<float>(mode);
  ComposePayload payload{};
  payload.uniforms = aurora::gfx::push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  if (payload.uniforms.size == 0) {
    aurora::gfx::take_push_full(); // (the frame's uniforms are full: left out, not the next draw)
    return;
  }
  payload.left = left.Get();
  payload.right = targets.color.Get();
  payload.width = targets.width;
  payload.height = targets.height;
  wgpuTextureViewAddRef(payload.left);
  wgpuTextureViewAddRef(payload.right);
  if (!aurora::gfx::push_custom_draw(sComposeType, &payload, sizeof(payload))) {
    wgpuTextureViewRelease(payload.left);
    wgpuTextureViewRelease(payload.right);
  }
}
