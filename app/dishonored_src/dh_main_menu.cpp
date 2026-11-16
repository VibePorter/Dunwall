// Dishonored (2026-11-11) - menu entries the Switch has no use for: the main menu's DLC store button and Options -> System ->
// Storage Device (below).
//
// No "Purchase Downloadable Content" button on the main menu (user request: there is no
// store behind it on the Switch).
//
// The main menu is the Scaleform movie of Dishonored_MainMenu.xxx (decompressed: GFX at 0x169AC7). Its class
// MainMenuButtonBar shows the extra button (_DLCButton_mc, X: t_DownloadableContent_XBOX, OnDLCClicked) and takes the
// X button only while its _isDLC is true (SetDLCButton, InitInputs, XPressed, ...). _isDLC is written in two places
// only, the class MainMenu's Open(isDLC) and UpdateMainMenu(isDLC), both
//     Push r1, '_menu_mc'; GetMember; Push '_isDLC', r2; SetMember      96 04 00 04 01 08 0F 4E 96 04 00 08 1C 04 02 4F
// (r2 = the game's isDLC argument). Both become  Push '_isDLC', false  (the register value 04 02 -> the boolean 05 00,
// same length): the menu never has the button, X does nothing there. The DLC itself (installed content, its missions)
// is untouched.
//
// The package is LZX-compressed, so the bytes are changed as it is read: every block UE3's appDecompressMemory
// (sub_822E44F8, dh_hot.cpp) produces is searched for the sequence. Both sites are 0xDDD / 0xE36 into the same 128 KB
// block (chunk 2, block 4), so a sequence split across blocks is not an issue here.
//
// dh_hide_dlc_store (default on): off = the game's button.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_BOOL(dh_hide_dlc_store, true, "Dishonored",
                    "No \"Purchase Downloadable Content\" button on the main menu (the DLC itself is kept)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// --- Dunwall City Trials: no Leaderboards (2026-11-13, user request) ---------------------------------------------------
