# WP-1 notes — vector PDF, DXF, normalization, IR JSON, evaluation, corpus (phase 1)

Status 16.11.2026: all WP-1 stage functions are implemented, with no P0 stubs left in WP-1 directories. Full `ctest` is
green in `build/` (232 tests, run serially) and `pc_drawing_tests` is green in a `-DPC_DRAWING_DISABLE_OPTIONAL=ON` build.

## Owned files (exact list)
Library
* `src/drawing/include/pc/drawing/GroundTruth.h` (additive only: `GtDimension::textId/tolTextIds/nativeDimId`,
  `toJson(GroundTruth)`, `saveGroundTruth`, `gtPrimIdsWithRole`)
* `src/drawing/ir/IRJson.cpp`, `ir/JsonUtil.h`
* `src/drawing/sniff/Sniff.cpp`
* `src/drawing/pdf/PdfIngest.cpp`, `PdfInterp.{h,cpp}`, `PdfText.{h,cpp}`, `pdf/nodeps/PdfUnavailable.cpp`
* `src/drawing/dxf/DxfReader.{h,cpp}`, `DxfIngest.cpp`
* `src/drawing/normalize/Normalize.cpp`, `ArcFit.cpp`, `Dashes.cpp`, `TextMerge.cpp`, `SymbolFonts.{h,cpp}`,
  `GeomUtil.h`, `NormInternal.h`
* `src/drawing/eval/GroundTruth.cpp`, `Metrics.cpp`

Tool: `tools/drawing_corpus/{Corpus.h, Parts.cpp, Sheet.cpp, Render.cpp, Dxf.cpp, main.cpp}` -> `pc_drawing_corpus`

Tests and data
* `tests/drawing/test_drawing_ir.cpp`, `test_ingest_pdf.cpp` (PdfIngest.*, Sniff.*), `test_ingest_dxf.cpp`,
  `test_normalize.cpp`, `test_eval.cpp`, `test_eval_corpus.cpp`, `wp1_helpers.h`, `wp1_pdfmini.h`, `wp1_dump.cpp`
  (the disabled developer dump tool)
* `tests/drawing/fixtures/wp1/` (ezdxf R12/R2010 fixtures and their generator script)
* `tests/drawing/corpus/` is the committed core set: 6 families x 1 case, made with `--quick --core`, 2.5 MB

## Corpus generator
`pc_drawing_corpus --out <dir> [--samples <dir>] [--quick] [--core] [--count N] [--families a,b]`
* Families: `plate` (4 holes), `lbracket`, `flange` (4 or 6 holes on a bolt circle), `shaft` (3 steps + chamfer),
  `bushing` (collar), `injector` (thread + hex A/F + head + cone + bore). Each family is an OCCT solid.
  * The geometry comes from `HLRBRep_Algo` (visible, hidden and outline edges).
  * Edges are converted to Line/Arc/Circle; edge-on circles become lines. Collinear and co-circular pieces are merged.
* Sheet: A3, first-angle placement (Front, Top below, Left to the right). The scale is chosen from 5:1 to 1:5.
  * Line styles: visible 0.5, hidden 0.25 with dashes [3 1.5], centre 0.25 with [8 2 1 2]. Centre lines are generated from the cylinder axes.
  * Dimensions are ISO linear/diameter (inside or outside), radial leaders, chamfer leaders and `4x Ø`, with ± tolerances and M threads.
  * Title block: `PARÇA`, `MALZEME`, `ÖLÇEK n:m`, `RESİM NO`.
* Outputs per case:
  * Vector PDF: `sheet.pdf` (QPdfWriter, real text, Bezier arcs), `sheet_outline.pdf` (text as outlines),
    `sheet_pscript.pdf` (flattened arcs, dashes as separate segments).
  * DXF: `sheet.dxf` (R12, layers, HIDDEN/CENTER linetypes, `DIMENSION` + `*D` blocks, `$DIMLFAC` = scale, CP1254 + `%%c/%%p/%%d`)
    and `sheet_exploded.dxf` (same content without `DIMENSION`).
  * Raster: `clean.png` (300 dpi; `--core` 150), `scan_a.jpg` (+ `scan_b/c` in the full run; skew 0.5–3°, blur, noise, stains,
    fold mark, q40–70, 200 dpi), `photo_a.jpg` (perspective ±15°, lighting gradient).
  * `gt.json`: degradation parameters go in `gt.attrs["raster.*"]`.
* `gt.json` holds the perfect S2 sheet with roles, views (box + `sheetToView`), dims (text id, bound geometry ids), scale,
  params, strategy and volume. Dimension texts carry two attrs: `corpus.dimPrims` (the ids of the dimension's lines and arrows) and
  `corpus.dxfDim` (defpoints).
* Run times: `--quick` takes 3.8 s. The full run (10 cases x 6 families = 60) takes 65 s and writes 131 MB.

## Metrics (full generated corpus, 60 cases; mean of `evaluate` after ingest + normalize)
| file | geom.recall | prim.recall | prim.precision | arc.recall | text.exact | dim.textExact | endpointRms mm |
|---|---|---|---|---|---|---|---|
| sheet.pdf | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| sheet_pscript.pdf | 1.000 | 0.941 | 0.691 | 0.758 | 1.000 | 1.000 | 0.000 |
| sheet_outline.pdf | 1.000 | 1.000 | 0.334 (glyph outlines) | 1.000 | 0 (phase 2) | 0 | 0.000 |
| sheet.dxf / sheet_exploded.dxf | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |

* The ctest gates are in `EvalCorpus.*`: per-file thresholds, run on both the committed and the generated corpus.
* Real enjektör PDF (`PdfIngest.EnjektorDimensionStringsExact`): all dimension strings are exact: 16, -0.25, 7.5±0.5, 14±0.2,
  Ø16, -0.5, 20.5 max., Ø11.8 max., R8, -0.2, 7.25, -0.1, 45°/60°/80°.
  * The PDF is read from `tests/drawing/real/` or `/mnt/user-data/uploads/`. If neither exists, the test is skipped.

## Normalize fixes made in this session (found by the corpus)
* `sameStroke` now compares dash patterns. Before, a solid extension line on top of a dashed centre line was merged into one line.
* `mergeCocircularArcs`: touching co-circular arcs are merged into one arc, or into a circle when they close. This handles
  flattened circles that a leader tip splits.
* Ligatures U+FB00..FB04 are expanded. Qt maps "fl" to U+FB02 through ToUnicode.

## Known gaps (phase 2)
* PScript emulation loses two things:
  * Dashed circles and arcs (split dash pieces along a curve) are not rebuilt.
  * Short dash-dot centre lines that overlap solid extension lines in the same style can be merged with them.
* Outline text (SHX emulation) is not recognised as text.
* The corpus has no `model.pcad`: parts are built directly in OCCT, not through `pc::model::regenerate`, so `--samples` is unused.
  It also lacks section A-A with hatch, angular dims, H7 fits, stacked `+0/-0.25` tolerances (`tolTextIds` is always empty)
  and `NativeDimension` entries in gt (`nativeDimId = -1`; the DXF `DIMENSION`s are still written).

## DRW requests
None needed. The GroundTruth additions are WP-1-owned and additive. If WP-3 wants each dimension's line and arrow ids as a
typed field and not the `corpus.dimPrims` text attr, WP-1 can add `GtDimension::annotPrimIds` (additive).
