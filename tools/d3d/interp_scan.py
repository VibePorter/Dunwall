# Dishonored (2026-11-08): dump each shader container's interpolator table (usage, usage index, register) so the
# register linkage Renut uses can be compared with the semantic linkage D3DVertexShader_Bind does on the Xbox.
import os, struct, sys, collections
U = ["POS","BW","BI","NRM","PSIZE","TEX","TAN","BIN","TESS","POST","COL","FOG","DEP","SMP"]+["?"]*2
def be(d,o): return struct.unpack_from(">I",d,o)[0]
def table(d):
    if len(d) < 36 or (be(d,0) & 0xFFFFFF00) != 0x102A1100: return None
    vs = be(d,0) & 1
    so = be(d,24)
    info = be(d,so+20); n = (info>>5)&0x1F
    if vs:
        f18 = be(d,so+24); ve = be(d,so+28); base = so+36+4*(f18+ve)
    else:
        base = so+32
    out=[]
    for i in range(n):
        v = be(d,base+4*i)
        out.append((U[(v>>4)&15], v&15, (v>>8)&15))
    return ("VS" if vs else "PS"), out
root = sys.argv[1]
for f in sorted(os.listdir(root)):
    d = open(os.path.join(root,f),"rb").read()
    try: t = table(d)
    except struct.error: t = None
    if t: print(f, t[0], " ".join("%s%d=r%d"%x for x in t[1]))
