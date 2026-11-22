#!/usr/bin/env python3
"""WP-3 hand-made "perfect S2" DrawingIR fixtures (pcdrw/1 JSON, conventions of DrawingIR.h).

Run:  python3 gen_fixtures.py   (writes *.pcdrw.json next to this script; committed)
Every fixture is an A3 sheet (420x297 mm) with border, title block, views drawn with ISO 128 line conventions:
  visible 0.5 mm continuous, hidden 0.25 dashed [3,1.5], centre 0.25 dash-dot [8,1.5,1,1.5], dimension/extension
  0.25 continuous, filled arrowheads (3 mm), texts h=3.5 (DejaVu-like metrics: width = 0.6*h per glyph).
The top-level key "wp3Expect" (ignored by IR readers) carries the known model: volume (mm^3), bbox size (mm),
strategy and expected parameters.
"""
import json, math, os

H_TXT = 3.5
W_VIS, W_THIN, W_BORDER, W_TB = 0.5, 0.25, 0.7, 0.35
DASH_HID = [3.0, 1.5]
DASH_CEN = [8.0, 1.5, 1.0, 1.5]


class Sheet:
    def __init__(self, name):
        self.name = name
        self.prims, self.texts = [], []
        self.nid = 1

    def _id(self):
        i = self.nid
        self.nid += 1
        return i

    def _prim(self, kind, width, dash=None, rgb=0, filled=False, layer="", **kw):
        p = {"id": self._id(), "kind": kind, "p0": [0, 0], "p1": [0, 0], "center": [0, 0], "radius": 0, "a0": 0,
             "a1": 360, "minorRatio": 1, "rotDeg": 0, "pts": [], "closed": False, "hatchAngleDeg": 0,
             "hatchPitch": 0,
             "style": {"width": width, "dash": dash or [], "dashPhase": 0, "rgb": rgb, "stroked": True,
                       "filled": filled, "layer": layer, "linetype": ""},
             "conf": 0.95, "prov": {"source": "VectorPdf", "page": 0, "ref": "wp3 fixture"}, "mergedFrom": [],
             "fromDimBlock": False, "clipped": False, "attrs": {}}
        for k, v in kw.items():
            p[k] = [round(v[0], 6), round(v[1], 6)] if isinstance(v, tuple) else v
        self.prims.append(p)
        return p["id"]

    def line(self, a, b, w=W_VIS, dash=None):
        return self._prim("Line", w, dash, p0=tuple(a), p1=tuple(b))

    def vis(self, a, b): return self.line(a, b, W_VIS)
    def hid(self, a, b): return self.line(a, b, W_THIN, DASH_HID)
    def cen(self, a, b): return self.line(a, b, W_THIN, DASH_CEN)
    def thin(self, a, b): return self.line(a, b, W_THIN)

    def circle(self, c, r, w=W_VIS, dash=None):
        return self._prim("Circle", w, dash, center=tuple(c), radius=r)

    def arc(self, c, r, a0, a1, w=W_VIS, dash=None):
        while a1 <= a0: a1 += 360
        return self._prim("Arc", w, dash, center=tuple(c), radius=r, a0=a0, a1=a1)

    def arrow(self, tip, toward):
        dx, dy = toward[0] - tip[0], toward[1] - tip[1]
        L = math.hypot(dx, dy)
        base = (tip[0] + 3 * dx / L, tip[1] + 3 * dy / L)
        return self._prim("Arrowhead", W_THIN, None, filled=True, p0=tuple(tip), p1=base)

    def hatch(self, poly, ang=45, pitch=1.5):
        return self._prim("HatchRegion", W_THIN, None, pts=[[round(x, 6), round(y, 6)] for x, y in poly],
                          closed=True, hatchAngleDeg=ang, hatchPitch=pitch)

    def text(self, s, origin, rot=0, h=H_TXT):
        w = 0.6 * h * len(s)
        c, sn = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        o = origin
        q = [o, (o[0] + w * c, o[1] + w * sn), (o[0] + w * c - h * sn, o[1] + w * sn + h * c), (o[0] - h * sn, o[1] + h * c)]
        t = {"id": self._id(), "utf8": s, "origin": [round(o[0], 6), round(o[1], 6)], "height": h, "rotDeg": rot,
             "quad": [[round(x, 6), round(y, 6)] for x, y in q], "font": "DejaVuSans", "rgb": 0, "alternatives": [],
             "asOutlines": False, "invisible": False, "fromDimBlock": False, "conf": 0.95,
             "prov": {"source": "VectorPdf", "page": 0, "ref": "wp3 fixture"}, "mergedFrom": [], "attrs": {}}
        self.texts.append(t)
        return t["id"]

    def ctext(self, s, centre, rot=0, h=H_TXT):
        """text whose box centre is `centre`"""
        w = 0.6 * h * len(s)
        c, sn = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        ox = centre[0] - (w / 2) * c + (h / 2) * sn
        oy = centre[1] - (w / 2) * sn - (h / 2) * c
        return self.text(s, (ox, oy), rot, h)

    # ------------------------------------------------------------------ dimensions
    def lin_dim(self, a, b, horizontal, at, text, tol=None, gap=1.0, over=2.0):
        """Linear dim between geometry points a,b. horizontal: measures x (dim line at y=at) else y (line at x=at)."""
        if horizontal:
            pa, pb = (a[0], at), (b[0], at)
            for g, p in ((a, pa), (b, pb)):
                if abs(g[1] - at) > 1e-9:
                    s = 1 if at > g[1] else -1
                    self.thin((g[0], g[1] + s * gap), (g[0], at + s * over))
            mid = ((pa[0] + pb[0]) / 2, at + 1 + H_TXT / 2)
            rot = 0
        else:
            pa, pb = (at, a[1]), (at, b[1])
            for g, p in ((a, pa), (b, pb)):
                if abs(g[0] - at) > 1e-9:
                    s = 1 if at > g[0] else -1
                    self.thin((g[0] + s * gap, g[1]), (at + s * over, g[1]))
            mid = (at - 1 - H_TXT / 2, (pa[1] + pb[1]) / 2)
            rot = 90
        self.thin(pa, pb)
        self.arrow(pa, pb)
        self.arrow(pb, pa)
        tid = self.ctext(text, mid, rot)
        if tol:   # stacked tolerance texts right of (above, for rot 90: left-up) the main text
            tw = 0.6 * H_TXT * len(text)
            if rot == 0:
                x = mid[0] + tw / 2 + 0.8
                self.text(tol[0], (x, mid[1] + 0.2), 0, 2.5)
                self.text(tol[1], (x, mid[1] - 2.7), 0, 2.5)
            else:
                y = mid[1] + tw / 2 + 0.8
                self.text(tol[0], (mid[0] - 0.2, y), 90, 2.5)
                self.text(tol[1], (mid[0] + 2.7, y), 90, 2.5)
        return tid

    def leader(self, tip, elbow, text, right=True):
        self.thin(tip, elbow)
        self.arrow(tip, elbow)
        w = 0.6 * H_TXT * len(text)
        end = (elbow[0] + (w + 1 if right else -(w + 1)), elbow[1])
        self.thin(elbow, end)
        x0 = elbow[0] + 0.5 if right else end[0] + 0.5
        return self.text(text, (x0, elbow[1] + 0.8))

    # ------------------------------------------------------------------ frame
    def frame(self, scale_text, extra=(), proj_symbol=None, split_scale=False):
        W, H = 420.0, 297.0
        b = [(10, 10), (410, 10), (410, 287), (10, 287)]
        for i in range(4):
            self.line(b[i], b[(i + 1) % 4], W_BORDER)
        # title block 180 x 40 lower right, 4 rows x 10, columns at 230, 290, 350
        x0, y0, x1, y1 = 230.0, 10.0, 410.0, 50.0
        self.line((x0, y0), (x0, y1), W_TB)
        self.line((x0, y1), (x1, y1), W_TB)
        for y in (20, 30, 40):
            self.line((x0, y), (x1, y), W_TB)
        for x in (290, 350):
            self.line((x, y0), (x, y1), W_TB)
        self.text("ADI", (232, 42.5), 0, 2.5)
        self.text(self.name.upper(), (292, 42.5), 0, 3.5)
        self.text("MALZEME", (232, 32.5), 0, 2.5)
        self.text("1.4005", (292, 32.5), 0, 3.5)
        self.text("RESİM NO", (232, 22.5), 0, 2.5)
        self.text("WP3-" + self.name.upper(), (292, 22.5), 0, 3.5)
        if split_scale:
            self.text("ÖLÇEK", (232, 12.5), 0, 2.5)
            self.text(scale_text, (292, 12.5), 0, 3.5)
        else:
            self.text(scale_text, (232, 12.5), 0, 3.5)
        for i, s in enumerate(extra):
            self.text(s, (352, 32.5 - 10 * i), 0, 2.5)
        if proj_symbol:   # ISO first angle: truncated cone (trapezoid) left, circles right; third angle mirrored
            cx, cy = 380.0, 15.0
            tz_x = cx - 8 if proj_symbol == "first" else cx + 8
            cc_x = cx + 8 if proj_symbol == "first" else cx - 8
            a = 1 if proj_symbol == "first" else -1
            # trapezoid: small side towards the circles
            self.line((tz_x - 4 * a, cy - 3.5), (tz_x - 4 * a, cy + 3.5), W_THIN)
            self.line((tz_x + 4 * a, cy - 2), (tz_x + 4 * a, cy + 2), W_THIN)
            self.line((tz_x - 4 * a, cy + 3.5), (tz_x + 4 * a, cy + 2), W_THIN)
            self.line((tz_x - 4 * a, cy - 3.5), (tz_x + 4 * a, cy - 2), W_THIN)
            self.circle((cc_x, cy), 3.5, W_THIN)
            self.circle((cc_x, cy), 2.0, W_THIN)

    def dump(self, path, expect):
        sheet = {"source": "VectorPdf", "file": "wp3/" + self.name, "fileSha256": "", "page": 0, "layout": "",
                 "sheet": {"lo": [0, 0], "hi": [420, 297]}, "unitToMm": 1.0, "prims": self.prims,
                 "texts": self.texts, "nativeDims": [], "raster": None, "diagnostics": [], "attrs": {}}
        ir = {"schema": "pcdrw/1", "irVersion": "1.0", "nextId": self.nid, "raw": sheet, "norm": sheet,
              "lastStage": 2, "attrs": {"wp3.fixture": self.name}, "edits": [], "wp3Expect": expect}
        with open(path, "w", encoding="utf-8") as f:
            json.dump(ir, f, ensure_ascii=False, indent=1)
            f.write("\n")


