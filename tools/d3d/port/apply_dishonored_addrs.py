"""Puts Dishonored's verified Direct3D addresses and device globals into the renderer sources (one-time port step)."""
import glob
import os
import re

f = 'app/src/renut_engine/gpu/d3d/hooks/device.cpp'
s = open(f, encoding='utf-8').read()
for a, b in [
    ('kDeviceOffsetsMinusOne[] = {21560, 21564, 14936};',
     'kDeviceOffsetsMinusOne[] = {21968, 21972, 15340};  /* ME2 21560, 21564, 14936; Dishonored hw init sub_82B135B8, SetRingBufferParameters */'),
    ('kGlobalQwordVa = 0x83387E90;',
     'kGlobalQwordVa = 0x83495AC8;  /* ME2 0x83387E90; Dishonored CreateDevice `lis r11,-31927; ld r11,23240(r11)` */'),
    ('kVdGlobalDeviceImportSlot = 0x82000800;',
     'kVdGlobalDeviceImportSlot = 0x820007BC;  /* ME2 0x82000800; Dishonored IsFencePending sub_82B01D28 */'),
    ('kMe2GlobalDevice = 0x833D074C;',
     'kMe2GlobalDevice = 0x834CFE60;  /* ME2 0x833D074C; Dishonored CreateDevice `stw r31,-416(r30)` */'),
    ('device_va + 16568 /*me2: displayed swaps, flip handler sub_82AA4A38*/',
     'device_va + 16976 /*displayed swaps (ME2 16568, flip handler sub_82AA4A38)*/'),
]:
    assert s.count(a) == 1, a
    s = s.replace(a, b)
open(f, 'w', encoding='utf-8', newline='\n').write(s)

me2 = {m.group(2): m.group(1) for m in re.finditer(r'\(0x([0-9A-F]{8}), "(\w+)"', open('tools/me2_renut_hooks.py').read())}
dis = {m.group(2): m.group(1)
       for m in re.finditer(r'0x([0-9A-F]{8}) = \{ name = "(\w+)"', open('tools/d3d/dishonored_d3d_funcs.toml').read())}
amap = {me2[n]: dis[n] for n in me2}
total = 0
for path in glob.glob('app/src/**/*.cpp', recursive=True) + glob.glob('app/src/**/*.h', recursive=True):
    if os.path.normpath(path).endswith(os.path.normpath('hooks/state.cpp')):
        continue  # not compiled (N&B's state setters)
    text = open(path, encoding='utf-8', errors='surrogateescape').read()
    count = 0

    def rep(m):
        global count
        a = m.group(1).upper()
        if a in amap:
            count += 1
            return m.group(0)[:-8] + amap[a]
        return m.group(0)

    new = re.sub(r'(?:sub_|0x)([0-9A-Fa-f]{8})(?![0-9A-Fa-f])', rep, text)
    if count:
        open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='\n').write(new)
        total += count
        print(count, path)
print('total', total)
