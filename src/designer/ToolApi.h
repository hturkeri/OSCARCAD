#pragma once
// PersistCAD designer — SpaceClaim-UX contract between the shell (WP-A) and the tools (WP-B). FROZEN after P0.
//
// Contract version: SC-UX ToolApi 1.0 (P0 bootstrap, 2026-11-15). See docs/spaceclaim_ux/SPEC.md §7.
// ToolApi 1.1 (P0-SC23, 2026-11-18): additive only, items marked "[SC23]" — operation state (OperationState.h),
// IActiveTool::operation()/operationChanged, ToolManager::operationChanged, IDesignHost::showOperationFeedback.
// See docs/sc2023/P0_CONTRACT.md; SC23 change requests use the prefix "SC23:".
// Change requests: docs/CONTRACT_REQUESTS.md, prefix "SC-UX:". Both WPs approve, WP-A applies.
//
// Ownership:
//   * this header + ToolApi.cpp (SelectionModel, small helpers) ............ WP-A
//   * IDesignHost implementation (MainWindow, DesignMode.cpp) ............... WP-A
//   * ToolManager implementation, IActiveTool implementations,
//     appendSketchRegionFaces (src/designer/tools/) ........................ WP-B
//     (P0 ships tools/ToolManager.cpp as a compilable stub; WP-B replaces its content.)
//
// Everything in SPEC §7.1 is kept verbatim (same names, same signatures). Items marked "[P0+]" are additive
// extensions decided during the bootstrap so that neither WP has to edit this file again (see SPEC "Bootstrap
// tamamlandı"). All of them have defaults or are pure virtual on the host side only.
//
// Threading: everything here lives on the GUI thread.
#include "OperationState.h"
#include "pc/Document.h"
#include "pc/Model.h"
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <map>
#include <set>
#include <string>
class QAction; class QKeyEvent; class QMenu; class QWidget;
namespace pc::viewer { class ShapeViewer; }

