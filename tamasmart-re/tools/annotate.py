# Adds register names and known function names to a trace listing.
# usage: python3 tools/annotate.py out/trace.asm analysis/tamasmart_eng_code.asm
import csv, json, re, sys
names = {int(k, 16): v for k, v in json.load(open('analysis/regnames.json')).items()}
syms = {int(r['address'], 16): r['name'] for r in csv.DictReader(open('analysis/symbols.csv'))}
scenes = {}
try:
    for r in csv.DictReader(open('analysis/scene_table.csv')):
        for k in ('init', 'update', 'exit', 'extra'):
            scenes.setdefault(int(r[k], 16), []).append('scene_%s_%s' % (r['scene_id'], k))
except FileNotFoundError:
    pass
out = open(sys.argv[2], 'w')
out.write("; Tamagotchi Smart ENG RAW firmware - recursive-descent disassembly (unSP 2.0)\n"
          "; CPU word address = flash byte offset / 2 + 0x9000. Regenerate with tools/regen.sh.\n")
for l in open(sys.argv[1]):
    l = l.rstrip('\n')
    m = re.match(r'; ---- sub_([0-9a-f]{6})(.*)', l)
    if m:
        a = int(m.group(1), 16); label = []
        if a in syms: label.append(syms[a])
        if a in scenes: label.append('/'.join(scenes[a][:3]) + ('…' if len(scenes[a]) > 3 else ''))
        if label: l = '; ---- sub_%s = %s%s' % (m.group(1), ' '.join(label), m.group(2))
    elif re.match(r'^[0-9a-f]{6}:', l):
        m = re.search(r'(?:\[|= )(7[0-9a-f]{3})\]?', l)
        if m and int(m.group(1), 16) in names and int(m.group(1), 16) < 0x7c00:
            l = '%-52s ; %s' % (l, names[int(m.group(1), 16)])
        m = re.search(r'call ([0-9a-f]{6})$', l)
        if m and int(m.group(1), 16) in syms:
            l = '%-52s ; %s' % (l, syms[int(m.group(1), 16)])
    out.write(l + '\n')
out.close()
