// Dishonored (2026-11-09): the A/B rotation (dh_ab_cycle.cpp), from Lazarus' me2_ab_cycle. A test run turns the listed
// features off one at a time (all on, the first off, all on, the second off, ...) and logs frames and draws each
// second, so a player standing still gets each feature's cost or saving from neighbouring slots of the same view
// (tools/rhi/ab_draws.py).
#pragma once

enum class DhAb : int {
  kOcclusion,  // dh_occlusion_queries (off = every occlusion query answers visible)
  kHistory,    // dh_occlusion_history_frames (off = a late query answers visible)
  kFillCheck,  // Renut's vertex-fill mapping check (render/draw.cpp GuestRangeReadable; off = no check)
  kUnscaled,   // renut_unscaled_max_size (off = small targets follow the resolution: the 3D picture tints yellow)
  kSmallMesh,  // dh_cull_small_factor (off = small static meshes keep the game's own draw distance)
  kPrepassSkip,  // dh_prepass_skip (off = the world depth prepass is drawn, as the game does)
  kLodScale,     // dh_lod_distance_scale (off = the game's own LOD and draw distances)
  kRasterPrims,  // dh_raster_prims (off = hidden primitives are only counted)
  kRingSpin,     // sa2_renut_ring_spin_us (off = Renut's render thread sleeps as soon as it has caught up)
  kCount
};

// Whether the rotation has this feature off right now (false without a rotation).
bool DhAbOff(DhAb id);
// Once a frame (the Swap hook): moves the rotation on and writes the "[ab]" line each second.
void DhAbTick();
