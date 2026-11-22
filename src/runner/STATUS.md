READY v3
runner: CLI [app.pcapp] [--set N=V] [--screenshot f] [--show-planes] [--validate-only [--text]]; embedded payload; async validation (QtConcurrent, stale dropped); inline indicators, suggestions, ghost+banner, issue panel with domain tabs, info strip, STL/STEP export. Also pc_mkapp CLI (src/apppkg/mkapp.cpp).
QA: per-field "Uygula" validates immediately (also in non-live mode); validateSafe never throws (OCCT Standard_Failure
included); an empty model is not "acceptable". GUI coverage: tests/gui/gui_harness.cpp (ctest "gui_harness"), tests/e2e.sh.
v2 (multi-body): >1 body -> distinct body colours (setFaceColors), "Gövdeler" list (swatch, name, volume, mass, visibility checkbox ->
setHiddenFaces; selecting a row highlights its faces), issue rows show "Gövde: <name>" + body filter combo, datum planes hidden by default
("Yardımcı düzlemleri göster" / --show-planes), info strip shows body count + totals, STL/STEP export asks "Tüm gövdeler" or one body
(exports BodyInfo::shape). --validate-only JSON: "bodies":[{name,volumeMm3,massG}] and issue "body"; text: "Gövdeler (N)" + "[Gövde: X]".
Single-body apps look/behave as in v1 (no colours, no body panel). Tests: tests/test_apppkg_v2.cpp, tests/runner_v2_cli.sh.
Screenshots: docs/screenshots/v2_runner_{default,planes,violation}.png.
Review fix: per-body export file names are sanitised (":" of split pieces and other Windows-invalid characters -> "_").

v3: "Yardımcı düzlemleri göster" planes use viewer::datumPlaneHalfSize (local sizing, smaller/subtler).

v4: section view in generated apps: toolbar toggle "Kesit" + preset combo (XZ / YZ / XY / Çeyrek kesit) + offset slider + Ortala +
Ters çevir (pc::viewer::SectionControls, RunnerWindow::sectionControls()); CLI `--section xz|yz|xy|quarter [--section-offset PCT]
[--section-flip]` (works with --screenshot; unknown value -> exit 1). Gövdeler list: right-click > "Saydam" makes a body translucent
(setBodyTransparent, kept by body name across updates). Screenshot: docs/screenshots/section_runner.png. Test: gui_harness_v4.

## Review Nov 2026 (independent reviewer)
- "Hesaplanan değerler" box: non-exposed parameters that carry a label are listed read-only under the first input group and
  refreshed after every validation (out-of-range values red). `--show-results` scrolls to it (screenshots). Planes with
  `show=0` are never drawn.
