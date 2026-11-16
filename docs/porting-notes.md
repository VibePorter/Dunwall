# 🔬 Porting notes

What was found inside *Dishonored* (Xbox 360, program 0.0.3.17) and how each problem was solved. Addresses are guest
addresses of that program; `sub_X` is the generated function, and a hook on it lives in the file named. Dates are
November 2026. This is the long form of the README's "What we fixed".

## The program

- Title `425307E3`, XEX 0.0.0.17, title update → 0.0.3.17, XDK 2.0.21076. Unreal Engine 3, package version 801,
  Wwise audio (XMA), Scaleform menus (ActionScript 2), libpng 1.0.5 (its `setjmp` 0x82D88DD0 / `longjmp`
  0x82D887B0 must be declared in `overrides.toml`).
- Codegen needs three tail-branch entries, one function missed behind a vtable (`sub_82BA3418`), and 190 functions
  only reachable through pointers (`tools/pointer_fixpoint.py`).
- The optimised flavour keeps 60 functions classic: 56 exception funclets, the stack probe 0x82D86094 and its
  continuation, and two special cases. XDK copy routines clobber v0-v65: a native that wraps them must not assume
  the non-volatile vector registers survive.
- A new hook folder has to be known to `tools/direct_calls.py` (and `flush_mode_flow.py`), or the optimised code
  calls the original past the hook. This hung the first optimised build (the occlusion read spun on its marker).

## Direct3D and the renderer

See [d3d-map.md](d3d-map.md) for the functions and device fields.

- **Device watch.** Renut compares only the device chunks that watched writers touched. With Mass Effect 2's writer
  list nothing was compared and every draw kept the first draw's state (flat yellow frames). Dishonored's list
  (`app/watch_writers.txt`, 737 functions) comes from a run-time census plus a static scan of the XDK range.
- **Resolve.** `r8` is the destination slice or face. The colour-grading volume (16 slices) only got slice 0.
- **Colour grading on the Switch.** The LUT is rendered into a 32x16 target; scaled with the scene it became 16x8
  and the final pass read wrong cells: targets up to 32 px stay unscaled (`renut_unscaled_max_size`). Then plume's
  deko3d back end made 2D views of array images with one layer: the stacked LUT collapsed to slice 0.
- **Texture fetch exponent adjust** (fetch constant dword 3, bits 13-18) was ignored. The game's FXAA reads one
  image through three fetch slots with adjusts 0 / -1 / -2. The adjust now travels in the texture word and the
  translator scales every fetch result.
- **Interpolators.** XenosRecomp linked vertex outputs to pixel inputs by register; UE3 packs both tables in order
  of use, so they must be linked by semantic.
- **Shader literal constants** live in the shader's GPU memory and are loaded at the shader flush; the port copies
  them into the device constants before each draw.
- **Fixed-point resolves.** 16_16 and 16_16_16_16 targets are emulated as SNORM; a resolve from them needs +5 on
  the exponent bias. Without it the sword-hit blur (three chained resolves) went black.
- **Polygon offset.** UE3 sets a slightly different depth bias on shadow and decal draws; as part of the pipeline
  key that made ~640 new pipelines every 10 s (the rat swarm at 3-7 fps). It is dynamic state now.
- **The UI pass.** The HUD is drawn at 720p after a "starter" pass is recognised. Under water, or after a sword
  hit, the final pass detours through a 320x180 buffer and back; the starter is now kept across such detours.
- **The sampler table** (256 entries) was full in every console session, turning later samplers into linear clamp;
  it holds 1,024 now.
- **Pipeline list.** Every pipeline the console builds is recorded and prebuilt at the next start. Records are
  written by their own thread (writing on the render thread cost 5 fps at Press Start); a record cut by a killed
  session used to stop the loader for good (21,903 of 65,433 pipelines loaded): the loader now resynchronises and
  rewrites the list clean.
- **The shader cache file.** One handle only: Horizon refuses a second open of a file that is open for writing.
- **GPU memory.** Render targets (hardware-compressed) and textures come from separate chunks. Recycled storage is
  flushed from the CPU cache before deko3d gets it.

### The "GPU timeout" crash

At some level loads deko3d aborted with a channel kickoff failure (libnx `0xD5C`, Timeout). Breadcrumbs (a counter
the GPU reports after every piece of work) always named an innocent draw or copy. Six theories were built and ruled
out on the console (block arena, CPU cache, depth compression, Zcull, range reuse, swapchain acquire) before the
timestamps of the memory-block events showed that the GPU had kept working up to each failure: the channel's
hardware command ring holds 2,048 entries, the CPU was ~2,000 entries ahead, and nvservices answers a submit that
finds no room with Timeout. `sdk/src/ui/switch_perf.cpp` wraps the kickoff and retries it. The same wrapper submits
early (at 256 entries), which Ryujinx needs.

