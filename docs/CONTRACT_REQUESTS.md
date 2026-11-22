
## Agent E (apppkg/appui/runner) notes
* mfg (Agent C): bracket.pcad default raises HOLE.TIP_BREAKOUT on non-exposed derived param `Lbore` and the message says "Lbore <= 24.20" in one run and ">= 24.20" in another; direction text looks inconsistent. Suggestions on non-exposed params cannot be applied in the runner (only exposed params have inputs).
* lintApp returns strings prefixed "HATA: " / "UYARI: " (helpers in src/apppkg/pc/AppLint.h).

## B2 (model) -> C2 (mfg): analytic model ignores OnPlane sketches (bug, Nov 2026)
`src/mfg/AnalyticModel.cpp` (sketch case, ~line 626) has no branch for `SketchPlaneKind::OnPlane`, so OnPlane
sketches fall through to `sketch::standardFrame()` = world XY. Their extrudes are then analysed as Z-prisms in
the XY plane, which produces phantom CNC.SHARP_INTERNAL_CORNER / MIN_WALL / AM.OVERHANG issues on
samples/v2_multibody.pcad (e.g. "edge(Extrude3.side.e5|Extrude3.side.e8)", an edge that does not exist).
Minimal fix (verified on a local copy: v2_multibody default -> 0 errors, 4 inherent warnings):
```cpp
} else if (f.sketch->plane.kind == SketchPlaneKind::OnPlane) {
  // datum frame not known analytically -> frameOk stays false (geometric checks cover these features)
} else {
```
Better: compute the datum frame analytically, or take `ModelResult::datumPlanes[planeRef].frame` + the
sketch offset (same rule as `pc::model::sketchFrame`).

## DRW: designer hook — APPLIED (integration, 16.11.2026)
Requested by WP-3 (OWNERSHIP.md §4). Applied at integration:
* root `CMakeLists.txt` (after `add_subdirectory(src/drawing)`, because `src/designer` is configured first and
  `if(TARGET pc_drawing_ui)` would be false there): `pc_designer` links `pc_drawing_ui` PUBLIC and gets
  `PC_HAVE_DRAWING_IMPORT=1`. `src/designer/CMakeLists.txt` is unchanged.
* `src/designer/MainWindowShell.cpp`, two `#ifdef PC_HAVE_DRAWING_IMPORT` blocks: the action
  (`actionDrawingImport`) is inserted into Dosya before "Dışa Aktar"; `commit` = `MainWindow::commitDocument`
  (ONE undo step "Teknik resimden içe aktar"); ribbon group Tasarım > "İçe aktar" > "Teknik resim"
  (`actRibbonDrawingImport`). `openAsNewDocument` / `editSketch` are not wired (optional, phase 2).
* Covered by `tests/gui/gui_harness_drawing_import.cpp`.
