#!/usr/bin/env bash
# Dishonored: HOME-menu NSP forwarder for sdmc:/switch/dishonored/dishonored.nro (from ME2's
# tools/nx/build_me2_forwarder.sh + me2-nx-deko-tu2/forwarder: Forwarder-Mod (nx-hbloader) 9853bd9, NPDM with the
# 39-bit address space and CPU cores 0-3).
#   KEYS=/path/to/your/prod.keys bash tools/nx/build_dishonored_forwarder.sh [sdmc:/switch/dishonored/dishonored.nro]
# prod.keys (the player's own; only header_key is read, to encrypt the NCA headers) stays where it is
# (KEYS, required). It is never copied into the work tree or the output.
# The NSP needs sigpatches like any homebrew NSP.
set -euo pipefail
NRO_SD="${1:-sdmc:/switch/dishonored/dishonored.nro}"
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$REPO/forwarder"
KEYS="${KEYS:?set KEYS to the path of your own prod.keys}"
# The icon: a 256x256 JPEG (ICON, default app/icon_dishonored.jpg, the project's own emblem).
ICON="${ICON:-$REPO/app/icon_dishonored.jpg}"
OUT="${OUT:-$REPO/out/nsp}"
# 01 + "DSHD" + 000000. The game keeps its saves in this title's per-user save data (me2_switch_save_title_id in
# app/dishonored_switch.toml must match); --save-mb / --journal-mb = kSaveSize / kJournalSize in
# app/me2_src/me2_switch_save.cpp.
TID="${TITLE_ID:-0144534844000000}"
W="${WORK:-$HOME/dishonoredfwd}"

[ -f "$KEYS" ] || { echo "no $KEYS"; exit 1; }
mkdir -p "$W" "$OUT"
cp "$SRC"/build_forwarder.py "$SRC"/nsp_container.py "$W"/
[ -f "$ICON" ] || { echo "no icon: put a 256x256 JPEG at $ICON or set ICON"; exit 1; }
cp "$ICON" "$W"/icon_dishonored.jpg
if [ ! -d "$W/loader/.git" ]; then
  git clone -q https://github.com/Skywalker25/Forwarder-Mod.git "$W/loader"
fi
git -C "$W/loader" checkout -q 9853bd9167d5a7f0c5fdf935864245c1fbcf8282
# Current libnx no longer defines NORETURN.
sed -i 's/void NORETURN /void __attribute__((noreturn)) /' "$W/loader/source/main.c"
[ -f "$W/loader/hbl.json.orig" ] || cp "$W/loader/hbl.json" "$W/loader/hbl.json.orig"

export DEVKITPRO=/opt/devkitpro
cd "$W"
python3 build_forwarder.py --nro "$NRO_SD" --icon icon_dishonored.jpg --title "Dishonored" \
  --author "${AUTHOR:-Dunwall}" --version "${VERSION:-0.1.0}" --tid "$TID" --keys "$KEYS" --out "$OUT" \
  --save-mb 64 --journal-mb 32
ls -la "$OUT"
