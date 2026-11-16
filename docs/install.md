# 📦 Installing

What goes on the SD card, where it comes from, and how to start the game. Every file that comes from the game has to
be made from **your own copy**; this repository has none of them ([building.md](building.md) makes them).

## What you need

- A Switch that runs homebrew (Atmosphère), with about **6 GB** free on the SD card (the game pack ~3.8 GB, shader
  files ~270 MB, the title update and DLC on top). The game pack is under 4 GB, so FAT32 works.
- Your Xbox 360 copy of *Dishonored* (one disc) and its **title update** package (`TU_...`).
- Optional: your DLC packages.
- The build output: `dishonored.nro`, `deko3d_glsl.bin`, `deko3d_shaders.bin`, `deko3d_shaders.bin.idx`.

## SD card layout

The port expects the folder **`sdmc:/switch/dishonored/`** (the settings file names it; change the paths in
`dishonored.toml` if you use another one).

```text
sdmc:/switch/dishonored/
  ├── dishonored.nro                   the game
  ├── dishonored.toml                  settings: config/dishonored.toml
  ├── deko3d_glsl.bin                  every game shader as GLSL (made from your game)
  ├── deko3d_shaders.bin               precompiled shader variants (made from your game)
  ├── deko3d_shaders.bin.idx           its index
  ├── game_root.nxpack                 the game folder as one asset pack (see below)
  ├── game_root.nxidx                  its index
  ├── update/                          the title update
  │     ├── default.xexp
  │     └── DishonoredGame/ ...
  ├── dlc/                             (optional) DLC, see below
  │     └── <package>.nxpack + .nxidx
  └── userdata/                        written by the game: profile, settings, saves (from hbmenu)
```

The game writes some files itself: `pipeline_list.bin` (the shader pipelines seen so far, built early on the next
start), `dishonored.log`, `logs/`, and its saves. Shader variants the console compiles while you play are **appended
to `deko3d_shaders.bin` on the SD card**: when you update the NRO, keep that file unless the shaders themselves were
rebuilt, or the console compiles them all again (stutter in the first run through each level).

### The game pack

1. Extract the disc image: `python tools/extract_iso.py <disc.iso> -o assets/game_root` (or any Xbox 360 ISO
   extractor such as extract-xiso), and put the title update's `default.xexp` next to its `default.xex` (see the
   next section), as for the build.
2. Pack it: `python tools/build_asset_pack.py assets/game_root --verify`. This writes `game_root.nxpack` and
   `game_root.nxidx` next to the folder; copy both to the SD card. The console mounts the pack instead of opening
   thousands of files, and the `game_root` folder itself is not needed there. (A folder of the same name next to
   the pack wins over it, which is handy for testing a changed file.)

### The title update

Extract the update package with `python tools/stfs_extract.py <TU package> out/tu` and copy its `default.xexp` and
its `DishonoredGame` folder (the `Patches/*.bs` files) to `update/`. The SDK applies the `.xexp` patch at load (the
log says `XEX patch applied: 0.0.0.17 -> 0.0.3.17`) and the game finds its package patches there.

> [!NOTE]
> The NRO must be built against the **same** `default.xex` + `default.xexp`: the code generator translates the
> patched program, and every hook in the port points at its addresses.

### DLC

Extract each DLC package with `python tools/stfs_extract.py <package> assets/dlc/<package file name>`, then either

- pack each folder: `python tools/build_asset_pack.py assets/dlc/<name>` and copy `<name>.nxpack` + `.nxidx` to
  `dlc/`, or
- copy the extracted folder itself to `dlc/<name>/`.

The log names each one at start: `[dlc] ... mounted from its asset pack`. The packages are licensed as on a console
that owns them (`license_mask`).

### Language

The disc tested holds English, Russian, Polish, Czech and Hungarian text (Russian has its own voices; the others use
the English ones). `dh_language = "auto"` plays in English, or in Russian when the console is set to Russian (the
Switch has no Polish, Czech or Hungarian setting); name a language to force it
([configuration.md](configuration.md)).

## Settings

Copy [`config/dishonored.toml`](../config/dishonored.toml) as `sdmc:/switch/dishonored/dishonored.toml`. What the
settings do: [configuration.md](configuration.md). (`app/dishonored_switch.toml`, which the build deploys beside the
NRO, is the same file with the profiling report and the debug overlay turned on.)

## Starting the game

- **From the Homebrew Menu, with full memory:** hold **R** while starting an installed game (title takeover). The
  applet mode (album) does not have enough memory.
- **From the HOME menu:** build and install the [NSP forwarder](forwarder.md). It also gives the game the 4th CPU
  core and saves into the Switch user you pick.

The first start is slower: the console compiles the shader pipelines it meets and records them in
`pipeline_list.bin`; later starts build them in the background before they are needed.

## Ryujinx

Dunwall runs in Ryujinx for testing (not as the main way to play). An NRO there has no path of its own, so the game
always uses `sdmc:/switch/dishonored` inside Ryujinx's SD folder. Add this to the `dishonored.toml` you use in Ryujinx:

```toml
switch_guest_views_eager = true    # Ryujinx reports no data aborts: map guest memory up front
```

Never copy that line to the console: eager views cost memory there. `tools\win\sync_ryujinx_dishonored.ps1` (run by
`run_dishonored_ryujinx_renderdoc.bat`) fills Ryujinx's SD folder from `out\sd\switch\dishonored` and adds the line
for you. Known Ryujinx differences: colours can be wrong (an unimplemented shader instruction), GPU occlusion counters
do not count (the port notices and treats everything as visible), and a level's first seconds stall while Ryujinx
compiles shaders.
