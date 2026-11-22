# WP-3 notes — interpretation, sketch, 3D, pipeline, wizard (phase 1)

Status 16.11.2026: S3–S7, pipeline, edit replay, merge, CLI and the 7-step wizard work end-to-end on the five WP-3
fixtures. `ctest` (whole project, 215 tests) passes. No frozen contract file was changed.

## 1. Owned files (exactly these; merge copies them as-is)

| Path | Content |
|---|---|
| `src/drawing/interp/InterpUtil.{h,cpp}` | private helpers: geometry, text fold, fallback dim-text parser (used while WP-2's `parseDimText` is a stub), lenient pcdrw/1 reader (used while WP-1's `loadIR` is a stub) |
| `src/drawing/interp/Arrangement.{h,cpp}` | planar arrangement (split, snap, half-edge faces) for closed profiles |
| `src/drawing/interp/Layout.cpp` | S3 `analyzeLayout`: border, title block + fields, scale (title block vs robust dimension median), projection symbol, view clustering/labels |
| `src/drawing/interp/Annotate.cpp` | S4 `annotate`: line roles (layer/linetype, width classes, dash, context), dimension assembly (text → dim line → arrows → extension lines → geometry), stacked tolerances, tolerance-midpoint consistency band, driving/reference |
| `src/drawing/interp/Views.cpp` | S5 `buildViews`: alignment relations, first/third-angle roles, primary view, axis + mirror score, rotational verdict, `sheetToView` |
| `src/drawing/interp/SketchBuild.cpp` | S6 `reconstructSketches`: half profile / extrude profile / hole pattern, constraints chosen by DOF test, dims → named parameters (D1.., L1.., L_top, W/H/T, Dh1/n_h1, D_bc1, R1, A1, C1), `D/2` expressions, redundant chain total → `[T]` reference, DOF closing with `[Ö]` measured params, deviation check |
| `src/drawing/interp/Edits.cpp` | `applyUserEdits` — whole `UserEdit.h` catalogue; stale refs → `edit.staleRef` warning |
| `src/drawing/interp/Pipeline.cpp` | `ingest` (sniff dispatch; `*.pcdrw.json` input), `runStage`, `runPipeline` (raster defaults/strict) |
| `src/drawing/interp/Merge.cpp` | `mergeIntoDocument` (param/feature-id clash → `_2`, expression and reference renaming) |
| `src/drawing/synth/Synthesize.cpp` | S7: Revolve / SectionRevolve (+ hole groups as cut features), ExtrudeTwoView (+ through/blind Hole, circular/linear Pattern), SketchOnly always last; `pc::model::regenerate` + HLR re-projection error + volume; scoring |
| `src/drawing/cli/main.cpp` | `pc_drawing_cli <in> --out part.pcad --ir ir.pcdrw.json [--from/--to Stage] [--strategy] [--tolmid] [--strict] [--no-reproject]` |
| `src/drawing_ui/wizard/ImportWizard.{h,cpp}` | QWizard (Turkish, modal, 7 pages: Kaynak, Sayfa düzeni, Geometri, Ölçüler, Sketch, 3B, Özet), `installDrawingImportAction`, `runDrawingImportWizard`; uses only `OverlayView.h` of WP-2 |
| `tests/drawing/test_pipeline_wp3.cpp`, `tests/drawing/wp3_helpers.h` | full chain on the five fixtures (views, scale, projection, dims bound/in band, DOF 0, param names/values, strategy, volume, bbox, re-projection, merge + regenerate) + targeted tests (hole feature, bolt-circle pattern parametric, redundant total, tol-mid option, third angle, stale edit) |
| `tests/drawing/fixtures/wp3/gen_fixtures.py` + `{plate4,lbracket,flange,shaft,injector_body}.pcdrw.json` | hand-made perfect-S2 fixtures (deterministic: regenerating gives byte-identical files), `wp3Expect` block = known model |
| `tests/drawing/gui/drawing_wizard_harness.cpp` | wizard GUI harness (see §4) |
| `docs/drawing_import/WP-3_NOTES.md` | this file |

No `P0Stubs.cpp` is left in `interp/`, `synth/`, `wizard/`.

## 2. The single designer hook (integration, applied by the designer owner)

Unchanged from OWNERSHIP.md §4, to be filed as `DRW: designer hook` in `docs/CONTRACT_REQUESTS.md` when the
designer work has landed:
1. `src/designer/CMakeLists.txt`: `if(TARGET pc_drawing_ui) target_link_libraries(pc_designer PUBLIC pc_drawing_ui)
   target_compile_definitions(pc_designer PUBLIC PC_HAVE_DRAWING_IMPORT=1) endif()`
2. `MainWindowShell.cpp`, one `#ifdef PC_HAVE_DRAWING_IMPORT` block: fill `DrawingImportHost` (`currentDocument`,
   `commit` = push ONE undo step with the given text "Teknik resimden içe aktar", optional `openAsNewDocument`,
   optional `editSketch` = modal SketchEditor), call `installDrawingImportAction(this, menuFile_, h)` and add the
   returned action to the ribbon (Ekle > "Teknik resim").
