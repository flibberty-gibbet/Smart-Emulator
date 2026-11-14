import re,sys,json,collections
names={int(k,16):v for k,v in json.load(open('analysis/regnames.json')).items()}
def nm(a):
    if a in names: return names[a]
    if 0x7100<=a<0x7200: return 'Tx_Hvoffset[]'
    if 0x7200<=a<0x7300: return 'HCMValue[]'
    if 0x7300<=a<0x7400: return 'Palette[]'
    if 0x7400<=a<0x7800: return 'SpriteRAM[]'
    if 0x7b80<=a<0x7bc0: return 'SPU_ctrl+%02x'%(a-0x7b80)
    if 0x7c00<=a<0x7e00: return 'SPU_chan[]'
    if 0x7e00<=a<0x8000: return 'SPU_phase[]'
    return '?'
L=open(sys.argv[1]).read().split('\n')
R=collections.Counter(); W=collections.Counter(); IMM=collections.Counter(); funcs=collections.defaultdict(set); cur=None
for l in L:
    m=re.match(r'; ---- sub_([0-9a-f]{6})',l)
    if m: cur=m.group(1); continue
    m=re.match(r'^([0-9a-f]{6}): (?:[0-9a-f]{4} )+\s+(.*)$',l)
    if not m: continue
    t=m.group(2)
    for a in re.findall(r'\[(7[0-9a-f]{3})\]',t):
        a=int(a,16)
        if re.match(r'\[7[0-9a-f]{3}\] (=|\|=|&=|\+=|-=|\^=)',t): W[a]+=1
        else: R[a]+=1
        funcs[a].add(cur)
    m2=re.search(r'r\d = (7[0-9a-f]{3})$',t) or re.search(r'bp = (7[0-9a-f]{3})$',t)
    if m2: IMM[int(m2.group(1),16)]+=1; funcs[int(m2.group(1),16)].add(cur)
allr=sorted(set(R)|set(W)|set(IMM))
out=[]
for a in allr:
    out.append((a,nm(a),R[a],W[a],IMM[a],len(funcs[a])))
print("addr  name                 reads writes ptr-loads funcs")
for o in out: print("%04x  %-20s %5d %5d %5d %5d"%o)
json.dump(out,open('out/census.json','w'))