def pappus(poly):
    """Volume of revolution of a closed polygon (r, z) about the z axis = 2*pi*|sum A_i r_i|."""
    A = 0.0
    M = 0.0
    n = len(poly)
    for i in range(n):
        r0, z0 = poly[i]
        r1, z1 = poly[(i + 1) % n]
        c = r0 * z1 - r1 * z0
        A += c
        M += c * (r0 + r1)
    return abs(2 * math.pi * M / 6.0)


def arcpts(c, r, p0, p1, n=400, ccw=True):
    a0 = math.atan2(p0[1] - c[1], p0[0] - c[0])
    a1 = math.atan2(p1[1] - c[1], p1[0] - c[0])
    if ccw:
        while a1 <= a0: a1 += 2 * math.pi
    else:
        while a1 >= a0: a1 -= 2 * math.pi
    return [(c[0] + r * math.cos(a0 + (a1 - a0) * k / n), c[1] + r * math.sin(a0 + (a1 - a0) * k / n)) for k in range(n + 1)]


HERE = os.path.dirname(os.path.abspath(__file__))


# =========================================================================================== 1 plate with 4 holes
def plate():
    s = Sheet("plate4")
    k = 0.5                                   # 1:2
    W, Hh, T, D = 100.0, 60.0, 10.0, 10.0
    holes = [(15, 15), (85, 15), (15, 45), (85, 45)]
    ox, oy = 80.0, 80.0                       # plan (primary) lower-left
    P = lambda x, y: (ox + x * k, oy + y * k)
    c = [P(0, 0), P(W, 0), P(W, Hh), P(0, Hh)]
    for i in range(4): s.vis(c[i], c[(i + 1) % 4])
    for hx, hy in holes:
        cc = P(hx, hy)
        s.circle(cc, D / 2 * k)
        e = D / 2 * k + 1.5
        s.cen((cc[0] - e, cc[1]), (cc[0] + e, cc[1]))
        s.cen((cc[0], cc[1] - e), (cc[0], cc[1] + e))
    # elevation above the plan (first angle: view from below placed above) — x shared
    ey = oy + Hh * k + 25
    E = lambda x, z: (ox + x * k, ey + z * k)
    c2 = [E(0, 0), E(W, 0), E(W, T), E(0, T)]
    for i in range(4): s.vis(c2[i], c2[(i + 1) % 4])
    for hx in (15, 85):
        for dx in (-D / 2, D / 2): s.hid(E(hx + dx, 0), E(hx + dx, T))
        s.cen(E(hx, -3), E(hx, T + 3))
    # dims (values in part mm)
    s.lin_dim(P(0, 0), P(15, 15), True, oy - 6, "15")
    s.lin_dim(P(15, 15), P(85, 15), True, oy - 6, "70")
    s.lin_dim(P(0, 0), P(W, 0), True, oy - 14, "100")
    s.lin_dim(P(0, 0), P(15, 15), False, ox - 6, "15")
    s.lin_dim(P(15, 15), P(15, 45), False, ox - 6, "30")
    s.lin_dim(P(W, 0), P(W, Hh), False, ox + W * k + 8, "60")
    s.lin_dim(E(W, 0), E(W, T), False, ox + W * k + 8, "10")
    hc = P(85, 45)
    a = math.radians(45)
    tip = (hc[0] + D / 2 * k * math.cos(a), hc[1] + D / 2 * k * math.sin(a))
    s.leader(tip, (tip[0] + 8, tip[1] + 8), "4x Ø10")
    s.frame("1:2", ["ISO 2768-m"], proj_symbol="first", split_scale=True)
    s.text("GENEL TOLERANSLAR ISO 2768-m", (20, 30), 0, 3.5)
    vol = W * Hh * T - 4 * math.pi * (D / 2) ** 2 * T
    s.dump(os.path.join(HERE, "plate4.pcdrw.json"),
           {"strategy": "ExtrudeTwoView", "volume": vol, "bbox": [W, Hh, T], "scale": [1, 2], "projection": "FirstAngle",
            "params": {"W": 100, "H": 60, "T": 10, "Dh1": 10}, "views": 2, "dims": 8})


