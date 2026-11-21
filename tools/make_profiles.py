"""Dishonored Switch profiles (2026-11-16), laid out as Lazarus' release profiles (2026-11-21):
English / Russian / Russian VO x Performance (FPS) / Fidelity (Quality).

    python tools/make_profiles.py [out_dir]     (default config/profiles)

Writes <out>/<variant>/<profile>/dishonored.toml. One template, so the six differ only in the values below.
English = English text and voices; Russian = Russian text, English voices; Russian VO = Russian text and the Russian
dub (dh_voice, needs voice_ru/ beside the NRO).
Performance: tuned for stock clocks. Fidelity: for the overclock Lazarus' fidelity profile pairs with
(CPU 1785 / GPU ~920-1000 / memory 1600 MHz, set in sys-clk; a .toml cannot set clocks).
"""
import sys
from pathlib import Path

PROFILES = {
    'Performance (FPS)': dict(
        title='PERFORMANCE profile. Tuned for STOCK clocks (handheld CPU 1020 / GPU 460.8, docked CPU 1020 / GPU 768 '
              'MHz):\n# no overclock needed.',
        handheld=480, docked=540,
        handheld_note='853x480 (console-tested 2026-11-13: GPU-bound only in the heaviest spots)',
        docked_note='960x540 on the docked GPU clock (768 MHz)',
        cull_factor=50, cull_min=900,
        lod_scale='1.25',
        no_dominant_shadows='true',
        system_settings='DetailMode=1,DecalCullDistanceScale=0.5,ParticleLODBias=1',
    ),
    'Fidelity (Quality)': dict(
        title='FIDELITY profile. Pair with clocks CPU 1785 / GPU ~920-1000 / memory 1600 MHz (set them in sys-clk /\n'
              '# your overclock tool; this file cannot set clocks). At stock clocks use the performance profile.',
        handheld=540, docked=720,
        handheld_note='960x540 + FXAA, upscaled to the 720p HUD (the overclocked GPU has ~2x the stock handheld one)',
        docked_note='native 1280x720 (no upscale)',
        cull_factor=70, cull_min=1200,
        lod_scale='1.0',
        no_dominant_shadows='false',
        system_settings='',
    ),
}

# Folder: (dh_language, text language's name, dh_voice, voices' name).
VARIANTS = {
    'English': ('english', 'English', 'english', 'English'),
    'Russian': ('russian', 'Russian', 'english', 'English'),
    # Voices (2026-11-21): the dub (Zarudnev_voice_banks, tools/build_voice_overlay.py) over the game's English banks.
    'Russian VO': ('russian', 'Russian', 'russian', 'Russian (dub)'),
}