### Texture copies and the rainbow textures

UE3's texture streaming moves texture memory **with the GPU**: a vertex shader (`FMemCopyVertexShader`, hash
`0FF548D34E454B3D`, eight streams in, memory exports out) copies the small mips into a newly allocated texture.
Renut returns export results to guest memory a frame or more later.

- The copy exports format 26 (16_16_16_16 unsigned) with endian 8-in-16; the shader path did not write it
  correctly. The copy is now done on the CPU (`dh_renut_cpu_memcopy`), at the game's own call
  (`dh_renut_memcopy_at_call`), so the game cannot reuse the source first.
- A texture whose fetch constant names more mip levels than have data keeps only the levels with data.
- Under memory pressure the resolve pool used to stop and the UI's resolve target was recreated ~50 times a second;
  it stops only under 32 MB now.

Checked and ruled out along the way: store cards (0 changes missed), late exports over CPU writes (0), guest memory
changing unseen (0 in 106,961 whole-texture checks). The artefact is intermittent and was not confirmed gone at the
time of writing ([known-issues.md](known-issues.md)).

## Shaders

- Packages are XMem LZX; material shaders are compressed again inside them, in UE3's shader code cache: chunks of
  records `{u16 chunk, u16 size, u32 offset}`, each chunk LZX (flag 20) or zlib (17). The game creates a shader the
  first time a material needs it (`FShader::InitRHI` `sub_823ABFD0` → cache lookup `sub_823A94E8` →
  `appDecompressMemory` `sub_822E44F8` → `XGRegisterPixelShader`). The game has no shader compiler.
- The scanner (`tools/deko/ue3_shader_scan.cpp`) missed 4,147 shaders for four reasons: chunks end with up to five
  non-zero slack bytes after the LZX end marker; fully compressed packages (one chunk at offset 0: `Startup`,
  `Engine`, `DishonoredGame`) were skipped; a block the decoder refuses dropped its whole chunk; and the title
  update's BSDIFF patches apply to the decompressed package.
- XenosRecomp names a shader by XXH3-64 over its virtual and physical size parts. Position inputs 5-7 needed
  locations of their own (the copy shader reads eight).
- For deko3d the texture index mask in the GLSL has to be recognised (`tools/deko3d_glsl.py`), or every texture
  becomes a computed bindless handle, which Ryujinx cannot follow (a green screen).

## Unreal Engine 3 in this game

**Render thread passes** (from console stack samples): scene render `sub_82631D10`; view setup `sub_8262FF20`
(relevance `sub_82646368` and siblings: frustum `sub_82327A88` → distance → occlusion `sub_82645A40` → relevance
`sub_82636AC0`); prepass `sub_826320E0(renderer, DPG, view)`; base pass `sub_82630D30`; translucency
`sub_8264F460`. About 70 % of the draws are base pass, 19 % prepass. There is no per-light additive mesh pass.

**Occlusion queries.** UE3 batches up to 200 queries (manager: +800 count, +804 fence, +812 report buffer); begin
`sub_82777BA0`, the end is seen at the XDK's report write `sub_82B085F8`, the batched box loop is `sub_8262CEC0`,
and the read `sub_827777B8` spins on GPU-written reports (marker -275). Answers are always a frame old.

**The occlusion rasterizer** (`dh_occlusion_raster.cpp`). At the prepass, UE3's render thread lists the visible
occluder meshes (static draw lists at scene + DPG × 2448 + {8, 80}); a worker on cores 2-3 extracts and caches
their triangles and rasterizes a 256x144 depth buffer. The query read then tests each query's bounds against the
previous frame's buffer without waiting. On the PC 93-97 % of its answers agree with the GPU's, with none wrongly
hidden; GPU box draws for answered queries are skipped. Notes: `std::thread::detach()` throws on the Switch (leak
the thread instead); the 4 KB guest heap is below 0x40000000.

**The primitive cull** (`dh_raster_prims`, hook on `sub_82636AC0`): every primitive that passes UE3's own tests is
checked against that buffer. Characters seen through walls in Dark Vision are primitives UE3 never queries, so only
primitives with static meshes are culled.