# =========================================================================================== 2 L-bracket two-view
def lbracket():
    s = Sheet("lbracket")
    L, Ht, tb, tl, Dp = 80.0, 50.0, 10.0, 10.0, 40.0
    ox, oy = 60.0, 90.0
    F = lambda x, y: (ox + x, oy + y)
    poly = [(0, 0), (L, 0), (L, tb), (tl, tb), (tl, Ht), (0, Ht)]
    for i in range(len(poly)): s.vis(F(*poly[i]), F(*poly[(i + 1) % len(poly)]))
    sx = ox + L + 30                          # left view placed right of the front (first angle)
    S = lambda z, y: (sx + z, oy + y)
    r = [(0, 0), (Dp, 0), (Dp, Ht), (0, Ht)]
    for i in range(4): s.vis(S(*r[i]), S(*r[(i + 1) % 4]))
    s.hid(S(0, tb), S(Dp, tb))                # step edge seen from the left: hidden behind the leg
    s.lin_dim(F(0, 0), F(L, 0), True, oy - 12, "80")
    s.lin_dim(F(0, 0), F(0, Ht), False, ox - 12, "50")
    s.lin_dim(F(L, 0), F(L, tb), False, ox + L + 8, "10")
    s.lin_dim(F(0, Ht), F(tl, Ht), True, oy + Ht + 8, "10")
    s.lin_dim(S(0, 0), S(Dp, 0), True, oy - 12, "40")
    s.frame("ÖLÇEK 1:1")
    vol = (L * tb + tl * (Ht - tb)) * Dp
    s.dump(os.path.join(HERE, "lbracket.pcdrw.json"),
           {"strategy": "ExtrudeTwoView", "volume": vol, "bbox": [L, Ht, Dp], "scale": [1, 1], "projection": "FirstAngle",
            "params": {"W": 80, "H": 50, "T": 40}, "views": 2, "dims": 5})


