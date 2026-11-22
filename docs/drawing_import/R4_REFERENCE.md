# R4 — ground-truth reference models of the two real sample drawings

Hand-built (not derived from the import code) from `tests/drawing/real/radrad_swirler_SWC-RR-001.pdf` and
`simplex_atomizer_ATM-PS-001.pdf`.

| file | purpose |
|---|---|
| `samples/tools/gen_real_refs.py` | generator (same style as `gen_enjektor.py`; fully ordinate-dimensioned sketches, parameters named as on the drawings) |
| `samples/ref_swc_rr_001.pcad`, `samples/ref_atm_ps_001.pcad` | the reference models |
| `tests/drawing/real/ground_truth/{swc_rr_001,atm_ps_001}.json` | recorded volume, bbox, bodies, features, patterns, parameters |
| `.../{swc_rr_001,atm_ps_001}.step` | STEP export of the models |
| `.../*.dims.json` | every dimension text of each sheet with kind / nominal / meaning / expected parameter name |
| `tests/drawing/test_real_acceptance.cpp` | `RealRef.*` (always run) and `RealAcceptance.*` (full pipeline, skipped until enabled) |

Regenerate: `python3 samples/tools/gen_real_refs.py`, then `PC_R4_WRITE_GT=1 pc_drawing_tests --gtest_filter=RealRef.*` rewrites json + STEP.
Enable acceptance: `PC_REAL_ACCEPT=1 pc_drawing_tests --gtest_filter=RealAcceptance.*` (or delete the two `REQUIRE_ENABLED()` lines in the test).
Acceptance checks: body count, volume within 3 %, sorted bbox extents within 2 % (rotation invariant), circular-pattern counts
(8 and 10 / 3 and 3), and that every key dimension value of the sheet occurs among the parameter values (names may differ).

Frame: model Z = drawing axis x (x = 0 at the forward / exit face); sketches of revolve profiles are on XZ (u = radius, v = x).

## Numbers (OCCT regeneration, all features ok)

| model | bodies | volume mm³ | bbox | independent check (numpy point sampling of the drawing definition) |
|---|---|---|---|---|
| SWC-RR-001 | 1 (`SwirlCup`) | 26288.4 | Ø50 x 30 (X,Y ±25, Z 0..30) | 26273.8 (0.06 %) |
| ATM-PS-001 | 2 (`NozzleTip` 364.27, `SwirlerInsert` 107.57) | 471.85 | Ø8 x 10 (Z 0..10; insert Z 4.2..10) | 364.37 + 107.62 (0.03 %) |

Derived values reproduce the drawing notes: A_pri = 160, A_sec = 240, throat 153.9 mm² (154), flare 43/47, groove 22.5, Ap = 0.75, K = 0.293 (0.29), Ls = 2.0, exit cone 0.6, chamber cone end 2.2.

## SWC-RR-001 — interpretation

