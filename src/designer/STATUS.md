READY v3
persistcad designer (Turkish UI). Build: `cmake -S . -B build-gui -G Ninja && ninja -C build-gui -j1 persistcad`.
CLI: `persistcad [file.pcad] [--screenshot out.png] [--sketch-screenshot out.png [--sketch-id ID]]
 [--dialog-screenshot out.png [--dialog-feature ID]] [--mfg-screenshot out.png]
 [--try-change NAME=EXPR --review-screenshot out.png] [--select-issue N] [--section xz|yz|xy|quarter] [--selftest | --selftest2]`
(run under `xvfb-run -a -s "-screen 0 1600x1000x24"`). --selftest/--selftest2 drive sketch tools, constraints, drag-solve,
extrude, picking, param change/undo/rename/rollback/suppress/delete through real mouse events and the change pipeline.
Done: menus/toolbars/docks (feature tree + context menu + rollback, parameter table with rename-rewrite, issues table with
per-row "Öneriyi uygula" + viewer highlight), status bar (regen ms, volume, sketch DOF, error/warn counts, hovered persistent
face name), change pipeline (validateChange before commit; review dialog with Önerileri Uygula / Yine de Uygula / Vazgeç and
ghost shape), modeless feature dialogs with expression completer + live value + "+P" new parameter, face/edge picking into
Fillet/Chamfer/Shell lists, sketch editor (point/line/rect/circle/arc, construction, snapping incl. H/V auto-constraints,
11 constraints, 7 dimension types with named-parameter creation, double-click dimension edit, live drag solve, DOF/conflicts),
undo/redo (document snapshots; sketch-local undo while sketching), Save as App via pc::appui::runSaveAsApp, STEP/STL export.
Limitations: no marquee selection / trim / offset / mirror in sketch; Pattern/Revolve dialogs untested against all model edge
cases; sketch editor doesn't display the 3D model underlay; undo after "Yine de Uygula" re-runs the full check asynchronously.

v2 (D2): datum planes + multi-body UI. Ctrl+click / right-click an edge -> popup menu "Bu kenara dik düzlem" (immediate edgeNormal,
t=0.5), "Bu kenardan açılı düzlem" (dialog, edgeAngle), "İki kenardan düzlem" (pick 2nd edge), "Düzlem diyaloğu...". Ekle > Düzlem
dialog (kind combo, Seç-buttons fed by viewer picks, t/angle/offset/rotate expressions with completer/+P, flip, live preview via
regenerate + status label). Click a datum plane (datumPicked) -> "Bu düzlemde sketch başlat"; Ekle > Sketch has "Seçili düzlemde (datum)".
Extrude/Revolve: İşlem = Birleştir (Add) / Çıkar (Subtract) / Kesişim (Intersect) / Ayrı gövde (New body), Hedef gövdeler checklist,
keepTool, Gövde adı; Hole/Pattern have the targets list. Ekle > Gövdeleri Birleştir (Combine). "Gövdeler" dock: colour swatch,
visibility (setHiddenFaces), volume, rename (writes bodyName + rewrites targets/tools), select = highlight. Issues table: "Gövde"
column (col 6; button moved to col 7). View > Datum düzlemleri. CLI: --plane-screenshot, --plane-dialog-screenshot (writes <out>_main.png
with the dialog composited). Harness: tests/gui/gui_harness_v2.cpp (ctest gui_harness_v2). New file: MainWindowV2.cpp.
Limitations: plane display size = 0.6 x model hint; no face-based context menu for faceOffset (dialog only); sketch editor still has no 3D underlay.
Review fixes (v2): feature dialogs keep op/bodyName/keepTool/targets they have no widget for (Pattern/Hole/Shell);
editing a feature lists only bodies created before it; edge/datum menus, edge-menu planes and body rename are refused
while a modeless feature dialog is open (its OK commits its own snapshot and would drop them); renaming a split piece
renames the stem and remaps "<stem>:k" references/hidden state; stale selected datum is ignored; a background click
cancels a pending two-edge plane; double-click on the body name renames. Harness: gui_harness_v2 section 8.

