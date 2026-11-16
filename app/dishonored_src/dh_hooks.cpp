// Dishonored (2026-11-08): the game-side hooks of the three Direct3D functions Renut's renderer hooks as "shared"
// (BANJO_RENUT_HOOK_SHARED in renut_engine/gpu/d3d/hooks: it only defines renut_native_sub_X, the renderer's half).
// The hook at the game's address is the game port's, as ME2's were in me2_src/me2_hooks.cpp: with the Renut renderer it
// hands the call to renut_native_sub_X (SA2_RENUT_YIELD_SHARED returns from here), otherwise the original runs.
// Without these, Dishonored's own Swap and Draw*Vertices ran: draws never reached the renderer and frames never ended.
// Addresses: out/d3d/dishonored_d3d_funcs.toml (verified against ME2's, see that file).
#include "dh_ab_cycle.h"
#include <atomic>
#include <cstdint>
#include <string>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#include "renut_engine/gpu/sa2_renut_yield.h"
#include "sa2_capture.h"

// RenderDoc capture on demand (PC diagnostic, 2026-11-08): dh_renderdoc_capture_frame = N starts a RenderDoc capture
// at game frame N and ends it two frames later (Renut records on its own thread, a little behind the game, so two
// swaps hold one whole frame). Needs RenderDoc in the process: start sa2.exe through renderdoccmd capture
// (tools/win/rdc_capture_dishonored.ps1). Works with the window minimized (no present needed).
#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <renderdoc_app.h>
#endif

REXCVAR_DEFINE_INT32(dh_renderdoc_capture_frame, 0, "Dishonored",
                     "Diagnostic: game frame at which to capture two frames with RenderDoc when sa2.exe runs under "
                     "renderdoccmd capture (0 = off)");

namespace {
#if defined(_WIN32)
RENDERDOC_API_1_0_0* RenderDocApi() {
  HMODULE module = GetModuleHandleA("renderdoc.dll");
  if (!module) return nullptr;
  auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
  RENDERDOC_API_1_0_0* api = nullptr;
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void**>(&api))) return nullptr;
  return api;
}
#endif

void RenderDocOnSwap(uint64_t frame) {
#if defined(_WIN32)
  static const int32_t at = REXCVAR_GET(dh_renderdoc_capture_frame);
  if (at <= 0) return;
  if (frame == uint64_t(at)) {
    RENDERDOC_API_1_0_0* api = RenderDocApi();
    if (!api) {
      REXLOG_WARN("[dh-rdc] frame {}: RenderDoc is not loaded (start through renderdoccmd capture)", frame);
      return;
    }
    api->StartFrameCapture(nullptr, nullptr);
    REXLOG_INFO("[dh-rdc] frame {}: RenderDoc capture started", frame);
  } else if (frame == uint64_t(at) + 2) {
    RENDERDOC_API_1_0_0* api = RenderDocApi();
    const bool ok = api && api->EndFrameCapture(nullptr, nullptr);
    REXLOG_INFO("[dh-rdc] frame {}: RenderDoc capture ended: {}", frame, ok ? "ok" : "FAILED");
  }
#else
  (void)frame;
#endif
}
}  // namespace

void DhStartLoadBoost();  // dh_load_boost.cpp (2026-11-12): the CPU boost mode during loads

REX_EXTERN(__imp__sub_82B03228);
REX_HOOK_RAW(sub_82B03228) {  // D3DDevice_Swap (called by the game's present sub_82776FB0 after the interval sync)
  DhStartLoadBoost();  // its watchdog, at the first Swap (the configuration is loaded by then)
  DhAbTick();  // 2026-11-09: the A/B rotation (dh_ab_cycle.cpp)
  RenderDocOnSwap(g_sa2_game_frames.load(std::memory_order_relaxed));
  SA2_RENUT_YIELD_SHARED(sub_82B03228);
  __imp__sub_82B03228(ctx, base);
  g_sa2_game_frames.fetch_add(1, std::memory_order_relaxed);
}

// Guest memory access for the hooks below (as the generated code's REX_RAW_ADDR).
#include <algorithm>
#include <cstring>

#include <rex/logging.h>