# =========================================================================================== 3 flange with bolt circle
def flange():
    s = Sheet("flange")
    R, Rh, Rb, tf, Lt, pcd, dh, n = 50.0, 20.0, 10.0, 12.0, 30.0, 75.0, 9.0, 6
    ax, ay = 200.0, 160.0                     # front elevation: axis horizontal, z to the right
    F = lambda z, r: (ax + z, ay + r)
    for sg in (1, -1):
        s.vis(F(0, sg * R), F(tf, sg * R))
        s.vis(F(tf, sg * Rh), F(Lt, sg * Rh))
        s.hid(F(0, sg * Rb), F(Lt, sg * Rb))
    s.vis(F(0, -R), F(0, R))
    s.vis(F(tf, -R), F(tf, R))
    s.vis(F(Lt, -Rh), F(Lt, Rh))
    for th in range(0, 360, 60):              # bolt holes (X = r cos th) as hidden lines in the flange
        X = pcd / 2 * math.cos(math.radians(th))
        if th in (90, 270): continue
        if th > 90 and th < 270 and False: continue
        for dx in (-dh / 2, dh / 2): s.hid(F(0, X + dx), F(tf, X + dx))
    # remove duplicate hidden lines (th and 360-th coincide)
    seen, keep = set(), []
    for p in s.prims:
        key = (p["kind"], tuple(p["p0"]), tuple(p["p1"]), p["radius"])
        if key in seen: continue
        seen.add(key); keep.append(p)
    s.prims = keep
    s.cen(F(-5, 0), F(Lt + 5, 0))
    # end view (from the right, placed LEFT of the front: first angle)
    ex = ax - 90.0
    C = (ex, ay)
    for rr in (R, Rh, Rb): s.circle(C, rr)
    s.circle(C, pcd / 2, W_THIN, DASH_CEN)
    s.cen((ex - R - 5, ay), (ex + R + 5, ay))
    s.cen((ex, ay - R - 5), (ex, ay + R + 5))
    for th in range(0, 360, 60):
        hx, hy = ex + pcd / 2 * math.sin(math.radians(th)), ay + pcd / 2 * math.cos(math.radians(th))
        s.circle((hx, hy), dh / 2)
    # dims
    s.lin_dim(F(0, R), F(0, -R), False, ax - 10, "Ø100")
    s.lin_dim(F(Lt, Rh), F(Lt, -Rh), False, ax + Lt + 10, "Ø40")
    s.lin_dim(F(0, -R), F(tf, -R), True, ay - R - 8, "12")
    s.lin_dim(F(0, -R), F(Lt, -Rh), True, ay - R - 16, "30")
    th = math.radians(30)
    tip = (ex + Rb * math.cos(th), ay + Rb * math.sin(th))
    s.leader(tip, (ex + 30, ay + 6), "Ø20")
    hx, hy = ex + pcd / 2 * math.sin(math.radians(60)), ay + pcd / 2 * math.cos(math.radians(60))
    tip = (hx + dh / 2 * math.cos(math.radians(45)), hy + dh / 2 * math.sin(math.radians(45)))
    s.leader(tip, (ex + 50, ay + 40), "6x Ø9")
    tip = (ex - pcd / 2 * math.cos(math.radians(30)), ay - pcd / 2 * math.sin(math.radians(30)))
    s.leader(tip, (ex - 50, ay - 45), "Ø75", right=False)
    s.frame("ÖLÇEK 1:1", ["ISO-E"])
    vol = math.pi * (R * R - Rb * Rb) * tf + math.pi * (Rh * Rh - Rb * Rb) * (Lt - tf) - n * math.pi * (dh / 2) ** 2 * tf
    s.dump(os.path.join(HERE, "flange.pcdrw.json"),
           {"strategy": "Revolve", "volume": vol, "bbox": [2 * R, 2 * R, Lt], "scale": [1, 1], "projection": "FirstAngle",
            "params": {"D1": 100, "Dh1": 9, "n_h1": 6}, "views": 2, "dims": 7, "holes": n})