v3: feature dialog OK merges only the edited feature (+ params created by "+P") into the CURRENT document (parameter-table edits made
while the dialog is open survive); plane sizes via viewer::datumPlaneHalfSize (preview too); `--selftest2` without a suitable file
prints a message and exits 2. Test: tests/gui/gui_harness_v3.cpp (ctest gui_harness_v3).

v4: section view. Görünüm > Kesit (K) + toolbar toggle "Kesit" (icon "section") + Görünüm > Kesit düzlemi (XZ / YZ / XY / Çeyrek kesit,
Ters çevir); while on, a small panel floats in the viewer's top-right corner (preset combo, offset slider within the bbox with mm
read-out, Ortala, Ters çevir) — pc::viewer::SectionControls, MainWindow::sectionControls(). View-only: not stored in the document,
no undo entry. CLI: `--section xz|yz|xy|quarter [--section-offset PCT(-100..100)] [--section-flip]` together with --screenshot.
Screenshot: docs/screenshots/section_designer.png. Test: tests/gui/gui_harness_v4.cpp.

## Review Nov 2026 (independent reviewer)
- Plane dialog: kind `axisAngle` (axis combo + angle, no picks) and `show` checkbox; planes with `show=0` are drawn only while
  selected / their sketch is current. CLI for documentation shots: `--only-bodies A,B`, `--view iso|top|front|...`,
  `--viewport-screenshot f [--viewport-size WxH]`. GUI test: tests/gui/gui_harness_v5.cpp.

## v6 (Nov 2026) — customer report "sketch parametreleri kayboluyor / 3D işlemine parametre veremiyorum"
Root causes (sketch): `MainWindow::finishSketch` committed `work_`, the document snapshot taken when the sketch was
opened -> every parameter-table edit / rename / mfg-setting change made while sketching was reverted; parameters created
in the dimension dialog lived only in that copy (not in the table, not editable); Kaydet / Kapat / Aç / Yeni / Save-as-App
ignored the open sketch (saved without it, no "unsaved" mark, a stale sketch session survived Yeni/Aç); dimensioning
scaled the geometry out of the sketch view and finished sketches were not drawn in 3D (looked "lost").
Fixes: candidate = CURRENT document + edited sketch (`sketchCandidate`); parameters created anywhere (ExprEdit "Yeni…",
NewParameterDialog, dimension dialog) are committed at once through `installParameterCommitHook` (undoable "Parametre ekle")
and appear in the table; `applyDocument` keeps the open sketch's / feature dialog's parameters in sync (table edits re-solve
the open sketch, renames rewrite it, removing a parameter used by it is refused); `resolveOpenSketch` (Sketch'i Bitir /
Değişiklikleri at / Vazgeç) before save, close, open, new, export, Save-as-App; window shows [*] while the sketch has edits;
Vazgeç asks when there are changes (created parameters stay); sketch view re-fits after dimension/parameter changes.
3D: finished sketches are drawn as wires (edge names `sketch:<id>:e<entity>`, Ctrl+click = edit); consumed sketches hidden
by default, tree context menu "3B görünümde göster" toggles (view state only).
Parameters for features: every ExprEdit (all feature dialog fields, plane fields, sketch dimension dialog, new toolbar field
"Düzlem ofseti" in sketch mode) = expression + live value + picker combo `paramPick` ("— değer / ifade —", "NAME = value unit",
"Yeni parametre…") + visible "Yeni…" button (`paramNew`, replaces "+P") -> NewParameterDialog (name, value, unit, min, max,
Uygulamada göster, label, group, description). Picker shows the binding when a dialog/dimension is re-opened. Dimension dialog:
inline "Bu değerle yeni adlandırılmış parametre oluştur" + name + "Uygulamada göster". Tree context menu "Parametreleri göster"
(direct + indirect parameters; click selects the table row), `featureParameterSummary()`. Tree labels "r = R", "d = …".
Status bar cleared when leaving the sketch / closing a feature dialog. Runner: per-field suggestion uses the same strictest
rule as the banner; `--validate-only` JSON has `bbox` + `volumeMm3`.
Test: tests/gui/gui_harness_v6.cpp (ctest gui_harness_v6). Screenshots: docs/screenshots/v6_*.png.
v6 follow-ups: 3D dimension labels ("H = 30", "40", "Ø…") for the selected / last edited sketch via the additive
`pc::viewer::ShapeViewer::setLabels(Label3D)` (Görünüm > Sketch ölçüleri (3B), objectName actSketchDims3D); runner re-fits
the camera when the regenerated part grows out of the last fitted box (camera kept otherwise).

