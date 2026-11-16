// Dishonored (2026-11-10): a software occlusion rasterizer for UE3's primitive occlusion queries, after ME2's
// (Lazarus app/me2_src/me2_occlusion_raster.cpp: the rasterizer, the near-plane clip, the corner samples, the box test).
//
// WHY
//   UE3 issues its occlusion queries after the depth pre-pass and reads them a frame later; Renut submits the frame at
//   Swap and runs a frame behind, so the GPU counts are often not back when UE3 reads them: dh_occlusion.cpp answers
//   those from a history of earlier frames, which made characters vanish for a moment (console, 2026-11-09/10). Here the
//   frame's occluders (the static meshes of the depth pre-pass, as the GPU draws them) are drawn on the CPU with the
//   frame's own camera into a small depth buffer, and every query box of the frame is tested against it when UE3 reads
//   the query (a frame later; the worker is long done by then): every query has its answer, of its own frame.
//
// WHAT THE ENGINE HAS (from the disassembly and a PC dump, dh_raster_dump, 2026-11-10)
//   RenderPrePass sub_826320E0(renderer r3, DPG r4, FViewInfo r5): FScene = [renderer + 0]; the DPG's depth draw lists
//   at FScene + DPG * 2448 + {8, 80} (+152 / +224: the masked lists, drawn when [renderer + 4484]; not occluders: alpha
//   holes), drawn by TStaticMeshDrawList::DrawVisible (sub_82643828 / sub_82643988) with the view's static mesh
//   visibility bits (view + 1344, a TBitArray: its words at +16, or inline at +0 when that is 0).
//   A draw list (72 bytes): +0 / +4 the ordered policy indices (data, count), +12 the policy links (80 bytes each). A
//   link: +0 its compact visibility array (8 bytes an element: the visibility map's word index, bit mask), +12 the
//   elements (12 bytes: +4 the FStaticMesh), +16 the element count, +24 the drawing policy.
//   FStaticMesh (DrawMesh sub_825FCC18): +0 FIndexBuffer* (+20 the RHI object, its +8 the D3DIndexBuffer), +4 the
//   vertex factory, +48 LocalToWorld (row vectors; WorldToLocal at +112), +176 FirstIndex, +180 NumPrimitives,
//   +184 / +188 Min / MaxVertexIndex, +208 flags (bit 31 dynamic data, (>> 24) & 7 the primitive type: 0 list, 1 strip).
//   The vertex factory's position streams (DrawShared sub_825FBE60): count +408, 12 bytes each from +216
//   (FVertexBuffer*, stride, offset); FVertexBuffer +20 the RHI object, its +8 the D3DVertexBuffer (+24 address, +28 fetch
//   dword 1: endian bits 0-1, size bits 2-25). As ME2's.
//   FViewInfo: +560 the view-projection matrix (world space, row vectors: the camera at +752 projects to w = 0), +416 the
//   same without the translation, +544 the pre-view translation (the occlusion batchers' origins include it), +752 the
//   view origin, +80 the view matrix, +144 the projection (near 5).
//
// HOW
//   UE3's render thread (RenderPrePass, the world DPG) lists the meshes of the two opaque depth lists the view sees this
//   frame and hands them, with the view's matrix, to a worker on cores 2-3. The worker copies each mesh it has not seen
//   (or that changed) once into world space, then draws them largest first (up to a triangle budget) into a 256 x 144
//   buffer of 1/w at pixel corners; a pixel holds the farthest of its four corners. A query box (dh_occlusion.cpp, the
//   batch hook) is hidden only when every pixel under its screen rectangle holds an occluder nearer than the box's
//   nearest corner; one reaching the near plane is visible. Triangles are culled by UE3's winding, flipped for a mirrored
//   world matrix (negative determinant), as UE3 does.
//   dh_raster_mode: 0 off, 1 compare (the answers tallied against the GPU counts, "[dh-raster]" every 10 s), 2 answer
//   (the queries take the rasterizer's answer; the GPU counts and the history only where it has none).
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

