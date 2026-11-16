# 🛠️ Building Dunwall

From your Xbox 360 disc to `dishonored.nro` and its shader files. The PC side (code generator, shader conversion)
runs on **Windows**; the Switch NRO is built in **WSL** (Ubuntu 24.04) or on Linux with devkitA64.

> [!IMPORTANT]
> Everything made in these steps from the game (`assets/`, `app/generated/`, the shader folders under `out/`, the
> NRO) stays on your machine. `.gitignore` keeps it out of git: never force-add it.

> [!NOTE]
> This is the maintainer's working flow, written down from the scripts and commands that produced the tested builds.
> The chain has not yet been re-run end to end from a fresh clone; if a step fails for you, please open an issue with
> its log.

## 0. What you need

| | |
| :--- | :--- |
| Windows 10/11 | **Visual Studio 2022 Build Tools** (MSVC libraries + Windows SDK), **LLVM/clang 20**, **CMake** 3.28+, **Ninja**, **Git**, **Python 3.10+** (with Pillow for the icon tools) |
| WSL | **Ubuntu 24.04** with devkitPro (devkitA64, libnx, switch portlibs) and clang 19: `sudo bash tools/nx/setup_wsl.sh` checks and explains |
| Disk | ~50 GB (game ~4 GB, generated C++ ~250 MB, shader work folders ~3 GB, build trees ~20 GB) |
| RAM | 16 GB: keep `JOBS` at 6 or less for the Switch build, the LTO link alone takes ~8 GB |
| Your game | *Dishonored* (Xbox 360) and its **title update** package; DLC packages if you have them |

`tools\win\env.ps1` loads the Windows toolchain into a PowerShell session. It looks in the default install folders;
set `SA2_LLVM`, `SA2_VSDIR`, `SA2_CMAKE_DIR` or `SA2_NINJA_DIR` if yours differ.

If Docker Desktop made itself the default WSL distribution, name yours in every command:
`wsl -d Ubuntu-24.04 bash <script>`.

## 1. Source and third-party code

```sh
git clone https://github.com/VibePorter/Dunwall.git
cd Dunwall
python tools/fetch_thirdparty.py        # sdk/thirdparty: ReXGlue SDK v0.10.0 and its submodules (~400 MB)
python tools/fetch_app_thirdparty.py    # app/thirdparty: plume's and XenosRecomp's submodules at pinned commits
```

The repository keeps only the third-party files this port changed; both scripts add everything else and never
overwrite a tracked file.

## 2. The game files

```sh
python tools/extract_iso.py "<disc>.iso" -o assets/game_root
python tools/stfs_extract.py "<title update package>" out/tu
python tools/stfs_extract.py "<DLC package>" assets/dlc/<package file name>      # each DLC, optional
```

Copy the update's `default.xexp` and its `DishonoredGame` folder to `assets/update/`, and `default.xexp` also next
to `assets/game_root/default.xex` (the code generator applies the sibling patch). The PC build reads
`assets/game_root`, `assets/update` and `assets/dlc` directly.

## 3. The code generator and the game's C++

```powershell
. tools\win\env.ps1
cd sdk
cmake --preset win-amd64
cmake --build out/build/win-amd64 --config Release --target rexglue
cd ..
powershell -NoProfile -ExecutionPolicy Bypass -File tools\codegen.ps1 -Flavor optimised
```