namespace pcd {
class SketchEditor;

// [P0+] Bumped only through an approved SC-UX change request. Harnesses may assert it.
constexpr int kToolApiVersionMajor = 1;
constexpr int kToolApiVersionMinor = 1;   // [SC23] 1.1

// The three SpaceClaim design modes (SPEC §2.1). Owned by the host (MainWindow::setDesignMode, WP-A).
enum class DesignMode { ThreeD, Sketch, Section };

// ---- selection --------------------------------------------------------------------------------
struct SelItem {
  enum class Kind { Face, Edge, Body, Datum, SketchRegion, SketchCurve, SketchPoint, Sketch, Feature, Parameter };
  Kind kind = Kind::Face;
  QString name;   // Face/Edge: persistent name; Body: body name; Datum/Feature/Sketch: feature id;
                  // SketchRegion "sketch:<sid>:r<k>"; SketchCurve "sketch:<sid>:e<id>"; SketchPoint "sketch:<sid>:p<id>";
                  // Parameter: parameter name
  bool operator==(const SelItem& o) const { return kind == o.kind && name == o.name; }
  bool operator!=(const SelItem& o) const { return !(*this == o); }
};
enum class SelFilter { Smart, Face, Edge, Body, Datum, Sketch };

// [P0+] Display helpers (implemented in ToolApi.cpp), used by the status-bar selection list and the tools' prompts.
QString selKindText(SelItem::Kind k, int count = 1);          // "yüz", "kenar", "gövde", "düzlem", "bölge", ...
QString selectionSummary(const QList<SelItem>& items);        // "1 yüz: Extrude1.end", "3 kenar", "2 öğe"
QString selFilterText(SelFilter f);                           // "Akıllı", "Yüz", "Kenar", "Gövde", "Düzlem", "Sketch"

// The single source of truth for "what is selected" (SPEC §2.11, §7.2). Tools (WP-B) write it; the shell (WP-A)
// listens and is the ONLY owner of selection highlighting in the viewer (setHighlightedFaces/Edges).
class SelectionModel : public QObject {          // implemented in ToolApi.cpp (WP-A)
  Q_OBJECT
public:
  using QObject::QObject;
  const QList<SelItem>& items() const { return items_; }
  bool isEmpty() const { return items_.isEmpty(); }
  int count() const { return items_.size(); }                                  // [P0+]
  bool contains(const SelItem& it) const { return items_.contains(it); }       // [P0+]
  QList<SelItem> ofKind(SelItem::Kind k) const;
  QStringList namesOfKind(SelItem::Kind k) const;                              // [P0+] names of ofKind(k), in order
  void set(const QList<SelItem>& items);         // emits changed() if different; pushes the old one for revert()
  void add(const SelItem& it);                   // no-op (no signal) if already present or rejected by the filter
  void toggle(const SelItem& it);                // Ctrl+click semantics
  void remove(const SelItem& it);                // [P0+] no-op if absent
  void clear();
  void revert();                                 // "Seçimi geri al" (status bar): swaps current and previous selection
  bool canRevert() const { return !prev_.isEmpty() || !items_.isEmpty(); }     // [P0+]
  SelFilter filter() const { return filter_; }
  void setFilter(SelFilter f);                   // emits filterChanged(); does NOT drop already selected items
  bool accepts(SelItem::Kind k) const;           // filter test (Smart accepts every kind; Sketch = region/curve/point/sketch)
signals:
  void changed();
  void filterChanged(pcd::SelFilter f);
private:
  QList<SelItem> items_, prev_;
  SelFilter filter_ = SelFilter::Smart;
};

// ---- tool descriptors (rendered by WP-A: Options panel; by WP-B: mini-toolbar, tool guides) -----
struct ToolOption {
  enum class Type { Bool, Choice, Expr, Text, Action, Info /*[P0+] read-only label, value = text*/ };
  QString key;        // stable, e.g. "merge", "edge", "bothSides", "ruler", "createGroup", "keepDims"
  QString label;      // Turkish UI text, e.g. "Kes (Cut)"
  Type type = Type::Bool;
  QVariant value;     // Bool: bool; Choice: QString key of the chosen item; Expr/Text/Info: QString; Action: unused
  QStringList choices;       // Choice: "key|Etiket (English)" entries
  QString group;      // section title in the Options panel, e.g. "Birleştirme"
  QString tip;
  QString icon;       // pcd::makeIcon kind
  bool enabled = true;
  bool onMiniToolbar = false;
  // [P0+] Expr only: suggested parameter name / unit kind for the field's "Yeni…" (ExprEdit::setParamHint).
  QString paramHint;
  pc::ParamKind paramKind = pc::ParamKind::Length;
};
// One tool guide (vertical icon strip at the right edge of the viewer, SPEC §1.2 #7). Tab cycles them.
struct ToolGuide { QString key; QString label; QString icon; QString tip; bool enabled = true; /*[P0+]*/ };

// [P0+] A pick forwarded from the viewer to the active tool (SPEC §7.2 step 1). The host builds it from the
// viewer signals (facePicked / edgePicked / datumPicked / doubleClicked / tripleClicked) and only forwards it when
// no host-owned special state is pending (sketch-face pick, open FeatureDialog, two-edge plane, ...).
struct PickEvent {
  SelItem item;                         // name == "" -> background click (item.kind is then meaningless)
  Qt::KeyboardModifiers modifiers = Qt::NoModifier;   // Ctrl = add/toggle, Alt = driver (axis) pick, Shift
  Qt::MouseButton button = Qt::LeftButton;
  int clickCount = 1;                   // 1 single, 2 double (edge loop), 3 triple (body)
  QPoint viewerPos;                     // widget pixel in the viewer (or (-1,-1) if unknown)
  bool isBackground() const { return item.name.isEmpty(); }
};

// ---- host (implemented by MainWindow, WP-A) ----------------------------------------------------
// Everything WP-B needs from the main window. WP-B never includes MainWindow.h (SPEC §10.4 #6).
class IDesignHost {
public:
  virtual ~IDesignHost() = default;
  virtual QWidget* hostWindow() = 0;                             // parent for dialogs / message boxes
  // Committed document (what is on the undo stack) and its regenerated model (rollback applied).
  virtual const pc::Document& document() const = 0;
  virtual const pc::model::ModelResult& modelResult() const = 0;
  virtual int rollbackIndex() const = 0;                         // -1 = none
  virtual pc::viewer::ShapeViewer* viewer() const = 0;
  virtual SketchEditor* sketchEditor() const = 0;
  virtual SelectionModel* selection() const = 0;

