# SC23 P0 — ortak kancalar (frozen contract)

P0-SC23 bootstrap, 2026-11-18. Base for the two parallel work packages **WP-S** (shell/layout) and **WP-O**
(operation/state), see `ANALYSIS.md` §4.2, §4.5, §5.2, §5.3. Everything below is **frozen**: changes are requested in
`docs/CONTRACT_REQUESTS.md` with the prefix **"SC23:"** and approved by both WPs.

## 1. `src/designer/OperationState.h` (new, header only)
```cpp
namespace pcd {
enum class OpState { Idle, AwaitingSelection, AwaitingInput, Preview, Invalid, Committing, Completed, Cancelled };
struct OpStatus { OpState state = OpState::Idle; QString toolId, reason, valueText, unit;
                  bool operator==(const OpStatus&) const; bool operator!=(const OpStatus&) const; };
QString opStateText(OpState);   // inline, Turkish short label, never empty ("Boşta", "Geçersiz", ...)
}
Q_DECLARE_METATYPE(pcd::OpState)  Q_DECLARE_METATYPE(pcd::OpStatus)
```
Completed / Cancelled are momentary; afterwards the tool reports AwaitingInput or AwaitingSelection again.

## 2. `src/designer/ToolApi.h` → ToolApi 1.1 (additive, items marked `[SC23]`)
* `kToolApiVersionMinor = 1` (major stays 1). `ToolApi.h` includes `OperationState.h`.
* `IActiveTool` (a QObject):
  * `virtual pcd::OpStatus operation() const;` — default `{Idle, toolId = id()}`.
  * signal `void operationChanged(const pcd::OpStatus&);` — a tool that overrides `operation()` emits it on change.
* `ToolManager`: signal `void operationChanged(const pcd::OpStatus&);` — forwards the active tool's signal
  (connected in `activate()` together with promptChanged/optionsChanged/guidesChanged, disconnected when the tool
  is deactivated) and is emitted once with `next->operation()` at the end of every successful `activate()`.
* `IDesignHost`: `virtual void showOperationFeedback(const pcd::OpStatus&) = 0;`
  * `MainWindow` implementation in `DesignMode.cpp` (P0 minimal, WP-O refines): Invalid or a non-empty reason →
    `statusBar()->showMessage(reason)` (Invalid without reason → `opStateText`); HUD: Invalid → Error (persistent until
    the next non-Invalid status), Completed/Cancelled with reason → Info 2 s, other states with reason → Info.
  * Test hosts updated: `tests/gui/sc_stub_host.h` (`lastOperation`), `tests/gui/gui_harness_sc_tools.cpp` (`lastOp`).
