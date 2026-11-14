import pickle,re,sys,json
F=pickle.load(open('out/funcs.pkl','rb'))
names={int(k,16):v for k,v in json.load(open('analysis/regnames.json')).items()}
def show(a,maxl=400):
    if a not in F: print("sub_%06x not found"%a); return
    n=0
    print("--- sub_%06x (%d insns)"%(a,len(F[a]['lines'])))
    for pc,t in F[a]['lines']:
        if re.match(r'(ds = r[34]|r[34] = 00|ds = 00|nop)$',t): continue
        m=re.search(r'= (7[0-9a-f]{3})$',t)
        if m and int(m.group(1),16) in names: t+='        ; '+names[int(m.group(1),16)]
        m=re.search(r'\[(7[0-9a-f]{3})\]',t)
        if m and int(m.group(1),16) in names: t+='        ; '+names[int(m.group(1),16)]
        print('%06x: %s'%(pc,t)); n+=1
        if n>=maxl: print('...'); break
for x in sys.argv[1:]:
    if ':' in x: a,m=x.split(':'); show(int(a,16),int(m))
    else: show(int(x,16))