Revolve profile (x, r), 19 points, one body: (0,5) (0,25) (19,25) (19,16) (22.5,16) (30,23.5) (30,21.5) (22,13.5) (16,13.5) (16,17) (10,17) (10,11) (21,9.5) (21,8.5) (15,7) (13,7) (8,12) (3,12) (3,5).
* Venturi: inlet cone 90° Ø24 @ x=8 -> Ø14 @ x=13, throat 2 long (x 13..15), exit cone to Ø17 @ x=21; lip Ø19 @ x=21; outer surface from Ø22 @ x=10 straight to the lip (single cone). Unlabeled in the drawing and assumed: the venturi outer is one straight line.
* Flare 90° incl., "wall 2" read as 2 mm measured radially (= axially, not normal to the cone): inner Ø27 @ x=22 -> Ø43 @ x=30, outer Ø32 @ x=22.5 -> Ø47 @ x=30; the 3.5 groove (x 19..22.5, floor Ø32 "hub") follows. This is the only reading that closes all of Ø43/Ø47/3.5/22/8.
* Primary chamber is Ø24 (section B-B and note 5), not the ~Ø28 outline seen in section A-A: A-A is drawn on the cut plane through the slot walls, whose intersection with the plane lies at r = 14.1. Chamber Ø34 for the secondary stage (C-C) is consistent with A-A.
* Slots are one-sided straight rectangular cutters (sketch on a plane normal to the axis, extruded 5 / 6 along x): primary strip |Y| = 6..10 (width 4, centreline 8), X = 0..28 from the axis; secondary |Y| = 9..13 (centreline 11) starting 9 mm along the line (so the cutter never touches the venturi root Ø22), X to 28. Circular pattern 8 x 45° / 10 x 36°. Both strips are anchored from the picture of the sheets: the primary strip sits at +Y (above the axis in B-B) and the secondary at -Y (mirror image, CW).
* Swirl sense: "viewed from downstream" = looking from +Z at the XY plane = standard XY orientation; CCW primary is the strip at +Y flowing toward -X (L_z > 0), CW secondary at -Y. Checked with solid-classification points.
* Not on the drawing / not modelled: edge breaks, fuel nozzle, material.

## ATM-PS-001 — interpretation

* (1) NozzleTip, revolve (x, r): (0,1) (0,3.5) (0.5,4) (10,4) (10,2.5) (4.2,2.5) (4.2,1.6) (2.2,1.6) (1.0,0.4) (0.6,0.4): 0.5x45° outer chamfer, Ø2 90° exit countersink (0.6 long), Ø0.8 orifice land 0.4 (x 0.6..1.0), 90° cone to Ø3.2 chamber (x 1.0..2.2), chamber cylinder 2.0 long to x=4.2, Ø5 counterbore x 4.2..10 (insert seat), Ø8 outside.
* (2) SwirlerInsert: cylinder Ø5, x 4.2..10 (5.8), front face against the chamber floor at x=4.2; touching, not merged -> 2 bodies.
* Slots: 3 tangential strips 0.5 wide, cut 0.5 deep (axial, x 4.2..4.7) into the insert front face; centreline offset 1.35 = Ds/2 - 0.25 (outer wall tangent to the chamber, note 5, dimension 1.35 in view B); strip starts at the tangent point (closed end wall) and runs 1.9 outward into the feed groove [interp: length chosen so both strip end corners lie inside the groove].
* Feed grooves: 0.6 wide, 0.5 deep (flat floor at r = 2.0), full insert length x 4.2..10 (section A-A shows the groove open to x=10); centre azimuth 37° in the model frame. View B shows groove 1 at 143° from +u; the model frame is mirrored (X = -u) so that the model is right-handed for a viewer on the orifice side (view B looks at the front face from the front, i.e. along +x). Pattern 3 x 120° for slot and groove.
* The A-A section through groove 1 draws the notch r 1.6..2.5 white for x 4.2..4.7; in the model the slot strip crosses the groove plane at r ≈ 1.8..2.5, so that detail differs by < 0.02 mm³ in volume.
* "Break all sharp edges except orifice exit edge" (note 6) is not modelled (no size given); only the dimensioned 0.5x45° chamfer.
* The rotated labels "°06" and "Ø6" near the orifice are the two 90° angle dimensions (read upside down), not 60 or Ø6.

## Integration (16.11.2026)
* `RealAcceptance.*` always runs (REQUIRE_ENABLED removed) and checks only the **top-ranked** candidate (the one the
  wizard / CLI select), no longer "any candidate".
* Key value `22.5` of SWC-RR-001 replaced by `3.5`: 22.5 is not printed on the sheet (it is the derived `x_gr1 =
  x_groove + w_groove`), the test contract is "every key dimension value of the sheet"; 3.5 is the printed value.
  Reference model, volume and STEP unchanged (`ground_truth/swc_rr_001.json` key_values edited only).
* No interpretation error found in the references; the import now reproduces both (26288.4 / 471.84 mm³).
