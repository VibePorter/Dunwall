// Dishonored (2026-11-09): the game's language (dh_language).
//
// This disc is the Eastern European release: text in English (INT: Coalesced_INT.bin, Xbox360TOC.txt, the _LOC_INT
// packages), Russian, Polish, Czech and Hungarian; English voices only.
// The game picks its language in UE3's appGetLanguageExt (sub_827713E8): the console language and country (ExGetXConfigSetting 3/9 and 3/14) through its table, then the result must be listed
// in CookedXbox360\COOKEDLANGUAGES (RUS, CZE, HUN, POL); otherwise CookedXbox360\DEFAULTLANGUAGE (RUS) is used. So
// English is never picked on its own. The hook lets the game choose, then replaces the three letters in the returned
// FString and in the function's cached copy (FString at 0x83523858: data, count with the terminator, capacity).
// The voice banks (Bk_<id>.RUS, English speech: the disc has no Russian dub) are on the disc as .RUS only, so with
// another language their names open as .RUS (VirtualFileSystem::SetExtensionFallback).
// 2026-11-21: a dub (Russian, Zarudnev_voice_banks) is an overlay of those .RUS files, independent of the text language
// (game_overlay_root + dlc_overlay_root, tools/build_voice_overlay.py). dh_voice = "russian" sets both to
// <game data folder's parent>/voice_ru/game and .../dlc (ApplyVoice, before the runtime mounts the game data).
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/pack_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ppc.h>
#include <rex/runtime.h>

#if defined(__SWITCH__)
#include <switch.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_STRING(dh_language, "auto", "Dishonored",
                      "Game language: auto (English, or Russian / Polish / Czech / Hungarian when that is the system "
                      "language), english, russian, polish, czech, hungarian, or disc (the disc's own choice: Russian)");

REXCVAR_DEFINE_STRING(dh_voice, "english", "Dishonored",
                      "Voices: english (the disc's) or russian (the dub: voice_ru/ beside the game data folder, "
                      "tools/build_voice_overlay.py). Independent of dh_language. Read at startup")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DECLARE(std::string, game_overlay_root);  // system/runtime.cpp
REXCVAR_DECLARE(std::string, dlc_overlay_root);   // system/xam/content_manager.cpp

namespace {

constexpr uint32_t kCachedLanguage = 0x83523858u;  // appGetLanguageExt's static FString

uint8_t* HostAt(uint8_t* base, uint32_t a) {
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)
  return base + a + (a >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + a;
#endif
}
uint32_t LoadBE(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, HostAt(base, a), 4);
  return __builtin_bswap32(v);
}

// The system language as one of the disc's languages (INT when it has none of them).
const char* SystemLanguage() {
#if defined(__SWITCH__)
  // The Switch has no Polish, Czech or Hungarian system language.
  const char* ext = "INT";
  if (R_SUCCEEDED(setInitialize())) {
    u64 code = 0;
    SetLanguage language = SetLanguage_ENUS;
    if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &language)) &&
        language == SetLanguage_RU) {
      ext = "RUS";
    }
    setExit();
  }
  return ext;
#elif defined(_WIN32)
  switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_RUSSIAN: return "RUS";
    case LANG_POLISH: return "POL";
    case LANG_CZECH: return "CZE";
    case LANG_HUNGARIAN: return "HUN";
    default: return "INT";
  }
#else
  return "INT";
#endif
}

// nullptr = keep the game's choice.
const char* WantedLanguage() {
  static const char* wanted = [] {
    std::string name = REXCVAR_GET(dh_language);
    std::erase(name, '"');
    for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (name == "disc") return static_cast<const char*>(nullptr);
    if (name == "english") return "INT";
    if (name == "russian") return "RUS";
    if (name == "polish") return "POL";
    if (name == "czech") return "CZE";
    if (name == "hungarian") return "HUN";
    if (name != "auto") REXLOG_WARN("[dh-language] dh_language = \"{}\" is not known: auto is used", name);
    return SystemLanguage();
  }();
  return wanted;
}

