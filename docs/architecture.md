# 🧭 How Dunwall works

```text
 default.xex + .xexp ──▶ ReXGlue code generator ──▶ app/generated (C++ of every guest function)
                                                          │
           app/dishonored_src (game hooks, natives) ──────┤
           app/src/renut_engine (renderer) ───────────────┤──▶ devkitA64 + LTO + PGO ──▶ dishonored.nro
           sdk/ (Xbox 360 kernel, XAM, audio, input, FS) ─┘
                                                                 at run time
 game shaders ──▶ XenosRecomp ──▶ SPIR-V ──▶ GLSL / DKSH ──────▶ deko3d (Switch GPU)
```

## 1. The CPU side: static recompilation

The game is a PowerPC (Xenon) program. The [ReXGlue](https://github.com/rexglue/rexglue-sdk) code generator turns
every function of `default.xex` into a C++ function `sub_<address>(ctx, base)`: guest registers in `ctx`, guest memory
at `base` (big-endian, byte-swapped on access). `app/sa2_manifest.toml` and `app/overrides.toml` tell it where the
functions are where its own analysis is unsure; `app/perf_overrides.toml` holds the optimised flavour's options, and
`tools/post_codegen.py` rewrites the result further (registers as locals, direct calls, AArch64-friendly vector code).

Each generated function is a **weak** symbol. Defining `REX_HOOK_RAW(sub_X)` anywhere replaces it at link time and
`__imp__sub_X` still calls the original: that is how every hook and native in the port works.

> The names tell the tree's history: "sa2" (`sa2_recomp.*.cpp`, the CMake target) comes from sa2-nx, "mgr" from
> mgr-nx-deko, "banjo" from banjo-nx and "me2" from Lazarus. They were never renamed, to keep the scripts working;
> a setting called `me2_renut_...` is a renderer setting that Lazarus introduced, not a Mass Effect one.

## 2. The runtime: `sdk/`

ReXGlue's runtime with Xenia's Xbox 360 kernel emulation (`xboxkrnl`, `xam`), a virtual file system (asset packs,
the title update, DLC packages), XMA audio decoding (FFmpeg), input, threads, and the **Horizon layer** for the
Switch (guest memory views, exceptions, cores and priorities, libnx input, audio output, the GPU channel wrappers).
Changes against upstream are described in `patches/`.

## 3. The GPU side: Renut on deko3d

The Xbox 360 GPU is never emulated. **Renut** (`app/src/renut_engine`) hooks 35 functions of the game's own XDK
Direct3D (create device, draws, shader binds, resolves, swap, fences; [d3d-map.md](d3d-map.md)), keeps the D3D state
from the device the game fills in, and replays each frame through [plume](https://github.com/renderbag/plume) on
**deko3d**:

- `gpu/d3d/`: the hooks and the device layout (`me2_hook_map.h` checks each hooked function's instructions at start);
- `gpu/render/`: draws, surfaces, textures (untiling, formats, gamma, exponent adjust), resolves, memory exports,
  the pipeline list, the EDRAM aliases UE3 relies on;
- `gpu/backend/`: frame recording on a render thread fed through a lock-free ring, uploads, presentation, the final
  FXAA / upscale pass;
- `banjo_renut_deko3d.cpp`: shader packages, DKSH variants (compiled with uam when a specialisation is new);
- `app/thirdparty/plume/plume_deko3d*.cpp`: plume's deko3d back end (memory blocks, image heaps, command lists).

Shaders are converted ahead of time (Xenos microcode → HLSL → SPIR-V by XenosRecomp, → GLSL by SPIRV-Cross, → DKSH by
uam) because ~49,500 of them are far too many to compile on the console.

## 4. Game-specific work: `app/dishonored_src`

| File | |
| :--- | :--- |
| `dh_hooks.cpp` | shared hooks: swap, draws, shader constant blocks, core layout, RenderDoc capture, shader trace |
| `dh_occlusion.cpp` | UE3's occlusion query batches mapped to host queries (begin, end, read) and their history |
| `dh_occlusion_raster.cpp` | the CPU occlusion rasterizer (256x144 depth from the prepass lists, on a worker), the primitive cull, the prepass skip |
| `dh_cull_distance.cpp` | size-based draw distance and the LOD distance scale |
| `dh_hot.cpp`, `dh_hot_shadow.cpp`, `hot/` | **natives**: hand-written C++ for hot guest functions (constant setters, `memcpy`, string functions, LZX decompression, Wwise reverb and delay, a set lookup), each run side by side with the original and compared |
| `dh_settings.cpp`, `dh_ini_override.cpp` | the game's `[SystemSettings]` and config values replaced from the toml |
| `dh_language.cpp` | the language choice and the shared English voice files |
| `dh_button_icons.cpp`, `icons/` | Switch button prompts and the controller picture (the maintainer's own art, as texture blocks) |
| `dh_control_scheme.cpp` | the control scheme "Type E" |
| `dh_main_menu.cpp` | menu clean-ups: the DLC store button, the storage device row, the leaderboards (Scaleform bytecode patched as packages decompress) |
| `dh_load_boost.cpp` | the CPU boost mode during loads |
| `dh_ab_cycle.cpp` | the A/B rotation used for measuring |
| `dh_stubs.cpp` | what the shared application files expect from a game |

`app/me2_src` is Lazarus's game folder. Dunwall builds four of its files (`me2_main.cpp`: start-up and the built-in
configuration; `me2_switch_save.cpp`: Switch user saves; `me2_write_census.cpp`; `hot/me_hot_guest.cpp`: the native
framework); the rest is Mass Effect 2's and is kept so the two trees stay easy to compare (`-DPORT_GAME=dishonored`
is the default and leaves it out).

## 5. Folders

| Folder | |
| :--- | :--- |
| `app/` | the application: CMake, game hooks, renderer, codegen configuration |
| `sdk/` | the ReXGlue SDK fork (BSD-3-Clause) |
| `shaders/` | XenosRecomp fork and the shader library tools of the native (reference) renderer |
| `tools/` | codegen, post-codegen passes, shader conversion, analysis; `nx/` = Switch build, `win/` = Windows and RenderDoc, `deko/` = shader collection and UE3 package tools, `d3d/` = Direct3D matching, `rhi/` = device census; `dh_*.py` = Dishonored's packages, textures, icons and Scaleform bytecode |
| `config/` | the settings file for play |
| `forwarder/` | the NSP forwarder's packer |
| `patches/` | third-party changes as diffs against their upstreams |
| `docs/` | these guides |
