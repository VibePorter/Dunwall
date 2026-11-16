# One-time port steps

Small scripts used once on 2026-11-08 to retarget the Lazarus tree to Dishonored; kept as a record of how it was
done. Run from the repository root. They are not part of any build.

- `addr_scan.py`: where Mass Effect 2's Direct3D addresses appear in the renderer sources
- `apply_dishonored_addrs.py`: puts Dishonored's verified addresses and device globals into those sources
- `callgraph_check.py`: compares the callers of matched functions in both games (`ME2_GENERATED` = Mass Effect 2's
  `app/generated/default`)
- `global_store.py`: finds the stores to a global (the device pointer) in both games (`ME2_GENERATED` as above)
- `cmake_port_game.py`: added the `PORT_GAME` switch to the CMake files

`tools/deko/find_xxh3.cpp` checks which bytes of a shader container XenosRecomp's XXH3 name covers.