REXCVAR_DEFINE_INT32(dh_raster_mode, 1, "Dishonored",
                     "Software occlusion rasterizer: 0 off, 1 compare (tallied against the GPU counts, logged), 2 the "
                     "occlusion queries take its answers")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_w, 256, "Dishonored", "Occlusion rasterizer: depth buffer width (pixels)")
    .range(64, 1024)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_h, 144, "Dishonored", "Occlusion rasterizer: depth buffer height (pixels)")
    .range(36, 576)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_tris, 60000, "Dishonored", "Occlusion rasterizer: triangles drawn a frame at most")
    .range(1000, 2000000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(dh_raster_min_px, 24.0, "Dishonored",
                      "Occlusion rasterizer: occluders whose screen rectangle covers fewer pixels are not drawn")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(dh_raster_min_px_per_tri, 1.0, "Dishonored",
                      "Occlusion rasterizer: occluders with more triangles than this many per covered pixel are not drawn")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_mesh_tris, 32768, "Dishonored",
                     "Occlusion rasterizer: meshes with more triangles are not occluders")
    .range(16, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_mem_mb, 64, "Dishonored", "Occlusion rasterizer: occluder geometry kept at most (MB)")
    .range(4, 1024)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_winding, 1, "Dishonored",
                     "Occlusion rasterizer: the screen winding culled for UE3's front faces: 1 clockwise (y down), 2 the "
                     "other, 0 no culling")
    .range(0, 2)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_DOUBLE(dh_raster_bias, 0.0, "Dishonored",
                      "Occlusion rasterizer: a box is hidden only behind occluders this much nearer (relative 1/w)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_mask, 0xC, "Dishonored",
                     "Switch: the occlusion rasterizer worker's core mask (trimmed to the process's cores)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_dump, 0, "Dishonored/Diagnostics",
                     "Occlusion rasterizer: log RenderPrePass's view (floats) every this many calls (5 times); 0 = off")
    .range(0, 100000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern std::atomic<uint64_t> g_sa2_game_frames;  // src/sa2_capture.cpp: counted at each Swap

#if REX_PLATFORM_SWITCH
extern "C" bool RexSwitchSetCurrentThreadMask(int preferred, unsigned long long mask);  // sdk core/threading_switch.cpp
extern "C" unsigned long long RexSwitchProcessCoreMask(void);                          // sdk core/threading_switch.cpp
#endif

namespace {

uint32_t L32(const uint8_t* base, uint32_t va) { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + va)); }
uint32_t L32le(const uint8_t* base, uint32_t va) { return *reinterpret_cast<const uint32_t*>(base + va); }
uint16_t L16(const uint8_t* base, uint32_t va) { return __builtin_bswap16(*reinterpret_cast<const uint16_t*>(base + va)); }
uint16_t L16le(const uint8_t* base, uint32_t va) { return *reinterpret_cast<const uint16_t*>(base + va); }
float LF(const uint8_t* base, uint32_t va) {
  const uint32_t u = L32(base, va);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
float LFle(const uint8_t* base, uint32_t va) {
  const uint32_t u = L32le(base, va);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
bool Plausible(uint32_t va) { return va >= 0x00100000u && va < 0xC0000000u; }  // the 4 KB heap is below 0x40000000

// Mapped host pages under guest [va, va + len) (as render/draw.cpp GuestRangeReadable): the worker reads guest memory
// the game may have released.
bool Readable(const uint8_t* base, uint32_t va, uint32_t len) {
  if (!Plausible(va) || uint64_t(va) + len > 0xC0000000ull) return false;
  if (!len) return true;
  static const size_t page = rex::memory::page_size();
  size_t off = 0;
  while (off < len) {
    uint8_t* at = const_cast<uint8_t*>(base + va + off);
    size_t span = len - off + page;
    rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
    if (!rex::memory::QueryProtect(at, span, access) || access == rex::memory::PageAccess::kNoAccess) return false;
    const size_t in_page = reinterpret_cast<uintptr_t>(at) & (page - 1);
    if (span <= in_page) return false;
    off += span - in_page;
  }
  return true;
}

// ---- occluders ------------------------------------------------------------------------------------------------------
enum Bad : uint8_t { kOk, kBadDynamic, kBadType, kBadTooBig, kBadPointers, kBadStream, kBadRange, kBadValues,
                     kBadMemory, kBadCount };
constexpr const char* kBadNames[kBadCount] = {"ok", "dynamic", "type", "too big", "pointers", "stream", "range",
                                              "values", "memory"};

struct Occluder {
  uint32_t mesh = 0;
  uint32_t ib = 0, first = 0, prims = 0;  // what it was extracted from (a change re-extracts)
  float m0 = 0, m12 = 0, m13 = 0, m14 = 0;  // the matrix' first element and translation at extraction
  std::vector<float> v;       // world positions, 3 a vertex
  std::vector<uint32_t> tri;  // 3 vertex indices a triangle
  float lo[3] = {}, hi[3] = {};
  uint8_t cull = 1;  // 1 the normal side, 2 the reversed side (mirrored)
  uint8_t bad = kOk;
  uint64_t used = 0;  // the last frame it was listed
};
// The worker's alone (the render thread only lists mesh pointers).
std::vector<Occluder> g_occ;
std::unordered_map<uint32_t, uint32_t> g_by_mesh;
std::vector<uint32_t> g_free;
uint64_t g_bytes = 0;

struct Stats {
  uint64_t frames = 0, listed = 0, walk_ns = 0;  // the render thread's (under g_mutex)
  uint64_t jobs = 0, extracted = 0, re_extracted = 0, drawn = 0, tris = 0, small = 0, dense = 0, off = 0, budget = 0,
           hidden_occ = 0, raster_ns = 0, find_ns = 0, bad[kBadCount] = {};  // the worker's (added under g_mutex after each job)
  uint64_t cached = 0, bytes = 0;
};
Stats g_st;     // reported (under g_mutex)
Stats g_wst;    // the worker's running counts (the worker only)

void Release(Occluder& o) {
  g_bytes -= o.v.capacity() * sizeof(float) + o.tri.capacity() * sizeof(uint32_t);
  std::vector<float>().swap(o.v);
  std::vector<uint32_t>().swap(o.tri);
}

uint8_t Extract(const uint8_t* base, Occluder& o, uint32_t mesh) {
  static const uint32_t max_tris = uint32_t(REXCVAR_GET(dh_raster_mesh_tris));
  static const uint64_t max_bytes = uint64_t(REXCVAR_GET(dh_raster_mem_mb)) << 20;
  if (!Readable(base, mesh, 240)) return kBadPointers;
  const uint32_t flags = L32(base, mesh + 208);
  if (flags & 0x80000000u) return kBadDynamic;
  const uint32_t type = (flags >> 24) & 7;
  if (type > 1) return kBadType;
  const uint32_t ibo = L32(base, mesh), vf = L32(base, mesh + 4);
  const uint32_t first = L32(base, mesh + 176), prims = L32(base, mesh + 180);
  const uint32_t minv = L32(base, mesh + 184), maxv = L32(base, mesh + 188);
  if (!prims) return kBadType;
  if (prims > max_tris) return kBadTooBig;
  if (!Readable(base, ibo, 24) || !Readable(base, vf, 412)) return kBadPointers;
  const uint32_t ib_rhi = L32(base, ibo + 20);
  if (!Readable(base, ib_rhi, 12)) return kBadPointers;
  const uint32_t d3dib = L32(base, ib_rhi + 8);
  if (!Readable(base, d3dib, 32)) return kBadPointers;
  const uint32_t common = L32(base, d3dib), iaddr = L32(base, d3dib + 24), isize = L32(base, d3dib + 28);
  const bool i32 = (common & 0x80000000u) != 0;
  const uint32_t iend = (common >> 29) & 3;
  const uint32_t npos = L32(base, vf + 408);
  if (npos == 0 || npos > 16) return kBadStream;
  const uint32_t vbo = L32(base, vf + 216), stride = L32(base, vf + 220);
  if (stride < 12 || stride > 512 || (stride & 3)) return kBadStream;
  if (!Readable(base, vbo, 24)) return kBadPointers;
  const uint32_t vb_rhi = L32(base, vbo + 20);
  if (!Readable(base, vb_rhi, 12)) return kBadPointers;
  const uint32_t d3dvb = L32(base, vb_rhi + 8);
  if (!Readable(base, d3dvb, 32)) return kBadPointers;
  const uint32_t vaddr = L32(base, d3dvb + 24) & ~3u, f1 = L32(base, d3dvb + 28);
  const uint32_t vsize = f1 & 0x03FFFFFCu, vend = f1 & 3;
  if (vend != 0 && vend != 2) return kBadStream;
  if (i32 ? (iend != 0 && iend != 2) : (iend != 0 && iend != 1)) return kBadStream;
  const uint32_t isz = i32 ? 4 : 2;
  const uint64_t nidx = type == 0 ? uint64_t(prims) * 3 : uint64_t(prims) + 2;
  if ((uint64_t(first) + nidx) * isz > isize || maxv < minv || maxv - minv > 0x100000u ||
      (uint64_t(maxv) * stride + 12) > vsize)
    return kBadRange;
  if (!Readable(base, iaddr + first * isz, uint32_t(nidx * isz)) ||
      !Readable(base, vaddr + minv * stride, (maxv - minv) * stride + 12))
    return kBadPointers;
  const uint64_t need = (uint64_t(maxv - minv + 1) * 3 + uint64_t(prims) * 3) * 4;
  if (g_bytes + need > max_bytes) return kBadMemory;

  float m[16];
  for (int i = 0; i < 16; ++i) m[i] = LF(base, mesh + 48 + 4 * i);
  for (float f : m)
    if (!std::isfinite(f)) return kBadValues;
  static thread_local std::vector<int32_t> remap;
  remap.assign(size_t(maxv - minv + 1), -1);
  std::vector<float> verts;
  std::vector<uint32_t> tris;
  tris.reserve(size_t(prims) * 3);
  auto index_at = [&](uint64_t k) -> uint32_t {
    const uint32_t a = iaddr + uint32_t((first + k) * isz);
    if (i32) return iend ? L32(base, a) : L32le(base, a);
    return iend ? L16(base, a) : L16le(base, a);
  };
  auto local = [&](uint32_t idx, uint32_t& out) -> bool {
    if (idx < minv || idx > maxv) return false;
    int32_t& r = remap[idx - minv];
    if (r < 0) {
      const uint32_t a = vaddr + idx * stride;
      const float x = vend ? LF(base, a) : LFle(base, a);
      const float y = vend ? LF(base, a + 4) : LFle(base, a + 4);
      const float z = vend ? LF(base, a + 8) : LFle(base, a + 8);
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
      r = int32_t(verts.size() / 3);
      verts.push_back(x * m[0] + y * m[4] + z * m[8] + m[12]);
      verts.push_back(x * m[1] + y * m[5] + z * m[9] + m[13]);
      verts.push_back(x * m[2] + y * m[6] + z * m[10] + m[14]);
    }
    out = uint32_t(r);
    return true;
  };
  const uint32_t restart = i32 ? 0xFFFFFFFFu : 0xFFFFu;
  if (type == 0) {
    for (uint32_t t = 0; t < prims; ++t) {
      uint32_t a, b, c;
      if (!local(index_at(uint64_t(t) * 3), a) || !local(index_at(uint64_t(t) * 3 + 1), b) ||
          !local(index_at(uint64_t(t) * 3 + 2), c))
        return kBadRange;
      if (a == b || b == c || a == c) continue;
      tris.push_back(a);
      tris.push_back(b);
      tris.push_back(c);
    }
  } else {
    uint32_t i0 = 0, i1 = 0;
    int have = 0, parity = 0;
    for (uint64_t k = 0; k < nidx; ++k) {
      const uint32_t raw = index_at(k);
      if (raw == restart) {
        have = 0;
        parity = 0;
        continue;
      }
      uint32_t li;
      if (!local(raw, li)) return kBadRange;
      if (have < 2) {
        (have == 0 ? i0 : i1) = li;
        ++have;
        continue;
      }
      if (i0 != i1 && i1 != li && i0 != li) {
        tris.push_back(parity ? i1 : i0);
        tris.push_back(parity ? i0 : i1);
        tris.push_back(li);
      }
      parity ^= 1;
      i0 = i1;
      i1 = li;
    }
  }
  if (tris.empty()) return kBadType;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (size_t i = 0; i < verts.size(); i += 3)
    for (int c = 0; c < 3; ++c) {
      lo[c] = std::min(lo[c], verts[i + c]);
      hi[c] = std::max(hi[c], verts[i + c]);
    }
  for (int c = 0; c < 3; ++c)
    if (!(hi[c] - lo[c] < 2.0e6f) || std::fabs(lo[c]) > 1.0e7f) return kBadValues;
  verts.shrink_to_fit();
  tris.shrink_to_fit();
  o.v = std::move(verts);
  o.tri = std::move(tris);
  g_bytes += o.v.capacity() * sizeof(float) + o.tri.capacity() * sizeof(uint32_t);
  std::memcpy(o.lo, lo, sizeof(lo));
  std::memcpy(o.hi, hi, sizeof(hi));
  // UE3 flips the culling for a mirrored LocalToWorld (its determinant's sign).
  const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                    m[2] * (m[4] * m[9] - m[5] * m[8]);
  o.cull = det < 0.0f ? 2 : 1;
  return kOk;
}

void Remember(const uint8_t* base, Occluder& o, uint32_t mesh) {
  o.ib = L32(base, mesh);
  o.first = L32(base, mesh + 176);
  o.prims = L32(base, mesh + 180);
  o.m0 = LF(base, mesh + 48);
  o.m12 = LF(base, mesh + 96), o.m13 = LF(base, mesh + 100), o.m14 = LF(base, mesh + 104);
}
bool Same(const uint8_t* base, const Occluder& o, uint32_t mesh) {
  return L32(base, mesh) == o.ib && L32(base, mesh + 176) == o.first && L32(base, mesh + 180) == o.prims &&
         LF(base, mesh + 48) == o.m0 && LF(base, mesh + 96) == o.m12 && LF(base, mesh + 100) == o.m13 &&
         LF(base, mesh + 104) == o.m14;
}

// The cached occluder of 'mesh' (extracted when new or changed), or null (not an occluder).
Occluder* Find(const uint8_t* base, uint32_t mesh, uint64_t frame) {
  // 2026-11-10 (PC: 55-108 ms a job): no mapping check for a cached mesh; one the render thread just found in the live
  // draw list is about to be drawn (mapped). New or changed ones are checked by Extract.
  auto it = g_by_mesh.find(mesh);
  Occluder* o;
  if (it != g_by_mesh.end()) {
    o = &g_occ[it->second];
    if (Same(base, *o, mesh)) {
      o->used = frame;
      return o->bad == kOk ? o : nullptr;
    }
    Release(*o);
    ++g_wst.re_extracted;
  } else {
    uint32_t slot;
    if (!g_free.empty()) {
      slot = g_free.back();
      g_free.pop_back();
    } else {
      slot = uint32_t(g_occ.size());
      g_occ.emplace_back();
    }
    g_by_mesh[mesh] = slot;
    o = &g_occ[slot];
    *o = Occluder{};
    ++g_wst.extracted;
  }
  o->mesh = mesh;
  o->used = frame;
  Remember(base, *o, mesh);  // what it was tried from (a failure is not tried again while this stays)
  o->bad = Extract(base, *o, mesh);
  if (o->bad != kOk) {
    ++g_wst.bad[o->bad];
    return nullptr;
  }
  return o;
}

// Occluders not listed for 600 frames go (their memory back).
void Prune(uint64_t frame) {
  if (frame % 64) return;
  for (uint32_t s = 0; s < g_occ.size(); ++s) {
    Occluder& o = g_occ[s];
    if (!o.mesh || frame - o.used < 600) continue;
    Release(o);
    g_by_mesh.erase(o.mesh);
    o = Occluder{};
    g_free.push_back(s);
  }
}

// ---- rasterizer (ME2's) -----------------------------------------------------------------------------------------------
constexpr float kNear = 1.0f;  // clip w below which a point counts as at the near plane (Dishonored's near: 5)
struct V4 {
  float x, y, z, w;
};
typedef float V4f __attribute__((vector_size(16)));
V4 Xform(const float* m, float x, float y, float z) {
  return {x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
          x * m[2] + y * m[6] + z * m[10] + m[14], x * m[3] + y * m[7] + z * m[11] + m[15]};
}

struct Buffer {
  int w = 0, h = 0;
  std::vector<float> corner;  // (w + 1) x (h + 1) samples at pixel corners: nearest occluder 1/w (0 = none)
  std::vector<float> pix;     // w x h: the farthest of the pixel's four corners
  std::vector<float> tile;    // (w / 8) x (h / 8): the farthest pixel of the tile
  int tw = 0, th = 0;
  float vp[16] = {};
};

void InitBuffer(Buffer& b, int w, int h) {
  b.w = w;
  b.h = h;
  b.corner.assign(size_t(w + 1) * size_t(h + 1), 0.0f);
  b.pix.assign(size_t(w) * size_t(h), 0.0f);
  b.tw = (w + 7) / 8;
  b.th = (h + 7) / 8;
  b.tile.assign(size_t(b.tw) * size_t(b.th), 0.0f);
}

// One screen triangle (sx, sy in pixels, y down; iz = 1/w) into the corner samples; cull: 0 none, 1 cull clockwise
// (area > 0 with y down), 2 cull counter-clockwise.
void DrawTri(Buffer& b, const float* p0, const float* p1, const float* p2, int cull) {
  const float area = (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]);
  if (std::fabs(area) < 1e-6f) return;
  if ((cull == 1 && area > 0.0f) || (cull == 2 && area < 0.0f)) return;
  const int x0 = std::max(0, int(std::ceil(std::min({p0[0], p1[0], p2[0]}))));
  const int x1 = std::min(b.w, int(std::floor(std::max({p0[0], p1[0], p2[0]}))));
  const int y0 = std::max(0, int(std::ceil(std::min({p0[1], p1[1], p2[1]}))));
  const int y1 = std::min(b.h, int(std::floor(std::max({p0[1], p1[1], p2[1]}))));
  if (x1 < x0 || y1 < y0) return;
  auto edge = [](const float* u, const float* v, float& A, float& B, float& C) {
    A = u[1] - v[1];
    B = v[0] - u[0];
    C = u[0] * v[1] - u[1] * v[0];
  };
  float A0, B0, C0, A1, B1, C1, A2, B2, C2;
  edge(p1, p2, A0, B0, C0);
  edge(p2, p0, A1, B1, C1);
  edge(p0, p1, A2, B2, C2);
  const float s = area > 0.0f ? 1.0f : -1.0f;
  A0 *= s, B0 *= s, C0 *= s, A1 *= s, B1 *= s, C1 *= s, A2 *= s, B2 *= s, C2 *= s;
  const float inv = 1.0f / std::fabs(area);
  const float z0 = p0[2] * inv, z1 = p1[2] * inv, z2 = p2[2] * inv;
  const float zA = A0 * z0 + A1 * z1 + A2 * z2, zB = B0 * z0 + B1 * z1 + B2 * z2, zC = C0 * z0 + C1 * z1 + C2 * z2;
  const float A[3] = {A0, A1, A2}, B[3] = {B0, B1, B2}, C[3] = {C0, C1, C2};
  const int stride = b.w + 1;
  float ls[2] = {0.0f, 0.0f}, li[2] = {float(x0), float(x0)};
  float rs[2] = {0.0f, 0.0f}, ri[2] = {float(x1), float(x1)};
  int nl = 0, nr = 0;
  int ry0 = y0, ry1 = y1;
  for (int k = 0; k < 3; ++k) {
    if (A[k] > 0.0f || A[k] < 0.0f) {
      const float ik = 1.0f / A[k];
      const float slope = -B[k] * ik, icpt = -C[k] * ik;
      if (A[k] > 0.0f) {
        if (nl < 2) ls[nl] = slope, li[nl] = icpt, ++nl;
      } else {
        if (nr < 2) rs[nr] = slope, ri[nr] = icpt, ++nr;
      }
    } else {
      if (B[k] > 0.0f) {
        const float t = std::ceil(-C[k] / B[k]);
        if (!(t <= float(y1))) return;
        if (t > float(ry0)) ry0 = int(t);
      } else if (B[k] < 0.0f) {
        const float t = std::floor(-C[k] / B[k]);
        if (!(t >= float(y0))) return;
        if (t < float(ry1)) ry1 = int(t);
      } else if (C[k] < 0.0f) {
        return;
      }
    }
  }
  const float bx0 = float(x0), bx1 = float(x1);
  for (int y = ry0; y <= ry1; ++y) {
    const float fy = float(y);
    const float lo = std::max(std::max(bx0, ls[0] * fy + li[0]), ls[1] * fy + li[1]);
    const float hi = std::min(std::min(bx1, rs[0] * fy + ri[0]), rs[1] * fy + ri[1]);
    if (!(lo <= hi)) continue;
    const int xs = int(std::ceil(lo)), xe = int(std::floor(hi));
    if (xs > xe) continue;
    const float zr = zB * fy + zC;
    float* row = &b.corner[size_t(y) * stride];
    int x = xs;
    if (xe - xs >= 3) {
      const V4f step = {0.0f, 1.0f, 2.0f, 3.0f};
      const V4f za4 = {zA, zA, zA, zA};
      for (; x + 3 <= xe; x += 4) {
        V4f r;
        std::memcpy(&r, row + x, sizeof(r));
        const float z0x = zr + zA * float(x);
        const V4f v = V4f{z0x, z0x, z0x, z0x} + za4 * step;
        r = r > v ? r : v;
        std::memcpy(row + x, &r, sizeof(r));
      }
    }
    for (; x <= xe; ++x) {
      const float v = zr + zA * float(x);
      row[x] = row[x] > v ? row[x] : v;
    }
  }
}

// A clip-space triangle cut at the near plane, drawn as a fan.
void DrawClipTri(Buffer& b, const V4& v0, const V4& v1, const V4& v2, int cull) {
  const V4 in[3] = {v0, v1, v2};
  V4 poly[4];
  int n = 0;
  for (int i = 0; i < 3; ++i) {
    const V4& p = in[i];
    const V4& q = in[(i + 1) % 3];
    const bool pin = p.w > kNear, qin = q.w > kNear;
    if (pin) poly[n++] = p;
    if (pin != qin) {
      const float t = (kNear - p.w) / (q.w - p.w);
      poly[n++] = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t, kNear};
    }
  }
  if (n < 3) return;
  float s[4][3];
  for (int i = 0; i < n; ++i) {
    const float iw = 1.0f / poly[i].w;
    s[i][0] = (poly[i].x * iw * 0.5f + 0.5f) * float(b.w);
    s[i][1] = (0.5f - poly[i].y * iw * 0.5f) * float(b.h);
    s[i][2] = iw;
  }
  DrawTri(b, s[0], s[1], s[2], cull);
  if (n == 4) DrawTri(b, s[0], s[2], s[3], cull);
}

