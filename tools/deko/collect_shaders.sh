#!/bin/bash
# Collects every Xenos shader container of YOUR game into one folder, the input of XenosRecomp
# (docs/building.md, "Shaders"). Linux / WSL / Git Bash with a C++20 compiler.
#
#   bash tools/deko/collect_shaders.sh <game folder> <out folder> [<DLC folder> ...]
#
#   1. ue3_shader_scan: decompresses every package (*.xxx; LZO chunks for Mass Effect, XMem LZX for Dishonored) and
#      writes each shader container it finds as {vs,ps}_<fnv64>.bin (material and global shaders cooked into the
#      packages).
#   2. extract_unaligned_containers.py: the global shader files keep their containers at any byte offset
#      (ME2: BioGame/CookedXenon/GlobalShaders-Xbox360.bin, media/effects/SimpleShaders.fxobj;
#      Dishonored: DishonoredGame/CookedXbox360/GlobalShaderCache-Xbox360.bin). Paths that do not exist are skipped.
# A few dozen shaders are compiled by the game at run time (from HLSL) and are in no file; the renderer logs any shader
# it does not find in its table.
set -euo pipefail
GAME=${1:?usage: collect_shaders.sh <game folder> <out folder> [<DLC folder> ...]}
OUT=${2:?usage: collect_shaders.sh <game folder> <out folder> [<DLC folder> ...]}
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
CXX=${CXX:-clang++}
CC=${CC:-clang}
BIN=${SCAN_BIN:-$HERE/../../out/tools/ue3_shader_scan}
MSPACK="$HERE/../../sdk/thirdparty/libmspack/cabextract/mspack"
mkdir -p "$(dirname "$BIN")" "$OUT"
# XMem LZX through the SDK's libmspack (Dishonored, 2026-11-08); LZO stays built in.
"$CC" -O2 -c -I"$MSPACK" "$MSPACK/lzxd.c" -o "$BIN.lzxd.o"
"$CC" -O2 -c -I"$MSPACK" "$MSPACK/system.c" -o "$BIN.system.o"
"$CXX" -O2 -std=c++20 -DSCAN_LZX -I"$MSPACK" -o "$BIN" "$HERE/ue3_shader_scan.cpp" "$BIN.lzxd.o" "$BIN.system.o"
for root in "$GAME" "$@"; do
  find "$root" -iname '*.xxx' -print0 | xargs -0 -n 200 "$BIN" "$OUT"
done
globals=()
for g in BioGame/CookedXenon/GlobalShaders-Xbox360.bin media/effects/SimpleShaders.fxobj \
         DishonoredGame/CookedXbox360/GlobalShaderCache-Xbox360.bin; do
  [ -f "$GAME/$g" ] && globals+=("$GAME/$g")
done
[ ${#globals[@]} -gt 0 ] && python3 "$HERE/extract_unaligned_containers.py" "$OUT" "${globals[@]}"
echo "containers: $(ls "$OUT" | wc -l) ($(ls "$OUT" | grep -c '^vs_') vertex shaders)"
