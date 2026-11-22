READY v3
# pc_mfg — status (Agent C)

Implements `pc/MfgChecks.h` (interface unchanged). Files:
`MfgChecks.cpp` (toString/Report, presets, drill table, checkParameters, validateChange, merge/dedupe),
`Suggest.cpp` (driver detection + suggested values, public solveParamFor), `AnalyticModel.cpp` (analytic
CSG model), `AnalyticChecks.cpp` (analytic rules), `GeometricChecks.cpp` (B-Rep rules), `MfgInternal.h`
(internal, not a contract). Tests: `tests/test_mfg.cpp` (31 tests, all passing with the real sketch/model).

Timing on samples/bracket.pcad: checkAnalytic ≈ 0.6 ms (no issues), ≈ 6 ms with 8 issues incl. suggestions;
checkGeometric ≈ 110–280 ms.

## Pipeline (validateChange)
1. `params.evaluate(overrides)`: failure -> PARAM.EVAL errors, stop (`regenerated=false`).
2. `checkParameters`: any Error -> stop before regeneration.
3. `checkAnalytic` (no B-Rep). 4. `model::regenerate`. 5. `checkGeometric`, merged into the analytic issues
   (same ruleId + featureId -> one issue: the analytic params/suggestion are kept, geometric faces/edges are
   added, the higher severity wins). Issues are sorted Error > Warning > Info.

## How analytic checks work
Sketches are solved on copies (`sketch::solve`, stored coordinates are used if that fails ->
ANALYTIC.SKETCH_UNSOLVED Info). They are placed in 3D (standard planes; OnFace frames resolved analytically
from `<Extrude>.end/start/side.e<id>` and `<Hole>.h<k>.bottom/cb_floor` per the SketchData.h rule).
Extrudes become prisms, every hole location becomes a cylinder + cb/cs + drill-tip cone (depth is to the shoulder,
same as pc_model), and pattern instances are copies. A material classifier replays the history.
**Slab analysis**: for each family of parallel prisms and holes the part is cut into axial slabs. In each
slab, boundary curves are sampled and 2D rays are cast inward (walls, ligaments, boss walls, hole-to-hole) and
outward (gaps, slots). Concave vertices give internal vertical corners and concave arcs give milled radii. This
works for any sketch and feature; nothing is specific to the sample.
**Suggestions**: candidates are the parameters referenced by the involved expressions (feature props and
sketch dimensions). EXPOSED parameters come first, including exposed parameters reached through derived ones
(Lbore = Hb+T-4 -> Hb, T). Each candidate is probed: values are re-evaluated so dependents follow, and the model
is rebuilt so sketches re-solve. Caller overrides stay fixed. Insensitive candidates are dropped. The first
driver is solved (secant, then bracketing + bisection). Its value must be in the parameter's [min,max]
(otherwise the next driver is tried). The value is rounded toward the admissible side on a 0.1 / 0.05 / 0.01
grid (0.5° for angles, integers for counts) and checked against the metric. `params` = [chosen, other exposed
drivers, derived drivers]. The message says "P ≥ v" or "P ≤ v", depending on which side of the current value
the fix lies. CNC.STD_DRILL uses "=".

## Rule ids  (formula; severity)
Parameter domain
* PARAM.EVAL — parameter cannot be evaluated / missing / NaN; Error.
* PARAM.RANGE — v < min or v > max; Error; suggestion = clamped value.
* PARAM.COUNT — Count parameter not an integer; Error; suggestion = rounded value (clamped).
* PARAM.FEATURE_EXPR / PARAM.SKETCH_EXPR — a feature property / sketch dimension expression fails; Error.

Model domain
* MODEL.FEATURE_FAILED — model.status[i].ok == false; Error; params = the feature's referenced parameters.
* MODEL.MULTI_BODY — more than one body. Info ("N gövde") when every extra body is intentional: its creating feature has
  op=new / keepTool=1 / bodyName, or is a Combine with keepTools=1. Warning when a body name contains ":k" (split), a body was
  created without such a flag (e.g. an auto-join that touched nothing) or a body holds > 1 solid.  MODEL.REGENERATE — regenerate threw; Error.
