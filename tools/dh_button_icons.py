#!/usr/bin/env python3
"""Dishonored (2026-11-10): the Switch button icons, built from the game's own (its worn-metal style kept, only the
label changed), and the replacement table for Renut (app/dishonored_src/dh_button_icons_data.inc).

  python tools/dh_button_icons.py out/ui_pkg app/dishonored_src/icons/switch_icons_sheet.png app/dishonored_src/icons/dh_button_icons_data.inc [--preview out.png]

  out/ui_pkg: uncompressed packages (tools/deko/ue3_decompress.cpp): Startup.upk (the shared Scaleform library
    Common_assets.lib_*: every button prompt) and UI_Loading*_SF.upk (the loading screens' own "X360_A").
  The art sheet: the user's 15 icons in the game's style (tools/dh_icon_sheet.py cuts them out and fits each to the
    frame of the game icon it replaces; ART below says which goes where).

Mapping, with the port's input (A and B by letter, X and Y by position, as ME2): the game's X (left) shows Y, its Y
(top) shows X; LB/RB/LT/RT -> L/R/ZL/ZR; LS/RS and their clicks; Back/Start -> -/+; the D-pad as the Joy-Con's four buttons (the user's
icons/dpad/dpad-<neutral|up|right|down|left>.png, 2026-11-11). Face buttons
in the Super Famicom's colours (A red, B yellow, X blue, Y green).

Each icon is encoded as DXT5 in the guest's layout (blocks untiled, 16-bit words big-endian) and keyed by the FNV-1a 64
hash of the game's level as Renut's ReadLevel gathers it (untiled blocks, guest byte order).
"""
import argparse
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps

sys.path.insert(0, os.path.dirname(__file__))
import dh_package as dp  # noqa: E402
import dh_texture as dt  # noqa: E402
from dh_icon_sheet import cut, cut_transparent, fit  # noqa: E402

# game icon -> the art sheet's picture (tools/dh_icon_sheet.py SHEET): A and B by letter, X and Y by position (the
# game's X, left, shows the Switch's Y; its Y, top, the Switch's X).
ART = {'I13': 'A', 'I12': 'B', 'I2': 'Y', 'I1': 'X', 'IB': 'L', 'I7': 'R', 'I8': 'ZL', 'I4': 'ZR',
       'IA': 'LStick', 'I6': 'RStick', 'I9': 'LClick', 'I5': 'RClick', 'I11': 'Minus', 'I3': 'Plus'}
from me2_texture import tiled_offset_2d  # noqa: E402

# Common_assets.lib_<id> of Startup.upk -> (Switch label, base icon id or None for itself, label kind)
# kinds: 'face' (light label on a coloured disc), 'plate' (dark label on a light plate), 'stick' (light label on a
# dark disc), 'click' (the stick top with an arrow and a light label under it), 'pill' (dark symbol on a grey pill).
COMMON = {
    'I13': ('A', 'I12', 'face'),   # game A -> Switch A, red (the game's B disc)
    'I12': ('B', 'I1', 'face'),    # game B -> Switch B, yellow (the game's Y disc)
    'I2': ('Y', 'I13', 'face'),    # game X (left) -> Switch Y, green (the game's A disc)
    'I1': ('X', 'I2', 'face'),     # game Y (top) -> Switch X, blue (the game's X disc)
    'IB': ('L', None, 'plate'),
    'I7': ('R', None, 'plate'),
    'I8': ('ZL', None, 'plate'),
    'I4': ('ZR', None, 'plate'),
    'IA': ('L', None, 'stick'),
    'I6': ('R', None, 'stick'),
    'I9': ('L', None, 'click'),
    'I5': ('R', None, 'click'),
    'I11': ('Minus', None, 'pill'),
    'I3': ('Plus', None, 'pill'),
}
# The loading screens' own A (UI_Loading*.X360_A): as the shared A.
# The D-pad (X360_DPad / _Up / _Right / _Down / _Left) -> the user's D-pad pictures (icons/dpad/dpad-<name>.png)
DPAD = {'ID': 'neutral', 'IC': 'up', 'IE': 'right', 'I10': 'down', 'IF': 'left'}
LOADING = ('UI_Loading_SF.upk', 'UI_Loading_LRUS_SF.upk', 'UI_Loading_LCZEHUNPOL_SF.upk')

GLYPH_FILE = {'A': 'A', 'B': 'B', 'X': 'X', 'Y': 'Y', 'L': 'L', 'R': 'R', 'ZL': 'ZL', 'ZR': 'ZR',
              'Minus': 'Minus', 'Plus': 'Plus'}
