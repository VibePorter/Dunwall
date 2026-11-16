// Dishonored (2026-11-11) - a fifth controller scheme, "Type E": Type A with the A and B buttons' actions swapped
// (the A button sneaks and leaves keyholes, the B button jumps), chosen in Options -> Controls -> Control Scheme like
// the game's own Type A..D (user request).
//
// HOW THE GAME DOES ITS FOUR (from the generated listings; Engine.u / DishonoredGame.u of the title update)
//   - The schemes are four arrays of key bindings on the native Input class, m_PadBindingSet1..4 (config, from
//     [Engine.PlayerInput] of Xbox360-DishonoredInput.ini in Coalesced_*.bin): KeyBind entries of 24 bytes, the
//     command FString at +8. Type A: XboxTypeS_A = "GBA_Jump | DebugMenuEnter", XboxTypeS_B = "GBA_Sneak_Gamepad |
//     Dis_ExitKeyhole".
//   - The profile setting (Id 76, PSI_Gamepad_BindingSet) is id-mapped: ArkProfileSettings' ProfileMappings entry
//     (64 bytes; Id +0, MappingType +24, ValueMappings TArray +28/+32/+36 of 12-byte {Id, FName}) lists Ids 0..3
//     named PBS_1..PBS_4 (the FName "PBS" with numbers 2..5), shown as [ProfileSettingValues] PBS_n of Settings.int
//     ("Type A".."Type D", per language).
//   - Applying the gamepad settings, sub_8287DFC8(this = Input + 448, settings r4, mode r5): for mode 0 or 3 it takes
//     the profile value (settings +28), maps it through the input tweaks' m_PadBindingSetMap[4] (DisTweaks_
//     PlayerInput, byte array at +316 of the object sub_82849070 returns) and copies that m_PadBindingSetN (this-308,
//     -296, -284, -272) into the live bindings (sub_8287EEA0).
//   - The controller screen, sub_829A6428(menu r3, profile value r4): the same map picks one of four tables of 18
//     label FStrings ((menu +500) +380 + 216 * set; order of the movie's GamepadMappingEnum: A, B, X, Y, Start, Back,
//     LB, RB, LT, RT, LS, LS click, RS, RS click, D-pad up, down, left, right) and hands it to FillGamepadMapping.
//   - The options rows, sub_829A4FA8: the profile settings object is GEngine's virtual +1420; a row's values are its
//     mapping's ValueMappings, each named by Localize("ProfileSettingValues", <FName text>, "Settings")
//     (sub_822DE7F0).
//
// WHAT IS ADDED (profile value 4)
//   - sub_829A4FA8 (before the original, once): Id 4 named PBS_5 appended to the setting's ValueMappings (grown with
//     the game's TArray realloc sub_82299EC8(data, old bytes, new bytes), as the game frees it).
//   - sub_822DE7F0: "PBS_5" in ProfileSettingValues is the game's own PBS_4 text with its "D" made an "E" (Type E,
//     Тип E, Typ E, E típus).
//   - sub_8287DFC8: value 4 runs as value 0 (Type A) with the commands of Type A's A and B entries swapped for the
//     copy, then swapped back.
//   - sub_829A6428: value 4 shows Type A's labels with A's and B's swapped.
// dh_control_scheme_e (default on): off = the game's four schemes only (a saved Type E then reads as nothing).

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/ppc/func.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

