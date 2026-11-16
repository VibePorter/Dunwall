// Dishonored (2026-11-12, user: speed up the loadings) - the Switch's CPU boost mode during loads, from Lazarus'
// me2_load_boost (me2_src/me2_frame_diag.cpp, ME2-only there).
//
// The console logs of 2026-11-12 (graphical_bugs, 17:50 session) have level loads of 12-31 s, and in them UE3's game
// thread (Main XThread) at 84 % of a core with 10 % waiting in the kernel: the load is the CPU (package serialization,
// PostLoad, LZX), not the SD (58.6 MB in 6 s of reads in a 15 s window, beside it). The CPU runs at 1020 MHz. Horizon's
// boost mode (appletSetCpuBoostMode FastLoad: CPU 1785 MHz, GPU at its minimum, 76.8 MHz) is what retail games use for
// their loading screens: an official mode, not an overclock. A load is seen as frames with almost no draws (the loading
// screen draws ~6 a frame, play 150 or more, the main menu ~800) or as no Swap for dh_load_boost_stall_ms with the game
// in focus (the game thread blocked in a load); the boost ends at the first frame that draws more again. A fast check:
// a watchdog thread looks every 50 ms. Each boost logs one "[load-boost]" line when it ends (how long, the frames in it).
//
// Only an application gets the boost (started from the HOME menu forwarder); from hbmenu (an applet) the call fails and
// is logged once. Off by default (2026-11-12, user: not wanted); dh_load_boost = true turns it on.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(dh_load_boost, false, "Dishonored",
                    "CPU boost mode (1785 MHz, GPU at its minimum) while the game loads: frames with almost no draws, "
                    "or no frame for a while")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_load_boost_draws, 24, "Dishonored",
                     "dh_load_boost: a frame with at most this many draws counts as a loading frame")
    .range(0, 1000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_load_boost_stall_ms, 200, "Dishonored",
                     "dh_load_boost: no frame for this long (the game in focus) counts as a load")
    .range(50, 10000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern std::atomic<uint64_t> g_sa2_game_frames;  // src/sa2_capture.cpp: counted at each Swap
extern std::atomic<uint64_t> g_me2_draws_total;  // renut d3d hooks/draw.cpp: every draw (game side)

namespace {

uint64_t NowMs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

#if REX_PLATFORM_SWITCH
bool SetBoost(bool on) {
  const Result rc = appletSetCpuBoostMode(on ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
  static int failures = 0;
  if (R_FAILED(rc)) {
    if (failures++ < 2)
      REXLOG_WARN("[load-boost] CPU boost {} refused (0x{:X}; only an application started from the HOME menu gets "
                  "it)",
                  on ? "on" : "off", rc);
    return false;
  }
  return true;
}

void Watchdog() {
  const uint64_t few = uint64_t(std::max(0, REXCVAR_GET(dh_load_boost_draws)));
  const uint64_t stall_ms = uint64_t(REXCVAR_GET(dh_load_boost_stall_ms));
  uint64_t frames_before = g_sa2_game_frames.load(std::memory_order_relaxed);
  uint64_t draws_before = g_me2_draws_total.load(std::memory_order_relaxed);
  uint64_t last_frame_ms = NowMs();
  bool boosted = false;
  uint64_t since_ms = 0, boost_frames0 = 0, boosts = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const uint64_t now = NowMs();
    const uint64_t frames = g_sa2_game_frames.load(std::memory_order_relaxed);
    const uint64_t draws = g_me2_draws_total.load(std::memory_order_relaxed);
    const uint64_t new_frames = frames - frames_before, new_draws = draws - draws_before;
    if (new_frames)
      last_frame_ms = now;
    const bool in_focus = appletGetFocusState() == AppletFocusState_InFocus;
    // Loading: the frames of these 50 ms drew almost nothing, or no frame came for a while.
    const bool quiet = new_frames && new_draws <= few * new_frames;
    const bool stalled = !new_frames && now - last_frame_ms >= stall_ms;
    const bool drawing = new_frames && new_draws > few * new_frames;
    if (!boosted && in_focus && (quiet || stalled)) {
      if (SetBoost(true)) {
        boosted = true;
        since_ms = now;
        boost_frames0 = frames;
        ++boosts;
      }
    } else if (boosted && (drawing || !in_focus)) {
      SetBoost(false);
      boosted = false;
      REXLOG_INFO("[load-boost] #{}: CPU at 1785 MHz for {:.1f} s ({} frames, until {})", boosts,
                  double(now - since_ms) / 1000.0, frames - boost_frames0,
                  in_focus ? "a frame drew again" : "the game left focus");
    }
    frames_before = frames;
    draws_before = draws;
  }
}
#endif

}  // namespace

// Called once the configuration is loaded (dh_hooks.cpp).
void DhStartLoadBoost() {
#if REX_PLATFORM_SWITCH
  static std::atomic<bool> started{false};
  if (started.exchange(true) || !REXCVAR_GET(dh_load_boost))
    return;
  REXLOG_INFO("[load-boost] on: CPU boost mode while the game loads (at most {} draws a frame, or no frame for {} ms)",
              REXCVAR_GET(dh_load_boost_draws), REXCVAR_GET(dh_load_boost_stall_ms));
  // Left allocated, never joined (libnx has no pthread_detach; see me2_frame_diag.cpp's watchdog).
  new std::thread(Watchdog);
#endif
}
