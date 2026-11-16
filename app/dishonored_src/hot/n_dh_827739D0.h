// Dishonored (2026-11-10): UE3's sampler setter of the material parameters (texture + sampler state of one sampler
// slot), sub_827739D0, with the six XDK functions it calls inlined: D3DDevice_SetTexture sub_82AFFFB0 and the sampler
// state setters sub_82AFA668 / sub_82AFA4C0 (filters), sub_82AFAA28 (LOD bias), sub_82AFAAD0 / sub_82AFAB50 (mip
// clamps). Console stacks of 2026-11-10 (UE3 render thread, intervals <= 20 fps): 1.7 % self + 1.0 % in SetTexture +
// the setters. The role of ME2's n_827DB288.h, written from Dishonored's listings (tools/d3d/gen_disasm.py).
//
//   sub_827739D0(slot s r3, sampler state desc r4, texture parameter r5); dev = [0x834CFE60] (GDevice), reread before
//   each step as the original; cache entry e = 0x83508FC8 + 24 s {+0 desc word, +4 texture, +8 desc bits 20-23,
//   +12 / +16 / +20 the mip clamp / max mip / LOD bias last set}; F = dev + 24 (s + 48) (the slot's fetch constant);
//   bit = 0x8000000000000000 >> (s + 32) (0 when bit 6 of the shift is set), ORed into the dirty mask [dev + 24].
//     1. tex = [r5 + 8]; tex != [e + 4]: SetTexture(dev, s, tex, bit); [e + 4] = tex
//     2. w = [desc]; w != [e + 0]: filter A (w >> 28), filter B ([desc] byte & 15), three fields of F + 0 from desc
//        (each with the bit ORed in); [e + 0] = [desc]
//     3. n = (w >> 20) & 15 (w read at entry); n != [e + 8]: F + 12 bits 23-24 = n; [e + 8] = n
//     4. [e + 20] != 0: LOD bias 0; [e + 20] = 0.   5. [e + 12] != 0: clamp A (0); [e + 12] = 0.
//     6. [e + 16] != 13: clamp B (13); [e + 16] = 13.
//   SetTexture: old = [dev + 12920 + 4 s]; tex != 0: F words from the texture header (+28..+48) merged with F's own,
//   max / min with the clamp bytes [dev + s + 12356] / [dev + s + 12382], bit ORed; tex == 0: F + 0 &= ~3 (no bit).
//   Then [dev + 12920 + 4 s] = tex; old != 0: [dev + 11036] != 0: [old + 8] = it; else ([dev + 11040] & [old]) != 0:
//   a pending-resource record in the command buffer (sub_82B0BA98 when it is full, and a stack word the original
//   reads uninitialised): DECLINED, decided before any write.
// Bit fields: each rlwimi / rlwinm of the listings as a mask on 32 bits (the generated code's upper halves never reach a
// 32-bit store); the shifts of the dirty bit as the generated code's (low byte of the 32-bit sum).
// Floats: the LOD bias setter multiplies the bias (0 here) by 32.0f and converts: always 0; its lfs clears the flush
// mode, done here when that step runs.
// Registers: tools/hot/liveness_me2.py: 86 direct call sites, none reads a volatile register after the call. The
// original's stack (its own frame and the XDK functions' frames, below r1) is not reproduced.
// DECLINED (the original runs): s >= 32, the pending-resource path above, an old texture header overlapping a range
// this call writes.
#pragma once

#include <cstdint>

#include "../../me2_src/hot/me_hot_common.h"

