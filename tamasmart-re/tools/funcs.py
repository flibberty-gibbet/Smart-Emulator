# Parse a trace listing into functions; build call graph + I/O usage; helper queries.
import re,sys,json,collections,pickle
names={int(k,16):v for k,v in json.load(open('analysis/regnames.json')).items()}
def regname(a):
    if a in names: return names[a]
    if 0x7100<=a<0x7300: return 'PPU_lineRAM'
    if 0x7300<=a<0x7400: return 'Palette'
    if 0x7400<=a<0x7800: return 'SpriteRAM'
    if 0x7b80<=a<0x7bc0: return 'SPU_ctrl%02x'%(a-0x7b80)
    if 0x7c00<=a<0x8000: return 'SPU_chan'
    return 'io%04x'%a
def load(path):
    F=collections.OrderedDict(); cur=None
    for l in open(path):
        m=re.match(r'; ---- sub_([0-9a-f]{6})',l)
        if m:
            cur=int(m.group(1),16); F[cur]={'lines':[],'calls':[],'io':collections.Counter(),'ram':collections.Counter(),'imm':[]}; continue
        m=re.match(r'^([0-9a-f]{6}): ((?:[0-9a-f]{4} )+)\s+(.*)$',l)
        if not m or cur is None: continue
        a=int(m.group(1),16); t=m.group(3).strip(); f=F[cur]; f['lines'].append((a,t))
        m2=re.match(r'(call|goto) ([0-9a-f]{6})',t)
        if m2: f['calls'].append((m2.group(1),int(m2.group(2),16)))
        for x in re.findall(r'\[([0-9a-f]{4})\]',t):
            x=int(x,16)
            w=bool(re.match(r'\[[0-9a-f]{4}\] (=|\|=|&=|\+=|-=|\^=)',t))
            if 0x7000<=x<0x8000: f['io'][(x,'w' if w else 'r')]+=1
            elif x<0x2800: f['ram'][(x,'w' if w else 'r')]+=1
        m3=re.search(r'= ([0-9a-f]{4})$',t)
        if m3:
            v=int(m3.group(1),16)
            if 0x7000<=v<0x7c00: f['io'][(v,'p')]+=1
            f['imm'].append(v)
    return F
def label(F,a):
    f=F.get(a)
    if not f: return 'sub_%06x(?)'%a
    regs=sorted(set(regname(x) for (x,k) in f['io']))
    s='sub_%06x[%d]'%(a,len(f['lines']))
    if regs: s+=' {'+','.join(regs[:6])+('…' if len(regs)>6 else '')+'}'
    return s
def tree(F,a,depth,maxd,seen,out,prefix=''):
    out.append(prefix+label(F,a))
    if depth>=maxd or a in seen or a not in F: return
    seen.add(a)
    done=set()
    for k,t in F[a]['calls']:
        if t in done: continue
        done.add(t); tree(F,t,depth+1,maxd,seen,out,prefix+'  ')
if __name__=='__main__':
    F=load(sys.argv[1]); pickle.dump(F,open('out/funcs.pkl','wb'))
    cmd=sys.argv[2]
    if cmd=='tree':
        out=[]; tree(F,int(sys.argv[3],16),0,int(sys.argv[4]),set(),out); print('\n'.join(out))
    elif cmd=='show':
        for a,t in F[int(sys.argv[3],16)]['lines']: print('%06x: %s'%(a,t))
    elif cmd=='users':   # functions touching a register
        r=int(sys.argv[3],16)
        for a,f in F.items():
            ks=[k for (x,k) in f['io'] if x==r]
            if ks: print(label(F,a), ''.join(sorted(ks)))
    elif cmd=='callers':
        t=int(sys.argv[3],16)
        for a,f in F.items():
            if any(x==t for _,x in f['calls']): print(label(F,a))
