#!/usr/bin/env python3
"""Generates the PersistCAD models of the Fluidics simplex atomizer 09-DB01-4A-E (EN 293/299).

Source of truth: docs/enjektor/SPEC.md + spec.json (geometry measured from the vector drawing).
Tags used in parameter descriptions:  [Ö] measured from the drawing, [S] standard / nominal,
[V] ASSUMPTION (not visible in the drawing; kept as a clearly labelled parameter), [T] derived.

Every sketch is drawn first and is fully constrained (DOF 0): each vertex is pinned by ordinate
dimensions (DistanceX / DistanceY from the sketch origin = the nozzle axis) whose values are
expressions over the parameters; arcs get their radius from the pinned centre + start point.

Usage:  gen_enjektor.py [outdir]      (default: the samples/ directory next to this script)
  writes  enjektor_09DB01.pcad        hardware: 5 solid bodies (no fluid volume -> fast, clean sections)
          enjektor_09DB01_kesit.pcad  solids only, quarter cut like the datasheet (swirler uncut)
          enjektor_09DB01_akis.pcad   fluid volume only (solids consumed), for CFD export / viewing
"""
import json, math, os, sys

# ============================================================================ parameters
def make_params():
    P = []
    def par(name, expr, kind="length", mn=None, mx=None, desc="", exposed=False, label="", step=0, group=""):
        P.append(dict(name=name, expr=str(expr), kind=kind, min=mn, max=mx, description=desc,
                      exposed=exposed, label=label, step=step, group=group))

    # ---- Akış (design inputs) ---------------------------------------------------------------
    par("Q", "1.90", "scalar", 1.9, 7.5, "[S] Nominal debi @ test basıncı [kg/h] (EN 293: 0.50 USgal/h = 1.90 kg/h)",
        True, "Debi Q [kg/h]", 0.05, "Akış")
    par("dP", "10", "scalar", 7, 20, "[S] Püskürtme basınç farkı [bar] (EN 293 test: 10 bar)", True, "Basınç ΔP [bar]", 0.5, "Akış")
    par("Cd", "0.1283", "scalar", 0.08, 0.45,
        "[T] Etkin boşaltma katsayısı: çizilen d0=0.36 ve 1.90 kg/h @10 bar ile kalibre (SPEC §6)", True,
        "Boşaltma katsayısı Cd", 0.005, "Akış")
    par("d0_mod", "0", "count", 0, 1, "Orifis modu: 0 = Ø debiden hesaplanır (Q, ΔP, Cd); 1 = elle verilen d0_el kullanılır",
        True, "Orifis modu (0: debiden, 1: elle Ø)", 1, "Akış")
    par("d0_el", "0.36", "length", 0.2, 1.0, "[Ö] Orifis çapı, elle (çizim: Ø0.36) — yalnız d0_mod=1 iken etkin", True,
        "Orifis Ø (elle) [mm]", 0.01, "Akış")
    par("Lo", "0.28", "length", 0.15, 1.0, "[Ö] Orifis silindirik boyu (çizim 0.28, L/d=0.78)", True, "Orifis boyu Lo [mm]", 0.01, "Akış")
    par("Ds", "1.52", "length", 1.2, 4.0, "[Ö] Girdap odası giriş çapı (H24, Ø1.52)", True, "Girdap odası Ø Ds [mm]", 0.02, "Akış")
    par("alfa_s", "69.2", "angle", 60, 120, "[Ö] Girdap odası koni tam açısı (çizim 69.2°)", True, "Girdap odası koni açısı [°]", 0.5, "Akış")
    par("rho", "817", "scalar", 700, 1000, "[S] Test yağı yoğunluğu [kg/m³] (ISO 4113, 37.8 °C)")
    par("nu_cSt", "3.5", "scalar", 1, 10, "[S] Test yağı kinematik viskozitesi [cSt]")
    par("mu", "rho*nu_cSt/1000000", "scalar", None, None, "[T] Dinamik viskozite [Pa·s]")
    par("d0_hes", "1000*sqrt(4*(Q/3600)/(Cd*pi*sqrt(2*rho*dP*100000)))", "length", None, None,
        "[T] Orifis çapı debiden: ṁ = Cd·(π d0²/4)·√(2ρΔP)")
    par("d0", "(1-d0_mod)*d0_hes + d0_mod*d0_el", "length", None, None, "[T] Kullanılan orifis çapı [mm]", label="Orifis çapı d0", group="Hesaplanan")

    # ---- Swirler (design variables) ---------------------------------------------------------
    par("n_slot", "2", "count", 1, 4, "[V] VARSAYIM: teğetsel slot adedi (çizimde yalnız 1 slot görünür; 1–4 hepsi tutarlı)",
        True, "Slot adedi (VARSAYIM)", 1, "Swirler")
    par("w_slot", "0.16", "length", 0.1, 0.5, "[Ö] Slot genişliği (0.16 ± 0.03)", True, "Slot genişliği w [mm]", 0.01, "Swirler")
    par("h_slot", "0.19", "length", 0.1, 0.3, "[Ö] Slot derinliği, 110° koni yüzeyine dik (0.19 ± 0.04)", True,
        "Slot derinliği h [mm]", 0.01, "Swirler")
    par("e_slot", "0.20", "length", 0.05, 0.5, "[Ö] Slot ekseninin nozul ekseninden kaçıklığı (0.20 ± 0.05); e ≤ Ds/2 − w/2",
        True, "Slot eksen kaçıklığı e [mm]", 0.01, "Swirler")
    par("koni_on", "110", "angle", 100, 120, "[Ö] Yuva / swirler uç konisi tam açısı (110°)", True, "Uç (yuva) konisi açısı [°]", 1, "Swirler")
    par("koni_arka", "60", "angle", 50, 70, "[Ö] Swirler arka konisi tam açısı (60°)", True, "Arka koni açısı [°]", 1, "Swirler")
    par("d_uc", "0.80", "length", 0.5, 1.1, "[Ö] Swirler uç düzlüğü çapı (Ø0.80)", True, "Uç düzlüğü Ø [mm]", 0.05, "Swirler")
    # design relations (derived, range-checked -> PARAM.RANGE with a suggested input value)
    par("slot_uc_payi", "d_uc/2 - h_slot/cos(koni_on/2)", "length", 0, None,
        "[T] Tasarım bağıntısı: slot tabanı uç düzlüğünde eksene ulaşmamalı (karşılıklı slotlar uçta birleşir, girdap kolu kalmaz)",
        False, "Slot tabanı–eksen payı (uçta)")
    par("slot_ic_payi", "e_slot - w_slot/2", "length", 0, None,
        "[T] Tasarım bağıntısı: slotun iç duvarı eksen düzlemini geçmemeli (e ≥ w/2), aksi halde akışın bir kısmı ters yönde döner",
        False, "Slot iç duvarı–eksen düzlemi payı")
    par("slot_dis_payi", "Ds/2 - e_slot - w_slot/2", "length", 0, None,
        "[T] Tasarım bağıntısı: slotun dış duvarı girdap odası yarıçapının içinde kalmalı (e + w/2 ≤ Ds/2)",
        False, "Slot dış duvarı–girdap odası payı")
    par("slot_az", "-75", "angle", -180, 180, "[Ö] 1. slotun açısal konumu: kesit düzleminden izleyiciye doğru ≈75° (diğerleri eşit aralıklı [V])")

    # ---- Filtre ------------------------------------------------------------------------------
    par("elek_goz", "75", "scalar", 75, 140, "[S] Elek göz açıklığı [µm]: 75 (200 mesh, 0.50–0.85 USgal/h) / 140 (120 mesh, 1.00–11.0)",
        True, "Elek göz açıklığı [µm]", 5, "Filtre")
    par("elek_tel", "52", "scalar", 30, 80, "[V] Elek tel çapı [µm] (kare örgü: 200 mesh → 52, 120 mesh → 72)", True,
        "Elek tel çapı [µm]", 1, "Filtre")
    par("L_elek", "9.10", "length", 7.5, 10.8, "[Ö] Elek kovanı boyu (çizim 9.10; zarf 20.5 max → ≤ 10.8)", True,
        "Elek boyu [mm]", 0.1, "Filtre")
    par("pen_aci", "150", "angle", 100, 160,
        "[V] VARSAYIM: filtre penceresi yay açısı (sıra başına n_pen pencere). Çizim yalnız 0°–90° arasının açık olduğunu gösterir (≥ 100°)", True,
        "Pencere açısı (VARSAYIM) [°]", 5, "Filtre")
    par("n_pen", "2", "count", 2, 2, "[V] VARSAYIM: sıra başına pencere adedi (köprüler 135° ve 315°'de: çizimde görünmeyen bölgeler)")
    par("pen_merkez", "45", "angle", 0, 360,
        "[Ö]/[V] 1. pencerenin merkez azimutu: çizimde kesit düzlemi (0°) ve arkadaki çeyrek (0°–90°) tamamen açık → merkez 45°")
    par("dLf", "L_elek - 9.10", "length", None, None, "[T] Elek boyu farkı (iki kanal tabanına eşit paylaştırılır)")
    par("elek_acik", "(elek_goz/(elek_goz+elek_tel))^2", "scalar", None, None, "[T] Elek açık alan oranı (kare örgü)", label="Elek açık alan oranı", group="Hesaplanan")
    par("elek_mesh", "25400/(elek_goz+elek_tel)", "scalar", None, None, "[T] Mesh sayısı [1/inç]", label="Elek mesh sayısı [1/inç]", group="Hesaplanan")

    # ---- Tutucu vida ---------------------------------------------------------------------------
    par("n_capraz", "2", "count", 1, 2, "[V] VARSAYIM: çapraz delik adedi (2 = boydan boya delinmiş; kesitte yalnız biri görünür)",
        True, "Çapraz delik adedi (VARSAYIM)", 1, "Tutucu vida")
    par("d_capraz", "2.40", "length", 1.5, 3.0, "[Ö] Çapraz delik çapı (Ø2.40)")
    par("z_capraz", "3.06", "length", None, None, "[Ö] Çapraz delik ekseni kotu")

    # ---- standard / fixed dimensions (not exposed) -------------------------------------------
    par("H_bas", "14.01", "length", None, None, "[S] Baş yüksekliği 14 ±0.2 (çizimde 14.01)")
    par("AF_alt", "15.85", "length", None, None, "[S] Altıgen anahtar ağzı 16 +0/−0.25 (çizimde 15.85)")
    par("AF_soket", "4.76", "length", None, None, "[S] Alyan yuvası SW 3/16\"")
    par("D_delik", "8.84", "length", None, None, "[Ö] Baş deliği = 3/8\"-40 UNS iç diş dibi çapı")
    par("D_dis", "9.48", "length", None, None, "[Ö] 3/8\"-40 UNS erkek diş üstü çapı (çizim)")
    par("D_vida", "7.48", "length", None, None, "[Ö] Tutucu vida gövde çapı")
    par("d_vida_delik", "3.90", "length", None, None, "[Ö] Tutucu vida eksenel deliği")
    par("d_filtre_delik", "7.40", "length", None, None, "[Ö] Filtre gövdesi deliği")
    par("D_etek", "7.60", "length", None, None, "[Ö] Swirler etek çapı")
    par("d_sap", "3.70", "length", None, None, "[Ö] Swirler sap çapı")
    par("R_tr", "0.48", "length", None, None, "[Ö] Orifis çıkış trompeti yarıçapı")
    par("h_tr", "0.28", "length", None, None, "[Ö] Çıkış trompeti yüksekliği (H2→H3)")
    par("h_havsa", "0.21", "length", None, None, "[Ö] Çıkış havşası derinliği (Ø3.98)")
    par("D_zarf", "11.8", "length", None, None, "[S] Filtre azami zarf çapı Ø11.8 max = akış hacmi dış sınırı [V: brülör adaptörü deliği]")
    par("L_zarf", "20.5", "length", None, None, "[S] Filtre azami zarf boyu 20.5 max = akış hacmi alt sınırı")

    # ---- derived axial stack-up (top of the head is fixed by the standard, the seat follows) ----
    par("z_f", "H_bas - h_havsa", "length", None, None, "[T] Havşa tabanı = trompet sonu (H3, H4)")
    par("z_2", "z_f - h_tr", "length", None, None, "[T] Orifis silindirinin sonu (H2)")
    par("z_1", "z_2 - Lo", "length", None, None, "[T] Orifis girişi (H1)")
    par("z_24", "z_1 - (Ds-d0)/2/tan(alfa_s/2)", "length", None, None, "[T] Girdap odası konisi başlangıcı (H24)")
    par("z_yuva", "z_24 - (4.29-Ds/2)/tan(koni_on/2)", "length", None, None, "[T] Yuva konisi başlangıcı (H23), çizimde 9.93")
    par("z_uc", "z_24 + (Ds-d_uc)/2/tan(koni_on/2)", "length", None, None, "[T] Swirler uç düzlüğü (S1,S2), çizimde 12.66")
    par("z_S3", "z_24 - (2.79-Ds/2)/tan(koni_on/2)", "length", None, None, "[T] Uç konisi sonu (S3), çizimde 10.98")
    par("z_S4", "z_S3 - (3.78-2.79)/tan(koni_arka/2)", "length", None, None, "[T] Arka koni sonu (S4), çizimde 9.27")
    par("z_alt", "z_S4 - 1.21", "length", None, None, "[T] Swirler alt yüzü = tutucu vida üst yüzü, çizimde 8.06")
    par("slot_s0", "-(h_slot*sin(koni_on/2)+0.15)/cos(koni_on/2)", "length", None, None,
        "[T] Slot takımının uç tarafı (anadoğru boyunca, uç düzlüğünü 0.15 aşar)")
    par("slot_s1", "(2.79-d_uc/2)/sin(koni_on/2) + h_slot/tan((koni_on-koni_arka)/2) + 0.3", "length", None, None,
        "[T] Slot takımının arka ucu (arka konide tükenme noktası + 0.3)")

    # ---- flow-path area table (read-only, derived) ---------------------------------------------
    par("A_orifis", "pi*d0^2/4", "scalar", None, None, "[T] Orifis alanı [mm²]", label="Orifis alanı [mm²]", group="Hesaplanan")
    par("A_slot", "n_slot*w_slot*h_slot", "scalar", None, None, "[T] Toplam slot alanı n·w·h [mm²] (ölçüm kesiti)", label="Slot alanı n·w·h (anma) [mm²]", group="Hesaplanan")
    par("A_slot_etkin", "n_slot*w_slot*(h_slot-(e_slot^2+w_slot^2/12)/Ds*cos(koni_on/2))", "scalar", None, None,
        "[T] Modeldeki GERÇEK en dar slot kesiti (girdap odası ağzı, r = Ds/2): kaçık düz kanal koni yüzeyine göre "
        "sığlaşır; n·w·h'den ≈ %7–9 küçük (OCCT kesit ölçümüyle doğrulandı) [mm²]", label="Slot en dar kesiti (model) [mm²]", group="Hesaplanan")
    par("A_oda_giris", "pi*((Ds/2-(z_uc-z_24)*tan(alfa_s/2))^2-(d_uc/2)^2)", "scalar", None, None,
        "[T] Swirler ucu ile girdap odası konisi arasındaki halka [mm²]")
    par("A_kama", "pi*(4.29^2-(3.78-(z_yuva-0.45-z_S4)*tan(koni_arka/2))^2)", "scalar", None, None,
        "[T] Ø8.58 kademe / arka koni halkası [mm²]")
    par("A_etek", "pi*(D_delik^2-D_etek^2)/4", "scalar", None, None, "[T] Etek/delik halka boşluğu [mm²]")
    par("A_halka", "pi*(D_delik^2-D_vida^2)/4", "scalar", None, None, "[T] Vida/delik halka boşluğu [mm²]")
    par("A_capraz", "n_capraz*pi*d_capraz^2/4", "scalar", None, None, "[T] Çapraz delik(ler) [mm²]")
    par("A_vida_delik", "pi*d_vida_delik^2/4", "scalar", None, None, "[T] Vida eksenel deliği [mm²]")
    par("A_soket", "sqrt(3)/2*AF_soket^2", "scalar", None, None, "[T] Alyan yuvası [mm²]")
    par("A_aralik", "pi*AF_soket*1.05*0.58", "scalar", None, None, "[T] Filtre üstü / vida altı aralığı (0.58 mm, alyan ağzı çevresi) [mm²]")
    par("A_filtre_delik", "pi*d_filtre_delik^2/4", "scalar", None, None, "[T] Filtre gövde deliği [mm²]")
    par("A_pencere", "n_pen*pen_aci/360*2*pi*3.895*(3.20+dLf)", "scalar", None, None,
        "[T] Filtre pencereleri, 2 sıra (ort. yarıçap 3.895) [mm²] — pencere açısı VARSAYIM")
    par("A_elek", "2*pi*4.74*(6.41+dLf)*elek_acik", "scalar", None, None, "[T] Kanal üstü serbest elek açık alanı [mm²]")
    par("A_min_diger", "min(min(min(A_oda_giris,A_kama),min(A_etek,A_halka)),min(min(A_capraz,A_vida_delik),min(min(A_soket,A_aralik),min(A_filtre_delik,min(A_pencere,A_elek)))))",
        "scalar", None, None, "[T] Slot ve orifis dışındaki en dar geçit [mm²]")
    par("oran_slot_orifis", "A_slot/A_orifis", "scalar", None, None, "[T] A_slot / A_orifis", label="A_slot / A_orifis", group="Hesaplanan")
    par("oran_diger_orifis", "A_min_diger/A_orifis", "scalar", None, None, "[T] En dar diğer geçit / A_orifis (≫1 olmalı)", label="En dar diğer geçit / A_orifis", group="Hesaplanan")
    par("K_geo", "A_slot/(Ds*d0)", "scalar", None, None, "[T] Atomizör sabiti K = A_p/(D_s·d0)", label="Atomizör sabiti K = A_slot/(Ds·d0)", group="Hesaplanan")
    par("Cd_RL", "0.35*K_geo^0.5*(Ds/d0)^0.25", "scalar", None, None,
        "[T] Rizk–Lefebvre: Cd = 0.35·K^0.5·(Ds/d0)^0.25 (gösterge; mikro/viskoz nozul için doğrulanmamış)", label="Cd (Rizk–Lefebvre, gösterge)", group="Hesaplanan")
    par("Q_RL", "Cd_RL*A_orifis/1000000*sqrt(2*rho*dP*100000)*3600", "scalar", None, None,
        "[T] Rizk–Lefebvre Cd ile beklenen debi [kg/h] (gösterge)", label="Debi (Rizk–Lefebvre, gösterge) [kg/h]", group="Hesaplanan")
    par("FN", "(Q/3600)/sqrt(dP*100000*rho)", "scalar", None, None, "[T] Akış sayısı FN = ṁ/√(ΔP·ρ) [m²]")
    par("aci_RL", "6*K_geo^(-0.15)*(dP*100000*(d0/1000)^2*rho/mu^2)^0.11", "angle", None, None,
        "[T] Rizk–Lefebvre püskürtme açısı 2θ = 6·K^−0.15·(ΔP·d0²·ρ/μ²)^0.11 [°] (gösterge)", label="Püskürtme açısı 2θ (R–L, gösterge)", group="Hesaplanan")
    par("Lo_d0", "Lo/d0", "scalar", None, None, "[T] Orifis L/d", label="Orifis L/d", group="Hesaplanan")
    par("Ds_d0", "Ds/d0", "scalar", None, None, "[T] Ds/d0 (tipik 2.7–5)", label="Ds/d0", group="Hesaplanan")
    par("v_slot", "(Q/3600)/rho/(A_slot/1000000)", "scalar", None, None, "[T] Slot ortalama hızı [m/s]", label="Slot ortalama hızı [m/s]", group="Hesaplanan")
    return P


