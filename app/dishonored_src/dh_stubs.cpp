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

// me2_src/me2_hooks.cpp: ME2's frame-cap switch, read by the renderer's pacing (sa2_renut_pacing.cpp). Kept as a
// setting so the pacing code is unchanged; Dishonored's own frame cap is not patched (yet).
REXCVAR_DEFINE_BOOL(me2_unlock_framerate, false, "ME2",
                    "Not used by Dishonored (ME2's guest frame cap); the renderer's pacing reads it");

// me2_src/me2_frame_diag.cpp: set while UE3 has rendering suspended (ME2's hook); never set for Dishonored.
std::atomic<bool> g_me2_d3d_suspended{false};

// me2_src/hot/me_hot_guest.cpp: 0 = the stale pixel-constant check (Me2BadConstsDraw) is off and never called.
extern "C" int8_t g_me2_bad_consts = 0;

// me2_src/me2_me1_import.cpp, me2_src/me2_gyro_menu.cpp: ME2 features started by the app (me2_main.cpp); none here.
namespace rex {
class Runtime;
}
namespace me2::me1_import {
void Startup(rex::Runtime*) {}
}
namespace me2::gyro_menu {
void Load() {}
}
