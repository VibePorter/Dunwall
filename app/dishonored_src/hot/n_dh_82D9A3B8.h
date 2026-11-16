// Dishonored (2026-11-08): native XMemDecompress (sub_82D9A3B8) for the LZX blocks of UE3's appDecompressMemory
// (sub_822E4308): every Dishonored package is XMem LZX, and in ME2 the recompiled LZX decoder under this call was the
// hottest function of the loading-heavy profiles (7.4 %). The decoding is ME2's (n_8279B210.h: the XMemCompress frames
// stripped into one LZX stream, libmspack lzxd with window 2^17).
//
// Hooked one level below appDecompressMemory, not at it as ME2 does: Dishonored's appDecompressMemory also times every
// call and adds the time and byte counts to global statistics (a double and two 64-bit counters), which a native would
// write differently each call (the guard would see a difference). XMemDecompress has one caller, sub_822E4308:
//
//   XMemCreateDecompressionContext(XMEMCODEC_LZX = 1, default parameters, 0, &ctx)   (sub_82D9A430, a fresh context)
//   size = *dst_size_ptr - 4; hr = XMemDecompress(ctx, dst, &size, src, src_size)    (this function)
//   *dst_size_ptr = size + 4; XMemDestroyDecompressionContext(ctx); return hr >= 0
//
// so the context never carries state from an earlier call, and what the native leaves different in it (the original
// decodes through the context's window) is destroyed right after without being read.
//
//   r3 = context, r4 = dst, r5 = &size (in: capacity, out: bytes written; big-endian u32), r6 = src, r7 = src size.
//   The original: codec word (ctx + 4) != 1 -> 0x80004005, else the LZX decoder (sub_82DA9958).
//   Native result: r3 = 0 (S_OK), *r5 = the frames' uncompressed total, dst = the decoded bytes.
//
// The native declines (the original runs) for anything else: another codec, no frames, a frame total of 0 or above the
// capacity or 1 MB, an lzxd error, a range that is not contiguous host memory. It decodes into a per-thread buffer and
// copies to the guest only on success, so a decline has written nothing. The caller reads only r3 after the call
// (tools/hot/liveness_me2.py).
#pragma once

#include "../../me2_src/hot/n/n_8279B210.h"

namespace me::hot::n_dh_82D9A3B8 {

// v14-v31: the decoder's XDK copy routines (sub_82DB1DE0, sub_82DB2104) leave their copy data there (Cmp::vr_skip).
inline constexpr Cmp kCmp = {R(3), R(1), R(1), 0xFFFFC000u};

inline bool Native(PPCContext& ctx, uint8_t* base) {
  namespace d = n_8279B210::detail;
  const uint32_t c = ctx.r3.u32, dst = ctx.r4.u32, size_ptr = ctx.r5.u32, src = ctx.r6.u32, len = ctx.r7.u32;
  if (!c || !size_ptr || !src || !dst || len < 2) return false;
  if (Ld32(base, c + 4) != 1) return false;  // not LZX: the original returns its error
  const uint32_t cap = Ld32(base, size_ptr);
  if (!cap || cap > n_8279B210::kMaxBlock) return false;
  if (uint64_t(src) + len > 0x100000000ull || uint64_t(dst) + cap > 0x100000000ull) return false;
  if (PhysOff(src) != PhysOff(src + len - 1) || PhysOff(dst) != PhysOff(dst + cap - 1)) return false;
  d::Buffers& b = d::ThreadBuffers();
  size_t stream_len = 0;
  uint64_t total = 0;
  if (!d::StripFrames(Raw(base, src), len, b, stream_len, total) || total == 0 || total > cap) return false;
  const uint32_t out_len = uint32_t(total);
  if (!d::Reserve(b.out, b.out_cap, out_len)) return false;
  if (!d::Decode(b.stream, stream_len, b.out, out_len)) return false;
  std::memcpy(StorePtr(base, dst, out_len), b.out, out_len);
  MarkCards(base, dst, out_len);
  St32(base, size_ptr, out_len);
  ctx.r3.u64 = 0;
  return true;
}

inline void Writes(const PPCContext& ctx, const uint8_t* base, me::hot::Writes& w) {
  const uint32_t size_ptr = ctx.r5.u32;
  if (!size_ptr) return;
  const uint32_t cap = Ld32(base, size_ptr);
  if (!cap || cap > n_8279B210::kMaxBlock) return;
  w.Add(ctx.r4.u32, cap);
  w.Add(size_ptr, 4);
}

}  // namespace me::hot::n_dh_82D9A3B8

// UE3's appDecompressMemory, main path (sub_822E44F8): ME2's native (n_8279B210.h) with the same v14-v31 exception:
// a PC run's 700th guarded block (src 1,006 bytes -> 32 KB) left v23 = 0 in the original (copy data), as input in the
// native, everything else equal.
namespace me::hot::n_dh_822E44F8 {
inline constexpr Cmp kCmp = {R(3), 0, 0, 0xFFFFC000u};
using n_8279B210::Native;
using n_8279B210::Writes;
}  // namespace me::hot::n_dh_822E44F8
