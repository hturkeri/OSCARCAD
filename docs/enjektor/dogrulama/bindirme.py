#!/usr/bin/env python3
"""Bagimsiz dogrulama: model XZ kesitini (pc_kesit ciktisi) kaynak cizimin uzerine bindirir.

  pdftocairo -svg -f 1 -l 1 ef502211-09-DB01-4A-E.pdf p1.svg
  pdftoppm -r 600 -f 1 -l 1 -png ef502211-09-DB01-4A-E.pdf p600      -> p600-1.png
  pc_kesit samples/enjektor_09DB01.pcad xz.json --edges
  python3 bindirme.py p1.svg p600-1.png xz.json <cikti_klasoru>

Kalibrasyon (sayfa 1, 600 dpi piksel): olcu uzatma cizgileri y=3249 (z=14), y=4242 (z=0), y=4773 (z=-7.5),
y=5694 (z=-20.5) -> 70.87 px/mm (3:1 = 70.866); eksen x=1884.
Cizim vektor cizgileri SIYAH, model kesiti govde basina renkli; sapma = model noktasinin en yakin cizim
cizgisine uzakligi."""
import json, math, re, sys
from PIL import Image, ImageDraw, ImageFont

S, AX, Y0 = 70.8661, 1884.0, 4242.0
COL = {"Govde": (230, 0, 0), "Swirler": (0, 90, 255), "TutmaVidasi": (0, 160, 0), "Filtre": (200, 0, 200),
       "Elek": (0, 170, 170), "AkisHacmi": (255, 140, 0)}

def parse_svg(svg):
    txt = open(svg, encoding="utf-8").read()
    segs = []
    H = 842 / 0.12
    for m in re.finditer(r"<path ([^>]*?)/>", txt, re.S):
        a = m.group(1)
        tr = re.search(r'transform="matrix\(([^)]*)\)"', a)
        if not tr or abs(float(tr.group(1).split(",")[0]) - 0.12) > 1e-6: continue
        d = re.search(r'\bd="([^"]*)"', a).group(1)
        w = re.search(r'\bstroke-width="([^"]*)"', a)
        w = float(w.group(1)) if w else 0
        toks = re.findall(r"[MLCZ]|-?\d+\.?\d*(?:e-?\d+)?", d)
        i = 0; cur = start = None; cmd = None
        while i < len(toks):
            if toks[i] in "MLCZ": cmd = toks[i]; i += 1
            if cmd == "Z":
                if cur and start and cur != start: segs.append((*cur, *start, w))
                cur = start; continue
            if cmd == "M":
                cur = start = (float(toks[i]), H - float(toks[i + 1])); i += 2; cmd = "L"
            elif cmd == "L":
                p = (float(toks[i]), H - float(toks[i + 1])); i += 2; segs.append((*cur, *p, w)); cur = p
            elif cmd == "C":
                c = [float(v) for v in toks[i:i + 6]]; i += 6
                p1, p2, p3 = (c[0], H - c[1]), (c[2], H - c[3]), (c[4], H - c[5]); prev = cur
                for k in range(1, 13):
                    t = k / 12; u = 1 - t
                    q = (u**3 * cur[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                         u**3 * cur[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1])
                    segs.append((*prev, *q, w)); prev = q
                cur = p3
    return segs

def to_rz(x, y): return ((x - AX) / S, (Y0 - y) / S)

def dist_seg(p, s):
    ax, ay, bx, by = s[:4]; dx, dy = bx - ax, by - ay; L2 = dx * dx + dy * dy
    t = 0 if L2 == 0 else max(0, min(1, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L2))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)

