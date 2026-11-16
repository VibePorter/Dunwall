// Dishonored (2026-11-10): UE3 TSet<pointer key>::FindId (a TMap lookup by object pointer), sub_8251FE10, with its key
// hash sub_82292600 (GetTypeHash of a pointer: Bob Jenkins' 96-bit mix with 0x9E3779B9) inlined. Console stacks of
// 2026-11-10 (UE3 render thread, intervals <= 20 fps): 1.5 % + 0.2 % of its samples, from 2 direct call sites (a loop).
// The role of ME2's TMap<FName>::Find native (me2_src/hot/n/n_82993E40.h), written from Dishonored's listing.
//
//   sub_8251FE10(out r3, set r4, key r5): r3 = out (returned)
//     size = [set + 52]; size == 0: [out] = -1
//     id = [[set + 48] + 4 * (hash(key) & (size - 1))]
//     while id != -1: e = [set + 0] + 32 * id; [e + 0] == key: [out] = id, return; id = [e + 24]
//     [out] = -1
// Registers: r3 returned unchanged (the out pointer); the original leaves r4-r12, cr0/cr6 as scratch. 2 direct call
// sites; tools/hot/liveness_me2.py: one reads r3 (the return) after the call, none any other volatile register.
// Declined (the original runs): a chain longer than 1 << 20 links (a corrupt set: the original would spin).
#pragma once

#include <cstdint>

#include "../../me2_src/hot/me_hot_common.h"

namespace me::hot::n_dh_8251FE10 {

inline constexpr Cmp kCmp = {R(3), 0, 0};

// sub_82292600, instruction for instruction in 32 bits (the generated code's 64-bit upper halves never reach the
// result: the caller masks the hash with size - 1, a 32-bit value).
inline uint32_t PointerHash(uint32_t a) {
  uint32_t r3 = a, r4, r5, r6, r7, r8, r9, r10, r11;
  r10 = r3 << 8;
  r9 = 0x9E3779B9u;
  r8 = r9 - r3;
  r11 = r8 ^ r10;
  r7 = r11 + r3;
  r6 = r11 >> 13;
  r5 = 0u - r7;
  r4 = r3 - r11;
  r3 = r5 ^ r6;
  r10 = r4 - r3;
  r9 = r3 >> 12;
  r8 = r10 ^ r9;
  r7 = r11 - r8;
  r6 = r8 << 16;
  r5 = r7 - r3;
  r4 = r5 ^ r6;
  r3 = r3 - r4;
  r11 = r4 >> 5;
  r10 = r3 - r8;
  r9 = r8 - r4;
  r8 = r10 ^ r11;
  r7 = r9 - r8;
  r6 = r8 >> 3;
  r5 = r7 ^ r6;
  r4 = r4 - r5;
  r3 = r5 << 10;
  r11 = r4 - r8;
  r10 = r11 ^ r3;
  r9 = r8 - r10;
  r8 = r10 >> 15;
  r7 = r9 - r5;
  return r7 ^ r8;
}

inline bool Native(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32, set = ctx.r4.u32, key = ctx.r5.u32;
  const uint32_t size = Ld32(base, set + 52);
  uint32_t found = 0xFFFFFFFFu;
  if (size) {
    const uint32_t hashes = Ld32(base, set + 48);
    uint32_t id = Ld32(base, hashes + 4u * (PointerHash(key) & (size - 1)));
    const uint32_t elems = Ld32(base, set + 0);
    for (uint32_t links = 0; id != 0xFFFFFFFFu; ++links) {
      if (links > (1u << 20)) return false;
      const uint32_t e = elems + 32u * id;
      if (Ld32(base, e) == key) {
        found = id;
        break;
      }
      id = Ld32(base, e + 24);
    }
  }
  St32(base, out, found);
  return true;
}

inline void Writes(const PPCContext& ctx, const uint8_t*, me::hot::Writes& w) { w.Add(ctx.r3.u32, 4); }

}  // namespace me::hot::n_dh_8251FE10