TEMPLATE = """\
# Dishonored deko3d - {title}
# Game language: {lang_name} (dh_language), voices: {voice_name}. Diagnostics off: no CPU profile report, stack sampling, draw-stage timing
# or periodic report lines in dishonored.log (the frame-rate line, saves, audio dropouts, warnings and errors stay);
# no ImGui (debug overlay, settings menu, log console, achievement toasts).
# Copy this file over sdmc:/switch/dishonored/dishonored.toml to use it.

sa2_renderer = "renut"
renut_gpu_backend = "deko3d"
license_mask = 0xFFFFFFFF
input_xbox_layout = true
# Game language: auto (English; Russian when the Switch is set to Russian), english, russian, polish, czech,
# hungarian, or disc (this Eastern European disc's own choice, always Russian).
dh_language = "{lang}"
# Voices: english (the disc's) or russian (the dub, game and DLC; needs voice_ru/ beside the NRO: game.nxpack/.nxidx
# and dlc/<package>.nxpack/.nxidx). Independent of dh_language. Without voice_ru/ the voices stay English.
dh_voice = "{voice}"

# Gyro aiming (the console's or controller's gyroscope adds to the right stick). Options -> Controls has rows for it;
# what is chosen there is saved in userdata/dishonored_gyro.cfg and wins over these lines.
# input_gyro: 0 = off, 1 = while the Power/Gadget button is held (ZL in Type A/C/E, L in Type B, R in Type D),
# 2 = always. input_gyro_sensitivity 0.5 .. 10 (in game, L+R+X raises it and L+R+Y lowers it by 0.5).
input_gyro = 1
input_gyro_sensitivity = 3.0
input_gyro_invert_x = true
input_gyro_invert_y = true

# Game data beside the NRO: game_root.nxpack/.nxidx, the title update in update/, the DLC packages in dlc/.
game_data_root = "/switch/dishonored/game_root"
update_data_root = "/switch/dishonored/update"
sa2_dlc_dir = "/switch/dishonored/dlc"
user_data_root = "/switch/dishonored/userdata"
# Saves in the console's per-user save data when started from the HOME-menu forwarder; from hbmenu on the SD card.
me2_switch_save_title_id = "0144534844000000"

# Frame rate. me2_fps_limit: -1 = the game's own 30 fps (default), 0 = the display's refresh rate (60), N = at most
# N fps; read every frame. me2_unlock_framerate = true removes the cap altogether (read at startup). Above 30 fps is
# untested in this game.
# me2_fps_limit = 60
# me2_unlock_framerate = true

# Resolution (Renut takes 360, 480, 540, 720); the HUD and menus render at 720p.
sa2_render_height_handheld = {handheld}   # {handheld_note}
sa2_render_height_docked = {docked}   # {docked_note}

# Draw distance and detail.
# Small static meshes are not drawn beyond factor x their radius (at least min_distance units; the game's own: remove
# both lines). Lower = fewer draws, more pop-in of small props.
dh_cull_small_factor = {cull_factor}
dh_cull_small_min_distance = {cull_min}
# Static meshes pick LOD and draw distance as if this many times farther away (1.0 = the game's own).
dh_lod_distance_scale = {lod_scale}
# UE3's world depth prepass is skipped where it is not needed for shadows.
dh_prepass_skip = true
# true: the sun's dynamic shadows of characters / movable objects are not drawn (baked shadows stay), so the prepass
# skip applies everywhere. false: the game's shadows.
dh_prepass_no_dominant_shadows = {no_dominant_shadows}
# The game's [SystemSettings] replaced as it loads them; empty = the 360's own (DetailMode=2, full decals, particles).
dh_system_settings = "{system_settings}"
# UE3 garbage collection 4x less often (a dip every 4-5 min instead of every minute).
dh_ini_override = "Engine.Engine:NumberOfAllocsBetweenGC=65536|Engine.Engine:TotalAllocSizeBetweenGC=4194304"

# Renderer (as console-tested).
banjo_deko3d_alpha_spec = true
me2_renut_upload_budget_kb = 4096
dh_renut_export_merge = true
banjo_renut_store_cards_verify = false
# Texture uploads batched into one companion list per command list; readable guest ranges remembered (2026-11-21).
renut_batched_uploads = true
renut_readable_memo = true
# CPU occlusion rasterizer answers UE3's occlusion queries; hidden primitives are left out of every pass.
dh_raster_mode = 2
dh_raster_prims = 3
sa2_renut_wake_batch = 64
sa2_renut_ring_spin_us = 0
kernel_critsec_spin = 150
me2_hot_guard_period = 1048576
sa2_io_ranges_min_kb = 128

# Cores: audio threads on cores 2-3, background shader compiles on core 3 (needs the forwarder for core 3).
switch_audio_core_mask = 0xC
banjo_background_compile_core = 3
# Horizon's CPU boost during loads: off (user's choice).
dh_load_boost = false

# No ImGui in game (debug overlay, settings menu, log console, achievement overlay / toasts).
ui_dev_overlays = false

# Logging: dishonored.log keeps start-up lines, the frame-rate report, saves, audio dropouts, warnings and errors.
log_file = "/switch/dishonored/dishonored.log"
log_level = "info"
log_async = true
log_flush_interval = 2
log_quiet_prefixes = "{quiet}"
# Diagnostics off: the CPU profile report (rex_profile.log), CPU stack samples, draw-stage timing, "[enh] uses".
switch_profile_report = false
switch_profile_stacks = false
renut_draw_stage_timing = false
sa2_enhancements_report_s = 0
"""

# Periodic report lines of a console log (logs/new/dishonored.log, 2026-11-14), each tag 100+ lines a session.
QUIET = ','.join([
    '![renut] 10.', '![renut] game thread',
    '[renut]', '[hot]', '[io]', '[me2-present]', '[latency]', '[display]', '[color-handover]', '[prepass]',
    '[dh-raster]', '[dh-occlusion]', '[xma]', '[resolve-pool]', '[cull]', '[cpu-copy]', '[tex-stream]', '[export]',
    '[sparse]', '[prio]', '[NtCreateFile]', '[icons]', '[save] XamContentCreate', '[save] open',
    '[audio] the game is',
])


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else 'config/profiles')
    for variant, (lang, lang_name, voice, voice_name) in VARIANTS.items():
        for name, p in PROFILES.items():
            path = out / variant / name / 'dishonored.toml'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(TEMPLATE.format(lang=lang, lang_name=lang_name, voice_name=voice_name, voice=voice,
                                            quiet=QUIET, **p), encoding='utf-8', newline='\n')
            print(path)


if __name__ == '__main__':
    main()
