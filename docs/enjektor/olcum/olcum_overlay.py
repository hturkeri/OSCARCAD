#!/usr/bin/env python3
"""spec.json'daki (r,z) profillerini PDF rasteri uzerine cizer (dogrulama).
Kullanim: pdftoppm -r 600 -png -f 1 -l 1 ef502211-09-DB01-4A-E.pdf p   ->  p-1.png
          python3 olcum_overlay.py p-1.png ../spec.json .
Kalibrasyon (sayfa 1, 600 dpi): eksen x=1884 px, z=0 y=4242 px, 70.8661 px/mm (3:1)."""
import json,math,sys
from PIL import Image,ImageDraw,ImageFont
S=70.8661;AX=1884.0;Y0=4242.0
COL=dict(bas=(230,0,0),girdap_konisi=(0,90,255),tutucu_vida=(0,170,0),filtre_govdesi=(200,0,200),elek=(0,170,170))
def arcpts(a,b,c,R,n=40):
    t0=math.atan2(a[1]-c[1],a[0]-c[0]);t1=math.atan2(b[1]-c[1],b[0]-c[0])
    d=t1-t0
    while d>math.pi:d-=2*math.pi
    while d<-math.pi:d+=2*math.pi
    return [(c[0]+R*math.cos(t0+d*i/n),c[1]+R*math.sin(t0+d*i/n)) for i in range(n+1)]
def polyline(p):
    ks=p['kose'];arcs={a['bas']:a for a in p.get('yaylar',[])}
    out=[]
    for i,k in enumerate(ks):
        nx=ks[(i+1)%len(ks)]
        if k['id'] in arcs:
            a=arcs[k['id']];out+=arcpts((k['r'],k['z']),(nx['r'],nx['z']),a['merkez'],a['R'])[:-1]
        else: out.append((k['r'],k['z']))
    out.append(out[0]);return out
def main(png,spec,outdir):
    D=json.load(open(spec));page=Image.open(png).convert('RGB')
    try: font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',15)
    except Exception: font=ImageFont.load_default()
    def view(name,r0,r1,z0,z1,sc,labels=True,mirror=False,slot=False,parts=None):
        box=(int(AX+r0*S),int(Y0-z1*S),int(AX+r1*S),int(Y0-z0*S))
        im=page.crop(box);W,H=int(im.width*sc),int(im.height*sc)
        im=im.resize((W,H),Image.LANCZOS);im=Image.blend(im,Image.new('RGB',(W,H),'white'),0.35);d=ImageDraw.Draw(im)
        def P(r,z):return ((AX+r*S-box[0])*sc,(Y0-z*S-box[1])*sc)
        for nm,p in D['profiller'].items():
            if parts and nm not in parts: continue
            pl=polyline(p);d.line([P(*q) for q in pl],fill=COL[nm],width=2)
            if mirror:d.line([P(-q[0],q[1]) for q in pl],fill=COL[nm],width=1)
            for k in p['kose']:
                x,y=P(k['r'],k['z'])
                if not(0<=x<W and 0<=y<H):continue
                d.ellipse((x-4,y-4,x+4,y+4),outline=COL[nm],width=2)
                if labels:d.text((x+6,y-8),f"{k['id']} ({k['r']:.2f},{k['z']:.2f})",fill=COL[nm],font=font)
        if slot:
            s=D['donel_olmayan']['slot']
            for key,c in (('kenar_A',(255,120,0)),('kenar_B',(255,120,0)),('taban_cizgisi',(120,0,160))):
                d.line([P(*q) for q in s[key]],fill=c,width=2)
                for q in s[key]:
                    x,y=P(*q);d.rectangle((x-3,y-3,x+3,y+3),outline=c)
        cd=D['donel_olmayan']['capraz_delik'];x,y=P(3.74,cd['z']);rr=cd['cap']/2*S*sc
        if 0<=x<W and 0<=y<H:d.line([P(1.95,cd['z']+1.2),P(3.74,cd['z']+1.2)],fill=(0,170,0),width=2);d.line([P(1.95,cd['z']-1.2),P(3.74,cd['z']-1.2)],fill=(0,170,0),width=2)
        for w in D['donel_olmayan']['filtre_pencereleri']['sira']:
            d.rectangle((*P(3.70,w['z_ust']),*P(4.09,w['z_alt'])),outline=(255,120,0),width=2)
        # mm grid ticks
        for zi in range(math.ceil(z0),math.floor(z1)+1):
            x,y=P(r0,zi);d.line((0,y,10,y),fill=(0,0,0));d.text((12,y-8),f"z={zi}",fill=(0,0,0),font=font)
        for ri in range(math.ceil(r0),math.floor(r1)+1):
            x,y=P(ri,z0);d.line((x,H-10,x,H),fill=(0,0,0));d.text((x+2,H-26),f"r={ri}",fill=(0,0,0),font=font)
        im.save(f"{outdir}/{name}.png")
    view('01_genel_kesit_overlay',-9.6,9.6,-19.2,14.5,0.62,labels=False,mirror=True)
    view('02_bas_ust_bolge',-0.3,9.6,6.6,14.3,2.0,parts=('bas','girdap_konisi'))
    view('03_orifis_girdap_odasi',-0.15,2.5,12.1,14.15,7.0,slot=True,parts=('bas','girdap_konisi'))
    view('04_konik_slot',-0.3,4.7,7.6,13.0,3.6,slot=True,parts=('bas','girdap_konisi','tutucu_vida'))
    view('05_tutucu_vida',-0.3,6.5,-4.0,8.6,1.6,parts=('bas','girdap_konisi','tutucu_vida','filtre_govdesi'))
    view('06_filtre',-0.3,7.4,-19.0,-3.0,1.4,parts=('bas','tutucu_vida','filtre_govdesi','elek'))
if __name__=='__main__': main(*sys.argv[1:4])
