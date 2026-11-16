// Dishonored - the button prompts with the Switch's buttons.
//
// The game draws every button prompt from the shared Scaleform library's textures (Startup: Common_assets.lib_*,
// 40x40 / 52x52 / 40x44 DXT5) and the loading screens' own "X360_A" (UI_Loading*_SF). Renut's ReadLevel
// (render/textures.cpp, g_texture_replace_blocks) hands every small block-compressed 2D level here untiled, in the
// guest's byte order; one whose FNV-1a 64 hash is a known icon's gets its Switch replacement in the same layout.
// The table: tools/dh_button_icons.py from the user's art sheet (dishonored_src/icons/switch_icons_sheet.png); the
// mapping, as ME2's: A and B by letter, X and Y by position, LB/RB/LT/RT -> L/R/ZL/ZR, Back/Start -> -/+, face buttons
// in the Super Famicom's colours (A red, B yellow, X blue, Y green). 2026-11-11: the D-pad (lib_ID/IC/IE/I10/IF) as the
// Joy-Con's four buttons (icons/dpad, the user's pictures), also the base of the shared library's sprite
// lib_DPAD_Multi_b (a region of the 512x512 atlas lib_IAA; the controller screen's D-pad).
// 2026-11-11: also the options' controller picture (UI_GamepadMapping.Options_GamepadMapping_Xenon, 840x600 DXT5):
// the user's Switch picture with lines to the game's labels (tools/dh_gamepad_screen.py, icons/dh_gamepad_screen_data.inc).
//
// dh_switch_button_icons (default on): the Switch icons. Off: the game's.

#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>

REXCVAR_DEFINE_BOOL(dh_switch_button_icons, true, "Dishonored",
                    "Button prompts with the Switch's buttons (the game's Xbox 360 icons replaced as they are loaded)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu {
extern bool (*g_texture_replace_blocks)(uint32_t block_bytes, uint32_t blocks_w, uint32_t blocks_h,
                                        uint8_t* guest_blocks);
extern uint32_t g_texture_replace_blocks_extra[4][2];
}

namespace {

struct IconReplacement {
  uint64_t hash;
  uint32_t blocks_w, blocks_h;
  const uint8_t* blocks;
  const char* name;
};

// A replacement of part of a texture: only the region's blocks are stored, the rest stays the game's.
struct AtlasPatch {
  uint64_t hash;
  uint32_t blocks_w, blocks_h;  // the whole texture
  uint32_t x, y, w, h;          // the region, in blocks
  const uint8_t* blocks;        // w * h blocks, row by row
  const char* name;
};

#include "icons/dh_button_icons_data.inc"
#include "icons/dh_gamepad_screen_data.inc"

uint64_t Fnv1a64(const uint8_t* p, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001B3ull;
  }
  return h;
}

std::atomic<uint32_t> g_logged{0};

bool Replace(uint32_t block_bytes, uint32_t blocks_w, uint32_t blocks_h, uint8_t* blocks) {
  if (block_bytes != 16) return false;  // DXT5 (DXT2-5) only: the icons'
  // Only the sizes the icons have (40x40, 52x52, 40x44), the controller picture's (840x600) and the atlas' (512x512).
  const bool icon = (blocks_w == 10 && (blocks_h == 10 || blocks_h == 11)) || (blocks_w == 13 && blocks_h == 13);
  const bool screen = blocks_w == kScreens[0].blocks_w && blocks_h == kScreens[0].blocks_h;
  const bool atlas = blocks_w == kAtlases[0].blocks_w && blocks_h == kAtlases[0].blocks_h;
  if (!icon && !screen && !atlas) return false;
  const size_t bytes = size_t(blocks_w) * blocks_h * 16;
  // 512x512 DXT5 is a common size: its first 4 KB first
  if (atlas && !screen && Fnv1a64(blocks, 4096) != kAtlasPrefixHash) return false;
  const uint64_t hash = Fnv1a64(blocks, bytes);
  auto try_table = [&](const IconReplacement* table, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      const IconReplacement& r = table[i];
      if (r.hash != hash || r.blocks_w != blocks_w || r.blocks_h != blocks_h) continue;
      std::memcpy(blocks, r.blocks, bytes);
      if (g_logged.fetch_add(1, std::memory_order_relaxed) < 64)
        REXLOG_INFO("[icons] {}", r.name);
      return true;
    }
    return false;
  };
  if (icon) return try_table(kIcons, std::size(kIcons));
  if (screen && try_table(kScreens, std::size(kScreens))) return true;
  if (!atlas) return false;
  for (const AtlasPatch& r : kAtlases) {
    if (r.hash != hash || r.blocks_w != blocks_w || r.blocks_h != blocks_h) continue;
    for (uint32_t row = 0; row < r.h; ++row) {
      std::memcpy(blocks + (size_t(r.y + row) * blocks_w + r.x) * 16, r.blocks + size_t(row) * r.w * 16,
                  size_t(r.w) * 16);
    }
    if (g_logged.fetch_add(1, std::memory_order_relaxed) < 64)
      REXLOG_INFO("[icons] {}", r.name);
    return true;
  }
  return false;
}

struct Install {
  Install() {
    renut::gpu::g_texture_replace_blocks_extra[0][0] = kScreens[0].blocks_w;
    renut::gpu::g_texture_replace_blocks_extra[0][1] = kScreens[0].blocks_h;
    renut::gpu::g_texture_replace_blocks_extra[1][0] = kAtlases[0].blocks_w;
    renut::gpu::g_texture_replace_blocks_extra[1][1] = kAtlases[0].blocks_h;
    renut::gpu::g_texture_replace_blocks = [](uint32_t bb, uint32_t w, uint32_t h, uint8_t* b) {
      static const bool on = REXCVAR_GET(dh_switch_button_icons);
      return on && Replace(bb, w, h, b);
    };
  }
} g_install;

}  // namespace