namespace {
uint8_t* HostAt(uint8_t* base, uint32_t a) {  // as the generated code's REX_RAW_ADDR
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)
  return base + a + (a >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + a;
#endif
}
uint32_t LoadBE(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, HostAt(base, a), 4);
  return __builtin_bswap32(v);
}
void StoreLE(uint8_t* base, uint32_t a, uint32_t v) {  // the GPU's byte order (the guest swaps after loading)
  std::memcpy(HostAt(base, a), &v, 4);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Shader constant blocks (2026-11-08). Besides the device's constant table, an Xbox 360 shader carries constant data in
// its GPU memory, next to its code. At every draw, Direct3D's shader flush (Dishonored sub_82B053F8) calls
// sub_82B05208(device, table, physical base) for the bound pixel shader (table = shader + 40, base = shader + 24) and
// vertex shader (shader + 872, shader + 32), which writes one LOAD_ALU_CONSTANT packet per table entry: the GPU loads
// those registers straight from the shader's memory, never through the device's table. Renut reads only the device's
// table, so these constants reached the renderer as zeros or stale values (FXAA's thresholds and direction weights
// c3-c6: bright outlines along every edge). Table layout as sub_82B05208 reads it: header at table + [table + 20];
// entries from header + 20, header[+16] bytes of them: u16 first register, u16 count (0 ends), u32 offset from base.
// Here, before each draw reaches the renderer, the blocks are copied into the device's constant table (VS then PS float
// constants, contiguous from +1920, as the GPU numbers them) and their 4-register groups marked in m_Pending.
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(dh_shader_constant_blocks, true, "Dishonored",
                    "Load each bound shader's own constant blocks (the console's LOAD_ALU_CONSTANT at the shader flush) "
                    "into the device's constant table before a draw. Off: the renderer sees only the table");
REXCVAR_DEFINE_INT32(dh_shader_constant_blocks_log, 0, "Dishonored",
                     "Diagnostic: log the constant block tables of the first N distinct shaders ([dh-cblocks])");

namespace {
constexpr uint32_t kDevicePixelShader = 13068, kDeviceVertexShader = 13072;  // SA2 / Dishonored layout
constexpr uint32_t kDeviceFloatConstants = 1920;  // m_Constants: VertexShaderF[256], then PixelShaderF[256]
constexpr uint32_t kFloatRegisters = 512;

uint16_t LoadBE16(uint8_t* base, uint32_t a) {
  uint16_t v;
  std::memcpy(&v, HostAt(base, a), 2);
  return __builtin_bswap16(v);
}

void MarkPending(uint8_t* base, uint32_t device, uint32_t first_reg, uint32_t regs) {
  for (uint32_t r = first_reg; r < first_reg + regs && r < kFloatRegisters; r += 4) {
    const uint32_t word = r < 256 ? 0 : 1;  // m_Pending[0] vertex, [1] pixel float constants
    const uint32_t group = (r % 256) / 4;   // bit 63 - group, as the renderer reads it (queued.cpp)
    uint8_t* p = HostAt(base, device + word * 8);
    uint64_t v;
    std::memcpy(&v, p, 8);
    v = __builtin_bswap64(__builtin_bswap64(v) | (1ull << (63 - group)));
    std::memcpy(p, &v, 8);
  }
}

void LoadConstantBlocks(uint8_t* base, uint32_t device, uint32_t shader, uint32_t table, uint32_t physical,
                        bool pixel) {
  if (!table || !physical) return;
  const uint32_t rel = LoadBE(base, table + 20);
  if (!rel) return;
  const uint32_t header = table + rel;
  uint32_t p = header + 20;
  const uint32_t end = p + LoadBE(base, header + 16);
  static const int32_t log_max = REXCVAR_GET(dh_shader_constant_blocks_log);
  static int32_t logged = 0;
  static uint32_t last_logged = 0;
  const bool log = log_max > 0 && logged < log_max && shader != last_logged;
  if (log) {
    last_logged = shader;
    ++logged;
  }
  for (int guard = 0; p < end && guard < 64; ++guard) {
    const uint32_t reg = LoadBE16(base, p), count = LoadBE16(base, p + 2);
    if (!count) break;
    const uint32_t offset = LoadBE(base, p + 4);
    p += 8;
    // The packet takes the register times 4 (a dword index into the 512-register file) and the count as it is: dwords.
    const uint32_t regs = (count + 3) / 4;
    if (log)
      REXLOG_INFO("[dh-cblocks] {} shader {:08X}: register {} count {} ({} registers) from {:08X} + {:X}; first "
                  "{:08X} {:08X} {:08X} {:08X}", pixel ? "pixel" : "vertex", shader, reg, count, regs, physical,
                  offset, LoadBE(base, physical + offset), LoadBE(base, physical + offset + 4),
                  LoadBE(base, physical + offset + 8), LoadBE(base, physical + offset + 12));
    if (reg >= kFloatRegisters || reg + regs > kFloatRegisters) continue;
    std::memcpy(HostAt(base, device + kDeviceFloatConstants + reg * 16), HostAt(base, physical + offset), count * 4);
    MarkPending(base, device, reg, regs);
  }
}
}  // namespace

// Also called by Renut's Begin*Vertices hooks (hooks/draw.cpp), the draws that do not go through these two.
extern "C" void DhLoadShaderConstantBlocks(uint8_t* base, uint32_t device) {
  static const bool on = REXCVAR_GET(dh_shader_constant_blocks);
  if (!on || !device) return;
  if (const uint32_t ps = LoadBE(base, device + kDevicePixelShader))
    LoadConstantBlocks(base, device, ps, ps + 40, LoadBE(base, ps + 24), true);
  if (const uint32_t vs = LoadBE(base, device + kDeviceVertexShader))
    LoadConstantBlocks(base, device, vs, vs + 872, LoadBE(base, vs + 32), false);
}

// Draw trace (diagnostic): dh_draw_trace_frame = N logs every draw of game frame N with its shaders, render target 0,
// the device's pixel constants c3-c6 and the pending masks ([dh-draw]), to see what a pass reads.
REXCVAR_DEFINE_INT32(dh_draw_trace_frame, 0, "Dishonored",
                     "Diagnostic: log every draw of this game frame ([dh-draw]; 0 = off)");
REXCVAR_DEFINE_BOOL(dh_draw_trace_rdc, false, "Dishonored",
                    "Diagnostic: log every draw of each frame RenderDoc captures ([dh-draw]; the RenderDoc launcher "
                    "turns it on)");

namespace {
void TraceDraw(uint8_t* base, uint32_t device, const char* kind) {
  static const int32_t at = REXCVAR_GET(dh_draw_trace_frame);
  static const bool with_rdc = REXCVAR_GET(dh_draw_trace_rdc);
  const uint64_t frame = g_sa2_game_frames.load(std::memory_order_relaxed);
  bool on = at > 0 && frame == uint64_t(at);
#if defined(_WIN32)
  // While RenderDoc records a frame (F12 under run_dishonored_pc_renderdoc.bat), every draw of it: the device's
  // constants next to the capture, which cannot show Renut's constant buffers (buffer device addresses).
  if (!on && with_rdc) {
    static RENDERDOC_API_1_0_0* api = RenderDocApi();
    on = api && api->IsFrameCapturing();
  }
#endif
  if (!on) return;
  static std::atomic<uint32_t> n{0};
  static uint64_t header_frame = ~0ull;
  if (header_frame != frame) {
    header_frame = frame;
    n = 0;
    REXLOG_INFO("[dh-draw] ---- game frame {} (draw numbers restart; RenderDoc's draws follow the same order)", frame);
  }
  {
    // The pixel shader's float constants c0-c11 and c249-c255 (its literal block), as floats.
    auto f = [&](uint32_t reg, uint32_t k) {
      const uint32_t w = LoadBE(base, device + kDeviceFloatConstants + (256 + reg) * 16 + k * 4);
      float v;
      std::memcpy(&v, &w, 4);
      return v;
    };
    std::string line;
    for (uint32_t reg : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 249u, 250u, 251u, 252u, 253u, 254u, 255u})
      line += fmt::format(" c{}=({:.4g} {:.4g} {:.4g} {:.4g})", reg, f(reg, 0), f(reg, 1), f(reg, 2), f(reg, 3));
    REXLOG_INFO("[dh-draw] {} constants{}", n.load(), line);
  }
  const uint32_t ps = LoadBE(base, device + kDevicePixelShader), vs = LoadBE(base, device + kDeviceVertexShader);
  const uint32_t rt0 = LoadBE(base, device + 12816);  // RenderTargets[0] (SA2 / Dishonored layout)
  auto c = [&](uint32_t reg, uint32_t k) { return LoadBE(base, device + kDeviceFloatConstants + reg * 16 + k * 4); };
  REXLOG_INFO("[dh-draw] {} {} ps {:08X} vs {:08X} rt0 {:08X} pending {:08X}{:08X} {:08X}{:08X} | c3 {:08X} {:08X} "
              "{:08X} {:08X} c4 {:08X} {:08X} {:08X} {:08X} c5 {:08X} {:08X} {:08X} {:08X} c6 {:08X} {:08X} {:08X} "
              "{:08X}", n.fetch_add(1), kind, ps, vs, rt0, LoadBE(base, device), LoadBE(base, device + 4),
              LoadBE(base, device + 8), LoadBE(base, device + 12), c(259, 0), c(259, 1), c(259, 2), c(259, 3),
              c(260, 0), c(260, 1), c(260, 2), c(260, 3), c(261, 0), c(261, 1), c(261, 2), c(261, 3), c(262, 0),
              c(262, 1), c(262, 2), c(262, 3));
  // The first four texture fetch constants (m_Constants at +0x480, 6 words each): their base address and exponent
  // adjust (word 3, bits 13-18, signed).
  for (uint32_t slot = 0; slot < 4; ++slot) {
    const uint32_t f = device + 0x480 + slot * 24;
    const uint32_t w1 = LoadBE(base, f + 4), w3 = LoadBE(base, f + 12);
    const int32_t exp_adjust = int32_t(w3 << (31 - 18)) >> (31 - 18 + 13);
    if (w1)
      REXLOG_INFO("[dh-draw]   fetch {}: words {:08X} {:08X} {:08X} {:08X} exp adjust {}", slot, LoadBE(base, f),
                  w1, LoadBE(base, f + 8), w3, exp_adjust);
  }
}

void DhDrawPrepare(uint8_t* base, uint32_t device, const char* kind) {
  TraceDraw(base, device, kind);
  DhLoadShaderConstantBlocks(base, device);
}
}  // namespace