The wizard calls `commit` exactly once on Bitir (verified by the harness) and nothing on İptal.

## 3. Metrics (fixtures, vector-exact S2 input)

| Fixture | Strategy | Score | Volume err | Re-projection | Sketches / params |
|---|---|---|---|---|---|
| plate4 (2 views, 4x hole, 1:2) | ExtrudeTwoView + Hole | 1.00 | 0.000 % | 0.002 mm | 2 / 8 |
| lbracket (2 views) | ExtrudeTwoView | 1.00 | 0.000 % | 0.000 mm | 1 / 5 |
| flange (2 views, 6x Ø9 on Ø75) | Revolve + Hole + circular Pattern | 0.98 | 0.000 % | 0.002 mm | 2 / 8 |
| shaft (1 view, 2:1, redundant total) | Revolve | 1.00 | 0.000 % | 0.000 mm | 1 / 7 |
| injector_body (section A-A, 3:1, tolerances, R arc) | SectionRevolve | 0.81 | 1.03 % (schematic drawing; drawn at tol. mid) | 0.046 mm | 1 / 36 (≥10 `[Ö]`) |

All dims bound and in band, every sketch DOF 0, no conflicts, SketchOnly fallback always present and last.
Wizard harness: 42 checks (xvfb-run with real 3D preview, and offscreen fallback). pc_drawing_tests WP-3 part: 11 tests, ~1.3 s.

## 4. Wizard harness

`drawing.gui.drawing_wizard_harness` re-executes itself under `xvfb-run` (xcb, 1600x1000x24) when available so the
OpenGL ShapeViewer preview is exercised; otherwise (Windows, `PC_WIZARD_NO_XVFB=1`) it runs on the environment QPA
(ctest: offscreen) and the wizard shows a text placeholder instead of the viewer (QOpenGLWidget crashes on
offscreen). Uses `Qt::AA_DontUseNativeDialogs`, `tests/gui/harness_robust.h` (watchdog, modal deadlines, verified
typing/ticking). Runs: (1) plate4 via "Gözat…" file dialog, all 7 pages checked, parameter rename in the dimension
table (edit replay), Yalnız sketch toggle, Bitir → one commit, `W` clash → `W_2`, merged volume; (2) shaft,
projection ISO A + "yeniden hesapla", İptal → no commit; (3) menu action opens the modal wizard.

## 5. Known limits (phase 1) / follow-ups

* The wizard runs the pipeline synchronously (wait cursor); fixtures/DXF take < 1 s. Threaded run with
  progress/cancel (ProgressSink is already plumbed) is needed for OCR inputs — WP-3 follow-up.
* Edits of the Sketch/Synthesize stages refer to sketch/candidate ids; re-running from an EARLIER stage gives new ids,
  so such edits become `edit.staleRef` (warned, skipped). `setParamName` is name-based and survives.
* Until WP-1/WP-2 land, `ingest` of `*.pcdrw.json` falls back to the lenient reader and dim texts to the fallback
  grammar (marked `wp3.fallbackGrammar`); with the real `loadIR`/`parseDimText` the same fixtures must still pass
  (they follow the pcdrw/1 conventions of `DrawingIR.h`). Side-car `<file>.pcdrw.json` is written via `saveIR`
  (no-op while WP-1's stub is in place).
* Non-rotational details on rotational parts (radial holes, A/F flats), chamfer/fillet recognition, multi-feature
  (strategy d): phase 2, reported as warnings.

## 6. DRW requests

None needed for phase 1. Only the designer hook (§2) is pending, to be filed at integration.

## 7. Integration (16.11.2026)
* Stand-ins removed: `ingest` of `*.pcdrw.json` uses WP-1 `loadIR` only, dimension text uses WP-2 `parseDimText` only
  (`readIRLenient`, `fallbackParseDimText`, the normalize pass-through are gone). The 11 fixture tests pass unchanged.
* Designer hook applied (see `docs/CONTRACT_REQUESTS.md`), GUI harness `tests/gui/gui_harness_drawing_import.cpp`.
* Real e2e on the WP-1 corpus: `tests/drawing/test_e2e_corpus.cpp` (table in `<build>/tests/drawing/tmp/e2e_metrics.md`).
  Fixes found by it: hole projections on the axis no longer read as a bore (SketchBuild), linear Ø between two hole
  centres = pitch circle, depth dimension taken from any view, DXF DIMENSION diameters bound in revolve profiles,
  layered drawings without widths (dims on DIM/OLCU layer) keep geometry out of the dimension roles, arrows whose
  BASE sits on a shortened dimension line (CAD exports), raw-geometry sketch fallback when no profile closes,
  dimension-driven parameters `exposed` (group "Teknik resim").
