// Dishonored (2026-11-09) - size-aware draw distance for small static meshes, from Lazarus' me2_cull_distance.cpp
// (ME2, ported there from masseffect-cinnamon's ME1 code).
//
// Small static meshes (props, debris, bottles, rubble) are drawn however far away when the designers set no cull
// distance, and on the console both render threads cost about the same per draw (2026-11-09: UE3's and Renut's render
// threads at 92-97 % with 2,000-2,900 draws a frame). Here a small primitive gets a draw distance proportional to its
// size: beyond dh_cull_small_factor x its bounds sphere radius (at least dh_cull_small_min_distance) its static mesh
// elements are not drawn (in no pass: UE3 leaves it out of the view's static mesh visibility map). At the 360-line
// scene a primitive 100 x its radius away is a few pixels across. Primitives with a radius above
// dh_cull_small_max_radius (buildings, walls, terrain, large props) keep their own distance; dynamic primitives
// (characters, particles, effects) do not take this path.
//
// Where: UE3's static mesh relevance of one primitive, sub_82636AC0 (tools/dh_cull_distance.py): it compares
// distance^2 x LODFactor^2 with each element's MaxDrawDistanceSquared (FStaticMesh +228); the patch makes it read the
// maximum through dh_cull_max_distance_sq(max, FPrimitiveSceneInfo*, base, distance). FPrimitiveSceneInfo +28: the
// bounds sphere radius (the function's own screen-size test squares it against the distance).
// The A/B rotation (dh_ab_cycle "small_mesh") turns it off for a slot.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "dh_ab_cycle.h"

REXCVAR_DEFINE_INT32(dh_cull_small_factor, 100, "Dishonored",
                     "Small static meshes are not drawn beyond this many times their bounds radius (0 = off, the "
                     "game's own draw distances)")
    .range(0, 10000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_cull_small_max_radius, 256, "Dishonored",
                     "Only primitives with a bounds radius up to this many units get the size-based draw distance")
    .range(0, 100000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_cull_small_min_distance, 2000, "Dishonored",
                     "The size-based draw distance is at least this many units")
    .range(0, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

struct Settings {
  bool on = false;
  double factor = 0.0, max_radius = 0.0, min_distance = 0.0;
};
Settings g_settings;
bool g_ready = false;  // UE3's render thread only (InitViews)

const Settings& Get() {
  if (!g_ready) {
    g_settings.factor = double(REXCVAR_GET(dh_cull_small_factor));
    g_settings.max_radius = double(REXCVAR_GET(dh_cull_small_max_radius));
    g_settings.min_distance = double(REXCVAR_GET(dh_cull_small_min_distance));
    g_settings.on = g_settings.factor > 0.0;
    REXLOG_INFO("[cull] small static meshes: {} (beyond {} x radius, at least {} units, radius up to {})",
                g_settings.on ? "on" : "off", g_settings.factor, g_settings.min_distance, g_settings.max_radius);
    g_ready = true;
  }
  return g_settings;
}

uint64_t g_calls = 0, g_clamped = 0, g_culled = 0, g_logged = 0;
std::chrono::steady_clock::time_point g_last = std::chrono::steady_clock::now();

}  // namespace

// dist_sq: the element's distance squared x LOD factor squared (f0), only for the statistics: an element is culled
// here when its own maximum would have drawn it and the size-based one does not.
extern "C" double dh_cull_max_distance_sq(double max_sq, uint32_t primitive, uint8_t* base, double dist_sq) {
  const Settings& s = Get();
  if (!primitive || !s.on || DhAbOff(DhAb::kSmallMesh)) return max_sq;
  uint32_t bits;
  std::memcpy(&bits, base + primitive + 28, sizeof(bits));
  bits = __builtin_bswap32(bits);
  float radius;
  std::memcpy(&radius, &bits, sizeof(radius));
  ++g_calls;
  // Samples of the small ones (the first ones UE3 tests are the sky and the level's largest meshes).
  if (g_logged < 8 && radius > 0.0f && radius <= s.max_radius) {
    ++g_logged;
    REXLOG_INFO("[cull] sample primitive {:08X}: bounds radius {:.1f}, own max draw distance {:.0f}, distance {:.0f}",
                primitive, radius, max_sq > 0.0 && max_sq < 1e30 ? std::sqrt(max_sq) : -1.0,
                dist_sq > 0.0 ? std::sqrt(dist_sq) : 0.0);
  }
  if ((g_calls & 0xFFF) == 0) {
    const auto now = std::chrono::steady_clock::now();
    if (now - g_last >= std::chrono::seconds(10)) {
      REXLOG_INFO("[cull] last ~10 s: {} static mesh element tests, {} with a size-based draw distance, {} culled by it "
                  "({:.1f} %)",
                  g_calls, g_clamped, g_culled, g_calls ? 100.0 * double(g_culled) / double(g_calls) : 0.0);
      g_calls = g_clamped = g_culled = 0;
      g_last = now;
    }
  }
  if (!(radius > 0.0f) || radius > s.max_radius) return max_sq;
  const double d = std::max(s.min_distance, double(radius) * s.factor);
  const double d2 = d * d;
  if (d2 >= max_sq) return max_sq;
  ++g_clamped;
  if (dist_sq >= d2 && dist_sq < max_sq) ++g_culled;
  return d2;
}

// ---------------------------------------------------------------------------------------------------------------------
// 2026-11-11 (user: draw distance and LOD scale, balance of picture and performance): UE3's render thread on core 0 is
// the console's limit and nearly all its draws are the prepass and base pass static meshes. sub_82636AC0 tests every
// static mesh of a visible primitive with f0 = distance squared x the view's LOD factor (view +1156) against the mesh's
// Min / MaxDrawDistanceSquared: the LOD bands and the draw distances alike. tools/dh_cull_distance.py routes f0 through
// this: with dh_lod_distance_scale S every primitive counts as S times farther away, so simpler LODs come in sooner and
// meshes with a draw distance (the designers' or the size-based one above) go that much sooner. Meshes without any
// distance limit are untouched. 1 = off. The A/B rotation's "lod_scale" slot turns it off.
REXCVAR_DEFINE_DOUBLE(dh_lod_distance_scale, 1.0, "Dishonored",
                      "Static meshes pick their LOD and draw distance as if this many times farther from the camera "
                      "(1 = the game's own; 1.25 = LODs and draw distances 20 % nearer)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern "C" double dh_lod_distance_sq(double dist_sq) {
  static const double scale_sq = [] {
    const double s = std::clamp(REXCVAR_GET(dh_lod_distance_scale), 0.25, 8.0);
    REXLOG_INFO("[cull] LOD / draw distance scale of static meshes: {} ({})", s, s == 1.0 ? "off" : "on");
    return s * s;
  }();
  if (scale_sq == 1.0 || DhAbOff(DhAb::kLodScale)) return dist_sq;
  return double(float(dist_sq * scale_sq));
}