// Renut's Begin*Vertices hooks call this (hooks/draw.cpp, null in a build without the game port's code).
extern void (*g_dh_draw_prepare)(uint8_t* base, uint32_t device, const char* kind);
static const bool g_dh_draw_prepare_set = (g_dh_draw_prepare = DhDrawPrepare, true);

// ---------------------------------------------------------------------------------------------------------------------
// Switch core layout (2026-11-08, from Lazarus me2_hooks.cpp, me2_core_layout 1): the thread issuing the D3D draws
// (UE3's render thread, which also runs Renut's per-draw hooks) alone on core 0, the other guest threads on cores 1-2
// and, with the NSP forwarder, core 3 (which they share with the Wwise thread, switch_audio_guest_entries). Re-evaluated
// every kLayoutDraws draws of a thread: the first thread to draw can be a loading or movie thread that ends later.
// ---------------------------------------------------------------------------------------------------------------------
REXCVAR_DEFINE_INT32(dh_core_layout, 1, "Dishonored",
                     "Switch: 1 = the D3D draw thread (UE3's render thread) alone on core 0, the other guest threads on "
                     "cores 1-2 (+3 with the forwarder); 0 = off (every guest thread on any core)")
    .range(0, 1)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_core_layout_others, 0, "Dishonored",
                     "Switch, dh_core_layout 1: core mask of the other guest threads (0 = 0x6 plus core 3 when the "
                     "process has it)")
    .range(0, 15)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
