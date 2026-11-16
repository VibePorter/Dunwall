// Dishonored (2026-11-08): native replacements of hot guest functions, from Lazarus (ME2). The framework is ME2's
// (me2_src/hot/me_hot_guest.cpp built with ME_HOT_FRAMEWORK_ONLY): each hook runs the native and, for the first
// me2_hot_guard_calls calls and 1 in me2_hot_guard_period afterwards, also the recompiled original on the real state,
// comparing registers and written memory; a difference logs "[hot] DIFFERENCE" and turns that hook off for the run.
//
// Only functions whose code is the same as ME2's instruction for instruction (tools/d3d/match_funcs.py against the
// ME2 TU2 generated code, branches relative, every immediate equal): the n_*.h natives are ME2's, named by ME2's
// address. Call sites checked with tools/hot/liveness_me2.py on Dishonored's generated code: after the call they read
// only r3 (the return), plus r7 after the vertex constant setter (neither version changes it) and registers of
// `subfe rX,rY,rY`, which uses only the carry.
#include <mspack.h>  // n_dh_82D9A3B8.h (ME2's LZX through libmspack lzxd)
#include <lzx.h>

#include "../me2_src/hot/me_hot_guest.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include "../me2_src/hot/n/n_82C47090.h"  // strlen
#include "../me2_src/hot/n/n_82D5F6D0.h"  // CRT wcslen
#include "../me2_src/hot/n/n_82D65BD0.h"  // CRT memcpy
#include "../me2_src/hot/n/n_82A92C50.h"  // XDK SetVertex/PixelShaderConstantF fast paths
#include "hot/n_dh_82D9A3B8.h"            // XMemDecompress (LZX) of appDecompressMemory
#include "hot/n_dh_82BB5D28.h"            // Wwise reverb core (ME2 n_82E95FC8, one sum order changed)
#include "hot/n_dh_82BB4C58.h"            // Wwise delay line, two operations (ME2 n_82E94F28 + an n == 0 guard)
#include "hot/n_dh_8251FE10.h"            // UE3 TSet<pointer>::FindId
#include "hot/n_dh_82773D38.h"            // UE3 SetVertex/PixelShaderValues wrappers
#include "hot/n_dh_827739D0.h"            // UE3 sampler setter with SetTexture and the sampler setters

// The constant setters' bad-float diagnostic (ME2's me_hot_guest.cpp) is not built: g_me2_bad_consts is 0
// (dh_stubs.cpp), so the setters never call this.
extern "C" void Me2BadConstsCheck(const PPCContext&, const uint8_t*, bool, uint32_t, uint32_t) {}

// XDK D3D float constant setters (ME2 sub_82A92C50 / sub_82A92D28): the hottest XDK functions of ME2's UE3 render
// thread (3.0 % + 1.0 % of its samples).
ME_HOT_CVAR(dh_hot_vs_consts, "Native sub_82AFBEF8 (XDK SetVertexShaderConstantF fast path, ME2 n_82A92C50), self-checked");
ME_HOT_HOOK(82AFBEF8, dh_hot_vs_consts, me::hot::n_82A92C50)
ME_HOT_CVAR(dh_hot_ps_consts, "Native sub_82AFBFD0 (XDK SetPixelShaderConstantF fast path, ME2 n_82A92D28), self-checked");
ME_HOT_HOOK(82AFBFD0, dh_hot_ps_consts, me::hot::n_82A92D28)

// C runtime: memcpy (1,692 direct call sites), wcslen, strlen.
ME_HOT_CVAR(dh_hot_crt_memcpy, "Native sub_82D860C0 (CRT memcpy, ME2 n_82D65BD0), self-checked");
ME_HOT_HOOK(82D860C0, dh_hot_crt_memcpy, me::hot::n_82D65BD0)
ME_HOT_CVAR(dh_hot_crt_wcslen, "Native sub_82D822C0 (CRT wcslen, ME2 n_82D5F6D0), self-checked");
ME_HOT_HOOK(82D822C0, dh_hot_crt_wcslen, me::hot::n_82D5F6D0)
ME_HOT_CVAR(dh_hot_strlen, "Native sub_82D9A390 (strlen, ME2 n_82C47090), self-checked");
ME_HOT_HOOK(82D9A390, dh_hot_strlen, me::hot::n_82C47090)

// XMemDecompress of UE3's appDecompressMemory (LZX, every package): ME2's LZX decoding (libmspack lzxd) in place of the
// recompiled XMem decoder; see hot/n_dh_82D9A3B8.h for why this function rather than appDecompressMemory itself.
ME_HOT_CVAR(dh_hot_lzx, "Native sub_82D9A3B8 (XMemDecompress, LZX blocks of appDecompressMemory), self-checked");
ME_HOT_HOOK(82D9A3B8, dh_hot_lzx, me::hot::n_dh_82D9A3B8)

