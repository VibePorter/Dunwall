import os, re, sys, difflib
sys.path.insert(0, 'tools/d3d')
from gen_disasm import load
ref = load(os.environ['ME2_GENERATED'])  # Mass Effect 2's app/generated/default (a Lazarus tree)
ours = load('app/generated/default')
pairs = {}
for line in open('out/d3d/match_me2.txt'):
    m = re.match(r'(\w+)\s+ref 0x([0-9A-F]+) .*-> sub_([0-9A-F]+) ([\d.]+)', line)
    if m: pairs[int(m.group(2), 16)] = (m.group(1), int(m.group(3), 16), float(m.group(4)), 'check' in line)
names = {r: n for r, (n, _, _, _) in pairs.items()}
def calls(f, a): return [int(i.split()[1], 16) for i in f.get(a, []) if i.startswith('bl ')]
# Evidence: for each matched pair, align their call lists; a named ref callee lines up with which of our callees?
votes = {}
for r, (n, o, score, chk) in pairs.items():
    rc, oc = calls(ref, r), calls(ours, o)
    sm = difflib.SequenceMatcher(None, [names.get(x, '?') for x in rc], [names.get(x, '?') if False else '?' for x in oc])
    if len(rc) == len(oc):
        for x, y in zip(rc, oc):
            if x in names: votes.setdefault(names[x], []).append((n, y))
for r, (n, o, score, chk) in sorted(pairs.items(), key=lambda p: p[1][0]):
    v = votes.get(n, [])
    agree = sum(1 for _, y in v if y == o)
    other = sorted({'sub_%08X' % y for _, y in v if y != o})
    flag = 'CHECK' if chk else ''
    print(f'{n:34s} sub_{o:08X} {score:.2f} {flag:5s} callers agree {agree}/{len(v)} {"other: " + ",".join(other) if other else ""}')