**The prepass skip.** `[renderer+4484]` selects the full prepass; it is set from `sub_8263C368` when a light needs
shadows projected from depth before the base pass (the dominant light's dynamic shadows).
`dh_prepass_no_dominant_shadows` answers 0 there, and the prepass is skipped everywhere: draws 1,305 → ~760-870 a
frame at the heavy spot, with no GPU penalty measured.

**Size culling and LOD** (`dh_cull_distance.cpp`): draw distance by bounds radius, and one multiply patched in
`sub_82636AC0` for the LOD distance.

**Settings.** `FSystemSettingsData::LoadFromIni` is `sub_823BFBE0`, with getters `sub_8229F1B0` (bool),
`sub_8229F050` (int) and `sub_8229F100` (float); `dh_settings.cpp` replaces values after the game's read. The config
cache is at `[0x834D1624]`; `dh_ini_override.cpp` hooks the section lookup `sub_82308478`. `Coalesced_*.bin` is a
big-endian coalesced file (strings with negative length are UTF-16). The Xbox runs `DetailMode=2` with every shadow
kind on, no motion blur or ambient occlusion, 4x anisotropy.

**Language.** `appGetLanguageExt` is `sub_827713E8`: it maps the console's language to an extension that must be in
`COOKEDLANGUAGES` (RUS, CZE, HUN, POL) or falls back to the default, RUS. `dh_language.cpp` answers INT unless
another is asked for. The English voices are stored under the `.RUS` names on this disc; a file-extension fallback
in the virtual file system lets INT open them.

**Menus.** Button prompts are `Startup.upk` `Common_assets.lib_*` textures (DXT5, 40-52 px), replaced as texture
blocks by hash (`dh_button_icons.cpp`, `tools/dh_button_icons.py`); the controller picture is
`UI_GamepadMapping.Options_GamepadMapping_Xenon` (840x600). Control schemes are the input object's
`m_PadBindingSet1..4` and profile setting 76; "Type E" adds a fifth (`dh_control_scheme.cpp`). The DLC store
button, the storage device row (settings flagged 0x10000000) and the Dunwall City Trials leaderboards are removed by
patching the movies' ActionScript as the packages decompress (`dh_main_menu.cpp`).

**The "install" prompt** on the main menu came from `GetDiskFreeSpaceEx` answering 0 for the game drive on the
Switch (the asset pack device reported no free units).

**Leaderboards.** `XUserReadStats` (XGI 0x000B0021) returned success without writing results, and the stats
enumerator was a stub: the game sorted garbage rows. Both now answer "no rows".

**DLC.** The game enumerates content type 2; extracted marketplace packages next to the program are listed and
opened in place (or mounted from asset packs), with the license mask set.

## The Switch

- **Cores:** UE3's render thread on core 0, Renut's on core 1, the game thread on core 2, background shader work,
  the rasterizer worker and audio on cores 2-3 (core 3 only with the forwarder). Putting Wwise on one core starved
  the shader prebuild worker once (4-7 fps for minutes).
- **newlib has one malloc lock.** Allocation in hot paths (the run-time shader compiler, per-wait vectors in the
  kernel's wait calls, guard copies of natives) showed up as waiting on core 0; the hot ones were removed.
- **Save data has no file times:** the port keeps them in a hidden file beside the saves. The time zone comes from
  the console.
- **Start-up:** the shader preparation reads the cache in file order with read-ahead (it took 234 s as random
  reads); the pipeline list is read in 4 MB blocks into reserved containers.
- **PGO:** see [pgo.md](pgo.md). The stream is written every two minutes, so a session survives a crash.

## Natives

Self-checked replacements (`app/dishonored_src/hot/`, `dh_hot.cpp`): float constant setters `0x82AFBEF8` /
`0x82AFBFD0`, `memcpy` `0x82D860C0`, `wcslen` `0x82D822C0`, `strlen` `0x82D9A390`, `appDecompressMemory`'s LZX path
`sub_822E44F8` and `XMemDecompress` `0x82D9A3B8`, Wwise reverb `0x82BB5D28` and delay `0x82BB4C58`, a `TSet` lookup
`0x8251FE10`, shader parameter setters `0x82773D38` / `0x82773E40`. A sampler setter native (`0x827739D0`) measured
slower on the console and is off. Several were found by matching Mass Effect 2's natives by instruction shape
(`tools/d3d/match_funcs.py`).

## Measured on the console

| Step | Heavy spot |
| :--- | :--- |
| First profile (10 Nov) | 11-15 fps, 4,000-4,400 draws a frame, Renut's render thread ~50 ms |
| Device watch, natives, variant cache, naruto-nx ports | ~20-23 fps, 1,300-1,700 draws |
| Prepass skip, primitive cull, fence wait removed (11 Nov) | 23-24 fps, ~760-870 draws, UE3's render thread 88-90 % |

A/B rotation at one spot (11 Nov): primitive cull off = +28 % draws, -1.3 fps; LOD scale off = no change; queue
spin-wait off = +0.5-1 fps (so it is off).