void Resolve(Buffer& b) {
  const int stride = b.w + 1;
  for (int y = 0; y < b.h; ++y)
    for (int x = 0; x < b.w; ++x) {
      const float* r0 = &b.corner[size_t(y) * stride + x];
      const float* r1 = r0 + stride;
      b.pix[size_t(y) * b.w + x] = std::min(std::min(r0[0], r0[1]), std::min(r1[0], r1[1]));
    }
  for (int ty = 0; ty < b.th; ++ty)
    for (int tx = 0; tx < b.tw; ++tx) {
      float m = 1e30f;
      for (int y = ty * 8; y < std::min(b.h, ty * 8 + 8); ++y)
        for (int x = tx * 8; x < std::min(b.w, tx * 8 + 8); ++x) m = std::min(m, b.pix[size_t(y) * b.w + x]);
      b.tile[size_t(ty) * b.tw + tx] = m;
    }
}

struct Candidate {
  float area;
  const Occluder* o;
  float x0, x1, y0, y1, max_iz;
};
struct ScreenVert {
  float x, y, iz;
  uint32_t out;
};

bool HiddenAlready(const Buffer& b, const Candidate& c) {
  if (!(c.max_iz > 0.0f)) return false;
  const int sx0 = std::max(0, int(std::floor(c.x0))), sx1 = std::min(b.w, int(std::ceil(c.x1)));
  const int sy0 = std::max(0, int(std::floor(c.y0))), sy1 = std::min(b.h, int(std::ceil(c.y1)));
  if (sx1 < sx0 || sy1 < sy0) return false;
  if ((sx1 - sx0 + 1) * (sy1 - sy0 + 1) > 4096) return false;
  const int stride = b.w + 1;
  for (int y = sy0; y <= sy1; ++y) {
    const float* row = &b.corner[size_t(y) * stride];
    for (int x = sx0; x <= sx1; ++x)
      if (!(row[x] > c.max_iz)) return false;
  }
  return true;
}

