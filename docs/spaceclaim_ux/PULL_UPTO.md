# Çek: Kadar (Up To) + Cetvel (Ruler), Doldur (Fill) tolerant regions, multi-region Pull — SC-PULLUPTO

Status: implemented in `/home/claude/v2` (Kasım 2026). Goal: SpaceClaim's 2D → 3D workflow. Draw or import
curves, Fill the closed loops into regions, Pull them into 3D, and use "Pull Up To" to link geometry between
sketches or views. SPEC §5 rows 16 (Kadar) and 17 (Cetvel) were phase 2. They are now done, and Kadar is
**associative**, which goes further than SPEC row 16 ("bir kez hesaplanır").

## 1. User flow (SpaceClaim terms)
| Step | What the user does | Result |
|---|---|---|
| Fill | Click any curve, region or point of a sketch, press **F**, then **Enter** | The sketch's closed loops become regions `sketch:<sid>:r<k>` and all of them are selected. Status: `Doldur: Sketch1 → 1 bölge (…), 6 boşluk kapatıldı (≤ 0.1 mm), …`. If a loop is open, the open ends get **red circles** with a "boşluk" tag and the curves that end there are emphasised. The status gives the distance between the two nearest open ends. Setting the Fill option **"Boşluk toleransı"** to a larger value and pressing Enter again stores `gapTol` on the sketch (one undo step), which closes the loop. |
| Fill on hole edges | Select the rim edge(s) of a hole (a closed loop on one planar face), press **F**, then **Enter** | If the hole comes from a Hole feature or from a cut Extrude/Revolve with a single tool solid, it is filled parametrically, as Fill on its face would do (suppress/delete). Otherwise the hole gets a **plug**: a Sketch on the face holding the loop, plus an Extrude (join, into the material, as deep as the hole walls), in one undo step. |
| Pull, several regions | Ctrl+click regions (from one or several sketches, on any plane), press **P**, drag or type a value, then **Enter** | One Extrude per sketch with `regions=<k,…>`, all in one undo step. Regions not selected stay visible and pickable. Regions already pulled are no longer drawn. |
| Kadar (Up To) | With a region, a planar face or an Extrude end face selected in Pull: click the **Kadar** guide (or press **U**), then click the target. A target is a sketch curve end point of another sketch (snaps within 14 px), a sketch curve, a model vertex (an edge clicked near its end), an edge, a face, a datum plane, or a region (= the plane of its sketch). | Ghost preview from a real candidate regeneration, a "Kadar" value box with the distance, and the status `Çek (Kadar): hedef Sketch2 noktası p5 → 40 mm (ilişkisel…)`. **Enter** applies, **Esc** drops the target, and clicking another target re-targets. Ekle / Kes / Birleştirme yok / Kesişim / İki yöne çek / eğim follow the Options panel as for ordinary pulls. |
| Cetvel (Ruler) | With something selected in Pull: click the **Cetvel** guide, click a reference (same kinds as Kadar), type the gap, then **Enter** | Stored as Up To + offset (`upToOffset = -gap`), so it is associative too. The ghost follows the typed value. |
| Pull the end face of an Up To extrude | Drag or type | Changes `upToOffset` (field "Ofset", delta). The target link is kept. |

## 2. Model
### Extrude props (additive; schema comment in `src/core/pc/Feature.h` left untouched to avoid a full rebuild; documented in `src/model/pc/UpTo.h`)
* `upTo=<ref>`: the length is the signed distance `s` of the target from the sketch plane, along the sketch normal. It is **re-evaluated at every regeneration**. The direction (`normal`/`reverse`) comes from the sign of `s`; `symmetric` gives a total of 2·(|s|+offset). `distance` keeps the value from when it was made (informative only).
* `upToOffset=<expr>` (default 0): added to |s|. Cetvel writes `-gap`.
* `regions=<k,…>` (also on Revolve): only these regions of `pc::sketch::sketchRegions()` are used.
* Sketch prop `gapTol=<mm>`: gap tolerance for that sketch's regions (default `autoGapTol` = min(0.1 mm, 0.5 % of the sketch size)).

