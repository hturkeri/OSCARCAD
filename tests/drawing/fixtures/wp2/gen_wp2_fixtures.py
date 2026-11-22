#!/usr/bin/env python3
# WP-2 synthetic raster fixtures: clean 300-dpi A4-landscape drawings with exactly known content.
#   usage: python3 gen_wp2_fixtures.py <outdir>      (writes <name>.png + <name>.truth.json)
# Noisy / skewed variants are produced at test time (OpenCV, fixed seed) from the clean PNGs, see wp2_fixtures.cpp.
#
# truth.json (pixel coordinates of the 300-dpi image, x right, y DOWN; angles in SHEET convention = CCW, y up):
#   lines:  [{"p0":[x,y],"p1":[x,y],"w":px,"type":"visible|thin|hidden|center|border"}]
#   circles:[{"c":[x,y],"r":px,"w":px}]           arcs: [{"c":[x,y],"r":px,"a0":deg,"a1":deg,"w":px}]
#   arrows: [{"tip":[x,y],"base":[x,y]}]
#   texts:  [{"box":[x0,y0,x1,y1],"text":"...","rot":0|90,"dim":true|false,"circle":bool,"angular":bool}]
# Drawn at 2x and downsampled (anti-aliased like a real render). Font: DejaVu Sans (has Ø ± °).
import json, math, os, sys
from PIL import Image, ImageDraw, ImageFont

DPI = 300
MM = DPI / 25.4
SS = 2
W, H = 3508, 2480
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
TXT_PX = 50          # font size (1x); cap height ~3 mm
VIS, THIN = 6, 3     # 0.5 mm / 0.25 mm


