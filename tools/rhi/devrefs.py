# Functions that reference the global D3D device pointer (ME2 0x833D074C: lis rX,0x833D / lwz|addi rY,0x74C(rX)).
#   python tools/rhi/devrefs.py <outdir> [device pointer address, hex; default ME2's 833D074C]
# Dishonored (2026-11-09): the address as an argument (Dishonored: 834CFE60, lis 0x834D / lwz -0x1A0).
import json,bisect,struct,collections,sys
BASE=0x82000000
img=open("out/default_image.bin","rb").read()
g=json.load(open("out/callgraph.json"))
starts=sorted(int(n[4:],16) for n in g if n.startswith("sub_"))
GDEV=int(sys.argv[2],16) if len(sys.argv)>2 else 0x833D074C
lo=GDEV&0xffff; hi=((GDEV>>16)+(1 if lo&0x8000 else 0))&0xffff  # lis + a signed displacement
hits=collections.defaultdict(int)
text_lo,text_hi=0x82200000,0x83100000
lisreg={}
for a in range(text_lo,text_hi,4):
    w=struct.unpack_from(">I",img,a-BASE)[0]
    op=w>>26
    if op==15:  # addis / lis
        rt=(w>>21)&31; ra=(w>>16)&31; imm=w&0xffff
        if ra==0 and imm==hi: lisreg[rt]=a
    elif op in (32,14,48,36):  # lwz, addi, lfs, stw
        rt=(w>>21)&31; ra=(w>>16)&31; d=w&0xffff
        if d==lo and ra in lisreg and a-lisreg[ra]<0x400:
            i=bisect.bisect_right(starts,a)-1
            hits[starts[i]]+=1
reg=collections.Counter()
for f in hits: reg[f>>20]+=1
print(len(hits),"functions reference GDevice")
for r,n in sorted(reg.items()): print(hex(r<<20),n)
json.dump(sorted("%08X"%f for f in hits),open(__import__("sys").argv[1]+"/devrefs.json","w"))
