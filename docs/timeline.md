# 📜 How Dunwall came together

One week, 8-14 November 2026, starting from the [Lazarus](https://github.com/VibePorter/Lazarus) tree (Mass Effect 2).
Console results come from the maintainer's Switch; "PC" is the Windows build of the same app. The technical detail of
each item is in [porting-notes.md](porting-notes.md).

## 8 November: a new game in an old tree

- Lazarus cloned; the game's files in place (title `425307E3`, program 0.0.0.17 + title update → 0.0.3.17). The XDK
  is 2.0.21076, the one Sonic Adventure 2 and Metal Gear Rising use, so the Direct3D device layout comes from that
  side of the family, not from Mass Effect 2.
- Codegen runs with three tail-branch overrides; 190 functions reachable only through pointers found by a fixpoint.
- All 35 Direct3D functions Renut hooks matched against Mass Effect 2's and verified.
- The shaders: packages are XMem LZX, and material shaders are LZX-compressed again inside them. ~45,000 collected.
- **PC: the game boots**, plays its start-up movies and reaches the 3D menu at 30 fps with sound.
- First picture bugs: flat yellow frames (the wrong device-watch list), wrong colours (`Resolve`'s slice argument,
  the gamma ramp's offset), shader literal constants (constant blocks), a vertex shader with eight position
  streams, thin bright outlines (texture fetch exponent adjust), swirling materials (interpolators linked by
  register instead of semantic).
- The first Switch NRO builds; asset pack, forwarder NSP.
- First performance pass: natives for the constant setters, `memcpy`, string functions and LZX decompression.

## 9 November: Ryujinx, the console, optimised code

- The disc is an Eastern European one and the game picked Russian: the language choice (`dh_language`), with the
  English voices found under their Russian file names.
- DLC: seven packages mounted from extracted folders. The "install to hard drive" prompt traced to a free-space
  answer of zero.
- Ryujinx: a green screen (bindless texture handles after the mask change) and a crash (a submit with too many
  command entries).
- **First console run:** 30 fps in light scenes; a yellow-green 3D picture, fixed twice over (the colour-grading
  target scaled with the scene; a 2D-array texture view made with one layer).
- Optimised codegen: the audits rerun for this program, a 60-entry keep list, `setjmp` / `longjmp` declared. A hang
  in the optimised build: the direct-call pass did not know the new hook folder.
- Real occlusion queries, mapped onto UE3's query batches: 50-80 % of the queried objects hidden on the console.

## 10 November: where the time goes

- Console profile of the worst area: 11-15 fps at 4,000+ draws a frame.
- Device watch for this game (737 writer functions found by a census), store cards on.
- More natives: Wwise reverb and delay, a set lookup, shader parameter setters.
- The **CPU occlusion rasterizer**: characters had been popping in and out with late GPU answers.
- Switch button prompts.
- The rat swarm: 3-7 fps for minutes. First a starved shader-prebuild worker, then the real cause: per-draw polygon
  offsets in the pipeline key.
- Save dates (1970) and the time zone fixed.
- Runtime shader variants: the shipped cache held 119 of the 1,433 the game needed; regenerated from a pipeline list.
- Ports from naruto-nx: alpha test as a pipeline constant, lock-free render queue, repeated clears skipped, a draw
  fast path, critical-section spinning.
- PGO capture builds begin.

## 11 November: UE3's render thread

- New icon art, the controller picture redrawn as a Switch, the control scheme "Type E".
- The DLC store button and the storage device row removed.
- The HUD dropping to scene resolution after leaving water.
- The core-0 wall measured: UE3's render thread at ~90 %, the port's share of it small. A map of UE3's passes from
  stack samples; no per-light mesh pass like Mass Effect 2's exists here.
- Size culling tuned, the **prepass skip** (and what forces the prepass: a dominant light's dynamic shadows), a LOD
  distance scale, the **primitive cull** against the rasterizer's depth.
- The game's own quality settings and config values replaced from the toml.
- Five console runs with A/B rotations: 21 → 23-24 fps at the heavy spot, draws 1,305 → ~760-870 a frame.
- Two crashes read from logs: a navigation NaN and a "GPU timeout" at a level load.

## 12 November: every shader, and the first long hunts

- The codegen that re-ran on every PC build: the manifest was rewritten after the stamp.
- **All shaders ahead of time.** The "run-time compiled" shaders the console kept capturing were ordinary material
  shaders the scanner had dropped: four bugs, +4,147 shaders, all 62 captures accounted for. Dark Vision's green
  screen and the missing blink sphere end here.
- Press Start at 5 fps (pipeline records written to the SD card on the render thread), 234 s of start-up shader
  preparation (random reads under one lock), 13-18 s reading the pipeline list: each found in the log and fixed.
  One fix opened the shader cache a second time, which Horizon refuses: a night's build that never reached the menu.
- Rainbow textures at a distance: the hunt begins. Store cards, late exports and unseen changes ruled out one by one.

## 13 November: the command ring

- The level-load crash reproduced on a DLC save, session after session. Breadcrumbs through the GPU's command
  stream; arena, cache, compression, Zcull and acquire theories each built, run and ruled out. Then the block
  timestamps showed the GPU had never stopped: **the channel's 2,048-entry command ring was full**. The submit is
  retried; confirmed on the console.
- Rainbow textures, continued: the copy done on the CPU, the resolve pool's floor, render targets in their own
  memory chunks, the staging flush, the copy synchronised with the game's thread.
- Dunwall City Trials: the leaderboard crash (stats calls that answered nothing), then the leaderboards hidden.
- Dark Vision showing nobody behind walls: the primitive cull.
- A new icon; handheld back to 480p.

## 14 November: release

- The pipeline list had been half-read at every start since a killed session damaged it: repaired and compacted.
- DLC as asset packs. **The release build**: PGO (one long session) + LTO.
- Black screen on a sword hit (fixed-point resolve exponent), a 40 s hang on the PC, the frame-rate overlay,
  the texture upload budget, the sampler table that had been full in every session.
- The copy moved to the game's own call; the pipeline list loaded without the allocator storm; docked at 540p.
- "Everything's great", from the console, except the start-up time, addressed in the last build of the day.

## 16 November

- The tree becomes its own repository, **Dunwall**.