  // --- modes / sketch session (SPEC §2.1, §2.2) ---
  virtual DesignMode designMode() const = 0;
  virtual bool setDesignMode(DesignMode m) = 0;                  // K/X/D semantics; false if refused/cancelled
  virtual bool beginSketch(const pc::SketchPlane& plane) = 0;    // enters Sketch mode on a new sketch
  virtual bool editSketch(const std::string& sketchFeatureId) = 0;
  virtual bool finishSketch(bool accept) = 0;                    // commit ("Sketch'i Bitir") / discard
  virtual std::string openSketchId() const = 0;                  // "" if not sketching
  // [P0+] "Sketch düzlemi seç" state (K without a usable selection / mini-toolbar "Yeni sketch düzlemi seç").
  // Commits an open sketch first. false if refused (e.g. the commit was cancelled).
  virtual bool beginSketchPlanePick() = 0;
  // [P0+] World frame of the open sketch (offset applied, current parameter values). false if not sketching or
  // the frame cannot be evaluated. Arrays are origin, u, v, normal (unit vectors).
  virtual bool openSketchFrame(double origin[3], double u[3], double v[3], double normal[3]) const = 0;
  // [P0+] Document that expression fields must see: the committed document, or while sketching / while a
  // FeatureDialog is open the host's working copy (same parameters, possibly more). Never null. Pass it to
  // ExprEdit / completers; new parameters created through ExprEdit reach the real document through the
  // host's installParameterCommitHook (v6 behaviour).
  virtual pc::Document* expressionContext() = 0;

  // --- change pipeline: mfg validation + review dialog + one undo entry. false = cancelled/refused. ---
  virtual bool commitDocument(const pc::Document& cand, const QString& undoText) = 0;
  virtual bool insertFeature(const pc::Feature& f, const QString& undoText) = 0;   // at the rollback position
  virtual bool changeParameter(const std::string& name, const std::string& expr) = 0;
  // [P0+] Feature removal / suppression through the existing host flows (dependency warning + confirmation
  // of deleteFeature, toggleSuppress). Used by Fill (SPEC §2.8). false = cancelled/refused/unknown id.
  virtual bool deleteFeatureInteractive(const std::string& featureId) = 0;
  virtual bool setFeatureSuppressed(const std::string& featureId, bool suppressed) = 0;
  // [P0+] Regenerate a candidate WITHOUT committing (no validation, no undo, no viewer change). Used for ghost
  // previews and for computing "Kadar"/ruler distances. Uses the host's caches where possible.
  virtual pc::model::ModelResult evaluateCandidate(const pc::Document& cand) const = 0;
  // [P0+] Evaluate an expression against expressionContext()'s parameters. false + *error on parse/unknown name.
  virtual bool evaluateExpression(const QString& expr, double* value, QString* error = nullptr) const = 0;

  // NewParameterDialog; on OK the parameter is committed (undoable) and its name returned, "" = cancelled
  virtual QString createParameterInteractive(const QString& suggestedName, pc::ParamKind kind,
                                             const QString& valueExpr, QWidget* parent) = 0;

  // --- feedback ---
  virtual void setGhost(const TopoDS_Shape& s) = 0;              // preview (clearGhost with a null shape)
  virtual void showPrompt(const QString& text, int ms = 0) = 0;  // status bar message area
  virtual void showQuickMeasure(const QString& text) = 0;
  // [P0+] Secondary, tool-owned emphasis (e.g. all regions of a multi-region sketch, Combine target/cutter).
  // Drawn by the host in a different colour than the selection; empty lists clear it. The selection highlight
  // itself stays WP-A's (SPEC §7.2 #2).
  virtual void setToolEmphasis(const QStringList& faces, const QStringList& edges) = 0;

