// Ray-traced lighting (setting rt_lighting: Off, On, High; Graphics tab): rays traced through the 3D scene as it
// is on screen, against its depth (WebGPU has no hardware ray tracing on any of our targets). One full-screen pass
// over the 3D scene, under the HUD (port/src/aurora/rt_lighting_aurora.c calls portRtLightingDraw where bloom
// goes): ambient occlusion from rays over each surface's hemisphere, a contact shadow from a ray toward the
// scene's main light, and on High one bounce of the colour the rays hit. Aurora's resolve_pass snapshots the
// colour and depth, aurora_scene_view (port/patches/aurora/0033) gives the projection the draws used, and the
// pass is a custom draw. Windows and Linux desktops only; where the device can't snapshot depth or the pipeline
// fails, it turns itself off for the run and the row goes. Draw-only: nothing the game reads changes.
#include <aurora/gfx.hpp>
#include <aurora/scene.h>
#include <webgpu/webgpu_cpp.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "port/settings.h"

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#define RT_LIGHTING_DESKTOP 1
#else
#define RT_LIGHTING_DESKTOP 0
#endif

namespace {

const char* const kLevels[] = {"Off", "On", "High"};

struct Uniforms {
  float proj[16];     // rows: clip = proj * view
  float invProj[16];  // rows: view (homogeneous) = invProj * (ndc, depth, 1)
  float viewport[4];  // left, top, width, height (target pixels)
  float target[4];    // width, height, depth range min, max
  float light[4];     // toward the light, view space; w: strength
  float params[4];    // radius, occlusion strength, bounce strength, shadow strength
  float params2[4];   // shadow length, thickness, reversed Z (1/0), 0
  float rays[4];      // rays, steps a ray, shadow steps, bounce (1/0)
};
static_assert(sizeof(Uniforms) % 16 == 0);

struct Payload {
  aurora::gfx::Range uniforms;
  WGPUTextureView color; // referenced (AddRef'd); released by the draw
  WGPUTextureView depth;
};

constexpr const char* kShader = R"(
struct U {
  proj: mat4x4f,
  inv_proj: mat4x4f,
  viewport: vec4f,
  tgt: vec4f,
  light: vec4f,
  params: vec4f,
  params2: vec4f,
  rays: vec4f,
};
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var color_tex: texture_2d<f32>;
@group(0) @binding(2) var depth_tex: texture_2d<f32>;

struct VsOut {
  @builtin(position) pos: vec4f,
};

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
  let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  var o: VsOut;
  o.pos = vec4f(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
  return o;
}

fn far_depth(d: f32) -> bool {
  return select(d >= 0.99999, d <= 0.00001, u.params2.z > 0.5);
}

fn load_depth(p: vec2i) -> f32 {
  let c = clamp(p, vec2i(0), vec2i(u.tgt.xy) - vec2i(1));
  return textureLoad(depth_tex, c, 0).r;
}

// (rows uploaded as vec4s: v * M is M (row-major) times v)
fn view_at(px: vec2f, d: f32) -> vec3f {
  let ndc = vec2f((px.x - u.viewport.x) / u.viewport.z * 2.0 - 1.0,
                  1.0 - (px.y - u.viewport.y) / u.viewport.w * 2.0);
  let z = (d - u.tgt.z) / max(u.tgt.w - u.tgt.z, 1e-6);
  let h = vec4f(ndc, z, 1.0) * u.inv_proj;
  return h.xyz / h.w;
}

// a view-space point to target pixels; z < 0: behind the eye
fn to_screen(v: vec3f) -> vec3f {
  let c = vec4f(v, 1.0) * u.proj;
  if (c.w <= 1e-4) {
    return vec3f(0.0, 0.0, -1.0);
  }
  let n = c.xy / c.w;
  return vec3f(u.viewport.x + (n.x * 0.5 + 0.5) * u.viewport.z, u.viewport.y + (0.5 - n.y * 0.5) * u.viewport.w, 1.0);
}

fn inside(s: vec3f) -> bool {
  return s.z > 0.0 && s.x >= u.viewport.x && s.y >= u.viewport.y && s.x < u.viewport.x + u.viewport.z &&
         s.y < u.viewport.y + u.viewport.w;
}

fn ign(p: vec2f) -> f32 {
  return fract(52.9829189 * fract(dot(p, vec2f(0.06711056, 0.00583715))));
}

// march from o along dir over len in steps; the distance (0-1 of len) to the first surface in front of the ray
// by less than the thickness, else -1; hit_px: where
fn march(o: vec3f, dir: vec3f, len: f32, steps: i32, jitter: f32, hit_px: ptr<function, vec2i>) -> f32 {
  let thick = u.params2.y;
  for (var j = 0; j < steps; j++) {
    let f = (f32(j) + jitter) / f32(steps);
    let s = o + dir * (len * f);
    let px = to_screen(s);
    if (!inside(px)) {
      return -1.0;
    }
    let q = vec2i(px.xy);
    let dq = load_depth(q);
    if (far_depth(dq)) {
      continue;
    }
    let scene = view_at(vec2f(q) + vec2f(0.5), dq);
    let gap = scene.z - s.z; // > 0: the scene there is nearer the eye than the ray
    if (gap > 0.0 && gap < thick) {
      *hit_px = q;
      return f;
    }
  }
  return -1.0;
}

@fragment
fn fs_main(in: VsOut) -> @location(0) vec4f {
  let p = vec2i(in.pos.xy);
  let col = textureLoad(color_tex, p, 0);
  let d = load_depth(p);
  if (far_depth(d) || !inside(vec3f(in.pos.xy, 1.0))) {
    return col;
  }
  let P = view_at(in.pos.xy, d);
  let pr = view_at(in.pos.xy + vec2f(1.0, 0.0), load_depth(p + vec2i(1, 0)));
  let pl = view_at(in.pos.xy - vec2f(1.0, 0.0), load_depth(p - vec2i(1, 0)));
  let pd = view_at(in.pos.xy + vec2f(0.0, 1.0), load_depth(p + vec2i(0, 1)));
  let pu = view_at(in.pos.xy - vec2f(0.0, 1.0), load_depth(p - vec2i(0, 1)));
  let dx = select(pr - P, P - pl, abs(pl.z - P.z) < abs(pr.z - P.z));
  let dy = select(pd - P, P - pu, abs(pu.z - P.z) < abs(pd.z - P.z));
  var n = cross(dx, dy);
  if (dot(n, n) < 1e-12) {
    return col;
  }
  n = normalize(n);
  if (dot(n, -P) < 0.0) {
    n = -n;
  }
  let up = select(vec3f(0.0, 1.0, 0.0), vec3f(1.0, 0.0, 0.0), abs(n.y) > 0.9);
  let t = normalize(cross(up, n));
  let b = cross(n, t);
  let radius = u.params.x;
  let o = P + n * (0.03 * radius + 0.002 * abs(P.z));
  let noise = ign(in.pos.xy);
  let noise2 = ign(in.pos.xy + vec2f(37.0, 17.0));
  let rays = i32(u.rays.x);
  let steps = i32(u.rays.y);
  var occlusion = 0.0;
  var bounce = vec3f(0.0);
  for (var i = 0; i < rays; i++) {
    let fi = (f32(i) + noise) / f32(rays);
    let phi = 6.2831853 * (f32(i) * 0.618034 + noise2);
    let r = sqrt(fi);
    let dir = t * (cos(phi) * r) + b * (sin(phi) * r) + n * sqrt(max(1.0 - fi, 0.0));
    var hit = vec2i(0);
    let f = march(o, dir, radius, steps, noise, &hit);
    if (f >= 0.0) {
      let w = 1.0 - f;
      occlusion += w;
      if (u.rays.w > 0.5) {
        bounce += textureLoad(color_tex, hit, 0).rgb * w;
      }
    }
  }
  occlusion /= f32(rays);
  bounce /= f32(rays);
  var shadow = 0.0;
  let l = u.light.xyz;
  if (u.light.w > 0.0 && dot(n, l) > 0.05) {
    var hit = vec2i(0);
    let f = march(o, l, u.params2.x, i32(u.rays.z), noise, &hit);
    if (f >= 0.0) {
      shadow = (1.0 - f * 0.6) * smoothstep(0.05, 0.3, dot(n, l));
    }
  }
  let lit = col.rgb * (1.0 - u.params.y * occlusion) * (1.0 - u.params.w * u.light.w * shadow) +
            u.params.z * bounce * col.rgb;
  return vec4f(lit, col.a);
}
)";

