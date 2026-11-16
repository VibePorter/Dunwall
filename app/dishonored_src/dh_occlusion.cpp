// Dishonored - real occlusion queries for UE3's occlusion culling on Renut's renderer (2026-11-09).
//
// WHAT THE GAME DOES (Dishonored's UE3 Xenon RHI, from the disassembly)
//   Queries come in batches ("managers", up to 200 queries each, a ring of them: sub_827779B8 hands out the next slot).
//   A manager keeps its query objects at +0 (4 bytes each), the count at +800, a GPU fence at +804 and a report buffer at
//   +812. The buffer holds records (stride +32, records +24, tiles +28, used +36, bases +40 / +44, the next record +52);
//   a record has one 32-byte tile per GPU tile, and the GPU writes its running ZPASS sample count there (little-endian
//   words +16 and +20 of each tile). sub_82B085F8(buffer) (XDK D3D) puts the GPU commands for the next record into the
//   command buffer and moves +36 / +52 on.
//     begin  sub_82777BA0(query): takes a slot (query +8 = manager, +12 = index k); for k == 0 writes record 0, the
//            batch's starting snapshot.
//     end    sub_82777C18(query): writes record k + 1. UE3's batched box loop sub_8262CEC0 (begin, DrawVertices of the
//            box, then sub_82B085F8 itself) does the same inline, so the END is taken at sub_82B085F8: a call with r > 0
//            records already written (+36) is the end of query r - 1 (r == 0: the batch's starting snapshot).
//     read   sub_827777B8(manager), after its fence (sub_82B01D28 / sub_82B01CC0): waits until the last record is no
//            longer the -275 marker, gives query k the count Sum(record k + 1) - Sum(record k) (query +16), then resets
//            the buffer to -275. UE3 draws its occlusion boxes back to back, so a difference is that query's box.
//
// WHY
//   Renut draws from the D3D calls it hooks and never runs the game's command buffer, so no record was ever written:
//   UE3's render thread spun forever on the first 3D frame. Since 2026-11-08 the read was answered "visible" (4096
//   samples a query, dh_hooks.cpp): no occlusion culling at all, every primitive in the frustum drawn.
//
// HOW (the host side is ME2's me2_occlusion.cpp: Renut's g_occlusion_before_draw / g_occlusion_after_draw and the
// render thread's carried BEGIN / END)
//   begin / end queue BEGIN / END to Renut's render thread in draw order; while a query is open each draw the render
//   thread records gets a host occlusion query of its own (Vulkan on the PC, deko3d counter reports on the Switch); the
//   guest query is their sum. The read writes cumulative counts into the records (record 0 = 0, record k + 1 = the sum
//   of queries 0..k) and lets the original take the differences. A query without a host result (not drawn, or recorded
//   into a command buffer) gets 4096: "visible", as before.
//   The GPU is usually still drawing the frame UE3 reads (it reads a frame later), so a query whose result is not back
//   yet takes the result the same boxes got up to dh_occlusion_history_frames frames earlier (the keys and history
//   below), else visible. A primitive that comes into view can so show up to that many frames later than on the 360.
//
// dh_occlusion_queries (default on): the above. Off: every query "visible" (the 2026-11-08 behaviour).

#include <algorithm>
#include <array>
#include <vector>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/backend/queued.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/guest/command_buffers.h"
#include "renut_engine/gpu/render/draw.h"
#include "renut_engine/gpu/render/surfaces.h"  // ResolutionScale
#include "renut_engine/gpu/render/textures.h"

REXCVAR_DEFINE_BOOL(dh_occlusion_queries, true, "Dishonored",
                    "Real occlusion queries for UE3's occlusion culling (host queries around the queries' draws); off = "
                    "every occlusion query answers visible")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// 2026-11-09 (console log 19:09, 2,000 draws a frame, the GPU 27-47 ms a frame): UE3 reads a batch one frame after
// issuing it, while the GPU is still drawing that frame: 60-100 % of the answers were late (visible, no culling), and
// the 8 ms waits took up to 1 s in every 10 s of UE3's render thread, the thread that limits the frame rate. Now 0: no
// wait; a late query takes the answer the same boxes got in a recent frame (dh_occlusion_history_frames).
REXCVAR_DEFINE_INT32(dh_occlusion_wait_us, 0, "Dishonored",
                     "How long a batch read waits for its host GPU results (with a forced submit) before the late "
                     "queries take a recent answer or visible (us); 0 = no wait")
    .range(0, 200000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_occlusion_history_frames, 3, "Dishonored",
                     "A query whose result is not back yet takes the result the same occlusion boxes got at most this "
                     "many frames earlier (keyed by the boxes' world-space bounds); 0 = late queries answer visible")
    .range(0, 16)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// 2026-11-10 (console: characters still vanished for a moment now and then, as Emily's head did): a hidden answer from
// the history is stricter than a visible one. Each primitive counts its consecutive hidden results; a late query
// answers hidden only when every primitive's last dh_occlusion_history_hidden_streak results were hidden, the newest
// at most dh_occlusion_history_hidden_frames old. Visible answers still come from up to dh_occlusion_history_frames.
REXCVAR_DEFINE_INT32(dh_occlusion_history_hidden_frames, 1, "Dishonored",
                     "A late query answers hidden from the history only with results at most this many frames old "
                     "(capped by dh_occlusion_history_frames)")
    .range(1, 16)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(dh_occlusion_history_hidden_streak, 2, "Dishonored",
                     "A late query answers hidden from the history only when each primitive's last this many results "
                     "were hidden (1 = the last one, as before)")
    .range(1, 8)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// 2026-11-10: with the rasterizer answering (dh_raster_mode 2), the batched queries it has boxes for need no GPU count:
