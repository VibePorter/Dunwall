# 🐞 Debugging

## Logs

| File | |
| :--- | :--- |
| `dishonored.log` (next to the NRO) | the runtime log: `[renut]`, `[dlc]`, `[dh-raster]`, `[prepass]`, `[cpu-copy]`, `[draw-stages]`, `[settings]`, `[ini]`, `[ab]`, ... |
| `deko3d.log` | the graphics back end: memory blocks, shader variant compiles, GPU channel messages. Flushed line by line, so it keeps the last lines before a crash |
| `logs/rex/rex_profile.log` | the periodic performance report ([measuring.md](measuring.md)) |
| `logs/rex/rex_crash.log` | written when the process catches a crash (it keeps the last one) |

`dishonored.log` is appended across sessions and flushed every `log_flush_interval` seconds: the last seconds before
a crash or a HOME-button exit can be missing. For a crash hunt set `log_flush_interval = 1`. The console's clock is
whatever the console is set to; screenshots are named in UTC.

In **Ryujinx** the NRO has no path, so logs land in `sdmc:/switch/dishonored/` of Ryujinx's SD folder, and Ryujinx's
own log (`Logs/Ryujinx_*.log`) has the guest stack of a crash.

## Turning a crash stack into function names

Every build keeps its unstripped ELF in `out/deployments/<sha256 of the NRO>/dishonored.elf`. Find the right one with
`sha256sum dishonored.nro`, then:

```sh
aarch64-none-elf-addr2line -e out/deployments/<sha>/dishonored.elf -f -C -i -p 0x2496650 0x2709158 ...
```

(`llvm-symbolizer --obj=<elf> 0x<offset>` works too.) Names come out as `__imp__sub_82XXXXXX`: the guest function at
that Xbox 360 address, whose C++ is in `app/generated/default` (search for `DEFINE_REX_FUNC(sub_82XXXXXX)`; every
instruction is there as a comment, and `tools/d3d/gen_disasm.py` prints a listing with `--calls` / `--callers`). LTO
folds functions, so a frame now and then names a neighbour: trust the chain, check the odd ones.

If the ELF's hash is unknown, match the build by the instruction word at the crash's program counter.

## GPU problems

- `dh_gpu_breadcrumbs = true` records every draw, clear, copy and resolve with a counter the GPU reports back; at a
  GPU channel failure `deko3d.log` gets the last finished item, the following ones with their shaders and buffer
  addresses, and the recent memory block and queue events. `dh_gpu_breadcrumbs_exact` makes the GPU finish each
  item before the next (slow, exact).
- `deko3d.log`: "`gpu channel: the GPU's command ring was full ...`" is the retried submit that used to be the
  level-load crash ([porting-notes.md](porting-notes.md)).
- A debug build of the back end (`PLUME_DEKO3D_DEBUG`) makes deko3d check every call and write its complaints to
  `dishonored.log` ("deko3d says").

## The PC build and RenderDoc

The PC build runs the same game code and renderer on Vulkan:

- `run_dishonored_pc.bat [360]`; the log is `out\pc\dishonored.log`.
- `run_dishonored_pc_renderdoc.bat` and `tools\win\rdc_capture_dishonored.ps1 -Frame N` capture a frame
  (`dh_renderdoc_capture_frame`); `tools/win/rdc_passes.py`, `rdc_draws.py`, `rdc_shader.py`, `rdc_texture.py`,
  `rdc_pixels.py`, `rdc_bindings.py`, `rdc_copies.py`, `rdc_clears.py` read a capture through
  `qrenderdoc --python`.
- RenderDoc shows wrong contents for Renut's float constant buffers: use `dh_draw_trace_frame` (the device's
  constants and fetch constants of each draw, in the log) instead.
- `run_dishonored_ryujinx_renderdoc.bat` starts the Switch NRO in Ryujinx under RenderDoc (F12 captures).
- `tools\win\sample_stacks_dishonored.ps1` samples a hung or busy `sa2.exe` with cdb.

## Inside the game's code

- `out/default_image.bin` (written by codegen) is the patched guest image, laid out as in memory from `0x82000000`.
- `tools/deko/ue3_decompress.cpp` decompresses a package (LZX, also whole-file compressed ones);
  `tools/dh_package.py` reads its tables, `tools/dh_texture.py` exports a `Texture2D` as PNG, and
  `tools/dh_as2dis.py <upk> <word>` disassembles the ActionScript 2 of a Scaleform movie inside it: most menu logic
  lives there.
- `tools/d3d/match_funcs.py` matches functions against Mass Effect 2's generated code by instruction shape;
  `tools/rhi/census_dev.py`, `devrefs.py` and `devwrites.py` find the code that writes the Direct3D device.
- `tools/deko/d3d_index.py`, `d3d_find.py`, `d3d_offsets.py`: an index of the Direct3D functions and the device
  fields they touch.

## Before reporting an issue

Attach `dishonored.log`, the NRO's sha256 and, for a crash, `deko3d.log`, `logs/rex/rex_crash.log` and the Ryujinx
log or the console's crash report. Say which settings you changed and which mission / place it happened in.