## SC-UX WP-A "kabuk" (Nov 2026) — SpaceClaim-like shell (docs/spaceclaim_ux/SPEC.md, WP-A_NOTES.md)
- Ribbon (`Ribbon.h/.cpp`: Dosya button + quick access + Tasarım/Görüntü/Ölçüm/İmalat tabs, embedded QToolBars),
  menu bar hidden, old "Ana" / sketch toolbars removed (sketch tools, constraints, dimensions come from ToolManager).
- Docks: Yapı (Yapı / Geçmiş / Seçim / Gruplar tabs at the bottom), Seçenekler / Özellikler, İmalat Denetimi (bottom).
- Modes K / X / D (`DesignMode.cpp`), "Sketch düzlemi seç" state, Shortcuts.cpp table + unsupported-key messages +
  text-input guard, status bar widgets, MainWindow implements IDesignHost and creates the ToolManager.
- Sketch editor is a child of the viewer (overlay geometry), no QStackedWidget.
- Tests: tests/gui/gui_harness_sc_shell.cpp (SH items of ACCEPTANCE.md); every older harness passes unchanged.

## SC-UX integration (WP-A shell + WP-B tools merged, 15 Nov 2026)
- Real ToolManager: `toolsReal_` is true, the P0 stub bridge (SketchEditor tool sync, constraint / dimension / construction /
  delete forwarding) is inert. WP-B overlays created by the shell: `createMiniToolbar(viewer)`, `createToolGuideBar(viewer)`.
- Viewer: `NavigationScheme::SpaceClaim` in the designer (detected at compile time in DesignMode.cpp); the runner keeps the
  viewer's Legacy default. Picks come from `ShapeViewer::picked(name, kind, mods, clicks, pos)` -> `onViewerPicked` (host
  states first: plane pick, sketch-face / datum pick, open FeatureDialog, two-edge plane; then `ToolManager::handlePick`; then
  the legacy selection path); `hovered` -> `handleHover` (faces and edges). Legacy facePicked/edgePicked/datumPicked slots are
  used only in the fallback (Legacy) scheme. Double click on a 3D sketch curve edits the sketch (was Ctrl+click).
- WP-B requests: A-1 construction lines are drawn and pickable in 3D (`addSketchOverlay`; Alt+click = revolve axis);
  A-2 parameters created while sketching go to `work_` (expressionContext) and are committed to the document at once;
  A-3 plain click on an edge / datum no longer opens the edge / datum menu (right click does, `viewerContextMenu`);
  #4 `tools_->onDesignModeChanged` runs after the mode is in effect (`notifyDesignMode`, every K / X / D / plane-pick path);
  #3 sketch overlay: `attachToViewer` once, `setPlaneFrame` from `openSketchFrame` on open, offset change (ribbon field,
  Ctrl+→/←) and every applyDocument; solids see-through (0.35) while sketching; `planView()` on entering; V = WP-B plan view.
  #5 `updateViewer` adds `appendSketchRegionFaces` for the sketches shown in 3D (the open sketch excluded).
  A-4 (PlaneTool "Diyalogla oluştur") not done: needs ToolApi 1.1 `openNewFeatureDialog` (Oluştur > Klasik > Düzlem... exists).
