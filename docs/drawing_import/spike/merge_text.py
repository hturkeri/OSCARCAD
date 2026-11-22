# Spike post-pass: symbol-font repair via /Differences glyph names + merge per-glyph runs on a common baseline.
import json, math, sys
SYM = {'asciitilde':'°', 'grave':'±', 'n':'Ø'}          # CAD driver symbol font (PScript5/Inventor) observed in the sample
DIFF = {'/R269': {1:'asciitilde', 2:'grave', 3:'n'}, '/R267': {1:'I', 2:'seven', 3:'zero', 4:'six'}}
NAMES = {'I':'I','seven':'7','zero':'0','six':'6'}
d = json.load(open(sys.argv[1])); runs=[]
for t in d['text']:
    s=t['s']
    if t['font'] in DIFF:
        s=''.join(SYM.get(DIFF[t['font']].get(ord(c),''), NAMES.get(DIFF[t['font']].get(ord(c),''),'?')) for c in s)
    runs.append(dict(t, s=s))
# group: same rotation, same baseline (perpendicular offset < 0.35*size), gap along baseline < 1.2*size
runs.sort(key=lambda r:(round(r['rot']), -r['y'] if round(r['rot'])==0 else r['x']))
used=[False]*len(runs); out=[]
for i,r in enumerate(runs):
    if used[i]: continue
    a=math.radians(r['rot']); ux,uy=math.cos(a),math.sin(a); grp=[r]; used[i]=True; changed=True
    while changed:
        changed=False
        for j,q in enumerate(runs):
            if used[j] or abs(q['rot']-r['rot'])>1: continue
            for g in grp:
                dx,dy=q['x']-g['x'],q['y']-g['y']; along=dx*ux+dy*uy; perp=-dx*uy+dy*ux
                if abs(perp)<0.35*r['size'] and abs(along)<(len(g['s'])*0.6+1.2)*r['size']:
                    grp.append(q); used[j]=True; changed=True; break
    grp.sort(key=lambda g:(g['x']-r['x'])*ux+(g['y']-r['y'])*uy)
    out.append((''.join(g['s'] for g in grp), round(r['x'],1), round(r['y'],1), round(r['rot'])))
for s in out:
    if s[1]<300 and s[2]<640 and any(c.isdigit() for c in s[0]): print(s)