// their box draws are not drawn at all (UE3's batch loop sub_8262CEC0 draws them with DrawVertices, dh_hooks.cpp). Such a
// query ends with no draw; when the rasterizer has no answer for it after all (no buffer of its frame), it reads visible.
REXCVAR_DEFINE_BOOL(dh_raster_skip_gpu_boxes, true, "Dishonored",
                    "With dh_raster_mode 2: the occlusion boxes of the queries the rasterizer answers are not drawn on "
                    "the GPU (less work for both render threads and the GPU)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(dh_occlusion_fence_nowait, true, "Dishonored",
                    "UE3's occlusion result read does not wait for the report buffer's GPU fence (the records are "
                    "written by the port, not the GPU)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
extern thread_local bool t_dh_fence_nowait;  // renut d3d hooks/device.cpp
REXCVAR_DEFINE_INT32(dh_occlusion_scale, 0, "Dishonored",
                     "Host samples are multiplied by this for the guest count (Renut renders below the guest's 720p); "
                     "0 = automatic: (1 / the scene's resolution scale) squared")
    .range(0, 64)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern std::atomic<uint64_t> g_sa2_game_frames;  // src/sa2_capture.cpp: counted at each Swap

// The A/B rotation (dishonored_src/dh_ab_cycle.cpp, set before main; null without it): whether it has the occlusion
// answers / the history off right now.
bool (*g_dh_ab_occlusion_off)() = nullptr;
bool (*g_dh_ab_history_off)() = nullptr;

// dh_occlusion_raster.cpp: the software rasterizer's answers (dh_raster_mode).
extern "C" int DhRasterMode();
extern "C" uint8_t DhRasterTestBox(uint32_t view, uint64_t frame, const float mn[3], const float mx[3]);
extern "C" uint8_t DhRasterTestBoxes(uint32_t view, uint64_t frame, const float* boxes, uint32_t n);
extern "C" void DhRasterCompare(uint8_t cpu, bool gpu_visible);
extern "C" void DhRasterAnswered();

#if defined(PLUME_DEKO3D)
namespace plume {
bool Deko3DOcclusionSnapshot(const RenderQueryPool*, uint32_t, uint64_t (&)[4]);  // plume_deko3d.cpp
}
#endif

namespace {
using namespace renut::gpu;

constexpr uint32_t kMarker = 0xFFFFFEEDu;  // -275: the records' "not written yet"
constexpr uint32_t kVisible = 4096;        // samples a query reads when it cannot be answered (as before)
constexpr uint32_t kBatch = 200;           // queries a manager holds (sub_827779B8)
constexpr uint32_t kPoolSize = 1u << 16;   // host queries, one a draw inside a guest query; reused after a wrap
constexpr uint32_t kRecords = 1u << 14;    // guest query issues kept for the reads

uint32_t LoadBE(const uint8_t* base, uint32_t va) {
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + va));
}
void StoreLE(uint8_t* base, uint32_t va, uint32_t v) { std::memcpy(base + va, &v, 4); }

// One issue (BEGIN..END) of a guest query. Written by the render thread, read by the batch read.
struct Record {
  std::atomic<uint64_t> seq{0};
  std::atomic<uint32_t> first{0}, count{0};
  std::atomic<uint32_t> state{0};  // kOpen, kClosed, kBroken
  std::atomic<uint64_t> frame{0};  // g_sa2_game_frames at BEGIN
  std::atomic<uint64_t> members{0};       // where its primitives' keys start in g_member_keys (a running position)
  std::atomic<uint32_t> member_count{0};  // how many (0: not a batched query, no history)
  std::atomic<bool> visible_last{false};  // from the one-primitive-a-query batcher (visible last frame)
  std::atomic<uint64_t> boxes{0};     // its boxes in g_boxes (a running position; with member_count; 0: none)
  std::atomic<uint32_t> box_view{0};  // the view they belong to (the rasterizer's buffers are by view and frame)
};
constexpr uint32_t kOpen = 0, kClosed = 1, kBroken = 2;
Record g_records[kRecords];
std::atomic<uint64_t> g_seq{0};
std::atomic<plume::RenderQueryPool*> g_pool{nullptr};

// Game side: the issue of each (report buffer, index). A manager keeps its buffer, and managers are pooled guest
// objects (a ring), so a small table by buffer address, never cleared: a new batch overwrites its indices. (By buffer,
// not manager: the END is seen at sub_82B085F8, which only has the buffer.)
struct ManagerSlot {
  std::atomic<uint32_t> manager{0};  // the report buffer's address
  std::atomic<uint64_t> seq[kBatch];
};
constexpr uint32_t kManagerSlots = 64;
ManagerSlot g_managers[kManagerSlots];

ManagerSlot* FindManager(uint32_t manager, bool insert) {
  if (!manager) return nullptr;
  const uint32_t h = ((manager >> 2) * 2654435761u) >> (32 - 6);
  for (uint32_t i = 0; i < kManagerSlots; ++i) {
    ManagerSlot& s = g_managers[(h + i) & (kManagerSlots - 1)];
    uint32_t m = s.manager.load(std::memory_order_acquire);
    if (m == manager) return &s;
    if (m == 0) {
      if (!insert) return nullptr;
      if (s.manager.compare_exchange_strong(m, manager, std::memory_order_acq_rel) || m == manager) return &s;
    }
  }
  return nullptr;
}

// Render thread only (State().mutex held).
struct RenderSide {
  std::unique_ptr<plume::RenderQueryPool> pool;
  bool pool_failed = false;
  uint32_t next = 0;
  uint64_t open_seq = 0;  // 0 = none
  uint32_t open_first = 0, open_count = 0;
  bool open_broken = false;
} g_rs;

// Stats (10 s report).
std::atomic<uint64_t> st_begun{0}, st_ended{0}, st_recording{0}, st_draws{0}, st_reads{0}, st_real_visible{0}, st_real_hidden{0},
    st_not_ready{0}, st_no_draws{0}, st_broken{0}, st_unknown{0}, st_waits{0}, st_wait_us{0}, st_kicks{0};
std::atomic<uint64_t> st_age[4];  // the reads by the batch's age in guest frames (0, 1, 2, 3+)
std::atomic<uint64_t> st_dead{0};

// 2026-11-09 (Ryujinx 1.3.272): deko3d's SamplesPassed reports come back, but the counter does not count (288000
// before and after every box, minutes apart), so every query read 0 samples and UE3 would have culled everything it
// tested. On the console UE3 tests the primitives in view and plenty of them have pixels. When kDeadAnswers answers in a
// row over at least kDeadReads batch reads all had 0 samples, the counter is taken as not counting: every query answers
// visible (as before this file) and the reads do not wait; a later result with samples turns the real answers back on.
constexpr uint32_t kDeadAnswers = 1500, kDeadReads = 60;
std::atomic<bool> g_counter_dead{false};
std::atomic<uint32_t> g_zero_answers{0}, g_zero_reads{0};
// 2026-11-09 (Ryujinx, in a level: ~200 queries a frame): the results never came back in time at all, every read waited
// dh_occlusion_wait_us after a forced submit (1.5 s in every 10 s, 9 fps) and answered visible. kLateReads reads in a
// row with nothing ready after their wait are treated like a counter that does not count (no waiting, all visible);
// a later read that finds a result with samples turns the real answers back on.
constexpr uint32_t kLateReads = 30;
std::atomic<uint32_t> g_late_reads{0};