  virtual void openClassicEditor(const std::string& featureId) = 0;   // FeatureDialog / sketch edit
  virtual bool featureDialogOpen() const = 0;
  virtual void refreshViewer(bool keepCamera = true) = 0;        // rebuild the viewer compound (regions, overlays)
  // [SC23] Operation feedback (ANALYSIS §4.5, §5.3 #1): HUD message + status bar for Invalid / a non-empty reason.
  // ToolManager::operationChanged is connected to it by the host; tools may also call it directly.
  virtual void showOperationFeedback(const pcd::OpStatus& status) = 0;
};

// ---- tools (implemented by WP-B) --------------------------------------------------------------
// One interactive tool. The ToolManager owns all tools; exactly one is active at a time.
class IActiveTool : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual QString id() const = 0;            // "select","pull","move","fill","combine","plane","shell",
                                             // "sketch.select","sketch.line","sketch.rect","sketch.circle",
                                             // "sketch.arc","sketch.point","sketch.trim"
  virtual QString title() const = 0;         // "Çek (Pull)"  (Options panel title: "Seçenekler - <title>")
  virtual QString prompt() const = 0;        // status-bar instruction for the current step
  virtual QList<ToolGuide> guides() const { return {}; }
  virtual QString activeGuide() const { return {}; }
  virtual void setActiveGuide(const QString&) {}
  virtual QList<ToolOption> options() const { return {}; }
  virtual void setOption(const QString& /*key*/, const QVariant& /*value*/) {}   // Action options: value ignored
  virtual void activate(IDesignHost* host) = 0;
  virtual void deactivate() = 0;
  virtual bool busy() const { return false; } // drag or dimension entry in progress
  virtual bool cancel() { return false; }     // one Esc step; false = nothing left to cancel

  // ---- [P0+] optional hooks (defaults = "not handled") ----
  // Viewer picks forwarded by the host (SPEC §7.2 #1). true = consumed.
  virtual bool pick(const PickEvent& /*e*/) { return false; }
  virtual void hover(const SelItem& /*item*/) {}             // item.name "" = nothing under the cursor
  // Keys routed by ToolManager::installKeyRouting before shortcuts (Tab/Space/digits/Enter/Esc/arrows).
  // wantsKey() answers ShortcutOverride (true = the tool will take this key, the QAction must not fire).
  virtual bool wantsKey(const QKeyEvent* /*e*/) const { return false; }
  virtual bool keyPress(QKeyEvent* /*e*/) { return false; }
  // Ctrl+G "Grup oluştur" while this tool is active (dimension box / last pulled value). true = handled.
  virtual bool createGroup() { return false; }
  // Del while this tool is active (sketch tools delete sketch items). true = handled.
  virtual bool deleteSelection() { return false; }
  // Custom Options-panel content. nullptr (default) = WP-A renders options() generically. A non-null widget is
  // owned by the caller (parent given) and replaces the generic rendering for this tool.
  virtual QWidget* createOptionsWidget(QWidget* /*parent*/) { return nullptr; }
  // Extra entries for the viewer context menu (SPEC §3.10), e.g. "Yuvarlat (Çek)". Added after the host's own.
  virtual void populateContextMenu(QMenu* /*menu*/, const QList<SelItem>& /*underCursor*/) {}
  // The document / model changed under the tool (commit, undo/redo, open, parameter edit). Drop stale refs.
  virtual void documentChanged() {}
  // [SC23] Where the tool's operation stands (ANALYSIS §4.2). Default: Idle with toolId = id(). A tool that
  // overrides it emits operationChanged() whenever the returned value changes.
  virtual pcd::OpStatus operation() const { OpStatus s; s.toolId = id(); return s; }
signals:
  void optionsChanged();
  void guidesChanged();
  void promptChanged(const QString& text);
  void finished();                           // [P0+] the tool wants the manager to fall back to Select
  void operationChanged(const pcd::OpStatus& status);   // [SC23]
};