* HOLE.TIP_BREAKOUT — blind hole: the material under the tip apex (along the axis) is < 0, i.e. the drill tip pierces the
  opposite face; Warning; suggestion = depth driver so that the apex keeps max(AM, CNC) minWall.
* ANALYTIC.SKETCH_UNSOLVED (Info), GEOM.CHECK_FAILED (Warning, OCCT exception during geometric checks).

Additive (AM; off when am.enabled=false or process None)
* AM.MIN_WALL — Error when t < am.minWallThickness, where t is any of: in-plane wall/ligament (slab rays,
  exact pair distance); axial thickness of join prisms (plates); pocket floor; material under a blind-hole tip;
  Shell thickness; B-Rep ray thickness (sampled points, inward normal, first non-adjacent facing exit).
* AM.MIN_HOLE — Ø < am.minHoleDiameter; Error.
* AM.HORIZONTAL_HOLE — hole/cylindrical cut whose axis angle to the build plate is < minOverhangAngle and
  Ø > maxSelfSupportingHoleDia; Warning (support or teardrop). Not for SLS.
* AM.ASPECT_RATIO — wall or pin height/thickness > am.maxAspectRatio. Height is the axial overlap of the two
  bounding features; a pin uses its own Ø. Warning; height parameter first.
* AM.MIN_GAP — gap between walls (outward slab rays) < am.minGap; Warning.
* AM.MIN_FEATURE — fillet radius / chamfer distance < am.minFeatureSize; Warning.
* AM.BUILD_ENVELOPE — extents in the build frame (z = buildDir) > buildX/Y/Z (XY swap allowed); Error.
* AM.OVERHANG — down-facing exposed prism caps/sides (analytic) or B-Rep face samples with
  angle-to-plate = acos(-n·b) < minOverhangAngle, above z_min; Warning (Info for short bridges ≤ process bridge
  limit). Not for SLS.
* AM.TRAPPED_POWDER — closed Shell (no removed faces), fully enclosed cut, or a solid with > 1 shell; Error;
  LPBF/EBM/SLS/SLA only.
* AM.BLIND_HOLE — blind hole Ø < minEscapeHoleDia (Warning) or depth/Ø > 4 (Info); powder/resin processes.

Conventional (CNC; off when conv.enabled=false or process None)
* CNC.MIN_WALL — same quantities as AM.MIN_WALL vs conv.minWallThickness; Error.
* CNC.WALL_RATIO — wall height/thickness > conv.maxWallHeightToThickness; Warning.
* CNC.HOLE_LD — L/Ø > maxHoleDepthToDia. L = blind depth, or for through holes the material length along
  the axis; geometric check uses the hole wall length. Error; suggestion from (depth)/(diameter).
* CNC.MIN_HOLE — Ø < conv.minHoleDiameter; Error.
* CNC.STD_DRILL — preferStandardDrills and Ø ≤ 50 not in standardDrillSizes(); Info; suggestion = nearestStandardDrill.
* CNC.FILLET_TOOL_RADIUS — Fillet on a CONCAVE edge parallel to the tool axis (5-axis: wall-wall edges) with
  R < minToolDiameter/2; Error. Analytic edge convexity comes from the material fraction around the edge;
  geometric: concave cylinders of Fillet features. Convex fillets are never flagged.
* CNC.CORNER_RADIUS — concave sketch arc / milled round pocket (cut circle) parallel to the tool axis with
  R < minToolDiameter/2; Error.
* CNC.SHARP_INTERNAL_CORNER — concave vertical corner (material angle > 180°, non-tangent) not covered by a
  Fillet; B-Rep: sharp concave line edges parallel to the tool axis; Error; edges listed.
