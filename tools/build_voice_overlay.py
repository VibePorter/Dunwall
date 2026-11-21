#!/usr/bin/env python3
"""Dishonored (2026-11-21): a voice-over replacement (e.g. the Russian dub, Zarudnev_voice_banks) as game overlays.

The disc's voices are Wwise file packages Bk_<id>.RUS (AKPK, big endian, English speech; DishonoredGame/CookedXbox360
and the DLC06 / DLC07 packages). A dub replaces them one for one under the same names, in the same layout:

  <voices>/DishonoredGame/CookedXbox360/Bk_<id>.RUS   (game data)
  <voices>/DLC06/Bk_<id>.RUS, <voices>/DLC07/...      (DLC packages; DLC06 and DLC07 share 8 names with other data)

This writes

  <out>/game/DishonoredGame/CookedXbox360/Bk_*.RUS + DishonoredGame/Xbox360TOC_RUS.txt   (game_overlay_root)
  <out>/dlc/<package>/DLC06/Bk_*.RUS ...                                                (dlc_overlay_root)

and with --pack their asset packs (<out>/game.nxpack + .nxidx, <out>/dlc/<package>.nxpack + .nxidx, the SD card's
copy; tools/build_asset_pack.py). The runtime lays each over the game data or its DLC package (OverlayDevice).

Xbox360TOC_RUS.txt: the game's table of contents (Russian text) lists every Bk_*.RUS with its size; the dub's
files are mostly bigger, so the overlay carries the TOC with their sizes. With English text the TOC (Xbox360TOC.txt)
lists no voice banks: the game opens Bk_<id>.INT, which opens the .RUS file (dh_language.cpp).

Each package is checked against the game's: same bank and streamed-file ids, same bank version and chunks, same
media ids, the same codec and format for every sound. Byte-identical files are left out, and so are files that
differ only in a broken AKPK header (Zarudnev_voice_banks has 11, language-neutral banks without speech).

  python tools/build_voice_overlay.py ../Zarudnev_voice_banks --assets assets --out assets/voice_ru [--pack]
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOC = 'DishonoredGame/Xbox360TOC_RUS.txt'


def chunks_riff(w):
    if w[:4] not in (b'RIFX', b'RIFF'):
        return None, {}
    e = '>' if w[:4] == b'RIFX' else '<'
    p, r = 12, {}
    while p + 8 <= len(w):
        t = w[p:p + 4]
        n = struct.unpack_from(e + 'I', w, p + 4)[0]
        r[t] = (p + 8, n)
        p += 8 + n + (n & 1)
    return e, r


def media_format(w):
    """(format tag, channels, sample rate) of a RIFX/RIFF sound, or the first 4 bytes."""
    e, c = chunks_riff(w)
    if b'fmt ' not in c:
        return w[:4]
    return struct.unpack_from(e + 'HHI', w, c[b'fmt '][0])


def parse_akpk(path):
    b = open(path, 'rb').read()
    if b[:4] != b'AKPK':
        raise ValueError('not an AKPK file package')
    _, _, lang_size, banks_size, files_size, _ = struct.unpack_from('>6I', b, 4)

    def lut(o):
        count = struct.unpack_from('>I', b, o)[0]
        return [struct.unpack_from('>5I', b, o + 4 + i * 20) for i in range(count)]

    o = 0x1C + lang_size
    out = {'banks': {}, 'files': {}}
    for bid, block, size, start, _ in lut(o):
        d = b[start * block:start * block + size]
        if len(d) != size:
            raise ValueError('bank %08x is cut short' % bid)
        p, ch = 0, {}
        while p + 8 <= len(d):
            n = struct.unpack_from('>I', d, p + 4)[0]
            ch[d[p:p + 4]] = (p + 8, n)
            p += 8 + n
        media = {}
        if b'DIDX' in ch and b'DATA' in ch:
            s, n = ch[b'DIDX']
            data = ch[b'DATA'][0]
            for k in range(n // 12):
                mid, mo, ms = struct.unpack_from('>3I', d, s + k * 12)
                media[mid] = media_format(d[data + mo:data + mo + ms])
        version = struct.unpack_from('>I', d, ch[b'BKHD'][0])[0] if b'BKHD' in ch else None
        out['banks'][bid] = (version, tuple(sorted(ch)), media)
    for fid, block, size, start, _ in lut(o + banks_size):
        w = b[start * block:start * block + size]
        if len(w) != size:
            raise ValueError('streamed file %08x is cut short' % fid)
        out['files'][fid] = media_format(w)
    return out


def check(stock_path, new_path):
    """Problems of a replacement package against the game's (empty = it fits)."""
    try:
        s, n = parse_akpk(stock_path), parse_akpk(new_path)
    except (ValueError, struct.error, KeyError) as e:
        return ['unreadable: %s' % e]
    probs = []
    if set(s['banks']) != set(n['banks']):
        probs.append('bank ids differ')
    if s['files'] != n['files']:
        probs.append('streamed files differ (ids or formats)')
    for bid, (ver, ch, media) in s['banks'].items():
        if bid not in n['banks']:
            continue
        nver, nch, nmedia = n['banks'][bid]
        if ver != nver:
            probs.append('bank %08x: version %s, the game has %s' % (bid, nver, ver))
        if ch != nch:
            probs.append('bank %08x: chunks %s, the game has %s' % (bid, nch, ch))
        if media != nmedia:
            probs.append('bank %08x: embedded media differ (ids or formats)' % bid)
    return probs