//
// There is no Xbox Live behind them (the SDK answers every stats read with an empty board). Two menus list them, both
// built in ActionScript as an array of {txt, callback, lockState} entries, one `menu.push({...})` statement each
// (41 bytes), then handed to _menu_mc.SetMenu:
//   - the DLC's main menu: UI_ChallengesMenu_DLC05.ChallengesMenu in L_DLC05_MainMenu_P.xxx (Challenges selection,
//     Leaderboards, Gallery, Credits, ...), the statement at 0x536ECD of the decompressed package;
//   - a trial's results screen: UI_Results_DLC05.Results in UI_Results_DLC05_SF.xxx (Replay, Leaderboards, Exit
//     challenge, Exit), at 0x4379D.
//     Push 'txt', r5 (the Leaderboards label), 'callback', r1, 'OnLeaderboardsClicked'; GetMember;
//     Push 'lockState', false, 3; InitObject; Push 1, r4, 'push'; CallMethod; Pop
// The statement's first 5 bytes become Jump +36 (to the next statement): the entry is never added. Each entry carries
// its own callback, so the others work as before. Both statements lie inside one 128 KB decompression block. The two
// differ only in their constant pool indices. dh_hide_leaderboards (default on).
REXCVAR_DEFINE_BOOL(dh_hide_leaderboards, true, "Dishonored",
                    "No Leaderboards entry in the Dunwall City Trials menus (there is no Xbox Live behind them)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
constexpr uint8_t kSetIsDlc[] = {0x96, 0x04, 0x00, 0x04, 0x01, 0x08, 0x0F, 0x4E,
                                 0x96, 0x04, 0x00, 0x08, 0x1C, 0x04, 0x02, 0x4F};
constexpr size_t kValueAt = 13;  // the pushed value: 04 02 (register 2) -> 05 00 (false)

constexpr size_t kPushBytes = 41;
// ChallengesMenu (constants txt 0x1B, callback 0x1D, OnLeaderboardsClicked 0x20, lockState 0x21, push 0x1F).
constexpr uint8_t kLeaderboardsMenu[kPushBytes] = {
    0x96, 0x0A, 0x00, 0x08, 0x1B, 0x04, 0x05, 0x08, 0x1D, 0x04, 0x01, 0x08, 0x20, 0x4E, 0x96, 0x09, 0x00, 0x08, 0x21,
    0x05, 0x00, 0x07, 0x03, 0x00, 0x00, 0x00, 0x43, 0x96, 0x09, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08,
    0x1F, 0x52, 0x17};
// Results (txt 0x1D, callback 0x1F, OnLeaderboardsClicked 0x23, lockState 0x21, push 0x22).
constexpr uint8_t kLeaderboardsResults[kPushBytes] = {
    0x96, 0x0A, 0x00, 0x08, 0x1D, 0x04, 0x05, 0x08, 0x1F, 0x04, 0x01, 0x08, 0x23, 0x4E, 0x96, 0x09, 0x00, 0x08, 0x21,
    0x05, 0x00, 0x07, 0x03, 0x00, 0x00, 0x00, 0x43, 0x96, 0x09, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08,
    0x22, 0x52, 0x17};
// ActionJump (0x99, length 2) over the rest of the statement: offset from the end of the jump.
constexpr uint8_t kSkipPush[5] = {0x99, 0x02, 0x00, uint8_t(kPushBytes - 5), 0x00};

// The challenge list (same ChallengesMenu movie) opens a challenge's leaderboard with X and shows "X Leaderboards" in
// its help bar, in both of its states (challList, challList_locked): `help.push({btn: 'X', txt: r6})`, r6 the
// Leaderboards label (0x51C505 and 0x51C5B7, 31 bytes each). Those statements are skipped as above, and XPressed's body
// (0x52AE7C, 126 bytes: close the list, open leaderboardsScreen_mc for the selected challenge) jumps to its end: X does
// nothing there.
constexpr size_t kHelpXBytes = 31;
constexpr uint8_t kHelpXLeaderboards[kHelpXBytes] = {
    0x96, 0x0D, 0x00, 0x08, 0x21, 0x08, 0x26, 0x08, 0x23, 0x04, 0x06, 0x07, 0x02, 0x00, 0x00, 0x00,
    0x43, 0x96, 0x09, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x0D, 0x08, 0x25, 0x52, 0x17};
constexpr uint8_t kSkipHelpX[5] = {0x99, 0x02, 0x00, uint8_t(kHelpXBytes - 5), 0x00};
constexpr size_t kXPressedBytes = 126;
constexpr uint8_t kXPressedLeaderboards[kXPressedBytes] = {
    0x96, 0x0B, 0x00, 0x08, 0x7A, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x02, 0x08, 0x78, 0x4E, 0x96, 0x02, 0x00,
    0x08, 0x79, 0x52, 0x17, 0x96, 0x04, 0x00, 0x04, 0x01, 0x08, 0x32, 0x4E, 0x96, 0x02, 0x00, 0x08, 0x4B, 0x4E,
    0x87, 0x01, 0x00, 0x03, 0x17, 0x96, 0x04, 0x00, 0x04, 0x01, 0x08, 0x47, 0x4E, 0x96, 0x02, 0x00, 0x08, 0x60,
    0x4E, 0x96, 0x02, 0x00, 0x08, 0x5A, 0x4E, 0x87, 0x01, 0x00, 0x04, 0x17, 0x96, 0x0D, 0x00, 0x06, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x08, 0x7D, 0x52, 0x17, 0x96, 0x04, 0x00, 0x04, 0x02, 0x08,
    0x7E, 0x4E, 0x96, 0x04, 0x00, 0x08, 0x11, 0x05, 0x00, 0x4F, 0x96, 0x0F, 0x00, 0x05, 0x01, 0x04, 0x04, 0x04,
    0x03, 0x07, 0x03, 0x00, 0x00, 0x00, 0x04, 0x02, 0x08, 0x7E, 0x4E, 0x96, 0x02, 0x00, 0x08, 0x0E, 0x52, 0x17};
constexpr uint8_t kSkipXPressed[5] = {0x99, 0x02, 0x00, uint8_t(kXPressedBytes - 5), 0x00};

// Every occurrence of 'pattern' in [p, p + size): 'edit' applied to it; returns the count.
template <size_t N, typename Edit>
int PatchAll(uint8_t* p, uint32_t size, const uint8_t (&pattern)[N], Edit edit) {
  if (size < N) return 0;
  uint8_t* const end = p + size - N + 1;
  int patched = 0;
  while (p < end) {
    p = static_cast<uint8_t*>(std::memchr(p, pattern[0], size_t(end - p)));
    if (!p) break;
    if (std::memcmp(p, pattern, N) == 0) {
      edit(p);
      ++patched;
      p += N;
    } else {
      ++p;
    }
  }
  return patched;
}
}  // namespace