// ---- Keys and history (2026-11-09) ----------------------------------------------------------------------------------
// UE3 tests primitives through two batchers (view +3928: one query for a group of primitives occluded last frame;
// +3968: one query a primitive), flushed by sub_8262CEC0. A batcher keeps each primitive's bounds (origin, extent: 24
// bytes, big-endian floats). Each primitive is keyed by those bounds rounded to whole units: UE3 builds the origin as
// view origin + (primitive - view origin), so its last bits change with the camera (exact keys never matched again,
// console 2026-11-09 20:00), and the batches' membership shifts from frame to frame, so a group is not a key either.
// The history keeps per primitive the last host result of a query it was in and that query's issue frame. A late
// query answers hidden only when every one of its primitives was hidden within dh_occlusion_history_frames, visible
// when one of them was visible, else visible (no answer). sub_8262D230's own queries (lights, by their matrices) are not
// keyed. Only UE3's render thread (begin, the batch loop, the read) touches the keys, the history and the late list.
uint64_t Mix(uint64_t h, uint64_t w) {
  h = (h ^ w) * 0x9E3779B97F4A7C15ull;
  return h ^ (h >> 29);
}

// 'shift': the view's translation the batcher's origins include (2026-11-09 20:26: UE3 hands the batcher origin +
// view +544, camera-relative, so without taking it off the keys changed whenever the camera moved).
uint64_t PrimitiveKey(const uint8_t* bounds, const float (&shift)[3]) {
  uint64_t h = 0x7072696D69746976ull;
  for (uint32_t i = 0; i < 6; ++i) {
    const uint32_t bits = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(bounds + i * 4));
    float f;
    std::memcpy(&f, &bits, 4);
    const double v = double(f) - (i < 3 ? double(shift[i]) : 0.0);
    const int64_t q = std::isfinite(f) ? int64_t(std::llround(v)) : int64_t(bits);
    h = Mix(h, uint64_t(q));
  }
  return h ? h : 1;
}

constexpr uint32_t kMemberRing = 1u << 18;
uint64_t g_member_keys[kMemberRing];
uint64_t g_member_pos = 0;  // running; a record's span is valid while fewer than kMemberRing were written after it

struct BatchKey {
  uint32_t query;
  uint64_t members;
  uint32_t count;
  bool visible_last;
  uint64_t boxes;  // the query's world boxes in g_boxes (dh_raster_mode; 0: none)
  uint32_t view;
};
// 2026-11-10 (dh_raster_mode): each batched primitive's world box (min xyz, max xyz), for the rasterizer's test at the
// query's read. UE3's render thread only.
constexpr uint32_t kBoxRing = 1u << 18;
float g_boxes[kBoxRing][6];
uint64_t g_box_pos = 1;  // running; 0 means "no boxes"
// 2026-11-09 20:14: 512 were too few (the one-primitive-a-query batcher flushes more in a frame). UE3's render thread
// only, so plain globals (thread_local arrays would be in every guest thread); the begins come in batch order, so a
// cursor finds each at once.
constexpr uint32_t kMaxBatchKeys = 8192;
BatchKey t_batch_keys[kMaxBatchKeys];
uint32_t t_batch_key_count = 0, t_batch_cursor = 0;
thread_local bool t_batch_loop = false;  // inside sub_8262CEC0
thread_local bool t_skip_box_draws = false;  // the open batched query's boxes are not drawn (dh_raster_skip_gpu_boxes)
std::atomic<uint64_t> st_skipped_draws{0};
// The view of UE3's occlusion pass sub_8262D230 (its argument + 96) while it runs: its batchers are view +3928 and
// +3968, the translation of their origins view +544.
thread_local uint32_t t_view = 0;

struct HistoryEntry {
  uint64_t key = 0;
  uint64_t frame = 0;  // the issue frame of the query whose result this is
  uint32_t count = 0;  // the guest count it answered (0: hidden)
  uint32_t hidden_streak = 0;  // consecutive hidden results up to this one (0: this one visible)
};
// 2026-11-09 20:14: 16K entries were evicted before the next frame looked them up ("a primitive never seen"): the
// group queries hold many primitives each. 256K entries (6 MB).
constexpr uint32_t kHistorySize = 1u << 18, kHistoryProbe = 8;
HistoryEntry g_history[kHistorySize];

void HistoryStore(uint64_t key, uint64_t frame, uint32_t count) {
  if (!key) return;
  const uint32_t h = uint32_t(key >> 40) & (kHistorySize - 1);
  HistoryEntry* victim = nullptr;
  for (uint32_t i = 0; i < kHistoryProbe; ++i) {
    HistoryEntry& e = g_history[(h + i) & (kHistorySize - 1)];
    if (e.key == key) {
      if (frame > e.frame) {  // a later harvest of an older issue does not win
        const uint32_t streak = count ? 0u : (e.count == 0 ? e.hidden_streak + 1 : 1u);
        e = {key, frame, count, streak};
      } else if (frame == e.frame && count) {
        e.count = count;  // the same frame's queries disagree: visible wins
        e.hidden_streak = 0;
      }
      return;
    }
    if (!victim || e.frame < victim->frame) victim = &e;
  }
  *victim = {key, frame, count, count ? 0u : 1u};
}

const HistoryEntry* HistoryFind(uint64_t key) {
  if (!key) return nullptr;
  const uint32_t h = uint32_t(key >> 40) & (kHistorySize - 1);
  for (uint32_t i = 0; i < kHistoryProbe; ++i) {
    const HistoryEntry& e = g_history[(h + i) & (kHistorySize - 1)];
    if (e.key == key) return &e;
  }
  return nullptr;
}

// Queries answered before their host result was back: their results are taken into the history when they come.
struct Late {
  uint64_t seq, frame;
};
constexpr uint32_t kLateSize = 4096;
Late g_late[kLateSize];
uint32_t g_late_count = 0;
constexpr uint64_t kLateKeepFrames = 8;

std::atomic<uint64_t> st_hist_weak{0};  // primitives hidden in the history, but too old or not hidden often enough
std::atomic<uint64_t> st_hist_visible{0}, st_hist_hidden{0}, st_harvested{0}, st_keyed{0}, st_late_dropped{0}, st_members{0};

bool MembersValid(uint64_t members, uint32_t count) {
  return count && g_member_pos - members <= uint64_t(kMemberRing - count);
}

// The record's result (issued in 'frame', 'count' guest samples) for each of its primitives.
void StoreMembers(const Record& r, uint64_t frame, uint32_t count) {
  const uint64_t members = r.members.load(std::memory_order_relaxed);
  const uint32_t n = r.member_count.load(std::memory_order_relaxed);
  if (!MembersValid(members, n)) return;
  for (uint32_t i = 0; i < n; ++i) HistoryStore(g_member_keys[(members + i) & (kMemberRing - 1)], frame, count);
}