* Data flow (§5.3 #1): tool `operationChanged` → `ToolManager::operationChanged` → `MainWindow::showOperationFeedback`
  (connected in `MainWindow::buildHud()`).

## 3. `src/designer/ViewportHud.h/.cpp` (new; API frozen, `.cpp` owned by WP-S)
```cpp
class ViewportHud : public QWidget {            // child of the viewer, objectName "viewportHud"
  enum class Severity { Info, Warning, Error };
  explicit ViewportHud(QWidget* viewer);
  void setToolText(const QString&);  void setSelectionText(const QString&);
  void showMessage(const QString&, Severity = Severity::Info, int ms = 0);  void clearMessage();
  QRect guideArea() const;                      // viewer coordinates, below tool line + one message line
  QLabel* messageLabel() const;                 // objectName "viewportHud.message"
  QString toolText() const, selectionText() const, messageText() const;  Severity messageSeverity() const;
signals: void layoutChanged();
};
```
* objectNames: `viewportHud` (HUD), `viewportHud.message` (message strip), `viewportHud.tool` (tool/selection label).
* Top-left, semi-transparent; line 1 = `"<tool> · <selection>"`; follows the viewer size through an event filter.
* Mouse: the HUD body has `WA_TransparentForMouseEvents`. Qt skips the whole subtree of such a widget, so the message
  strip is a **sibling** (child of the viewer, created/positioned/deleted by the HUD) and does take the mouse. Find it
  with `hud->messageLabel()` or `viewer->findChild<QLabel*>("viewportHud.message")`.
* Wiring (`MainWindowShell.cpp`, WP-S): `buildHud()` creates `hud_` in the constructor after `wireSelection()`;
  `activeToolChanged` → `updateHudToolText()` (title before " (": "Çek"), `SelectionModel::changed` →
  `updateHudSelectionText()` (`selectionSummary()` before ':': "1 yüz", "3 kenar", "1 bölge").

## 4. `src/designer/MainWindow.h` (frozen block "SC23 FROZEN")
* Forward declarations: `ViewportHud`, `DocumentTabBar`, `ViewsPanel` (the last two are **not** created; WP-S adds them).
* Public: `void showOperationFeedback(const pcd::OpStatus&) override;`, `ViewportHud* viewportHud() const;`,
  `DocumentTabBar* documentTabs() const;`, `ViewsPanel* viewsPanel() const;`
* Private: `void buildHud(); void updateHudToolText(); void updateHudSelectionText();`,
  `ViewportHud* hud_; DocumentTabBar* docTabs_ = nullptr; ViewsPanel* viewsPanel_ = nullptr; QAction* actViewTrimetric_ = nullptr;`

## 5. `MainWindow::rebuildStructure()` → `src/designer/MainWindowStructure.cpp` (unchanged body, WP-O)
No CMake edit: `src/designer/sources.cmake` globs `src/designer/*.cpp` (CONFIGURE_DEPENDS).

## 6. `Icons.cpp` stubs (WP-S afterwards)
`surface`, `curve`, `solid`, `trimetric`, `document`, `hud.error` added; `views` already existed and is reused.

## 7. Tests
* `tests/gui/gui_harness_sc2023.cpp` (shared, frozen): real `MainWindow` 1240×800, `check/note/section/pump/waitFor/shot`,
  `Sc23Ctx { MainWindow& w; QString tmpDir; tools(); hud(); openSample(); reset(); }`, `--shots` →
  `docs/sc2023/shots/` (`PC_SC23_SHOTS_DIR`). Includes:
  * `tests/gui/sc2023_shell_checks.inc` → `void runShellChecks(Sc23Ctx&)` (WP-S)
  * `tests/gui/sc2023_op_checks.inc` → `void runOpChecks(Sc23Ctx&)` (WP-O)
* Registered in `tests/gui/CMakeLists.txt` with `pc_add_gui_harness(gui_harness_sc2023 ...)` (xvfb-run; RESOURCE_LOCK
  on WIN32 through the shared function).
* `tests/test_value_input.cpp` (gtest, links `pc_designer`, registered like `test_pull_face`): placeholder; WP-O fills it.

## 8. File ownership after P0 (ANALYSIS §5.2)
**WP-S "kabuk/yerleşim"**
* Existing: `Ribbon.h/.cpp`, `MainWindowShell.cpp` (`buildRibbon/buildDocks/buildStatusBar/buildActions`, `buildHud`),
  `MainWindow.cpp` (central container, `updateTitle`), `StatusBarWidgets.*`, `SelectionPanel.*`, `OptionsPanel.*`
  (presentation), `Shortcuts.*`, `Icons.*`, `MainWindowV2.cpp` (`viewerContextMenu`), `MainWindowVis.cpp`,
  `tools/ToolGuideBar.*`, `tools/MiniToolbar.*`, `src/viewer/pc/ShapeViewer.*` (**additive only**: `View::Trimetric`).
* New: `ViewportHud.cpp`, `DocumentTabBar.*`, `ViewsPanel.*`, `HelpViewer.*`, `sc2023_shell_checks.inc`.
* Tests to update: `gui_harness_sc_shell.cpp`, `gui_harness_qa.cpp`, `gui_harness_v4.cpp`,
  `gui_harness_colors/visibility/video2d` (`setCurrentTab("display")` kept).

**WP-O "işlem/durum"**
* Existing: `tools/PullTool.*`, `PullMapping.*`, `DimensionBox.*`, `ToolManager.cpp`, `ToolBase.*`,
  `SketchToolAdapter.*`, `SketchRegions.*`, `ViewportOverlay.*`, `SelectTool.*`, `MoveTool.*`, `FillTool.*`,
  `CombineTool.*`, `ShellTool.*`, `PlaneTool.*`; `SketchEditor.*`, `MainWindowOps.cpp` (`tryCommit`, `finishSketch`),
  `DesignMode.cpp` (incl. `showOperationFeedback`), `StructurePanel.*`, `PropertiesPanel.*`.
* New: `MainWindowStructure.cpp`, `tools/ValueInput.*`, `tests/test_value_input.cpp`, `sc2023_op_checks.inc`.
* Pull API to keep: `PullTool::target()`, `dimensionBox()`, `commitValue()`, 600 ms auto-commit, "Çek"-prefixed prompts.

**Frozen / shared**: `OperationState.h`, `ToolApi.h`, `ViewportHud.h`, `MainWindow.h`, `gui_harness_sc2023.cpp`,
`docs/KULLANIM_KILAVUZU.md` (WP-S §2, WP-O §4–§5).
**Not touched**: `src/core`, `src/sketch`, `src/model`, `src/mfg`, `src/apppkg`, `src/appui`, `src/runner`,
`ParamSelect*`, `Import2D*`, `MainWindowNs.cpp`, `MainWindowColors.cpp`, `FeatureDialog*`, `ExprEdit*`,
`tests/gui/harness_robust.h`.