// After appDecompressMemory succeeded: dst / size of its output in guest memory.
extern "C" void DhPatchDecompressed(uint8_t* base, uint32_t dst, uint32_t size) {
  static const bool store = REXCVAR_GET(dh_hide_dlc_store);
  static const bool leaderboards = REXCVAR_GET(dh_hide_leaderboards);
  uint8_t* const p = base + dst;
  if (store) {
    const int n = PatchAll(p, size, kSetIsDlc, [](uint8_t* at) {
      at[kValueAt] = 0x05;
      at[kValueAt + 1] = 0x00;
    });
    if (n) REXLOG_INFO("[dh-menu] main menu: DLC store button removed ({} places)", n);
  }
  if (leaderboards) {
    const auto skip = [](uint8_t* at) { std::memcpy(at, kSkipPush, sizeof(kSkipPush)); };
    if (const int n = PatchAll(p, size, kLeaderboardsMenu, skip))
      REXLOG_INFO("[dh-menu] Dunwall City Trials menu: Leaderboards entry removed ({} places)", n);
    if (const int n = PatchAll(p, size, kLeaderboardsResults, skip))
      REXLOG_INFO("[dh-menu] Dunwall City Trials results screen: Leaderboards entry removed ({} places)", n);
    if (const int n = PatchAll(p, size, kHelpXLeaderboards,
                               [](uint8_t* at) { std::memcpy(at, kSkipHelpX, sizeof(kSkipHelpX)); }))
      REXLOG_INFO("[dh-menu] Dunwall City Trials challenge list: \"X Leaderboards\" prompt removed ({} places)", n);
    if (const int n = PatchAll(p, size, kXPressedLeaderboards,
                               [](uint8_t* at) { std::memcpy(at, kSkipXPressed, sizeof(kSkipXPressed)); }))
      REXLOG_INFO("[dh-menu] Dunwall City Trials challenge list: X no longer opens the leaderboards ({} places)", n);
  }
}

// --- Options -> System -> Storage Device (2026-11-11, user request) ---------------------------------------------------
//
// The options movie's categories come from native code, from arrays the menu script fills:
//   FillCategories   sub_829A4818(menu, TArray<category>*)   category 36 bytes: name FString +0, subcategories +12/+16
//                                                             (28 bytes: name +0, settings +12/+16), settings +24/+28
//   one category     sub_829A4AC0(menu, category*, out)       (also RefreshCategory's, sub_829A49A8)
//   one subcategory  sub_829A4D70(menu, subcategory*, out)
//   one setting row  sub_829A4FA8 (dh_control_scheme.cpp)     setting 20 bytes: id +0, flags +16 = m_bGamepadBindingMenu
//       0x80000000, m_bVideoSettings 0x40000000, m_bGammaMenu 0x20000000, m_bDeviceSelectionMenu 0x10000000 (the row
//       "Storage Device" that opens "DeviceSelectionMenu", the Xbox 360's storage device selector), m_bDropList ...
// For each call the settings with m_bDeviceSelectionMenu are moved out of the arrays (the others keep their order),
// a subcategory or category left with nothing by that is left out too (System holds only Storage Device), and after
// the call every array is put back byte for byte: the script's data never changes. dh_hide_storage_device (default on).

#include <rex/hook.h>
#include <rex/ppc.h>

#include <vector>