# =========================================================================================== 4 stepped shaft (2:1)
def shaft():
    s = Sheet("shaft")
    k = 2.0
    steps = [(0, 15, 20.0), (15, 55, 30.0), (55, 80, 25.0)]
    ax, ay = 90.0, 170.0
    F = lambda z, r: (ax + z * k, ay + r * k)
    for z0, z1, d in steps:
        for sg in (1, -1): s.vis(F(z0, sg * d / 2), F(z1, sg * d / 2))
    for z, r in ((0, 10), (15, 15), (55, 15), (80, 12.5)): s.vis(F(z, -r), F(z, r))
    s.cen(F(-3, 0), F(83, 0))
    for (z0, z1, d) in steps:   # inside diameter dims between the silhouette lines
        zc = (z0 + z1) / 2 + (3 if d == 30 else 0)
        s.lin_dim(F(zc, -d / 2), F(zc, d / 2), False, F(zc, 0)[0], "Ø%g" % d)
    s.lin_dim(F(0, -10), F(15, -15), True, ay - 15 * k - 10, "15")
    s.lin_dim(F(15, -15), F(55, -15), True, ay - 15 * k - 10, "40")
    s.lin_dim(F(55, -15), F(80, -12.5), True, ay - 15 * k - 10, "25")
    s.lin_dim(F(0, -10), F(80, -12.5), True, ay - 15 * k - 20, "80")
    s.frame("SCALE 2:1")
    vol = sum(math.pi * (d / 2) ** 2 * (z1 - z0) for z0, z1, d in steps)
    s.dump(os.path.join(HERE, "shaft.pcdrw.json"),
           {"strategy": "Revolve", "volume": vol, "bbox": [30, 30, 80], "scale": [2, 1], "projection": "FirstAngle",
            "params": {"D1": 20, "D2": 30, "D3": 25}, "views": 1, "dims": 7, "redundant": 1})