enum class Recent { kHidden, kVisible, kNoKey, kNoEntry, kOld };
// What the history says of the record's primitives, for its issue frame; 'age' the oldest answer used (frames). Hidden
// needs every primitive hidden in its last 'streak' results, the newest at most 'hidden_limit' frames old; a primitive
// hidden but not enough (or too long ago) gives kOld, so the query answers visible.
Recent LookupMembers(const Record& r, uint64_t frame, uint64_t limit, uint64_t& age) {
  static const uint64_t hidden_limit =
      std::min<uint64_t>(uint64_t(std::max(REXCVAR_GET(dh_occlusion_history_hidden_frames), 1)), limit);
  static const uint32_t streak = uint32_t(std::max(REXCVAR_GET(dh_occlusion_history_hidden_streak), 1));
  const uint64_t members = r.members.load(std::memory_order_relaxed);
  const uint32_t n = r.member_count.load(std::memory_order_relaxed);
  if (!MembersValid(members, n)) return Recent::kNoKey;
  age = 0;
  Recent miss = Recent::kHidden;
  for (uint32_t i = 0; i < n; ++i) {
    const HistoryEntry* e = HistoryFind(g_member_keys[(members + i) & (kMemberRing - 1)]);
    if (!e) {
      miss = Recent::kNoEntry;
      continue;
    }
    if (e->frame > frame || frame - e->frame > limit) {
      if (miss == Recent::kHidden) miss = Recent::kOld;
      age = std::max(age, e->frame <= frame ? frame - e->frame : uint64_t(0));
      continue;
    }
    if (e->count) return Recent::kVisible;  // one primitive seen: the group is visible either way
    if (frame - e->frame > hidden_limit || e->hidden_streak < streak) {
      if (miss == Recent::kHidden) miss = Recent::kOld;
      st_hist_weak.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    age = std::max(age, frame - e->frame);
  }
  return miss;
}
// Why a late query found no answer in the history: no key, no entry for its key, or one older than the limit (and the
// sum of those ages in frames).
std::atomic<uint64_t> st_hist_refused{0};  // hidden in the history, but visible last frame: answered visible
std::atomic<uint64_t> st_miss_nokey{0}, st_miss_noentry{0}, st_miss_old{0}, st_miss_old_frames{0}, st_hist_age{0};

void CloseOpenLocked(bool broken) {
  if (!g_rs.open_seq) return;
  Record& r = g_records[g_rs.open_seq & (kRecords - 1)];
  if (r.seq.load(std::memory_order_relaxed) == g_rs.open_seq) {
    r.first.store(g_rs.open_first, std::memory_order_relaxed);
    r.count.store(g_rs.open_count, std::memory_order_relaxed);
    r.state.store(broken || g_rs.open_broken ? kBroken : kClosed, std::memory_order_release);
  }
  g_rs.open_seq = 0;
}

void BeginLocked(uint64_t seq) {
  CloseOpenLocked(true);  // a BEGIN without END before it: that one is not answered from the GPU
  if (!g_rs.pool && !g_rs.pool_failed) {
    plume::RenderDevice* device = State().device.get();
    g_rs.pool = device ? device->createOcclusionQueryPool(kPoolSize) : nullptr;
    g_rs.pool_failed = !g_rs.pool;
    g_pool.store(g_rs.pool.get(), std::memory_order_release);
    REXLOG_INFO("[dh-occlusion] host occlusion query pool: {}", g_rs.pool ? "created" : "NOT available (all visible)");
  }
  g_rs.open_seq = seq;
  g_rs.open_first = g_rs.next;
  g_rs.open_count = 0;
  g_rs.open_broken = !g_rs.pool;
}

void EndLocked(uint64_t seq) {
  if (g_rs.open_seq == seq) CloseOpenLocked(false);
}

void CarriedOcclusion(uint64_t end_seq, uint64_t begin_seq) {
  if (end_seq) EndLocked(end_seq);
  if (begin_seq) BeginLocked(begin_seq);
}

uint32_t BeforeDraw(plume::RenderCommandList* cmd) {
  if (!g_rs.open_seq || !g_rs.pool || g_rs.open_broken) return UINT32_MAX;
  uint32_t index = g_rs.next;
  if (index >= kPoolSize) {
    index = 0;
    if (g_rs.open_count)  // the issue's queries must be contiguous
      g_rs.open_broken = true;
    else
      g_rs.open_first = 0;
    if (g_rs.open_broken) return UINT32_MAX;
  }
  g_rs.next = index + 1;
  g_rs.pool->resetOcclusion(index, 1);
  cmd->beginOcclusionQuery(g_rs.pool.get(), index);
  ++g_rs.open_count;
  st_draws.fetch_add(1, std::memory_order_relaxed);
  return index;
}

void AfterDraw(plume::RenderCommandList* cmd, uint32_t index) { cmd->endOcclusionQuery(g_rs.pool.get(), index); }

bool Enabled() {
  static const bool on = [] {
    const bool e = REXCVAR_GET(dh_occlusion_queries);
    REXLOG_INFO("[dh-occlusion] real occlusion queries: {} (wait {} us)", e ? "on" : "off (all visible)",
                REXCVAR_GET(dh_occlusion_wait_us));
    return e;
  }();
  return on;
}

void Install() {
  static const bool installed = [] {
    g_occlusion_before_draw = BeforeDraw;
    g_occlusion_after_draw = AfterDraw;
    render_thread::SetCarriedOp(CarriedOcclusion);
    return true;
  }();
  (void)installed;
}

// The guest count for 'host' samples: the host renders below the guest's 720p.
uint32_t GuestCount(uint64_t host) {
  if (!host) return 0;
  static const int32_t fixed = REXCVAR_GET(dh_occlusion_scale);
  double k = double(fixed);
  if (fixed <= 0) {
    const double f = double(ResolutionScale().Factor());  // host size / guest size
    k = f > 0.0 ? 1.0 / (f * f) : 1.0;
  }
  // At most 2^20 a query: 200 of them still fit the records' 32-bit running sum.
  return uint32_t(std::clamp(std::round(double(host) * k), 1.0, double(1u << 20)));
}

// 0: the host result, else not ready yet ('ready' false) or not answerable from the GPU (count kVisible, ready true).
// 'counted' false (a late result taken into the history): the answer stats are left alone.
uint32_t TryAnswer(uint64_t seq, bool& ready, bool counted = true) {
  ready = true;
  Record& r = g_records[seq & (kRecords - 1)];
  if (!seq || r.seq.load(std::memory_order_acquire) != seq) {
    if (counted) st_unknown.fetch_add(1, std::memory_order_relaxed);
    return kVisible;
  }
  const uint32_t state = r.state.load(std::memory_order_acquire);
  if (state == kBroken) {
    if (counted) st_broken.fetch_add(1, std::memory_order_relaxed);
    return kVisible;
  }
  if (state != kClosed) {
    ready = false;
    return kVisible;
  }
  const uint32_t first = r.first.load(std::memory_order_relaxed), count = r.count.load(std::memory_order_relaxed);
  plume::RenderQueryPool* pool = g_pool.load(std::memory_order_acquire);
  if (count == 0 || !pool) {
    if (counted) st_no_draws.fetch_add(1, std::memory_order_relaxed);
    return kVisible;
  }
  uint64_t samples[64];
  uint64_t sum = 0;
  for (uint32_t done = 0; done < count;) {
    const uint32_t n = std::min<uint32_t>(count - done, 64);
    if (!pool->occlusionResults(first + done, n, samples)) {
      ready = false;
      return kVisible;
    }
    for (uint32_t i = 0; i < n; ++i) sum += samples[i];
    done += n;
  }
  if (r.seq.load(std::memory_order_acquire) != seq) {  // reused while reading (a ring wrap): not trusted
    if (counted) st_unknown.fetch_add(1, std::memory_order_relaxed);
    return kVisible;
  }
  if (sum) {
    g_zero_answers.store(0, std::memory_order_relaxed);
    g_zero_reads.store(0, std::memory_order_relaxed);
    if (g_counter_dead.exchange(false, std::memory_order_relaxed))
      REXLOG_INFO("[dh-occlusion] a query has samples again: real answers");
  } else if (g_zero_answers.fetch_add(1, std::memory_order_relaxed) + 1 >= kDeadAnswers &&
             g_zero_reads.load(std::memory_order_relaxed) >= kDeadReads &&
             !g_counter_dead.exchange(true, std::memory_order_relaxed)) {
    REXLOG_WARN("[dh-occlusion] {} answers in a row over {} batch reads had 0 samples: the GPU's sample counter is "
                "taken as not counting (Ryujinx does this); every occlusion query answers visible until one has samples",
                g_zero_answers.load(), g_zero_reads.load());
  }
  if (g_counter_dead.load(std::memory_order_relaxed)) {
    if (counted) st_dead.fetch_add(1, std::memory_order_relaxed);
    return kVisible;
  }
  if (!counted) return GuestCount(sum);
  (sum ? st_real_visible : st_real_hidden).fetch_add(1, std::memory_order_relaxed);
#if defined(PLUME_DEKO3D)
  uint64_t w[4] = {};
  plume::Deko3DOcclusionSnapshot(pool, first, w);
  // Diagnostic (2026-11-09): the first answers' raw SamplesPassed reports (begin value, begin timestamp, end value,
  // end timestamp) of their first host query.
  static std::atomic<uint32_t> raw_logged{0};
  if (raw_logged.fetch_add(1, std::memory_order_relaxed) < 8) {
    REXLOG_INFO("[dh-occlusion] raw: query seq {} host {} (+{} more): begin {} @{} end {} @{} -> {} samples, guest {}",
                seq, first, count - 1, w[0], w[1], w[2], w[3], sum, GuestCount(sum));
  }
#endif
  return GuestCount(sum);
}

// The late queries whose host results came back since: into the history. Old ones (or reused records) are dropped.
void HarvestLate(uint64_t now) {
  uint32_t kept = 0;
  for (uint32_t i = 0; i < g_late_count; ++i) {
    const Late l = g_late[i];
    const Record& r = g_records[l.seq & (kRecords - 1)];
    if (r.seq.load(std::memory_order_acquire) != l.seq || now - l.frame > kLateKeepFrames) {
      st_late_dropped.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    bool ready = false;
    const uint32_t a = TryAnswer(l.seq, ready, false);
    if (ready) {
      StoreMembers(r, l.frame, a);
      st_harvested.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    g_late[kept++] = l;
  }
  g_late_count = kept;
}

void Report() {
  static std::atomic<int64_t> last{0};
  const int64_t now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  int64_t was = last.load(std::memory_order_relaxed);
  if (was == 0) {
    last.compare_exchange_strong(was, now);
    return;
  }
  if (now - was < 10 || !last.compare_exchange_strong(was, now)) return;
  const auto take = [](std::atomic<uint64_t>& a) { return a.exchange(0, std::memory_order_relaxed); };
  const uint64_t waits = take(st_waits), wait_us = take(st_wait_us);
  const uint64_t hits = st_hist_visible.load() + st_hist_hidden.load(), old = take(st_miss_old);
  REXLOG_INFO("[dh-occlusion] {} s: {} queries begun ({} while recording a command buffer), {} ended, {} draws measured; {} batch "
              "reads (age 0/1/2/3+: {}/{}/{}/{}); answers: {} visible, {} hidden (culled), late: {} from a recent frame "
              "visible, {} from a recent frame hidden (culled), {} hidden there but seen last frame -> visible, {} without one "
              "(of their primitives {} hidden there only once or too long ago) "
              "-> visible; {} without a draw, {} broken, {} "
              "unknown, {} visible (counter not counting); {} queries keyed ({} primitives), {} late results taken into the history, {} "
              "dropped; history hits {:.1f} frames old on average, misses: {} not batched, {} a primitive never seen, {} "
              "too old ({:.1f} frames on average); {} waits ({} ms in all), {} forced submits; {} box draws skipped (the rasterizer's)",
              now - was, take(st_begun), take(st_recording), take(st_ended), take(st_draws), take(st_reads), take(st_age[0]),
              take(st_age[1]), take(st_age[2]), take(st_age[3]), take(st_real_visible), take(st_real_hidden),
              take(st_hist_visible), take(st_hist_hidden), take(st_hist_refused), take(st_not_ready), take(st_hist_weak), take(st_no_draws), take(st_broken),
              take(st_unknown), take(st_dead), take(st_keyed), take(st_members), take(st_harvested), take(st_late_dropped),
              hits ? double(take(st_hist_age)) / double(hits) : (take(st_hist_age), 0.0), take(st_miss_nokey),
              take(st_miss_noentry), old, old ? double(take(st_miss_old_frames)) / double(old) : (take(st_miss_old_frames), 0.0), waits,
              wait_us / 1000, take(st_kicks), take(st_skipped_draws));
}

// The report buffer's device (sub_82B085F8: buffer +48).
uint32_t DeviceOf(const uint8_t* base, uint32_t manager) {
  const uint32_t buf = manager ? LoadBE(base, manager + 812) : 0;
  return buf ? LoadBE(base, buf + 48) : 0;
}

// The fallback (dh_occlusion_queries off, or a buffer this does not understand): every query reads kVisible samples
// and the read's wait ends at once (the 2026-11-08 behaviour).
void AnswerAllVisible(uint8_t* base, uint32_t buf, uint32_t count) {
  const uint32_t records = LoadBE(base, buf + 24), tiles = LoadBE(base, buf + 28), stride = LoadBE(base, buf + 32),
                 used = LoadBE(base, buf + 36), base40 = LoadBE(base, buf + 40), base44 = LoadBE(base, buf + 44);
  if (!records || !tiles || tiles > 64 || stride < tiles * 32 || stride > 4096 || !base44 || !base40) return;
  const uint32_t last = std::min(count, records - 1);
  for (uint32_t k = 0; k <= last; ++k)
    for (uint32_t j = 0; j < tiles; ++j) {
      const uint32_t tile = base44 + k * stride + j * 32;
      StoreLE(base, tile + 16, j == 0 ? k * kVisible : 0);
      StoreLE(base, tile + 20, 0);
      StoreLE(base, tile + 28, 0);
    }
  if (used && used <= records) {  // the slot the wait reads (base +40), when it is not one written above
    const uint32_t check = base40 + used * stride - 32;
    if (LoadBE(base, check + 16) == kMarker) StoreLE(base, check + 16, 0);
    if (LoadBE(base, check + 28) == kMarker) StoreLE(base, check + 28, 0);
  }
}

}  // namespace

// UE3's occlusion query begin [sub_82777BA0(query)]: after the original, query +8 = manager, +12 = index.
// dh_hooks.cpp's DrawVertices hook: whether this draw is an occlusion box the rasterizer answers for (not drawn).
extern "C" bool DhOcclusionSkipDraw() {
  if (!(t_skip_box_draws && t_batch_loop)) return false;
  st_skipped_draws.fetch_add(1, std::memory_order_relaxed);
  return true;
}

REX_EXTERN(__imp__sub_82777BA0);
REX_HOOK_RAW(sub_82777BA0) {
  const uint32_t query = ctx.r3.u32;
  t_skip_box_draws = false;
  __imp__sub_82777BA0(ctx, base);
  if (!query || !Enabled() || !banjo::renut_gpu::Active()) return;
  const uint32_t manager = LoadBE(base, query + 8), k = LoadBE(base, query + 12);
  const uint32_t buf = manager ? LoadBE(base, manager + 812) : 0;
  ManagerSlot* slot = buf && k < kBatch ? FindManager(buf, true) : nullptr;
  if (!slot) return;
  Install();
  const uint64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed) + 1;
  Record& r = g_records[seq & (kRecords - 1)];
  r.count.store(0, std::memory_order_relaxed);
  r.member_count.store(0, std::memory_order_relaxed);
  r.visible_last.store(false, std::memory_order_relaxed);
  r.boxes.store(0, std::memory_order_relaxed);
  r.frame.store(g_sa2_game_frames.load(std::memory_order_relaxed), std::memory_order_relaxed);
  st_begun.fetch_add(1, std::memory_order_relaxed);
  const uint32_t device = DeviceOf(base, manager);
  if (!device || IsRecordingCommandBuffer(device)) {
    // Drawn into a command buffer the game replays later (or no device): the host draws are not in this order.
    r.state.store(kBroken, std::memory_order_relaxed);
    r.seq.store(seq, std::memory_order_release);
    slot->seq[k].store(seq, std::memory_order_release);
    st_recording.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  r.state.store(kOpen, std::memory_order_relaxed);
  r.seq.store(seq, std::memory_order_release);
  slot->seq[k].store(seq, std::memory_order_release);
  FlushDeferredVertices(device);  // a Begin*Vertices draw before the BEGIN belongs before it
  render_thread::CarryBegin(seq);
  // The history's keys: the batch's primitives (the sub_8262CEC0 hook).
  if (t_batch_loop && t_batch_key_count) {
    uint32_t i = t_batch_cursor < t_batch_key_count && t_batch_keys[t_batch_cursor].query == query ? t_batch_cursor
                                                                                                   : UINT32_MAX;
    for (uint32_t j = 0; i == UINT32_MAX && j < t_batch_key_count; ++j)
      if (t_batch_keys[j].query == query) i = j;
    if (i != UINT32_MAX) {
      t_batch_cursor = i + 1;
      r.members.store(t_batch_keys[i].members, std::memory_order_relaxed);
      r.member_count.store(t_batch_keys[i].count, std::memory_order_relaxed);
      r.visible_last.store(t_batch_keys[i].visible_last, std::memory_order_relaxed);
      r.boxes.store(t_batch_keys[i].boxes, std::memory_order_relaxed);
      r.box_view.store(t_batch_keys[i].view, std::memory_order_relaxed);
      static const bool skip = REXCVAR_GET(dh_raster_skip_gpu_boxes);
      t_skip_box_draws = skip && DhRasterMode() == 2 && t_batch_keys[i].boxes != 0;
      st_keyed.fetch_add(1, std::memory_order_relaxed);
      st_members.fetch_add(t_batch_keys[i].count, std::memory_order_relaxed);
    }
  }
}

// UE3's occlusion pass [sub_8262D230(r3)]: the view is r3 + 96 (its batchers' translation, above).
REX_EXTERN(__imp__sub_8262D230);
REX_HOOK_RAW(sub_8262D230) {
  const uint32_t was = t_view;
  t_view = ctx.r3.u32 ? ctx.r3.u32 + 96 : 0;
  __imp__sub_8262D230(ctx, base);
  t_view = was;
}

// UE3's batched occlusion queries [sub_8262CEC0(batcher)] (FOcclusionQueryBatcher's flush: the primitives occluded last
// frame, tested in groups with one instanced box draw a group): batcher +0 the queries (4 bytes each), +4 how many,
// +12 / +16 the primitives' bounds (24 bytes each: origin, extent; big-endian floats) and their count, +28 primitives a
// query. Query k tests primitives [k * +28, min((k + 1) * +28, +16)): their keys (PrimitiveKey) go to query k.
REX_EXTERN(__imp__sub_8262CEC0);
REX_HOOK_RAW(sub_8262CEC0) {
  const uint32_t batcher = ctx.r3.u32;
  t_batch_key_count = 0;
  t_batch_cursor = 0;
  if (batcher && Enabled() && banjo::renut_gpu::Active() &&
      (REXCVAR_GET(dh_occlusion_history_frames) > 0 || DhRasterMode())) {
    const uint32_t queries = LoadBE(base, batcher + 0), batches = LoadBE(base, batcher + 4),
                   bounds = LoadBE(base, batcher + 12), primitives = LoadBE(base, batcher + 16),
                   per = LoadBE(base, batcher + 28);
    float shift[3] = {0.0f, 0.0f, 0.0f};
    const bool own_view = t_view && (batcher == t_view + 3928 || batcher == t_view + 3968);
    if (own_view)
      for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t bits = LoadBE(base, t_view + 544 + i * 4);
        std::memcpy(&shift[i], &bits, 4);
        if (!std::isfinite(shift[i])) shift[i] = 0.0f;
      }
    {
      // Once per batcher (the first few): which one is which.
      static uint32_t seen[4] = {};
      for (uint32_t& b : seen) {
        if (b == batcher) break;
        if (b == 0) {
          b = batcher;
          REXLOG_INFO("[dh-occlusion] batcher {:08X} (view +{}): {} primitives a query ({})", batcher,
                      t_view ? int64_t(batcher) - int64_t(t_view) : int64_t(-1), per,
                      per == 1 ? "one a query: seen last frame, no hidden answer from the history"
                               : "grouped: occluded last frame");
          break;
        }
      }
    }
    if (queries && bounds && per && per <= 4096 && primitives <= (1u << 20)) {
      const uint32_t n = std::min(batches, kMaxBatchKeys);
      for (uint32_t k = 0; k < n; ++k) {
        const uint64_t first = uint64_t(k) * per;
        if (first >= primitives) break;
        const uint32_t count = uint32_t(std::min<uint64_t>(primitives - first, per));
        if (count > kMemberRing / 4) break;
        const uint64_t members = g_member_pos;
        for (uint32_t i = 0; i < count; ++i)
          g_member_keys[g_member_pos++ & (kMemberRing - 1)] = PrimitiveKey(base + bounds + (uint32_t(first) + i) * 24, shift);
        // 2026-11-10 (dh_raster_mode): the query's primitives' world boxes, tested by the rasterizer at its read.
        uint64_t boxes = 0;
        if (own_view && DhRasterMode() && count <= kBoxRing / 4) {
          boxes = g_box_pos;
          for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* b = base + bounds + (uint32_t(first) + i) * 24;
            float v[6];
            for (uint32_t c = 0; c < 6; ++c) {
              const uint32_t bits = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(b + c * 4));
              std::memcpy(&v[c], &bits, 4);
            }
            float* box = g_boxes[g_box_pos++ & (kBoxRing - 1)];
            for (uint32_t c = 0; c < 3; ++c) {
              box[c] = v[c] - shift[c] - v[3 + c];
              box[3 + c] = v[c] - shift[c] + v[3 + c];
            }
          }
        }
        t_batch_keys[t_batch_key_count++] = {LoadBE(base, queries + k * 4), members, count, per == 1, boxes, t_view};
      }
    }
  }
  t_batch_loop = true;
  __imp__sub_8262CEC0(ctx, base);
  t_batch_loop = false;
  t_skip_box_draws = false;
  t_batch_key_count = 0;
}

