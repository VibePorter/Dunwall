#!/usr/bin/env python3
"""bspatch (BSDIFF40): python bspatch.py <old> <new> <patch>"""
import bz2
import struct
import sys


def offtin(b):
    v = struct.unpack('<Q', b)[0]
    return -(v & 0x7FFFFFFFFFFFFFFF) if v & (1 << 63) else v


def main():
    old = open(sys.argv[1], 'rb').read()
    patch = open(sys.argv[3], 'rb').read()
    if patch[:8] != b'BSDIFF40':
        sys.exit('not BSDIFF40')
    ctrl_len, diff_len, new_size = offtin(patch[8:16]), offtin(patch[16:24]), offtin(patch[24:32])
    ctrl = bz2.decompress(patch[32:32 + ctrl_len])
    diff = bz2.decompress(patch[32 + ctrl_len:32 + ctrl_len + diff_len])
    extra = bz2.decompress(patch[32 + ctrl_len + diff_len:])
    new = bytearray(new_size)
    old_pos = new_pos = ci = di = ei = 0
    while new_pos < new_size:
        x, y, z = offtin(ctrl[ci:ci + 8]), offtin(ctrl[ci + 8:ci + 16]), offtin(ctrl[ci + 16:ci + 24])
        ci += 24
        for i in range(x):
            o = old[old_pos + i] if 0 <= old_pos + i < len(old) else 0
            new[new_pos + i] = (diff[di + i] + o) & 0xFF
        di += x
        new_pos += x
        old_pos += x
        new[new_pos:new_pos + y] = extra[ei:ei + y]
        ei += y
        new_pos += y
        old_pos += z
    open(sys.argv[2], 'wb').write(new)
    print(f'{sys.argv[2]}: {new_size} bytes')


main()