// Draws the occluders 'occ' with view-projection vp into b (the worker).
void Rasterize(const float* vp, const std::vector<const Occluder*>& occ, Buffer& b) {
  static const uint64_t budget = uint64_t(REXCVAR_GET(dh_raster_tris));
  static const float min_px = float(REXCVAR_GET(dh_raster_min_px));
  static const float min_ppt = float(REXCVAR_GET(dh_raster_min_px_per_tri));
  static const int winding = REXCVAR_GET(dh_raster_winding);
  std::fill(b.corner.begin(), b.corner.end(), 0.0f);
  std::memcpy(b.vp, vp, sizeof(b.vp));
  static std::vector<Candidate> cands;
  cands.clear();
  const float W = float(b.w), H = float(b.h);
  for (const Occluder* op : occ) {
    const Occluder& o = *op;
    int out_l = 0, out_r = 0, out_b = 0, out_t = 0, out_n = 0;
    float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f, max_iz = 0.0f;
    for (int c = 0; c < 8; ++c) {
      const V4 p = Xform(vp, (c & 1) ? o.hi[0] : o.lo[0], (c & 2) ? o.hi[1] : o.lo[1], (c & 4) ? o.hi[2] : o.lo[2]);
      out_l += p.x < -p.w;
      out_r += p.x > p.w;
      out_b += p.y < -p.w;
      out_t += p.y > p.w;
      if (p.w <= kNear) {
        ++out_n;
        continue;
      }
      const float iw = 1.0f / p.w;
      const float sx = (p.x * iw * 0.5f + 0.5f) * W, sy = (0.5f - p.y * iw * 0.5f) * H;
      mnx = std::min(mnx, sx);
      mxx = std::max(mxx, sx);
      mny = std::min(mny, sy);
      mxy = std::max(mxy, sy);
      max_iz = std::max(max_iz, iw);
    }
    if (out_l == 8 || out_r == 8 || out_b == 8 || out_t == 8 || out_n == 8) {
      ++g_wst.off;
      continue;
    }
    float area;
    if (out_n) {
      area = W * H;
      max_iz = 0.0f;
    } else {
      area = (std::min(mxx, W) - std::max(mnx, 0.0f)) * (std::min(mxy, H) - std::max(mny, 0.0f));
      if (!(area >= min_px)) {
        ++g_wst.small;
        continue;
      }
      if (min_ppt > 0.0f && area < min_ppt * float(o.tri.size() / 3)) {
        ++g_wst.dense;
        continue;
      }
    }
    cands.push_back({area, op, mnx, mxx, mny, mxy, max_iz});
  }
  std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& c) { return a.area > c.area; });
  uint64_t tris = 0;
  static std::vector<V4> clip;
  static std::vector<ScreenVert> sv;
  for (const Candidate& c : cands) {
    const Occluder& o = *c.o;
    const uint64_t n = o.tri.size() / 3;
    if (tris + n > budget) {
      ++g_wst.budget;
      continue;
    }
    if (HiddenAlready(b, c)) {
      ++g_wst.hidden_occ;
      continue;
    }
    tris += n;
    ++g_wst.drawn;
    const size_t nv = o.v.size() / 3;
    clip.resize(nv);
    sv.resize(nv);
    for (size_t i = 0; i < nv; ++i) {
      const V4 p = Xform(vp, o.v[3 * i], o.v[3 * i + 1], o.v[3 * i + 2]);
      clip[i] = p;
      ScreenVert& q = sv[i];
      q.out = uint32_t(p.x < -p.w) | uint32_t(p.x > p.w) << 1 | uint32_t(p.y < -p.w) << 2 |
              uint32_t(p.y > p.w) << 3 | uint32_t(p.w <= kNear) << 4;
      if (!(q.out & 16)) {
        const float iw = 1.0f / p.w;
        q.x = (p.x * iw * 0.5f + 0.5f) * W;
        q.y = (0.5f - p.y * iw * 0.5f) * H;
        q.iz = iw;
      }
    }
    const int cull = !winding ? 0 : (o.cull == 1 ? winding : 3 - winding);
    for (size_t t = 0; t < o.tri.size(); t += 3) {
      const uint32_t ia = o.tri[t], ib = o.tri[t + 1], ic = o.tri[t + 2];
      const uint32_t oa = sv[ia].out, ob = sv[ib].out, oc = sv[ic].out;
      if (oa & ob & oc) continue;
      if ((oa | ob | oc) & 16)
        DrawClipTri(b, clip[ia], clip[ib], clip[ic], cull);
      else
        DrawTri(b, &sv[ia].x, &sv[ib].x, &sv[ic].x, cull);
    }
  }
  g_wst.tris += tris;
  Resolve(b);
}

