# ⚙️ Configuration

All settings live in **`dishonored.toml`** next to the NRO (`key = value`, TOML). Start from
[`config/dishonored.toml`](../config/dishonored.toml) and change single lines; every key the build knows is also
accepted on the PC build's command line as `--key=value`. The file's own comments record why each value was chosen
and what was measured.

`app/dishonored_switch.toml` (deployed by the Switch build) is the development copy: the same values with the
profiling report, stack samples, draw-stage timing and the debug overlay on.

## Settings worth knowing

### Paths

| Key | |
| :--- | :--- |
| `game_data_root` | the game folder or its pack (`/switch/dishonored/game_root` → `game_root.nxpack`) |
| `update_data_root` | the title update (`default.xexp`, `DishonoredGame/`) |
| `sa2_dlc_dir` | DLC packs or extracted package folders |
| `user_data_root` | profile, settings and (from hbmenu) saves |
| `log_file` | the log (`/switch/dishonored/dishonored.log`) |

### Game

| Key | |
| :--- | :--- |
| `dh_language` | `auto` (English, or Russian on a console set to Russian; the PC build also follows a Polish, Czech or Hungarian Windows), `english`, `russian`, `polish`, `czech`, `hungarian`, or `disc` (the disc's own choice) |
| `dh_control_scheme_e` | the extra control scheme "Type E" in *Options → Controls* (default on) |
| `dh_switch_button_icons` | Switch button prompts and the Switch controller picture (default on) |
| `dh_hide_dlc_store`, `dh_hide_storage_device`, `dh_hide_leaderboards` | the menu entries a Switch has no use for (default hidden) |
| `input_xbox_layout` | X / Y by position (the game's left button is the Switch's Y); A / B stay by letter |
| `me2_switch_save_title_id` | the forwarder's title ID, whose save data the game uses ([forwarder.md](forwarder.md)) |

### Picture

| Key | |
| :--- | :--- |
| `sa2_render_height_handheld` / `_docked` | 3D scene height: **360, 480, 540, 720** (480 / 540 in the shipped file; the HUD is always 720p) |
| `me2_renut_ui_upscale` | 1 = FXAA + bilinear (the default here), 0 = FXAA + FSR 1 |
| `dh_system_settings` | the game's own `[SystemSettings]` values, `Key=Value,...`, replaced as it loads them (`DetailMode`, `DecalCullDistanceScale`, `ParticleLODBias`, ...); `[settings]` log lines show the game's value |
| `dh_ini_override` | any value of the game's config files, `Section:Key=Value\|...`; `[ini]` log lines show the game's value |
| `me2_renut_upload_budget_kb` | staging a frame for a large new texture: its big levels arrive over the next frames instead of in one long frame (0 = whole textures at once) |

### Performance features

| Key | |
| :--- | :--- |
| `dh_raster_mode` | the CPU occlusion rasterizer: 2 = answers UE3's occlusion queries, 1 = compare with the GPU's answers only, 0 = off |
| `dh_raster_prims` | hidden primitives left out of every pass: 3 = all, 2 = only the classes UE3 never queries, 1 = count only |
| `dh_prepass_skip`, `dh_prepass_no_dominant_shadows` | the world depth prepass is not drawn; the second also drops the sun's dynamic shadows of characters and movable objects, which otherwise force the prepass (baked shadows stay) |
| `dh_cull_small_factor`, `dh_cull_small_min_distance` | size-based draw distance of small static meshes (lower = more culling; remove both for the game's own) |
| `dh_lod_distance_scale` | static meshes pick their LOD as if this much farther away |
| `banjo_deko3d_alpha_spec` | the alpha test as part of the pipeline, so opaque draws get early depth rejection |
| `dh_hot_*`, `me2_hot_guard_period` | the native replacements of hot game functions, and how often each is checked against the original |
| `dh_load_boost` | Horizon's CPU boost mode during loads (forwarder only; off in the shipped file) |
| `switch_audio_core_mask`, `banjo_background_compile_core` | which cores the audio threads and the background shader work use |
| `dh_ab_cycle` | A/B rotation for measuring: turns the named features off in turn, 10 s each, with `[ab]` lines |

### Logs and diagnostics

`log_level`, `switch_profile_report` (the `logs/rex/rex_profile.log` report, see [measuring.md](measuring.md)),
`ui_dev_overlays` (**L + R + D-pad up**: FPS, frame times and a frame graph) and the many `*_diag_*` / `*_trace` /
`*_verify` keys, which are off in the shipped file. Turn one on for a single run when a guide or an issue asks for
it: most cost frame rate.
