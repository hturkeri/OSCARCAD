# Pull on faces — SpaceClaim semantics (swirl-cup video fix)

## Bug
Swirl cup (`samples/ref_swc_rr_001.pcad`: Revolve 360° + slot cuts). Selecting the large planar END face
(`Cup_Don.rev.e58`, perpendicular to the axis) and pulling the arrow showed **"Açı"** and dragged the Revolve angle
(6°, 8°, ... 13°) -> a 13° sector of the part. Cause: `PullMapping.cpp` mapped every Revolve face (`start`, `end`,
`rev.*`) to `Kind::RevolveAngle`. Second cause found while testing: during a drag the value box kept the field labels
of an EARLIER target (e.g. "Açı" after a revolve) — `PullTool::viewerMouseMove` only built the fields when the box
had none.

## Behaviour now
| Selected face | Pull does |
|---|---|
| `start` / `end` cap of a **partial** revolve (angle < 360) | revolve angle ("Açı", unchanged) |
| planar revolve profile face `rev.e<k>` (360° or partial) | **extrude the face** (below) |
| cylindrical revolve face `rev.e<k>` (line parallel to a U/V axis, radial position = DistanceX/DistanceY from the sketch origin to both end points) | **radius offset** ("Mesafe", delta): those dimensions get `expr + d` (folded: `D/2 + 1` -> `D/2 + 2`; plain numbers rewritten; one plain-number parameter behind all of them -> the parameter changes). Outward pull grows an outer wall, shrinks a bore. |
| conical / toroidal revolve face, axis = sketch line, undimensioned radius | refused with a clear message ("... döndürme açısı değiştirilmez; profil sketch'ini düzenleyin") — nothing changes |
| any other **planar** face without a direct mapping: faces of cuts, patterns, shells, combines, plain extrude sides, imported / drawing-import bodies, unknown names | **extrude the face** |
| existing mappings | unchanged: Extrude `end` -> distance, `start` -> base offset, side with Ø/R dimension -> dimension, Fillet -> radius, Chamfer -> distance, Hole wall / bottom / counterbore -> diameter / depth, sketch regions -> new Extrude / Revolve, edges -> Fillet / Chamfer |
| non-planar face without mapping | refused (CR-2 message), never reinterpreted |

**Extrude the face** (`Kind::FaceExtrude`, field "Mesafe", mm, typed or dragged, parameter / expression allowed like
other pulls, Ctrl+G offers `Extrude<n>_distance`): one undo step inserting
1. `Sketch<n>` — `OnFace` on the pulled face (offset 0); geometry = the face's outer + inner loops as lines, arcs,
   circles (shared end points); spline / ellipse edges -> refusal message;
2. `Extrude<n>` — `sketch=Sketch<n>`, `distance=|v|`, direction along the face's **outward** normal (v > 0) or into
   the material (v < 0), `pullFace=<face>`; op from the Pull options panel: Otomatik (v>0 Add, v<0 Cut), Ekle -> join,
   Kes -> cut (into the material also for v>0), Birleştirme yok -> new body (`bodyName` option), Kesişim -> intersect;
   join / cut target = the body owning the face (`targets`), "İki yöne çek" -> symmetric, draft option honoured.
The new `Extrude<n>.end` stays selected: pulling it again edits that Extrude's distance (no feature pile-up).
Under a rollback bar both features are inserted at the bar (two `insertFeature` steps).

Note (model, not changed): `frameFromFace` takes the plane's axis as the outward normal; for INDIRECT planes (planar
faces of a Revolve) that axis points into the material. `pull::faceSketch(..., &normalInward)` detects it and the
Extrude direction is chosen against the true outward normal. OnFace sketches made by hand on such faces still get
the inverted frame normal (pre-existing; fixing it in the model would flip existing documents).