int* sLevel;
bool sUnsupported = false;
aurora::gfx::DrawTypeId sDrawType = aurora::gfx::InvalidDrawType;

// (render worker)
std::mutex sPipelineMutex;
wgpu::BindGroupLayout sLayout;
wgpu::ShaderModule sModule;
std::unordered_map<uint64_t, wgpu::RenderPipeline> sPipelines;
// the device refused the shader or the pipeline (Dawn hands back error objects, never null:
// its validation scope says so); the main thread then turns the feature off
std::atomic<bool> sPipelineFailed{false};

bool invert4(const float* m, float* out) {
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
           m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
           m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
           m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
            m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
           m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
           m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
           m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
            m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
           m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
           m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
            m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
            m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
           m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
           m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
            m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
            m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) < 1e-20f) {
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    out[i] = inv[i] / det;
  }
  return true;
}

// the pipeline for the pass it draws in (its attachments), made once per layout
wgpu::RenderPipeline pipelineFor(const aurora::gfx::DrawContext& ctx) {
  std::lock_guard lock{sPipelineMutex};
  if (const auto it = sPipelines.find(ctx.layout.key); it != sPipelines.end()) {
    return it->second;
  }
  ctx.device.PushErrorScope(wgpu::ErrorFilter::Validation);
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
        .label = "Ray-traced lighting bind group layout",
        .entryCount = 3,
        .entries = entries,
    };
    sLayout = ctx.device.CreateBindGroupLayout(&layoutDesc);
    wgpu::ShaderSourceWGSL source{};
    source.code = kShader;
    const wgpu::ShaderModuleDescriptor moduleDesc{.nextInChain = &source, .label = "Ray-traced lighting"};
    sModule = ctx.device.CreateShaderModule(&moduleDesc);
  }
  wgpu::ColorTargetState targets[aurora::gfx::MaxColorAttachments]{};
  const uint32_t count = std::max<uint32_t>(ctx.layout.colorAttachmentCount, 1);
  for (uint32_t i = 0; i < count && i < aurora::gfx::MaxColorAttachments; ++i) {
    targets[i].format = ctx.layout.colorAttachments[i].format;
    // the colour, not its alpha (the game's destination alpha); other attachments as they are
    targets[i].writeMask = i == aurora::gfx::SceneColorAttachmentIndex
                               ? wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue
                               : wgpu::ColorWriteMask::None;
  }
  const wgpu::FragmentState fragment{
      .module = sModule,
      .entryPoint = "fs_main",
      .targetCount = count,
      .targets = targets,
  };
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{.bindGroupLayoutCount = 1, .bindGroupLayouts = &sLayout};
  const auto pipelineLayout = ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);
  wgpu::DepthStencilState depth{
      .format = ctx.layout.depthStencilFormat,
      .depthWriteEnabled = wgpu::OptionalBool::False,
      .depthCompare = wgpu::CompareFunction::Always,
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = "Ray-traced lighting",
      .layout = pipelineLayout,
      .vertex = {.module = sModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = ctx.layout.depthStencilFormat != wgpu::TextureFormat::Undefined ? &depth : nullptr,
      .multisample = {.count = std::max<uint32_t>(ctx.layout.sampleCount, 1), .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  auto pipeline = ctx.device.CreateRenderPipeline(&desc);
  ctx.device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                           [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
                             if (status == wgpu::PopErrorScopeStatus::Success && type != wgpu::ErrorType::NoError) {
                               SDL_Log("rt_lighting: the device refused its pipeline: %.*s",
                                       static_cast<int>(message.length == WGPU_STRLEN ? 0 : message.length),
                                       message.data != nullptr ? message.data : "");
                               sPipelineFailed = true;
                             }
                           });
  sPipelines.emplace(ctx.layout.key, pipeline);
  return pipeline;
}

// (render worker) the pass: the snapshots and uniforms bound, a full-screen triangle
void draw(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void* data, size_t size,
          void*) {
  if (size != sizeof(Payload)) {
    return;
  }
  Payload payload;
  std::memcpy(&payload, data, sizeof(payload));
  const auto color = wgpu::TextureView::Acquire(payload.color);
  const auto depth = wgpu::TextureView::Acquire(payload.depth);
  const auto pipeline = pipelineFor(ctx);
  if (sPipelineFailed || !pipeline || !color || !depth) {
    return;
  }
  const wgpu::BindGroupEntry entries[] = {
      {.binding = 0, .buffer = ctx.uniformBuffer, .offset = payload.uniforms.offset, .size = sizeof(Uniforms)},
      {.binding = 1, .textureView = color},
      {.binding = 2, .textureView = depth},
  };
  const wgpu::BindGroupDescriptor bindDesc{.layout = sLayout, .entryCount = 3, .entries = entries};
  const auto bindGroup = ctx.device.CreateBindGroup(&bindDesc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup, 0, nullptr);
  pass.Draw(3);
}

void unsupported(const char* why) {
  if (!sUnsupported) {
    SDL_Log("rt_lighting: not supported here (%s): off for this run", why);
  }
  sUnsupported = true;
}

int shown() { return RT_LIGHTING_DESKTOP && !sUnsupported; }

const bool sAdded = [] {
  sLevel = portSettingsDeclare("rt_lighting", 0, 0, 2);
#if RT_LIGHTING_DESKTOP
  portSettingsAddChoice("Graphics", "Ray-traced lighting",
                        "Light traced through the scene as it is on screen: soft shadows where things meet and "
                        "fold, contact shadows from the room's main light, and on High light bouncing off nearby "
                        "colours. The HUD stays as it is. Costs graphics card time, most at high resolutions. "
                        "Desktop Windows and Linux only.",
                        0, "rt_lighting", kLevels, 3, "ray tracing rtx lighting ambient occlusion ssao gi shadows");
  portSettingsSetShown("rt_lighting", shown);
#endif
  return true;
}();

} // namespace