// UE3's appDecompressMemory, the main path (sub_822E44F8: flags r3, dst r4, dst size r5, src r6, src size r7; 5 call
// sites, only r3 read after them): the same code as ME2's sub_8279B210 (LZX: a fresh XMem context, XMemDecompress with
// src size - 4, success when the size is exactly dst size; LZO through sub_8275F4C0), so ME2's native as it is. The
// timed variant above (sub_822E4308) did not run in a 90 s title-screen run.
ME_HOT_CVAR(dh_hot_lzx_main, "Native LZX of appDecompressMemory sub_822E44F8 (ME2 n_8279B210, libmspack lzxd), self-checked");
// 2026-11-11: spelled out (ME_HOT_HOOK_IMPL's body, umbrella on) to look at each decompressed block afterwards:
// DhPatchDecompressed (dh_main_menu.cpp) removes the main menu's DLC store button from its movie as it is read.
extern "C" void DhPatchDecompressed(uint8_t* base, uint32_t dst, uint32_t size);
namespace me::hot::n_dh_822E44F8 {
bool ShadowNative(PPCContext&, uint8_t*);
}
REX_EXTERN(__imp__sub_822E44F8);
static void DhLzxMain(PPCContext& ctx, uint8_t* base) {
  static thread_local uint32_t n_calls;
  static me::hot::Hook h{"sub_822E44F8", &__imp__sub_822E44F8, &me::hot::NativeB<&me::hot::n_dh_822E44F8::Native>,
                         &me::hot::n_dh_822E44F8::ShadowNative, &me::hot::n_dh_822E44F8::Writes,
                         me::hot::n_dh_822E44F8::kCmp};
  const uint8_t st = h.state.load(std::memory_order_relaxed);
  if (st == me::hot::kOn) [[likely]] {
    me::hot::Dispatch<&me::hot::n_dh_822E44F8::Native>(h, n_calls, ctx, base);
    return;
  }
  if (st == me::hot::kUninit) [[unlikely]] {
    me::hot::InitHook(h, REXCVAR_GET(dh_hot_lzx_main), true);
    if (h.state.load(std::memory_order_relaxed) == me::hot::kOn) {
      me::hot::Dispatch<&me::hot::n_dh_822E44F8::Native>(h, n_calls, ctx, base);
      return;
    }
  }
  __imp__sub_822E44F8(ctx, base);
}
REX_HOOK_RAW(sub_822E44F8) {  // appDecompressMemory(flags r3, dst r4, dst size r5, src r6, src size r7)
  const auto me2_replay_scope = me2::replay::EnterAt<0x822E44F8u>(ctx, base); /* me2_replay.h */
  const uint32_t dst = ctx.r4.u32, size = ctx.r5.u32;
  DhLzxMain(ctx, base);
  if (ctx.r3.u32 && dst) DhPatchDecompressed(base, dst, size);
}

// Wwise (2026-11-10, console stacks: the AK::EventManager thread at ~27 % of a core on cores 1-2, beside Renut's render
// thread): its reverb core (22.9 % of that thread's samples) and delay line (2.8 %), the same code as ME2's but for the
// differences listed in the headers, so ME2's natives adapted (hot/n_dh_82BB5D28.h, hot/n_dh_82BB4C58.h).
ME_HOT_CVAR(dh_hot_wwise_reverb, "Native sub_82BB5D28 (Wwise reverb core, ME2 n_82E95FC8 adapted), self-checked");
ME_HOT_HOOK(82BB5D28, dh_hot_wwise_reverb, me::hot::n_dh_82BB5D28)
ME_HOT_CVAR(dh_hot_wwise_delay, "Native sub_82BB4C58 / sub_82BB4D20 (Wwise delay line, ME2 n_82E94F28 adapted), self-checked");
ME_HOT_HOOK(82BB4C58, dh_hot_wwise_delay, me::hot::n_dh_82BB4C58)
ME_HOT_HOOK(82BB4D20, dh_hot_wwise_delay, me::hot::n_dh_82BB4D20)

// UE3 render thread (2026-11-10, console stacks, intervals <= 20 fps; the thread at ~93 % of core 0): a map lookup by
// object pointer (1.7 % with its hash) and the shader parameter setters (1.1 %, 554 call sites), which end in the XDK
// constant setters above.
ME_HOT_CVAR(dh_hot_set_find, "Native sub_8251FE10 (UE3 TSet<pointer>::FindId with its hash sub_82292600), self-checked");
ME_HOT_HOOK(8251FE10, dh_hot_set_find, me::hot::n_dh_8251FE10)
ME_HOT_CVAR(dh_hot_shader_params, "Native sub_82773D38 / sub_82773E40 (UE3 SetVertex/PixelShaderValues), self-checked");
ME_HOT_HOOK(82773D38, dh_hot_shader_params, me::hot::n_dh_82773D38)
ME_HOT_HOOK(82773E40, dh_hot_shader_params, me::hot::n_dh_82773E40)
// The material parameters' sampler setter (1.7 % + 1.0 % in SetTexture + the XDK sampler setters, 86 call sites).
// 2026-11-10 (console profile: 4.7 % of UE3's render thread against ~3 % for the recompiled code): off by default, and
// only its own switch turns it on (ME_HOT_HOOK_OWN: me2_hot_guest's umbrella overrode dh_hot_sampler_set = false).
REXCVAR_DEFINE_BOOL(dh_hot_sampler_set, false, "ME2",
                    "Native sub_827739D0 (UE3 sampler setter + XDK SetTexture / sampler state), self-checked; slower "
                    "than the recompiled code on the console")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
ME_HOT_HOOK_OWN(827739D0, dh_hot_sampler_set, me::hot::n_dh_827739D0)
