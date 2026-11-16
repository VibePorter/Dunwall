# Contributing to Dunwall

Thanks for helping. A few rules keep the project healthy and legal.

## Never commit game data

Nothing made from the game may enter the repository: game files, `app/generated/`, `out/default_image.bin`, shader
containers, SPIR-V / GLSL / DKSH made from the game's shaders, asset packs, NROs, ELFs, saves, logs with save
contents, game art (icons, logos, key art) and keys. `.gitignore` covers the usual names; check `git status` before
every commit and never `git add -f` such a file.

## Bug reports

Use the issue template. Attach `dishonored.log`, the sha256 of your `dishonored.nro`, the settings you changed, and
for a crash `deko3d.log`, `logs/rex/rex_crash.log` and the Ryujinx log or the console's crash report
([docs/debugging.md](docs/debugging.md)).

## Changes

- One topic per pull request, with what it fixes and how you checked it (PC build, Ryujinx, console; clocks and place
  for performance claims).
- Hooks use the addresses of program 0.0.3.17 (0.0.0.17 + title update). A new hook of a function the optimised
  codegen calls directly needs `python tools/direct_calls.py --redo` (or a new codegen) so the calls go through the
  hook.
- A new setting: a `REXCVAR_DEFINE_*` with a clear description, off by default unless it is safe everywhere, and a
  line in [docs/configuration.md](docs/configuration.md) if users should know it.
- A fix that also applies to Mass Effect 2: add it to [docs/lazarus-backports.md](docs/lazarus-backports.md).
- Keep code comments short and factual; the history belongs in the commit message.
- Code in `sdk/` is BSD-3-Clause, `shaders/` MIT, the rest GPL-3.0 (see `THIRD_PARTY_NOTICES.md`).
