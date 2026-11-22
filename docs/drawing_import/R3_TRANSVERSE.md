# R3 — transverse sections, slots/grooves, notes → cut features

Status 16.11.2026. Scope: the non-axisymmetric features of turned combustor parts that the drawing gives in
transverse views (swirl cup SWC-RR-001: SECTION B-B / C-C; simplex atomizer ATM-PS-001: VIEW B). The revolve
itself is r2's job; R3 adds the slots/grooves to whatever revolved bodies exist. No frozen contract file changed.

## 1. Files (owned, merge copies as-is)

| Path | Content |
|---|---|
| `src/drawing/interp/Notes.{h,cpp}` | notes / caption parser → `NoteHints` (feature hints, captions, scale, flow, named Ø, tangency, modelling steps) |
| `src/drawing/interp/Transverse.{h,cpp}` | transverse-view analysis → `TransverseResult` (slot/groove families, scale, axial link, flow sign); `attachTransverse` (S5 hook) |
| `src/drawing/synth/CutFeatures.{h,cpp}` | `addTransverseCuts` (params + Plane/Sketch/Extrude-cut/Pattern per family), `applyTransverseCuts` (S7 hook) |
| `tests/drawing/test_notes_r3.cpp` | `R3Notes.*` (6 tests) |
| `tests/drawing/test_transverse_r3.cpp` | `R3Transverse.*` (3), `R3CutFeatures.*` (3) |
| `tests/drawing/fixtures/r3/gen_r3_fixtures.py`, `swirl_cup_SWC-RR-001.pcdrw.json`, `atomizer_ATM-PS-001.pcdrw.json` | IR fixtures |
| `docs/drawing_import/R3_TRANSVERSE.md` | this file |

Private headers use relative includes (`"Notes.h"`, `"../interp/Transverse.h"`) so tests can include them by path
without a CMake change (tests/drawing/CMakeLists.txt is frozen).

## 2. Hooks (exact lines)

`src/drawing/interp/Pipeline.cpp`
```cpp
#include "interp/Transverse.h"   // R3 hook
...
      case Stage::Views:
        r = buildViews(ir, opts.interp, prog);
        if (r.ok()) attachTransverse(ir, r);   // R3 hook: transverse-view slot/groove families -> views.attrs
        break;
```
Writes `ir.views.attrs["r3.transverse.<tag>"]` (e.g. `B: 8x slot W4 OFS8 CCW H5 pitch45 phase45 x=3..8`),
`"r3.transverse.flow"`, Info diagnostics `r3.transverse`, Warnings `r3.transverse.mismatch` (drawing vs note).

`src/drawing/synth/Synthesize.cpp`
```cpp
#include "synth/CutFeatures.h"   // R3 hook
...
    // R3 hook: non-axisymmetric slots/grooves from transverse views (synth/CutFeatures.h)
    if (revolve) synth::applyTransverseCuts(ir, doc, s.warnings, s.attrs);
    // ---- regenerate
```
Only for revolve / section-revolve candidates; candidate attrs `r3.cuts` (feature ids), `r3.cuts.ok`.
The analysis is a pure function of `ir.norm`, so S7 recomputes it (no serialization contract needed).

## 3. Approach