`codegen.ps1` translates `assets\game_root\default.xex` into `app\generated\` (about 74,000 functions), then runs the
post-codegen passes of `tools\post_codegen.py` (registers as locals, direct calls, store cards, watch marks, AArch64
rewrites, ...). It also writes `out\default_image.bin`, the patched image, which the analysis tools read. The log is
`out\codegen.log`.

`app/sa2_manifest.toml` and `app/overrides.toml` describe the program; `app/perf_overrides.toml` holds the optimised
flavour's options and its keep list (56 exception funclets and a few special functions, found by the audits of
`tools/non_argument_audit.py` and `tools/read_before_write.py`).

> After adding a hook (`REX_HOOK*`) for a function the optimised code calls directly, run
> `python tools/direct_calls.py --redo` (or a new codegen): otherwise the callers skip the hook.

## 4. Shaders

The renderer draws with the game's own shaders, converted ahead of time. Four stages:

```text
your game ──collect──▶ ~49,500 containers ──XenosRecomp──▶ SPIR-V + shader_cache.cpp ──SPIRV-Cross/uam──▶ GLSL + DKSH
```

**4a. Collect the containers** (WSL):

```sh
CXX=clang++-19 CC=clang-19 bash tools/deko/collect_shaders.sh assets/game_root out/shader_containers assets/dlc
```

Dishonored's packages are XMem LZX, and the material shaders are LZX-compressed a second time inside them (UE3's
compressed shader code cache); `tools/deko/ue3_shader_scan.cpp` handles both. The title update changes a few shaders
of `Engine.xxx`: its `Patches/*.bs` files are BSDIFF patches of the **decompressed** packages. To include those
shaders, decompress the patched packages (`SCAN_DUMP_PLAIN=<dir>` of the scanner, or `tools/deko/ue3_decompress.cpp`),
apply the patches with `tools/deko/bspatch.py`, and run the scanner over the result as one more input folder.

Any build of the port also writes a shader missing from its table to `shader_capture/` next to the program; copy
such `.bin` files into `out/shader_containers` and run 4c and 4d again. With the full collection the tested builds
report none missing.

**4b. Build the tools** (Windows): SPIRV-Cross, XenosRecomp (Renut flavour) and uam.

```powershell
git clone --depth 1 --branch vulkan-sdk-1.4.304.0 https://github.com/KhronosGroup/SPIRV-Cross.git `
    out\deko3d_tools\SPIRV-Cross-vulkan-sdk-1.4.304.0
Set-Content out\deko3d_tools\spirv-cross-revision.txt 'vulkan-sdk-1.4.304.0'
powershell -File tools\win\build_deko_shader_tools.ps1
powershell -File tools\win\build_uam.ps1
```

**4c. Translate** (Windows): every container to SPIR-V and the renderer's shader table.

```powershell
. tools\win\env.ps1
$env:BANJO_DEKO3D_SPIRV_DIR = "$PWD\out\shaders_xenos\spirv"
out\deko3d_tools\xenos-build\XenosRecomp\XenosRecomp.exe out\shader_containers `
    out\shaders_xenos\shader_cache.cpp app\thirdparty\reblue-XenosRecomp\XenosRecomp\shader_common.h out\shaders_xenos\hlsl
```

(The PC build makes the same translator at `out\pc\xenos_recomp\build\XenosRecomp\XenosRecomp.exe`; the tested
builds used that one.)

**4d. GLSL and DKSH** (Windows):

```powershell
python tools\deko3d_shaders.py --spirv out\shaders_xenos\spirv --out out\dishonored-deko-shaders\conversion --jobs 8
$env:DEKO_SHADERS = "out\dishonored-deko-shaders"
python tools\me2_scale34_patch.py out\dishonored-deko-shaders\scale_list.txt
```

`--reuse` on a later run keeps the finished shaders and compiles only the changed ones. The last line patches the few
shaders that read the render-scale code, so that 480p and 540p work on deko3d; the Switch build refuses a conversion
without it.

**4e. Variants the console needs.** `deko3d_shaders.bin` holds precompiled default variants; the console compiles the
specialised ones it meets and appends them, and records every pipeline in `pipeline_list.bin`. To ship a cache that
already has them, take the list from a console (or PC) session:

```sh
python tools/deko3d_variant_cache.py --list pipeline_list.bin --glsl <the GLSL folder> --merge <old deko3d_shaders.bin> \
    --out out/deko3d_shaders_new.bin --jobs 12
python tools/deko3d_cache_index.py out/deko3d_shaders_new.bin
```

## 5. The Switch NRO (WSL)

```sh
sudo bash tools/nx/setup_wsl.sh            # once
export SHADER_CACHE_CPP=/mnt/c/<path>/Dunwall/out/shaders_xenos/shader_cache.cpp
export GLSL_DIR=/mnt/c/<path>/Dunwall/out/dishonored-deko-shaders/conversion/glsl
export DKSH_DIR=/mnt/c/<path>/Dunwall/out/dishonored-deko-shaders/conversion/dksh
bash tools/nx/build_switch.sh              # sync, tools, shaders, configure, build
```

The repository is copied to `~/dishonored-build` first (`BUILD_ROOT`). The result is in `out/sd/switch/dishonored/`:
`dishonored.nro`, `dishonored.toml` (from `app/dishonored_switch.toml`), `deko3d_glsl.bin`, `deko3d_shaders.bin` and
its `.idx`. The unstripped ELF of every build is kept in `out/deployments/<sha256 of the NRO>/dishonored.elf` for
crash stacks.

| | |
| :--- | :--- |
| `DEV=1 bash tools/nx/build_switch.sh build` | no LTO: a one-file change links in minutes instead of ~12 |
| `bash tools/nx/build_switch.sh sync build` | after a source change (the shaders step is only needed when shaders change) |
| `PGO=generate` / `PGO=use` | profile-guided builds, see [pgo.md](pgo.md) |
| `JOBS=4 LTO_JOBS=6` | less memory |

Check the NRO's time and the `nro_sha256` line of the output, not only the exit line. On an existing build tree leave
the `configure` step out: ninja re-runs CMake by itself when a source file is added.

## 6. The PC build (optional)

The same app builds for Windows (Vulkan through plume), handy for debugging with RenderDoc:

```powershell
. tools\win\env.ps1
cd app
cmake --preset win-pc -DPORT_GAME=dishonored -DMGR_RENUT_RENDERER=ON -DMGR_NATIVE_RENDERER=OFF `
    -DMGR_RENUT_PREBUILT_SHADER_CACHE=OFF -DRENUT_SHADER_DUMP_DIR="$PWD\..\out\shader_containers" `
    -DRENUT_SHADER_MAX_FAILURES=8
cd ..
cmake --build out\pc --target sa2
run_dishonored_pc.bat            # or: run_dishonored_pc.bat 360   (the scene at 360p, as a small Switch picture)
```

Notes from the maintainer's runs:

- The first build after touching `app/dishonored_src` can fail with "`sa2_pch.h` has been modified since the
  precompiled header": build again.
- An SDK change that leaves the import library unchanged does not relink `sa2.exe`, so its post-build step does not
  copy `sdk\out\win-amd64\rexruntimerd.dll` to `out\pc`: copy it by hand.
- `run_dishonored_pc_renderdoc.bat` starts the game under RenderDoc; `tools/win/rdc_*.py` read the captures.

## When the game changes

A different `default.xex` / `default.xexp` means different addresses. The Direct3D hooks are checked at start-up
against instruction "witnesses" (`app/src/renut_engine/gpu/d3d/me2_hook_map.h`, written by
`tools/dishonored_renut_hooks.py` from `tools/d3d/dishonored_d3d_funcs.toml`) and the log reports any mismatch; the
game hooks in `app/dishonored_src` are plain addresses and have to be found again
([porting-notes.md](porting-notes.md) says what each one is).