# python mirror of the PersistCAD expression evaluator (trig in degrees)
def make_env(P, overrides=None):
    env = {"pi": math.pi, "sqrt": math.sqrt, "min": min, "max": max, "abs": abs,
           "tan": lambda x: math.tan(math.radians(x)), "sin": lambda x: math.sin(math.radians(x)),
           "cos": lambda x: math.cos(math.radians(x)), "__builtins__": {}}
    for p in P:
        e = (overrides or {}).get(p["name"], p["expr"])
        env[p["name"]] = eval(str(e).replace("^", "**"), env)
    return env


# ============================================================================ sketch builder
class SK:
    """Ordinate-dimensioned sketch: pt() pins a point with DistanceX/DistanceY from the origin."""
    def __init__(self, ev, plane="XZ", offset="0", planeRef=""):
        self.ev = ev
        self.pts = [{"id": 0, "x": 0.0, "y": 0.0}]
        self.ents, self.cons = [], []
        self.nid = 1
        self.plane, self.offset, self.planeRef = plane, offset, planeRef
    def _id(self):
        i = self.nid; self.nid += 1; return i
    def pt(self, xe, ye, pin_x=True, pin_y=True):
        xe, ye = str(xe), str(ye)
        i = self._id()
        self.pts.append({"id": i, "x": float(self.ev(xe)), "y": float(self.ev(ye))})
        if pin_x: self.cons.append({"id": self._id(), "type": "DistanceX", "refs": [0, i], "value": xe})
        if pin_y: self.cons.append({"id": self._id(), "type": "DistanceY", "refs": [0, i], "value": ye})
        return i
    def line(self, a, b):
        i = self._id(); self.ents.append({"id": i, "type": "line", "points": [a, b]}); return i
    def arc(self, c, s, t):
        """CCW arc from s to t around c (radius implied by |s-c|)."""
        i = self._id()
        p = {q["id"]: q for q in self.pts}
        r = math.hypot(p[s]["x"] - p[c]["x"], p[s]["y"] - p[c]["y"])
        self.ents.append({"id": i, "type": "arc", "points": [c, s, t], "radius": r}); return i
    def circle(self, c, dia_expr):
        i = self._id()
        self.ents.append({"id": i, "type": "circle", "points": [c], "radius": float(self.ev(dia_expr)) / 2})
        self.cons.append({"id": self._id(), "type": "Diameter", "refs": [i], "value": str(dia_expr)})
        return i
    def poly(self, ids, arcs=None):
        """Closed polygon through ids; arcs = {k: (centre, start, end)} replaces edge k -> k+1."""
        arcs = arcs or {}
        for k in range(len(ids)):
            if k in arcs: self.arc(*arcs[k])
            else: self.line(ids[k], ids[(k + 1) % len(ids)])
    def json(self):
        pl = {"kind": self.plane, "faceRef": "", "offset": self.offset}
        if self.planeRef: pl["planeRef"] = self.planeRef
        return {"plane": pl, "points": self.pts, "entities": self.ents, "constraints": self.cons, "nextId": self.nid}


