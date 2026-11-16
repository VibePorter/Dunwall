import sys, re
sys.path.insert(0, 'tools/d3d')
from gen_disasm import load
def stores(F, va=None, lo=None, hi_=None):
    out = []
    for a, b in F.items():
        hi = {}
        for i, x in enumerate(b):
            m = re.match(r'lis (r\d+),(-?\d+)$', x)
            if m: hi[m.group(1)] = int(m.group(2)) & 0xFFFF; continue
            m = re.match(r'(stw|lwz) (r\d+),(-?\d+)\((r\d+)\)$', x)
            if m and m.group(4) in hi:
                v = (hi[m.group(4)] << 16) + int(m.group(3))
                if (va is not None and v == va) or (lo is not None and lo <= v < hi_):
                    out.append((a, i, m.group(1), v))
    return out
import os
R = load(os.environ['ME2_GENERATED'])  # Mass Effect 2's app/generated/default (a Lazarus tree)
w = [s for s in stores(R, 0x833D074C) if s[2] == 'stw']
print('ME2 writers of 0x833D074C:', ['sub_%08X@%d' % (a, i) for a, i, _, _ in w])
for a, i, _, _ in w[:3]:
    print('  context:', ' | '.join(R[a][max(0, i - 6):i + 2]))
