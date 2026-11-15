// Frame interpolation by replay (port/src/aurora/interp_replay_aurora.c, docs/FRAME_INTERPOLATION.md): the GPU's
// side. The game's own frame is drawn first; its in-between frames come before it on screen. So at the retrace the
// game's picture is kept (a pass snapshot copied into a texture of our own, which outlives the frame), the frame is
// cleared for the first in-between frame, and after the in-between frames the kept picture is drawn into a frame
// of its own and shown at its time. Draw-only.
#include <aurora/gfx.hpp>
#include <webgpu/webgpu_cpp.h>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

struct TexturePayload {
  WGPUTexture src; // referenced; released by the task
  WGPUTexture dst;
  uint32_t width, height;
};

struct ViewPayload {
  WGPUTextureView view; // referenced; released by the draw
};

constexpr const char* kShaders = R"(
struct VsOut {
  @builtin(position) pos: vec4f,
};

@vertex
fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {
  let corner = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
  var o: VsOut;
  o.pos = vec4f(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, FAR, 1.0);
  return o;
}

@group(0) @binding(0) var held: texture_2d<f32>;
@group(0) @binding(1) var<uniform> target_size: vec4f;

@fragment
fn fs_clear(in: VsOut) -> @location(0) vec4f {
  return vec4f(0.0, 0.0, 0.0, 0.0);
}

@fragment
fn fs_show(in: VsOut) -> @location(0) vec4f {
  let dims = vec2f(textureDimensions(held));
  let p = in.pos.xy * dims / max(target_size.xy, vec2f(1.0));
  return textureLoad(held, clamp(vec2i(floor(p)), vec2i(0), vec2i(dims) - vec2i(1)), 0);
}
)";

bool sUnsupported = false;
aurora::gfx::DrawTypeId sClearType = aurora::gfx::InvalidDrawType;
aurora::gfx::DrawTypeId sShowType = aurora::gfx::InvalidDrawType;
aurora::gfx::EncoderTaskId sCopyType = aurora::gfx::InvalidEncoderTask;
wgpu::Texture sHeld;
wgpu::TextureView sHeldView;
uint32_t sHeldWidth = 0, sHeldHeight = 0;
wgpu::TextureFormat sHeldFormat = wgpu::TextureFormat::Undefined;
bool sHeldValid = false;

// (render worker)
std::mutex sMutex;
wgpu::ShaderModule sModule;
wgpu::BindGroupLayout sLayout;
wgpu::Buffer sSizeBuffer; // the target's size, written before each show
std::unordered_map<uint64_t, wgpu::RenderPipeline> sClearPipelines, sShowPipelines;

