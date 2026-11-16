#!/usr/bin/env python3
"""Dishonored (2026-11-10): reads the tables of an uncompressed Dishonored package (UE3 version 801, licensee 30,
big-endian; tools/deko/ue3_decompress.cpp writes compressed ones out uncompressed).

  python tools/dh_package.py <package.upk> [--class Texture2D] [--grep text]   lists exports: index class name
                                                                                size offset
"""
import argparse
import struct


class R:
    def __init__(self, d, o=0):
        self.d, self.o = d, o

    def u32(self):
        v = struct.unpack_from('>I', self.d, self.o)[0]
        self.o += 4
        return v

    def i32(self):
        v = struct.unpack_from('>i', self.d, self.o)[0]
        self.o += 4
        return v

    def u64(self):
        v = struct.unpack_from('>Q', self.d, self.o)[0]
        self.o += 8
        return v

    def fstring(self):
        n = self.i32()
        if n >= 0:
            s = self.d[self.o:self.o + n].rstrip(b'\0').decode('latin-1')
            self.o += n
        else:
            s = self.d[self.o:self.o - 2 * n].decode('utf-16-be').rstrip('\0')
            self.o += -2 * n
        return s


class Package:
    def __init__(self, d):
        self.d = d
        r = R(d)
        assert r.u32() == 0x9E2A83C1
        ver = r.u32()
        self.version, self.licensee = ver & 0xFFFF, ver >> 16
        r.u32()  # total header size
        r.fstring()  # folder
        self.flags = r.u32()
        name_count, name_off = r.u32(), r.u32()
        export_count, export_off = r.u32(), r.u32()
        import_count, import_off = r.u32(), r.u32()
        self.names = []
        n = R(d, name_off)
        for _ in range(name_count):
            self.names.append(n.fstring())
            n.u64()
        self.imports = []
        n = R(d, import_off)
        for _ in range(import_count):
            cls_pkg, cls = (n.i32(), n.i32()), (n.i32(), n.i32())
            outer = n.i32()
            name = (n.i32(), n.i32())
            self.imports.append({'class': self.name(cls), 'outer': outer, 'name': self.name(name)})
        self.exports = []
        n = R(d, export_off)
        for _ in range(export_count):
            e = {'class_ref': n.i32(), 'super': n.i32(), 'outer': n.i32()}
            e['name'] = self.name((n.i32(), n.i32()))
            e['archetype'] = n.i32()
            e['flags'] = n.u64()
            e['size'], e['offset'] = n.i32(), n.i32()
            e['export_flags'] = n.u32()
            gens = n.u32()
            n.o += 4 * gens
            n.o += 16  # guid
            n.u32()  # package flags
            self.exports.append(e)

    def name(self, ref):
        i, num = ref
        s = self.names[i] if 0 <= i < len(self.names) else f'?{i}'
        return s if num == 0 else f'{s}_{num - 1}'

    def obj_name(self, ref):
        if ref > 0:
            return self.exports[ref - 1]['name']
        if ref < 0:
            return self.imports[-ref - 1]['name']
        return 'None'

    def class_name(self, e):
        return 'Class' if e['class_ref'] == 0 else self.obj_name(e['class_ref'])

    def full_name(self, ref):
        parts = []
        while ref:
            if ref > 0:
                parts.append(self.exports[ref - 1]['name'])
                ref = self.exports[ref - 1]['outer']
            else:
                parts.append(self.imports[-ref - 1]['name'])
                ref = self.imports[-ref - 1]['outer']
        return '.'.join(reversed(parts))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package')
    ap.add_argument('--class', dest='cls')
    ap.add_argument('--grep')
    a = ap.parse_args()
    p = Package(open(a.package, 'rb').read())
    print(f'# version {p.version} licensee {p.licensee}: {len(p.names)} names, {len(p.imports)} imports, '
          f'{len(p.exports)} exports')
    for i, e in enumerate(p.exports, 1):
        c = p.class_name(e)
        full = p.full_name(i)
        if a.cls and c != a.cls:
            continue
        if a.grep and a.grep.lower() not in full.lower():
            continue
        print(f'{i:5d} {c:20s} {full:60s} {e["size"]:8d} {e["offset"]:#x}')


if __name__ == '__main__':
    main()
