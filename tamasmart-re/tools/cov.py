import re,sys,collections
L=open(sys.argv[1]).read().split('\n')
cov=set(); n=0
for l in L:
    m=re.match(r'^([0-9a-f]{6}): ((?:[0-9a-f]{4} )+)',l)
    if m:
        a=int(m.group(1),16); k=len(m.group(2).split()); n+=1
        for i in range(k): cov.add(a+i)
print("insns",n,"words covered",len(cov))
def rng(lo,hi): 
    c=sum(1 for a in range(lo,hi) if a in cov); return "%06x-%06x: %.0f%%"%(lo,hi,100*c/(hi-lo))
for lo in range(0x9000,0x98000,0x8000): print(rng(lo,min(lo+0x8000,0x98000)), end='  ')
print()
for lo in range(0x158000,0x180000,0x8000): print(rng(lo,lo+0x8000), end='  ')
print()
bad=[l for l in L if '<BAD>' in l or '<inv' in l or re.search(r':\s+[0-9a-f]{4}\s+--',l)]
print("suspicious:",len(bad)); print('\n'.join(bad[:10]))