// Owns the tool actions and the active tool. Created by the host with parent = MainWindow so that every action
// stays findChildren<QAction*>()-visible to the harnesses.
class ToolManager : public QObject {            // implemented in ToolManager.cpp (WP-B)
  Q_OBJECT
public:
  ToolManager(IDesignHost* host, QObject* parent);   // parent = MainWindow (actions stay findChildren-visible)
  ~ToolManager() override;
  // Checkable actions, objectName "tool.<id>", texts as in SPEC §1.3 ("Seç", "Çizgi", "Dikdörtgen", ...).
  // NO shortcuts are set here (WP-A's Shortcuts.cpp assigns them).
  // Non-tool actions use objectName "tool.<key>" as well: "tool.sketch.construction" (Yapı çizgisi, checkable
  // toggle), "tool.sketch.delete" (Sil), "tool.constraint.<SkConstraintType int>", "tool.dimension.<int>".
  QAction* action(const QString& toolId) const;     // nullptr if unknown
  QList<QAction*> editActions() const;        // Seç, Çek, Taşı, Doldur
  QList<QAction*> sketchActions() const;      // Çizgi, Dikdörtgen, Çember, Yay, Nokta, Yapı çizgisi, Kırp
  QList<QAction*> constraintActions() const;  // 11 constraints, texts unchanged
  QList<QAction*> dimensionActions() const;   // 7 dimensions, texts unchanged
  QList<QAction*> otherActions() const;       // Birleştir, Düzlem (tool), Kabuk (tool), "Sil" (sketch)
  QStringList toolIds() const;                // [P0+] every id accepted by activate()
  IActiveTool* active() const;
  QString activeId() const;
  bool activate(const QString& toolId);       // false if unknown or refused (e.g. the current tool is busy and
                                              // could not be cancelled)
  bool cancel();                              // Esc chain; false when already idle in Select
  void installKeyRouting(QWidget* w);         // event filter: Tab/Space/digits/Enter/Esc/Shift+arrows first
  void onDesignModeChanged(pcd::DesignMode m);
  QWidget* createMiniToolbar(QWidget* viewer);     // owned overlays (WP-B)
  QWidget* createToolGuideBar(QWidget* viewer);

  // ---- [P0+] host -> tools forwarding (see IActiveTool hooks) ----
  bool routeKey(QKeyEvent* e);                // same path as the event filter; true = consumed
  bool handlePick(const PickEvent& e);        // true = consumed by the active tool
  void handleHover(const SelItem& item);
  bool createGroup();                         // Ctrl+G; false = not handled by a tool (WP-A: Gruplar panel)
  bool deleteSelection();                     // Del;    false = not handled by a tool (WP-A: 3D feature delete)
  void populateContextMenu(QMenu* menu, const QList<SelItem>& underCursor);
  void onDocumentChanged();                   // host calls after every applyDocument (commit/undo/redo/open)
  QWidget* createOptionsWidget(QWidget* parent);   // active tool's custom options widget or nullptr
  void setOption(const QString& key, const QVariant& value);   // forwards to active()->setOption
  void cycleGuide(int step = 1);              // Tab / Shift+Tab when no dimension box is open
signals:
  void activeToolChanged(const QString& id);
  void optionsChanged();                      // active tool's options changed (Options panel re-renders)
  void promptChanged(const QString& text);
  void guidesChanged();                       // [P0+] active tool's guides / active guide changed
  // [SC23] The active tool's IActiveTool::operationChanged, forwarded (connected on activation, disconnected on
  // deactivation). Also emitted once with the new tool's operation() right after activeToolChanged.
  void operationChanged(const pcd::OpStatus& status);
private:
  struct Impl; Impl* d;
};

// ---- WP-B free functions called by WP-A --------------------------------------------------------
// Adds pickable sketch-region faces ("sketch:<sid>:r<k>") of the given (not consumed / explicitly shown) sketches.
// Returns the number of faces added. Region faces are display-only (not in mfg checks / export).
int appendSketchRegionFaces(const pc::Document& doc, const pc::model::ModelResult& model,
                            const std::set<std::string>& sketchIds, TopoDS_Compound& comp,
                            std::map<std::string, TopoDS_Face>& faceNames);
} // namespace pcd

Q_DECLARE_METATYPE(pcd::SelItem)
Q_DECLARE_METATYPE(pcd::SelFilter)
Q_DECLARE_METATYPE(pcd::DesignMode)