* CNC.SLOT_WIDTH — gap/slot width < minToolDiameter (milling, axis ∥ tool or 5-axis); Error.
* CNC.POCKET_DEPTH — cut depth > maxToolLengthToDia × D. D = max(minToolDiameter, min(pocket width, 2 × min
  concave corner/fillet radius)); Warning.
* CNC.STOCK_ENVELOPE — extents in the tool frame > stockX/Y/Z; turning: 2·Rmax > stockX (bar Ø) and
  length > stockZ; Error.
* CNC.ACCESS — 3-axis: hole/cut axis not parallel to the tool axis (Warning) or drilled from below (Info, 2nd setup).
* CNC.UNDERCUT — analytic: a cut blocked by material on both sides along the tool axis (Error). B-Rep: ≥ 1/3 of
  a face's samples are reachable from neither +k nor −k, because the normal faces away or the ray is blocked
  (Warning; 3-axis only).
* CNC.ENCLOSED_VOID — closed shell / enclosed cut / inner shell; Error.
* CNC.TURN_AXISYMMETRY — turning: non-axisymmetric prism (Warning); off-axis hole (Info).

## Presets
amPreset: LPBF (0.4 wall, Ø8 self-supporting, 45°, escape Ø2, 250×250×300), EBM (0.6, escape Ø3, 200×200×380),
DED (2.0 wall, 60°, no powder rules), FDM (0.8 wall, Ø2 holes, 10 mm bridges), SLA (0.5, 30°, drain Ø3.5),
SLS (0.7, no overhang/support rules, escape Ø3.5). convPreset: 3-axis (Ø3 tool, L/D 4, drill 10, wall 0.8),
5-axis (L/D 6, 500×500×400), turning (bar Ø100×300). The rationale is in comments in MfgChecks.cpp.

## Limits
* Analytic model: draft angles, fillets/chamfers (as material) and shells are not in the material classifier.
  Revolves only contribute to extents. Wall/gap rays only use boundaries of prisms parallel to the slab axis
  (non-parallel joins can hide walls; the geometric checks cover those).