- WP-A requests to WP-B: R1 Esc in "Sketch düzlemi seç" (Select's cancel() is false with an empty selection -> shell
  cancels), R2 Del, R3 right-click menus, R4 tool sync, R5 icons, R6 camera properties (used by sc_e2e ACC 27) — all hold.
- Fixes found by the integration: SelectTool took Ctrl+Shift+A (Save as App) as Ctrl+A; Pull "Kes (Cut)" with a positive
  value on an OnFace sketch cut into the air (now `direction=reverse`, into the material); solids stayed transparent after
  leaving a sketch; hover pre-highlight hid the selection colour; Options panel had a horizontal scroll bar; stale
  "Sketch DOF" in 3D; `--selftest` clicked the hole rim (edge preferred in the SpaceClaim scheme).
- Runner `--validate-only`: JSON `parameters` (exposed parameters with the values used), text `Parametreler: ...` line.
- ToolApi.h unchanged (SC-UX ToolApi 1.0).
- Tests: `tests/gui/gui_harness_sc_e2e.cpp` (ctest gui_harness_sc_e2e, 110 checks: the ACCEPTANCE first-10-minutes flow on
  the real MainWindow, save/reopen, Save as App + app `--validate-only --set`), `--shots` -> docs/spaceclaim_ux/shots/final_*.png.
  gui_harness_v2 / qa / sc_shell adapted to the SpaceClaim scheme (picks via `picked`, edge menus via right click, 6 px edge
  pick radius). Full ctest 192/192 under Xvfb; GUI harnesses also with QT_FONT_DPI=72.

## Seçimden parametre (Nov 2026) — "Seç ile kenar / çap / yarıçap seçip parametre yapayım"
- `ParamSelect.h/.cpp` (pure logic): `sketchMeasures` (line -> length, W/H for rectangle sides; circle -> Ø + R;
  arc -> R + Ø + sweep (info only); 2 points -> distance / dx / dy; point -> X / Y from the origin; 2 lines -> angle or
  parallel distance; point + line; dimension label -> that dimension; several circles / lines -> ONE measure with
  several targets), `modelMeasures` (Extrude end/start -> distance, cylindrical side -> sketch Ø (added if missing),
  planar side -> line length + distance, edges -> sketch curve / extrude distance / hole rim, Fillet / Chamfer / Hole /
  Revolve / Shell / Pattern faces -> their field; unmapped -> read-only geometric value), `suggestName`, `bindInSketch` /
  `bindInDocument` (re-binds an existing equivalent dimension — `N/2`, `2*N`, `-N` where needed — or adds it; on
  over-constraint returns the conflicting constraint ids, dimensions first, removing any ONE fixes it).
- `ParamSelectUi.h/.cpp`: `ParamSelectController` (owned by MainWindow, `paramSelect()`), floating badge
  `paramBadge` (P per value, Ø⇄R toggle, ⛓ bind), inline `QuickParamPopup` `quickParamPopup` (Ölçü, Bağla, Ad, Değer,
  Min/Maks ±50 %, Uygulamada göster; Enter / Esc), conflict box `paramConflictBox` (Çakışanı kaldır ve bağla /
  Referans parametre / Vazgeç), context menus ("Parametre olarak ekle", "Parametreye bağla"; sketch right click with
  Seç = `sketchContextMenu`), Properties rows "Seçimden parametre" (`PropertiesPanel::Hooks::measureRows`,
  buttons `prop.param.<key>`), Ctrl+G = popup when the selection is measurable (otherwise NewParameterDialog as before).
