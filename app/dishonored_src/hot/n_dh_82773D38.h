// Dishonored (2026-11-10): UE3's shader parameter setters (FRHI SetVertexShaderValues / SetPixelShaderValues),
// sub_82773D38 (vertex) and sub_82773E40 (pixel), from the console stacks of 2026-11-10 (UE3 render thread, intervals
// <= 20 fps): 0.6 % + 0.5 % of its samples, with 151 + 403 direct call sites. The same thin wrapper as ME2's
// sub_822EB710 (me2_src/hot/n/n_822EB710.h, whose plan is reused) with other arguments: no clamp of the size and a byte
// offset in place of the parameter record:
//
//   sub_82773D38 / sub_82773E40(offset r3 (bytes), size r4 (bytes), value r5)
//     rounded = (size + 15) & ~15; rounded != size: the scratch TArray at 0x835145B8 {data, num, max} (initialised once:
//       bit 0 of 0x835145C4; grown by sub_82305BF8 / sub_82305B78 when num < rounded) holds the value:
//       memcpy(data, value, size) (sub_82D860C0); memset(data + size, 0, rounded - size) (sub_82D9A1F0); src = data
//       (else src = value)
//     start = offset >> 4; count = (size + 15) >> 4 (both 32 bits)
//     g0 = start >> 2; g1 = (start + count - 1) >> 2; mask = (0x8000000000000000 >>arith min(g1 - g0, 63)) >> g0
//       (0 if g0 & 0x40)
//     SetVertexShaderConstantF / SetPixelShaderConstantF([0x834CFE60], start, src, count, mask) (sub_82AFBEF8 /
//       sub_82AFBFD0, the n_82A92C50.h natives)
// DECLINED (decided before any write, the original runs): the scratch not initialised yet or too small (it grows), value
// and scratch overlapping, and the constant setter's own declines (shader_consts::Set).
// Registers: tools/hot/liveness_me2.py: no direct call site reads a volatile register after the call, but one
// `subfe r7,r10,r10` after sub_82773E40 in sub_82603D88 (only the carry). r3-r7 are left as the original leaves them
// (the constant setter's arguments).
#pragma once

#include <cstring>

#include "../../me2_src/hot/me_hot_common.h"
#include "../../me2_src/hot/n/n_82A92C50.h"

namespace me::hot::dh_shader_params {

inline constexpr uint32_t kScratch = 0x835145B8u;  // TArray<BYTE> {data, num, max}
inline constexpr uint32_t kScratchInit = 0x835145C4u;
inline constexpr uint32_t kDevicePtr = 0x834CFE60u;

struct Plan {
  uint32_t n = 0, pad = 0, src = 0, data = 0, start = 0, count = 0, dev = 0;
  uint64_t mask = 0;
};

// The call's plan from its inputs (reads only). false = declined.
inline bool MakePlan(const PPCContext& ctx, const uint8_t* base, Plan& p) {
  const uint32_t offset = ctx.r3.u32, size = ctx.r4.u32, value = ctx.r5.u32;
  const uint32_t rounded = (size + 15u) & ~15u;
  p.n = size;
  p.pad = rounded - size;
  p.src = value;
  if (rounded != size) {
    if (!(Ld32(base, kScratchInit) & 1)) return false;
    p.data = Ld32(base, kScratch);
    if (Ld32(base, kScratch + 4) < rounded) return false;
    const uint64_t d0 = p.data, d1 = d0 + rounded, v0 = value, v1 = v0 + size;
    if (d1 > 0x100000000ull || v1 > 0x100000000ull) return false;
    if (d0 < v1 && v0 < d1) return false;  // overlap
    if (PhysOff(p.data) != PhysOff(uint32_t(d1 - 1)) || (size && PhysOff(value) != PhysOff(uint32_t(v1 - 1))))
      return false;
    p.src = p.data;
  }
  p.start = offset >> 4;
  p.count = (size + 15u) >> 4;
  const uint32_t g0 = p.start >> 2, g1 = (p.start + p.count - 1) >> 2;
  uint32_t sh = (g1 - g0) & 0x7F;
  if (sh > 63) sh = 63;
  const uint64_t m = uint64_t(int64_t(0x8000000000000000ull) >> sh);
  p.mask = (g0 & 0x40) ? 0 : (m >> (g0 & 0x7F));
  p.dev = Ld32(base, kDevicePtr);
  return true;
}

// The constant setter's declines (shader_consts::Set), checked before the scratch is written.
inline bool SetterTakes(const Plan& p, uint32_t base_reg) {
  if (p.count > 512) return false;
  const uint32_t dst = (p.dev + ((p.start + base_reg) << 4)) & ~0xFu, len = p.count * 16;
  if (!len) return true;
  if (uint64_t(dst) + len > 0x100000000ull || uint64_t(p.src) + len > 0x100000000ull) return false;
  if (uint64_t(dst) < uint64_t(p.src) + len && uint64_t(p.src) < uint64_t(dst) + len) return false;
  if (PhysOff(dst) != PhysOff(dst + len - 1) || PhysOff(p.src) != PhysOff(p.src + len - 1)) return false;
  // the scratch copy is read by the setter: it must not overlap the constants it writes either (covered above, as
  // src = data then)
  return true;
}

inline bool Run(PPCContext& ctx, uint8_t* base, uint32_t base_reg, uint32_t mask_off) {
  Plan p;
  if (!MakePlan(ctx, base, p) || !SetterTakes(p, base_reg)) return false;
  if (p.pad) {
    if (p.n) std::memcpy(Raw(base, p.data), Raw(base, ctx.r5.u32), p.n);
    std::memset(Raw(base, p.data + p.n), 0, p.pad);
    MarkCards(base, p.data, p.n + p.pad);
  }
  ctx.r3.u64 = p.dev;
  ctx.r4.u64 = p.start;
  ctx.r5.u64 = p.src;
  ctx.r6.u64 = p.count;
  ctx.r7.u64 = p.mask;
  return shader_consts::Set(ctx, base, base_reg, mask_off);  // cannot decline: its conditions were checked above
}

inline void WritesOf(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w, uint32_t base_reg,
                     uint32_t mask_off) {
  Plan p;
  if (!MakePlan(ctx, base, p)) return;
  if (p.pad) w.Add(p.data, p.n + p.pad);
  if (p.count <= 512) w.Add((p.dev + ((p.start + base_reg) << 4)) & ~0xFu, p.count * 16);
  w.Add(p.dev + mask_off, 8);
}

}  // namespace me::hot::dh_shader_params

namespace me::hot::n_dh_82773D38 {
inline constexpr Cmp kCmp = {0, 0, 0};
inline bool Native(PPCContext& ctx, uint8_t* base) { return dh_shader_params::Run(ctx, base, 120, 0); }
inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  dh_shader_params::WritesOf(ctx, base, w, 120, 0);
}
}  // namespace me::hot::n_dh_82773D38

namespace me::hot::n_dh_82773E40 {
inline constexpr Cmp kCmp = {0, 0, 0};
inline bool Native(PPCContext& ctx, uint8_t* base) { return dh_shader_params::Run(ctx, base, 376, 8); }
inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  dh_shader_params::WritesOf(ctx, base, w, 376, 8);
}
}  // namespace me::hot::n_dh_82773E40
