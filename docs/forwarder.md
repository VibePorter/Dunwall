# 🏠 The NSP forwarder

An installable HOME-menu entry that starts `dishonored.nro`. Worth it for three reasons:

- **The 4th CPU core.** Its NPDM allows cores 0-3; a homebrew launch only gets three. The shipped settings put the
  background shader work, XMA decoding and (by preference) the audio threads there
  (`banjo_background_compile_core`, `switch_audio_core_mask`).
- **Switch user saves.** The HOME menu asks for a user, and the game saves into that user's save data
  (`app/me2_src/me2_switch_save.cpp`, 64 MB + 32 MB journal) instead of the SD card. That file system keeps no file
  times, so the port keeps them itself and the save list shows real dates.
- **Full memory** without holding R.

## Building it

```sh
KEYS=/path/to/your/prod.keys bash tools/nx/build_dishonored_forwarder.sh [sdmc:/switch/dishonored/dishonored.nro]
```

- `KEYS` is **your own console's** `prod.keys`. Only `header_key` is read, to encrypt the NCA headers; the file is
  never copied. **Never commit or share keys.**
- The loader is [Forwarder-Mod](https://github.com/Skywalker25/Forwarder-Mod) (nx-hbloader) at a pinned commit,
  built with your devkitA64; the NSP packing is [prototype-switch](https://github.com/fungalspores/prototype-switch)'s
  `build_forwarder.py` / `nsp_container.py` ([`forwarder/`](../forwarder)).
- `TITLE_ID` (default `0144534844000000`), `AUTHOR`, `VERSION`, `ICON` (default `app/icon_dishonored.jpg`, a
  256x256 JPEG) and `OUT` change the entry. The title ID must match `me2_switch_save_title_id` in
  `dishonored.toml`, or the saves go to another title.
- Output: `out/nsp/`. Install it with any NSP installer; like every homebrew NSP it needs sigpatches.

The NSP only points at the NRO's path: updating the game means replacing `dishonored.nro`, not reinstalling the NSP.
