# 📊 Measuring

With `switch_profile_report = true` the game writes a report every 10 seconds to `logs/rex/rex_profile.log`
(per-thread CPU, cores, audio counters), and a short line to `dishonored.log`:

```text
[renut] 10.0 s: 294 game frames (29.30 fps), render thread busy 4.07 ms a frame, GPU ... ms a frame; 103227 draws, ...
```

- **game frames / fps:** frames the game presented in the window;
- **render thread busy:** the port's renderer thread (Renut) per frame;
- **GPU:** the GPU's own time per frame;
- **draws:** how much the game asked for. 750-900 draws a frame is a heavy view with the shipped settings;
  without the prepass skip and the culling the same views were 1,300-2,400.

On the console, **L + R + D-pad up** shows the same live (FPS, worst frame, CPU / GPU / render times and a frame
graph with the 33.3 ms line) when `ui_dev_overlays = true`.

Rules of thumb from the console runs:

- **fps low, render thread low, GPU low:** the game's CPU threads are the limit, nearly always UE3's render thread
  on core 0. Nothing in the graphics settings helps much; draw-count features (occlusion, the prepass skip, size
  culling) do.
- **GPU near 33 ms:** lower `sa2_render_height_*`. With the GPU at 460.8 MHz, handheld 480p took 41-48 ms of GPU a
  frame in heavy spots before the draw-count work (360p: 24-27 ms); after it, 360p measured 17.6 ms median.
- Compare runs at the **same place and the same clocks**: one standing spot for a few minutes is more useful than
  a long mixed session.

More detail when you need it (each costs a little, remove afterwards):

| Setting | Log line | |
| :--- | :--- | :--- |
| `switch_profile_stacks = true` (+ `_top = 6`) | `logs/rex/rex_stacks.log` | sampled stacks of the busiest threads; symbolise with the build's ELF ([debugging.md](debugging.md)) |
| `renut_draw_stage_timing = true` | `[draw-stages]` | the render thread's time per draw stage, pipeline lookups, shader cache reads |
| `me2_diag_draw_callers = true` | `[draw-callers]` | every draw counted by its UE3 caller chain and kind (depth / opaque / blend) |
| `dh_ab_cycle = "a,b,..."` | `[ab]` | turns each named feature off in turn for 10 s: `small_mesh`, `raster_prims`, `prepass_skip`, `lod_scale`, `ring_spin` |

The A/B rotation is the honest way to judge a feature: stand still in a heavy spot for a few minutes and sum the
frames of each slot. That is how the primitive cull was kept (+1.3 fps), the LOD scale found to change nothing at
the spot tested, and the queue's spin-wait found to cost 0.5-1 fps.
