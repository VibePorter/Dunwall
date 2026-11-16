// Dishonored (2026-11-09): the A/B rotation (see dh_ab_cycle.h), adapted from Lazarus' app/me2_src/me2_ab_cycle.cpp.
//
// dh_ab_cycle lists the features to rotate ("occlusion,history,fill_check"), dh_ab_cycle_s the seconds of a slot. Slot
// k of the rotation: k even = everything as configured; k odd = feature k / 2 of the list off. Each second a line
//   [ab] t <s> slot <k> (<s> s in) off <name|-> frames <n> draws <n> (<x> a frame) begin <n> (<x> a frame)
// (the same as ME2's, so tools/rhi/ab_draws.py pairs each "off" slot with the slots around it; the first second of a
// slot is left out). Each slot change is logged too ("[ab] slot <k>: <name> off" / "everything on"), so a run watched
// on screen can be matched to the log by its time.
#include "dh_ab_cycle.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(dh_ab_cycle, "", "Dishonored/Diagnostics",
                      "A/B rotation: comma-separated features turned off one at a time (occlusion, history, "
                      "fill_check, unscaled, small_mesh, prepass_skip, lod_scale, raster_prims, ring_spin); \"[ab]\" lines each second; empty = off")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_ab_cycle_s, 10, "Dishonored/Diagnostics", "A/B rotation: seconds of a slot")
    .range(2, 600)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The parts outside this target ask through hooks set before main (dh_occlusion.cpp in mgr_renut_gpu, Renut's
// render/draw.cpp).
extern bool (*g_dh_ab_occlusion_off)();
extern bool (*g_dh_ab_history_off)();
extern bool (*g_dh_ab_raster_prims_off)();  // dh_occlusion_raster.cpp (dh_raster_prims)
extern bool (*g_dh_ab_prepass_off)();  // dh_occlusion_raster.cpp (dh_prepass_skip)
namespace renut::gpu {
extern bool (*g_fill_check_ab_off)();
extern bool (*g_unscaled_ab_off)();
extern bool (*g_ring_spin_ab_off)();
}
[[maybe_unused]] static const bool kHooksSet =
    (g_dh_ab_occlusion_off = [] { return DhAbOff(DhAb::kOcclusion); },
     g_dh_ab_history_off = [] { return DhAbOff(DhAb::kHistory); },
     g_dh_ab_prepass_off = [] { return DhAbOff(DhAb::kPrepassSkip); },
     g_dh_ab_raster_prims_off = [] { return DhAbOff(DhAb::kRasterPrims); },
     renut::gpu::g_fill_check_ab_off = [] { return DhAbOff(DhAb::kFillCheck); },
     renut::gpu::g_unscaled_ab_off = [] { return DhAbOff(DhAb::kUnscaled); },
     renut::gpu::g_ring_spin_ab_off = [] { return DhAbOff(DhAb::kRingSpin); }, true);

extern std::atomic<uint64_t> g_me2_draws_total;        // renut d3d hooks/draw.cpp: every DrawVertices / DrawIndexedVertices
extern std::atomic<uint64_t> g_me2_begin_draws_total;  // ... and the Begin*Vertices draws

namespace {

constexpr const char* kNames[int(DhAb::kCount)] = {"occlusion", "history", "fill_check", "unscaled", "small_mesh",
                                                    "prepass_skip", "lod_scale", "raster_prims", "ring_spin"};

std::vector<int>& Cuts() {
  static std::vector<int> cuts = [] {
    std::vector<int> out;
    const std::string list = REXCVAR_GET(dh_ab_cycle);
    size_t at = 0;
    while (at <= list.size()) {
      size_t end = list.find(',', at);
      if (end == std::string::npos) end = list.size();
      std::string name = list.substr(at, end - at);
      while (!name.empty() && name.front() == ' ') name.erase(name.begin());
      while (!name.empty() && name.back() == ' ') name.pop_back();
      if (!name.empty()) {
        int found = -1;
        for (int i = 0; i < int(DhAb::kCount); ++i)
          if (name == kNames[i]) found = i;
        if (found >= 0)
          out.push_back(found);
        else
          REXLOG_WARN("[ab] unknown feature '{}' in dh_ab_cycle (left out)", name);
      }
      at = end + 1;
    }
    if (!out.empty()) {
      std::string names;
      for (int c : out) names += std::string(names.empty() ? "" : ", ") + kNames[c];
      REXLOG_INFO("[ab] A/B rotation: {} s a slot, off in turn: {}", REXCVAR_GET(dh_ab_cycle_s), names);
    }
    return out;
  }();
  return cuts;
}

std::atomic<int> g_off{-1};  // the feature off now (-1: none)

}  // namespace

bool DhAbOff(DhAb id) { return g_off.load(std::memory_order_relaxed) == int(id); }

void DhAbTick() {
  const std::vector<int>& cuts = Cuts();
  if (cuts.empty()) return;
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  static const int slot_s = REXCVAR_GET(dh_ab_cycle_s);
  static int64_t last_second = -1, last_slot = -1;
  static uint64_t frames = 0, draws0 = g_me2_draws_total.load(std::memory_order_relaxed);
  static uint64_t begin0 = g_me2_begin_draws_total.load(std::memory_order_relaxed);
  ++frames;
  const int64_t t = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start).count();
  const int64_t slot = t / slot_s;
  const int64_t period = int64_t(cuts.size()) * 2;
  const auto off_in = [&](int64_t s) {
    const int64_t k = s % period;
    return (k & 1) ? cuts[size_t(k / 2)] : -1;
  };
  const int off = off_in(slot);
  if (t != last_second) {
    if (last_second >= 0) {
      // The second that just ended (its slot and feature).
      const int64_t prev_slot = last_second / slot_s;
      const int prev_off = off_in(prev_slot);
      const uint64_t draws1 = g_me2_draws_total.load(std::memory_order_relaxed);
      const uint64_t begin1 = g_me2_begin_draws_total.load(std::memory_order_relaxed);
      REXLOG_INFO("[ab] t {} slot {} ({} s in) off {} frames {} draws {} ({:.1f} a frame) begin {} ({:.1f} a frame)",
                  last_second, prev_slot, last_second - prev_slot * slot_s, prev_off < 0 ? "-" : kNames[prev_off],
                  frames, draws1 - draws0, frames ? double(draws1 - draws0) / double(frames) : 0.0, begin1 - begin0,
                  frames ? double(begin1 - begin0) / double(frames) : 0.0);
      draws0 = draws1;
      begin0 = begin1;
    }
    frames = 0;
    last_second = t;
  }
  if (slot != last_slot) {
    last_slot = slot;
    if (off < 0)
      REXLOG_INFO("[ab] slot {}: everything on", slot);
    else
      REXLOG_INFO("[ab] slot {}: {} off", slot, kNames[off]);
  }
  g_off.store(off, std::memory_order_relaxed);
}
