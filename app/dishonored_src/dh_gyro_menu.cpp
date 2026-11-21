// Dishonored (2026-11-21) - gyro aiming in Options -> Controls (user request: gyro controls, "while left hand held +
// option to have enabled all the time, similar how ME2 did that", with rows in the game's Options). The gyro itself is
// the SDK's (input_gyro*, sdk/src/input/switch/switch_input_driver.cpp, from masseffect-cinnamon via ME2).
//
// HOW THE GAME BUILDS ITS ROWS (from the generated listings; DishonoredGame.u / Engine.u of the title update)
//   - Options -> Controls is a DisSettingsCategory of the menu script (DisGFxMoviePlayerMenuBase.m_SettingsCategoryList);
//     its rows are DisSetting entries of 20 bytes: m_SettingID +0, m_SettingNameOverride FString +4 (the row's label
//     as is when not empty, else Localize("ProfileSettingIDs", PSI_<id>)), flags +16. The category / subcategory
//     builders (sub_829A4AC0 / sub_829A4D70, hooked in dh_main_menu.cpp) make one row per entry with sub_829A4FA8.
//   - A row shows a profile setting of ArkProfileSettings (GEngine virtual +1420): ProfileSettings +68/+72 (24-byte
//     entries: PropertyId +4, Data.Type +8 (1 = SDT_Int32, 5 = SDT_Float), Data.Value1 +12) and ProfileMappings
//     +80/+84 (64-byte entries: Id +0, MappingType byte +24 (2 Ranged, 3 IdMapped, 4 Ark's Boolean), ValueMappings
//     +28 of 12-byte {Id, FName}, MinVal +52, MaxVal +56, RangeIncrement +60). Value names are
//     Localize("ProfileSettingValues", <FName text>, "Settings").
//   - A changed row calls OnSettingChange(_SettingID, _fValue) -> sub_829A6958(menu, id r4, value f1): list rows
//     take ValueMappings[floor(value + 0.5)].Id, ranged rows the value. Then sub_826F38A8 copies the profile into
//     the game's settings struct and its listeners apply it (PlayerInput: sub_8287DFC8).
//
// WHAT IS ADDED
//   - Four rows after the gamepad rows, carried by the PC's mouse settings, which the console menu never shows:
//       67 PSI_Mouse_Sensitivity (Ranged float)  -> Gyro Sensitivity, range made 0.5 .. 10 by 0.5  (input_gyro_sensitivity)
//       68 PSI_Mouse_bSmooting  (Boolean)        -> Invert Gyro X                                  (input_gyro_invert_x)
//       69 PSI_Mouse_bInvertY   (Boolean)        -> Invert Gyro Y                                  (input_gyro_invert_y)
//       70 PSI_Mouse_bAutoAim   (Boolean)        -> Gyro Aiming: made IdMapped with three values,  (input_gyro)
//                                                   Off / With Power/Gadget / Always
//     The labels are the entries' m_SettingNameOverride; the three value names are the FName BP_False with numbers
//     101..103 ("BP_False_100".."BP_False_102"), which Localize (hook in dh_control_scheme.cpp) answers with our text.
//   - The game still sees the mouse defaults: after sub_826F38A8 its struct's mouse fields (+0 sensitivity 1.0, +4
//     smoothing 1, +8 invert 0, +12 auto aim 0, the Engine.u DefaultSettings) are put back, so these values never
//     reach the PlayerInput, whatever the profile holds.
//   - The values are the cvars (the rows show them each time they are built) and are saved in
//     <user data>/dishonored_gyro.cfg (read at start, over the toml's values), as ME2's me2_gyro.cfg.
//   - Mode 1 ("With Power/Gadget") is active while the left hand's button is held: after the game applies its
//     control scheme, the live bindings (PlayerInput +104, KeyBind 24 bytes: Name FName +0, Command FString +8) are
//     searched for GBA_Secondary and input_gyro_aim_button set to its 360 control (Type A/C/E: LT, B: LB, D: RB).
// dh_gyro_menu (default on): off = no rows, the mouse settings left alone; the gyro then follows the toml only.

#include "dh_gyro_menu.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