**Notes (Notes.cpp).** Canonical form: upper case, Turkish fold, Ø→`@`, °→`~`, ×→`X`; OCR repairs: O/Q/I/L/|/S/B
glued to digits → digits, digits inside words → letters, decimal comma, `". ."`→`..`, garbled count tokens before a
feature word (`lO SECONDARY SLOTS`→10), `014 THROAT`→Ø14 (WP-2 grammar `parseDimText` with circle context, or a
leading-zero integer), `45o`/`450` after `EQ. SP.`→45°. Each repair is recorded and multiplies confidence by
`kConfCorrectionPenalty`. Keywords are matched with confusion classes. A text is split into *feature phrases* at
each `[CUT] [n[x]] [TANGENTIAL] [PRIMARY|SECONDARY] SLOTS|GROOVES|PASSAGES`; inside a phrase: `a x b` (width x
height/depth), `EQ. SP. p°`, CCW/CW, `x = a..b`. Sentences parsed and blanked first: `SLOT CENTRELINE OFFSET ...
PRIMARY 8, SECONDARY 11`, `SLOT OUTER WALL TANGENT TO <name>`; global: `SCALE n:m`, `FLOW LEFT TO RIGHT`,
`VIEWED FROM DOWNSTREAM`, `MODELLING: (a) .. (b) ..`, named diameters (`Ø14 THROAT`, `SWIRL CHAMBER Ø3.2`),
captions (`SECTION B-B PRIMARY SWIRLER`, `VIEW B - SWIRLER (2) FRONT FACE` → letter, role, part no., face).
Sheet merge: a text right below a caption inherits its role/letter; hints merge by (kind, role).

**Transverse views (Transverse.cpp).** Concentric circle/arc groups with ≥180° coverage on the largest radius.
Lines inside 1.08·R are oriented outward (inner→outer end); signed offset d = cross(t, a−C). Parallel same-direction
pairs: same sign → tangential slot (e = (d1+d2)/2, W = |d1−d2|, sense in view = sign e: CCW if e > 0, because the
inflow −t at foot e·n has angular momentum e); opposite symmetric → radial notch (groove; bottom = along-t inner
end). Families: cluster (type, W, e) at 0.8 % R, one pair per angle (closest to the family median), accepted if
N ≥ 3, equal pitch (±2°) and equal radial span per instance — section hatching (parallel lines, two directions)
never passes. Through-wall slot if both inner wall ends lie on one circle (snapped to it), else closed end at uIn.
Scale per view = vote of Ø texts vs circle diameters (the PDFs are reduced prints: "2:1" is not the sheet scale;
B-B factor 1.2894), fallback note scale. Caption = nearest caption below the circles. Hints (by caption letter, then
role) override count/width/offset/height/sense when consistent, else warn; tangency note gives offset = D/2 − W/2.
Axial link: forward face x = 0 = min x of visible-width lines of the longitudinal view in the same row; section
marker letter (outside every transverse view) → `xMarker = (x_marker − x_face)·f`, cross-checked against note range
`x = a..b` (warn if outside); without a range: xMarker ± H/2 (conf ×0.7). `... FACE` captions → face-relative slot of
depth H; grooves → full body length. Flow sign: note, else `ORIFICE` callout left/right of the longitudinal view,
else +x.

**Synthesis (CutFeatures.cpp).** Bodies revolve about Z, drawing x → +Z (x = 0 at min Z unless `zOfX0`). View taken
as seen from downstream; view angle a → a (flow +Z) or 180−a (flow −Z, viewer looks along +Z, X mirrored);
`SENSE_T = flow · senseView` = sign of angular momentum about +Z. Per family T:
`PL_T` Plane axisAngle Z, angle `PHI_T`, offset `-SENSE_T*OFS_T` (plane through the slot centreline ∥ Z);
`SK_T` rectangle (u along slot, v = Z) fully dimensioned by expressions; `CUT_T` Extrude cut symmetric `W_T`,
`targets` = owning body; `PAT_T` circular, `N_T`, 360°, axis Z. Through-wall slots start inside the bore at
`sqrt(RI^2-(OFS+W/2)^2)-0.1W` and run to RO+0.5; face slots from u = 0 (tangent point) to RO+0.5, v from the
downstream face into the part by H; grooves u ∈ [RO−H, RO+0.5], full body length ±0.5.
Target body: caption part number "(2)" → 2nd body; else the body containing a point of the slot annulus; else the
body whose OD matches. Parameters (tags [D]/[Ö]/[T]/[V] in the description): `N_T W_T H_T OFS_T (DSC_T) RI_T RO_T
PHI_T SENSE_T X0_T X1_T`, `FLOW_R3`; tags `pri sec slt grv` (`_2`… if repeated).