// The report write [sub_82B085F8(buffer)] (XDK D3D: the GPU commands for the buffer's next record). Called by begin for
// a batch's first query (record 0) and at every END: by sub_82777C18 and inline by UE3's box loop sub_8262CEC0.
REX_EXTERN(__imp__sub_82B085F8);
REX_HOOK_RAW(sub_82B085F8) {
  const uint32_t buf = ctx.r3.u32;
  if (buf && Enabled() && banjo::renut_gpu::Active()) {
    const uint32_t written = LoadBE(base, buf + 36);  // records before this one: this is the END of query written - 1
    const ManagerSlot* slot = written && written <= kBatch ? FindManager(buf, false) : nullptr;
    const uint64_t seq = slot ? slot->seq[written - 1].load(std::memory_order_acquire) : 0;
    const Record& r = g_records[seq & (kRecords - 1)];
    if (seq && r.seq.load(std::memory_order_acquire) == seq && r.state.load(std::memory_order_acquire) == kOpen) {
      const uint32_t device = LoadBE(base, buf + 48);
      if (device) FlushDeferredVertices(device);  // the query's own Begin*Vertices box belongs before the END
      render_thread::CarryEnd(seq);
      st_ended.fetch_add(1, std::memory_order_relaxed);
    }
  }
  __imp__sub_82B085F8(ctx, base);
}

