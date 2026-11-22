# Drawing import — file ownership (P0 bootstrap, DrawingIR 1.0)

Status: P0 done (15.11.2026). Contract headers are **FROZEN**. Change requests: `docs/CONTRACT_REQUESTS.md`, prefix
`DRW:`; both affected WPs approve, the owner (WP-3) applies and bumps the version in `Version.h`.

Rule of thumb: **a WP only creates, edits and deletes files inside the directories it owns.** No WP edits any
`CMakeLists.txt`: every module directory is globbed (`CONFIGURE_DEPENDS`); a new `.cpp`/`.h` is picked up on the next
build. Re-run CMake only if you add a new *directory* (directories are fixed below — use the ones listed).

## 1. Frozen (P0) — nobody edits without a `DRW:` request

| Path | Content |
|---|---|
| `src/drawing/include/pc/drawing/*.h` except `GroundTruth.h` | DrawingIR 1.0 contract: `Version.h`, `Geometry.h`, `Enums.h` (+ JSON names), `Confidence.h`, `Diagnostics.h` (StageResult/Status/Diagnostic), `Primitives.h`, `Layout.h`, `Annotation.h` (line roles, DimText, Dimension), `Views.h`, `Sketches.h`, `Synthesis.h`, `UserEdit.h` (edit-op catalogue), `DrawingIR.h` (+ JSON conventions), `Raster.h`, `Stages.h`, `DimGrammar.h`, `Pipeline.h`, `Features.h`, `Eval.h`, `Drawing.h` (umbrella) |
| `src/drawing_qt/include/pc/drawing/qt/PdfRender.h` | QtPdf render / QImage conversion API (implemented by WP-2) |
| `src/drawing_ui/include/pc/drawing/ui/OverlayView.h` | overlay widget API (implemented by WP-2, used by WP-3 wizard) |
| `src/drawing_ui/include/pc/drawing/ui/ImportAction.h` | designer entry point (implemented by WP-3) |
| `src/drawing/core/*` | `Features.cpp`, `Enums.cpp` (Turkish labels), `IRUtil.cpp` (find*/roleOf/clearFromStage) |
| `src/drawing/CMakeLists.txt`, `tests/drawing/CMakeLists.txt`, root `CMakeLists.txt` line `add_subdirectory(src/drawing)` | build |
| `tests/drawing/test_drawing_bootstrap.cpp`, `tests/drawing/gui/drawing_ui_bootstrap.cpp` | must keep passing (stubs → real code) |

`GroundTruth.h` is owned by **WP-1** and is **additive-only** (new members/functions allowed; existing ones keep name,
type and meaning — WP-3 codes against them).

## 2. Work packages

Each owned directory initially contains `P0Stubs.cpp` returning `Status::NotImplemented` ("uygulanmadı").
**Delete the stub (or the functions in it) when you implement them** — otherwise you get duplicate symbols.
Directories marked [dep] are compiled only when the dependency is found; otherwise their `nodeps/` subdirectory is
compiled instead (fallbacks returning `Status::Unavailable`). The owner keeps `nodeps/` in sync with the public
functions of the directory and never includes the dependency there. Use `#if PC_DRAWING_HAVE_<X>` in shared code
(always defined 0/1). Private headers stay inside the owning directory (`#include "pdf/PdfInterp.h"` works: `src/drawing`
is a private include dir of pc_drawing; same for `src/drawing_qt` and `src/drawing_ui`).