REXCVAR_DEFINE_BOOL(dh_control_scheme_e, true, "Dishonored",
                    "Controller scheme \"Type E\" in Options: Type A with the A and B buttons' actions swapped")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Game functions called without hooking them, spelled in two parts so tools/direct_calls.py (which counts every
// literal 8-digit 82... address in the sources as hooked) keeps inlining them in the optimised codegen.
#define DH_GUEST_FN(hi, lo) __imp__sub_##hi##lo
#define DH_TARRAY_REALLOC DH_GUEST_FN(8229, 9EC8)  // (data, old bytes, new bytes) -> data
REX_EXTERN(DH_TARRAY_REALLOC);

REX_EXTERN(__imp__sub_829A4FA8);  // options: one setting's row
REX_EXTERN(__imp__sub_822DE7F0);  // Localize(FString* out, section, key, package, ...)
REX_EXTERN(__imp__sub_8287DFC8);  // Input: apply the gamepad settings
REX_EXTERN(__imp__sub_829A6428);  // controller screen: FillGamepadMapping

namespace {

constexpr uint32_t kGEngine = 0x834EBB18;
constexpr uint32_t kEngineProfileSettingsSlot = 1420;  // virtual: the profile settings object
constexpr uint32_t kProfileMappings = 80;              // TArray of 64-byte metadata entries
constexpr uint32_t kMappingSize = 64, kMappingValueMappings = 28;
constexpr int32_t kBindingSetId = 76;  // PSI_Gamepad_BindingSet
constexpr int32_t kTypeE = 4;
constexpr uint32_t kSettingsBindingSet = 28;     // in sub_8287DFC8's settings
constexpr uint32_t kPadBindingSet1 = 0xFFFFFECC;  // this - 308 in sub_8287DFC8
constexpr uint32_t kMenuMappingTables = 500, kMappingTable0 = 380;  // sub_829A6428

uint32_t Load32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void Store32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
void Swap12(uint8_t* base, uint32_t a, uint32_t b) {
  uint8_t t[12];
  std::memcpy(t, base + a, 12);
  std::memcpy(base + a, base + b, 12);
  std::memcpy(base + b, t, 12);
}

bool On() {
  static const bool on = REXCVAR_GET(dh_control_scheme_e);
  return on;
}

// A guest TCHAR string (big-endian UTF-16) equals an ASCII one.
bool GuestTcharEquals(const uint8_t* base, uint32_t at, const char* ascii) {
  if (!at) return false;
  for (size_t i = 0;; ++i) {
    const uint16_t ch = uint16_t(base[at + 2 * i] << 8 | base[at + 2 * i + 1]);
    if (ch != uint8_t(ascii[i])) return false;
    if (!ch) return true;
  }
}
// A guest FString (data, num with the terminator, max) starts with an ASCII text.
bool GuestFStringStartsWith(const uint8_t* base, uint32_t fstring, const char* ascii) {
  const uint32_t data = Load32(base, fstring), num = Load32(base, fstring + 4);
  const size_t n = std::strlen(ascii);
  if (!data || num < n + 1) return false;
  for (size_t i = 0; i < n; ++i)
    if (uint16_t(base[data + 2 * i] << 8 | base[data + 2 * i + 1]) != uint8_t(ascii[i])) return false;
  return true;
}
void WriteTchar(uint8_t* base, uint32_t at, const char* text) {
  for (size_t i = 0;; ++i) {
    base[at + 2 * i] = 0;
    base[at + 2 * i + 1] = uint8_t(text[i]);
    if (!text[i]) break;
  }
}

// --- the fifth value of the setting -------------------------------------------------------------------------------

std::mutex g_mutex;
uint32_t g_patched_settings = 0;  // the profile settings object already given Type E

void AddTypeE(PPCContext& ctx, uint8_t* base) {
  const uint32_t engine = Load32(base, kGEngine);
  if (!engine) return;
  const uint32_t vtable = Load32(base, engine);
  const uint32_t fn = vtable ? Load32(base, vtable + kEngineProfileSettingsSlot) : 0;
  PPCFunc* get = fn ? rex::runtime::ResolveIndirectFunction(fn) : nullptr;
  if (!get) return;
  ctx.r3.u64 = engine;
  get(ctx, base);
  const uint32_t settings = ctx.r3.u32;
  if (!settings || settings == g_patched_settings) return;
  const uint32_t maps = Load32(base, settings + kProfileMappings);
  const int32_t count = int32_t(Load32(base, settings + kProfileMappings + 4));
  for (int32_t i = 0; maps && i < count; ++i) {
    const uint32_t m = maps + uint32_t(i) * kMappingSize;
    if (int32_t(Load32(base, m)) != kBindingSetId) continue;
    const uint32_t arr = m + kMappingValueMappings;
    uint32_t data = Load32(base, arr);
    const uint32_t num = Load32(base, arr + 4), max = Load32(base, arr + 8);
    g_patched_settings = settings;
    if (num == 5 && data && int32_t(Load32(base, data + 4 * 12)) == kTypeE) return;  // done before (same object)
    // Expect the game's own: Ids 0..3 named PBS with numbers 2..5.
    bool expected = num == 4 && data != 0;
    const uint32_t name = expected ? Load32(base, data + 4) : 0;
    for (uint32_t k = 0; expected && k < 4; ++k)
      expected = int32_t(Load32(base, data + k * 12)) == int32_t(k) && Load32(base, data + k * 12 + 4) == name &&
                 Load32(base, data + k * 12 + 8) == k + 2;
    if (!expected) {
      REXLOG_WARN("[dh-scheme] the control scheme setting has an unexpected shape ({} values): Type E not added", num);
      return;
    }
    if (max < 5) {
      ctx.r3.u64 = data;
      ctx.r4.u64 = max * 12;
      ctx.r5.u64 = 5 * 12;
      DH_TARRAY_REALLOC(ctx, base);
      data = ctx.r3.u32;
      if (!data) return;
      Store32(base, arr, data);
      Store32(base, arr + 8, 5);
    }
    Store32(base, data + 4 * 12, uint32_t(kTypeE));
    Store32(base, data + 4 * 12 + 4, name);
    Store32(base, data + 4 * 12 + 8, 6);  // PBS_5
    Store32(base, arr + 4, 5);
    REXLOG_INFO("[dh-scheme] Control Scheme: Type E added (profile settings {:08X})", settings);
    return;
  }
}

}  // namespace

REX_HOOK_RAW(sub_829A4FA8) {
  if (On()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const PPCContext saved = ctx;
    AddTypeE(ctx, base);
    ctx = saved;
  }
  __imp__sub_829A4FA8(ctx, base);
}

// --- its name ------------------------------------------------------------------------------------------------------

REX_HOOK_RAW(sub_822DE7F0) {
  const uint32_t out = ctx.r3.u32;
  if (!On() || !GuestTcharEquals(base, ctx.r5.u32, "PBS_5") ||
      !GuestTcharEquals(base, ctx.r4.u32, "ProfileSettingValues")) {
    __imp__sub_822DE7F0(ctx, base);
    return;
  }
  // The key "PBS_4" on the guest stack below this frame; the call's own frames go below it.
  const uint64_t r1 = ctx.r1.u64;
  const uint32_t key = (ctx.r1.u32 - 0x40u) & ~15u;
  WriteTchar(base, key, "PBS_4");
  ctx.r1.u64 = key - 0x40u;
  Store32(base, ctx.r1.u32, uint32_t(r1));  // back chain
  ctx.r5.u64 = key;
  __imp__sub_822DE7F0(ctx, base);
  ctx.r1.u64 = r1;
  // "Type D" -> "Type E" (every language's text has the one Latin D)
  const uint32_t data = Load32(base, out), num = Load32(base, out + 4);
  for (uint32_t i = 0; data && i + 1 < num; ++i) {
    if (base[data + 2 * i] == 0 && base[data + 2 * i + 1] == 'D') {
      base[data + 2 * i + 1] = 'E';
      break;
    }
  }
}

// --- applying it ---------------------------------------------------------------------------------------------------

REX_HOOK_RAW(sub_8287DFC8) {
  const uint32_t self = ctx.r3.u32, settings = ctx.r4.u32, mode = ctx.r5.u32;
  if (!On() || !settings || (mode != 0 && mode != 3) ||
      int32_t(Load32(base, settings + kSettingsBindingSet)) != kTypeE) {
    __imp__sub_8287DFC8(ctx, base);
    return;
  }
  // Type A's A and B entries, by their commands
  const uint32_t set1 = self + kPadBindingSet1;
  const uint32_t data = Load32(base, set1);
  const int32_t num = int32_t(Load32(base, set1 + 4));
  uint32_t jump = 0, sneak = 0;
  for (int32_t i = 0; data && i < num; ++i) {
    const uint32_t command = data + uint32_t(i) * 24 + 8;
    if (!jump && GuestFStringStartsWith(base, command, "GBA_Jump")) jump = command;
    if (!sneak && GuestFStringStartsWith(base, command, "GBA_Sneak_Gamepad")) sneak = command;
  }
  Store32(base, settings + kSettingsBindingSet, 0);
  if (jump && sneak) Swap12(base, jump, sneak);
  __imp__sub_8287DFC8(ctx, base);
  if (jump && sneak) Swap12(base, jump, sneak);
  Store32(base, settings + kSettingsBindingSet, uint32_t(kTypeE));
  static bool logged = false;
  if (!logged) {
    logged = true;
    if (jump && sneak)
      REXLOG_INFO("[dh-scheme] Type E applied: Type A with A and B swapped");
    else
      REXLOG_WARN("[dh-scheme] Type E: Type A's jump / sneak bindings not found, applied as Type A");
  }
}

// --- the controller screen -----------------------------------------------------------------------------------------

REX_HOOK_RAW(sub_829A6428) {
  if (!On() || int32_t(ctx.r4.u32) != kTypeE) {
    __imp__sub_829A6428(ctx, base);
    return;
  }
  const uint32_t tables = Load32(base, ctx.r3.u32 + kMenuMappingTables);
  const uint32_t a = tables ? tables + kMappingTable0 : 0, b = a + 12;  // Type A's labels for A and B
  ctx.r4.u64 = 0;
  if (a) Swap12(base, a, b);
  __imp__sub_829A6428(ctx, base);
  if (a) Swap12(base, a, b);
}