def same_but_header(stock_path, new_path):
    """The same size and bytes as the game's file after its AKPK header (the language map and file tables)."""
    a, b = open(stock_path, 'rb').read(), open(new_path, 'rb').read()
    if len(a) != len(b) or a[:4] != b'AKPK' or b[:4] != b'AKPK':
        return False
    end = 8 + struct.unpack_from('>I', a, 4)[0]
    return a[end:] == b[end:]


def dlc_packages(assets):
    """DLC folder name (DLC06 ...) -> package folder name, from assets/dlc/<package>/<DLCxx>."""
    out = {}
    root = os.path.join(assets, 'dlc')
    for pkg in sorted(os.listdir(root)):
        d = os.path.join(root, pkg)
        if os.path.isdir(d):
            for sub in os.listdir(d):
                if os.path.isdir(os.path.join(d, sub)):
                    out[sub] = pkg
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('voices', help='the replacement banks, in the game\'s layout (DishonoredGame/..., DLC06/...)')
    ap.add_argument('--assets', default='assets', help='the game data: <assets>/game_root and <assets>/dlc/<package>')
    ap.add_argument('--out', required=True, help='output folder (its game/ and dlc/ are replaced)')
    ap.add_argument('--pack', action='store_true', help='also write the asset packs (SD card copy)')
    args = ap.parse_args()

    game_root = os.path.join(args.assets, 'game_root')
    packages = dlc_packages(args.assets)
    files = []
    for dp, _, fn in os.walk(args.voices):
        for f in fn:
            files.append(os.path.relpath(os.path.join(dp, f), args.voices).replace(os.sep, '/'))
    files.sort()
    if not files:
        sys.exit('no files in %s' % args.voices)

    for sub in ('game', 'dlc'):
        shutil.rmtree(os.path.join(args.out, sub), ignore_errors=True)
    errors, copied, same, broken_header, sizes = [], 0, 0, 0, {}
    for rel in files:
        top = rel.split('/')[0]
        if top == 'DishonoredGame':
            stock = os.path.join(game_root, rel)
            dest = os.path.join(args.out, 'game', rel)
        elif top in packages:
            stock = os.path.join(args.assets, 'dlc', packages[top], rel)
            dest = os.path.join(args.out, 'dlc', packages[top], rel)
        else:
            errors.append('%s: not game data or a known DLC package (%s)' % (rel, ', '.join(sorted(packages))))
            continue
        if not os.path.isfile(stock):
            errors.append('%s: the game has no such file' % rel)
            continue
        src = os.path.join(args.voices, rel)
        if os.path.basename(rel).lower().startswith('bk_'):
            probs = check(stock, src)
            if probs and same_but_header(stock, src):
                # Zarudnev_voice_banks: 11 language-neutral banks (2 KB, no speech) whose file table entry the
                # repacker wrote shifted (bank size = its id); the data is the game's, so the game's file is used.
                broken_header += 1
                continue
            if probs:
                errors.append('%s: %s' % (rel, '; '.join(probs[:3])))
                continue
        if os.path.getsize(stock) == os.path.getsize(src) and open(stock, 'rb').read() == open(src, 'rb').read():
            same += 1
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(src, dest)
        copied += 1
        if top == 'DishonoredGame':
            sizes['..\\' + rel.replace('/', '\\').lower()] = os.path.getsize(src)
    if errors:
        for e in errors[:40]:
            print('error:', e, file=sys.stderr)
        sys.exit('%d file(s) do not fit the game: nothing usable was written' % len(errors))

    # The TOC with the overlay's sizes.
    toc_in = os.path.join(game_root, TOC)
    lines = open(toc_in, 'rb').read().split(b'\r\n')
    patched = 0
    for i, line in enumerate(lines):
        parts = line.split(b' ')
        if len(parts) >= 5:
            key = parts[3].decode('ascii', 'replace').lower()
            if key in sizes:
                parts[0] = str(sizes.pop(key)).encode()
                lines[i] = b' '.join(parts)
                patched += 1
    if sizes:
        sys.exit('%d game file(s) are not in %s, e.g. %s' % (len(sizes), TOC, next(iter(sizes))))
    toc_out = os.path.join(args.out, 'game', TOC)
    os.makedirs(os.path.dirname(toc_out), exist_ok=True)
    open(toc_out, 'wb').write(b'\r\n'.join(lines))
    print('%d file(s) copied, %d identical to the game\'s left out, %d TOC size(s) updated' % (copied, same, patched))

    if args.pack:
        builder = os.path.join(HERE, 'build_asset_pack.py')
        jobs = [(os.path.join(args.out, 'game'), args.out, 'game')]
        dlc_out = os.path.join(args.out, 'dlc')
        if os.path.isdir(dlc_out):
            jobs += [(os.path.join(dlc_out, p), dlc_out, p) for p in sorted(os.listdir(dlc_out))
                     if os.path.isdir(os.path.join(dlc_out, p))]
        for folder, out_dir, name in jobs:
            subprocess.run([sys.executable, builder, folder, '--out-dir', out_dir, '--name', name, '--verify'],
                           check=True)


if __name__ == '__main__':
    main()