### WP-1 — vector PDF, DXF, normalization, IR JSON, evaluation, corpus
| Owns | Implements |
|---|---|
| `src/drawing/ir/` | `toJson`/`drawingIRFromJson`/`toJson(PrimitiveSheet)`/`primitiveSheetFromJson`/`saveIR`/`loadIR` (conventions in `DrawingIR.h` header comment) |
| `src/drawing/sniff/` | `sniff` (use `#if PC_DRAWING_HAVE_QPDF` for PDF page statistics) |
| `src/drawing/pdf/` [qpdf] + `pdf/nodeps/` | `ingestPdfVector`, `extractPdfPageImage` |
| `src/drawing/dxf/` | `ingestDxf` |
| `src/drawing/normalize/` | `normalize` |
| `src/drawing/eval/` | `evaluate`, `loadGroundTruth`, `irFromGroundTruth` (the "gt → perfect S2" converter WP-3 needs by P0+1 week) |
| `src/drawing/include/pc/drawing/GroundTruth.h` | additive-only (see §1) |
| `tools/drawing_corpus/` | `pc_drawing_corpus` (Qt6::Gui + OCCT HLR; target appears when a `.cpp` exists). CLI contract used by ctest fixture `drawing.corpus.generate`: `pc_drawing_corpus --out <dir> --samples <dir> --quick`, exit 0 |
| `tests/drawing/test_drawing_ir.cpp`, `test_ingest_pdf.cpp`, `test_ingest_dxf.cpp`, `test_normalize.cpp`, `test_eval*.cpp`, `tests/drawing/wp1_*.{h,cpp}` (helpers) | tests |
| `tests/drawing/corpus/` (small committed core set), `tests/drawing/real/`, `tests/drawing/fixtures/wp1/` | data |

### WP-2 — raster, OCR, dimension-text grammar, QtPdf render, overlay
| Owns | Implements |
|---|---|
| `src/drawing/raster/` [OpenCV] + `raster/nodeps/` | `loadRasterFile`, `ingestRaster`; OpenCV 4.x/5.x adapter lives here |
| `src/drawing/ocr/` [OpenCV + Tesseract] + `ocr/nodeps/` | OCR behind a WP-2 private header (`ocr/Ocr.h`), called from `raster/`; `nodeps/` provides the same private API returning "unavailable" |
| `src/drawing/text/` | `parseDimText`, `rankDimTextHypotheses`, `formatDimText` (no optional deps) |
| `src/drawing_qt/` (+ `pdf/` [Qt6::Pdf] + `pdf/nodeps/`) | `pc_drawing_qt`: `toQImage`, `fromQImage`, `makePdfRasterizer` (root dir), `hasPdfRendering`, `pdfPageCount`, `renderPdfPage`, `renderPdfThumbnail` (`pdf/`) |
| `src/drawing_ui/overlay/` | `OverlayView` (pimpl `OverlayView::Impl`; emits `UserEdit`s from `UserEdit.h`) |
| `share/persistcad/tessdata/` | eng/tur traineddata (tests fall back to system tessdata via `PC_DRAWING_TESSDATA_DIR`) |
| `tests/drawing/test_ingest_raster.cpp`, `test_dimgrammar.cpp`, `test_ocr.cpp`, `tests/drawing/wp2_*.{h,cpp}`, `tests/drawing/fixtures/wp2/` | tests + own fixtures (may generate synthetic images there or into `PC_DRAWING_TMP_DIR`) |
| `tests/drawing/gui/overlay_*.cpp` | overlay GUI tests |

### WP-3 — layout, roles, dimensions, views, sketch, 3D, pipeline, wizard (owner of the contract)
| Owns | Implements |
|---|---|
| `src/drawing/interp/` | `analyzeLayout`, `annotate`, `buildViews`, `reconstructSketches`, `ingest` (S1 dispatch), `runStage`, `runPipeline`, `applyUserEdits`, `mergeIntoDocument` |
| `src/drawing/synth/` | `synthesize` (+ HLR re-projection; TKHLR is linked) |
| `src/drawing/cli/` | `pc_drawing_cli` (target appears when a `.cpp` exists): `pc_drawing_cli <in> --out part.pcad --ir ir.pcdrw.json` |
| `src/drawing_ui/wizard/` | `installDrawingImportAction`, `runDrawingImportWizard`, QWizard pages (uses only `OverlayView.h` from WP-2) |
| `tests/drawing/test_interp_*.cpp`, `test_sketch_build.cpp`, `test_synth.cpp`, `test_e2e_corpus.cpp`, `test_pipeline*.cpp`, `tests/drawing/wp3_*.{h,cpp}`, `tests/drawing/fixtures/wp3/` (hand-made IR JSON fixtures) | tests |
| `tests/drawing/gui/` except `overlay_*.cpp` and `drawing_ui_bootstrap.cpp` | wizard harness (`drawing_wizard_harness.cpp`) |
| contract files of §1 (applies approved `DRW:` requests) | |