class Sheet:
    def __init__(self):
        self.im = Image.new('L', (W * SS, H * SS), 255)
        self.d = ImageDraw.Draw(self.im)
        self.font = ImageFont.truetype(FONT, TXT_PX * SS)
        self.t = {'dpi': DPI, 'size': [W, H], 'lines': [], 'circles': [], 'arcs': [], 'arrows': [], 'texts': []}

    # ---- primitives (1x px coordinates)
    def _seg(self, p, q, w):
        self.d.line([p[0] * SS, p[1] * SS, q[0] * SS, q[1] * SS], fill=0, width=int(w * SS))

    def line(self, p, q, w=VIS, typ='visible', record=True):
        if typ == 'hidden':
            self._pattern(p, q, w, [3.0, 1.2])
        elif typ == 'center':
            self._pattern(p, q, w, [10.0, 1.2, 1.0, 1.2])
        else:
            self._seg(p, q, w)
        if record:
            self.t['lines'].append({'p0': list(p), 'p1': list(q), 'w': w, 'type': typ})

    def _pattern(self, p, q, w, pat_mm):
        """ISO-style: the pattern is scaled so that the line starts and ends with a full first dash."""
        L = math.dist(p, q)
        ux, uy = (q[0] - p[0]) / L, (q[1] - p[1]) / L
        P = sum(pat_mm) * MM
        d0 = pat_mm[0] * MM
        n = max(1, round((L - d0) / P))
        f = L / (n * P + d0)
        segs = [x * MM * f for x in pat_mm] * n + [d0 * f]
        s = 0.0
        for k, seg in enumerate(segs):
            if k % 2 == 0:
                self._seg((p[0] + ux * s, p[1] + uy * s), (p[0] + ux * (s + seg), p[1] + uy * (s + seg)), w)
            s += seg

    def circle(self, c, r, w=VIS):
        R = (r + w / 2) * SS
        self.d.ellipse([c[0] * SS - R, c[1] * SS - R, c[0] * SS + R, c[1] * SS + R], outline=0, width=int(w * SS))
        self.t['circles'].append({'c': list(c), 'r': r, 'w': w})

    def arc(self, c, r, a0, a1, w=VIS):
        """sheet-convention angles (CCW, y up), a0 < a1"""
        R = (r + w / 2) * SS
        # PIL: clockwise in image from start to end == CCW in sheet from -end to -start
        self.d.arc([c[0] * SS - R, c[1] * SS - R, c[0] * SS + R, c[1] * SS + R], start=-a1, end=-a0, fill=0,
                   width=int(w * SS))
        self.t['arcs'].append({'c': list(c), 'r': r, 'a0': a0 % 360, 'a1': a0 % 360 + (a1 - a0), 'w': w})

    def arrow(self, tip, toward, length=3.0 * MM, width=1.0 * MM):
        dx, dy = toward[0] - tip[0], toward[1] - tip[1]
        L = math.hypot(dx, dy)
        ux, uy = dx / L, dy / L
        base = (tip[0] + ux * length, tip[1] + uy * length)
        nx, ny = -uy * width / 2, ux * width / 2
        pts = [tip, (base[0] + nx, base[1] + ny), (base[0] - nx, base[1] - ny)]
        self.d.polygon([(x * SS, y * SS) for x, y in pts], fill=0)
        self.t['arrows'].append({'tip': list(tip), 'base': list(base)})

    def text(self, xy, s, rot=0, dim=True, circle=False, angular=False, anchor='mb'):
        """xy = anchor point (1x px). rot 0: anchor middle-bottom; rot 90: text reads bottom->top, anchor middle-right."""
        f = self.font
        x0, y0, x1, y1 = f.getbbox(s)
        tw, th = x1 - x0, y1 - y0
        pad = 4 * SS
        tile = Image.new('L', (tw + 2 * pad, th + 2 * pad), 255)
        ImageDraw.Draw(tile).text((pad - x0, pad - y0), s, font=f, fill=0)
        if rot == 90:
            tile = tile.rotate(90, expand=True)
        w, h = tile.size
        if rot == 0:
            ox = xy[0] * SS - w / 2 if anchor[0] == 'm' else xy[0] * SS
            oy = xy[1] * SS - h
        else:
            ox = xy[0] * SS - w
            oy = xy[1] * SS - h / 2
        ox, oy = int(round(ox)), int(round(oy))
        self.im.paste(Image.composite(Image.new('L', tile.size, 0), self.im.crop((ox, oy, ox + w, oy + h)),
                                      tile.point(lambda v: 255 - v)), (ox, oy))
        box = [(ox + pad) / SS, (oy + pad) / SS, (ox + w - pad) / SS, (oy + h - pad) / SS]
        self.t['texts'].append({'box': box, 'text': s, 'rot': rot, 'dim': dim, 'circle': circle, 'angular': angular})

    # ---- dimensions
    def hdim(self, xa, xb, yfeat, ydim, s, **kw):
        """horizontal linear dimension between x=xa and x=xb, feature at y=yfeat, dimension line at y=ydim"""
        g, o = 1.0 * MM, 2.0 * MM
        sgn = 1 if ydim > yfeat else -1
        for x in (xa, xb):
            self.line((x, yfeat + sgn * g), (x, ydim + sgn * o), THIN, 'thin')
        self.line((xa, ydim), (xb, ydim), THIN, 'thin')
        self.arrow((xa, ydim), (xb, ydim))
        self.arrow((xb, ydim), (xa, ydim))
        self.text(((xa + xb) / 2, ydim - 1.2 * MM), s, **kw)

    def vdim(self, ya, yb, xfeat, xdim, s, **kw):
        g, o = 1.0 * MM, 2.0 * MM
        sgn = 1 if xdim > xfeat else -1
        for y in (ya, yb):
            self.line((xfeat + sgn * g, y), (xdim + sgn * o, y), THIN, 'thin')
        self.line((xdim, ya), (xdim, yb), THIN, 'thin')
        self.arrow((xdim, ya), (xdim, yb))
        self.arrow((xdim, yb), (xdim, ya))
        self.text((xdim - 1.2 * MM, (ya + yb) / 2), s, rot=90, **kw)

    def diadim(self, c, r, ang_deg, s, ext=14 * MM):  # noqa: E741
        """diameter across a circle along direction ang (image deg, y down), text on a horizontal shoulder"""
        ux, uy = math.cos(math.radians(ang_deg)), math.sin(math.radians(ang_deg))
        p = (c[0] - ux * r, c[1] - uy * r)
        q = (c[0] + ux * r, c[1] + uy * r)
        e = (c[0] + ux * (r + ext), c[1] + uy * (r + ext))
        self.line(p, e, THIN, 'thin')
        self.arrow(p, q)
        self.arrow(q, p)
        sh = (e[0] + (1 if ux >= 0 else -1) * 22 * MM, e[1])
        self.line(e, sh, THIN, 'thin')
        self.text(((e[0] + sh[0]) / 2, e[1] - 1.0 * MM), s, circle=True)

    def border(self):
        m = 10 * MM
        x0, y0, x1, y1 = m, m, W - m, H - m
        for p, q in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
            self.line(p, q, 8, 'border')
        # title block 180 x 40 mm at lower right
        tx0, ty0 = x1 - 180 * MM, y1 - 40 * MM
        self.line((tx0, ty0), (x1, ty0), VIS, 'border')
        self.line((tx0, ty0), (tx0, y1), VIS, 'border')
        self.line((tx0, ty0 + 20 * MM), (x1, ty0 + 20 * MM), THIN, 'thin')
        self.line((tx0 + 90 * MM, ty0), (tx0 + 90 * MM, y1), THIN, 'thin')
        return tx0, ty0

    def save(self, outdir, name, title):
        tx0, ty0 = self.border()
        self.text((tx0 + 5 * MM, ty0 + 14 * MM), title[0], dim=False, anchor='lb')
        self.text((tx0 + 95 * MM, ty0 + 14 * MM), 'ÖLÇEK 1:2', dim=False, anchor='lb')
        self.text((tx0 + 5 * MM, ty0 + 34 * MM), title[1], dim=False, anchor='lb')
        im = self.im.resize((W, H), Image.LANCZOS)
        im.save(os.path.join(outdir, name + '.png'), dpi=(DPI, DPI), optimize=True)
        with open(os.path.join(outdir, name + '.truth.json'), 'w', encoding='utf-8') as f:
            json.dump(self.t, f, ensure_ascii=False, indent=0)


