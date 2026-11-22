# R1 — Text drawn as glyph outlines in vector PDFs

Status: done (16.11.2026). Owner: R1. Module: `src/drawing/glyphtext/`.

## Problem
Matplotlib PDFs (and Acrobat "convert text to outlines", SHX-like exports) draw every character as a filled path.
`pdftotext` returns nothing, the interpreter emitted ~3000 small filled Polyline/Bezier primitives per sheet, S3 found
no title block / scale and S4 no dimension text. In the two real drawings each text string is ONE fill operator
(`... f`) whose subpaths are the glyph contours in absolute page coordinates (no XObjects, no fonts). Two quirks
found: (1) contour order inside a string is not reading order; (2) the same glyph is written CW in one string and
CCW in another (DejaVu `Ø` therefore renders with a black half under the nonzero rule).

## Approach (`glyphtext::extractGlyphText`, called from `ingestPdfVector`)
1. **Candidates**: runs of consecutive fill-only closed primitives with the same `prov.ref` (= one fill operator,
   also one `Do` of a Form XObject). Every contour must be ≤ 12 mm.
2. **Glyphs**: contours nested in a larger contour's box are holes/islands of it (pieces); baseline axis = PCA of the
   piece centres, snapped to 0/90° (±4°, ±16° for ≤4 pieces); in the text frame pieces overlapping > 50 % in x are
   merged (i/j dots, `:`, `=`, `Ø` slash ends). Line metrics: cap height = median height of the tall glyphs, baseline
   = median bottom.
3. **Gate**: the page activates only with ≥ 3 strings of ≥ 3 glyphs (≥ 2 non-trivial, i.e. not a bare triangle/quad).
   Arrowheads and normal-text PDFs are left untouched (test `NormalTextPdfUntouched`, full corpus unchanged).
4. **Reading sense**: each string is rasterised (own scanline rasteriser, 4×4 supersampling, even-odd, cap = 36 px)
   in both senses and read by Tesseract (`ocr::Engine`, eng, PSM 7). Strings with ≥ 5 glyphs take the more
   confident sense; short strings (`Ø50`, `Ø3.2`, Tesseract is weak on `Ø`) take the sense in which more glyphs
   match already labelled outlines (OCR sense breaks ties).
5. **Glyph-signature cache**: signature = control points in the text frame, relative to the glyph box, scaled by its
   larger side (same glyph at any size matches), each contour oriented CCW and started at its lowest-leftmost point;
   compared point by point (tol 0.6 %), so a rotated/flipped glyph does not match (6 vs 9 only via orientation).
   Labels: every line reading with exactly one character per glyph votes for the glyph classes (weight 0.2 + line
   confidence); shape rules override: 1 outer + 2 diagonally placed holes → `Ø`, small raised ring → `°`. Classes
   without votes: the glyph alone, PSM 10.
6. **Single-glyph strings** (`5`, `A`, vertical `9`): matched against the cache in 0, 90, −90, 180°; unknown ones are
   OCR'd in the four orientations (PSM 10, alphanumeric whitelist) and kept only with confidence ≥ 0.6.
7. **Text**: characters from the class labels; spaces from the visual gap (closest ink approach over 12 cap-height
   scanlines, box gap for `.`/`-`): space if gap > 0.7 cap, or > 0.25 cap where Tesseract put a word break (> 0.45
   cap if the OCR reading is not glyph-aligned). If a glyph has no label: the line OCR reading, re-ranked with
   `rankDimTextHypotheses` for dimension-like strings (fallback path, `glyphtext.method=line-ocr+grammar`).
8. **Output**: one `TextRun` per string: `asOutlines=true` (S2 does not merge it), `font="glyph-outline"`, origin on
   the baseline, `quad`, `height` = cap height, `rotDeg`, `conf`, OCR alternative, `mergedFrom` = removed primitive ids,
   attrs `glyphtext.method` / `glyphtext.glyphs`. The glyph primitives are removed. Diagnostic `pdf.glyphText`
   (info); `pdf.glyphText.noOcr` (warning) when outline text is present but Tesseract is not built in.

Offline and deterministic: no randomness, stable ordering, Tesseract LSTM with fixed parameters.

## Metrics (`ctest -R GlyphText`, ground truth `tests/drawing/real/*.gt_text.json`)
| Drawing | strings | dimension texts exact | all items exact | recall (spacing-free) | title-block scale (S3) |
|---|---|---|---|---|---|
| radrad_swirler_SWC-RR-001 | 68 | 28/28 (100 %) | 71/71 (100 %) | 100 % | 2:1 |
| simplex_atomizer_ATM-PS-001 | 44 | 17/17 (100 %) | 47/47 (100 %) | 100 % | 10:1 |

All section labels (`SECTION A-A`, `SECTION B-B PRIMARY SWIRLER`, `SECTION C-C SECONDARY SWIRLER`, `VIEW B - ...`),
vertical dims (rot ±90: `Ø10 Ø17 Ø19 Ø27 (D0) Ø43 Ø47`, `Ø2 Ø3.2 Ø5 Ø8`, `90°` in both senses, `1.35`, `0.5`),
notes and title-block cells are read exactly. Geometry left after ingest contains no glyph contours.
Ingest time incl. OCR: ~10 s (radrad, ~140 Tesseract line calls) / ~4 s (atomizer).

## Files
Created
- `src/drawing/glyphtext/GlyphText.h`, `src/drawing/glyphtext/GlyphText.cpp` — module
- `src/drawing/pdf/GlyphTextUnit.cpp` — compiles `glyphtext/GlyphText.cpp` inside the pdf/ (qpdf) module, because
  `src/drawing/CMakeLists.txt` is frozen and globs a fixed directory list. Replace by a `pc_drw_collect(... glyphtext
  REQUIRES PC_DRW_HAVE_QPDF)` line when the build file is next opened.
- `tests/drawing/test_glyphtext.cpp` — rasteriser, normal-text PDF untouched, two real drawings (GT metrics +
  S0–S3 scale)
- `tests/drawing/real/radrad_swirler_SWC-RR-001.gt_text.json`, `tests/drawing/real/simplex_atomizer_ATM-PS-001.gt_text.json`
- `docs/drawing_import/R1_GLYPHTEXT.md`

Changed
- `src/drawing/pdf/PdfIngest.cpp` — include + 17-line hook after `interpretPage` (calls `extractGlyphText`, adds the
  diagnostic). No other file outside the owned set was touched.

## Limits
- Strings are taken per fill operator; a writer that puts a whole page of text into one fill would need splitting into
  lines (glyph gap > 4 cap heights already rejects such groups instead of mis-reading them).
- Text at arbitrary angles works (PCA axis) but was only verified at 0/±90°.
- Without Tesseract the glyph primitives stay in the geometry (warning `pdf.glyphText.noOcr`).
