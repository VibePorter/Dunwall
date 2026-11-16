#!/usr/bin/env python3
"""Prints a guest function's PowerPC listing from the comments of the generated C++ (no image needed).

  python tools/d3d/gen_disasm.py <generated/default folder> <address> [<address>...]
  python tools/d3d/gen_disasm.py <folder> --calls <address>      only its bl targets, in order
  python tools/d3d/gen_disasm.py <folder> --callers <address>    functions with a bl to it
"""
import glob
import os
import re
import sys


def load(gen):
    funcs = {}
    for path in glob.glob(os.path.join(gen, '*_recomp.*.cpp')):
        cur = None
        for line in open(path, errors='replace'):
            m = re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', line)
            if m:
                cur = funcs.setdefault(int(m.group(1), 16), [])
                continue
            m = re.match(r'\s*// (\S.*)$', line)
            if cur is not None and m and not line.lstrip().startswith('// ---'):
                cur.append(m.group(1))
    return funcs


def main():
    gen, args = sys.argv[1], sys.argv[2:]
    funcs = load(gen)
    if args[0] == '--calls':
        for i in funcs[int(args[1], 16)]:
            if i.startswith('bl '):
                print(i)
    elif args[0] == '--callers':
        target = 'bl 0x%x' % int(args[1], 16)
        for a, body in sorted(funcs.items()):
            if any(i == target for i in body):
                print('sub_%08X' % a)
    else:
        for a in args:
            body = funcs[int(a, 16)]
            print(f'== sub_{int(a, 16):08X} ({len(body)} ins)')
            print('\n'.join(body))


if __name__ == '__main__':
    main()
