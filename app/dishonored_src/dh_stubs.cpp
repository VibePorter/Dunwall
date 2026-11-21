// Dishonored (2026-11-08): what the generic app code still asks of ME2's game code (app/me2_src, built only with
// PORT_GAME=me2), answered with off / no-op versions. Each entry names the ME2 file it stands in for.

#include <cstdint>

// me2_src/me2_write_census.cpp: the device write census (an ME2 diagnostic build); off.
#if !defined(ME2_WRITE_CENSUS)  // a census build has the real ones (me2_src/me2_write_census.cpp)
extern "C" void Me2CensusDeviceStart() {}
extern "C" void Me2CensusDeviceTick() {}
#endif

// me2_src/hot/me_hot_guest.cpp: ME2's check of stale pixel constants before a draw (its DOF / bloom passes); off.
extern "C" void Me2BadConstsDraw(const uint8_t*, uint32_t) {}

#include <atomic>

#include <rex/cvar.h>

// me2_src/me2_hooks.cpp: ME2's frame-cap switch, read by the renderer's pacing (sa2_renut_pacing.cpp), name kept so
// the pacing code stays Lazarus'. 2026-11-16 (FPS unlocker from Lazarus, with me2_fps_limit there): Dishonored's 30 fps
// is only its presentation interval (TWO, device +14000, console log: 29.85 fps); UE3's own smoothing allows up to
// 130 (Coalesced_INT.bin Engine.Engine: bSmoothFrameRate TRUE, Min 30, MaxSmoothedFrameRate 130), so this lifts it.
REXCVAR_DEFINE_BOOL(me2_unlock_framerate, false, "ME2",
                    "Remove the game's presentation-interval frame cap (30 fps); display VSync remains controlled by "
                    "renut_vsync, UE3's own cap is 130 fps. me2_fps_limit sets a cap instead. Read at startup")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// me2_src/me2_frame_diag.cpp: set while UE3 has rendering suspended (ME2's hook); never set for Dishonored.
std::atomic<bool> g_me2_d3d_suspended{false};

// me2_src/hot/me_hot_guest.cpp: 0 = the stale pixel-constant check (Me2BadConstsDraw) is off and never called.
extern "C" int8_t g_me2_bad_consts = 0;

// me2_src/me2_me1_import.cpp: an ME2 feature started by the app (me2_main.cpp); none here.
namespace rex {
class Runtime;
}
namespace me2::me1_import {
void Startup(rex::Runtime*) {}
}
// me2::gyro_menu::Load is dh_gyro_menu.cpp's (2026-11-21: Options -> Controls gyro rows).
