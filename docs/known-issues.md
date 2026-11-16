# 🚧 Known issues

Dunwall is a work in progress. What is known not to work, or not yet verified:

## Performance

- The heaviest views drop to the low 20s fps (23-24 fps measured at two stationary spots, handheld):
  UE3's render thread is busy 88-90 % of its core at ~760-870 draws a frame there. It is the game's own
  single-threaded code; only a small part of that time is the port's.
- Level loads take 12-31 s; they are CPU-bound. `dh_load_boost` (forwarder only) shortens them and is off by
  default.
- One long frame (~0.7 s) about every 60-70 s was seen while standing still. The engine's garbage collection is
  suspected; `dh_ini_override` raises its limits fourfold, and whether that moved the dips has not been checked.
- The first run through a level can stutter while the console compiles new shader variants; they are kept in
  `deko3d_shaders.bin` on the SD card afterwards.
- Short audio dropouts (5-16 ms) can happen in heavy stretches.

## Rendering

- **Rainbow or black-dotted textures on distant objects**, intermittent, console only; an affected object stays
  wrong until the level is loaded again. Found and fixed on the way: memory-export copies done on the CPU, mip levels
  without data, the resolve pool stopping under memory pressure, render targets sharing memory chunks with textures,
  a staging-flush hole, and the copy running after the game reused its source. The last fix (the copy done at the
  game's call) has not been confirmed on the console over a long session.
- The world depth prepass and the sun's dynamic shadows of characters and movable objects are not drawn
  (`dh_prepass_skip`, `dh_prepass_no_dominant_shadows`); small far props disappear earlier (`dh_cull_small_*`), and
  detail objects, decals and particles are reduced (`dh_system_settings`). Remove the lines for the Xbox's picture
  at a lower frame rate.
- The primitive cull (`dh_raster_prims = 3`) can in principle hide a movable object that carries static mesh parts;
  set it to 2 or 1 if something pops.
- Only scene heights 360 / 480 / 540 / 720 exist on deko3d.
- PC build only: the Outsider's teeth show through his lips in one scene (a depth lean of the PC back end).
- Ryujinx only: colours can be wrong (an unimplemented shader instruction).

## Game

- **Main menu:** choosing *Continue* within the first seconds can answer "Required content unavailable" when the
  save needs DLC: the game reads the DLC list about 8 s after the menu appears. Wait a moment.
- A rare crash in the navigation code (a non-finite position, seen once in the Distillery District) is not fixed.
- Online does not exist: leaderboards and the DLC store are hidden.
- Not yet confirmed on the console: the DLC store button and the storage device row being gone, the control scheme
  "Type E" surviving a profile reload, the faster start-up after the pipeline-list fix, and docked play at 540p.
- A small atlas of monochrome button glyphs (`lib_I3E`) still shows Xbox letters where the game uses it.
- Saves made before the save-date fix show 1970 until they are saved again.

## Build

- The build chain is documented from the maintainer's runs; a full run from a fresh clone has not been repeated yet
  ([building.md](building.md)). The step that adds the title update's patched packages to the shader collection is
  manual.
- One edition was tested (the Eastern European disc, 0.0.0.17 + title update 0.0.3.17).
- `patches/` does not yet include this project's own changes to the SDK, plume and XenosRecomp; the files in the
  repository are the source of truth.
- `tools/` and `app/me2_src` still hold scripts and code from the projects this port came from (SA2, MGR, Mass
  Effect 2). They are kept because later steps reuse them; not all of them apply to Dishonored.