extern "C" int portRtLightingAvailable(void) { return RT_LIGHTING_DESKTOP && !sUnsupported; }

extern "C" void portRtLightingDraw(int level, const float* light) {
  if (sPipelineFailed) {
    unsupported("the device refused its shader");
  }
  if (!portRtLightingAvailable() || level <= 0) {
    return;
  }
  if (sDrawType == aurora::gfx::InvalidDrawType) {
    sDrawType = aurora::gfx::register_draw_type({.label = "Ray-traced lighting", .draw = draw});
    if (sDrawType == aurora::gfx::InvalidDrawType) {
      unsupported("no custom draws");
      return;
    }
  }
  AuroraSceneView view;
  if (!aurora_scene_view(&view) || view.viewport[2] < 1.f || view.viewport[3] < 1.f) {
    return; // (offscreen, or no frame: not this time)
  }
  Uniforms u{};
  std::memcpy(u.proj, view.proj, sizeof(u.proj));
  if (!invert4(view.proj, u.invProj)) {
    return;
  }
  aurora::gfx::ResolvedTargets targets;
  if (!aurora::gfx::resolve_pass({.color = true, .depth = true, .normal = false}, targets)) {
    return;
  }
  if (!targets.depth || !targets.color) {
    unsupported("no depth snapshots on this device");
    return;
  }
  const bool high = level >= 2;
  std::memcpy(u.viewport, view.viewport, sizeof(u.viewport));
  u.target[0] = static_cast<float>(targets.width);
  u.target[1] = static_cast<float>(targets.height);
  u.target[2] = view.depthRange[0];
  u.target[3] = view.depthRange[1];
  std::memcpy(u.light, light, sizeof(u.light));
  // (the game's units: Mario is about 32 tall)
  u.params[0] = high ? 30.f : 22.f;  // radius
  u.params[1] = high ? 0.7f : 0.6f;  // occlusion
  u.params[2] = high ? 0.45f : 0.f;  // bounce
  u.params[3] = 0.35f;               // contact shadow
  u.params2[0] = 36.f;               // shadow ray length
  u.params2[1] = high ? 14.f : 12.f; // thickness
  u.params2[2] = view.reversedZ ? 1.f : 0.f;
  u.rays[0] = high ? 10.f : 6.f;
  u.rays[1] = high ? 8.f : 6.f;
  u.rays[2] = high ? 16.f : 10.f;
  u.rays[3] = high ? 1.f : 0.f;
  Payload payload{};
  payload.uniforms = aurora::gfx::push_uniform(reinterpret_cast<const uint8_t*>(&u), sizeof(u));
  if (payload.uniforms.size == 0) {
    aurora::gfx::take_push_full(); // (the frame's uniforms are full: left out, not the game's next draw)
    return;
  }
  payload.color = targets.color.Get();
  payload.depth = targets.depth.Get();
  wgpuTextureViewAddRef(payload.color);
  wgpuTextureViewAddRef(payload.depth);
  if (!aurora::gfx::push_custom_draw(sDrawType, &payload, sizeof(payload))) {
    wgpuTextureViewRelease(payload.color);
    wgpuTextureViewRelease(payload.depth);
  }
}