wgpu::RenderPipeline pipelineFor(const aurora::gfx::DrawContext& ctx, bool show) {
  std::lock_guard lock{sMutex};
  auto& cache = show ? sShowPipelines : sClearPipelines;
  if (const auto it = cache.find(ctx.layout.key); it != cache.end()) {
    return it->second;
  }
  if (!sModule) {
    std::string code = kShaders;
    code.replace(code.find("FAR"), 3, aurora::gfx::uses_reversed_z() ? "0.0" : "1.0");
    wgpu::ShaderSourceWGSL source{};
    source.code = code.c_str();
    const wgpu::ShaderModuleDescriptor desc{.nextInChain = &source, .label = "Interpolation hold"};
    sModule = ctx.device.CreateShaderModule(&desc);
    const wgpu::BindGroupLayoutEntry entries[] = {
        {.binding = 0,
         .visibility = wgpu::ShaderStage::Fragment,
         .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                     .viewDimension = wgpu::TextureViewDimension::e2D}},
        {.binding = 1,
         .visibility = wgpu::ShaderStage::Fragment,
         .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 16}},
    };
    const wgpu::BindGroupLayoutDescriptor layoutDesc{
        .label = "Interpolation hold bind group layout", .entryCount = 2, .entries = entries};
    sLayout = ctx.device.CreateBindGroupLayout(&layoutDesc);
    const wgpu::BufferDescriptor bufferDesc{
        .label = "Interpolation hold size", .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, .size = 16};
    sSizeBuffer = ctx.device.CreateBuffer(&bufferDesc);
  }
  wgpu::ColorTargetState targets[aurora::gfx::MaxColorAttachments]{};
  const uint32_t count = std::max<uint32_t>(ctx.layout.colorAttachmentCount, 1);
  for (uint32_t i = 0; i < count && i < aurora::gfx::MaxColorAttachments; ++i) {
    targets[i].format = ctx.layout.colorAttachments[i].format;
    targets[i].writeMask =
        i == aurora::gfx::SceneColorAttachmentIndex ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
  }
  const wgpu::FragmentState fragment{
      .module = sModule, .entryPoint = show ? "fs_show" : "fs_clear", .targetCount = count, .targets = targets};
  const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc{.bindGroupLayoutCount = show ? 1u : 0u,
                                                          .bindGroupLayouts = show ? &sLayout : nullptr};
  const auto pipelineLayout = ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);
  wgpu::DepthStencilState depth{
      .format = ctx.layout.depthStencilFormat,
      .depthWriteEnabled = wgpu::OptionalBool::True, // (the far depth, as the frame's own clear)
      .depthCompare = wgpu::CompareFunction::Always,
  };
  const wgpu::RenderPipelineDescriptor desc{
      .label = show ? "Interpolation hold show" : "Interpolation hold clear",
      .layout = pipelineLayout,
      .vertex = {.module = sModule, .entryPoint = "vs_main"},
      .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
      .depthStencil = ctx.layout.depthStencilFormat != wgpu::TextureFormat::Undefined ? &depth : nullptr,
      .multisample = {.count = std::max<uint32_t>(ctx.layout.sampleCount, 1), .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  auto pipeline = ctx.device.CreateRenderPipeline(&desc);
  cache.emplace(ctx.layout.key, pipeline);
  return pipeline;
}

void fullTarget(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass) {
  const auto& color = ctx.layout.colorAttachments[aurora::gfx::SceneColorAttachmentIndex];
  const uint32_t w = std::max<uint32_t>(color.width, 1), h = std::max<uint32_t>(color.height, 1);
  pass.SetViewport(0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f);
  pass.SetScissorRect(0, 0, w, h);
}

// (render worker) the frame cleared for an in-between frame: black, the far depth
void drawClear(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void*, size_t,
               void*) {
  const auto pipeline = pipelineFor(ctx, false);
  if (!pipeline) {
    return;
  }
  pass.SetPipeline(pipeline);
  fullTarget(ctx, pass);
  pass.Draw(3);
}

// (render worker) the kept picture into the frame
void drawShow(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass, const void* data,
              size_t size, void*) {
  if (size != sizeof(ViewPayload)) {
    return;
  }
  ViewPayload payload;
  std::memcpy(&payload, data, sizeof(payload));
  const auto view = wgpu::TextureView::Acquire(payload.view);
  const auto pipeline = pipelineFor(ctx, true);
  if (!pipeline || !view) {
    return;
  }
  const auto& color = ctx.layout.colorAttachments[aurora::gfx::SceneColorAttachmentIndex];
  const float targetSize[4] = {static_cast<float>(color.width), static_cast<float>(color.height), 0.f, 0.f};
  ctx.queue.WriteBuffer(sSizeBuffer, 0, targetSize, sizeof(targetSize));
  const wgpu::BindGroupEntry entries[] = {
      {.binding = 0, .textureView = view},
      {.binding = 1, .buffer = sSizeBuffer, .offset = 0, .size = 16},
  };
  const wgpu::BindGroupDescriptor bindDesc{.layout = sLayout, .entryCount = 2, .entries = entries};
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, ctx.device.CreateBindGroup(&bindDesc), 0, nullptr);
  fullTarget(ctx, pass);
  pass.Draw(3);
}