namespace me::hot::n_dh_827739D0 {

inline constexpr Cmp kCmp = {0, 0, 0};

inline constexpr uint32_t kDevicePtr = 0x834CFE60u;
inline constexpr uint32_t kCache = 0x83508FC8u;
inline constexpr uint32_t kFilterTable = 0x82220CC8u;

inline uint32_t Rotl(uint32_t x, unsigned n) { return n ? (x << n) | (x >> (32 - n)) : x; }
inline uint32_t Ins(uint32_t into, uint32_t from, uint32_t mask) { return (into & ~mask) | (from & mask); }
inline uint64_t SlotBit(uint32_t s) {
  const uint8_t sh = uint8_t(s + 32);
  return (sh & 0x40) ? 0 : (0x8000000000000000ull >> (sh & 0x7F));
}
inline uint32_t Fetch(uint32_t dev, uint32_t s) { return dev + 24u * (s + 48u); }
inline void OrDirty(uint8_t* base, uint32_t dev, uint64_t bit) { St64(base, dev + 24, Ld64(base, dev + 24) | bit); }

// sub_82AFA668 (kB = false) / sub_82AFA4C0 (kB = true)
template <bool kB>
inline void Filter(uint8_t* base, uint32_t dev, uint32_t s, uint32_t v) {
  const uint32_t F = Fetch(dev, s);
  const uint32_t b1 = Ld8(base, dev + s + 10864);
  const uint32_t r7 = v >> 2;
  const uint32_t d4 = Ld32(base, F + 16), d3 = Ld32(base, F + 12);
  const uint32_t r6 = kB ? (d4 >> 10) & 1 : (d4 >> 11) & 1;
  const uint32_t t8 = Ld32(base, kFilterTable + (b1 << 2));
  const uint32_t r29 = kB ? Ins(d4, r7 << 11, 0x800) : Ins(d4, r7 << 10, 0x400);
  St32(base, F + 16, r29);
  uint32_t r9 = t8 & ~((r6 | r7) - 1u);
  r9 = kB ? r9 << 4 : r9 << 6;
  r9 = r9 | r7 | v;
  uint32_t r31 = d3;
  if (kB) {
    r31 = Ins(r31, Rotl(r9, 21), 0x00600000u);
    r31 = Ins(r31, Rotl(r9, 21), 0x0E000000u);
  } else {
    r31 = Ins(r31, Rotl(r9, 19), 0x00180000u);
    r31 = Ins(r31, Rotl(r9, 19), 0x0E000000u);
  }
  St32(base, F + 12, r31);
  const uint32_t b2 = Ld8(base, dev + s + 10890);
  uint32_t r6b = Ins(r31, Rotl(r31, 31), 0x0007FFFFu);
  r6b = Ins(r6b, Rotl(r31, 31), 0x7FF00000u);
  const uint32_t lim = (b2 >> 2) - 1u;
  const uint32_t r10 = ((Rotl(r6b, 13) & 0xFFFu) & lim) + (b2 & ~lim);
  St32(base, F + 16, (r10 & 3u) | (r29 & ~3u));
  OrDirty(base, dev, SlotBit(s));
}

// sub_82AFAA28 with bias 0: the field is 0 whatever the scale constant.
inline void LodBiasZero(PPCContext& ctx, uint8_t* base, uint32_t dev, uint32_t s) {
  ctx.fpscr.disableFlushMode();
  const uint32_t F = Fetch(dev, s);
  St32(base, F + 16, Ld32(base, F + 16) & ~0x003FF000u);
  OrDirty(base, dev, SlotBit(s));
}

// sub_82AFAAD0 (kMax: field = max(header nibble, v), bits 2-5, byte 12356) / sub_82AFAB50 (min, bits 6-9, byte 12382)
template <bool kMax>
inline void Clamp(uint8_t* base, uint32_t dev, uint32_t s, uint32_t v) {
  const uint32_t t = Ld32(base, dev + 4u * (s + 3230u));
  if (t) {
    const uint32_t hdr = Ld32(base, t + 44);
    uint32_t f = kMax ? (hdr >> 2) & 0xF : (hdr >> 6) & 0xF;
    if (kMax ? !(f > v) : !(f < v)) f = v;
    const uint32_t F = Fetch(dev, s);
    St32(base, F + 16, kMax ? Ins(Ld32(base, F + 16), f << 2, 0x3Cu) : Ins(Ld32(base, F + 16), f << 6, 0x3C0u));
    OrDirty(base, dev, SlotBit(s));
  }
  St8(base, dev + s + (kMax ? 12356u : 12382u), uint8_t(v));
}

// sub_82AFFFB0 without the pending-resource path (checked by Declines first).
inline void SetTexture(uint8_t* base, uint32_t dev, uint32_t s, uint32_t tex, uint64_t bit) {
  const uint32_t slot = dev + 4u * (s + 3230u);
  const uint32_t old = Ld32(base, slot);
  const uint32_t F = Fetch(dev, s);
  if (tex) {
    const uint32_t h48 = Ld32(base, tex + 48), h32 = Ld32(base, tex + 32), h44 = Ld32(base, tex + 44);
    const uint32_t h28 = Ld32(base, tex + 28), h36 = Ld32(base, tex + 36), h40 = Ld32(base, tex + 40);
    const uint32_t f0 = Ld32(base, F + 0), f12 = Ld32(base, F + 12), f20 = Ld32(base, F + 20);
    const uint32_t f16 = Ld32(base, F + 16), f4 = Ld32(base, F + 4);
    const uint32_t r8 = (((h48 >> 20) & 0xFFFu) + 512u) & 0x1000u;
    const uint32_t r3a = (((h32 >> 20) & 0xFFFu) + 512u) & 0x1000u;
    St32(base, F + 8, h36);
    const uint32_t r7 = r8 + (h48 & 0x1FFFFE00u);
    const uint32_t r3 = r3a + (h32 & 0x1FFFFFFFu);
    uint32_t r10 = (f16 & 0xFFFFFC03u) | (h44 & 0x3FCu);
    const uint32_t b12356 = Ld8(base, dev + s + 12356);
    St32(base, F + 16, r10);
    St32(base, F + 0, Ins(h28, f0, 0x003FFC00u));
    St32(base, F + 4, Ins(r3, f4, 0x800u));
    St32(base, F + 12, Ins(h40, f12, 0x7FF80000u));
    St32(base, F + 20, Ins(r7, f20, 0x1FFu));
    const uint32_t hi = (h44 >> 2) & 0xF;
    const uint32_t mx = hi > b12356 ? hi : b12356;
    const uint32_t h44b = Ld32(base, tex + 44);
    r10 = Ins(r10, mx << 2, 0x3Cu);
    const uint32_t b12382 = Ld8(base, dev + s + 12382);
    const uint32_t lo = (h44b >> 6) & 0xF;
    St32(base, F + 16, r10);
    const uint32_t mn = lo >= b12382 ? b12382 : lo;
    r10 = Ins(r10, mn << 6, 0x3C0u);
    St32(base, F + 16, r10);
    OrDirty(base, dev, bit);
  } else {
    St32(base, F + 0, Ld32(base, F + 0) & ~3u);
  }
  St32(base, slot, tex);
  if (old) {
    const uint32_t q = Ld32(base, dev + 11036);
    if (q) St32(base, old + 8, q);
  }
}

inline bool Overlaps(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen) {
  return uint64_t(a) < uint64_t(b) + blen && uint64_t(b) < uint64_t(a) + alen;
}

// Reads only: whether the call reaches SetTexture's pending-resource path (or another case left to the original).
inline bool Declines(const PPCContext& ctx, const uint8_t* base) {
  const uint32_t s = ctx.r3.u32;
  if (s >= 32) return true;
  const uint32_t e = kCache + 24u * s;
  const uint32_t tex = Ld32(base, ctx.r5.u32 + 8);
  if (tex == Ld32(base, e + 4)) return false;
  const uint32_t dev = Ld32(base, kDevicePtr);
  const uint32_t old = Ld32(base, dev + 4u * (s + 3230u));
  if (!old) return false;
  // the old header's words read (+0) and written (+8) must stay out of everything this call writes
  const uint32_t F = Fetch(dev, s);
  if (Overlaps(old, 12, e, 24) || Overlaps(old, 12, F, 24) || Overlaps(old, 12, dev + 24, 8) ||
      Overlaps(old, 12, dev + 4u * (s + 3230u), 4) || Overlaps(old, 12, dev + s + 12356, 1) ||
      Overlaps(old, 12, dev + s + 12382, 1) || Overlaps(old, 12, dev + 11036, 8))
    return true;
  if (Ld32(base, dev + 11036)) return false;
  return (Ld32(base, dev + 11040) & Ld32(base, old)) != 0;
}

inline bool Native(PPCContext& ctx, uint8_t* base) {
  if (Declines(ctx, base)) return false;
  const uint32_t s = ctx.r3.u32, desc = ctx.r4.u32, param = ctx.r5.u32;
  const uint32_t e = kCache + 24u * s;
  const uint64_t bit = SlotBit(s);
  const uint32_t w0 = Ld32(base, desc);
  const uint32_t tex = Ld32(base, param + 8);
  const uint32_t n = (w0 >> 20) & 0xF;
  // 1. the texture
  if (tex != Ld32(base, e + 4)) {
    SetTexture(base, Ld32(base, kDevicePtr), s, tex, bit);
    St32(base, e + 4, tex);
  }
  // 2. the filters and the desc fields of F + 0
  const uint32_t w = Ld32(base, desc);
  if (w != Ld32(base, e + 0)) {
    Filter<false>(base, Ld32(base, kDevicePtr), s, (w >> 28) & 0xF);
    Filter<true>(base, Ld32(base, kDevicePtr), s, Ld8(base, desc) & 0xFu);
    uint32_t dev = Ld32(base, kDevicePtr);
    const uint32_t off = 24u * (s + 48u);
    uint32_t d = Ld32(base, desc);
    St32(base, dev + off, Ins(Ld32(base, dev + off), Rotl(d, 26), 0x1C00u));
    OrDirty(base, dev, bit);
    dev = Ld32(base, kDevicePtr);
    {
      const uint32_t x = Ld32(base, dev + off);
      d = Ld32(base, desc);
      St32(base, dev + off, Ins(x, Rotl(d, 1), 0xE000u));
    }
    OrDirty(base, dev, bit);
    dev = Ld32(base, kDevicePtr);
    {
      const uint32_t x = Ld32(base, dev + off);
      d = Ld32(base, desc);
      St32(base, dev + off, Ins(x, Rotl(d, 8), 0x70000u));
    }
    OrDirty(base, dev, bit);
    St32(base, e + 0, Ld32(base, desc));
  }
  // 3. desc bits 20-23 into F + 12
  if (n != Ld32(base, e + 8)) {
    const uint32_t dev = Ld32(base, kDevicePtr);
    const uint32_t a = dev + 24u * s + 1164u;
    St32(base, a, Ins(Ld32(base, a), Rotl(n, 23), 0x01800000u));
    OrDirty(base, dev, bit);
    St32(base, e + 8, n);
  }
  // 4-6. LOD bias 0, mip clamps 0 and 13
  if (Ld32(base, e + 20) != 0) {
    LodBiasZero(ctx, base, Ld32(base, kDevicePtr), s);
    St32(base, e + 20, 0);
  }
  if (Ld32(base, e + 12) != 0) {
    Clamp<true>(base, Ld32(base, kDevicePtr), s, 0);
    St32(base, e + 12, 0);
  }
  if (Ld32(base, e + 16) != 13) {
    Clamp<false>(base, Ld32(base, kDevicePtr), s, 13);
    St32(base, e + 16, 13);
  }
  return true;
}

inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  const uint32_t s = ctx.r3.u32;
  if (s >= 32) return;
  const uint32_t dev = Ld32(base, kDevicePtr);
  w.Add(kCache + 24u * s, 24);
  w.Add(Fetch(dev, s), 24);
  w.Add(dev + 24, 8);
  w.Add(dev + 4u * (s + 3230u), 4);
  w.Add(dev + s + 12356, 1);
  w.Add(dev + s + 12382, 1);
  const uint32_t old = Ld32(base, dev + 4u * (s + 3230u));
  if (old) w.Add(old + 8, 4);
}

}  // namespace me::hot::n_dh_827739D0