SCALE = 4


def fill_holes(mask):
    w, h = mask.size
    inv = ImageOps.invert(mask.point(lambda v: 255 if v > 127 else 0))
    padded = Image.new('L', (w + 2, h + 2), 255)
    padded.paste(inv, (1, 1))
    ImageDraw.floodfill(padded, (0, 0), 128)
    outside = padded.crop((1, 1, w + 1, h + 1)).point(lambda v: 255 if v == 128 else 0)
    return ImageOps.invert(outside)


def glyph_label(glyphs, label):
    """The glyph's cut-out (the label's shape) as an L mask, cropped to its box."""
    g = Image.open(os.path.join(glyphs, GLYPH_FILE[label] + '.png')).convert('RGBA').getchannel('A')
    body = fill_holes(g)
    holes = ImageChops.subtract(body, g.point(lambda v: 255 if v > 127 else 0))
    holes = holes.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
    return holes.crop(holes.getbbox())



# --- DXT5 ---------------------------------------------------------------------------------------------------------

def rgb565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def unpack565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def encode_block(px):
    """16 RGBA pixels -> 16 bytes DXT5 (little-endian layout); colour endpoints by the principal axis' extremes."""
    alphas = [p[3] for p in px]
    a0, a1 = max(alphas), min(alphas)
    if a0 == a1:
        abits = 0
    else:
        tab = [a0, a1] + [((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)]
        abits = 0
        for i, a in enumerate(alphas):
            k = min(range(8), key=lambda j: abs(tab[j] - a))
            abits |= k << (3 * i)
    # colour: weighted by alpha so transparent texels do not pull the endpoints
    pts = [(p[0], p[1], p[2]) for p in px if p[3] > 8] or [(p[0], p[1], p[2]) for p in px]
    mean = [sum(c[k] for c in pts) / len(pts) for k in range(3)]
    # principal axis by power iteration on the covariance
    cov = [[sum((c[i] - mean[i]) * (c[j] - mean[j]) for c in pts) for j in range(3)] for i in range(3)]
    v = [1.0, 1.0, 1.0]
    for _ in range(8):
        v = [sum(cov[i][j] * v[j] for j in range(3)) for i in range(3)]
        n = max(abs(x) for x in v) or 1.0
        v = [x / n for x in v]
    proj = [sum((c[k] - mean[k]) * v[k] for k in range(3)) for c in pts]
    lo, hi = pts[proj.index(min(proj))], pts[proj.index(max(proj))]
    c0, c1 = rgb565(hi), rgb565(lo)
    if c0 < c1:
        c0, c1 = c1, c0
    p0, p1 = unpack565(c0), unpack565(c1)
    pal = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
    cbits = 0
    for i, p in enumerate(px):
        k = min(range(4), key=lambda j: sum((pal[j][t] - p[t]) ** 2 for t in range(3)))
        cbits |= k << (2 * i)
    if c0 == c1:
        cbits = 0
    return bytes((a0, a1)) + abits.to_bytes(6, 'little') + struct.pack('<HHI', c0, c1, cbits)


def encode_dxt5_guest(img):
    """RGBA image -> untiled DXT5 blocks in the guest's byte order (16-bit words big-endian)."""
    w, h = img.size
    bw, bh = (w + 3) // 4, (h + 3) // 4
    px = img.load()
    out = bytearray()
    for by in range(bh):
        for bx in range(bw):
            blk = []
            for j in range(16):
                x, y = min(w - 1, bx * 4 + (j & 3)), min(h - 1, by * 4 + (j >> 2))
                blk.append(px[x, y])
            le = encode_block(blk)
            out += b''.join(le[i:i + 2][::-1] for i in range(0, 16, 2))
    return bytes(out)


def guest_untiled(tex):
    """The game's level 0 as Renut's ReadLevel gathers it: blocks untiled, the guest's bytes."""
    m, data = tex.level(0)
    bw, bh = (m['x'] + 3) // 4, (m['y'] + 3) // 4
    pitch = max(bw, 32)
    out = bytearray()
    for by in range(bh):
        for bx in range(bw):
            s = tiled_offset_2d(bx, by, pitch, 4)
            out += data[s:s + 16]
    return bytes(out), bw, bh


def encode_blocks_region(img, x0, y0, x1, y1):
    """The DXT5 blocks of img touching pixels [x0, x1) x [y0, y1), in the guest's layout: (block x, block y, blocks
    wide, blocks high, bytes). Only these blocks are stored: the rest of the game's texture never leaves the game."""
    px = img.load()
    bx0, by0, bx1, by1 = x0 // 4, y0 // 4, (x1 + 3) // 4, (y1 + 3) // 4
    out = bytearray()
    for by in range(by0, by1):
        for bx in range(bx0, bx1):
            blk = [px[bx * 4 + (j & 3), by * 4 + (j >> 2)] for j in range(16)]
            le = encode_block(blk)
            out += b''.join(le[i:i + 2][::-1] for i in range(0, 16, 2))
    return bx0, by0, bx1 - bx0, by1 - by0, bytes(out)


def fnv1a64(b):
    h = 0xCBF29CE484222325
    for c in b:
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ui_pkg')
    ap.add_argument('art', help='the art sheet (app/dishonored_src/icons/switch_icons_sheet.png)')
    ap.add_argument('out')
    ap.add_argument('--preview')
    ap.add_argument('--dpad', help='the D-pad pictures (default: icons/dpad next to the art sheet)')
    a = ap.parse_args()
    sheet = Image.open(a.art)
    if 'A' in sheet.getbands():  # transparent background (2026-11-11 sheet)
        art = cut_transparent(sheet)
    else:  # the first sheet: beige background, labels under the icons
        sheet = sheet.convert('RGB')
        corners = sorted((sheet.getpixel((x, y)) for x in range(0, sheet.width, 50) for y in (2, sheet.height - 3)),
                         key=sum)
        art = cut(sheet, corners[len(corners) // 2])
    st = dp.Package(open(os.path.join(a.ui_pkg, 'Startup.upk'), 'rb').read())
    lib = {}
    for i, e in enumerate(st.exports, 1):
        full = st.full_name(i)
        if st.class_name(e) == 'Texture2D' and full.startswith('Common_assets.lib_'):
            lib[full.split('_')[-1]] = dt.Texture(st, i)

    def picture(t):
        w, h, px = t.rgba()
        return Image.frombytes('RGBA', (w, h), px)

    entries, previews = [], []
    for gid, (label, base_id, kind) in COMMON.items():
        t = lib[gid]
        orig = picture(t)
        new = fit(art[ART[gid]], orig)
        raw, bw, bh = guest_untiled(t)
        entries.append((fnv1a64(raw), bw, bh, encode_dxt5_guest(new), f'Common_assets.lib_{gid} -> {label}'))
        previews.append((f'{gid}->{label}', orig, new))
        if gid == 'I13':
            a_icon = new
    dpad_dir = a.dpad or os.path.join(os.path.dirname(a.art), 'dpad')
    # 2026-11-11 (user: bigger): the whole 40x40 icon less a pixel, not the old disc's box; the pressed button is
    # drawn in the picture, so no room is kept for the old arrow tips
    frame = Image.new('RGBA', picture(lib['ID']).size, (0, 0, 0, 0))
    ImageDraw.Draw(frame).rectangle((1, 1, frame.width - 2, frame.height - 2), fill=(255, 255, 255, 255))
    dpad_box = None
    for name in DPAD.values():
        b = Image.open(os.path.join(dpad_dir, f'dpad-{name}.png')).convert('RGBA').getchannel('A').getbbox()
        dpad_box = b if dpad_box is None else (min(dpad_box[0], b[0]), min(dpad_box[1], b[1]),
                                                max(dpad_box[2], b[2]), max(dpad_box[3], b[3]))
    for gid, name in DPAD.items():
        t = lib[gid]
        orig = picture(t)
        art_img = Image.open(os.path.join(dpad_dir, f'dpad-{name}.png')).convert('RGBA')
        # all five on one frame (the game's own boxes differ: its arrow tips stick out), the pictures cropped to one
        # common box so the four buttons stay in place
        new = fit(art_img.crop(dpad_box), frame)
        raw, bw, bh = guest_untiled(t)
        entries.append((fnv1a64(raw), bw, bh, encode_dxt5_guest(new), f'Common_assets.lib_{gid} -> D-pad {name}'))
        previews.append((f'{gid}->{name}', orig, new))
    # The D-pad drawn by the shared library's sprite lib_DPAD_Multi_b (the options' controller screen, beside the D-pad
    # labels): its base is a sub-image of the atlas lib_IAA (DefineSubImage 189: pixels (419, 368)-(499, 448), 80x80),
    # with white arrows (sub-image 191) sliding in from 26.5 px out for a lit direction (not shown on the controller
    # screen). The plain D-pad picture fills it (78 px; 2026-11-11, user: bigger); only the blocks under it change.
    atlases = []
    t = lib['IAA']
    atlas = picture(t)
    raw, bw, bh = guest_untiled(t)
    atlas_raw = raw
    x0, y0, x1, y1 = 419, 368, 499, 448
    frame80 = Image.new('RGBA', (x1 - x0, y1 - y0), (0, 0, 0, 0))
    ImageDraw.Draw(frame80).rectangle((1, 1, 78, 78), fill=(255, 255, 255, 255))
    neutral = Image.open(os.path.join(dpad_dir, 'dpad-neutral.png')).convert('RGBA').crop(dpad_box)
    base = fit(neutral, frame80)
    patched = atlas.copy()
    patched.paste(base, (x0, y0))  # the whole region replaced (the old disc filled it)
    atlases.append((fnv1a64(raw), bw, bh, encode_blocks_region(patched, x0, y0, x1, y1),
                    'Common_assets.lib_IAA (atlas) -> D-pad base of lib_DPAD_Multi_b'))
    previews.append(('IAA dpad', atlas.crop((x0, y0, x1, y1)), base))
    for pk in LOADING:
        p = dp.Package(open(os.path.join(a.ui_pkg, pk), 'rb').read())
        for i, e in enumerate(p.exports, 1):
            if p.class_name(e) == 'Texture2D' and p.full_name(i).endswith('.X360_A'):
                t = dt.Texture(p, i)
                raw, bw, bh = guest_untiled(t)
                orig = picture(t)
                new = fit(art['A_loading'], orig)
                h = fnv1a64(raw)
                if all(h != x[0] for x in entries):
                    entries.append((h, bw, bh, encode_dxt5_guest(new), f'{p.full_name(i)} -> A'))
                    previews.append(('load A', orig, new))
    with open(a.out, 'w', newline='\n') as f:
        f.write('// Generated by tools/dh_button_icons.py: the game\'s button icons (DXT5, untiled guest blocks) -> the\n'
                '// Switch ones in the same layout. Do not edit.\n')
        for k, (h, bw, bh, data, what) in enumerate(entries):
            f.write(f'// {what}\nalignas(16) static const uint8_t kIcon{k}[{len(data)}] = {{')
            f.write(','.join(str(b) for b in data))
            f.write('};\n')
        f.write('static const IconReplacement kIcons[] = {\n')
        for k, (h, bw, bh, data, what) in enumerate(entries):
            f.write(f'    {{0x{h:016X}ull, {bw}, {bh}, kIcon{k}, "{what}"}},\n')
        f.write('};\n')
        for k, (h, bw, bh, (rx, ry, rw, rh, data), what) in enumerate(atlases):
            f.write(f'// {what}: the {rw} x {rh} blocks under it\n'
                    f'alignas(16) static const uint8_t kAtlas{k}[{len(data)}] = {{\n')
            for j in range(0, len(data), 64):
                f.write(','.join(str(b) for b in data[j:j + 64]) + ',\n')
            f.write('};\n')
        # a quick first test for the atlas' size (512x512 DXT5 is common): the hash of its first 4 KB
        f.write(f'static constexpr uint64_t kAtlasPrefixHash = 0x{fnv1a64(atlas_raw[:4096]):016X}ull;\n')
        f.write('static const AtlasPatch kAtlases[] = {\n')
        for k, (h, bw, bh, (rx, ry, rw, rh, data), what) in enumerate(atlases):
            f.write(f'    {{0x{h:016X}ull, {bw}, {bh}, {rx}, {ry}, {rw}, {rh}, kAtlas{k}, "{what}"}},\n')
        f.write('};\n')
    print(f'{len(entries)} icons, {len(atlases)} atlas -> {a.out}')
    if a.preview:
        z = 3
        cw = max(o.width for _, o, _ in previews) * z + 8
        ch = max(o.height for _, o, _ in previews) * z + 4
        sheet = Image.new('RGBA', (cw * 2 + 70, (ch + 4) * len(previews)), (70, 66, 60, 255))
        d = ImageDraw.Draw(sheet)
        for k, (name, o, n) in enumerate(previews):
            y = k * (ch + 4)
            d.text((2, y + ch // 2 - 6), name, fill=(255, 255, 255, 255))
            sheet.alpha_composite(o.resize((o.width * z, o.height * z), Image.NEAREST), (70, y))
            sheet.alpha_composite(n.resize((n.width * z, n.height * z), Image.NEAREST), (70 + cw, y))
        sheet.save(a.preview)


if __name__ == '__main__':
    main()
