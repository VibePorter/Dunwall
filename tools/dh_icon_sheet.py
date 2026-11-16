"""Dishonored (2026-11-10): the Switch button icons from the user's art sheet (app/dishonored_src/icons/
switch_icons_sheet.jpg: 15 icons on a beige background, 5 x 3 cells, in the order of the reference sheet
out/ui_icons/glyphs_to_replace.png), each cut out and fitted to the frame of the game icon it replaces
(tools/dh_button_icons.py).

Cut-out: the background reached from the cell's border through pixels close to the beige (flood fill) is transparent;
pixels near that region get a soft alpha from their distance to the beige, with the beige taken out of their colour.
"""
from PIL import Image, ImageDraw, ImageFilter

# sheet cell -> what it shows (the sheet's order; its X and Y are in the reference sheet's X / Y slots unswapped)
SHEET = ['A', 'B', 'X', 'Y', 'L', 'R', 'ZL', 'ZR', 'LStick', 'RStick', 'LClick', 'RClick', 'Minus', 'Plus', 'A_loading']
COLS, ROWS = 5, 3
ICON_TOP_FRACTION = 0.72  # the icon's part of a cell (the labels below)


def dist(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5


def cut(sheet, bg, near=48.0, soft=95.0):
    w, h = sheet.size
    cw, ch = w / COLS, h / ROWS
    out = {}
    for k, name in enumerate(SHEET):
        x0, y0 = int((k % COLS) * cw), int((k // COLS) * ch)
        cell = sheet.crop((x0, y0, int(x0 + cw), int(y0 + ch * ICON_TOP_FRACTION))).convert('RGB')
        cwid, chei = cell.size
        px = cell.load()
        # flood the background from the border through near-beige pixels
        mark = Image.new('L', cell.size, 0)
        mp = mark.load()
        stack = [(x, y) for x in range(cwid) for y in (0, chei - 1)] + [(x, y) for y in range(chei) for x in (0, cwid - 1)]
        while stack:
            x, y = stack.pop()
            if mp[x, y] or dist(px[x, y], bg) > near or sum(px[x, y]) > sum(bg) + 30:  # white is not beige
                continue
            mp[x, y] = 255
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < cwid and 0 <= yy < chei and not mp[xx, yy]:
                    stack.append((xx, yy))
        # soft edge: pixels within 2 px of the background get alpha from their distance to the beige
        edge = mark.filter(ImageFilter.MaxFilter(5))
        ep = edge.load()
        rgba = Image.new('RGBA', cell.size, (0, 0, 0, 0))
        op = rgba.load()
        for y in range(chei):
            for x in range(cwid):
                if mp[x, y]:
                    continue
                c = px[x, y]
                if ep[x, y]:
                    a = max(0.0, min(1.0, (dist(c, bg) - near * 0.5) / (soft - near * 0.5)))
                    if a <= 0:
                        continue
                    # un-blend the beige: c = a * fg + (1 - a) * bg
                    fg = tuple(max(0, min(255, int(round((c[i] - (1 - a) * bg[i]) / a)))) for i in range(3))
                    op[x, y] = fg + (int(round(a * 255)),)
                else:
                    op[x, y] = c + (255,)
        box = rgba.getchannel('A').point(lambda v: 255 if v > 24 else 0).getbbox()
        out[name] = rgba.crop(box)
    return out


def _runs(flags):
    out, start = [], None
    for i, f in enumerate(flags):
        if f and start is None:
            start = i
        elif not f and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, len(flags)))
    return out


def cut_transparent(sheet, threshold=24):
    """2026-11-11 sheet (switch_icons_sheet.png): the icons on a transparent background, in the same order (3 rows
    of 5, no labels). Rows and columns are split at the fully transparent gaps."""
    sheet = sheet.convert('RGBA')
    solid = sheet.getchannel('A').point(lambda v: 255 if v > threshold else 0)
    w, h = sheet.size
    sp = solid.load()
    rows = _runs([any(sp[x, y] for x in range(0, w, 2)) for y in range(h)])
    boxes = []
    for y0, y1 in rows:
        cols = _runs([any(sp[x, y] for y in range(y0, y1)) for x in range(w)])
        boxes += [(x0, y0, x1, y1) for x0, x1 in cols]
    if len(boxes) != len(SHEET):
        raise SystemExit(f'sheet: {len(boxes)} icons found, {len(SHEET)} expected')
    out = {}
    for name, box in zip(SHEET, boxes):
        icon = sheet.crop(box)
        out[name] = icon.crop(icon.getchannel('A').getbbox())
    return out


def fit(art, game):
    """art placed on the game icon's frame: scaled to the box of the game icon's solid pixels, centred on it."""
    gbox = game.getchannel('A').point(lambda v: 255 if v > 96 else 0).getbbox() or (0, 0) + game.size
    abox = art.getchannel('A').point(lambda v: 255 if v > 96 else 0).getbbox() or (0, 0) + art.size
    art = art.crop(abox)
    gw, gh = gbox[2] - gbox[0], gbox[3] - gbox[1]
    s = min(gw / art.width, gh / art.height)
    # work at 4x, then down, for clean edges
    big = 4
    a = art.resize((max(1, round(art.width * s * big)), max(1, round(art.height * s * big))), Image.LANCZOS)
    canvas = Image.new('RGBA', (game.width * big, game.height * big), (0, 0, 0, 0))
    cx, cy = (gbox[0] + gbox[2]) / 2 * big, (gbox[1] + gbox[3]) / 2 * big
    canvas.alpha_composite(a, (int(round(cx - a.width / 2)), int(round(cy - a.height / 2))))
    # premultiplied downscale, so transparent texels do not darken the edges
    r, g, b, al = canvas.split()
    pm = Image.merge('RGBA', tuple(Image.composite(ch, Image.new('L', canvas.size, 0), al) for ch in (r, g, b)) + (al,))
    small = pm.resize(game.size, Image.LANCZOS)
    sr, sg, sb, sa = small.split()
    out = Image.new('RGBA', game.size)
    op, rp, gp, bp, ap = out.load(), sr.load(), sg.load(), sb.load(), sa.load()
    for y in range(game.height):
        for x in range(game.width):
            al_ = ap[x, y]
            if al_ == 0:
                op[x, y] = (0, 0, 0, 0)
            else:
                op[x, y] = tuple(min(255, rp[x, y] * 255 // al_) if i == 0 else
                                 min(255, (gp if i == 1 else bp)[x, y] * 255 // al_) for i in range(3)) + (al_,)
    return out
