#!/usr/bin/env python3
"""R3 fixtures: real PDFs (tests/drawing/real/{radrad_swirler_SWC-RR-001,simplex_atomizer_ATM-PS-001}.pdf) after the
existing WP-1 ingest + normalize, with the glyph-outline text (matplotlib PDF: text drawn as Bezier outlines; r1's
glyph-text OCR is not in this copy) replaced by hand-written TextRuns at the glyph clusters' sheet boxes (read from
the PNG renders hi_*.png / the glyph clusters of the normalized sheet).

Regenerate:
  cd build && for f in radrad_swirler_SWC-RR-001 simplex_atomizer_ATM-PS-001; do
    cp ../tests/drawing/real/$f.pdf /tmp/r3dump/ ;
    PC_WP1_DUMP=/tmp/r3dump/$f.pdf PC_WP1_NORM=1 bin/pc_drawing_tests --gtest_also_run_disabled_tests \
        --gtest_filter=Wp1Tool.* ; done
  python3 ../tests/drawing/fixtures/r3/gen_r3_fixtures.py /tmp/r3dump
Output: swirl_cup_SWC-RR-001.pcdrw.json, atomizer_ATM-PS-001.pcdrw.json (next to this script; committed).
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (text, [x0, y0, x1, y1] sheet mm box of the glyph cluster, rotDeg)
SWIRL = [
    ("B", [22.46, 104.39, 23.55, 106.10], 0), ("C", [28.17, 104.36, 29.41, 106.13], 0),
    ("A", [86.25, 105.94, 87.66, 107.65], 0), ("A", [86.25, 57.55, 87.66, 59.25], 0),
    ("A", [137.44, 105.94, 138.85, 107.65], 0), ("A", [137.44, 57.55, 138.85, 59.25], 0),
    ("VENTURI INLET CONE 90° INCL.", [44.00, 114.10, 69.46, 115.47], 0),
    ("FLARE 90° INCL., WALL 2", [49.58, 103.45, 69.66, 105.00], 0),
    ("Ø14 THROAT", [104.57, 105.92, 115.09, 107.36], 0), ("VENTURI", [154.13, 104.41, 161.12, 105.75], 0),
    ("Ø50", [66.94, 58.61, 70.10, 60.05], 0), ("Ø24", [105.34, 58.61, 108.52, 60.05], 0),
    ("Ø50", [118.12, 58.61, 121.29, 60.05], 0), ("Ø34", [153.43, 58.61, 156.61, 60.05], 0),
    ("4", [109.87, 92.24, 111.18, 93.11], 90), ("6", [109.61, 84.52, 110.98, 85.34], 90),
    ("9", [162.66, 78.69, 164.03, 79.51], 90), ("4", [161.05, 69.75, 162.37, 70.62], 90),
    ("SECTION B-B  PRIMARY SWIRLER", [68.33, 53.71, 105.67, 55.64], 0),
    ("8x TANGENTIAL SLOTS 4 x 5, EQ. SP. 45° - CCW SWIRL", [66.04, 50.89, 107.97, 52.37], 0),
    ("(viewed from downstream)", [78.00, 48.66, 95.92, 49.99], 0),
    ("SECTION C-C  SECONDARY SWIRLER", [117.41, 53.71, 158.97, 55.64], 0),
    ("10x TANGENTIAL SLOTS 4 x 6, EQ. SP. 36° - CW SWIRL", [117.34, 50.89, 159.11, 52.37], 0),
    ("(viewed from downstream)", [129.19, 48.66, 147.10, 49.99], 0),
    ("Ø22 VENTURI ROOT O.D. AT x=10", [40.19, 44.26, 67.85, 45.70], 0), ("Ø32 HUB", [42.52, 41.55, 49.72, 42.99], 0),
    ("SECTION A-A", [18.60, 36.57, 34.47, 38.66], 0),
    ("NOTES:", [5.67, 31.78, 10.91, 33.02], 0),
    ("1. ALL DIMENSIONS IN mm. SHEET A3, SCALE 2:1. x = 0 AT FORWARD FACE, FLOW LEFT TO RIGHT.", [5.69, 29.05, 79.24, 30.46], 0),
    ("2. COUNTER-ROTATING RADIAL-RADIAL SWIRL CUP: 8 PRIMARY PASSAGES (CCW), 10 SECONDARY PASSAGES (CW),", [5.63, 26.46, 91.35, 27.93], 0),
    ("VENTURI AND FLARE; REFERENCE DIAMETER D0 = 27. CONFIGURATION AFTER WANG ET AL., J. FLUID MECH. 583 (2007).", [6.95, 23.79, 96.86, 25.37], 0),
    ("3. ONLY THE PASSAGE COUNTS, SWIRL SENSES AND D0 ARE FROM THE PAPER. ALL OTHER DIMENSIONS ARE CHOSEN HERE.", [5.64, 21.37, 98.33, 22.78], 0),
    ("THE PAPER'S ELLIPTICAL PRIMARY JETS ARE REPLACED BY STRAIGHT RECTANGULAR SLOTS (SKETCH + EXTRUDE-CUT).", [6.93, 18.67, 95.49, 20.25], 0),
    ("4. SLOT CENTRELINE OFFSET FROM AXIS: PRIMARY 8, SECONDARY 11. FLOW AREAS: PRIMARY 160 mm2, SECONDARY 240 mm2", [5.60, 16.25, 100.89, 17.67], 0),
    ("(SPLIT 40/60); VENTURI THROAT 154 mm2.", [7.06, 13.67, 38.87, 15.13], 0),
    ("5. FUEL NOZZLE BORE Ø10. VENTURI: INLET Ø24, THROAT Ø14 x 2 LONG, EXIT Ø17, LIP Ø19 AT x = 21.", [5.64, 11.13, 82.59, 12.58], 0),
    ("6. MODELLING: (a) REVOLVE HALF PROFILE 360°; (b) CUT 8 PRIMARY SLOTS x = 3..8; (c) CUT 10 SECONDARY SLOTS x = 10..16.", [5.63, 8.55, 100.71, 10.02], 0),
    ("7. GENERIC RESEARCH GEOMETRY - NOT AN ENGINE PART DRAWING.", [5.64, 6.18, 57.20, 7.43], 0),
    ("TITLE", [114.10, 17.92, 117.39, 18.92], 0), ("RADIAL-RADIAL SWIRL CUP", [124.75, 17.81, 149.18, 19.34], 0),
    ("TYPE", [114.10, 14.50, 117.08, 15.51], 0), ("COUNTER-ROTATING, 8 PRI / 10 SEC", [124.66, 14.23, 152.32, 15.69], 0),
    ("SCALE", [114.19, 11.07, 118.01, 12.12], 0), ("2:1", [124.69, 11.01, 126.90, 12.28], 0),
    ("SHEET A3", [129.08, 10.99, 136.36, 12.28], 0), ("UNITS mm", [138.56, 10.99, 146.39, 12.28], 0),
    ("DWG NO", [114.22, 7.66, 119.49, 8.70], 0), ("SWC-RR-001", [124.67, 7.58, 134.14, 8.86], 0),
    ("REV A", [135.41, 7.60, 139.86, 8.84], 0), ("PURPOSE", [114.22, 4.25, 119.86, 5.29], 0),
    ("PersistCAD drawing-import input", [124.72, 3.83, 149.63, 5.48], 0),
]

ATOM = [
    ("(1) NOZZLE TIP", [13.48, 107.12, 24.77, 108.60], 0), ("(2) SWIRLER INSERT", [35.74, 107.12, 50.85, 108.60], 0),
    ("10", [40.85, 102.05, 42.60, 103.39], 0), ("0.5 x 45°", [11.03, 101.24, 18.11, 102.58], 0),
    ("0.5", [38.30, 99.82, 40.59, 101.16], 0), ("Ø0.8 ORIFICE", [5.74, 69.61, 15.57, 70.94], 0),
    ("Ø2", [12.57, 79.55, 13.98, 81.58], 90), ("Ø3.2", [35.23, 78.78, 36.65, 82.35], 90),
    ("Ø5", [65.99, 79.55, 67.41, 81.60], 90), ("Ø8", [70.04, 79.55, 71.45, 81.63], 90),
    ("0.6", [17.45, 60.25, 19.79, 61.59], 0), ("0.4", [25.50, 60.25, 28.90, 61.59], 0), ("2", [34.01, 61.16, 34.75, 62.48], 0),
    ("4.2", [28.76, 57.52, 31.06, 58.84], 0), ("5.8", [49.04, 57.49, 51.35, 58.84], 0),
    ("0.5", [116.25, 88.96, 118.73, 91.26], 90), ("1.35", [120.50, 85.78, 121.85, 89.03], 90),
    ("Ø5", [144.51, 83.60, 145.92, 85.65], 90),
    ("VIEW B  - SWIRLER (2) FRONT FACE", [112.09, 66.49, 149.25, 68.56], 0),
    ("3x TANGENTIAL SLOTS, EQ. SP. 120°", [117.97, 64.45, 143.46, 65.80], 0),
    ("dashed = swirl chamber Ø3.2 (part 1)", [119.42, 62.42, 142.00, 63.72], 0),
    ("SECTION A-A", [34.59, 50.49, 48.92, 52.39], 0),
    ("(axial plane through feed groove 1)", [29.36, 48.12, 54.01, 49.63], 0),
    ("NOTES:", [5.41, 40.08, 10.70, 41.34], 0),
    ("1. ALL DIMENSIONS IN mm. SHEET A3, SCALE 10:1.", [5.43, 37.39, 43.57, 38.83], 0),
    ("2. SIMPLEX PRESSURE-SWIRL ATOMIZER, HOLLOW-CONE SPRAY.", [5.37, 34.88, 53.60, 36.31], 0),
    ("3. 3 SLOTS 0.5 x 0.5, Ap = 0.75 mm2; GROOVES 0.6 x 0.5.", [5.38, 32.22, 49.18, 33.80], 0),
    ("4. Ds/do = 4.0, lo/do = 0.50, Ls/Ds = 0.625, K = Ap/(Ds.do) = 0.29.", [5.33, 29.71, 55.69, 31.32], 0),
    ("5. SLOT OUTER WALL TANGENT TO SWIRL CHAMBER WALL.", [5.38, 27.52, 49.78, 28.78], 0),
    ("6. BREAK ALL SHARP EDGES EXCEPT ORIFICE EXIT EDGE.", [5.37, 25.01, 48.02, 26.27], 0),
    ("7. GENERIC REFERENCE GEOMETRY FOR CAD IMPORT TESTS -", [5.38, 22.50, 51.72, 23.76], 0),
    ("NOT A RELEASED / FLIGHT DESIGN.", [6.84, 19.85, 33.09, 21.25], 0),
    ("TITLE", [103.20, 18.63, 106.63, 19.69], 0), ("SIMPLEX PRESSURE-SWIRL ATOMIZER", [113.04, 18.60, 146.93, 20.12], 0),
    ("PARTS", [103.33, 14.97, 107.32, 16.06], 0), ("(1) NOZZLE TIP  (2) SWIRLER INSERT", [113.05, 14.77, 140.57, 16.25], 0),
    ("SCALE", [103.29, 11.32, 107.29, 12.41], 0), ("10:1", [113.08, 11.32, 116.14, 12.58], 0),
    ("SHEET A3", [118.76, 11.32, 125.88, 12.58], 0), ("UNITS mm", [128.51, 11.32, 136.17, 12.58], 0),
    ("DWG NO", [103.33, 7.68, 108.83, 8.77], 0), ("ATM-PS-001", [112.93, 7.67, 121.81, 8.93], 0),
    ("REV A", [123.05, 7.70, 127.40, 8.91], 0), ("PURPOSE", [103.33, 4.03, 109.21, 5.12], 0),
    ("PersistCAD drawing-import test input", [113.06, 3.70, 140.79, 5.32], 0),
]

R3_EXPECT = {
    "swirl_cup_SWC-RR-001": {"B": {"tag": "pri", "count": 8, "pitch": 45, "width": 4, "offset": 8, "sense": "CCW",
                                  "rIn": 12, "rOut": 25, "x": [3, 8]},
                            "C": {"tag": "sec", "count": 10, "pitch": 36, "width": 4, "offset": 11, "sense": "CW",
                                  "rIn": 17, "rOut": 25, "x": [10, 16]}, "flow": 1},
    "atomizer_ATM-PS-001": {"B": {"slot": {"tag": "slt", "count": 3, "pitch": 120, "width": 0.5, "offset": 1.35,
                                           "depth": 0.5, "sense": "CW"},
                                  "groove": {"tag": "grv", "count": 3, "width": 0.6, "depth": 0.5, "rOut": 2.5}},
                            "flow": -1},
}


def glyph(p):
    if p["kind"] not in ("Bezier", "Polyline"):
        return False
    xs = [q[0] for q in p["pts"]]
    ys = [q[1] for q in p["pts"]]
    return max(xs) - min(xs) < 4 and max(ys) - min(ys) < 4


def text_run(tid, s, box, rot):
    x0, y0, x1, y1 = box
    if rot == 90:
        h = x1 - x0
        origin = [x1, y0]
        quad = [[x1, y0], [x1, y1], [x0, y1], [x0, y0]]
    else:
        h = y1 - y0
        origin = [x0, y0]
        quad = [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]
    return {"id": tid, "utf8": s, "origin": origin, "height": round(h, 3), "rotDeg": rot, "quad": quad,
            "font": "r3-hand", "rgb": 0, "alternatives": [], "asOutlines": True, "invisible": False,
            "fromDimBlock": False, "conf": 0.95, "prov": {"source": "VectorPdf", "page": 0, "ref": "r3 hand text (glyph cluster)"},
            "mergedFrom": [], "attrs": {"r3.handText": "1"}}


def build(norm_path, texts, name):
    norm = json.load(open(norm_path))
    keep = [p for p in norm["prims"] if not glyph(p)]
    removed = len(norm["prims"]) - len(keep)
    nid = max([p["id"] for p in norm["prims"]] + [0]) + 1
    norm["prims"] = keep
    norm["texts"] = []
    for s, box, rot in texts:
        norm["texts"].append(text_run(nid, s, box, rot))
        nid += 1
    norm["file"] = os.path.basename(norm_path).replace(".norm.json", "")
    norm["attrs"]["r3.glyphOutlinesRemoved"] = str(removed)
    ir = {"schema": "pcdrw/1", "irVersion": "1.0", "nextId": nid, "raw": {"prims": [], "texts": []}, "norm": norm,
          "lastStage": 2, "attrs": {"r3.fixture": name}, "edits": [], "r3Expect": R3_EXPECT[name]}
    out = os.path.join(HERE, name + ".pcdrw.json")
    with open(out, "w") as f:
        json.dump(ir, f, separators=(",", ":"))
    print(out, len(keep), "prims,", len(texts), "texts,", removed, "glyph outlines removed")


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "/tmp/r3dump"
    build(os.path.join(d, "radrad_swirler_SWC-RR-001.pdf.norm.json"), SWIRL, "swirl_cup_SWC-RR-001")
    build(os.path.join(d, "simplex_atomizer_ATM-PS-001.pdf.norm.json"), ATOM, "atomizer_ATM-PS-001")
