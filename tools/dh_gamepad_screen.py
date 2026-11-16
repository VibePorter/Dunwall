#!/usr/bin/env python3
"""Dishonored (2026-11-11): the options' controller screen (UI_GamepadMapping.Options_GamepadMapping_Xenon, 840x600
DXT5, one level) with the user's Switch picture (app/dishonored_src/icons/switch_gamepad_screen.png) and leader lines
from each button to where the game puts its label.

  python tools/dh_gamepad_screen.py out/ui_pkg app/dishonored_src/icons/switch_gamepad_screen.png
      app/dishonored_src/icons/dh_gamepad_screen_data.inc [--preview out.png] [--png picture.png]

The labels are text fields of the options movie (Startup.upk UI_OptionsMenu.OptionsMenu: sprite o_Options_padMapping,
its _txt_mc, frame "Xenon"), fixed in place; the game's picture carried the lines to them. The picture is loaded into
_img_mc at (97.1, -62.6), centred, so a label at (x, y) of the movie's sprite is at (x + 322.9, y + 362.6) here.
Labels on the left are right-aligned (their right end is fixed), the others left-aligned. Mapping as the button icons
(tools/dh_button_icons.py): the game's Y (top) -> X, X (left) -> Y, A -> A, B -> B, LB/LT -> L/ZL, RB/RT -> R/ZR,
Back/Start -> -/+, sticks and their clicks. The D-pad's labels sit below the picture round their own icon (the shared
library's D-pad), so they get no lines.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
import dh_package as dp  # noqa: E402
import dh_texture as dt  # noqa: E402
from dh_button_icons import encode_dxt5_guest, fnv1a64, guest_untiled  # noqa: E402

W, H = 840, 600
OFF = (322.9, 362.6)
# label: (x, y, width, height, align) of the text field in the movie's sprite (frame "Xenon")
LABELS = {
    'LShoulder': (-600.95, -202.0, 330, 30.75, 'right'), 'LTrigger': (-571.95, -251.0, 330, 30.75, 'right'),
    'Back': (-194.0, -236.0, 164, 30.75, 'right'), 'Start': (28.0, -235.05, 188, 30.75, 'left'),
    'RShoulder': (285.05, -206.0, 330, 30.75, 'left'), 'RTrigger': (244.05, -255.0, 330, 30.75, 'left'),
    'Y': (260.0, -142.05, 334, 35, 'left'), 'X': (260.0, -105.25, 331, 35, 'left'),
    'B': (260.0, -68.45, 331, 35, 'left'), 'A': (260.0, -31.55, 329, 35, 'left'),
    'RS': (294.05, 79.0, 330, 30.75, 'left'), 'RS_Click': (294.05, 118.0, 330, 30.75, 'left'),
    'LS': (-604.95, -93.0, 330, 30.75, 'right'), 'LS_Click': (-421.95, -54.0, 147, 30.75, 'right'),
    'DPAD_U': (-478.0, 162.95, 343, 30.75, 'center'), 'DPAD_D': (-479.0, 248.95, 344, 30.75, 'center'),
    'DPAD_L': (-628.95, 206.75, 290.4, 30.75, 'right'), 'DPAD_R': (-274.0, 206.75, 191.95, 30.75, 'left'),
}
# the Switch's parts in the picture (its pixels): centre, radius
PARTS = {
    'ZL': ((398, 206), 3), 'L': ((314, 226), 3), 'ZR': ((1330, 206), 3), 'R': ((1413, 226), 3),
    'Minus': ((432, 257), 14), 'Plus': ((1295, 257), 16),
    'X': ((1345, 299), 20), 'Y': ((1306, 336), 20), 'A': ((1383, 336), 20), 'B': ((1345, 373), 20),
    'LStick': ((385, 332), 42), 'RStick': ((1345, 467), 44),
}
# label -> the part its line points at
LINES = {
    'LTrigger': 'ZL', 'LShoulder': 'L', 'RTrigger': 'ZR', 'RShoulder': 'R', 'Back': 'Minus', 'Start': 'Plus',
    'Y': 'X', 'X': 'Y', 'B': 'B', 'A': 'A', 'LS': 'LStick', 'LS_Click': 'LStick', 'RS': 'RStick', 'RS_Click': 'RStick',
}
SCALE = 0.432            # picture -> screen texels (the console between the label columns)
ORIGIN = (-48.7, 144.6)  # the picture's (0, 0) on the screen texture
LINE = (226, 230, 220)
BEND = 22                # the line's horizontal run into the label
# Back and Start (Journal / Pause): their labels carry the button's icon at the inner end ("Journal (-)", "(+) Pause",
# measured in the game: its centre 19 px in from the right-aligned box's right end / 17 px in from the left-aligned
# box's left end, 2 px below the text's middle, radius ~12). Their buttons are below them, so the line comes up from
# below to the icon (as the game's own picture did) instead of running in under the icon.
FROM_BELOW = {'Back': -19, 'Start': 17}  # icon centre from the box's end
ICON_R = 12
RISE = 14                # the vertical run up to the icon


def to_tex(p):
    return ORIGIN[0] + p[0] * SCALE, ORIGIN[1] + p[1] * SCALE


def label_anchor(name):
    x, y, w, h, align = LABELS[name]
    x, y = x + OFF[0], y + OFF[1]
    mid = y + 14.5
    return (x + w - 4, mid) if align == 'right' else (x - 2, mid)


def compose(art):
    big = 4
    canvas = Image.new('RGBA', (W * big, H * big), (0, 0, 0, 0))
    a = art.resize((round(art.width * SCALE * big), round(art.height * SCALE * big)), Image.LANCZOS)
    canvas.alpha_composite(a, (round(ORIGIN[0] * big), round(ORIGIN[1] * big)))
    lines = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lines)
    for label, part in LINES.items():
        (cx, cy), r = PARTS[part]
        bx, by = to_tex((cx, cy))
        if label in FROM_BELOW:
            x, y, w, h, align = LABELS[label]
            x, y = x + OFF[0], y + OFF[1]
            ix = (x + w if align == 'right' else x) + FROM_BELOW[label]
            lx, ly = ix, y + 14.5 + 2 + ICON_R + 3   # just under the icon
            kx, ky = ix, ly + RISE
        else:
            lx, ly = label_anchor(label)
            kx, ky = (lx - BEND if lx > bx else lx + BEND), ly
        dx, dy = kx - bx, ky - by
        n = max(1e-6, (dx * dx + dy * dy) ** 0.5)
        rr = r * SCALE + 2.5
        sx, sy = bx + dx / n * rr, by + dy / n * rr
        d.line([(sx * big, sy * big), (kx * big, ky * big), (lx * big, ly * big)], fill=LINE + (255,),
               width=round(1.7 * big), joint='curve')
        e = 2.2 * big
        d.ellipse((sx * big - e, sy * big - e, sx * big + e, sy * big + e), fill=LINE + (255,))
    # a soft dark halo under the lines, as the picture's outlines have
    halo = lines.getchannel('A').filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(6))
    shade = Image.new('RGBA', canvas.size, (8, 10, 14, 0))
    shade.putalpha(halo.point(lambda v: v * 150 // 255))
    canvas.alpha_composite(shade)
    canvas.alpha_composite(lines)
    # premultiplied downscale, so transparent texels do not darken the edges
    r, g, b, al = canvas.split()
    zero = Image.new('L', canvas.size, 0)
    pm = Image.merge('RGBA', tuple(Image.composite(c, zero, al) for c in (r, g, b)) + (al,)).resize((W, H),
                                                                                                  Image.LANCZOS)
    out = Image.new('RGBA', (W, H))
    src, dst = pm.load(), out.load()
    for y in range(H):
        for x in range(W):
            pr, pg, pb, pa = src[x, y]
            dst[x, y] = (0, 0, 0, 0) if pa == 0 else (min(255, pr * 255 // pa), min(255, pg * 255 // pa),
                                                      min(255, pb * 255 // pa), pa)
    return out


def preview(img, path):
    """The screen as the game lays it out (1280x720, the sprite at the centre), with sample labels."""
    stage = Image.new('RGBA', (1280, 720), (24, 22, 20, 255))
    cx, cy = 640, 360
    stage.alpha_composite(img, (round(cx + 97.1 - W / 2), round(cy - 62.6 - H / 2)))
    d = ImageDraw.Draw(stage)
    for name, (x, y, w, h, align) in LABELS.items():
        x, y = cx + x, cy + y
        d.rectangle((x, y, x + w, y + h), outline=(90, 90, 70, 255))
        t = 'Action ' + name
        tw = d.textlength(t)
        tx = x + w - tw if align == 'right' else (x + (w - tw) / 2 if align == 'center' else x)
        d.text((tx, y + 8), t, fill=(227, 242, 214, 255))
    stage.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ui_pkg')
    ap.add_argument('art')
    ap.add_argument('out')
    ap.add_argument('--preview')
    ap.add_argument('--png', help='also save the composed 840x600 picture')
    a = ap.parse_args()
    img = compose(Image.open(a.art).convert('RGBA'))
    if a.png:
        img.save(a.png)
    if a.preview:
        preview(img, a.preview)
    p = dp.Package(open(os.path.join(a.ui_pkg, 'UI_GamepadMapping.upk'), 'rb').read())
    idx = next(i for i, e in enumerate(p.exports, 1) if p.full_name(i) == 'Options_GamepadMapping_Xenon')
    raw, bw, bh = guest_untiled(dt.Texture(p, idx))
    assert (bw, bh) == (W // 4, H // 4), (bw, bh)
    data = encode_dxt5_guest(img)
    with open(a.out, 'w', newline='\n') as f:
        f.write("// Generated by tools/dh_gamepad_screen.py: the options' controller picture (UI_GamepadMapping.\n"
                "// Options_GamepadMapping_Xenon, DXT5, untiled guest blocks) -> the Switch one. Do not edit.\n")
        f.write(f'alignas(16) static const uint8_t kGamepadScreen[{len(data)}] = {{\n')
        for k in range(0, len(data), 64):
            f.write(','.join(str(v) for v in data[k:k + 64]) + ',\n')
        f.write('};\n')
        f.write('static const IconReplacement kScreens[] = {\n')
        f.write(f'    {{0x{fnv1a64(raw):016X}ull, {bw}, {bh}, kGamepadScreen, '
                f'"UI_GamepadMapping.Options_GamepadMapping_Xenon -> Switch"}},\n}};\n')
    print(f'{a.out}: {bw}x{bh} blocks')


if __name__ == '__main__':
    main()
