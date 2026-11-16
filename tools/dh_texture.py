#!/usr/bin/env python3
"""Dishonored (2026-11-10): Texture2D exports of Dishonored's packages (UE3 801) -> PNG, after ME2's
tools/me2_texture.py (same mips layout and Xenos 2D tiling), with version 801's property tags: a bool's value is one
byte after the tag (size 0), a byte property names its enum (8 bytes) before the value.

  python tools/dh_texture.py info <pkg.upk> <export index>
  python tools/dh_texture.py pngs <pkg.upk> <regex> <out dir>      every Texture2D whose full name matches

Packages: uncompressed (tools/deko/ue3_decompress.cpp). A8R8G8B8 and DXT1/3/5 (DXT blocks big-endian 16-bit words).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import dh_package as dp  # noqa: E402
from me2_texture import tiled_offset_2d  # noqa: E402


class Texture:
    def __init__(self, p, ref):
        self.p, self.ref = p, ref
        e = p.exports[ref - 1]
        b = self.b = p.d[e['offset']:e['offset'] + e['size']]
        self.props = {}
        o = 4  # net index
        while True:
            nm = self.name(o)
            if nm == 'None':
                o += 8
                break
            typ = self.name(o + 8)
            size = struct.unpack_from('>i', b, o + 16)[0]
            o += 24
            if typ == 'BoolProperty':
                self.props[nm] = b[o] != 0
                o += 1
                continue
            if typ == 'StructProperty':
                o += 8
            if typ == 'ByteProperty':
                enum = self.name(o)
                o += 8
                v = self.name(o) if size == 8 and enum != 'None' else (b[o] if size == 1 else b[o:o + size])
            elif typ == 'IntProperty':
                v = struct.unpack_from('>i', b, o)[0]
            elif typ == 'FloatProperty':
                v = struct.unpack_from('>f', b, o)[0]
            elif typ == 'NameProperty':
                v = self.name(o)
            else:
                v = b[o:o + size]
            self.props[nm] = v
            o += size
        self.native_at = o
        o += 16  # source art bulk data header
        count = struct.unpack_from('>I', b, o)[0]
        o += 4
        self.mips = []
        for _ in range(count):
            flags, n, disk, off = struct.unpack_from('>IIII', b, o)
            o += 16
            data_at = o
            if not flags & 0x20 and disk:
                o += disk
            sx, sy = struct.unpack_from('>II', b, o)
            o += 8
            self.mips.append({'flags': flags, 'size': disk, 'data_at': data_at, 'x': sx, 'y': sy,
                              'inline': not flags & 0x20 and disk > 0})

    def name(self, o):
        i, n = struct.unpack_from('>II', self.b, o)
        s = self.p.names[i] if i < len(self.p.names) else f'?{i}'
        return s if n == 0 else f'{s}_{n - 1}'

    def level(self, k=0):
        m = self.mips[k]
        return m, self.b[m['data_at']:m['data_at'] + m['size']] if m['inline'] else None

    def rgba(self, k=0, linear=False):
        m, data = self.level(k)
        if data is None:
            raise ValueError('mip not stored in the export')
        fmt = self.props.get('Format')
        w, h = m['x'], m['y']
        out = bytearray(w * h * 4)
        if fmt == 'PF_A8R8G8B8':
            pitch = max(w, 32)
            for y in range(h):
                for x in range(w):
                    s = (y * w + x) * 4 if linear else tiled_offset_2d(x, y, pitch, 2)
                    a, r, g, bb = data[s:s + 4]
                    d = (y * w + x) * 4
                    out[d:d + 4] = bytes((r, g, bb, a))
            return w, h, bytes(out)
        if fmt in ('PF_DXT1', 'PF_DXT3', 'PF_DXT5'):
            bs = 8 if fmt == 'PF_DXT1' else 16
            bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
            pitch = max(bw, 32)
            log2 = 3 if bs == 8 else 4
            for by in range(bh):
                for bx in range(bw):
                    s = (by * bw + bx) * bs if linear else tiled_offset_2d(bx, by, pitch, log2)
                    blk = data[s:s + bs]
                    # big-endian 16-bit words -> little-endian bytes
                    blk = b''.join(blk[i:i + 2][::-1] for i in range(0, bs, 2))
                    px = decode_block(fmt, blk)
                    for j in range(16):
                        x, y = bx * 4 + (j & 3), by * 4 + (j >> 2)
                        if x < w and y < h:
                            d = (y * w + x) * 4
                            out[d:d + 4] = px[j]
            return w, h, bytes(out)
        raise ValueError(f'format {fmt} not handled')


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def decode_block(fmt, blk):
    if fmt == 'PF_DXT1':
        cb, alpha = blk, None
    else:
        cb = blk[8:]
        if fmt == 'PF_DXT3':
            bits = int.from_bytes(blk[:8], 'little')
            alpha = [((bits >> (4 * i)) & 15) * 17 for i in range(16)]
        else:
            a0, a1 = blk[0], blk[1]
            bits = int.from_bytes(blk[2:8], 'little')
            tab = [a0, a1] + ([((6 - i) * a0 + (i + 1) * a1) // 7 for i in range(6)] if a0 > a1 else
                              [((4 - i) * a0 + (i + 1) * a1) // 5 for i in range(4)] + [0, 255])
            alpha = [tab[(bits >> (3 * i)) & 7] for i in range(16)]
    c0, c1 = struct.unpack_from('<HH', cb, 0)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or fmt != 'PF_DXT1':
        cols = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
        opaque = [255] * 4
    else:
        cols = [p0, p1, tuple((a + b) // 2 for a, b in zip(p0, p1)), (0, 0, 0)]
        opaque = [255, 255, 255, 0]
    idx = struct.unpack_from('<I', cb, 4)[0]
    px = []
    for i in range(16):
        k = (idx >> (2 * i)) & 3
        a = alpha[i] if alpha is not None else opaque[k]
        px.append(bytes(cols[k]) + bytes((a,)))
    return px


def main():
    cmd = sys.argv[1]
    p = dp.Package(open(sys.argv[2], 'rb').read())
    if cmd == 'info':
        t = Texture(p, int(sys.argv[3]))
        print(p.full_name(int(sys.argv[3])), {k: v for k, v in t.props.items() if not isinstance(v, bytes)})
        for m in t.mips:
            print(m)
    elif cmd == 'pngs':
        from PIL import Image
        pat = re.compile(sys.argv[3])
        os.makedirs(sys.argv[4], exist_ok=True)
        for i, e in enumerate(p.exports):
            full = p.full_name(i + 1)
            if p.class_name(e) != 'Texture2D' or not pat.search(full):
                continue
            try:
                t = Texture(p, i + 1)
                w, h, px = t.rgba()
            except Exception as ex:  # noqa: BLE001
                print('skip', full, ex)
                continue
            Image.frombytes('RGBA', (w, h), px).save(os.path.join(sys.argv[4], full + '.png'))
            print(i + 1, full, w, h, t.props.get('Format'))


if __name__ == '__main__':
    main()
