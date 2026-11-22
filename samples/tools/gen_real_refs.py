#!/usr/bin/env python3
"""Reference (ground-truth) parametric models of the two REAL sample drawings in tests/drawing/real/:

  ref_swc_rr_001.pcad  radial-radial swirl cup  SWC-RR-001  (single body "SwirlCup")
  ref_atm_ps_001.pcad  simplex atomizer         ATM-PS-001   (two bodies "NozzleTip" (1) + "SwirlerInsert" (2))

Hand-built from the drawings (NOT from the drawing-import code). Interpretations are documented in
docs/drawing_import/R4_REFERENCE.md. Parameter names follow the drawing's own labels (D0, Ds, d0, lo ...).
Frame: PersistCAD Z = drawing axis x (x = 0 at the forward / exit face, flow along +x for the cup),
all sketches are fully ordinate-dimensioned (DOF 0) exactly like gen_enjektor.py.

Usage: gen_real_refs.py [outdir]   (default: samples/ next to this script)
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_enjektor import SK, make_env  # same sketch / evaluator helpers as the enjektor generator


def par(P, name, expr, kind="length", desc="", exposed=False, mn=None, mx=None, group=""):
    P.append(dict(name=name, expr=str(expr), kind=kind, min=mn, max=mx, description=desc, exposed=exposed,
                  label=name if exposed else "", step=0, group=group))


MFG = {"additive": {"enabled": False, "process": "LPBF_Metal", "material": "316L", "minWallThickness": 0.4,
                    "minFeatureSize": 0.3, "minHoleDiameter": 0.5, "maxSelfSupportingHoleDia": 8, "minOverhangAngle": 45,
                    "minGap": 0.3, "maxAspectRatio": 8, "minEscapeHoleDia": 2, "build": [250, 250, 300],
                    "buildDir": [0, 0, 1], "layerThickness": 0.03},
       "conventional": {"enabled": False, "process": "CNC_Turning", "material": "generic", "minToolDiameter": 0.1,
                       "maxToolLengthToDia": 6, "maxHoleDepthToDia": 3, "minWallThickness": 0.08,
                       "maxWallHeightToThickness": 15, "minHoleDiameter": 0.15, "preferStandardDrills": False,
                       "stock": [20, 20, 60], "toolAxis": [0, 0, 1]}}


class Builder:
    def __init__(self, name):
        self.name, self.P, self.F = name, [], []
    def par(self, *a, **k): par(self.P, *a, **k)
    def env(self):
        self.E = make_env(self.P)
        self.ev = lambda e: eval(str(e).replace("^", "**"), self.E)
        return self.ev
    def feat(self, fid, ftype, props=None, sketch=None):
        f = {"id": fid, "type": ftype, "suppressed": False, "props": props or {}, "refs": []}
        if sketch is not None: f["sketch"] = sketch.json()
        self.F.append(f)
    def doc(self):
        return {"schema": "persistcad/1", "name": self.name, "parameters": self.P, "features": self.F, "mfg": MFG}


# =============================================================================================== SWC-RR-001
def swirl_cup():
    B = Builder("SWC-RR-001_RadialRadialSwirlCup")
    p = B.par
    # --- overall / drawing dimensions (names = drawing labels)
    p("D_outer", 50, desc="Ø50 outer diameter of the cup body (x = 0 .. x_groove)", exposed=True)
    p("D_bore", 10, desc="Ø10 fuel nozzle bore through the forward face (x = 0..x_pri0)", exposed=True)
    p("L_total", 30, desc="30 overall length, x = 0 at forward face", exposed=True)
    p("x_pri0", 3, desc="3 : primary slots start (dim chain 3-5-2-6-3-11)")
    p("L_pri", 5, desc="5 : primary slot / chamber axial length (x = 3..8)", exposed=True)
    p("x_pri1", "x_pri0+L_pri", desc="8 : primary slots end = venturi inlet face")
    p("D_pri", 24, desc="Ø24 primary chamber diameter = venturi inlet (section B-B, note 5)", exposed=True)
    p("a_vent", 90, "angle", "90° venturi inlet cone, included angle", exposed=True)
    p("D_throat", 14, desc="Ø14 venturi throat", exposed=True)
    p("L_throat", 2, desc="2 throat cylinder length (note 5: 'x 2 LONG')", exposed=True)
    p("x_th0", "x_pri1+(D_pri-D_throat)/2/tan(a_vent/2)", desc="13 : throat start (inlet cone length 5)")
    p("x_th1", "x_th0+L_throat", desc="15 : throat end")
    p("L_exit_cone", 6, desc="6 : venturi exit (divergent) cone length (x = 15..21)", exposed=True)
    p("x_lip", "x_th1+L_exit_cone", desc="21 : venturi lip plane")
    p("D_vent_exit", 17, desc="Ø17 venturi exit inner diameter at the lip", exposed=True)
    p("D_lip", 19, desc="Ø19 venturi lip outer diameter at x = 21", exposed=True)
    p("D_vent_root", 22, desc="Ø22 venturi root O.D. at x = x_sec0", exposed=True)
    p("x_sec0", 10, desc="10 : secondary slots / chamber start (= venturi root)")
    p("L_sec", 6, desc="6 : secondary slot / chamber axial length (x = 10..16)", exposed=True)
    p("x_sec1", "x_sec0+L_sec", desc="16 : secondary chamber end")
    p("D_sec", 34, desc="Ø34 secondary chamber diameter (section C-C)", exposed=True)
    p("D0", 27, desc="Ø27 (D0) reference diameter = bore after the secondary chamber, x = x_sec1..x_flare", exposed=True)
    p("x_groove", 19, desc="19 : rear face of the Ø50 body = start of the Ø32 hub groove")
    p("D_hub", 32, desc="Ø32 hub (groove floor)", exposed=True)
    p("w_groove", 3.5, desc="3.5 groove width (x = 19..22.5)", exposed=True)
    p("x_gr1", "x_groove+w_groove", desc="22.5 : groove end where the outer flare cone leaves the hub")
    p("a_flare", 90, "angle", "90° flare, included angle", exposed=True)
    p("wall_flare", 2, desc="2 flare wall (measured radially/axially, not normal to the cone)", exposed=True)
    p("x_flare", 22, desc="22 : flare start (inner cone leaves Ø27 bore); dim 22 + 8")
    p("D_flare_in", "D0+2*(L_total-x_flare)/tan(a_flare/2)", desc="Ø43 flare inner exit diameter at x = 30")
    p("D_flare_out", "D_flare_in+2*wall_flare", desc="Ø47 flare outer exit diameter at x = 30")
    # --- slots
    p("n_pri", 8, "count", "8 primary tangential slots, CCW swirl viewed from downstream", exposed=True)
    p("w_pri", 4, desc="4 primary slot width", exposed=True)
    p("off_pri", 8, desc="8 primary slot centreline offset from the axis (edges at 6 and 10)", exposed=True)
    p("n_sec", 10, "count", "10 secondary tangential slots, CW swirl viewed from downstream", exposed=True)
    p("w_sec", 4, desc="4 secondary slot width", exposed=True)
    p("off_sec", 11, desc="11 secondary slot centreline offset (edges at 9 and 13)", exposed=True)
    p("t0_sec", 9, desc="[interp] secondary slot cutter starts 9 mm along the slot line so it never touches the venturi root")
    p("t_end", 28, desc="[interp] slot cutter length along the tangent (runs out through Ø50)")
    # --- derived (read-only, drawing notes)
    p("A_pri", "n_pri*w_pri*L_pri", "scalar", "160 mm² primary flow area (note 4)")
    p("A_sec", "n_sec*w_sec*L_sec", "scalar", "240 mm² secondary flow area (note 4)")
    p("A_throat", "pi*D_throat^2/4", "scalar", "154 mm² venturi throat (note 4)")
    ev = B.env()

    # revolve profile in (r, x) -> sketch (u = r, v = x), axis V
    pts = [("D_bore/2", "0"), ("D_outer/2", "0"), ("D_outer/2", "x_groove"), ("D_hub/2", "x_groove"),
           ("D_hub/2", "x_gr1"), ("D_flare_out/2", "L_total"), ("D_flare_in/2", "L_total"), ("D0/2", "x_flare"),
           ("D0/2", "x_sec1"), ("D_sec/2", "x_sec1"), ("D_sec/2", "x_sec0"), ("D_vent_root/2", "x_sec0"),
           ("D_lip/2", "x_lip"), ("D_vent_exit/2", "x_lip"), ("D_throat/2", "x_th1"), ("D_throat/2", "x_th0"),
           ("D_pri/2", "x_pri1"), ("D_pri/2", "x_pri0"), ("D_bore/2", "x_pri0")]
    sk = SK(ev, "XZ")
    sk.poly([sk.pt(u, v) for u, v in pts])
    B.feat("Ck_Profile", "Sketch", sketch=sk)
    B.feat("Cup_Don", "Revolve", {"sketch": "Ck_Profile", "axis": "V", "angle": "360", "op": "new", "bodyName": "SwirlCup"})

    def slot(tag, off, w, x0, L, n, sign, t0, count):
        # view from downstream (+Z looking at -Z) == standard XY view. Strip: t in [t0, t_end] along +X,
        # offset edges |Y| in [off-w/2, off+w/2]; sign=+1 -> flow (outer end -> chamber, direction -X) is CCW.
        a, b = f"({off})-({w})/2", f"({off})+({w})/2"
        if sign < 0: a, b = f"-({b})", f"-({a})"
        s = SK(ev, "XY", offset=x0)
        s.poly([s.pt(t0, a), s.pt("t_end", a), s.pt("t_end", b), s.pt(t0, b)])
        B.feat(f"Ck_Slot_{tag}", "Sketch", sketch=s)
        B.feat(f"Slot_{tag}", "Extrude", {"sketch": f"Ck_Slot_{tag}", "distance": L, "direction": "normal", "op": "cut",
                                          "targets": "SwirlCup"})
        B.feat(f"Slot_{tag}_Cogalt", "Pattern", {"kind": "circular", "source": f"Slot_{tag}", "count": count, "angle": "360",
                                                 "axis": "Z", "targets": "SwirlCup"})
    slot("Pri", "off_pri", "w_pri", "x_pri0", "L_pri", 8, +1, "0", "n_pri")
    slot("Sec", "off_sec", "w_sec", "x_sec0", "L_sec", 10, -1, "t0_sec", "n_sec")
    return B


# =============================================================================================== ATM-PS-001
def atomizer():
    B = Builder("ATM-PS-001_SimplexAtomizer")
    p = B.par
    p("L_total", 10, desc="10 overall length of the assembly (tip + insert), x = 0 at the orifice exit face", exposed=True)
    p("D_out", 8, desc="Ø8 nozzle tip outer diameter", exposed=True)
    p("chamfer", 0.5, desc="0.5 x 45° outer exit chamfer", exposed=True)
    p("d0", 0.8, desc="Ø0.8 orifice (d0 / do)", exposed=True)
    p("D_exit", 2, desc="Ø2 exit countersink diameter at the forward face", exposed=True)
    p("a_exit", 90, "angle", "90° exit cone included angle")
    p("L_exit", "(D_exit-d0)/2/tan(a_exit/2)", desc="0.6 exit cone length (dimension 0.6)")
    p("lo", 0.4, desc="0.4 orifice land length (lo; lo/do = 0.50)", exposed=True)
    p("x_c1", "L_exit+lo", desc="1.0 : orifice land end / start of the swirl-chamber cone")
    p("Ds", 3.2, desc="Ø3.2 swirl chamber diameter (Ds; Ds/do = 4.0)", exposed=True)
    p("a_cone", 90, "angle", "90° swirl chamber cone included angle", exposed=True)
    p("x_c0", "x_c1+(Ds-d0)/2/tan(a_cone/2)", desc="2.2 : chamber cylinder start (end of the 90° cone)")
    p("L_tip", 4.2, desc="4.2 length of part (1) at its bore / front face of the insert", exposed=True)
    p("Ls", "L_tip-x_c0", desc="2 swirl chamber cylindrical length (Ls; Ls/Ds = 0.625)")
    p("D_bore", 5, desc="Ø5 tip counterbore = insert outer diameter", exposed=True)
    p("L_ins", "L_total-L_tip", desc="5.8 swirler insert (2) length")
    p("n_slot", 3, "count", "3 tangential slots, eq. sp. 120°", exposed=True)
    p("slot_w", 0.5, desc="0.5 slot width (tangential)", exposed=True)
    p("slot_d", 0.5, desc="0.5 slot depth (axial, into the insert front face)", exposed=True)
    p("slot_off", "Ds/2-slot_w/2", desc="1.35 slot centreline offset from the axis (outer wall tangent to chamber, note 5)")
    p("slot_len", 1.9, desc="[interp] slot strip length from the tangent point to the groove (view B)")
    p("groove_w", 0.6, desc="0.6 feed groove width", exposed=True)
    p("groove_d", 0.5, desc="0.5 feed groove depth (radial, flat floor)", exposed=True)
    p("groove_az", 37, "angle", "[interp] azimuth of feed groove 1 in the model frame (view B: 143° from +u, mirrored)")
    p("Ap", "n_slot*slot_w*slot_d", "scalar", "0.75 mm² total slot area (note 3)")
    p("K", "Ap/(Ds*d0)", "scalar", "0.29 atomizer constant K = Ap/(Ds*do) (note 4)")
    ev = B.env()

    # (1) nozzle tip: revolve, profile in (r, x) -> (u = r, v = x)
    pts = [("D_exit/2", "0"), ("D_out/2-chamfer", "0"), ("D_out/2", "chamfer"), ("D_out/2", "L_total"),
           ("D_bore/2", "L_total"), ("D_bore/2", "L_tip"), ("Ds/2", "L_tip"), ("Ds/2", "x_c0"), ("d0/2", "x_c1"),
           ("d0/2", "L_exit")]
    sk = SK(ev, "XZ")
    sk.poly([sk.pt(u, v) for u, v in pts])
    B.feat("Ck_Tip", "Sketch", sketch=sk)
    B.feat("Tip_Don", "Revolve", {"sketch": "Ck_Tip", "axis": "V", "angle": "360", "op": "new", "bodyName": "NozzleTip"})
    # (2) swirler insert: plain cylinder Ø5 x 5.8 from x = 4.2 (revolved rectangle)
    sk = SK(ev, "XZ")
    sk.poly([sk.pt("0", "L_tip"), sk.pt("D_bore/2", "L_tip"), sk.pt("D_bore/2", "L_total"), sk.pt("0", "L_total")])
    B.feat("Ck_Insert", "Sketch", sketch=sk)
    B.feat("Insert_Don", "Revolve", {"sketch": "Ck_Insert", "axis": "V", "angle": "360", "op": "new", "bodyName": "SwirlerInsert"})
    # slot 1: strip X in [0, slot_len], Y in [slot_off -/+ slot_w/2], cut slot_d deep into the front face (+Z)
    sk = SK(ev, "XY", offset="L_tip")
    ya, yb = "slot_off-slot_w/2", "slot_off+slot_w/2"
    sk.poly([sk.pt("0", ya), sk.pt("slot_len", ya), sk.pt("slot_len", yb), sk.pt("0", yb)])
    B.feat("Ck_Slot", "Sketch", sketch=sk)
    B.feat("Slot", "Extrude", {"sketch": "Ck_Slot", "distance": "slot_d", "direction": "normal", "op": "cut", "targets": "SwirlerInsert"})
    B.feat("Slot_Cogalt", "Pattern", {"kind": "circular", "source": "Slot", "count": "n_slot", "angle": "360", "axis": "Z",
                                      "targets": "SwirlerInsert"})
    # feed groove 1: rotated rectangle s in [D_bore/2-groove_d, D_bore/2+0.3], |w| <= groove_w/2, full insert length
    c, s_ = "cos(groove_az)", "sin(groove_az)"
    def gp(s, w): return (f"({s})*{c}-({w})*{s_}", f"({s})*{s_}+({w})*{c}")
    s0, s1, wh = "D_bore/2-groove_d", "D_bore/2+0.3", "groove_w/2"
    sk = SK(ev, "XY", offset="L_tip")
    sk.poly([sk.pt(*gp(s0, f"-{wh}")), sk.pt(*gp(s1, f"-{wh}")), sk.pt(*gp(s1, wh)), sk.pt(*gp(s0, wh))])
    B.feat("Ck_Groove", "Sketch", sketch=sk)
    B.feat("Groove", "Extrude", {"sketch": "Ck_Groove", "distance": "L_ins", "direction": "normal", "op": "cut", "targets": "SwirlerInsert"})
    B.feat("Groove_Cogalt", "Pattern", {"kind": "circular", "source": "Groove", "count": "n_slot", "angle": "360", "axis": "Z",
                                        "targets": "SwirlerInsert"})
    return B


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(here, ".."))
    for fn, B in (("ref_swc_rr_001.pcad", swirl_cup()), ("ref_atm_ps_001.pcad", atomizer())):
        path = os.path.join(out, fn)
        json.dump(B.doc(), open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print("wrote", path, len(B.F), "features", len(B.P), "params")
        E = B.E
        print("  ", {k: round(E[k], 4) for k in E if k in ("x_th0", "x_th1", "x_lip", "x_gr1", "D_flare_in", "D_flare_out",
                                                          "A_pri", "A_sec", "A_throat", "L_exit", "x_c0", "Ls", "L_ins", "Ap", "K", "slot_off")})


if __name__ == "__main__":
    main()