// A world box against a buffer: 1 visible, 2 hidden, 3 reaches the near plane (visible), 5 outside the frustum.
// max_pixels: a box whose screen rectangle is larger is tested by tiles only (hidden only when every 8x8 tile under it
// is; otherwise visible without the per-pixel pass). The occlusion queries pass no limit.
uint8_t BoxCode(const Buffer& b, const float* mn, const float* mx, float bias, int max_pixels = 1 << 30) {
  int out_l = 0, out_r = 0, out_b = 0, out_t = 0, behind = 0;
  float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f, max_iz = 0.0f;
  bool near_cut = false;
  for (int i = 0; i < 8; ++i) {
    const V4 p = Xform(b.vp, (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1], (i & 4) ? mx[2] : mn[2]);
    out_l += p.x < -p.w;
    out_r += p.x > p.w;
    out_b += p.y < -p.w;
    out_t += p.y > p.w;
    behind += p.w <= 0.0f;
    if (p.w <= kNear) {
      near_cut = true;
      continue;
    }
    const float iw = 1.0f / p.w;
    const float sx = (p.x * iw * 0.5f + 0.5f) * float(b.w), sy = (0.5f - p.y * iw * 0.5f) * float(b.h);
    mnx = std::min(mnx, sx);
    mxx = std::max(mxx, sx);
    mny = std::min(mny, sy);
    mxy = std::max(mxy, sy);
    max_iz = std::max(max_iz, iw);
  }
  if (out_l == 8 || out_r == 8 || out_b == 8 || out_t == 8 || behind == 8) return 5;
  if (near_cut) return 3;
  const int x0 = std::max(0, int(std::floor(mnx))), x1 = std::min(b.w - 1, int(std::ceil(mxx)) - 1);
  const int y0 = std::max(0, int(std::floor(mny))), y1 = std::min(b.h - 1, int(std::ceil(mxy)) - 1);
  if (x1 < x0 || y1 < y0) return 5;
  const float limit = max_iz * (1.0f + bias);
  bool all = true;
  for (int ty = y0 / 8; all && ty <= y1 / 8; ++ty)
    for (int tx = x0 / 8; tx <= x1 / 8; ++tx)
      if (!(b.tile[size_t(ty) * b.tw + tx] > limit)) {
        all = false;
        break;
      }
  if (all) return 2;
  if ((x1 - x0 + 1) * (y1 - y0 + 1) > max_pixels) return 1;
  for (int y = y0; y <= y1; ++y) {
    const float* row = &b.pix[size_t(y) * b.w];
    for (int x = x0; x <= x1; ++x)
      if (!(row[x] > limit)) return 1;
  }
  return 2;
}

// ---- the worker ---------------------------------------------------------------------------------------------------
// One job at a time (the world DPG's pre-pass of a view): the meshes listed by the render thread, drawn by the worker.
struct Job {
  const uint8_t* base = nullptr;
  uint32_t view = 0;
  uint64_t frame = 0;
  float vp[16] = {};
  float origin[3] = {};  // the view's origin (view + 752): dh_raster_prims matches a buffer to a view by it
  std::vector<uint32_t> meshes;
};
std::mutex g_mutex;
std::condition_variable g_cv;
Job g_job;  // posted by the render thread
bool g_job_pending = false, g_job_running = false;
// The last two finished buffers (the queries of a frame are read a frame later): their view and frame.
struct Done {
  Buffer b;
  uint32_t view = 0;
  uint64_t frame = ~uint64_t(0);
  float origin[3] = {};
};
Done g_done[2];
uint32_t g_done_next = 0;  // the one the next finished job replaces
bool g_worker_started = false;

void Worker() {
#if REX_PLATFORM_SWITCH
  {
    // The preferred core: the highest the process has in the mask (core 3 with the NSP forwarder, mostly idle; core 2
    // otherwise). A preferred core outside the process's mask (or -1) is refused.
    const uint32_t mask = uint32_t(REXCVAR_GET(dh_raster_mask));
    const uint32_t usable = mask & uint32_t(RexSwitchProcessCoreMask());
    const int preferred = usable ? 31 - __builtin_clz(usable) : -1;
    const bool ok = RexSwitchSetCurrentThreadMask(preferred, (unsigned long long)mask);
    REXLOG_INFO("[dh-raster] worker on cores {:#x}, preferred {} ({})", mask, preferred, ok ? "set" : "FAILED");
  }
#endif
  const int w = REXCVAR_GET(dh_raster_w), h = REXCVAR_GET(dh_raster_h);
  Buffer work;
  InitBuffer(work, w, h);
  std::vector<const Occluder*> occ;
  std::vector<uint32_t> slots;
  // 2026-11-12 (perf item 2): the job lives across frames and its mesh list is swapped with the posted one, so both
  // lists keep their storage. Moving it out left g_job.meshes empty: the render thread's assign() allocated a new list
  // every frame and this thread freed the old one (newlib's one malloc lock, ~0.65 % of UE3's render thread in slow
  // frames of NRO b4563e94).
  Job job;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_cv.wait(lock, [] { return g_job_pending; });
      std::swap(job.meshes, g_job.meshes);
      job.base = g_job.base;
      job.view = g_job.view;
      job.frame = g_job.frame;
      std::memcpy(job.vp, g_job.vp, sizeof(job.vp));
      std::memcpy(job.origin, g_job.origin, sizeof(job.origin));
      g_job_pending = false;
      g_job_running = true;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // Slots first: Find may grow g_occ (moving it), so pointers are taken only once every mesh is found.
    slots.clear();
    for (uint32_t mesh : job.meshes)
      if (const Occluder* o = Find(job.base, mesh, job.frame)) slots.push_back(uint32_t(o - g_occ.data()));
    occ.clear();
    for (uint32_t slot : slots) occ.push_back(&g_occ[slot]);
    const auto t1 = std::chrono::steady_clock::now();
    g_wst.find_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    Rasterize(job.vp, occ, work);
    Prune(job.frame);
    g_wst.raster_ns +=
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    ++g_wst.jobs;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      Done& d = g_done[g_done_next];
      g_done_next ^= 1;
      std::swap(d.b, work);
      if (work.w != w || work.h != h) InitBuffer(work, w, h);
      d.view = job.view;
      d.frame = job.frame;
      std::memcpy(d.origin, job.origin, sizeof(d.origin));
      g_job_running = false;
      // the worker's counts into the report's
      Stats& s = g_st;
      s.jobs += g_wst.jobs, s.extracted += g_wst.extracted, s.re_extracted += g_wst.re_extracted;
      s.drawn += g_wst.drawn, s.tris += g_wst.tris, s.small += g_wst.small, s.dense += g_wst.dense;
      s.off += g_wst.off, s.budget += g_wst.budget, s.hidden_occ += g_wst.hidden_occ, s.raster_ns += g_wst.raster_ns;
      s.find_ns += g_wst.find_ns;
      for (int i = 0; i < kBadCount; ++i) s.bad[i] += g_wst.bad[i];
      s.cached = g_by_mesh.size();
      s.bytes = g_bytes;
      g_wst = Stats{};
    }
    g_cv.notify_all();
  }
}