REXCVAR_DEFINE_BOOL(dh_hide_storage_device, true, "Dishonored",
                    "No Options -> System -> Storage Device (the Xbox 360 storage device selector)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_829A4818);
REX_EXTERN(__imp__sub_829A4AC0);
REX_EXTERN(__imp__sub_829A4D70);

namespace {

constexpr uint32_t kSettingSize = 20, kSettingFlags = 16, kDeviceSelection = 0x10000000u;
constexpr uint32_t kSubSize = 28, kSubSettings = 12;
constexpr uint32_t kCatSize = 36, kCatSubs = 12, kCatSettings = 24;

uint32_t Ld(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void St(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}

bool HideStorage() {
  static const bool on = REXCVAR_GET(dh_hide_storage_device);
  return on;
}

bool Hidden(const uint8_t* base, uint32_t setting) { return (Ld(base, setting + kSettingFlags) & kDeviceSelection) != 0; }

// Settings of an array left after the hidden ones; *hidden = how many were hidden.
uint32_t VisibleSettings(const uint8_t* base, uint32_t array, uint32_t* hidden) {
  const uint32_t data = Ld(base, array);
  const int32_t num = int32_t(Ld(base, array + 4));
  uint32_t visible = 0, gone = 0;
  for (int32_t i = 0; data && i < num; ++i) (Hidden(base, data + uint32_t(i) * kSettingSize) ? gone : visible)++;
  if (hidden) *hidden = gone;
  return visible;
}
// A subcategory the filter leaves empty (it had settings, all hidden).
bool SubGone(const uint8_t* base, uint32_t sub) {
  uint32_t hidden = 0;
  return VisibleSettings(base, sub + kSubSettings, &hidden) == 0 && hidden > 0;
}
bool CatGone(const uint8_t* base, uint32_t cat) {
  uint32_t hidden = 0;
  if (VisibleSettings(base, cat + kCatSettings, &hidden) != 0) return false;
  const uint32_t subs = Ld(base, cat + kCatSubs);
  const int32_t nsubs = int32_t(Ld(base, cat + kCatSubs + 4));
  bool removed_any = hidden > 0;
  for (int32_t i = 0; subs && i < nsubs; ++i) {
    const uint32_t sub = subs + uint32_t(i) * kSubSize;
    if (!SubGone(base, sub)) return false;
    removed_any = true;
  }
  return removed_any;
}

// An array compacted for one call, restored afterwards.
struct Compacted {
  uint32_t array = 0;
  uint32_t num = 0;
  std::vector<uint8_t> bytes;
};

template <typename Keep>
bool Compact(uint8_t* base, uint32_t array, uint32_t size, Keep keep, Compacted& saved) {
  const uint32_t data = Ld(base, array);
  const int32_t num = int32_t(Ld(base, array + 4));
  if (!data || num <= 0) return false;
  std::vector<uint8_t> kept;
  for (int32_t i = 0; i < num; ++i) {
    const uint32_t e = data + uint32_t(i) * size;
    if (keep(e)) kept.insert(kept.end(), base + e, base + e + size);
  }
  if (kept.size() == size_t(num) * size) return false;
  saved.array = array;
  saved.num = uint32_t(num);
  saved.bytes.assign(base + data, base + data + size_t(num) * size);
  std::memcpy(base + data, kept.data(), kept.size());
  St(base, array + 4, uint32_t(kept.size() / size));
  return true;
}
void Restore(uint8_t* base, const Compacted& saved) {
  if (!saved.array) return;
  std::memcpy(base + Ld(base, saved.array), saved.bytes.data(), saved.bytes.size());
  St(base, saved.array + 4, saved.num);
}

}  // namespace

REX_HOOK_RAW(sub_829A4D70) {  // one subcategory
  Compacted settings;
  if (HideStorage())
    Compact(base, ctx.r4.u32 + kSubSettings, kSettingSize, [&](uint32_t s) { return !Hidden(base, s); }, settings);
  __imp__sub_829A4D70(ctx, base);
  Restore(base, settings);
}

REX_HOOK_RAW(sub_829A4AC0) {  // one category
  Compacted settings, subs;
  if (HideStorage()) {
    const uint32_t cat = ctx.r4.u32;
    Compact(base, cat + kCatSettings, kSettingSize, [&](uint32_t s) { return !Hidden(base, s); }, settings);
    Compact(base, cat + kCatSubs, kSubSize, [&](uint32_t sub) { return !SubGone(base, sub); }, subs);
  }
  __imp__sub_829A4AC0(ctx, base);
  Restore(base, subs);
  Restore(base, settings);
}

REX_HOOK_RAW(sub_829A4818) {  // FillCategories
  Compacted cats;
  if (HideStorage() && ctx.r4.u32) {
    if (Compact(base, ctx.r4.u32, kCatSize, [&](uint32_t c) { return !CatGone(base, c); }, cats)) {
      static bool logged = false;
      if (!logged) {
        logged = true;
        REXLOG_INFO("[dh-menu] options: Storage Device left out ({} categories shown of {})",
                    Ld(base, ctx.r4.u32 + 4), cats.num);
      }
    }
  }
  __imp__sub_829A4818(ctx, base);
  Restore(base, cats);
}