// UE3's occlusion result read [sub_827777B8(manager)] (the render thread, after the batch's fence).
REX_EXTERN(__imp__sub_827777B8);
REX_HOOK_RAW(sub_827777B8) {
  const uint32_t manager = ctx.r3.u32;
  if (banjo::renut_gpu::Active() && manager) {
    const uint32_t buf = LoadBE(base, manager + 812), count = std::min(LoadBE(base, manager + 800), kBatch);
    if (buf) {
      static bool logged = false;
      if (!logged) {
        logged = true;
        REXLOG_INFO("[dh-occlusion] report buffer {:08X}: {} records of {} bytes, {} tiles, {} used, bases {:08X} / "
                    "{:08X}; {} queries in the first batch read",
                    buf, LoadBE(base, buf + 24), LoadBE(base, buf + 32), LoadBE(base, buf + 28),
                    LoadBE(base, buf + 36), LoadBE(base, buf + 40), LoadBE(base, buf + 44), count);
      }
      // Every slot first as before ("visible" counts, the wait ended); then the real counts where there are some.
      AnswerAllVisible(base, buf, count);
      const ManagerSlot* slot = FindManager(buf, false);
      const uint32_t records = LoadBE(base, buf + 24), tiles = LoadBE(base, buf + 28),
                     stride = LoadBE(base, buf + 32), base44 = LoadBE(base, buf + 44);
      const bool ab_all_visible = g_dh_ab_occlusion_off && g_dh_ab_occlusion_off();  // A/B: as dh_occlusion_queries off
      if (Enabled() && !ab_all_visible && slot && count && count < records && tiles && tiles <= 64 &&
          stride >= tiles * 32 && base44) {
        st_reads.fetch_add(1, std::memory_order_relaxed);
        g_zero_reads.fetch_add(1, std::memory_order_relaxed);
        std::array<uint32_t, kBatch> answer{};
        std::array<bool, kBatch> done{};
        uint32_t pending = count;
        uint64_t oldest = UINT64_MAX;
        for (uint32_t k = 0; k < count; ++k) {
          const uint64_t seq = slot->seq[k].load(std::memory_order_acquire);
          const Record& r = g_records[seq & (kRecords - 1)];
          if (seq && r.seq.load(std::memory_order_relaxed) == seq)
            oldest = std::min(oldest, r.frame.load(std::memory_order_relaxed));
        }
        if (oldest != UINT64_MAX) {
          const uint64_t age = g_sa2_game_frames.load(std::memory_order_relaxed) - oldest;
          st_age[std::min<uint64_t>(age, 3)].fetch_add(1, std::memory_order_relaxed);
        }
        static const int32_t wait_us = REXCVAR_GET(dh_occlusion_wait_us);
        static const uint64_t history_frames = uint64_t(std::max(REXCVAR_GET(dh_occlusion_history_frames), 0));
        const uint64_t now_frame = g_sa2_game_frames.load(std::memory_order_relaxed);
        if (history_frames) HarvestLate(now_frame);
        const auto start = std::chrono::steady_clock::now();
        bool kicked = false, waited = false;
        for (;;) {
          for (uint32_t k = 0; k < count; ++k) {
            if (done[k]) continue;
            bool ready = false;
            const uint64_t seq = slot->seq[k].load(std::memory_order_acquire);
            const uint32_t a = TryAnswer(seq, ready);
            if (ready) {
              answer[k] = a;
              done[k] = true;
              --pending;
              const Record& r = g_records[seq & (kRecords - 1)];
              if (history_frames && seq && r.seq.load(std::memory_order_acquire) == seq)
                StoreMembers(r, r.frame.load(std::memory_order_relaxed), a);
            }
          }
          if (!pending || g_counter_dead.load(std::memory_order_relaxed)) break;  // dead: no waiting
          if (wait_us <= 0) break;  // no waiting: the late ones take a recent answer below
          if (!kicked) {
            // The batch's draws may still sit in the render thread's open command list: to the GPU now.
            kicked = true;
            st_kicks.fetch_add(1, std::memory_order_relaxed);
            render_thread::FlushCarried();
            render_thread::Queue([] {
              FlushVertexFillsLocked();
              FlushTextureFillsLocked();
              Video::KickLocked(true);
            });
          }
          if (std::chrono::steady_clock::now() - start > std::chrono::microseconds(wait_us)) break;
          waited = true;
          std::this_thread::yield();
        }
        if (waited && pending == count) {
          if (g_late_reads.fetch_add(1, std::memory_order_relaxed) + 1 == kLateReads &&
              !g_counter_dead.exchange(true, std::memory_order_relaxed))
            REXLOG_WARN("[dh-occlusion] {} batch reads in a row had no result after waiting {} us: the GPU's results "
                        "are taken as not coming back in time (Ryujinx does this); every occlusion query answers visible "
                        "without waiting until one has samples",
                        kLateReads, wait_us);
        } else if (pending < count) {
          g_late_reads.store(0, std::memory_order_relaxed);
        }
        if (waited) {
          st_waits.fetch_add(1, std::memory_order_relaxed);
          st_wait_us.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now() - start).count()),
                               std::memory_order_relaxed);
        }
        // 2026-11-10 (dh_raster_mode): the rasterizer's answers. Compare: against the GPU counts that came back. Answer:
        // every query it has one for takes it (the GPU count and the history only where it has none).
        if (DhRasterMode()) {
          for (uint32_t k = 0; k < count; ++k) {
            const uint64_t seq = slot->seq[k].load(std::memory_order_acquire);
            const Record& r = g_records[seq & (kRecords - 1)];
            if (!seq || r.seq.load(std::memory_order_acquire) != seq) continue;
            // The rasterizer's answer for the query: hidden when every box is hidden (or off screen), visible when one
            // is visible or reaches the near plane, none without a buffer of the query's frame (or boxes overwritten).
            const uint64_t boxes = r.boxes.load(std::memory_order_relaxed);
            const uint32_t n = r.member_count.load(std::memory_order_relaxed);
            if (!boxes || !n || g_box_pos - boxes > uint64_t(kBoxRing - n)) continue;
            const uint32_t view = r.box_view.load(std::memory_order_relaxed);
            const uint64_t frame = r.frame.load(std::memory_order_relaxed);
            static std::vector<float> flat;
            flat.resize(size_t(n) * 6);
            for (uint32_t i = 0; i < n; ++i)
              std::memcpy(&flat[size_t(i) * 6], g_boxes[(boxes + i) & (kBoxRing - 1)], sizeof(float) * 6);
            const uint8_t cpu = DhRasterTestBoxes(view, frame, flat.data(), n);
            if (!cpu) continue;
            // compared only with a real GPU count (a query whose boxes were not drawn reads visible from the GPU side)
            if (done[k] && r.count.load(std::memory_order_relaxed) && r.state.load(std::memory_order_relaxed) == kClosed)
              DhRasterCompare(cpu, answer[k] != 0);
            if (DhRasterMode() == 2) {
              answer[k] = cpu == 2 ? 0 : kVisible;
              done[k] = true;
              DhRasterAnswered();
            }
          }
        }
        // Cumulative records: record 0 = 0, record k + 1 = the counts of queries 0..k (tile 0's word +16; the
        // others 0, as AnswerAllVisible left them). The late ones take the answer the same boxes got at most
        // history_frames frames earlier, else visible; their results go into the history when they come back
        // (HarvestLate).
        const bool dead = g_counter_dead.load(std::memory_order_relaxed);
        const bool ab_no_history = g_dh_ab_history_off && g_dh_ab_history_off();  // A/B: late answers visible
        uint32_t sum = 0;
        for (uint32_t k = 0; k < count; ++k) {
          if (!done[k]) {
            answer[k] = kVisible;
            const uint64_t seq = slot->seq[k].load(std::memory_order_acquire);
            const Record& r = g_records[seq & (kRecords - 1)];
            const bool own = history_frames && seq && r.seq.load(std::memory_order_acquire) == seq;
            const uint64_t frame = r.frame.load(std::memory_order_relaxed);
            uint64_t age = 0;
            const Recent recent =
                own && !dead && !ab_no_history ? LookupMembers(r, frame, history_frames, age) : Recent::kNoKey;
            // 2026-11-09 (console: Emily's head flickered with the history, never without it): a primitive UE3 saw
            // last frame (the one-a-query batcher) stays visible until a real result says otherwise; only a primitive
            // already occluded keeps its hidden answer from the history.
            if (recent == Recent::kHidden && r.visible_last.load(std::memory_order_relaxed)) {
              answer[k] = kVisible;
              st_hist_refused.fetch_add(1, std::memory_order_relaxed);
            } else if (recent == Recent::kHidden || recent == Recent::kVisible) {
              answer[k] = recent == Recent::kHidden ? 0 : kVisible;
              (recent == Recent::kHidden ? st_hist_hidden : st_hist_visible).fetch_add(1, std::memory_order_relaxed);
              st_hist_age.fetch_add(age, std::memory_order_relaxed);
            } else {
              st_not_ready.fetch_add(1, std::memory_order_relaxed);
              if (recent == Recent::kNoKey) {
                st_miss_nokey.fetch_add(1, std::memory_order_relaxed);
              } else if (recent == Recent::kNoEntry) {
                st_miss_noentry.fetch_add(1, std::memory_order_relaxed);
              } else {
                st_miss_old.fetch_add(1, std::memory_order_relaxed);
                st_miss_old_frames.fetch_add(age, std::memory_order_relaxed);
              }
            }
            if (own && !dead && r.member_count.load(std::memory_order_relaxed) && g_late_count < kLateSize)
              g_late[g_late_count++] = {seq, frame};
          }
          sum += answer[k];
          StoreLE(base, base44 + (k + 1) * stride + 16, sum);
        }
      }
      Report();
    }
  }
  // 2026-11-11: the original locks the report buffer, which waits for its fence (see t_dh_fence_nowait).
  static const bool nowait = REXCVAR_GET(dh_occlusion_fence_nowait);
  t_dh_fence_nowait = nowait;
  __imp__sub_827777B8(ctx, base);
  t_dh_fence_nowait = false;
}