// Answer statistics (relaxed counters: the render thread's tests and dh_occlusion.cpp's comparisons).
std::atomic<uint64_t> st_tests{0}, st_no_buffer{0}, st_code[6] = {};
std::atomic<uint64_t> st_cmp_agree{0}, st_cmp_false_hidden{0}, st_cmp_false_visible{0}, st_cmp_gpu_hidden{0},
    st_cmp_cpu_hidden{0}, st_answered{0};

void Report() {
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(10)) return;
  last = now;
  Stats s;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    s = g_st;
    const uint64_t cached = g_st.cached, bytes = g_st.bytes;
    g_st = Stats{};
    g_st.cached = cached, g_st.bytes = bytes;
  }
  const auto take = [](std::atomic<uint64_t>& a) { return a.exchange(0, std::memory_order_relaxed); };
  std::string bad;
  for (int i = 1; i < kBadCount; ++i)
    if (s.bad[i]) bad += fmt::format(" {} {}", kBadNames[i], s.bad[i]);
  const uint64_t agree = take(st_cmp_agree), fh = take(st_cmp_false_hidden), fv = take(st_cmp_false_visible);
  const uint64_t gpu_hidden = take(st_cmp_gpu_hidden), cpu_hidden = take(st_cmp_cpu_hidden);
  const uint64_t cmp = agree + fh + fv;
  const uint64_t jobs = std::max<uint64_t>(s.jobs, 1);
  REXLOG_INFO("[dh-raster] 10 s: {} pre-passes, {:.0f} meshes listed a frame ({:.3f} ms on UE3's render thread a frame); "
              "worker: {} jobs, {:.2f} ms a job ({:.2f} finding the meshes), {:.0f} occluders drawn a job ({:.0f} triangles), {} off screen, {} "
              "small, {} dense, {} over the budget, {} already hidden; {} extracted, {} re-extracted, {} meshes cached "
              "({:.1f} MB); not occluders:{}. Tests {}: {} without a buffer; visible {}, hidden "
              "{}, near plane {}, off screen {}. Against the GPU counts: {} queries, {:.1f} % agree (GPU hidden {}, "
              "rasterizer hidden {}), {} false hidden, {} false visible; {} queries answered by the rasterizer",
              s.frames, s.frames ? double(s.listed) / double(s.frames) : 0.0,
              s.frames ? double(s.walk_ns) / 1e6 / double(s.frames) : 0.0, s.jobs, double(s.raster_ns) / 1e6 / double(jobs), double(s.find_ns) / 1e6 / double(jobs),
              double(s.drawn) / double(jobs), double(s.tris) / double(jobs), s.off, s.small, s.dense, s.budget,
              s.hidden_occ, s.extracted, s.re_extracted, s.cached, double(s.bytes) / 1048576.0,
              bad.empty() ? " none" : bad, take(st_tests), take(st_no_buffer),
              take(st_code[1]), take(st_code[2]), take(st_code[3]), take(st_code[5]), cmp,
              cmp ? 100.0 * double(agree) / double(cmp) : 0.0, gpu_hidden, cpu_hidden, fh, fv, take(st_answered));
}

std::atomic<uint32_t> g_dump_calls{0}, g_dumps{0};
void DumpView(const uint8_t* base, uint32_t view) {
  for (uint32_t o = 0; o < 1600; o += 64) {
    std::string line;
    for (uint32_t k = 0; k < 16; ++k) {
      const float f = LF(base, view + o + k * 4);
      line += std::isfinite(f) && std::fabs(f) < 1e7f ? fmt::format(" {:.4g}", f)
                                                      : fmt::format(" #{:08X}", L32(base, view + o + k * 4));
    }
    REXLOG_INFO("[dh-raster] view +{}:{}", o, line);
  }
}

// The visible meshes of one depth list (the view's static mesh visibility bits).
void ListVisible(const uint8_t* base, uint32_t list, uint32_t words, std::vector<uint32_t>& out) {
  const uint32_t order = L32(base, list), norder = L32(base, list + 4), links = L32(base, list + 12);
  if (!Plausible(order) || !Plausible(links) || norder > 100000) return;
  for (uint32_t i = 0; i < norder; ++i) {
    const uint32_t link = links + L32(base, order + 4 * i) * 80;
    const uint32_t compact = L32(base, link), n = L32(base, link + 16), full = L32(base, link + 12);
    if (!Plausible(compact) || !Plausible(full) || n > 1000000) continue;
    for (uint32_t e = 0; e < n; ++e) {
      const uint32_t w = L32(base, compact + 8 * e), m = L32(base, compact + 8 * e + 4);
      if (w > (1u << 20) || !(L32(base, words + 4 * w) & m)) continue;
      const uint32_t mesh = L32(base, full + 12 * e + 4);
      if (Plausible(mesh)) out.push_back(mesh);
    }
  }
}

}  // namespace

extern "C" int DhRasterMode() {
  static const int mode = REXCVAR_GET(dh_raster_mode);
  return mode;
}