extern "C" uint32_t RexSetGuestThreadCoreLayoutCaller(int main_core, uint32_t others_mask, int others_preferred);
#if REX_PLATFORM_SWITCH
extern "C" unsigned long long RexSwitchProcessCoreMask(void);  // sdk core/threading_switch.cpp
#endif
namespace {
constexpr uint32_t kLayoutDraws = 20000;
thread_local uint32_t t_layout_draws = 0;
std::atomic<const void*> g_layout_owner{nullptr};  // &t_layout_draws of the thread on core 0
std::atomic<uint32_t> g_layout_changes{0};
inline void RenderThreadLayoutOnce() {
#if REX_PLATFORM_SWITCH
  if (++t_layout_draws < kLayoutDraws) return;
  t_layout_draws = 0;
  if (g_layout_owner.load(std::memory_order_relaxed) == &t_layout_draws) return;
  if (REXCVAR_GET(dh_core_layout) != 1) return;
  g_layout_owner.store(&t_layout_draws, std::memory_order_relaxed);
  const uint32_t process = uint32_t(RexSwitchProcessCoreMask());
  const uint32_t others_set = uint32_t(REXCVAR_GET(dh_core_layout_others)) & process;
  const uint32_t others = others_set ? others_set : 0x6u | (process & 0x8u);
  const int others_pref = (others & 0x4u) ? 2 : __builtin_ctz(others);
  const uint32_t moved = RexSetGuestThreadCoreLayoutCaller(0, others, others_pref);
  if (g_layout_changes.fetch_add(1, std::memory_order_relaxed) < 16) {
    REXLOG_INFO("[dh] core layout: the D3D draw thread (UE3's render thread) alone on core 0, the other guest threads "
                "on cores {:#x} ({} threads; process cores {:#x})", others, moved, process);
  }
#endif
}
}  // namespace