// Writes the three letters into an FString holding a three-letter language (data, count 4 with the terminator).
bool Replace(uint8_t* base, uint32_t fstring, const char* ext, std::string* old) {
  const uint32_t data = LoadBE(base, fstring), count = LoadBE(base, fstring + 4), max = LoadBE(base, fstring + 8);
  if (!data || count != 4 || max < 4) return false;
  uint8_t* p = HostAt(base, data);
  if (old) {
    for (int i = 0; i < 3; ++i) old->push_back(char(p[i * 2 + 1]));
  }
  for (int i = 0; i < 3; ++i) {
    p[i * 2] = 0;
    p[i * 2 + 1] = uint8_t(ext[i]);
  }
  return true;
}

}  // namespace

REX_EXTERN(__imp__sub_827713E8);
REX_HOOK_RAW(sub_827713E8) {  // appGetLanguageExt(FString* out)
  const uint32_t out = ctx.r3.u32;
  __imp__sub_827713E8(ctx, base);
  const char* ext = WantedLanguage();
  if (!ext) return;
  std::string old;
  const bool replaced = Replace(base, out, ext, &old);
  Replace(base, kCachedLanguage, ext, nullptr);
  static bool logged = false;
  if (!logged) {
    logged = true;
    // The voices: the disc's Wwise voice banks (Bk_<id>.RUS, AKPK audio only, English speech; also in the DLC) exist
    // under the RUS extension alone. Another language opens them under its own (Bk_<id>.INT ...): those open as .RUS.
    if (std::strcmp(ext, "RUS") != 0) {
      if (auto* runtime = rex::Runtime::instance(); runtime && runtime->file_system()) {
        runtime->file_system()->SetExtensionFallback(ext, "RUS");
      }
    }
    if (replaced) {
      REXLOG_INFO("[dh-language] the disc picked {}, using {} (dh_language = {})", old, ext,
                  REXCVAR_GET(dh_language));
    } else {
      REXLOG_WARN("[dh-language] the game's language string has an unexpected shape: left as it is");
    }
  }
}

namespace dh::language {

// dh_voice (2026-11-21): the dub's overlays as game_overlay_root and dlc_overlay_root, unless the toml sets those
// itself. Called before the runtime mounts the game data (OnPreSetup).
void ApplyVoice(const std::filesystem::path& game_data_root) {
  std::string voice = REXCVAR_GET(dh_voice);
  std::erase(voice, '"');
  for (char& c : voice) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (voice.empty() || voice == "english") {
    REXLOG_INFO("[dh-voice] English voices (the disc's)");
    return;
  }
  if (voice != "russian") {
    REXLOG_WARN("[dh-voice] dh_voice = \"{}\" is not known (english, russian): English voices", voice);
    return;
  }
  const std::filesystem::path root = std::filesystem::absolute(game_data_root).parent_path() / "voice_ru";
  const auto usable = [](const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec) || rex::filesystem::PackDevice::Exists(p);
  };
  if (!usable(root / "game")) {
    REXLOG_ERROR("[dh-voice] dh_voice = \"russian\", but {} has no game.nxpack (or game folder): English voices",
                 rex::path_to_utf8(root));
    return;
  }
  const auto set_unless_given = [](const char* name, std::string& storage, const std::filesystem::path& value) {
    std::string given = storage;
    std::erase(given, '"');
    if (!given.empty()) {
      REXLOG_INFO("[dh-voice] {} is set in the config ({}): left as it is", name, given);
      return;
    }
    storage = rex::path_to_utf8(value);
  };
  set_unless_given("game_overlay_root", FLAGS_game_overlay_root_storage_(), root / "game");
  // The DLC overlays are optional (dlc/<package>.nxpack): a package without one keeps its English voices.
  set_unless_given("dlc_overlay_root", FLAGS_dlc_overlay_root_storage_(), root / "dlc");
  REXLOG_INFO("[dh-voice] Russian voices from {}", rex::path_to_utf8(root));
}

}  // namespace dh::language
