// Dishonored (2026-11-10): Wwise delay line (ring buffer of floats), two operations: sub_82BB4C58 (swap) and
// sub_82BB4D20 (in -> ring -> out), the same functions as ME2's sub_82E94F28 / sub_82E94FE8 (me2_src/hot/n/
// n_82E94F28.h) plus one guard: Dishonored's Wwise tests n == 0 before the unwrapped loop (ME2's would run 2^32 times,
// so ME2's native declines that case). Console stacks of 2026-11-10 (intervals <= 20 fps): 2.5 % + 0.3 % of the Wwise
// thread's running samples. Listings diffed by hand against ME2's (registers and branch offsets equal but for the guard).
//
//   r3 = ring {[+0] length L (floats), [+4] buffer, [+8] position p}.
//   sub_82BB4C58: r4 = io (floats), r5 = n: for i < n, idx = (p + i) mod L: swap ring[idx] and io[i]
//                 (load ring, load io, store ring <- io, store io <- ring); p = (p + n) mod L.
//   sub_82BB4D20: r4 = in, r5 = out, r6 = n: for i < n: (load ring, load in, store ring <- in, store out <- ring).
//   Both: one unwrapped loop when L - p > n (p += n, no wrap test), else chunks of min(left, L - p) with p reset to 0
//   when it reaches L. The net effect is the per-element sequence above, in increasing i, reproduced element by element
//   in the same load/store order, so any aliasing between the buffers gives the original's result.
//   n == 0: with L - p > 0 the guard skips the loop and stores p back (unchanged); with p == L the chunked path moves
//   nothing and stores nothing.
// Floats: lfs/stfs pairs (float -> double -> float), the identity for every value but a signalling NaN (the recompiled
// conversion may quiet it; a raw copy here: the guard would report it and switch the hook off). Every loop iteration
// clears the flush mode (lfs): done once when at least one element is moved.
// Registers: r3 unchanged; the original leaves f13 and r4-r11 as scratch. 6 / 7 direct call sites, none reads a volatile
// register after the call (tools/hot/liveness_me2.py on Dishonored's generated code).
// Declined (the original runs): L == 0 or p > L.
#pragma once

#include <cstdint>
#include <cstring>

#include "../../me2_src/hot/me_hot_common.h"

namespace me::hot::dh_wwise_delay {

inline uint32_t RawLd(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Raw(base, a), 4);
  return v;
}
inline void RawSt(uint8_t* base, uint32_t a, uint32_t v) { std::memcpy(StorePtr(base, a, 4), &v, 4); }

// kSwap: io = in = out (sub_82BB4C58)
template <bool kSwap>
inline bool Run(PPCContext& ctx, uint8_t* base) {
  const uint32_t ring = ctx.r3.u32;
  const uint32_t len = Ld32(base, ring), buf = Ld32(base, ring + 4);
  uint32_t p = Ld32(base, ring + 8);
  const uint32_t in = ctx.r4.u32;
  const uint32_t out = kSwap ? ctx.r4.u32 : ctx.r5.u32;
  const uint32_t n = kSwap ? ctx.r5.u32 : ctx.r6.u32;
  if (len == 0 || p > len) return false;
  if (n == 0) {
    if (len - p > 0) St32(base, ring + 8, p);  // the guard's path: p + 0 stored
    return true;
  }
  ctx.fpscr.disableFlushMode();
  for (uint32_t i = 0; i < n; ++i) {
    if (p == len) p = 0;
    const uint32_t ra = buf + 4u * p;
    const uint32_t old = RawLd(base, ra);
    const uint32_t v = RawLd(base, in + 4u * i);
    RawSt(base, ra, v);
    RawSt(base, out + 4u * i, old);
    ++p;
  }
  if (p == len) p = 0;
  St32(base, ring + 8, p);
  return true;
}

template <bool kSwap>
inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  const uint32_t ring = ctx.r3.u32;
  const uint32_t len = Ld32(base, ring), buf = Ld32(base, ring + 4), p = Ld32(base, ring + 8);
  const uint32_t n = kSwap ? ctx.r5.u32 : ctx.r6.u32;
  if (len == 0 || p > len) return;
  if (n) {
    w.Add(buf, 4u * len);  // the whole ring (n < L touches a part of it, possibly wrapped)
    w.Add(kSwap ? ctx.r4.u32 : ctx.r5.u32, 4u * n);
  }
  w.Add(ring + 8, 4);
}

}  // namespace me::hot::dh_wwise_delay

namespace me::hot::n_dh_82BB4C58 {
inline constexpr Cmp kCmp = {R(3), 0, 0};
inline bool Native(PPCContext& ctx, uint8_t* base) { return dh_wwise_delay::Run<true>(ctx, base); }
inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  dh_wwise_delay::Writes<true>(ctx, base, w);
}
}  // namespace me::hot::n_dh_82BB4C58

namespace me::hot::n_dh_82BB4D20 {
inline constexpr Cmp kCmp = {R(3), 0, 0};
inline bool Native(PPCContext& ctx, uint8_t* base) { return dh_wwise_delay::Run<false>(ctx, base); }
inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  dh_wwise_delay::Writes<false>(ctx, base, w);
}
}  // namespace me::hot::n_dh_82BB4D20