* Pattern instances that follow geometry (B's limit) are treated as rigid copies.
* B-Rep thickness sampling uses a 5×5 / 6×6 UV grid per face, so very local thin spots can be missed (the analytic
  pass finds ligaments exactly).
* Public `solveParamFor` returns the exact root (equality), because the frozen signature carries no side. Side-aware,
  rounded values are produced internally for Issue::suggestedValue.

## v2 — multi-body (Nov 2026)
**Per-body geometry.** `checkGeometric` runs every B-Rep rule (wall thickness, overhang, horizontal holes, enclosed voids,
CNC corners/fillet radii/undercut/hole L/D, envelope) separately on each body (`model.bodies`; when empty it falls back to the
solids of `model.shape`, named "Body1..n", a single solid is named like its first body-creating feature). Rays and the
undercut blocking test only see the body itself (separate setups); the build plate for overhang is each body's own z_min.
Issue messages start with "Gövde <name>: " when the part has > 1 body; `Issue::body` is filled for every geometric issue
(per-body run). Whole-build envelope (all bodies together) is a second AM.BUILD_ENVELOPE/CNC.STOCK_ENVELOPE pass with body ""
and message "Tüm yapı: ...". `dedupe`/`mergeInto` keys include the body.
**Analytic.** A body simulation of the BODIES contract (`AModel::bodyNames`, `featureBody`) tags every Extrude/Hole/Pattern op
with its target bodies (op=new -> bodyName|fid; join/cut/intersect -> `targets`, empty = auto/all). `AModel::material` is the
union of per-body replays, so a cut with `targets=A` never removes material from B. `Issue::body` = body of the issue's feature
(Fillet/Chamfer/Shell: body of the first ref; auto ops: the body if the part has exactly one so far, else ""). Plane features are
ignored (no material); Combine and keepTool bodies are NOT modelled analytically (limit; the B-Rep rules cover them).
**New rules**
* MODEL.BODY_INTERFERENCE — Error: V = volume(BRepAlgoAPI_Common(A, B)) > max(1e-3 mm^3, 1e-6 * min(V_A, V_B)). featureId = creator of
  the later body B, params = its driving parameters (distance/offset/diameter first), body = B, actual = V.
* AM.BODY_GAP — Warning: d = BRepExtrema_DistShapeShape(A, B) < am.minGap (powder/resin processes only: LPBF, EBM, SLS, SLA);
  not reported for overlapping bodies. d = 0 (touching, e.g. kept cut tool in its pocket) is reported too. faces = closest faces.
* CNC.SEPARATE_PARTS — Info when conv enabled and > 1 body: each body is its own part/setup.
**Tests**: `tests/test_mfg_v2.cpp` (hand-built ModelResults + analytic docs with Plane/Combine/targets; sample test skips until
samples/v2_multibody.pcad exists). Analytic with Plane+Combine+targets: well below 20 ms.
**Review fix**: MODEL.MULTI_BODY no longer calls "<name>:k" bodies a split when they are the pieces of an intentional
feature's own multi-solid tool (Pattern op=new, keepTool of several tool solids); a later split stays a Warning.

## v3 — analytic phase 2: datum planes, kept tools, Combine (Nov 2026)
**Pipeline.** Documents with OnPlane sketches (or OnFace sketches on Combine faces) run the analytic rules AFTER
`model::regenerate` ("phase 2", `AnalyticPhase2.cpp`): validateChange installs a thread-local `FrameProvider` and runs the
complete analytic rule set once, so features on datum planes get exactly the same parameter-driven issues and
suggestions as features on standard planes (messages identical, phase 1 is not run). Other documents are unchanged
(phase 1 before regeneration). If regeneration throws, phase 1 is the fallback.
* Frames: OnPlane sketch = `ModelResult::datumPlanes[planeRef].frame` translated by the sketch offset along its normal
  (`pc::model::sketchFrame`, same rule). OnFace sketches on faces the analytic model does not know: frame from the model
  regenerated up to the feature before the sketch.
* Suggestion probes (other parameter values): if no parameter referenced by the features up to the plane changed, the
  regenerated frame is used; otherwise the plane is re-evaluated ANALYTICALLY (pc/Feature.h rules for edgeNormal /
  edgeAngle / twoEdges / faceOffset, rotate/flip/offset) from the analytic faces: edge = intersection line of its two named
  planar faces, end points from the prism (cap+side, side+side). B-Rep unknowns (curve direction, normal side, edge
  parameter of the origin incl. fillet trims) are calibrated once against the exact datum frame; the analytic plane is
  only used if it reproduces that frame (sample: identical to regeneration for W, D, T, R, Atab changes). Otherwise the
  model is regenerated up to the plane (cached per value set). Sample: 0 regenerations during probes.
* keepTool=1 (Extrude cut/intersect): the kept tool is a new-body prism "<fid>.tool" (faces "<fid>.tool.<rest>", body
  `bodyName`/"<fid>.tool"); its issues are reported on featureId <fid>, body = the kept body.
* Combine (join/cut/intersect, keepTools) is replayed by the per-body material classifier; ops act on a body only from
  its creation on (`AModel::bodyBorn`).
* Multi-body overhang: each body sits on its own build plate (lowest point of the body), as in the geometric pass.
* Body of an issue the analytic body simulation cannot decide: `ModelResult::bodyOfFace` of its first face.
* Merge (phase 2): `mergeInto`, then a remaining geometric issue of the same rule and body is absorbed into the
  analytic issue if it highlights one of its faces or comes from a Fillet/Chamfer on an edge of such a face (thin rib
  measured on its root fillets) — the analytic issue keeps params + suggestedValue.
**New analytic rule input: drilled circular cuts.** An Extrude cut whose profile is one full circle and that has no
floor (through the material) is a drilled hole: AM.MIN_HOLE, CNC.MIN_HOLE, CNC.STD_DRILL (suggestion from the circle's
Diameter/Radius dimension, e.g. Dp = 7.5) and CNC.HOLE_LD (L = material length along the axis next to the circle).
**checkAnalytic(doc, values, settings)** alone (no model) keeps the v2 behaviour: fast (≈ 1 ms on the sample), sketches on
datum planes are not placed and their features (and kept tools/Combine built on them) are skipped; only
validateChange (designer, runner, AppLint) runs phase 2.
**Timing** (samples/v2_multibody.pcad, validateChange incl. regenerate + geometric): ≈ 0.35–0.6 s at defaults, tr=0.3,
Ttab=0.5 or Dp=7.3 (machine shared with other builds). Defaults: 0 errors; warnings = 2× AM.OVERHANG (30° tab, plug),
2× AM.BODY_GAP (touching bodies) + CNC.ACCESS for the plug hole (axis 30° to the 3-axis tool axis; new, analytic).
**Tests**: `tests/test_mfg_v3.cpp` (rib tr → AM/CNC.MIN_WALL params[0]=tr + suggestion ≥ limit, applying it clears the
error; Ttab → Tab and kept plug "Tapa"; Dp → CNC.STD_DRILL = 7.5; plug L/D; defaults 0 errors and < 1.5 s; checkAnalytic
alone skips datum features).
**Limits**: Hole keepTool (kept drill cylinder) and kept through-all tools are not modelled analytically (B-Rep rules
cover them). Datum planes on curved edges/faces fall back to partial regeneration in probes. Analytic overhang of
prism caps carries no angle suggestion (e.g. Atab); the geometric parameters are attached.

## Enjektör extensions (Nov 2026, samples/enjektor_09DB01*.pcad, tests/test_enjektor.cpp)
* **Reference bodies**: a body whose creating feature has the prop `mfg=0` (extracted fluid volume, CFD domain) is not a
  part: no per-body rule, no MODEL.BODY_INTERFERENCE / AM.BODY_GAP pair, not counted in MODEL.MULTI_BODY /
  CNC.SEPARATE_PARTS (`bodiesOf` in GeometricChecks.cpp).
* **Micro cuts in turned bodies** (`microCutRules`, AnalyticChecks.cpp): an Extrude cut whose explicit `targets` are all
  bodies created by a Revolve is invisible to the slab analysis (revolved material is not in the classifier). Its width =
  min(extrusion length, own minimum width of the profile) is checked against `conv.minToolDiameter` (CNC.SLOT_WIDTH,
  Error: slitting-saw blade / wire + spark gap / end mill) and `am.minGap` (AM.MIN_GAP, Warning). When the extrusion
  length is the width, the suggestion solves its expression (injector: "w_slot ≥ 0.20").
* **Holes in turned bodies**: a blind Hole whose explicit targets are all Revolve bodies is no longer skipped as a "hole in
  the air"; drilled length = blind depth, so CNC.MIN_HOLE / AM.MIN_HOLE / CNC.STD_DRILL / CNC.HOLE_LD apply (injector
  orifice: "Q ≥ 2.35" when the smallest drill is Ø0.40).
* Injector defaults (turning, saw 0.10 mm, drill Ø0.15, L/d ≤ 3): 0 errors, 5 CNC.TURN_AXISYMMETRY warnings (hexagon,
  slots, socket, cross hole need live tooling / milling), validateChange about 1.5 s.

## Review Nov 2026 (independent reviewer)
- `PARAM.RANGE` on a DERIVED parameter (design relation with min/max) no longer suggests "set the derived value": it solves for
  one of its input parameters (exposed first; scan + bisection in the input's range, rounded to its UI step) and reports it in
  `params[0]` / `suggestedValue`. Plain inputs keep the limit suggestion. Tests: tests/test_enjektor_dogrulama.cpp.
