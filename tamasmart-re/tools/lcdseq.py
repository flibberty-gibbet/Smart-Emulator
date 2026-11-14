import pickle,re,sys
F=pickle.load(open('out/funcs.pkl','rb'))
def seq(fa,cmdf=0x077a8d,dataf=0x077aa9,delayf=0x077973):
    last=None; out=[]
    for a,t in F[fa]['lines']:
        m=re.match(r'r3 = (-?[0-9a-f]+)$',t)
        if m: last=int(m.group(1),16)&0xffff if not m.group(1).startswith('-') else (-int(m.group(1)[1:],16))&0xffff
        m=re.match(r'r3 =- ([0-9a-f]+)$',t)
        if m: last=(-int(m.group(1),16))&0xffff
        m=re.match(r'call ([0-9a-f]{6})',t)
        if m:
            c=int(m.group(1),16)
            if c==cmdf: out.append(('CMD',last))
            elif c==dataf: out.append(('dat',last))
            elif c==delayf: out.append(('delay',last))
            else: out.append(('call_%06x'%c,last))
    return out
s=seq(int(sys.argv[1],16))
line=''
for k,v in s:
    if k=='CMD': 
        if line: print(line)
        line='CMD %02x :'%v
    elif k=='dat': line+=' %02x'%v
    else:
        if line: print(line); line=''
        print('   %s(%s)'%(k,hex(v) if v is not None else '?'))
if line: print(line)