`<ref>` grammar: `sketch:<sid>:p<id>` (point) · `sketch:<sid>:e<id>` (curve: its point farthest from the plane) ·
`sketch:<sid>:plane` / `sketch:<sid>:r<k>` (sketch plane, must be parallel) · `datum:<planeId>` (must be parallel) ·
`vtx:<edgeName>:<0|1>` (model vertex) · `edge(…)` (farthest point) · `<faceName>` (a parallel planar face uses its plane; any other face uses its farthest point, with a warning).
Targets must be regenerated before the Extrude, so a sketch after it fails with a clear message. Names are persistent face and edge names and
sketch entity/point ids, so `docs/named_selections/MODEL.md` naming is unchanged. Extrude faces keep
`<fid>.start/.end/.side.e<entityId>`. Region pieces keep the entity id of their source curve, and a split curve gives
`side.e<id>:k` per the existing split rule.

### Tolerant regions (`src/sketch/pc/Regions.h`)
`arrangeRegions()` builds the planar arrangement of the non-construction curves in seven steps:
1. End points closer than `gapTol` are merged. Two line ends are rebuilt at the intersection of their carrier lines, so axis-parallel imported outlines stay exact; anything else goes to the group mean, and arcs are refit through the moved ends.
2. An end point within `gapTol` of the inside of another curve splits that curve (T-junction).
3. Proper crossings split both curves.
4. Duplicate (overlapping) pieces are dropped.
5. Dangling chains are pruned and their ends reported as `openPoints`.
6. Half-edge face tracing turns every bounded minimal face into a region.
7. A connected component inside a region's face becomes a hole of the smallest such face. Nested loops give a region with a hole plus an island region; disjoint loops give separate regions.

Region order: outer-boundary area descending, then centroid.

`sketchRegions()` is the single region list used by the designer and by `regions=`. When the classic `buildProfile()`
is clean and has as many faces as the arrangement, it **returns the classic faces** (`legacy = true`), so existing
documents and harnesses keep their regions. Otherwise it returns the arrangement. Pull writes **no** `regions` prop when a
legacy sketch is pulled whole, so the document is byte-identical to before.

## 3. Tests
* `tests/test_pull_upto.cpp` (gtest, auto-globbed, links `pc_model`; 11 tests):
  * Up To a vertex of another sketch (depth 40).
  * Associative update when the target sketch's parameter W changes (40 → 55), and when the dimension expression itself changes (→ 25).
  * Curve target + offset (Cetvel).
  * Face (`Extrude1.start`, `.end`, which follows when Extrude1 changes), datum plane and sketch plane targets.
  * Model vertex `vtx:`, the `upToDistance` API, the non-parallel error and an unknown target.
  * Reverse direction from the sign.
  * Gaps of 0.01, 0.02 and 0.05 mm closed (classic profile rejects them); 0.5 mm reported, then closed with tolerance 0.6.
  * Overshoots of 0.03 and 0.5 mm (stubs pruned), T-junctions, a collinear overlap, two crossing rectangles → 3 regions.
  * Plate with a gappy outline + a hole + an island + a disjoint square → 4 regions with the right holes and areas.
  * Extrude `regions=` with entity-named sides; a bad index fails; `gapTol` honoured.
  * Arcs with gaps (slot on XZ) → valid solid.
* `tests/gui/gui_harness_pullupto.cpp` (`gui_harness_pullupto`, real MainWindow, real clicks and keys; 57 checks):
  1. Click a curve of the gappy front profile on XZ, press **F**, **Enter**: region r0 is selected.
  2. **P**, click the **Kadar** guide button, click the corner of the top outline on XY: the target is `sketch:Sketch2:p<corner>`, 40 mm, ghost shown, nothing committed yet.
  3. **Enter**: one undo step, solid y −40..0.
  4. Set W = 55 in the Gruplar cell: the solid follows (−55).
  5. Fill on an open loop: 2 red gap markers; tolerance 0.6 closes it (stored `gapTol`).
  6. Cetvel 5 mm under the solid's top face: z 0..25, `upTo` + `upToOffset=-5`.
  7. Ctrl+click 2 of 3 squares on YZ, **10**, **Enter**: `regions=0,2`, +2000 mm³; the third square stays a region; Ctrl+Z.
  8. Fill on the rim edge of one of two holes cut by one Extrude: a plug patches exactly that hole (+80π mm³).