def flange(outdir):
    s = Sheet()
    # front view (section-like rectangle with bore), centre line vertical
    x0, y0, x1, y1 = 500, 700, 1300, 1700
    for p, q in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        s.line(p, q)
    bx0, bx1 = 800, 1000
    s.line((bx0, y0), (bx0, y1), VIS)
    s.line((bx1, y0), (bx1, y1), VIS)
    s.line((900, 620), (900, 1780), THIN, 'center')
    s.line((x0 + 120, y0), (x0 + 120, y1), THIN, 'hidden')
    s.line((x1 - 120, y0), (x1 - 120, y1), THIN, 'hidden')
    s.hdim(x0, x1, y1, y1 + 160, '80')
    s.vdim(y0, y1, x0, x0 - 160, 'Ø100', circle=True)
    s.hdim(bx0, bx1, y0, y0 - 140, 'Ø20 H7', circle=True)
    # side view
    c = (2350, 1200)
    s.circle(c, 500)
    s.circle(c, 100)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        s.circle((c[0] + 350 * math.cos(a), c[1] + 350 * math.sin(a)), 40, THIN + 1)
    s.line((c[0] - 580, c[1]), (c[0] + 580, c[1]), THIN, 'center')
    s.line((c[0], c[1] - 580), (c[0], c[1] + 580), THIN, 'center')
    s.diadim((c[0] + 350 * math.cos(math.radians(-45)), c[1] + 350 * math.sin(math.radians(-45))), 40, -45, '4x Ø6.6')
    s.text((1700, 520), '12±0.1', dim=True)
    s.text((700, 2100), 'Ø40 +0.05/-0', circle=True)
    s.save(outdir, 'wp2_flange', ('FLANŞ', 'MALZEME: Ti-6Al-4V'))