# =========================================================================================== 5 injector body, section A-A (3:1)
def injector():
    s = Sheet("enjektor_govde")
    k = 3.0
    H = [(0.18, 13.24), (0.18, 13.52), (0.27, 13.8), (1.91, 13.8), (1.99, 13.87), (1.99, 13.96), (2.05, 14.01),
         (3.17, 14.01), (7.79, 7.01), (9.14, 5.87), (9.14, 1.59), (7.85, 0.51), (7.85, 0.0), (6.15, 0.0),
         (6.15, -1.5), (7.08, -2.43), (7.08, -6.49), (6.08, -7.49), (5.19, -7.49), (4.42, -6.7), (4.42, 9.41),
         (4.29, 9.48), (4.29, 9.93), (0.76, 12.4)]
    arcs = {1: ((0.66, 13.52), 0.48), 7: ((-0.19, 6.77), 7.98)}   # edge index i: H[i] -> H[i+1]
    ax, ay = 200.0, 120.0
    poly_full = []
    for side in (1, -1):
        F = lambda r, z: (ax + side * r * k, ay + z * k)
        hp, hh = [], []
        for i in range(len(H)):
            a, b = H[i], H[(i + 1) % len(H)]
            if i in arcs:
                c, rr = arcs[i]
                # orientation in (r,z): H2->H3 bulges away from the axis? decide by cross product of chord vs centre
                cr = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
                ccw = cr > 0
                pts = arcpts(c, rr, a, b, 400, ccw)
                hp += pts[:-1]
                hh += arcpts(c, rr, a, b, 24, ccw)[:-1]
                cs = F(*c)
                sa = math.degrees(math.atan2(F(*a)[1] - cs[1], F(*a)[0] - cs[0]))
                sb = math.degrees(math.atan2(F(*b)[1] - cs[1], F(*b)[0] - cs[0]))
                ccw_sheet = ccw if side == 1 else not ccw
                if ccw_sheet: s.arc(cs, rr * k, sa, sb)
                else: s.arc(cs, rr * k, sb, sa)
            else:
                hp.append(a)
                hh.append(a)
                s.vis(F(*a), F(*b))
        if side == 1: poly_full = hp
        s.hatch([F(*p) for p in hh], 45 if side == 1 else 135)
    # back edges seen through the bore (section view): circles at inner vertices project as lines across the axis
    for (r, z) in [H[3], H[5], H[19], H[20], H[22]]:
        s.vis((ax - r * k, ay + z * k), (ax + r * k, ay + z * k))
    s.cen((ax, ay - 9 * k), (ax, ay + 16 * k))
    s.ctext("A-A", (ax, ay + 18 * k + 4), 0, 5)
    # dims
    Fp = lambda r, z: (ax + r * k, ay + z * k)
    s.lin_dim(Fp(7.85, 0.0), Fp(3.17, 14.01), False, ax + 13 * k, "14±0.2")
    s.lin_dim(Fp(7.85, 0.0), Fp(6.08, -7.49), False, ax + 13 * k, "7.5±0.5")
    s.lin_dim(Fp(-7.85, 0.0), Fp(7.85, 0.0), True, ay - 12 * k, "Ø16", tol=("+0", "-0.5"))
    a = math.radians(40)
    c = Fp(-0.19, 6.77)
    tip = (c[0] + 7.98 * k * math.cos(a), c[1] + 7.98 * k * math.sin(a))
    s.leader(tip, (tip[0] + 12, tip[1] + 10), "R8 +0/-0.2")
    s.frame("ÖLÇEK 3:1")
    vol = pappus(poly_full)
    s.dump(os.path.join(HERE, "injector_body.pcdrw.json"),
           {"strategy": "SectionRevolve", "volume": vol, "bbox": [2 * 9.14, 2 * 9.14, 14.01 + 7.49], "scale": [3, 1],
            "projection": "FirstAngle", "params": {"D1": 16, "R1": 8}, "views": 1, "dims": 4, "volTolRel": 0.03})


if __name__ == "__main__":
    plate(); lbracket(); flange(); shaft(); injector()
    print("ok")
