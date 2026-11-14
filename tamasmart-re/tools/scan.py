import sys, struct, collections
d=open(sys.argv[1],'rb').read()
n=0x400000  # words to consider
w=struct.unpack_from('<%dH'%n,d,0)
BASE=0x9000
# density of call opcodes with in-range targets per 0x4000-word block
blocks=collections.Counter(); tg=collections.Counter()
for i in range(n-1):
    if (w[i]&0xffc0)==0xf040:
        t=((w[i]&0x3f)<<16)|w[i+1]
        if BASE<=t<BASE+n:
            blocks[(i+BASE)>>14]+=1; tg[t]+=1
print("call density per 0x4000 words (cpu addr: count):")
row=[]
for b in range((BASE>>14),((BASE+n)>>14)+1):
    if blocks[b]: row.append("%06x:%d"%(b<<14,blocks[b]))
print(' '.join(row))
# candidate targets: called >=1 and first word is push (0xd?88-ish: op0=0xd, opn=2 'push')
def is_push(x): return (x&0xf1c0)==0xd080 and ((x>>3)&7)>0
cands=[t for t,c in tg.items() if t-BASE<n]
good=[t for t in cands if is_push(w[t-BASE])]
print("call targets:",len(cands),"starting with push:",len(good))
open(sys.argv[2],'w').write('\n'.join('%x'%t for t in sorted(cands)))
