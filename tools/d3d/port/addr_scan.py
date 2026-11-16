import re, glob, collections, os
me2 = {m.group(2): m.group(1) for m in re.finditer(r'\(0x([0-9A-F]{8}), "(\w+)"', open('tools/me2_renut_hooks.py').read())}
dis = {m.group(2): m.group(1) for m in re.finditer(r'0x([0-9A-F]{8}) = \{ name = "(\w+)"', open('tools/d3d/dishonored_d3d_funcs.toml').read())}
amap = {me2[n]: dis[n] for n in me2}
files = glob.glob('app/src/**/*.cpp', recursive=True) + glob.glob('app/src/**/*.h', recursive=True)
unknown = collections.defaultdict(set)
for f in files:
    s = open(f, encoding='utf-8', errors='replace').read()
    for a in re.findall(r'sub_(82[A-B][0-9A-F]{5})', s):
        if a not in amap: unknown[a].add(os.path.relpath(f, 'app/src'))
print('mapped ME2 addresses:', len(amap))
for a, fs in sorted(unknown.items()): print('UNMAPPED sub_' + a, sorted(fs))