## Named selections (docs/named_selections/MODEL.md §3)
`Extrude.pullFace` is a naming hint: when join / cut consumes the pulled face, the new `<fid>.end` inherits the
pulled face's whole lineage (its `removedBy` entry is dropped). A selection on the pulled face resolves to the new
outer face (Add) or the new bottom (Cut); with "Birleştirme yok" the original face is untouched and keeps it.
Persistent names stay per the contract (new faces are named by the Extrude).

## Tests
* `tests/test_pull_face.cpp` (gtest, built with the GUI, links `pc_designer`; 10 tests): end face -> FaceExtrude
  "Mesafe" (not RevolveAngle), face sketch = 2 circles on the face; +5 Otomatik / Ekle -> 1 body, volume + 5·A,
  angle 360, lineage; "Birleştirme yok" -> 2 bodies; -2 and Kes 2 -> volume - 2·A; named selection follows (Add,
  Cut, new body); partial revolve caps still "Açı"; outer cylinder +1 -> `D_outer/2 + 1` (radius 26, folds to
  `+ 2`), bore +1 -> radius 4; cone refused; planar slot / pattern faces extrude; bracket mappings unchanged;
  rollback insertion.
* `tests/gui/gui_harness_sc_e2e.cpp` `swirlCupPull()` (real MainWindow, real clicks): open the sample, bottom view,
  click the end face, P -> target FaceExtrude, label "Mesafe", prompt without "Açı"; drag the arrow outward (value
  box label "Mesafe", ghost, angle untouched); 5 Enter -> Sketch + Extrude join, volume + 5·A, one body, one undo
  step, the new end face selected; 2 Enter -> same Extrude distance 7; undo; -2 Enter cuts; "Birleştirme yok" ->
  2 bodies; cone -> message, nothing changes.
* `tests/gui/gui_harness_sc_tools.cpp`: the plain planar side face `Extrude1.side.e5` is now FaceExtrude (was the CR-2
  refusal).

## Files changed / created (for the merge)
| File | Change |
|---|---|
| `src/designer/tools/PullMapping.h` | `Kind::FaceExtrude`, `Kind::RevolveRadius`; `Target::body/dims/sign`; `Result::hasPreFeature/preFeature`; `withNewFeatures()`, `faceSketch()` |
| `src/designer/tools/PullMapping.cpp` | revolve mapping (angle only for partial caps), planar fallback, radius offset, face sketch, apply cases |
| `src/designer/tools/PullTool.h` | `boxLabel_` |
| `src/designer/tools/PullTool.cpp` | FaceExtrude/RevolveRadius prompt, ghost, box, flip, commit (Sketch+Extrude as one undo step), Ctrl+G id, arrow placed ON ring faces, value-box fields rebuilt per target while dragging |
| `src/model/Regen.cpp` | `doExtrude`: `pullFace` lineage inheritance |
| `docs/named_selections/MODEL.md` | §3 note on Pull of a face |
| `tests/test_pull_face.cpp` | new |
| `tests/CMakeLists.txt` | exclude `test_pull_face.cpp` from the plain glob; build it with the GUI (links `pc_designer`) |
| `tests/gui/gui_harness_sc_e2e.cpp` | `swirlCupPull()` + includes |
| `tests/gui/gui_harness_sc_tools.cpp` | side-face expectation |
| `docs/spaceclaim_ux/PULL_FACE_FIX.md` | this file |

`pullFace` is an optional Extrude prop (schema comment in `src/core/pc/Feature.h` not edited, to avoid a full
rebuild / merge conflict; documented here and in MODEL.md).

## ctest in this copy (Release, xvfb)
335/337 pass, incl. all 10 `PullFace.*`, `gui_harness_sc_e2e` (138 checks), `gui_harness_sc_tools`,
`gui_harness_visibility`, named-selection and model suites. The 2 failures are outside Pull (neither harness touches the Pull tool;
not verified by reverting): `gui_harness_v2` segfaults in `QTreeWidgetItem::setCheckState` (Gövdeler panel
visibility box) and `gui_harness_sc_shell` "Yapı tree check box follows" — both in the hide/show code
(`StructurePanel.cpp` / `MainWindowVis.cpp` were mid-edit when this copy was taken).
