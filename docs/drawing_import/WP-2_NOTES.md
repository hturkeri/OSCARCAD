# WP-2 notes — raster ingest, OCR, dimension grammar, QtPdf render, overlay (phase 1, 16.11.2026)

Build: `cmake -S . -B build -G Ninja` (opencv 4.6.0, tesseract 5.3.4, qtpdf=1). Full `ctest` passes (229 tests;
`drawing.RasterIngest.CorpusCleanPng` skips until WP-1's corpus is merged). The drawing objects also compile with
`-DPC_DRAWING_DISABLE_OPTIONAL=ON` (nodeps paths).

## Owned files (merge exactly these; nothing else was touched)

```
src/drawing/raster/{CvCompat,LoadRaster,Preprocess,RasterIngest,TextRegions,Vectorize}.cpp  RasterInternal.h
src/drawing/raster/nodeps/RasterUnavailable.cpp
src/drawing/ocr/{Ocr.cpp,Ocr.h,OcrCommon.h,README.txt}  src/drawing/ocr/nodeps/{OcrUnavailable.cpp,README.txt}
src/drawing/text/DimGrammar.cpp
src/drawing_qt/QtImage.cpp                          (P0Stubs.cpp deleted)
src/drawing_qt/pdf/QtPdfRender.cpp                  (P0Stubs.cpp deleted)
src/drawing_qt/pdf/nodeps/PdfRenderUnavailable.cpp  (unchanged P0 fallback)
src/drawing_ui/overlay/OverlayView.cpp              (P0Stubs.cpp deleted)
share/persistcad/tessdata/{eng,tur}.traineddata README.md   (tessdata_fast, Apache-2.0, SHA-256 in README)
tests/drawing/{test_ingest_raster,test_dimgrammar,test_ocr,wp2_fixtures}.cpp  tests/drawing/wp2_fixtures.h
tests/drawing/fixtures/wp2/{gen_wp2_fixtures.py,.gitkeep,wp2_{flange,plate,shaft}.png,wp2_{flange,plate,shaft}.truth.json}
tests/drawing/gui/overlay_view.cpp  tests/drawing/gui/overlay_pdfrender.cpp
docs/drawing_import/WP-2_NOTES.md
```
Frozen headers (`Raster.h`, `DimGrammar.h`, `PdfRender.h`, `OverlayView.h`, ...) are unchanged.

## What is implemented

* **Raster ingest (S1c)** `loadRasterFile` (PNG pHYs / JPEG / TIFF DPI), `ingestRaster`: background normalisation,
  deskew (LSD or Hough fallback, OpenCV 4/5 adapter in `CvCompat.cpp`), binarisation, border/title block, text/graphics
  separation, skeleton vectoriser (lines, arcs, circles with arc refit on merge, tangent refinement at fillets,
  full-circle promotion incl. circles cut into several arcs by dimension lines, removal of arc fits that hug a circle
  + arrowhead, dashed/centre grouping incl. 2-piece thin groups, filled/open arrowheads, no merge across arrowheads),
  confidences per `Confidence.h`, attrs `raster.*` / `ocr.*`, optional debug PNG (`IngestOptions::debugDir`).
* **OCR** (`ocr/Ocr.h`, private): Tesseract LSTM, one engine per thread; per line: both readings for vertical text
  (90/270, chosen by OCR + grammar score), dimension whitelist pass, per-symbol n-best → `TextRun::alternatives`;
  notes / title block read with the `tur` model (eng+tur loses Ş/Ö/Ç/İ — measured), grammar re-ranks all hypotheses with
  geometric context (near circle / arc / angular).
* **Dimension grammar** `parseDimText` / `rankDimTextHypotheses` / `formatDimText`: Ø R SR SØ M-threads (+pitch, class),
  G/BSP, fits (H7/g6), ± / stacked / unilateral tolerances, chamfers, angles, counts (4x, 4 adet/DELİK), DP/↧,
  THRU/BOYDAN, (ref), [basic], max/min, Turkish decimal comma. OCR confusion corrections (only `fromOcr`), each logged
  in `DimText::corrections` and penalising conf: O/o/0/@/Q→Ø at token start, l/I/|→1, S→5, B→8, O→0 in numbers,
  t→±, o/*→° (angular), fit-class `ho`→`h6`, `4x 10.2`→`4x Ø10.2` next to a circle.
* **pc_drawing_qt**: `toQImage`/`fromQImage` (gray / BGR / BGRA, stride, dpi, alpha composited on white),
  `makePdfRasterizer`, QtPdf `pdfPageCount` / `renderPdfPage` (gray or BGR, white background, size guard) /
  `renderPdfThumbnail`; nodeps fallback returns "QtPdf olmadan derlendi".
* **OverlayView**: scene = sheet mm (y flipped); layers per `OverlayLayer`; role colours (Visible black, Hidden blue
  dashed, Center red dash-dot, Dim/Ext/Leader green, Hatch grey, ...), confidence glow (<0.6 red, 0.6–0.9 yellow),
  layout boxes with Turkish labels, dimension anchors + bound geometry coloured by residual, view axes/relations,
  sketch candidate (`entityToPrimitive`) highlight, background via `pxToSheet`. Wheel zoom at cursor, middle/Alt-drag
  pan, Home/F = fit. Tools: Select (click, Ctrl/Shift toggle, rubber band, Delete → `deletePrims` / `deleteTexts` /
  `deleteView` / `deleteDimension`, double click → `elementActivated`), EditBoxes (move / corner resize → `setViewBox`,
  `setTitleBlockBox`, `setBorder`; drag on empty → `addView`), DrawLine / DrawArc (3 points, CCW a0<a1) / DrawCircle →
  `addPrim`, Snap (end drag, snaps to another end within 8 px) → `snapPoint`, PickGeometry (2 clicks, or double click
  for one element) → `geometryPicked`. Edits carry `setStage()`; the IR is never mutated. Esc/right click cancel.

## Metrics (synthetic fixtures wp2_flange/shaft/plate, A3 @300 dpi; `build/tests/drawing/tmp/wp2_metrics.md`)

| variant | lines R/P | circles R/P | arcs R/P | arrows R/P | text boxes R | dim raw exact | dim exact (grammar) | notes exact | deskew° |
|---|---|---|---|---|---|---|---|---|---|
| clean | 0.92 / 0.91 | 1.00 / 1.00 | 1.00 / 1.00 | 0.81 / 0.87 | 1.00 | 13/25 (52%) | 24/25 (96%) | 9/9 | 0.000 |
| noisy (blur+noise+JPEG q60) | 0.92 / 0.91 | 1.00 / 1.00 | 1.00 / 0.83 | 0.88 / 1.00 | 1.00 | 13/25 (52%) | 22/25 (88%) | 8/9 | 0.000 |
| skewed (2°+noise+JPEG q85) | 0.92 / 0.91 | 1.00 / 1.00 | 1.00 / 1.00 | 0.88 / 1.00 | 1.00 | 13/25 (52%) | 24/25 (96%) | 8/9 | -2.000 |

Known gaps (phase 2): centre lines whose dots vanish are split into long dashes; arrowheads whose tip touches a circle
(diameter dims inside holes) are missed; a dim line crossing a circle is split there; `Ti-6Al-4V` → `Ti-6AI-4V`
(l/I in notes, no dictionary); noisy `Ø100` vertical text. Runtime ≈ 3–4 s per A3 page (2 cores, OCR dominated).

## DRW requests (to file in docs/CONTRACT_REQUESTS.md; none blocking)

1. `DRW: raster line type` — WP-3 S4 should be able to rely on `Primitive.attrs["raster.lineType"]`
   ("hidden"/"center") and `raster.arrowEnds` ("0","1","01"); promote to `LineStyle::linetype` (fill "HIDDEN"/"CENTER"
   for raster) or document the attr keys as contract.
2. `DRW: TextRun dimension parse` — raster ingest already parses dimension text (`ocr.dim`, `ocr.corrections` attrs);
   WP-3 re-parses with `parseDimText(t.utf8, ctx)` which is fine; only the attr key names need to be documented.
3. `DRW: OverlayView event overrides` (cosmetic) — the frozen header declares no protected event handlers, so the view
   routes input through an event filter on itself + viewport. Works; adding the overrides in 1.1 would be cleaner.
4. `DRW: Arrowhead base width` — raster puts the measured base width into `style.width` (sheet mm), as the overlay
   uses it; state this in `Enums.h` PrimKind::Arrowhead comment.