REXCVAR_DEFINE_BOOL(dh_gyro_menu, true, "Dishonored",
                    "Options -> Controls: gyro aiming rows (mode, sensitivity, invert X / Y), saved in "
                    "dishonored_gyro.cfg; the Power/Gadget button of the control scheme is the gyro's aim button")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(dh_gyro_menu_text, "", "Dishonored",
                      "Gyro rows' texts, '|' separated: mode label, Off, With Power/Gadget, Always, sensitivity "
                      "label, invert X label, invert Y label (UTF-8); empty = English, or Russian when the game is");

// Game functions called without hooking them, spelled in two parts so tools/direct_calls.py (which counts every
// literal 8-digit 82... address in the sources as hooked) keeps inlining them in the optimised codegen.
#define DH_GUEST_FN(hi, lo) __imp__sub_##hi##lo
#define DH_TARRAY_REALLOC DH_GUEST_FN(8229, 9EC8)  // (data, old bytes, new bytes) -> data
REX_EXTERN(DH_TARRAY_REALLOC);

REX_EXTERN(__imp__sub_829A6958);  // options: a row changed (menu, id, value f1)
REX_EXTERN(__imp__sub_826F38A8);  // the profile into the game's settings struct

namespace {

constexpr uint32_t kGEngine = 0x834EBB18;
constexpr uint32_t kEngineProfileSettingsSlot = 1420;
constexpr uint32_t kProfileSettings = 68, kSettingSize = 24, kSettingId = 4, kSettingType = 8, kSettingValue = 12;
constexpr uint32_t kProfileMappings = 80, kMappingSize = 64, kMappingType = 24, kMappingValues = 28, kMappingMin = 52,
                   kMappingMax = 56, kMappingStep = 60;
constexpr uint8_t kSdtInt32 = 1, kSdtFloat = 5;
constexpr uint8_t kRanged = 2, kIdMapped = 3, kBoolean = 4;
constexpr uint32_t kDisSettingSize = 20, kDisSettingLabel = 4, kDisSettingFlags = 16;
constexpr uint32_t kCachedLanguage = 0x83523858u;  // appGetLanguageExt's FString (dh_language.cpp)
constexpr uint32_t kGNames = 0x834FF19C;           // TArray<FNameEntry*> (FName::ToString, sub_822E53F0)
constexpr uint32_t kInputBindings = 104, kKeyBindSize = 24, kKeyBindCommand = 8;

constexpr int32_t kIdSens = 67, kIdInvX = 68, kIdInvY = 69, kIdMode = 70;
constexpr int32_t kRowIds[4] = {kIdMode, kIdSens, kIdInvX, kIdInvY};  // shown in this order
constexpr uint32_t kModeNameNumber = 101;                              // BP_False_100 .. BP_False_102
constexpr float kSensMin = 0.5f, kSensMax = 10.0f, kSensStep = 0.5f;   // input_gyro_sensitivity's range
// The game's own gamepad rows: the gyro rows go after the last of them.
bool IsPadRow(int32_t id) { return id >= 76 && id <= 84; }
bool IsGyroRow(int32_t id) { return id >= kIdSens && id <= kIdMode; }

uint32_t Ld(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
void St(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
float LdF(const uint8_t* base, uint32_t a) { return std::bit_cast<float>(Ld(base, a)); }
void StF(uint8_t* base, uint32_t a, float v) { St(base, a, std::bit_cast<uint32_t>(v)); }

bool Enabled() {
  static const bool on = REXCVAR_GET(dh_gyro_menu);
  return on;
}

std::mutex g_mutex;

// --- the values: the SDK's cvars (Switch); kept here too, for the PC build that has none --------------------------

struct Values {
  int mode = 1;
  double sens = 3.0;
  bool inv_x = true, inv_y = true;
} g_local;

bool HaveCvars() { return !rex::cvar::GetFlagByName("input_gyro").empty(); }
Values Current() {
  if (!HaveCvars()) return g_local;
  Values v;
  v.mode = std::clamp(rex::cvar::Query<int32_t>("input_gyro"), 0, 2);
  v.sens = rex::cvar::Query<double>("input_gyro_sensitivity");
  if (!(v.sens > 0)) v.sens = 3.0;
  v.inv_x = rex::cvar::Query<bool>("input_gyro_invert_x");
  v.inv_y = rex::cvar::Query<bool>("input_gyro_invert_y");
  return v;
}
void Set(const char* name, const std::string& value) {
  rex::cvar::SetFlagByName(name, value);
  if (!std::strcmp(name, "input_gyro")) g_local.mode = std::clamp(std::atoi(value.c_str()), 0, 2);
  if (!std::strcmp(name, "input_gyro_sensitivity")) g_local.sens = std::atof(value.c_str());
  if (!std::strcmp(name, "input_gyro_invert_x")) g_local.inv_x = value == "true";
  if (!std::strcmp(name, "input_gyro_invert_y")) g_local.inv_y = value == "true";
}

std::filesystem::path CfgPath() {
  rex::Runtime* rt = rex::Runtime::instance();
  return rt ? rt->user_data_root() / "dishonored_gyro.cfg" : std::filesystem::path("dishonored_gyro.cfg");
}

void Save() {
  const Values v = Current();
  const std::filesystem::path p = CfgPath();
  if (std::FILE* f = std::fopen(p.string().c_str(), "wb")) {
    std::fprintf(f, "input_gyro=%d\ninput_gyro_sensitivity=%.1f\ninput_gyro_invert_x=%s\ninput_gyro_invert_y=%s\n",
                 v.mode, v.sens, v.inv_x ? "true" : "false", v.inv_y ? "true" : "false");
    std::fflush(f);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    REXLOG_INFO("[dh-gyro] saved to {}{}", p.string(), ok ? "" : " (write error)");
  } else {
    REXLOG_WARN("[dh-gyro] cannot write {}", p.string());
  }
}

// --- texts -----------------------------------------------------------------------------------------------------------

enum Text { kMode, kOff, kHeld, kAlways, kSens, kInvX, kInvY, kTextCount };

std::string GameLanguage(const uint8_t* base) {
  const uint32_t data = Ld(base, kCachedLanguage), num = Ld(base, kCachedLanguage + 4);
  std::string s;
  for (uint32_t i = 0; data && i + 1 < num && i < 8; ++i) s.push_back(char(base[data + 2 * i + 1]));
  return s;
}

std::vector<std::string> Texts(const uint8_t* base) {
  static const char* const kEnglish[kTextCount] = {"Gyro Aiming",      "Off",           "With Power/Gadget",
                                                   "Always",           "Gyro Sensitivity", "Invert Gyro X",
                                                   "Invert Gyro Y"};
  // The game's own words where it has them: Выкл. / Всегда (HV_Off, HV_Always), Умение/устройство (PSI_GBA_Secondary).
  static const char* const kRussian[kTextCount] = {
      "Гироприцеливание", "Выкл.", "С умением/устройством", "Всегда", "Чувствительность гироскопа",
      "Инверсия гироскопа по оси X", "Инверсия гироскопа по оси Y"};
  const char* const* defaults = GameLanguage(base) == "RUS" ? kRussian : kEnglish;
  std::vector<std::string> t;
  const std::string s = REXCVAR_GET(dh_gyro_menu_text);
  for (size_t a = 0; !s.empty();) {
    const size_t e = s.find('|', a);
    t.push_back(s.substr(a, e == std::string::npos ? std::string::npos : e - a));
    if (e == std::string::npos) break;
    a = e + 1;
  }
  t.resize(kTextCount);
  for (int i = 0; i < kTextCount; ++i)
    if (t[i].empty()) t[i] = defaults[i];
  return t;
}

std::u16string Utf16(const std::string& s) {
  std::u16string out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = uint8_t(s[i]);
    uint32_t cp = c;
    int n = 0;
    if (c >= 0xF0) cp = c & 0x07, n = 3;
    else if (c >= 0xE0) cp = c & 0x0F, n = 2;
    else if (c >= 0xC0) cp = c & 0x1F, n = 1;
    ++i;
    for (; n > 0 && i < s.size(); --n, ++i) cp = (cp << 6) | (uint8_t(s[i]) & 0x3F);
    out.push_back(cp > 0xFFFF ? u'?' : char16_t(cp));
  }
  return out;
}

uint32_t GuestRealloc(PPCContext& ctx, uint8_t* base, uint32_t data, uint32_t old_bytes, uint32_t new_bytes) {
  const PPCContext saved = ctx;
  ctx.r3.u64 = data;
  ctx.r4.u64 = old_bytes;
  ctx.r5.u64 = new_bytes;
  DH_TARRAY_REALLOC(ctx, base);
  const uint32_t result = ctx.r3.u32;
  ctx = saved;
  return result;
}

// An FString's {data, num, max} at `at` holding `text`, allocated as FString::FString(const TCHAR*) does (the game
// frees it with its destructor when it owns it).
bool MakeFString(PPCContext& ctx, uint8_t* base, uint32_t at, const std::string& text) {
  const std::u16string u = Utf16(text);
  const uint32_t num = uint32_t(u.size() + 1);
  const uint32_t data = GuestRealloc(ctx, base, 0, 0, num * 2);
  if (!data) return false;
  for (uint32_t i = 0; i < num; ++i) {
    const char16_t ch = i < u.size() ? u[i] : 0;
    base[data + 2 * i] = uint8_t(ch >> 8);
    base[data + 2 * i + 1] = uint8_t(ch);
  }
  St(base, at, data);
  St(base, at + 4, num);
  St(base, at + 8, num);
  return true;
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

// An FName's text (FName::ToString without calling it): GNames[index] -> entry, flags +0 (bit 0 = wide), text +8.
std::string FNameText(const uint8_t* base, uint32_t fname) {
  const uint32_t index = Ld(base, fname), number = Ld(base, fname + 4);
  const uint32_t names = Ld(base, kGNames), count = Ld(base, kGNames + 4);
  if (!names || index >= count) return {};
  const uint32_t entry = Ld(base, names + 4 * index);
  if (!entry) return {};
  const bool wide = (Ld(base, entry) & 1) != 0;
  std::string s;
  for (uint32_t i = 0; i < 256; ++i) {
    const uint16_t ch = wide ? uint16_t(base[entry + 8 + 2 * i] << 8 | base[entry + 8 + 2 * i + 1])
                             : base[entry + 8 + i];
    if (!ch) break;
    s.push_back(ch < 0x80 ? char(ch) : '?');
  }
  if (number) s += "_" + std::to_string(number - 1);
  return s;
}

bool FStringStartsWith(const uint8_t* base, uint32_t fstring, const char* ascii) {
  const uint32_t data = Ld(base, fstring), num = Ld(base, fstring + 4);
  const size_t n = std::strlen(ascii);
  if (!data || num < n + 1) return false;
  for (size_t i = 0; i < n; ++i)
    if (uint16_t(base[data + 2 * i] << 8 | base[data + 2 * i + 1]) != uint8_t(ascii[i])) return false;
  return true;
}

// --- the profile settings ----------------------------------------------------------------------------------------------

uint32_t ProfileSettings(PPCContext& ctx, uint8_t* base) {
  const uint32_t engine = Ld(base, kGEngine);
  if (!engine) return 0;
  const uint32_t vtable = Ld(base, engine);
  const uint32_t fn = vtable ? Ld(base, vtable + kEngineProfileSettingsSlot) : 0;
  PPCFunc* get = fn ? rex::runtime::ResolveIndirectFunction(fn) : nullptr;
  if (!get) return 0;
  const PPCContext saved = ctx;
  ctx.r3.u64 = engine;
  get(ctx, base);
  const uint32_t settings = ctx.r3.u32;
  ctx = saved;
  return settings;
}

uint32_t FindMapping(const uint8_t* base, uint32_t settings, int32_t id) {
  const uint32_t data = Ld(base, settings + kProfileMappings);
  const int32_t num = int32_t(Ld(base, settings + kProfileMappings + 4));
  for (int32_t i = 0; data && i < num; ++i)
    if (int32_t(Ld(base, data + uint32_t(i) * kMappingSize)) == id) return data + uint32_t(i) * kMappingSize;
  return 0;
}
uint32_t FindSetting(const uint8_t* base, uint32_t settings, int32_t id) {
  const uint32_t data = Ld(base, settings + kProfileSettings);
  const int32_t num = int32_t(Ld(base, settings + kProfileSettings + 4));
  for (int32_t i = 0; data && i < num; ++i) {
    const uint32_t e = data + uint32_t(i) * kSettingSize;
    if (int32_t(Ld(base, e + kSettingId)) == id) return e;
  }
  return 0;
}

uint32_t g_prepared = 0;  // the profile settings object given the gyro rows' shapes
bool g_broken = false;    // the settings were not as expected: no rows

// The mappings made into the gyro rows' (once per profile settings object).
bool Prepare(PPCContext& ctx, uint8_t* base, uint32_t settings) {
  if (g_broken) return false;
  if (settings == g_prepared) return true;
  const uint32_t mode = FindMapping(base, settings, kIdMode), sens = FindMapping(base, settings, kIdSens),
                 inv_x = FindMapping(base, settings, kIdInvX), inv_y = FindMapping(base, settings, kIdInvY);
  const uint32_t s_mode = FindSetting(base, settings, kIdMode), s_sens = FindSetting(base, settings, kIdSens),
                 s_inv_x = FindSetting(base, settings, kIdInvX), s_inv_y = FindSetting(base, settings, kIdInvY);
  const auto fail = [&](const char* why) {
    g_broken = true;
    REXLOG_WARN("[dh-gyro] the mouse settings are not as expected ({}): no gyro rows in Options", why);
    return false;
  };
  if (!mode || !sens || !inv_x || !inv_y || !s_mode || !s_sens || !s_inv_x || !s_inv_y) return fail("missing");
  if (base[sens + kMappingType] != kRanged || base[s_sens + kSettingType] != kSdtFloat) return fail("sensitivity");
  for (const uint32_t s : {s_mode, s_inv_x, s_inv_y})
    if (base[s + kSettingType] != kSdtInt32) return fail("value type");
  for (const uint32_t m : {inv_x, inv_y}) {
    const uint32_t v = Ld(base, m + kMappingValues);
    if (base[m + kMappingType] != kBoolean || Ld(base, m + kMappingValues + 4) != 2 || !v ||
        Ld(base, v) != 0 || Ld(base, v + 12) != 1)
      return fail("invert");
  }
  // Gyro Aiming: three values (Ids 0..2) named by BP_False with numbers 101..103.
  const uint32_t arr = mode + kMappingValues;
  uint32_t data = Ld(base, arr);
  const uint32_t num = Ld(base, arr + 4), max = Ld(base, arr + 8);
  const bool done = base[mode + kMappingType] == kIdMapped && num == 3 && data &&
                    Ld(base, data + 2 * 12 + 8) == kModeNameNumber + 2;
  if (!done) {
    if (base[mode + kMappingType] != kBoolean || num != 2 || !data) return fail("mode");
    const uint32_t name = Ld(base, data + 4);  // BP_False
    if (max < 3) {
      data = GuestRealloc(ctx, base, data, max * 12, 3 * 12);
      if (!data) return fail("allocation");
      St(base, arr, data);
      St(base, arr + 8, 3);
    }
    for (uint32_t k = 0; k < 3; ++k) {
      St(base, data + k * 12, k);
      St(base, data + k * 12 + 4, name);
      St(base, data + k * 12 + 8, kModeNameNumber + k);
    }
    St(base, arr + 4, 3);
    base[mode + kMappingType] = kIdMapped;
  }
  StF(base, sens + kMappingMin, kSensMin);
  StF(base, sens + kMappingMax, kSensMax);
  StF(base, sens + kMappingStep, kSensStep);
  g_prepared = settings;
  REXLOG_INFO("[dh-gyro] Options -> Controls: gyro rows ready (profile settings {:08X})", settings);
  return true;
}

// The rows show the cvars: written into the profile before they are built.
void PushValues(uint8_t* base, uint32_t settings) {
  const Values v = Current();
  const float sens = std::clamp(float(std::round(v.sens / kSensStep) * kSensStep), kSensMin, kSensMax);
  StF(base, FindSetting(base, settings, kIdSens) + kSettingValue, sens);
  St(base, FindSetting(base, settings, kIdInvX) + kSettingValue, v.inv_x ? 1 : 0);
  St(base, FindSetting(base, settings, kIdInvY) + kSettingValue, v.inv_y ? 1 : 0);
  St(base, FindSetting(base, settings, kIdMode) + kSettingValue, uint32_t(v.mode));
}

// --- the rows' entries ----------------------------------------------------------------------------------------------

struct Labels {
  std::string language;
  uint32_t fstrings = 0;  // four FStrings of 12 bytes in guest memory, never freed (the entries point at them)
} g_labels;

bool MakeLabels(PPCContext& ctx, uint8_t* base) {
  const std::string language = GameLanguage(base);
  if (g_labels.fstrings && g_labels.language == language) return true;
  const std::vector<std::string> t = Texts(base);
  const uint32_t block = g_labels.fstrings ? g_labels.fstrings : GuestRealloc(ctx, base, 0, 0, 4 * 12);
  if (!block) return false;
  const int text[4] = {kMode, kSens, kInvX, kInvY};
  for (int i = 0; i < 4; ++i)
    if (!MakeFString(ctx, base, block + 12 * i, t[text[i]])) return false;
  g_labels.fstrings = block;
  g_labels.language = language;
  return true;
}

std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> g_buffers;  // array -> {guest buffer, capacity}

}  // namespace

namespace dh::gyro_menu {

bool Inject(PPCContext& ctx, uint8_t* base, uint32_t array, Injected& saved) {
  if (!Enabled() || !array) return false;
  const uint32_t data = Ld(base, array);
  const int32_t num = int32_t(Ld(base, array + 4));
  if (!data || num <= 0) return false;
  int32_t after = -1;
  for (int32_t i = 0; i < num; ++i) {
    const int32_t id = int32_t(Ld(base, data + uint32_t(i) * kDisSettingSize));
    if (IsGyroRow(id)) return false;  // the script has them already (never on the console)
    if (IsPadRow(id)) after = i;
  }
  if (after < 0) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint32_t settings = ProfileSettings(ctx, base);
  if (!settings || !Prepare(ctx, base, settings) || !MakeLabels(ctx, base)) return false;
  PushValues(base, settings);
  // One buffer per array: a category's builder calls its subcategories' while its own array is swapped.
  const uint32_t want = uint32_t(num) + 4;
  auto& [buffer, capacity] = g_buffers[array];
  if (capacity < want) {
    const uint32_t grown = GuestRealloc(ctx, base, buffer, capacity * kDisSettingSize, want * kDisSettingSize);
    if (!grown) return false;
    buffer = grown;
    capacity = want;
  }
  const uint32_t head = uint32_t(after + 1) * kDisSettingSize;
  std::memmove(base + buffer, base + data, head);
  for (uint32_t k = 0; k < 4; ++k) {
    const uint32_t e = buffer + head + k * kDisSettingSize;
    St(base, e, uint32_t(kRowIds[k]));
    std::memcpy(base + e + kDisSettingLabel, base + g_labels.fstrings + 12 * k, 12);
    St(base, e + kDisSettingFlags, 0);
  }
  std::memmove(base + buffer + head + 4 * kDisSettingSize, base + data + head,
               uint32_t(num) * kDisSettingSize - head);
  saved = {array, data, uint32_t(num), Ld(base, array + 8)};
  St(base, array, buffer);
  St(base, array + 4, want);
  St(base, array + 8, want);
  return true;
}

void Restore(uint8_t* base, const Injected& saved) {
  if (!saved.array) return;
  St(base, saved.array, saved.data);
  St(base, saved.array + 4, saved.num);
  St(base, saved.array + 8, saved.max);
}

bool Localize(PPCContext& ctx, uint8_t* base) {
  if (!Enabled() || !GuestTcharEquals(base, ctx.r4.u32, "ProfileSettingValues")) return false;
  int which = -1;
  for (int k = 0; k < 3; ++k) {
    const std::string key = "BP_False_" + std::to_string(kModeNameNumber - 1 + k);
    if (GuestTcharEquals(base, ctx.r5.u32, key.c_str())) which = k;
  }
  if (which < 0) return false;
  const uint32_t out = ctx.r3.u32;
  const std::vector<std::string> t = Texts(base);
  if (!MakeFString(ctx, base, out, t[kOff + which])) {
    St(base, out, 0);
    St(base, out + 4, 0);
    St(base, out + 8, 0);
  }
  ctx.r3.u64 = out;
  return true;
}

void AfterPadSettings(uint8_t* base, uint32_t input) {
  if (!Enabled() || !input) return;
  const uint32_t data = Ld(base, input + kInputBindings);
  const int32_t num = int32_t(Ld(base, input + kInputBindings + 4));
  static const char* const kButtons[4] = {"XboxTypeS_LeftTrigger", "XboxTypeS_RightTrigger",
                                          "XboxTypeS_LeftShoulder", "XboxTypeS_RightShoulder"};
  int found = -1;
  for (int32_t i = 0; data && i < num && i < 512 && found < 0; ++i) {
    const uint32_t bind = data + uint32_t(i) * kKeyBindSize;
    if (!FStringStartsWith(base, bind + kKeyBindCommand, "GBA_Secondary")) continue;
    const std::string name = FNameText(base, bind);
    for (int k = 0; k < 4; ++k)
      if (name == kButtons[k]) found = k;
  }
  static int last = -2;
  if (found < 0) {
    if (last != -1) REXLOG_WARN("[dh-gyro] no pad button bound to GBA_Secondary: the gyro's aim button stays");
    last = -1;
    return;
  }
  if (found == last) return;
  last = found;
  rex::cvar::SetFlagByName("input_gyro_aim_button", std::to_string(found));
  REXLOG_INFO("[dh-gyro] Power/Gadget is {}: gyro aiming mode 1 follows it (input_gyro_aim_button = {})",
              kButtons[found], found);
}

}  // namespace dh::gyro_menu

// At start (me2_main.cpp OnPostSetup): the menu's saved choices over the toml's values.
namespace me2::gyro_menu {
void Load() {
  if (!Enabled()) return;
  g_local = Current();
  const std::filesystem::path p = CfgPath();
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (!f) {
    REXLOG_INFO("[dh-gyro] no saved choices yet ({}): the toml's values", p.string());
    return;
  }
  char line[128];
  int n = 0;
  while (std::fgets(line, sizeof(line), f)) {
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    const size_t eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = s.substr(0, eq), value = s.substr(eq + 1);
    if (key == "input_gyro" || key == "input_gyro_sensitivity" || key == "input_gyro_invert_x" ||
        key == "input_gyro_invert_y") {
      Set(key.c_str(), value);
      ++n;
    }
  }
  std::fclose(f);
  const Values v = Current();
  REXLOG_INFO("[dh-gyro] {} setting(s) from {}: mode {}, sensitivity {:.1f}, invert x {} y {}", n, p.string(), v.mode,
              v.sens, v.inv_x, v.inv_y);
}
}  // namespace me2::gyro_menu

// A row changed: the gyro rows set the cvars and are saved.
REX_HOOK_RAW(sub_829A6958) {
  const int32_t id = ctx.r4.s32;
  const double value = ctx.f1.f64;
  __imp__sub_829A6958(ctx, base);
  if (!Enabled() || !IsGyroRow(id) || !g_prepared) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  const int index = int(std::floor(value + 0.5));
  char text[16];
  switch (id) {
    case kIdMode:
      Set("input_gyro", std::to_string(std::clamp(index, 0, 2)));
      break;
    case kIdSens:
      std::snprintf(text, sizeof(text), "%.1f",
                    std::clamp(std::round(value / kSensStep) * kSensStep, double(kSensMin), double(kSensMax)));
      Set("input_gyro_sensitivity", text);
      break;
    case kIdInvX:
      Set("input_gyro_invert_x", index ? "true" : "false");
      break;
    case kIdInvY:
      Set("input_gyro_invert_y", index ? "true" : "false");
      break;
  }
  Save();
  const Values v = Current();
  REXLOG_INFO("[dh-gyro] row {} = {:.2f}: mode {}, sensitivity {:.1f}, invert x {} y {}", id, value, v.mode, v.sens,
              v.inv_x, v.inv_y);
}

// The game's settings struct keeps the mouse defaults (Engine.u DefaultSettings 67..70): the rows' values in the
// profile never reach the PlayerInput.
REX_HOOK_RAW(sub_826F38A8) {
  const uint32_t out = ctx.r3.u32;
  __imp__sub_826F38A8(ctx, base);
  if (!Enabled() || !out) return;
  StF(base, out + 0, 1.0f);  // PSI_Mouse_Sensitivity
  St(base, out + 4, 1);      // PSI_Mouse_bSmooting
  St(base, out + 8, 0);      // PSI_Mouse_bInvertY
  St(base, out + 12, 0);     // PSI_Mouse_bAutoAim
}