def hexagon(sk, af):
    """Regular hexagon, across-flats `af` (expression), corners on the u axis (0°, 60°, ...)."""
    c, h = f"({af})/sqrt(3)", f"({af})/2"
    pts = [(c, "0"), (f"{c}/2", h), (f"-{c}/2", h), (f"-{c}", "0"), (f"-{c}/2", f"-{h}"), (f"{c}/2", f"-{h}")]
    sk.poly([sk.pt(x, y) for x, y in pts])


# ============================================================================ model
def build(variant="tam"):
    """variant: 'tam' (5 solids = hardware), 'kesit' (solids, quarter cut), 'akis' (fluid volume only)."""
    P = make_params()
    env = make_env(P)
    ev = lambda e: eval(str(e).replace("^", "**"), env)
    F = []
    def feat(fid, ftype, props=None, refs=None, sketch=None):
        f = {"id": fid, "type": ftype, "suppressed": False, "props": props or {}, "refs": refs or []}
        if sketch is not None: f["sketch"] = sketch.json()
        F.append(f)

    # ------------------------------------------------------------------ 1) Govde (head, 1.4005)
    # Revolve profile H1..H24 (SPEC §2.1): outside + TRUE internal cavity. The orifice cylinder is left
    # solid here (axis points A1, A2) and drilled afterwards as a Hole feature so that the hole rules
    # (min Ø, L/d) are tied to it. Arcs: trumpet H2-H3 (R_tr) and dome H8-H9 (R7.98, centre (-0.19, 6.77)).
    sk = SK(ev, "XZ")
    H1 = sk.pt("d0/2", "z_1"); A1 = sk.pt("0", "z_1"); A2 = sk.pt("0", "z_2"); H2 = sk.pt("d0/2", "z_2")
    Ct = sk.pt("d0/2+R_tr", "z_2")
    H3 = sk.pt("d0/2+R_tr-sqrt(R_tr^2-h_tr^2)", "z_f", pin_x=False)      # x follows from the arc radius
    H4 = sk.pt("1.91", "z_f"); H5 = sk.pt("1.99", "z_f+0.07"); H6 = sk.pt("1.99", "H_bas-0.05")
    H7 = sk.pt("2.05", "H_bas"); H8 = sk.pt("3.17", "H_bas")
    Cd_ = sk.pt("-0.19", "H_bas-7.24")
    H9 = sk.pt("7.79", "H_bas-7.00", pin_x=False)                         # x follows from the dome radius
    H10 = sk.pt("9.14", "5.87"); H11 = sk.pt("9.14", "1.59")
    H12 = sk.pt("7.85", "0.51"); H13 = sk.pt("7.85", "0"); H14 = sk.pt("6.15", "0"); H15 = sk.pt("6.15", "-1.50")
    H16 = sk.pt("7.08", "-2.43"); H17 = sk.pt("7.08", "-6.49"); H18 = sk.pt("6.08", "-7.49"); H19 = sk.pt("5.19", "-7.49")
    H20 = sk.pt("D_delik/2", "-6.70"); H21 = sk.pt("D_delik/2", "z_yuva-0.52"); H22 = sk.pt("4.29", "z_yuva-0.45")
    H23 = sk.pt("4.29", "z_yuva"); H24 = sk.pt("Ds/2", "z_24")
    ids = [H1, A1, A2, H2, H3, H4, H5, H6, H7, H8, H9, H10, H11, H12, H13, H14, H15, H16, H17, H18, H19, H20, H21, H22, H23, H24]
    sk.poly(ids, arcs={3: (Ct, H3, H2), 9: (Cd_, H9, H8)})
    feat("Ck_Govde", "Sketch", sketch=sk)
    feat("Govde_Don", "Revolve", {"sketch": "Ck_Govde", "axis": "V", "angle": "360", "op": "new", "bodyName": "Govde"})
    # hexagon 16 A/F: prism intersected with the revolved body (corners in the section plane)
    sk = SK(ev, "XY", offset="-9"); hexagon(sk, "AF_alt")
    feat("Ck_Altigen", "Sketch", sketch=sk)
    feat("Govde_Altigen", "Extrude", {"sketch": "Ck_Altigen", "distance": "24", "direction": "normal", "op": "intersect", "targets": "Govde"})
    # orifice: drilled from the trumpet side down into the swirl chamber
    sk = SK(ev, "XY", offset="z_2")
    p = sk.pt("0", "0", pin_x=False, pin_y=False)
    sk.cons.append({"id": sk._id(), "type": "Coincident", "refs": [0, p]})
    feat("Ck_Orifis", "Sketch", sketch=sk)
    feat("Orifis", "Hole", {"sketch": "Ck_Orifis", "diameter": "d0", "depth": "Lo+0.02", "kind": "simple", "tipAngle": "0", "targets": "Govde"})

    # ------------------------------------------------------------------ 2) Swirler (1.4005)
    # Double cone S1..S12 (SPEC §2.2); the 110° cone lies ON the head seat cone (contact band r 0.76..2.79).
    sk = SK(ev, "XZ")
    ids = [sk.pt("0", "z_uc"), sk.pt("d_uc/2", "z_uc"), sk.pt("2.79", "z_S3"), sk.pt("3.78", "z_S4"),
           sk.pt("D_etek/2", "z_S4-0.06"), sk.pt("D_etek/2", "z_S4-1.01"), sk.pt("D_etek/2-0.20", "z_alt"),
           sk.pt("d_sap/2+0.20", "z_alt"), sk.pt("d_sap/2", "z_alt-0.20"), sk.pt("d_sap/2", "4.85"),
           sk.pt("1.44", "4.16"), sk.pt("0", "4.16")]
    sk.poly(ids)
    feat("Ck_Swirler", "Sketch", sketch=sk)
    feat("Swirler_Don", "Revolve", {"sketch": "Ck_Swirler", "axis": "V", "angle": "360", "op": "new", "bodyName": "Swirler"})
    # Tangential slot (SPEC §3.2 "CAD tarifi"): datum plane through the axis at azimuth slot_az; the sketch
    # plane is that plane shifted by e_slot (slot centre plane, parallel to the meridian plane). Profile =
    # strip between the floor line (parallel to the 110° generatrix, h_slot inside) and a line 0.25 outside
    # the cone, from 0.15 above the tip flat to 0.3 past the run-out on the back cone. Extruded symmetric
    # by w_slot (slitting-saw cut), then patterned n_slot times about the axis.
    feat("Dz_Slot", "Plane", {"kind": "axisAngle", "axis": "Z", "angle": "slot_az", "show": "0"})
    sk = SK(ev, "OnPlane", offset="-e_slot", planeRef="Dz_Slot")
    sA, cA = "sin(koni_on/2)", "cos(koni_on/2)"
    fx = lambda s, off: f"d_uc/2 - ({off})*{cA} + ({s})*{sA}"       # point = tip + off*n_in + s*g
    fy = lambda s, off: f"z_uc - ({off})*{sA} - ({s})*{cA}"
    q = [sk.pt(fx("slot_s0", "h_slot"), fy("slot_s0", "h_slot")), sk.pt(fx("slot_s1", "h_slot"), fy("slot_s1", "h_slot")),
         sk.pt(fx("slot_s1", "-0.25"), fy("slot_s1", "-0.25")), sk.pt(fx("slot_s0", "-0.25"), fy("slot_s0", "-0.25"))]
    sk.poly(q)
    feat("Ck_Slot", "Sketch", sketch=sk)
    feat("Slot", "Extrude", {"sketch": "Ck_Slot", "distance": "w_slot", "direction": "symmetric", "op": "cut", "targets": "Swirler"})
    feat("Slot_Cogalt", "Pattern", {"kind": "circular", "source": "Slot", "count": "n_slot", "angle": "360", "axis": "Z", "targets": "Swirler"})

    # ------------------------------------------------------------------ 3) TutmaVidasi (holding screw, Ms58)
    # K1..K10 (SPEC §2.3). The bore mouth gets a 0.1x45° break that mates with the swirler root (S8-S9);
    # the socket is pre-bored 0.02 under the hexagon flats so the hexagon cut is never tangent to it.
    sk = SK(ev, "XZ")
    ids = [sk.pt("d_vida_delik/2", "z_alt-0.10"), sk.pt("d_vida_delik/2+0.10", "z_alt"), sk.pt("D_vida/2", "z_alt"),
           sk.pt("D_vida/2", "-0.73"), sk.pt("D_dis/2-0.20", "-0.73"), sk.pt("D_dis/2", "-0.95"), sk.pt("D_dis/2", "-2.94"),
           sk.pt("D_dis/2-0.20", "-3.13"), sk.pt("AF_soket/2-0.02", "-3.13"), sk.pt("AF_soket/2-0.02", "-0.62"),
           sk.pt("d_vida_delik/2", "-0.62+(AF_soket/2-0.02-d_vida_delik/2)")]
    sk.poly(ids)
    feat("Ck_Vida", "Sketch", sketch=sk)
    feat("Vida_Don", "Revolve", {"sketch": "Ck_Vida", "axis": "V", "angle": "360", "op": "new", "bodyName": "TutmaVidasi"})
    sk = SK(ev, "XY", offset="-3.63"); hexagon(sk, "AF_soket")
    feat("Ck_Alyan", "Sketch", sketch=sk)
    feat("Vida_Alyan", "Extrude", {"sketch": "Ck_Alyan", "distance": "2.99", "direction": "normal", "op": "cut", "targets": "TutmaVidasi"})
    # radial cross hole(s) Ø2.40 at z 3.06, axis in the section plane; n_capraz = 2 -> drilled right through
    sk = SK(ev, "YZ", offset="-(n_capraz-1)*5")
    c = sk.pt("0", "z_capraz"); sk.circle(c, "d_capraz")
    feat("Ck_Capraz", "Sketch", sketch=sk)
    feat("Vida_Capraz", "Extrude", {"sketch": "Ck_Capraz", "distance": "n_capraz*5", "direction": "normal", "op": "cut", "targets": "TutmaVidasi"})

    # ------------------------------------------------------------------ 4) Filtre (support body) + Elek (mesh sleeve)
    # F1..F22 (SPEC §2.4): threaded, blind (118° drill point), two circumferential grooves. L_elek stretches the
    # two groove floors equally (dLf/2 each).
    sk = SK(ev, "XZ")
    h, d = "dLf/2", "dLf"
    ids = [sk.pt("d_filtre_delik/2", "-3.71"), sk.pt("D_dis/2-0.20", "-3.71"), sk.pt("D_dis/2", "-3.91"), sk.pt("D_dis/2", "-5.46"),
           sk.pt("4.29", "-5.90"), sk.pt("4.29", "-6.53"), sk.pt("D_dis/2", "-6.91"), sk.pt("D_dis/2", "-8.10"),
           sk.pt("4.09", "-8.65"), sk.pt("4.09", f"-10.77-{h}"), sk.pt("D_dis/2", f"-11.30-{h}"), sk.pt("D_dis/2", f"-12.30-{h}"),
           sk.pt("4.09", f"-12.86-{h}"), sk.pt("4.09", f"-14.96-{d}"), sk.pt("D_dis/2", f"-15.51-{d}"), sk.pt("D_dis/2", f"-16.71-{d}"),
           sk.pt("5.15", f"-16.71-{d}"), sk.pt("5.15", f"-18.51-{d}"), sk.pt("4.95", f"-18.71-{d}"), sk.pt("0", f"-18.71-{d}"),
           sk.pt("0", f"-17.92-{d}"), sk.pt("d_filtre_delik/2", f"-15.71-{d}")]
    sk.poly(ids)
    feat("Ck_Filtre", "Sketch", sketch=sk)
    feat("Filtre_Don", "Revolve", {"sketch": "Ck_Filtre", "axis": "V", "angle": "360", "op": "new", "bodyName": "Filtre"})
    # windows at the groove floors: two rows, n_pen windows of pen_aci each [V]; sketched on a datum plane
    # through the axis at the start azimuth of window 1 and revolved by pen_aci, then patterned.
    feat("Dz_Pencere", "Plane", {"kind": "axisAngle", "axis": "Z", "angle": "pen_merkez-pen_aci/2", "show": "0"})
    sk = SK(ev, "OnPlane", planeRef="Dz_Pencere")
    sk.poly([sk.pt("3.40", "-8.90"), sk.pt("4.39", "-8.90"), sk.pt("4.39", f"-10.51-{h}"), sk.pt("3.40", f"-10.51-{h}")])
    sk.poly([sk.pt("3.40", f"-13.11-{h}"), sk.pt("4.39", f"-13.11-{h}"), sk.pt("4.39", f"-14.70-{d}"), sk.pt("3.40", f"-14.70-{d}")])
    feat("Ck_Pencere", "Sketch", sketch=sk)
    feat("Filtre_Pencere", "Revolve", {"sketch": "Ck_Pencere", "axis": "V", "angle": "pen_aci", "op": "cut", "targets": "Filtre"})
    feat("Pencere_Cogalt", "Pattern", {"kind": "circular", "source": "Filtre_Pencere", "count": "n_pen", "angle": "360", "axis": "Z", "targets": "Filtre"})
    # mesh sleeve M1..M4 (SPEC §2.5): thin sleeve = porous zone for CFD; opening / open-area ratio are parameters
    if variant != "akis":
      sk = SK(ev, "XZ")
      sk.poly([sk.pt("D_dis/2", "-7.61"), sk.pt("D_dis/2+0.10", "-7.61"), sk.pt("D_dis/2+0.10", f"-16.71-{d}"), sk.pt("D_dis/2", f"-16.71-{d}")])
      feat("Ck_Elek", "Sketch", sketch=sk)
      feat("Elek_Don", "Revolve", {"sketch": "Ck_Elek", "axis": "V", "angle": "360", "op": "new", "bodyName": "Elek"})

    # thread engagement (cosmetic threads): the head bore is opened to the male thread crests where the
    # screw and the filter are screwed in, so the bodies touch instead of overlapping.
    feat("Govde_DisYuvasi", "Combine", {"op": "cut", "target": "Govde", "tools": "TutmaVidasi,Filtre", "keepTools": "1"})

    # ------------------------------------------------------------------ 5) AkisHacmi (wetted fluid volume)
    if variant == "akis":
        # envelope: adapter bore Ø11.8 x 20.5 below the head, then a plug that stays inside the head wall (Ø9.8, a cone
        # parallel to the seat, Ø5.2) up to the tip face; minus the solids.
        # The mesh sleeve is NOT subtracted (porous zone), so the path filter-outside -> windows stays open.
        sk = SK(ev, "XZ")
        sk.poly([sk.pt("0", "-L_zarf"), sk.pt("D_zarf/2", "-L_zarf"), sk.pt("D_zarf/2", "-7.0"), sk.pt("4.9", "-7.0"),
                 sk.pt("4.9", "z_yuva-0.2"), sk.pt("2.6", "z_yuva-0.2+2.3/tan(koni_on/2)"), sk.pt("2.6", "H_bas"), sk.pt("0", "H_bas")])
        feat("Ck_Zarf", "Sketch", sketch=sk)
        feat("Zarf_Don", "Revolve", {"sketch": "Ck_Zarf", "axis": "V", "angle": "360", "op": "new", "bodyName": "AkisHacmi", "mfg": "0", "role": "fluid"})
        feat("AkisHacmi_Kes", "Combine", {"op": "cut", "target": "AkisHacmi", "tools": "Govde,Swirler,TutmaVidasi,Filtre",
                                          "keepTools": "0", "largestOnly": "1"})
    if variant == "kesit":
        # quarter cut (x>0, y<0) of everything except the swirler, as in the datasheet view
        sk = SK(ev, "XY", offset="-L_zarf-1")
        sk.poly([sk.pt("0", "0", pin_x=False, pin_y=False), sk.pt("12", "0"), sk.pt("12", "-12"), sk.pt("0", "-12")])
        sk.cons.append({"id": sk._id(), "type": "Coincident", "refs": [0, 1]})
        feat("Ck_Kesit", "Sketch", sketch=sk)
        feat("Kesit", "Extrude", {"sketch": "Ck_Kesit", "distance": "40", "direction": "normal", "op": "cut",
                                  "targets": "Govde,TutmaVidasi,Filtre,Elek"})
    doc = {
        "schema": "persistcad/1",
        "name": {"tam": "Enjektor_09-DB01_Simplex", "kesit": "Enjektor_09-DB01_Kesit", "akis": "Enjektor_09-DB01_AkisHacmi"}[variant],
        "parameters": P,
        "features": F,
        "mfg": {
            "additive": {"enabled": False, "process": "LPBF_Metal", "material": "316L", "minWallThickness": 0.4,
                         "minFeatureSize": 0.3, "minHoleDiameter": 0.5, "maxSelfSupportingHoleDia": 8,
                         "minOverhangAngle": 45, "minGap": 0.3, "maxAspectRatio": 8, "minEscapeHoleDia": 2,
                         "build": [250, 250, 300], "buildDir": [0, 0, 1], "layerThickness": 0.03},
            # turning + milling of micro features: slot by slitting saw (min blade 0.10 mm) or wire EDM,
            # orifice by micro drilling / EDM (min Ø0.15, L/d <= 3 for a clean land)
            # (the fluid-volume variant is reference geometry only: no machining checks, parameter rules still apply)
            "conventional": {"enabled": variant != "akis", "process": "CNC_Turning", "material": "1.4005 (X12CrS13)",
                             "minToolDiameter": 0.10, "maxToolLengthToDia": 6, "maxHoleDepthToDia": 3,
                             "minWallThickness": 0.08, "maxWallHeightToThickness": 15, "minHoleDiameter": 0.15,
                             "preferStandardDrills": False, "stock": [20, 20, 60], "toolAxis": [0, 0, 1]},
        },
    }
    return doc, env


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(here, ".."))
    for variant, name in (("tam", "enjektor_09DB01.pcad"), ("kesit", "enjektor_09DB01_kesit.pcad"), ("akis", "enjektor_09DB01_akis.pcad")):
        doc, env = build(variant)
        path = os.path.join(out, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(doc, f, ensure_ascii=False, indent=1)
        print(f"yazildi {path}: {len(doc['features'])} ozellik, {len(doc['parameters'])} parametre")
    print("d0=%.4f z_24=%.3f z_yuva=%.3f z_uc=%.3f z_S3=%.3f z_S4=%.3f z_alt=%.3f" %
          tuple(env[k] for k in ("d0", "z_24", "z_yuva", "z_uc", "z_S3", "z_S4", "z_alt")))
    print("A_slot=%.4f A_orifis=%.4f K=%.4f Cd_RL=%.3f Q_RL=%.2f aci_RL=%.1f" %
          tuple(env[k] for k in ("A_slot", "A_orifis", "K_geo", "Cd_RL", "Q_RL", "aci_RL")))


if __name__ == "__main__":
    main()