REX_EXTERN(__imp__sub_82AFD5A0);
REX_HOOK_RAW(sub_82AFD5A0) {  // D3DDevice_DrawIndexedVertices
  RenderThreadLayoutOnce();
  DhDrawPrepare(base, ctx.r3.u32, "DrawIndexedVertices");
  SA2_RENUT_YIELD_SHARED(sub_82AFD5A0);
  __imp__sub_82AFD5A0(ctx, base);
}

extern "C" bool DhOcclusionSkipDraw();  // dh_occlusion.cpp (dh_raster_skip_gpu_boxes)
REX_EXTERN(__imp__sub_82AFD188);
REX_HOOK_RAW(sub_82AFD188) {  // D3DDevice_DrawVertices
  if (DhOcclusionSkipDraw()) return;  // an occlusion box the rasterizer answers for: not drawn
  DhDrawPrepare(base, ctx.r3.u32, "DrawVertices");
  SA2_RENUT_YIELD_SHARED(sub_82AFD188);
  __imp__sub_82AFD188(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------------
// Occlusion query results: dishonored_src/dh_occlusion.cpp (2026-11-09: real host counts; before, every query read
// "visible" here).

// ---------------------------------------------------------------------------------------------------------------------
// Ahead-of-time shaders (2026-11-12, diagnostic): where the shader cache took a shader's code. FShader::InitRHI
// (sub_823ABFD0) asks each loaded shader cache (list at 0x83500174, per platform) through sub_823A94E8(cache, shader,
// platform, &code): TMap find of the shader type (sub_824BC970 on cache + 24, key shader + 52) -> type entry, TMap find
// of the shader's key (sub_823B3560, key at shader + 56) -> record {u16 chunk, u16 size, u32 offset}; the chunk
// {u32 uncompressed size, u32 compressed data, u32 compressed size, ?} (entry + 60, 16 bytes each) is decompressed whole
// (appDecompressMemory, LZX) and size bytes copied from offset. With dh_shader_trace set, this repeats the two finds
// after a successful lookup and leaves the record for the [shader-trace] / capture line of the shader this thread
// creates next (XGRegister*Shader, same call of InitRHI), with the compressed chunk dumped next to the captures.
#include <string>

REXCVAR_DECLARE(std::string, dh_shader_trace);  // renut_engine/gpu/render/shader_library.cpp
namespace renut::gpu {
void SetShaderSourceNote(std::string note, const uint8_t* chunk, uint32_t chunk_size);  // shader_library.cpp
}

REX_EXTERN(__imp__sub_823A94E8);
REX_EXTERN(__imp__sub_824BC970);
REX_EXTERN(__imp__sub_823B3560);
REX_HOOK_RAW(sub_823A94E8) {
  static const bool tracing = !REXCVAR_GET(dh_shader_trace).empty();
  const uint32_t cache = ctx.r3.u32, shader = ctx.r4.u32;
  __imp__sub_823A94E8(ctx, base);
  if (!tracing || ctx.r3.u32 == 0) return;

  PPCContext saved = ctx;
  const uint32_t type = LoadBE(base, shader + 52);
  ctx.r3.u32 = cache + 24;
  ctx.r4.u32 = type;
  __imp__sub_824BC970(ctx, base);
  const uint32_t entry = ctx.r3.u32;
  uint32_t record = 0;
  if (entry) {
    ctx.r3.u32 = entry;
    ctx.r4.u32 = shader + 56;
    __imp__sub_823B3560(ctx, base);
    record = ctx.r3.u32;
  }
  ctx = saved;
  if (!record) return;

  std::string key;
  for (uint32_t i = 0; i < 20; ++i) key += fmt::format("{:02x}", *HostAt(base, shader + 56 + i));
  const uint32_t index = LoadBE16(base, record), size = LoadBE16(base, record + 2), offset = LoadBE(base, record + 4);
  const uint32_t chunks = LoadBE(base, entry + 60), chunk_count = LoadBE(base, entry + 64);
  const uint32_t chunk = chunks + index * 16;
  const uint32_t raw_size = LoadBE(base, chunk), data = LoadBE(base, chunk + 4), data_size = LoadBE(base, chunk + 8);
  renut::gpu::SetShaderSourceNote(
      fmt::format("cache {:08X} type {:08X} key {} chunk {}/{} (+12 {:08X}): {} bytes at {} of {} "
                  "uncompressed; compressed {} bytes at {:08X}",
                  cache, type, key, index, chunk_count, LoadBE(base, chunk + 12), size, offset,
                  raw_size, data_size, data),
      data && data_size < (64u << 20) ? HostAt(base, data) : nullptr, data_size);
}
