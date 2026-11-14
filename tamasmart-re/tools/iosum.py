# Summarise register operations in a function: REG = const, REG |= x, REG &= x, read REG
import pickle,re,sys,json
F=pickle.load(open('out/funcs.pkl','rb'))
names={int(k,16):v for k,v in json.load(open('analysis/regnames.json')).items()}
def nm(a): return names.get(a,'io%04x'%a)
def summ(a):
    L=[t for pc,t in F[a]['lines'] if not re.match(r'(ds = r[34]|r[34] = 00|ds = 00|nop)$',t)]
    out=[]; i=0
    while i<len(L):
        t=L[i]
        m=re.match(r'(r\d) = (7[0-9a-f]{3})$',t)
        if m:
            reg=int(m.group(2),16); p=m.group(1)
            nxt=L[i+1] if i+1<len(L) else ''
            prev=L[i-1] if i>0 else ''
            if re.match(r'ds:\[%s\] = (r\d)'%p,nxt):
                src=re.match(r'ds:\[%s\] = (r\d)'%p,nxt).group(1)
                # find how src was computed: look back up to 4 lines
                back=' ; '.join(x for x in L[max(0,i-4):i] if x.startswith(src+' ') or x.startswith('clrb '+src) or x.startswith('setb '+src))
                out.append('%-18s <= %s'%(nm(reg),back or '?'))
                i+=2; continue
            elif re.match(r'r\d = ds:\[%s\]'%p,nxt):
                nn=L[i+2] if i+2<len(L) else ''
                out.append('%-18s read   -> %s'%(nm(reg),nn)); i+=2; continue
        m=re.match(r'call ([0-9a-f]{6})',t)
        if m: out.append('call sub_'+m.group(1))
        i+=1
    return out
for x in sys.argv[1:]:
    a=int(x,16); print('--- sub_%06x'%a); print('\n'.join(summ(a)))