// (render worker) the snapshot copied into the kept texture
void copyTask(const aurora::gfx::EncoderTaskContext&, const wgpu::CommandEncoder& cmd, const void* data, size_t size,
              void*) {
  if (size != sizeof(TexturePayload)) {
    return;
  }
  TexturePayload payload;
  std::memcpy(&payload, data, sizeof(payload));
  const auto src = wgpu::Texture::Acquire(payload.src);
  const auto dst = wgpu::Texture::Acquire(payload.dst);
  if (!src || !dst) {
    return;
  }
  const wgpu::TexelCopyTextureInfo from{.texture = src};
  const wgpu::TexelCopyTextureInfo to{.texture = dst};
  const wgpu::Extent3D extent{payload.width, payload.height, 1};
  cmd.CopyTextureToTexture(&from, &to, &extent);
}

bool registered() {
  if (sUnsupported) {
    return false;
  }
  if (sClearType == aurora::gfx::InvalidDrawType) {
    sClearType = aurora::gfx::register_draw_type({.label = "Interpolation hold clear", .draw = drawClear});
    sShowType = aurora::gfx::register_draw_type({.label = "Interpolation hold show", .draw = drawShow});
    sCopyType = aurora::gfx::register_encoder_task_type({.label = "Interpolation hold copy", .callback = copyTask});
    if (sClearType == aurora::gfx::InvalidDrawType || sShowType == aurora::gfx::InvalidDrawType ||
        sCopyType == aurora::gfx::InvalidEncoderTask) {
      SDL_Log("interp: no custom draws here: replayed in-between frames off for this run");
      sUnsupported = true;
      return false;
    }
  }
  return true;
}

} // namespace

// The frame's picture so far kept, and the frame cleared (black, the far depth) for an in-between frame. 0 when it
// can't be kept (then nothing changed).
extern "C" int portInterpGpuHold(void) {
  sHeldValid = false;
  if (!registered()) {
    return 0;
  }
  aurora::gfx::ResolvedTargets targets;
  if (!aurora::gfx::resolve_pass({.color = true, .depth = false, .normal = false}, targets) || !targets.colorTexture ||
      targets.width == 0 || targets.height == 0) {
    return 0;
  }
  if (!sHeld || sHeldWidth != targets.width || sHeldHeight != targets.height || sHeldFormat != targets.colorFormat) {
    const wgpu::TextureDescriptor desc{
        .label = "Interpolation held frame",
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
        .size = {targets.width, targets.height, 1},
        .format = targets.colorFormat,
    };
    sHeld = aurora::gfx::device().CreateTexture(&desc);
    sHeldView = sHeld ? sHeld.CreateView() : nullptr;
    sHeldWidth = targets.width;
    sHeldHeight = targets.height;
    sHeldFormat = targets.colorFormat;
  }
  if (!sHeld || !sHeldView) {
    return 0;
  }
  TexturePayload copy{targets.colorTexture.Get(), sHeld.Get(), targets.width, targets.height};
  wgpuTextureAddRef(copy.src);
  wgpuTextureAddRef(copy.dst);
  if (!aurora::gfx::push_encoder_task(sCopyType, &copy, sizeof(copy))) {
    wgpuTextureRelease(copy.src);
    wgpuTextureRelease(copy.dst);
    return 0;
  }
  aurora::gfx::push_custom_draw(sClearType, nullptr, 0);
  sHeldValid = true;
  return 1;
}

// The frame cleared (an in-between frame drawn again in the same frame: stereo's other eye has its own clear).
extern "C" void portInterpGpuClear(void) {
  if (registered()) {
    aurora::gfx::push_custom_draw(sClearType, nullptr, 0);
  }
}

// The kept picture drawn into the frame (0 when there is none).
extern "C" int portInterpGpuShowHeld(void) {
  if (!sHeldValid || !registered() || !sHeldView) {
    return 0;
  }
  ViewPayload payload{sHeldView.Get()};
  wgpuTextureViewAddRef(payload.view);
  if (!aurora::gfx::push_custom_draw(sShowType, &payload, sizeof(payload))) {
    wgpuTextureViewRelease(payload.view);
    return 0;
  }
  sHeldValid = false;
  return 1;
}