def main(svg, png, secjson, outdir):
    segs = parse_svg(svg)
    edge = [s for s in segs if abs(s[4] - 8.372) < 0.01 or abs(s[4] - 5.98) < 0.01]   # kenar + ince cizgiler
    # drawing edges in mm (r,z)
    dmm = [(*to_rz(s[0], s[1]), *to_rz(s[2], s[3])) for s in edge]
    D = json.load(open(secjson))
    page = Image.open(png).convert("RGB")
    try: font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 22)
    except Exception: font = ImageFont.load_default()

    def view(name, r0, r1, z0, z1, sc, bodies=None, proj=None, title=""):
        box = (int(AX + r0 * S), int(Y0 - z1 * S), int(AX + r1 * S), int(Y0 - z0 * S))
        im = page.crop(box); W, Hh = int(im.width * sc), int(im.height * sc)
        im = im.resize((W, Hh), Image.LANCZOS); im = Image.blend(im, Image.new("RGB", (W, Hh), "white"), 0.72)
        d = ImageDraw.Draw(im)
        P = lambda r, z: ((AX + r * S - box[0]) * sc, (Y0 - z * S - box[1]) * sc)
        for s in segs:   # drawing vectors, black
            if abs(s[4] - 8.372) < 0.01 or abs(s[4] - 5.98) < 0.01:
                d.line(((s[0] - box[0]) * sc, (s[1] - box[1]) * sc, (s[2] - box[0]) * sc, (s[3] - box[1]) * sc), fill=(0, 0, 0), width=3)
        for b in D["bodies"]:
            if b["name"] == "AkisHacmi" or (bodies and b["name"] not in bodies): continue
            for pl in b["polylines"]:
                q = [P(u, v) for u, v in pl if u >= -1e-6]
                if len(q) > 1: d.line(q, fill=COL[b["name"]], width=2)
            if proj and b["name"] in proj:   # outside (projected) view: edges on the viewer side (y <= 0)
                for pl in b.get("edges3d", []):
                    q = [P(x, z) for x, y, z in pl if y <= 0.02]
                    if len(q) > 1: d.line(q, fill=(255, 120, 0), width=2)
        for zi in range(math.ceil(z0), math.floor(z1) + 1):
            x, y = P(r0, zi); d.line((0, y, 14, y), fill=(0, 0, 0)); d.text((18, y - 12), f"z={zi}", fill=(0, 0, 0), font=font)
        for ri in range(math.ceil(r0), math.floor(r1) + 1):
            x, y = P(ri, z0); d.line((x, Hh - 14, x, Hh), fill=(0, 0, 0)); d.text((x + 3, Hh - 40), f"r={ri}", fill=(0, 0, 0), font=font)
        if title: d.text((60, 8), title, fill=(0, 0, 0), font=font)
        im.save(f"{outdir}/{name}.png")

    T = "siyah: cizim (vektor)   kirmizi: Govde   mavi: Swirler   yesil: TutmaVidasi   mor: Filtre   camgobegi: Elek   turuncu: slot izdusumu"
    view("01_tum_parca", -0.4, 9.6, -19.2, 14.5, 1.0, title="")
    view("02_bas_orifis_girdap_odasi", -0.1, 2.4, 12.2, 14.15, 10.0, bodies=("Govde", "Swirler"), proj=("Swirler",))
    view("03_swirler_koni_slot", -0.2, 4.7, 7.7, 13.0, 5.0, bodies=("Govde", "Swirler", "TutmaVidasi"), proj=("Swirler",))
    view("04_tutucu_vida_capraz_delik_halka", -0.2, 5.6, -4.0, 8.6, 2.6, bodies=("Govde", "Swirler", "TutmaVidasi", "Filtre"), proj=("TutmaVidasi",))
    view("05_filtre_pencere_elek", -0.2, 6.4, -19.0, -3.0, 2.4, bodies=("Govde", "TutmaVidasi", "Filtre", "Elek"))

    # ---- deviation statistics: every model section point (x >= 0) vs nearest drawing edge segment
    print("| Govde | nokta | ort. [mm] | %95 [mm] | en buyuk [mm] | en buyuk sapmanin yeri (r, z) |")
    print("|---|---:|---:|---:|---:|---|")
    worst = {}
    for b in D["bodies"]:
        if b["name"] == "AkisHacmi": continue
        ds = []
        for pl in b["polylines"]:
            # resample each polyline at <= 0.05 mm
            for (u0, v0), (u1, v1) in zip(pl, pl[1:]):
                n = max(1, int(math.hypot(u1 - u0, v1 - v0) / 0.05))
                for k in range(n + 1):
                    u, v = u0 + (u1 - u0) * k / n, v0 + (v1 - v0) * k / n
                    if u < 0.01: continue    # axis / left half (outside view in the drawing)
                    dd = min(dist_seg((u, v), s) for s in dmm)
                    ds.append((dd, u, v))
        ds.sort()
        if not ds: continue
        w = ds[-1]; worst[b["name"]] = ds
        print(f"| {b['name']} | {len(ds)} | {sum(x[0] for x in ds)/len(ds):.3f} | {ds[int(0.95*len(ds))][0]:.3f} | {w[0]:.3f} | ({w[1]:.2f}, {w[2]:.2f}) |")

    # ---- reverse check: every drawn edge point (thick lines, right half) vs nearest model line
    #      (section polylines + ALL projected body edges). Finds drawn geometry that the model does not have.
    ms = []
    for b in D["bodies"]:
        if b["name"] == "AkisHacmi": continue
        for pl in b["polylines"]:
            ms += [(a[0], a[1], c[0], c[1]) for a, c in zip(pl, pl[1:])]
        for pl in b.get("edges3d", []):
            ms += [(a[0], a[2], c[0], c[2]) for a, c in zip(pl, pl[1:])]
    grid = {}
    for m in ms:
        x0, x1 = sorted((m[0], m[2])); y0, y1 = sorted((m[1], m[3]))
        for i in range(int(math.floor(x0 / 0.5)) - 1, int(math.floor(x1 / 0.5)) + 2):
            for j in range(int(math.floor(y0 / 0.5)) - 1, int(math.floor(y1 / 0.5)) + 2):
                grid.setdefault((i, j), []).append(m)
    n = 0; bad = []
    for s in segs:
        if abs(s[4] - 8.372) > 0.01: continue
        a = to_rz(s[0], s[1]); c = to_rz(s[2], s[3])
        L = math.hypot(c[0] - a[0], c[1] - a[1]); k = max(1, int(L / 0.05))
        for i in range(k + 1):
            q = (a[0] + (c[0] - a[0]) * i / k, a[1] + (c[1] - a[1]) * i / k)
            if not (0.03 < q[0] < 9.3 and -18.8 < q[1] < 14.1): continue
            n += 1
            dd = min((dist_seg(q, m) for m in grid.get((int(math.floor(q[0] / 0.5)), int(math.floor(q[1] / 0.5))), [])), default=9)
            if dd > 0.04: bad.append((dd, q[0], q[1]))
    print(f"\nters kontrol (cizim -> model): {n} cizim noktasi, {len(bad)} tanesi modelden > 0.04 mm uzakta (%{100*len(bad)/max(n,1):.1f})")
    seen = []
    for dd, u, v in sorted(bad, reverse=True):
        if all(abs(u - a) + abs(v - b) > 0.6 for a, b, _ in seen): seen.append((u, v, dd))
    for u, v, dd in seen: print(f"  r={u:.2f} z={v:.2f}  {dd:.3f} mm")

    # ---- slot: drawn slot lines (thin lines in the slot window) vs projected visible swirler edges (y <= 0)
    sw = [b for b in D["bodies"] if b["name"] == "Swirler"]
    if sw and sw[0].get("edges3d"):
        vis = [(a[0], a[2], c[0], c[2]) for pl in sw[0]["edges3d"] for a, c in zip(pl, pl[1:]) if a[1] <= 0.02 and c[1] <= 0.02]
        ds = []
        for s in segs:
            if abs(s[4] - 5.98) > 0.01: continue
            for q in (to_rz(s[0], s[1]), to_rz(s[2], s[3])):
                if 0.15 < q[0] < 1.15 and 10.5 < q[1] < 12.7: ds.append(min(dist_seg(q, m) for m in vis))
        ds.sort()
        if ds: print(f"\nslot izdusumu: {len(ds)} cizim noktasi, ort. {sum(ds)/len(ds):.3f}, %95 {ds[int(.95*len(ds))]:.3f}, en buyuk {ds[-1]:.3f} mm")

if __name__ == "__main__": main(*sys.argv[1:5])