// The rasterizer's answer for a world box of a view's frame (dh_occlusion.cpp, at the query's read, a frame after it was
// issued): 0 none (no finished buffer for that view and frame), 1 visible, 2 hidden, 3 reaches the near plane, 5
// outside the frustum. Never waits.
extern "C" uint8_t DhRasterTestBox(uint32_t view, uint64_t frame, const float mn[3], const float mx[3]) {
  if (!DhRasterMode()) return 0;
  st_tests.fetch_add(1, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(g_mutex);
  const Done* d = nullptr;
  for (const Done& c : g_done)
    if (c.view == view && c.frame == frame && c.b.w) d = &c;
  if (!d) {
    st_no_buffer.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }
  static const float bias = float(REXCVAR_GET(dh_raster_bias));
  const uint8_t code = BoxCode(d->b, mn, mx, bias);
  st_code[code < 6 ? code : 0].fetch_add(1, std::memory_order_relaxed);
  return code;
}

// 2026-11-10 (console: the per-box lock cost 3 % of UE3's render thread): a query's boxes (6 floats each: min, max) under
// one lock: 0 none (no buffer), 1 visible (one box visible or reaching the near plane), 2 hidden (every box hidden or off
// screen).
extern "C" uint8_t DhRasterTestBoxes(uint32_t view, uint64_t frame, const float* boxes, uint32_t n) {
  if (!DhRasterMode() || !n) return 0;
  st_tests.fetch_add(n, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(g_mutex);
  const Done* d = nullptr;
  for (const Done& c : g_done)
    if (c.view == view && c.frame == frame && c.b.w) d = &c;
  if (!d) {
    st_no_buffer.fetch_add(n, std::memory_order_relaxed);
    return 0;
  }
  static const float bias = float(REXCVAR_GET(dh_raster_bias));
  for (uint32_t i = 0; i < n; ++i) {
    const float* box = boxes + 6 * i;
    const uint8_t code = BoxCode(d->b, box, box + 3, bias);
    st_code[code < 6 ? code : 0].fetch_add(1, std::memory_order_relaxed);
    if (code == 1 || code == 3) return 1;
  }
  return 2;
}

// A query whose GPU count came back: the rasterizer said 'cpu' (1 visible, 2 hidden).
extern "C" void DhRasterCompare(uint8_t cpu, bool gpu_visible) {
  if (!gpu_visible) st_cmp_gpu_hidden.fetch_add(1, std::memory_order_relaxed);
  if (cpu == 2) st_cmp_cpu_hidden.fetch_add(1, std::memory_order_relaxed);
  if ((cpu == 2) == !gpu_visible)
    st_cmp_agree.fetch_add(1, std::memory_order_relaxed);
  else if (cpu == 2)
    st_cmp_false_hidden.fetch_add(1, std::memory_order_relaxed);
  else
    st_cmp_false_visible.fetch_add(1, std::memory_order_relaxed);
}
extern "C" void DhRasterAnswered() { st_answered.fetch_add(1, std::memory_order_relaxed); }

// ---------------------------------------------------------------------------------------------------------------------
// 2026-11-11 (user): every primitive against the rasterizer, in UE3's view setup. UE3 asks an occlusion query only for
// part of the primitives in the frustum (about 200 of them a frame at the console's 23 fps spot, against ~1,500 static
// mesh elements drawn): some classes are never queried, and one that was visible is asked again only now and then.
// sub_82636AC0(renderer r3, view r4, compact primitive r5, f1 = distance squared, r7 = 1 when UE3 does not query this
// primitive's class) is the static mesh relevance of one primitive that passed UE3's own frustum, distance and
// occlusion tests; its callers (sub_82646368 and its siblings) treat a 0 answer as "not drawn" for every pass. The hook
// tests the primitive's bounds box (compact primitive +4 origin, +16 extent, world space) against the newest finished
// depth buffer of this view (the frame before: the same one-frame delay UE3's own queries have) and answers 0 for a
// hidden one. A box outside that buffer's frustum or at its near plane counts as visible. A buffer is this view's
// when it is at most 2 frames old and its camera stood within kPrimOriginReach units (scene captures and other views
// have their own origin: no buffer, nothing culled).
// dh_raster_prims: 0 off, 1 count only, 2 cull the hidden primitives of the classes UE3 never queries, 3 cull every
// hidden primitive. "[dh-raster] primitives" every 10 s. The A/B rotation's "raster_prims" slot counts only.
// 2026-11-13 (user: Dark Vision shows no characters behind walls): Dark Vision draws characters through walls by taking
// them out of UE3's occlusion queries (the caller's r7 = 1, "a class UE3 never queries"); this hook then hid them
// behind the wall like any other unqueried primitive. Only primitives with static mesh elements (compact primitive
// +56 = FPrimitiveSceneInfo, its +16/+20 = StaticMeshes data/count, as sub_82636AC0 itself reads them: the level's
// static geometry, nearly all of what this culls) are hidden now. Characters, skeletal and movable meshes (dynamic
// relevance only, 0 static meshes) are left to UE3: its own queries (answered by dh_raster_mode) still cull them
// outside Dark Vision. "N without static meshes kept" in the report.
REXCVAR_DEFINE_INT32(dh_raster_prims, 0, "Dishonored",
                     "Occlusion rasterizer for every primitive in UE3's view setup: 0 off, 1 count what it would hide, "
                     "2 hide the hidden primitives UE3 never queries, 3 hide every hidden primitive (needs "
                     "dh_raster_mode)")
    .range(0, 3)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_raster_prims_pixels, 96, "Dishonored",
                     "dh_raster_prims: a primitive whose box covers more depth-buffer pixels than this is tested by "
                     "8x8 tiles only (cheaper, a little less culled)")
    .range(0, 1 << 20)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
bool (*g_dh_ab_raster_prims_off)() = nullptr;  // dh_ab_cycle.cpp
namespace {
constexpr float kPrimOriginReach = 600.0f;
// UE3's render thread only.
struct PrimView {
  uint64_t frame = ~uint64_t(0);
  uint32_t view = 0;
  const Done* done = nullptr;
};
PrimView g_prim_view;
struct PrimStats {
  uint64_t calls = 0, no_buffer = 0, visible = 0, near_or_out = 0;
  uint64_t hidden[2] = {};  // [0] a class UE3 queries, [1] a class it never queries
  uint64_t culled = 0;
  uint64_t dynamic_kept = 0;  // hidden, but no static mesh elements (characters, Dark Vision): drawn
};
PrimStats g_prim;

// The newest finished buffer of this view (see above). The pointer stays good for the rest of this view setup: the one
// job that can finish meanwhile replaces the OLDER of the two buffers.
const Done* PrimBuffer(const uint8_t* base, uint32_t view, uint64_t frame) {
  if (g_prim_view.frame == frame && g_prim_view.view == view) return g_prim_view.done;
  g_prim_view.frame = frame;
  g_prim_view.view = view;
  g_prim_view.done = nullptr;
  float o[3];
  for (int i = 0; i < 3; ++i) o[i] = LF(base, view + 752 + 4 * i);
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const Done& c : g_done) {
    if (!c.b.w || c.frame > frame || frame - c.frame > 2) continue;
    const float dx = c.origin[0] - o[0], dy = c.origin[1] - o[1], dz = c.origin[2] - o[2];
    if (!(dx * dx + dy * dy + dz * dz <= kPrimOriginReach * kPrimOriginReach)) continue;
    if (!g_prim_view.done || c.frame > g_prim_view.done->frame) g_prim_view.done = &c;
  }
  return g_prim_view.done;
}

void PrimReport(int mode) {
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (now - last < std::chrono::seconds(10)) return;
  last = now;
  const PrimStats& s = g_prim;
  REXLOG_INFO("[dh-raster] primitives 10 s (mode {}): {} reached UE3's relevance, {} without a buffer of their view; "
              "visible {}, near plane or outside the old frustum {}; HIDDEN {} of the classes UE3 queries + {} of the "
              "classes it never queries; {} not drawn, {} without static meshes kept",
              mode, s.calls, s.no_buffer, s.visible, s.near_or_out, s.hidden[0], s.hidden[1], s.culled,
              s.dynamic_kept);
  g_prim = PrimStats{};
}
}  // namespace

REX_EXTERN(__imp__sub_82636AC0);
REX_HOOK_RAW(sub_82636AC0) {
  static const int mode = DhRasterMode() ? REXCVAR_GET(dh_raster_prims) : 0;
  if (mode) {
    const uint32_t view = ctx.r4.u32, prim = ctx.r5.u32;
    const bool unqueried = ctx.r7.u32 != 0;
    ++g_prim.calls;
    if ((g_prim.calls & 0x3FF) == 0) PrimReport(mode);
    const Done* d = PrimBuffer(base, view, g_sa2_game_frames.load(std::memory_order_relaxed));
    if (!d) {
      ++g_prim.no_buffer;
    } else {
      float mn[3], mx[3];
      bool ok = true;
      for (int i = 0; i < 3; ++i) {
        const float c = LF(base, prim + 4 + 4 * i), e = LF(base, prim + 16 + 4 * i);
        ok = ok && std::isfinite(c) && e >= 0.0f && e < 1.0e7f;
        mn[i] = c - e;
        mx[i] = c + e;
      }
      static const float bias = float(REXCVAR_GET(dh_raster_bias));
      // 2026-11-11 (console stacks: this test was ~10 % of UE3's view setup, as much as the relevance it guards): big
      // rectangles by tiles only.
      static const int max_px = REXCVAR_GET(dh_raster_prims_pixels);
      const uint8_t code = ok ? BoxCode(d->b, mn, mx, bias, max_px) : uint8_t(3);
      if (code == 2) {
        ++g_prim.hidden[unqueried ? 1 : 0];
        // The primitive's static meshes (sub_82636AC0 reads the same words first thing, no check needed).
        const uint32_t info = L32(base, prim + 56);
        const bool has_static = info != 0 && static_cast<int32_t>(L32(base, info + 20)) > 0;
        if (!has_static) {
          ++g_prim.dynamic_kept;
        } else if ((mode == 3 || (mode == 2 && unqueried)) &&
                   !(g_dh_ab_raster_prims_off && g_dh_ab_raster_prims_off())) {
          ++g_prim.culled;
          ctx.r3.u64 = 0;  // no static mesh relevance: the callers leave the primitive out of the view
          return;
        }
      } else if (code == 1) {
        ++g_prim.visible;
      } else {
        ++g_prim.near_or_out;
      }
    }
  }
  __imp__sub_82636AC0(ctx, base);
}

