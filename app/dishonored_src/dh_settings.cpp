// Dishonored (2026-11-11) - UE3 system settings overridden from the port's configuration.
//
// The Xbox 360 build reads its quality settings from [SystemSettings] of Xbox360-DishonoredEngine.ini inside
// Coalesced_*.bin (DetailMode=2, every shadow kind on, LOD multipliers 1: the PC's top settings). The title update
// patches that file with a binary patch (Patches\Coalesced_*.bin.bs), so the file itself is left alone; the values are
// replaced as the game reads them.
//
// FSystemSettingsData::LoadFromIni = sub_823BFBE0(this r3, section r4, ini file r5, ...): three tables of {key name,
// member address} on its stack, read by
//     sub_8229F1B0(section, key, UBOOL* out, file)   43 booleans
//     sub_8229F050(section, key, INT* out, file)     21 integers
//     sub_8229F100(section, key, FLOAT* out, file)   17 floats
// (each returns 1 when the key was found). While LoadFromIni runs on this thread, a key listed in dh_system_settings
// gets the listed value after the game's own read, found or not, in every section it loads (the base section and the
// ones based on it, split screen too).
//
// dh_system_settings: "Key=Value,Key=Value" (keys as in the ini, case-insensitive; True / False for booleans).
// "[settings]" lines log each override with the value the game had.

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_STRING(dh_system_settings, "", "Dishonored",
                      "UE3 [SystemSettings] values replaced as the game loads them: \"Key=Value,Key=Value\" (for "
                      "example DetailMode=1,bAllowWholeSceneDominantShadows=False); empty = the game's own")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

struct Override {
  std::string key;    // lower case
  std::string value;  // as written
  uint32_t logged = 0;
};

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<Override>& Overrides() {
  static std::vector<Override> list = [] {
    std::vector<Override> out;
    const std::string text = REXCVAR_GET(dh_system_settings);
    size_t at = 0;
    while (at <= text.size()) {
      size_t end = text.find(',', at);
      if (end == std::string::npos) end = text.size();
      const std::string item = Trim(text.substr(at, end - at));
      const size_t eq = item.find('=');
      if (eq != std::string::npos && eq > 0) {
        out.push_back({Lower(Trim(item.substr(0, eq))), Trim(item.substr(eq + 1))});
      } else if (!item.empty()) {
        REXLOG_WARN("[settings] '{}' in dh_system_settings is not Key=Value (left out)", item);
      }
      at = end + 1;
    }
    REXLOG_INFO("[settings] {} UE3 system settings overridden (dh_system_settings)", out.size());
    return out;
  }();
  return list;
}

// The guest's key (UTF-16, big-endian), lower case; empty when it is not plain ASCII.
std::string GuestKey(const uint8_t* base, uint32_t va) {
  std::string s;
  if (va < 0x10000) return s;
  for (uint32_t i = 0; i < 96; ++i) {
    const uint8_t hi = base[va + 2 * i], lo = base[va + 2 * i + 1];
    if (!hi && !lo) return s;
    if (hi || lo >= 0x80) return {};
    s.push_back(char(std::tolower(lo)));
  }
  return {};
}

Override* Find(const uint8_t* base, uint32_t key_va) {
  std::vector<Override>& list = Overrides();
  if (list.empty()) return nullptr;
  const std::string key = GuestKey(base, key_va);
  if (key.empty()) return nullptr;
  for (Override& o : list)
    if (o.key == key) return &o;
  return nullptr;
}

uint32_t LoadBE(const uint8_t* base, uint32_t va) {
  uint32_t v;
  std::memcpy(&v, base + va, 4);
  return __builtin_bswap32(v);
}
void StoreBE(uint8_t* base, uint32_t va, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + va, &v, 4);
}

thread_local int t_loading = 0;  // inside FSystemSettingsData::LoadFromIni on this thread

enum class Kind { kBool, kInt, kFloat };

// After the game's own read: the listed value into *out. True when it was replaced.
bool Apply(uint8_t* base, uint32_t key_va, uint32_t out_va, Kind kind, bool found) {
  if (!t_loading || out_va < 0x10000) return false;
  Override* o = Find(base, key_va);
  if (!o) return false;
  const uint32_t old = LoadBE(base, out_va);
  uint32_t bits = 0;
  std::string shown_old, shown_new;
  if (kind == Kind::kBool) {
    const std::string v = Lower(o->value);
    bits = (v == "true" || v == "1" || v == "yes" || v == "on") ? 1u : 0u;
    shown_old = old ? "True" : "False";
    shown_new = bits ? "True" : "False";
  } else if (kind == Kind::kInt) {
    bits = uint32_t(int32_t(std::strtol(o->value.c_str(), nullptr, 10)));
    shown_old = std::to_string(int32_t(old));
    shown_new = std::to_string(int32_t(bits));
  } else {
    const float f = std::strtof(o->value.c_str(), nullptr);
    std::memcpy(&bits, &f, 4);
    float fo;
    std::memcpy(&fo, &old, 4);
    shown_old = std::to_string(fo);
    shown_new = std::to_string(f);
  }
  StoreBE(base, out_va, bits);
  if (o->logged++ < 4)
    REXLOG_INFO("[settings] {} = {} (the game's: {}{})", o->key, shown_new, shown_old,
                found ? "" : ", key not in this section");
  return true;
}

}  // namespace

REX_EXTERN(__imp__sub_823BFBE0);
REX_HOOK_RAW(sub_823BFBE0) {  // FSystemSettingsData::LoadFromIni (calls itself for the section it is based on)
  ++t_loading;
  __imp__sub_823BFBE0(ctx, base);
  --t_loading;
}

#define DH_SETTING_GETTER(fn, kind)                                              \
  REX_EXTERN(__imp__##fn);                                                       \
  REX_HOOK_RAW(fn) {                                                             \
    const uint32_t key = ctx.r4.u32, out = ctx.r5.u32;                           \
    __imp__##fn(ctx, base);                                                      \
    if (t_loading && Apply(base, key, out, kind, ctx.r3.u32 != 0)) ctx.r3.u64 = 1; \
  }
DH_SETTING_GETTER(sub_8229F1B0, Kind::kBool)
DH_SETTING_GETTER(sub_8229F050, Kind::kInt)
DH_SETTING_GETTER(sub_8229F100, Kind::kFloat)
#undef DH_SETTING_GETTER
