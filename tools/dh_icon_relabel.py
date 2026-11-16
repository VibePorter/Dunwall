"""Dishonored (2026-11-10): relabels one of the game's button icons (tools/dh_button_icons.py): the old label found by
its colour against the face, refilled with the face's own texture, the new label drawn in the old label's colour.

Kinds: label box (fractions of the icon: left, top, right, bottom), label direction (lighter or darker than the face),
the new label's size against the old label's box, and how the old label's pixels are refilled.
"""
import math

from PIL import Image, ImageChops, ImageFilter

SCALE = 4

KINDS = {
    'face': ((0.2, 0.18, 0.8, 0.82), 'light', 1.0, 'rotate'),
    'stick': ((0.2, 0.2, 0.8, 0.8), 'light', 0.9, 'rotate'),
    'plate': ((0.1, 0.12, 0.92, 0.9), 'dark', 0.78, 'rows'),
    'click': ((0.15, 0.55, 0.85, 1.0), 'light', 0.95, 'avg'),
    'pill': ((0.22, 0.2, 0.78, 0.8), 'dark', 0.7, 'rows'),
}
# Per kind: a fixed detection threshold (None: from the face's spread), how much the Switch glyph's strokes are
# thinned (pixels at 4x; the game's condensed font is lighter than the glyphs'), and which part of the old label's
# colours to take (a percentile from its darkest; light labels take a high one).
TUNING = {
    'face': (None, 0, 0.75),
    'stick': (12.0, 0, 0.9),
    'plate': (None, 3, 0.25),
    'click': (12.0, 2, 0.9),
    'pill': (None, 0, 0.25),
}


def luma(px):
    r, g, b = px[:3]
    return 0.299 * r + 0.587 * g + 0.114 * b


def label_mask(img, kind):
    """The old label's pixels (L mask, native size): inside the label box, far from the face colour (measured on the
    opaque pixels outside the box), closed and grown by one pixel."""
    w, h = img.size
    box, direction, _, _ = KINDS[kind]
    bx0, by0, bx1, by1 = box[0] * w, box[1] * h, box[2] * w, box[3] * h
    inner = img.getchannel('A').point(lambda v: 255 if v > 200 else 0).filter(ImageFilter.MinFilter(5))
    px, al = img.load(), inner.load()
    face = sorted(luma(px[x, y]) for y in range(h) for x in range(w)
                  if al[x, y] and not (bx0 <= x < bx1 and by0 <= y < by1))
    if not face:
        face = sorted(luma(px[x, y]) for y in range(h) for x in range(w) if al[x, y])
    med = face[len(face) // 2]
    mad = sorted(abs(v - med) for v in face)[len(face) // 2]
    fixed = TUNING[kind][0]
    t = fixed if fixed is not None else max(14.0, 2.5 * mad)
    m = Image.new('L', (w, h), 0)
    mp = m.load()
    for y in range(h):
        for x in range(w):
            if not (bx0 <= x < bx1 and by0 <= y < by1) or px[x, y][3] < 128:
                continue
            d = luma(px[x, y]) - med
            if (d > t) if direction == 'light' else (d < -t):
                mp[x, y] = 255
    return m.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))