## 4. Metrics (tests, all green; full ctest 288/288 — 3 mfg timing tests need a serial rerun under -j4 load)

| Item | Drawing | Extracted |
|---|---|---|
| B-B primary | 8 slots, 45°, CCW, 4×5, ofs 8, Ø24/Ø50, x 3..8 | 8 / 45° / CCW (geom = note) / W 4.00 / ofs 8.00 / r 12–25 / x 3..8, marker B at x≈5.5 |
| C-C secondary | 10 slots, 36°, CW, 4×6, ofs 11, Ø34/Ø50, x 10..16 | 10 / 36° / CW / 4.00 / 11.00 / r 17–25 / x 10..16, marker C at x≈13 |
| View B slots | 3 × 0.5×0.5, 120°, outer wall tangent Ø3.2 | 3 / 120° / CW in view / W 0.50 / ofs 1.35 (=3.2/2−0.25) / closed end at tangent point / depth 0.5 from front face |
| View B grooves | 0.6×0.5 | 3 / 120° / W 0.60 / floor r 2.0 / OD 2.5 / full length |
| Flow | swirl cup: note; atomizer: orifice left | +1 / −1 |

Volumes vs hand computation (strip area ∫(√(Ro²−n²)−√(Ri²−n²))dn × height × N): swirl cup cut volume within 1e-4
relative (also after `N_pri=6, W_pri=3`); venturi body untouched; atomizer slots and grooves analytic within 1e-4 mm³,
both together vs grid-quadrature union within 2e-3 mm³; tip body untouched. Handedness checked with solid
classification (void at the drawn slot, material at its mirror image) and sign of r × (−u). S7 hook test: a
SectionRevolve candidate gets `PAT_pri`, `PAT_sec`, regenerates, volume = revolve − hand cut volume.

## 5. Comparison with r4 reference (R4_REFERENCE.md, samples/ref_*.pcad)

Agrees: primary strip at +Y, CCW (L_z > 0), 8×45°, centreline 8, width 4, x 3..8; secondary at −Y, CW, 10×36°,
centreline 11, x 10..16; atomizer slots offset 1.35, 0.5 deep at x 4.2..4.7, closed end at the tangent point, 3×120°;
model frame mirrored for view B (our 180−a), grooves 0.6×0.5, floor r 2.0, full length 4.2..10, azimuth 36.9° (r4: 37°).
Differs (both documented interpretations): r4's atomizer slot strip is 1.9 long ending inside the feed groove,
R3 runs it to the OD (same Ap; extra corner volume < 0.02 mm³ order); r4's secondary cutter starts 9 mm along the
line, R3 starts at the bore entry and relies on `targets` to spare the venturi body.

## 6. Limits / next

* Fixture texts are hand-placed (r1's glyph OCR missing); boxes come from the glyph clusters, so positions are exact.
* The current S6 builds no revolve for these two sections (r2), so the end-to-end pipeline still selects
  ExtrudeSingleView; the S7 hook fires as soon as a revolve candidate exists.
* Only axis Z; section-arrow direction not used (assumes "viewed from downstream" / front face as drawn).

## 7. Integration (16.11.2026)
* The cuts are applied to R2's multi-body revolve candidate (`synth/RevolveMulti.cpp` calls `applyTransverseCuts`
  after the revolves; Pappus / HLR profile checks use the uncut revolve, volume / bbox the cut model, drawing->model
  re-projection the cut model). The S7 hook in `Synthesize.cpp` still serves the generic single-profile revolve.
* Face slot that meets a groove of the same view (atomizer VIEW B): the slot now ends **inside the groove** — centreline
  end at the groove mid-depth radius, `LS_slt = sqrt((RO_slt-H_grv/2)^2-OFS_slt^2)` = 1.8 (R4 reading); grooves are
  processed first. `R3CutFeatures.AtomizerSlotsAndGrooves` updated (slot volume 3·0.5·1.8·0.5 = 1.35 mm³).