## 4. Files changed / created (for the merge)
| File | Change |
|---|---|
| `src/sketch/pc/Regions.h`, `src/sketch/Regions.cpp` | **new**: tolerant arrangement regions |
| `src/sketch/CMakeLists.txt` | `Regions.cpp` added to `pc_sketch` |
| `src/model/pc/UpTo.h`, `src/model/UpTo.cpp` | **new**: Up To references (`upToSignedDistance`, `upToDistance`, `upToLabel`) |
| `src/model/CMakeLists.txt` | `UpTo.cpp` added to `pc_model` |
| `src/model/Regen.cpp` | `regionProfile()` (`regions=` for Extrude and Revolve), `upToNow()`; `doExtrude` uses `upTo` / `upToOffset` |
| `src/designer/tools/SketchRegions.h/.cpp` | `sketchRegionInfo()`, `consumedRegions()`; `sketchRegionFaces` → `sketchRegions` with `gapTol`; `sketchConsumed` ignores partial (`regions=`) pulls; pulled regions not displayed |
| `src/designer/tools/PullMapping.h/.cpp` | `Kind::UpToOffset`, `Target::regionSets`, `Result::moreFeatures`, multi-region / multi-sketch NewExtrude, `regions` on Revolve, `regionsProp`, `supportsUpTo`, `upToPreview`, `applyUpTo`; `faceSketch(…, frameOut, onlyEdges)` |
| `src/designer/tools/PullTool.h/.cpp` | Kadar / Cetvel guides enabled, U key, target pick + snapping (`resolveUpToRef`), candidate-regeneration ghost, value box, `commitUpTo`, shared `commitResult` (multi-feature one-undo), region ghost / emphasis for the selected regions only |
| `src/designer/tools/FillTool.h/.cpp` | Fill on sketch items (regions, gap markers overlay, tolerance option → `gapTol`), Fill on hole edges (parametric or plug) |
| `src/designer/MainWindow.cpp` | shared hook, 1 line (below) |
| `src/designer/Shortcuts.cpp` | 1 line: text of the U key outside Pull |
| `tests/test_pull_upto.cpp` | **new** (gtest, picked up by the `test_*.cpp` glob, no CMake edit) |
| `tests/gui/gui_harness_pullupto.cpp` | **new** |
| `tests/gui/CMakeLists.txt` | shared hook, 2 lines appended (below) |
| `docs/spaceclaim_ux/PULL_UPTO.md` | this file |

### Shared-hook diffs
`src/designer/MainWindow.cpp` (`MainWindow::sketchShownIn3D`):
```diff
-    if (!f.suppressed && f.id != id && f.prop("sketch") == id) return false;
+    if (!f.suppressed && f.id != id && f.prop("sketch") == id && f.prop("regions").empty()) return false;   // SC-PULLUPTO: partial region pulls keep the sketch shown
```
`src/designer/Shortcuts.cpp` (`installUnsupportedKeys`):
```diff
-      {"U", "Kadar (Up To, U) kılavuzu henüz yok (2. faz)."},
+      {"U", "Kadar (Up To, U): Çek (P) aracında, çekilecek bölge / yüz seçiliyken kullanılır."},   // SC-PULLUPTO
```
`tests/gui/CMakeLists.txt` (appended at the end):
```diff
+# SC-PULLUPTO: Fill (tolerant loops) -> Pull Up To a vertex of another sketch -> associative update, Ruler, multi-region.
+pc_add_gui_harness(gui_harness_pullupto SOURCES gui_harness_pullupto.cpp TIMEOUT 300)
```
Ribbon: no change. The guide buttons come from `PullTool::guides()`, where `upto` and `ruler` are now enabled.

## 5. Notes / limits
* A non-parallel planar or curved face target is approximated by its farthest point (warning in the feature status). SpaceClaim would make the end face follow the target face.
* Region indices in `regions=` follow the region order. A topology change in the pulled sketch (adding or removing loops) can renumber the regions. Dimension changes keep the order unless region areas swap rank.
* SpaceClaim's ruler is not associative. Here Cetvel is stored as Up To + offset, so it follows the reference.