// ---------------------------------------------------------------------------------------------------------------------
// 2026-11-11 (user): the world depth prepass skipped. Console stacks of 2026-11-10: the prepass is ~11 % of the scene
// time of UE3's render thread (core 0, the frame's limit) and about a fifth of the draws, while the GPU has headroom at
// 360 lines. The base pass draws the same meshes with depth test and write (the prepass holds only the occluder lists,
// so the base pass cannot rely on it), so without the prepass the picture is the same and only the GPU's early depth
// rejection is lost (more overdraw). Kept when the renderer asks for the full prepass ([renderer + 4484] != 0: the
// masked lists are drawn too and sub_8263C650 projects shadows from that depth BEFORE the base pass), and for the other
// depth priority groups (the foreground group's depth, which sub_82631F30 / sub_82632348 draw with DPG 2). The
// rasterizer's mesh listing above runs either way. "[prepass]" every 10 s. The A/B rotation's "prepass_skip" slot
// draws the prepass again.
REXCVAR_DEFINE_BOOL(dh_prepass_skip, false, "Dishonored",
                    "UE3's world depth prepass is not drawn when nothing reads its depth before the base pass (less "
                    "work for both render threads, more overdraw on the GPU)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
bool (*g_dh_ab_prepass_off)() = nullptr;  // dh_ab_cycle.cpp
namespace {
// UE3's render thread only.
bool PrepassSkip(const uint8_t* base, uint32_t renderer, uint32_t dpg) {
  static const bool on = REXCVAR_GET(dh_prepass_skip);
  if (!on) return false;
  static uint64_t skipped = 0, full = 0, other = 0, ab = 0;
  static auto last = std::chrono::steady_clock::now();
  bool skip = false;
  if (dpg != 1 || !Readable(base, renderer + 4484, 4)) {
    ++other;
  } else if (L32(base, renderer + 4484) != 0) {
    ++full;
  } else if (g_dh_ab_prepass_off && g_dh_ab_prepass_off()) {
    ++ab;
  } else {
    ++skipped;
    skip = true;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - last >= std::chrono::seconds(10)) {
    REXLOG_INFO("[prepass] 10 s: {} world prepasses skipped, {} drawn because the renderer needs the full prepass "
                "(shadows from its depth), {} drawn in the A/B off slot, {} of other depth priority groups drawn",
                skipped, full, ab, other);
    skipped = full = ab = other = 0;
    last = now;
  }
  return skip;
}
}  // namespace

// 2026-11-11 (console run 03:15: with dh_prepass_skip the world prepass was never skipped at a 21 fps spot, 567 of 1,305
// draws a frame depth-only; the system setting bAllowWholeSceneDominantShadows=False did not change that).
// sub_8263C368(renderer, DPG) walks the scene's lights and answers 1 when one of them (a dominant light with dynamic
// shadows in view: sub_8263CCB8 / sub_82601088) needs its shadows projected BEFORE the base pass; sub_826313B0 stores
// the answer for the world DPG in [renderer + 4484], which makes the prepass draw everything (the masked lists too) and
// sub_8263C650 project those shadows from its depth. With dh_prepass_no_dominant_shadows the answer is 0, as on the
// frames where no such light is in view (the game's own path): the dominant lights' dynamic shadows (the sun's shadows
// of characters and movable objects; baked shadows stay) are not drawn, and the prepass can be skipped everywhere.
// Only with dh_prepass_skip; the A/B rotation's "prepass_skip" slot gives the game's own answer.
REXCVAR_DEFINE_BOOL(dh_prepass_no_dominant_shadows, false, "Dishonored",
                    "With dh_prepass_skip: dominant lights' dynamic shadows are not projected before the base pass, so "
                    "the full depth prepass is never needed (the sun's dynamic shadows go; baked ones stay)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REX_EXTERN(__imp__sub_8263C368);
REX_HOOK_RAW(sub_8263C368) {
  static const bool on = REXCVAR_GET(dh_prepass_skip) && REXCVAR_GET(dh_prepass_no_dominant_shadows);
  if (on && !(g_dh_ab_prepass_off && g_dh_ab_prepass_off())) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8263C368(ctx, base);
}

REX_EXTERN(__imp__sub_826320E0);
// RenderPrePass(renderer, DPG, view): the world DPG's visible depth-list meshes to the worker, then the pass.
REX_HOOK_RAW(sub_826320E0) {
  const uint32_t renderer = ctx.r3.u32, dpg = ctx.r4.u32, view = ctx.r5.u32;
  static const uint32_t every = uint32_t(std::max(REXCVAR_GET(dh_raster_dump), 0));
  if (every) {
    const uint32_t n = g_dump_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n % every == 0 && g_dumps.fetch_add(1, std::memory_order_relaxed) < 5) {
      REXLOG_INFO("[dh-raster] RenderPrePass call {}: renderer {:08X} scene {:08X} DPG {} view {:08X}", n, renderer,
                  L32(base, renderer), dpg, view);
      DumpView(base, view);
    }
  }
  if (DhRasterMode() && dpg == 1 && Readable(base, renderer, 4) && Readable(base, view, 1600)) {
    const auto t0 = std::chrono::steady_clock::now();
    const uint32_t scene = L32(base, renderer);
    const uint64_t frame = g_sa2_game_frames.load(std::memory_order_relaxed);
    static std::vector<uint32_t> meshes;
    meshes.clear();
    if (Readable(base, scene + 2448, 304)) {
      const uint32_t vis = view + 1344;
      const uint32_t heap = L32(base, vis + 16);
      const uint32_t words = heap ? heap : vis;
      for (uint32_t off : {8u, 80u}) ListVisible(base, scene + 2448 + off, words, meshes);
    }
    float vp[16];
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
      vp[i] = LF(base, view + 560 + 4 * i);
      ok = ok && std::isfinite(vp[i]);
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_worker_started) {
        g_worker_started = true;
        new std::thread(Worker);  // leaked on purpose: detach() throws on the Switch (ME2 learned it too)
        REXLOG_INFO("[dh-raster] occlusion rasterizer on (mode {}: {})", DhRasterMode(),
                    DhRasterMode() == 1 ? "compared with the GPU counts" : "answers the occlusion queries");
      }
      g_st.walk_ns +=
          uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
      ++g_st.frames;
      g_st.listed += meshes.size();
      if (ok && !g_job_pending && !g_job_running) {
        g_job.base = base;
        g_job.view = view;
        g_job.frame = frame;
        std::memcpy(g_job.vp, vp, sizeof(vp));
        for (int i = 0; i < 3; ++i) g_job.origin[i] = LF(base, view + 752 + 4 * i);
        g_job.meshes.assign(meshes.begin(), meshes.end());
        g_job_pending = true;
      }
    }
    g_cv.notify_all();
    Report();
  }
  if (PrepassSkip(base, renderer, dpg)) {
    ctx.r3.u64 = 1;  // "drawn", as the pass always answers in a level: the callers go on as usual
    return;
  }
  __imp__sub_826320E0(ctx, base);
}
