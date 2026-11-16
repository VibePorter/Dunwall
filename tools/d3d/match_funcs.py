#!/usr/bin/env python3
"""Finds a game's copies of another game's functions by their PowerPC listing (generated C++ comments).

  python tools/d3d/match_funcs.py <from generated/default> <to generated/default> <address>...

Branches are compared as offsets from the function start; 'loose' also masks call targets and lis/addi/ori/ld/st
immediates (addresses of globals differ between images). Prints exact and loose matches per address.
"""
import re, sys, os
sys.path.insert(0, os.path.dirname(__file__))
from gen_disasm import load

HEX = re.compile(r'0x([0-9a-f]{8})')


def norm(addr, body, loose):
    out = []
    for ins in body:
        def rel(m):
            t = int(m.group(1), 16)
            return f'@{t - addr:+x}' if abs(t - addr) < 0x100000 and not ins.startswith('bl ') else 'ADDR'
        s = HEX.sub(rel, ins)
        if loose:
            op = s.split(' ')[0]
            if op in ('bl', 'b') and 'ADDR' in s:
                s = op + ' CALL'
            elif op in ('lis', 'addi', 'ori', 'oris', 'addis') or op.startswith(('l', 'st')):
                s = re.sub(r'(-?\d+)(?=\(|$)', 'IMM', s)
        out.append(s)
    return tuple(out)


def main():
    src, dst = load(sys.argv[1]), load(sys.argv[2])
    index = {}
    for loose in (False, True):
        index[loose] = {}
        for a, b in dst.items():
            index[loose].setdefault(norm(a, b, loose), []).append(a)
    for arg in sys.argv[3:]:
        a = int(arg, 16)
        body = src.get(a)
        if body is None:
            print(f'{a:08X}: not in source'); continue
        exact = index[False].get(norm(a, body, False), [])
        loose = [x for x in index[True].get(norm(a, body, True), []) if x not in exact]
        print(f'{a:08X} ({len(body)} ins): exact {" ".join("%08X" % x for x in exact) or "-"}'
              f'  loose {" ".join("%08X" % x for x in loose[:6]) or "-"}{" ..." if len(loose) > 6 else ""}')


if __name__ == '__main__':
    main()