def inpaint(img, mask):
    """Fills the masked pixels from their unmasked neighbours (repeated 3x3 averaging of what is known)."""
    w, h = img.size
    px = img.load()
    known = [[mask.getpixel((x, y)) < 128 for x in range(w)] for y in range(h)]
    todo = [(x, y) for y in range(h) for x in range(w) if not known[y][x]]
    while todo:
        nxt, fills = [], []
        for x, y in todo:
            acc, n = [0, 0, 0, 0], 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and known[yy][xx]:
                        p = px[xx, yy]
                        for k in range(4):
                            acc[k] += p[k]
                        n += 1
            if n >= 3:
                fills.append((x, y, tuple(v // n for v in acc)))
            else:
                nxt.append((x, y))
        if not fills:
            break
        for x, y, p in fills:
            px[x, y] = p
            known[y][x] = True
        todo = nxt
    return img


def refill(img, mask, how):
    """Masked pixels refilled with the face's own texture: 'rotate' takes the nearest unmasked pixel on the same
    circle around the centre (discs), 'rows' the nearest unmasked pixel above or below (plates, pills), 'clear' makes
    them transparent (the click icons' label sits on nothing). What stays unfilled is averaged in."""
    w, h = img.size
    px, mk = img.load(), mask.load()
    al = img.getchannel('A').load()
    out = img.copy()
    op = out.load()
    left = Image.new('L', (w, h), 0)
    lp = left.load()
    cx, cy = (w - 1) / 2, (h - 1) / 2
    for y in range(h):
        for x in range(w):
            if mk[x, y] < 128:
                continue
            if how == 'clear':
                op[x, y] = (0, 0, 0, 0)
                continue
            if how == 'avg':
                lp[x, y] = 255
                continue
            found = None
            if how == 'rotate':
                r = math.hypot(x - cx, y - cy)
                a0 = math.atan2(y - cy, x - cx)
                steps = max(16, int(2 * math.pi * r))
                for k in range(1, steps // 2 + 1):
                    for sgn in (1, -1):
                        a = a0 + sgn * 2 * math.pi * k / steps
                        xx, yy = int(round(cx + r * math.cos(a))), int(round(cy + r * math.sin(a)))
                        if 0 <= xx < w and 0 <= yy < h and mk[xx, yy] < 128 and al[xx, yy] > 200:
                            found = px[xx, yy]
                            break
                    if found:
                        break
            else:
                for k in range(1, h):
                    for yy in (y - k, y + k):
                        if 0 <= yy < h and mk[x, yy] < 128 and al[x, yy] > 200:
                            found = px[x, yy]
                            break
                    if found:
                        break
            if found:
                op[x, y] = found[:3] + (px[x, y][3],)
            else:
                lp[x, y] = 255
    if how != 'clear' and left.getbbox():
        out = inpaint(out, left)
    if how == 'clear':
        return out
    soft = out.filter(ImageFilter.GaussianBlur(0.6))
    return Image.composite(soft, out, mask.filter(ImageFilter.GaussianBlur(0.8)))


def relabel(base, kind, label_shape, colour_from=None):
    """base: the icon (RGBA, native size); label_shape: the new label's L mask; colour_from: the icon whose label
    colour to use (default base)."""
    colour_from = colour_from or base
    _, direction, size, how = KINDS[kind]
    old = label_mask(colour_from, kind)
    cols = sorted((colour_from.getpixel((x, y)) for y in range(colour_from.height) for x in range(colour_from.width)
                   if old.getpixel((x, y)) > 128 and colour_from.getpixel((x, y))[3] > 200), key=luma)
    if cols:
        colour = cols[min(len(cols) - 1, int(len(cols) * TUNING[kind][2]))]
    else:
        colour = (230, 230, 230, 255)
    own = label_mask(base, kind)
    clean = refill(base, own, how)
    box = own.getbbox() or (base.width // 4, base.height // 4, base.width * 3 // 4, base.height * 3 // 4)
    big = clean.resize((base.width * SCALE, base.height * SCALE), Image.LANCZOS)
    bx0, by0, bx1, by1 = (v * SCALE for v in box)
    bw, bh = (bx1 - bx0) * size, (by1 - by0) * size
    s = min(bh / label_shape.height, bw / label_shape.width)
    lab = label_shape.resize((max(1, int(label_shape.width * s)), max(1, int(label_shape.height * s))), Image.LANCZOS)
    for _ in range(TUNING[kind][1]):
        lab = lab.filter(ImageFilter.MinFilter(3))
    cx, cy = (bx0 + bx1) / 2, (by0 + by1) / 2
    pos = (int(round(cx - lab.width / 2)), int(round(cy - lab.height / 2)))
    mask = Image.new('L', big.size, 0)
    mask.paste(lab, pos)
    layer = Image.new('RGBA', big.size, tuple(colour[:3]) + (255,))
    layer.putalpha(mask if how == 'clear' else ImageChops.multiply(mask, big.getchannel('A')))
    out = Image.alpha_composite(big, layer)
    return out.resize(base.size, Image.LANCZOS), own
