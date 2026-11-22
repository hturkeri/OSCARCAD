# Synthetic drawing sheet with known dimension texts -> clean.png, scan.png (skew 1.5 deg, blur, noise, JPEG)
import sys, random, numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W,H=3508,2480  # A4 landscape @300dpi
im=Image.new('L',(W,H),255); d=ImageDraw.Draw(im)
F=lambda s: ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',s)
d.rectangle((60,60,W-60,H-60),outline=0,width=6)
# front view of a flange: outer rect + bore, centreline dash-dot
d.rectangle((600,700,1400,1700),outline=0,width=5); d.rectangle((900,700,1100,1700),outline=0,width=3)
for y in range(650,1760,60): d.line((1000,y,1000,y+30),fill=0,width=2); d.line((1000,y+40,1000,y+44),fill=0,width=2)
# side view circle
d.ellipse((2000,700,3000,1700),outline=0,width=5); d.ellipse((2400,1100,2600,1300),outline=0,width=3)
for a in range(4):
    import math; cx=2500+350*math.cos(a*math.pi/2+math.pi/4); cy=1200+350*math.sin(a*math.pi/2+math.pi/4)
    d.ellipse((cx-40,cy-40,cx+40,cy+40),outline=0,width=3)
truth=[(620,1850,'80'),(400,1150,'Ø100'),(950,560,'Ø20 H7'),(2300,520,'4x Ø6.6'),(2700,1780,'R5'),(1500,900,'12±0.1'),
       (1500,1000,'2x45°'),(2300,1900,'M8x1.25'),(1500,1100,'30°'),(1500,1200,'Ø40 +0.05/-0'),(2800,2250,'ÖLÇEK 1:2'),(2800,2330,'MALZEME: Ti-6Al-4V')]
for x,y,s in truth: d.text((x,y),s,font=F(56),fill=0)
im.save(sys.argv[1]+'/clean.png')
sc=im.rotate(1.5,resample=Image.BICUBIC,fillcolor=255,expand=False).filter(ImageFilter.GaussianBlur(1.2))
a=np.array(sc).astype(np.float32); a+=np.random.default_rng(1).normal(0,18,a.shape); a=np.clip(a,0,255).astype(np.uint8)
Image.fromarray(a).save(sys.argv[1]+'/scan.jpg',quality=60)
print('\n'.join(s for _,_,s in truth))
