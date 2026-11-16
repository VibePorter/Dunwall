# 📈 Profile-guided builds (PGO)

The release NRO is compiled with GCC's profile-guided optimisation and LTO: the compiler learns which of the ~74,000
translated game functions and branches are hot from real play sessions. Three steps:

1. **Record.** `PGO=generate PGO_DIR=$HOME/dishonored-pgo/profile bash tools/nx/build_switch.sh sync build` makes an
   instrumented NRO (no LTO, slower). Play it on the console; each session writes `pgo/sa2_pgo_<N>.stream` next to
   the NRO (every two minutes, at HOME and at exit). Cover different places: the prologue, a mission, a hub, combat,
   a level load. Play with the settings you will ship, without A/B rotations or heavy diagnostics.
2. **Merge.** Copy the `pgo` folder to the PC, then
   `PROFILE=$HOME/dishonored-pgo/profile bash tools/nx/pgo_merge.sh <folder>` (an older profile is moved aside).
3. **Build.** `PGO=use PGO_DIR=$HOME/dishonored-pgo/profile bash tools/nx/build_switch.sh build`.

Both scripts default to Lazarus's profile folder, so name `PGO_DIR` / `PROFILE` as above.

What the maintainer's sessions showed: one 33-minute session executed 53 % of the translated functions, three
sessions 54.4 %; more play in the same areas adds little. The release profile is one long session (56 %).

A profile fits only the code it was recorded with: after a codegen change (or another game version) record again.
Renderer files changed between the recording and the final build only lose the profile for their own functions.
`tools/pgo_fit.py` and `tools/pgo_summary.py` report how well a profile still matches.
