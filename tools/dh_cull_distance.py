#!/usr/bin/env python3
"""Dishonored (2026-11-09): size-aware draw distance of small static meshes (run after every codegen; post_codegen.py
does). Ported from Lazarus' tools/pch_cull_distance.py (ME2).

Dishonored's UE3 keeps the static mesh relevance of one primitive in sub_82636AC0(SceneRenderer?, View r4 -> r23,
FPrimitiveSceneInfo* r5 -> r25, f1 = distance squared -> f30): f0 = f30 x the view's LOD factor (view +1156); for each
FStaticMesh (PrimitiveSceneInfo +56: the static meshes, data +16 / count +20) it marks View.StaticMeshVisibilityMap
(view +1316) bit Id (+240) when MinDrawDistanceSquared (+224) <= f0 < MaxDrawDistanceSquared (+228). Six sites load
+228 (the first visible LOD's scan and the unrolled marking loop), each followed by `fcmpu cr6,f0,f13`. After each load
this inserts

    <f13> = dh_cull_max_distance_sq(<f13>, <r25>.u32, base, <f0>);

(app/dishonored_src/dh_cull_distance.cpp, dh_cull_small_*: lower for small primitives; the bounds sphere radius is
FPrimitiveSceneInfo +28; f0 only feeds the statistics). The register names are read from the generated text (the
optimised flavour renames some: ll_f13). Idempotent. Exit 1 if the sites are not found as expected.
  python tools/dh_cull_distance.py --gen app/generated/default [--check]
"""
import argparse
import os
import re
import sys

FUNC = 'sub_82636AC0'
MARK = 'dh_cull_max_distance_sq'
SITES = 6
DECL = 'extern "C" double %s(double max_sq, uint32_t primitive, uint8_t* base, double dist_sq);' % MARK
TAG = '  // tools/dh_cull_distance.py'

LOAD = re.compile(r'(\t// lfs f13,228\((r\d+)\)\n\t(\w*f13)\.f64 = double\(REX_LOAD_D_F32\((\w*r\d+)\.u32, 228\)\);\n)'
                  r'(?=\t// fcmpu cr6,f0,f13\n\tcr6\.compare\((\w*f0)\.f64, \3\.f64\);)')
DONE = re.compile(r'\t\w*f13\.f64 = ' + MARK + r'\(')


def fail(msg):
    sys.exit('ERROR (post-codegen patch): dh_cull_distance: ' + msg)


# 2026-11-11: the LOD / draw distance scale (dh_lod_distance_scale). One site: `fmuls f0,f0,f30` (distance squared x the
# view's LOD factor, view +1156); after it  <f0> = dh_lod_distance_sq(<f0>);  so every min / max draw distance test of
# the primitive's static meshes (LOD bands and draw distances alike) sees the primitive that much farther away.
LOD_MARK = 'dh_lod_distance_sq'
LOD_DECL = 'extern "C" double %s(double dist_sq);' % LOD_MARK
LOD_SITE = re.compile(r'(\t// fmuls f0,f0,f30\n\t(\w*f0)\.f64 = double\(float\(\2\.f64 \* \w*f30\.f64\)\);\n)')


def patch_lod(path, check):
    text = open(path, encoding='utf-8', newline='').read()
    start = text.index('DEFINE_REX_FUNC(%s) {' % FUNC)
    end = text.index('\n}\n', start) + 3
    body = text[start:end]
    if (LOD_MARK + '(') in body:
        print('dh_cull_distance: LOD scale site already patched in %s' % os.path.basename(path))
        return
    if check:
        fail('the LOD scale site is not patched in %s' % os.path.basename(path))
    n = len(LOD_SITE.findall(body))
    if n != 1:
        fail('%d sites of `fmuls f0,f0,f30` in %s, 1 expected' % (n, FUNC))
    body = LOD_SITE.sub(
        lambda m: m.group(1) + '\t%s.f64 = %s(%s.f64);%s\n' % (m.group(2), LOD_MARK, m.group(2), TAG), body)
    text = text[:start] + body + text[end:]
    if LOD_DECL not in text:
        first = text.index('DEFINE_REX_FUNC(')
        text = text[:first] + LOD_DECL + '\n\n' + text[first:]
    open(path, 'w', encoding='utf-8', newline='').write(text)
    print('dh_cull_distance: LOD scale site patched in %s (%s)' % (os.path.basename(path), FUNC))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'app',
                                                  'generated', 'default'))
    ap.add_argument('--check', action='store_true', help='only report; exit 1 if the patch is missing')
    a = ap.parse_args()
    path = None
    for f in sorted(os.listdir(a.gen)):
        if re.match(r'\w+_recomp\.\d+\.cpp$', f):
            p = os.path.join(a.gen, f)
            if ('DEFINE_REX_FUNC(%s) {' % FUNC) in open(p, encoding='utf-8', errors='replace').read():
                path = p
                break
    if not path:
        fail('no %s definition in %s' % (FUNC, a.gen))
    patch_lod(path, a.check)
    text = open(path, encoding='utf-8', newline='').read()
    start = text.index('DEFINE_REX_FUNC(%s) {' % FUNC)
    end = text.index('\n}\n', start) + 3
    body = text[start:end]
    # The primitive's register (r25, perhaps renamed): from its first use, `lwz r11,56(r25)`.
    m = re.search(r'\t// lwz r11,56\(r25\)\n\t\w+\.u64 = REX_LOAD_D_U32\((\w*r25)\.u32, 56\);', body)
    if not m:
        fail('the primitive register (r25) is not where expected in %s' % FUNC)
    prim = m.group(1)
    done = len(DONE.findall(body))
    if done == SITES:
        print('dh_cull_distance: already patched (%d sites) in %s' % (done, os.path.basename(path)))
        return
    if a.check:
        fail('%d of %d sites patched in %s' % (done, SITES, os.path.basename(path)))
    if done:
        fail('a partly patched %s (%d of %d sites)' % (FUNC, done, SITES))
    sites = LOAD.findall(body)
    if len(sites) != SITES:
        fail('%d sites of `lfs f13,228(rN)` + `fcmpu cr6,f0,f13` in %s, %d expected' % (len(sites), FUNC, SITES))

    def insert(mm):
        f13, f0 = mm.group(3), mm.group(5)
        return mm.group(1) + '\t%s.f64 = %s(%s.f64, %s.u32, base, %s.f64);%s\n' % (f13, MARK, f13, prim, f0, TAG)

    new_body = LOAD.sub(insert, body)
    text = text[:start] + new_body + text[end:]
    if DECL not in text:
        first = text.index('DEFINE_REX_FUNC(')
        text = text[:first] + DECL + '\n\n' + text[first:]
    open(path, 'w', encoding='utf-8', newline='').write(text)
    print('dh_cull_distance: %d sites patched in %s (%s)' % (SITES, os.path.basename(path), FUNC))


if __name__ == '__main__':
    main()