All test files share ONE executable (`pc_drawing_tests`): use **unique suite names** prefixed by area
(`DxfIngest.*`, `DimGrammar.*`, `Layout.*`, ...) and put file-local helpers in an anonymous namespace. Tests that
need an optional dependency use `#if PC_DRAWING_HAVE_<X>` or `GTEST_SKIP()` when `buildFeatures()` says it is absent.
Tests that need the generated corpus look in `PC_DRAWING_CORPUS_GEN_DIR` and `GTEST_SKIP()` if it is missing.

## 3. Corpus and fixtures

* `tests/drawing/corpus/<family>/<case>/{model.pcad, sheet.pdf, sheet_outline.pdf, sheet_pscript.pdf, sheet.dxf,
  clean.png, scan_*.jpg, photo_*.jpg, gt.json}` — small committed core set, **WP-1**.
* Full corpus: generated by WP-1's `pc_drawing_corpus` into `<build>/drawing_corpus` (`PC_DRAWING_CORPUS_GEN_DIR`)
  by ctest fixture `drawing.corpus.generate` before the drawing tests (registered only once the tool exists).
* WP-2 / WP-3 may generate their own fixtures in `tests/drawing/fixtures/wp2/` resp. `fixtures/wp3/` (committed)
  or at test time into `PC_DRAWING_TMP_DIR`. They never write into `corpus/`.
* Real drawings (enjektör, archive drawings) never enter the repo beyond `tests/drawing/real/` symlinks + gt.json.

## 4. The single designer hook (WP-3, applied at INTEGRATION — not now)

Another agent currently owns `src/designer/*` and `tests/gui/*`. Nothing in this package touches them. When their work
has landed, request via `docs/CONTRACT_REQUESTS.md` ("DRW: designer hook"), applied by the designer owner:

1. `src/designer/CMakeLists.txt`: `if(TARGET pc_drawing_ui) target_link_libraries(pc_designer PUBLIC pc_drawing_ui)
   target_compile_definitions(pc_designer PUBLIC PC_HAVE_DRAWING_IMPORT=1) endif()`
2. `src/designer/MainWindowShell.cpp` (one block, guarded by `#ifdef PC_HAVE_DRAWING_IMPORT`):
   ```cpp
   #include "pc/drawing/ui/ImportAction.h"
   pc::drawing::ui::DrawingImportHost h;
   h.currentDocument = [this] { return doc_; };
   h.commit = [this](const pc::Document& d, const QString& t) { /* push ONE undo step "Teknik resimden içe aktar" */ };
   h.editSketch = [this](QWidget* p, pc::Sketch& s) { /* open SketchEditor modally */ return true; };
   QAction* a = pc::drawing::ui::installDrawingImportAction(this, menuFile_, h);
   /* ribbon: Ekle sekmesi > "Teknik resim" -> add `a` via the Ribbon API */
   ```
No other designer file changes. The wizard lives entirely in `src/drawing_ui/`.

## 5. Build

```
cmake -S . -B build-drw -G Ninja && cmake --build build-drw && (cd build-drw && ctest -R drawing)
cmake -S . -B build-drw-noopt -G Ninja -DPC_DRAWING_DISABLE_OPTIONAL=ON   # Windows w/o qpdf/OpenCV/Tesseract/QtPdf
```
Options: `PC_BUILD_DRAWING` (ON), `PC_DRAWING_DISABLE_OPTIONAL` (OFF), `PC_DRAWING_WITH_{QPDF,OPENCV,TESSERACT,QTPDF}` (ON).
CMake prints `pc_drawing: qpdf=… opencv=… tesseract=… qtpdf=…`. Each WP should work in its own copy of the repo
with its own build dir.