- SketchEditor: `selectionChanged`, `contextMenuRequested`, `applyExternalEdit(sketch, createdParams, msg)` (one
  sketch-local undo step that carries the created parameters; undo / redo emit `parametersUndone` /
  `parametersRedone`, the controller removes / restores them in the document), `selectionAnchor`,
  `dimensionLabelRect`. 3D creation = one `commitDocument` ("Parametre (seçimden): …").
- ToolApi.h unchanged. Test: `tests/gui/gui_harness_param_select.cpp` (ctest gui_harness_param_select, 100 checks,
  also with QT_FONT_DPI=72), `--shots` -> docs/param_select/*.png. Manual: KULLANIM_KILAVUZU §3.5.
- Limitations: arc sweep angle has no driving constraint type (shown, not parametrizable); "Referans parametre" is a
  plain parameter with the measured value (no driven-dimension display); 3D edges between unrelated faces (e.g. fillet
  boundaries) only show a geometric length.

## Adlandırılmış seçimler UI (Nov 2026) — "yüze ad ver; ad sonraki işlemlerle asla değişmesin" (SpaceClaim Named Selections)
- Ctrl+G is context-sensitive: tool value box / value just pulled (PullTool keeps `lastFid_` only until the next pick)
  -> parameter; 3D faces / edges / bodies selected -> `createNamedSelectionFromSelection` (Grup<k>, inline editor in
  Gruplar, typed name merged into the creation step = ONE undo step via `NsCommand::mergeWith`); sketch measurable
  selection -> ParamSelect popup; nothing -> NewParameterDialog. Face / edge right-click menu "Adlandırılmış seçim oluştur".
- Gruplar = QSplitter: Parametreler (unchanged table) + `NamedSelectionPanel` (`nsTree`: folder "Adlandırılmış Seçimler (n)",
  rows name / count / status icon, member rows; buttons nsCreate / nsReplace / nsDelete; F2 / double click rename,
  Del, context menu `nsContextMenu`: Seç ve vurgula, Yeniden adlandır, Sil, Seçimi değiştir, Seçili ögeleri ekle /
  çıkar, Yeniden bağla (lost / suspect members: suggestions or the selected entity, explicit only), Renk, STEP'e
  aktar, Vurguyu göster). Click = select members + highlight in the selection's colour.
- `NamedSelectionManager nsMgr_` updated in `refreshAll` (and NS-only commands); StatusChanged -> non-modal status
  message + permanent label `nsStatus` ("Ad. seçim: n · k dikkat"); never a dialog. NS-only edits = `NsCommand`
  (no regeneration). Hover: "Yüz: <name> — Ad: X"; Properties "Adlandırılmış seçim > Ad" (`prop.nsName`: create /
  add / rename / clear); Görünüm > "Adlandırılmış seçim etiketleri" (`actNsLabels`, Label3D at the largest face);
  tint (view state). Save: `refreshSignatures` before writing. Save-as-App: group `saNamedSelections` (exportStep ticks).
- Test: `tests/gui/gui_harness_named_sel.cpp` (ctest gui_harness_named_sel, 202 checks, also QT_FONT_DPI=72),
  `--shots` -> docs/named_selections/shots/*.png. gui_harness_sc_shell: selection cleared before its "Ctrl+G with
  nothing selected = parameter dialog" check.

## Gizle / Göster (Nov 2026) — "seçili YÜZ ve GÖVDE için gizle/göster" (SpaceClaim Hide / Hide Others / Show All)
- View state in the document: `pc::ViewState Document::view` (`hidden`, `transparent` = NamedSelection member lists:
  Face members with persistent names + signatures, Body members), `.pcad` key `"view"` (omitted when empty; a malformed
  section is ignored). Resolved with `pc::model::resolve` after every regeneration (`MainWindow::visUpdate`): split
  hidden face -> every piece hidden, lost face ignored silently (kept, comes back hidden), body split -> pieces;
  renameBody rewrites Body members. Save-as-App strips it; STEP / STL / volume / mfg never see it.
- `MainWindowVis.cpp`: `hideItems` / `showItems` / `isolateItems` / `showAllItems` / `setItemsTransparent` (each ONE
  `VisCommand` undo step, applied without regeneration via `applyViewState`), context-sensitive Ctrl+H (faces -> faces,
  bodies -> bodies), Alt+H isolate (faces: other faces of their bodies + other bodies), Ctrl+Shift+H; hidden items leave
  the selection. `setBodyVisible` / `bodyVisible` / `hideSelectedBodies` / `hideOtherBodies` / `showAllBodies` keep their
  signatures (now undoable). Queries: `faceHidden`, `hiddenFaceNames`, `hiddenBodyNames`, `hiddenFacesOfBody`,
  `visibilityStatusText`.
- UI: ribbon Tasarım > "Gövde / Görünürlük" (+ `actTransparent` "Saydam yap") and Görüntü > "Gizle / Göster"
  (`hideShow`); viewport menu (`ctxHide`, `ctxHideOthers`, `ctxHideBody`, `ctxIsolateBody`, `ctxShowBodyFaces`,
  `ctxTransparent`, `ctxShowAll`); Yapı: body row > "Gizli yüzler (n)" (`StructurePanel::hiddenFacesNode`, unchecked
  face rows, tick / right click "Göster" = show), body menu `structHide/Show/Isolate/ShowFaces/Transparent`; named
  selection menu `ns.hide` / `ns.show` / `ns.hideOthers`; status bar `visStatus` ("n gizli yüz, m gizli gövde", click =
  show all). Viewer unchanged: hidden faces are skipped in colour and pick passes (click goes through), edges bounding a
  visible face stay, edges only between hidden faces vanish; transparency via `setTransparentFaces` (sketch mode still
  makes everything see-through).
- Test: `tests/gui/gui_harness_visibility.cpp` (ctest gui_harness_visibility, 90 checks, also QT_FONT_DPI=72),
  `--shots` -> docs/visibility/shots/*.png. Manual: KULLANIM_KILAVUZU §3.7.
- Limitations: no "yalnız görünenleri dışa aktar" export option (STEP always exports everything); a selected transparent
  face is highlighted opaque; edges cannot be hidden on their own; showing a face of a hidden body needs the body shown.

## SC-2D-IMPORT (Nov 2026) — "2B çizimi 3B'ye çevir" (SpaceClaim: 2D drawing as curves + Layers + fold views)
- Dosya > İçe aktar > "2B çizim (eğri olarak)…" (`actImport2D`, ribbon Tasarım > 2B Çizim) + drag & drop of .dxf / .pdf:
  pc_drawing S1..S4 (ingest, normalize, layout, roles) -> one constraint-free Sketch per layer (props `importedCurves=1`,
  `layer`, `source`, `importGroup`, notes JSON), true scale ($INSUNITS / PDF dimension ratio / title block / prompt);
  dimensions + texts = construction curves + labels on "Ölçüler/Notlar". ONE undo step. `Import2D.h/.cpp` (logic),
  `Import2DUi.h/.cpp` (Import2DController, LayersPanel, PlaceViewDialog, LayerCommand).
- Katmanlar tab (`layersTree`, `layersContextMenu`): visibility / colour / line style / rename / new / Yalnız bunu göster /
  Seçimi bu katmana taşı; `Document::layers` (pc/Layers.h, .pcad key "layers"); hidden layer = not drawn, not pickable.
  Viewer: additive `ShapeViewer::setEdgeStyles` (colour + dashed / center / dotted patterns).
- Görünüş seç (`actViewSelect`, one click = connected view) + Görünüşü yerleştir (`actPlaceView`, `import2dPlaceDialog`:
  fold 90° about the edge facing the neighbour view, snap anchor -> hidden axisAngle datum plane, ONE undo step).
- Test: tests/gui/gui_harness_import2d.cpp (ctest gui_harness_import2d). Docs: docs/import2d/IMPORT2D.md.
