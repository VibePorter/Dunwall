"""Minimal AS2 disassembler for GFx movies inside decompressed UE3 packages.

usage: as2dis.py <package.upk> <grep word> [context actions]
Finds every GFX/FWS movie in the file, walks its tags (DoAction, DoInitAction, DefineSprite nested), disassembles
the actions with the constant pool resolved, and prints each action block (function body) that mentions the word,
with absolute file offsets.
"""
import struct
import sys

NAMES = {
    0x00: 'End', 0x04: 'NextFrame', 0x06: 'Play', 0x07: 'Stop', 0x0A: 'Add', 0x0B: 'Subtract', 0x0C: 'Multiply',
    0x0D: 'Divide', 0x0E: 'Equals', 0x0F: 'Less', 0x10: 'And', 0x11: 'Or', 0x12: 'Not', 0x13: 'StringEquals',
    0x17: 'Pop', 0x18: 'ToInteger', 0x1C: 'GetVariable', 0x1D: 'SetVariable', 0x20: 'SetTarget2', 0x21: 'StringAdd',
    0x26: 'Trace', 0x3A: 'Delete', 0x3B: 'Delete2', 0x3C: 'DefineLocal', 0x3D: 'CallFunction', 0x3E: 'Return',
    0x3F: 'Modulo', 0x40: 'NewObject', 0x41: 'DefineLocal2', 0x42: 'InitArray', 0x43: 'InitObject', 0x44: 'TypeOf',
    0x46: 'Enumerate', 0x47: 'Add2', 0x48: 'Less2', 0x49: 'Equals2', 0x4A: 'ToNumber', 0x4B: 'ToString',
    0x4C: 'PushDuplicate', 0x4D: 'StackSwap', 0x4E: 'GetMember', 0x4F: 'SetMember', 0x50: 'Increment',
    0x51: 'Decrement', 0x52: 'CallMethod', 0x53: 'NewMethod', 0x54: 'InstanceOf', 0x55: 'Enumerate2', 0x60: 'BitAnd',
    0x61: 'BitOr', 0x62: 'BitXor', 0x63: 'BitLShift', 0x64: 'BitRShift', 0x65: 'BitURShift', 0x66: 'StrictEquals',
    0x67: 'Greater', 0x68: 'StringGreater', 0x69: 'Extends', 0x87: 'StoreRegister', 0x88: 'ConstantPool',
    0x8E: 'DefineFunction2', 0x94: 'With', 0x96: 'Push', 0x99: 'Jump', 0x9B: 'DefineFunction', 0x9D: 'If',
    0x9F: 'GotoFrame2', 0x8B: 'SetTarget', 0x8C: 'GoToLabel', 0x81: 'GotoFrame', 0x83: 'GetURL', 0x9A: 'GetURL2',
}


def cstr(b, o):
    e = b.index(b'\0', o)
    return b[o:e].decode('utf-8', 'replace'), e + 1


def disasm(b, start, end, base, pool, out):
    o = start
    while o < end:
        at = o
        code = b[o]
        o += 1
        length = 0
        if code >= 0x80:
            length = struct.unpack_from('<H', b, o)[0]
            o += 2
        body = b[o:o + length]
        name = NAMES.get(code, 'op%02X' % code)
        arg = ''
        if code == 0x88:
            n = struct.unpack_from('<H', body, 0)[0]
            p = 2
            pool[:] = []
            for _ in range(n):
                s, p = cstr(body, p)
                pool.append(s)
            arg = '%d entries' % n
        elif code == 0x96:
            p, items = 0, []
            while p < length:
                t = body[p]
                p += 1
                if t == 0:
                    s, p = cstr(body, p); items.append(repr(s))
                elif t == 1:
                    items.append('%g' % struct.unpack_from('<f', body, p)[0]); p += 4
                elif t == 2:
                    items.append('null')
                elif t == 3:
                    items.append('undef')
                elif t == 4:
                    items.append('r%d' % body[p]); p += 1
                elif t == 5:
                    items.append('true' if body[p] else 'false'); p += 1
                elif t == 6:
                    lo, hi = struct.unpack_from('<II', body, p); items.append('%g' % struct.unpack('<d', struct.pack('<II', hi, lo))[0]); p += 8
                elif t == 7:
                    items.append('%d' % struct.unpack_from('<i', body, p)[0]); p += 4
                elif t == 8:
                    i = body[p]; p += 1; items.append('c%d:%r' % (i, pool[i] if i < len(pool) else '?'))
                elif t == 9:
                    i = struct.unpack_from('<H', body, p)[0]; p += 2; items.append('C%d:%r' % (i, pool[i] if i < len(pool) else '?'))
                else:
                    items.append('?type%d' % t); break
            arg = ', '.join(items)
        elif code in (0x99, 0x9D):
            off = struct.unpack_from('<h', body, 0)[0]
            arg = '-> %X' % (base + o + length + off)
        elif code == 0x87:
            arg = 'r%d' % body[0]
        elif code in (0x8E, 0x9B):
            fname, p = cstr(body, 0)
            if code == 0x9B:
                n = struct.unpack_from('<H', body, p)[0]; p += 2
                for _ in range(n):
                    _, p = cstr(body, p)
                size = struct.unpack_from('<H', body, p)[0]
            else:
                n = struct.unpack_from('<H', body, p)[0]; p += 2
                p += 1 + 2  # register count, flags
                for _ in range(n):
                    p += 1
                    _, p = cstr(body, p)
                size = struct.unpack_from('<H', body, p)[0]
            arg = '%r params %d body %d bytes' % (fname, n, size)
        out.append((base + at, name, arg))
        o += length
        if code == 0 and o >= end:
            break


def walk_tags(b, o, end, base, blocks):
    while o < end:
        hdr = struct.unpack_from('<H', b, o)[0]
        o += 2
        code, length = hdr >> 6, hdr & 0x3F
        if length == 0x3F:
            length = struct.unpack_from('<I', b, o)[0]
            o += 4
        if code == 0:
            break
        if code == 12:
            blocks.append((o, o + length))
        elif code == 59:
            blocks.append((o + 2, o + length))
        elif code == 39:
            walk_tags(b, o + 4, o + length, base, blocks)
        o += length


def movies(d):
    o = 0
    while True:
        i = d.find(b'GFX', o)
        if i < 0:
            return
        if d[i + 3] in (8, 9, 10):
            size = struct.unpack_from('<I', d, i + 4)[0]
            if 1000 < size < len(d) - i + 16:
                yield i, size
        o = i + 3


def main():
    d = open(sys.argv[1], 'rb').read()
    word = sys.argv[2]
    for start, size in movies(d):
        b = d[start:start + size]
        nbits = b[8] >> 3
        o = 8 + (5 + 4 * nbits + 7) // 8 + 4
        blocks = []
        walk_tags(b, o, len(b), start, blocks)
        for s, e in blocks:
            pool, out = [], []
            try:
                disasm(b, s, e, start, pool, out)
            except Exception as ex:  # noqa: BLE001
                out.append((start + s, 'ERR', str(ex)))
            text = '\n'.join('%08X %-16s %s' % x for x in out)
            if word in text:
                print('=== movie at %X, block %X-%X' % (start, start + s, start + e))
                lines = text.split('\n')
                hits = [i for i, l in enumerate(lines) if word in l]
                ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 25
                shown = set()
                for h in hits:
                    for i in range(max(0, h - ctx), min(len(lines), h + ctx)):
                        if i not in shown:
                            shown.add(i)
                            print(lines[i])
                    print('   ...')


main()