def shaft(outdir):
    s = Sheet()
    yc = 1150
    # stepped shaft half-lengths: (x_from, x_to, radius px)
    steps = [(500, 900, 180), (900, 1700, 260), (1700, 2300, 200), (2300, 2700, 120)]
    s.line((420, yc), (2780, yc), THIN, 'center')
    for i, (xa, xb, r) in enumerate(steps):
        ch = 30 if i in (0, 3) else 0
        top, bot = yc - r, yc + r
        xs = xa + ch if i == 0 else xa
        xe = xb - ch if i == 3 else xb
        s.line((xs, top), (xe, top))
        s.line((xs, bot), (xe, bot))
        if i == 0:
            s.line((xa, top + ch), (xa, bot - ch))
            s.line((xa, top + ch), (xs, top))
            s.line((xa, bot - ch), (xs, bot))
        if i == 3:
            s.line((xb, top + ch), (xb, bot - ch))
            s.line((xe, top), (xb, top + ch))
            s.line((xe, bot), (xb, bot - ch))
        if i > 0:
            rp = steps[i - 1][2]
            s.line((xa, yc - max(r, rp)), (xa, yc - min(r, rp)))
            s.line((xa, yc + min(r, rp)), (xa, yc + max(r, rp)))
    # dims
    s.vdim(yc - 180, yc + 180, 500, 330, 'Ø30 h6', circle=True)
    s.vdim(yc - 120, yc + 120, 2700, 2900, 'M20x1.5')
    s.hdim(500, 900, yc - 260, yc - 380, '34')
    s.hdim(900, 1700, yc - 260, yc - 380, '68±0.2')
    s.hdim(1700, 2300, yc - 200, yc - 380, '50.5')
    s.hdim(500, 2700, yc + 260, yc + 420, '186')
    s.text((620, 900), '2x45°', angular=True)
    s.text((1300, 1700), 'Ø44 +0/-0.5', circle=True)
    s.text((2000, 1700), 'Ø34 g6', circle=True)
    s.text((2000, 1850), 'R1.5 TYP')
    s.save(outdir, 'wp2_shaft', ('MİL', 'MALZEME: 42CrMo4'))


def plate(outdir):
    s = Sheet()
    x0, y0, x1, y1, rf = 500, 600, 2100, 1700, 120
    s.line((x0 + rf, y0), (x1 - rf, y0))
    s.line((x1, y0 + rf), (x1, y1 - rf))
    s.line((x1 - rf, y1), (x0 + rf, y1))
    s.line((x0, y1 - rf), (x0, y0 + rf))
    s.arc((x1 - rf, y0 + rf), rf, 0, 90)
    s.arc((x0 + rf, y0 + rf), rf, 90, 180)
    s.arc((x0 + rf, y1 - rf), rf, 180, 270)
    s.arc((x1 - rf, y1 - rf), rf, 270, 360)
    for (cx, cy) in [(800, 900), (1800, 900), (800, 1400), (1800, 1400)]:
        s.circle((cx, cy), 60, VIS - 1)
        s.line((cx - 100, cy), (cx + 100, cy), THIN, 'center')
        s.line((cx, cy - 100), (cx, cy + 100), THIN, 'center')
    s.circle((1300, 1150), 150)
    s.line((1300 - 220, 1150), (1300 + 220, 1150), THIN, 'center')
    s.line((1300, 1150 - 220), (1300, 1150 + 220), THIN, 'center')
    s.hdim(x0, x1, y1, y1 + 170, '135')
    s.vdim(y0, y1, x0, x0 - 170, '93')
    s.hdim(800, 1800, 900, y0 - 150, '85±0.05')
    s.diadim((800, 1400), 60, 45, '4x Ø10.2')
    s.diadim((1300, 1150), 150, -60, 'Ø25.4')
    s.text((x1 + 60, y0 + 40), 'R10', anchor='lb')
    # side view: thin plate with hidden holes
    sx0, sx1 = 2500, 2700
    for p, q in [((sx0, y0), (sx1, y0)), ((sx1, y0), (sx1, y1)), ((sx1, y1), (sx0, y1)), ((sx0, y1), (sx0, y0))]:
        s.line(p, q)
    for cy in (900, 1400):
        s.line((sx0, cy - 60), (sx1, cy - 60), THIN, 'hidden')
        s.line((sx0, cy + 60), (sx1, cy + 60), THIN, 'hidden')
    s.hdim(sx0, sx1, y0, y0 - 150, '17')
    # angled leg + angle text
    ox, oy = 2800, 1820
    s.line((ox, oy), (ox + 480, oy))
    s.line((ox, oy), (ox + 480 * math.cos(math.radians(30)), oy - 480 * math.sin(math.radians(30))))
    s.arc((ox, oy), 360, 0, 30, THIN)
    s.text((ox + 390, oy - 120), '30\u00b0', angular=True, anchor='lb')
    s.text((700, 2150), 'M8x1.25', dim=True)
    s.save(outdir, 'wp2_plate', ('PLAKA', 'MALZEME: AlMg3'))


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    os.makedirs(out, exist_ok=True)
    flange(out)
    shaft(out)
    plate(out)
    print('ok', out)
