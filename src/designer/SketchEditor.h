#pragma once
// 2D sketch editor widget (QPainter): drawing tools, snapping, constraints, dimensions, live solving.
//
// SC-UX (WP-B, SPEC §5, §6.1): two display modes.
//  * standalone (default, legacy): an opaque 2D canvas with its own pan/zoom (org_/scale_).
//  * overlay: attachToViewer(viewer) makes the editor a transparent child covering the 3D viewer. The sketch plane
//    frame (setPlaneFrame) and the viewer camera give an exact affine plane->screen transform (orthographic view),
//    so the user can spin the model (middle drag / wheel are forwarded to the viewer) and keep drawing on the plane.
// Both modes share one transform (planeToScreen()); every drawing tool shows on-the-fly dimension fields
// (DimensionBox): type a number (or Space), Tab locks the field and moves to the next, Enter creates the element,
// Esc steps back. Typed (locked) values become driving dimensions ("Yazılan ölçüleri kısıt olarak sakla").
#include "pc/Document.h"
#include "pc/ShapeViewer.h"
#include "pc/SketchSolver.h"
#include <QPointer>
#include <QSizeF>
#include <QTransform>
#include <QWidget>
#include <functional>
#include <vector>

namespace pcd {
class DimensionBox;

class SketchEditor : public QWidget {
  Q_OBJECT
  // SC-UX test hooks (read-only)
  Q_PROPERTY(QSizeF previewSize READ previewSize)
  Q_PROPERTY(int drawStage READ drawStage)
  Q_PROPERTY(bool dimensionEditing READ dimensionEditing)
public:
  enum class Tool { Select, Point, Line, Rect, Circle, Arc, Trim /*SC-UX: appended only*/ };

  explicit SketchEditor(QWidget* parent = nullptr);

  // `work` provides parameters (for dimension expressions / solving). Not owned.
  void setContext(pc::Document* work) { work_ = work; reloadParams(); }
  void reloadParams();
  void setSketch(const pc::Sketch& s, const QString& title);
  const pc::Sketch& sketch() const { return sk_; }
  bool modified() const { return modified_; }

  void setTool(Tool t);
  Tool tool() const { return tool_; }
  void setConstructionMode(bool on) { construction_ = on; }
  bool constructionMode() const { return construction_; }

  void applyConstraint(pc::SkConstraintType t);   // uses the current selection
  void applyDimension(pc::SkConstraintType t);    // asks for an expression
  void deleteSelection();
  void toggleConstructionOfSelection();
  void undo();
  void redo();
  void fitView();
  const pc::sketch::SolveResult& lastSolve() const { return solve_; }
  int dof() const { return solve_.dof; }
  QPointF pointScreenPos(int id) const { return ptScreen(id); }
  void resolve();
  // v6: plane offset (expression) of the edited sketch; marks the sketch modified.
  void setPlaneOffset(const QString& expr);
  // v6: keep the open sketch consistent with parameter-table renames.
  void renameParameter(const std::string& from, const std::string& to);
  bool referencesParameter(const std::string& name) const;
  void ensureVisible();   // re-fit the view if solved geometry left it

  // ---- SC-UX (WP-B) ----
  void attachToViewer(pc::viewer::ShapeViewer* viewer);      // overlay mode; nullptr = standalone
  pc::viewer::ShapeViewer* attachedViewer() const { return viewer_; }
  void setPlaneFrame(const double o[3], const double u[3], const double v[3]);   // world frame of the sketch plane
  bool planeFrame(double o[3], double u[3], double v[3]) const;
  QTransform planeToScreen() const { return T_; }            // sketch (u,v) -> widget pixel
  QPointF sketchToScreen(double u, double v) const { return toScreen(u, v); }
  QPointF screenToSketch(const QPointF& px) const { return toSketch(px); }
  void planView();                                           // V: look at the plane, v up (overlay mode)
  bool edgeOn() const { return edgeOn_; }                    // plane seen edge-on: drawing input disabled
  QSizeF previewSize() const;                                // size of the element being drawn (sketch units)
  int drawStage() const { return drawStage_; }
  bool dimensionEditing() const;
  DimensionBox* dimensionBox() const { return box_; }
  bool hasSelection() const { return !sel_.empty(); }
  void clearSelection();
  std::vector<int> selectedIds() const { return sel_; }
  void selectIds(const std::vector<int>& ids);
  // Keyboard (also used by ToolManager's key routing): digits / . - ( / Space open the dimension fields, Tab cycles,
  // Enter completes, Ctrl+arrows move the grid. true = consumed.
  bool wantsKey(const QKeyEvent* e) const;
  bool handleKey(QKeyEvent* e);
  bool cancelStep();                // one Esc step inside the tool: dimension edit -> drawing stage. false = nothing
  bool createGroupFromBox();        // Ctrl+G: the edited / live field becomes a new parameter
  using ParameterRequest = std::function<QString(const QString& suggestedName, pc::ParamKind kind, const QString& valueExpr)>;
  void setParameterRequest(ParameterRequest fn) { paramRequest_ = std::move(fn); }
  // ---- "Seçimden parametre" (ParamSelectUi) ----
  // Replaces the sketch with `s` (already solved) as ONE sketch-local undo step. `createdParams` are the parameters
  // created together with this step: undoing the step emits parametersUndone (the host removes them from the
  // document), redoing it emits parametersRedone.
  void applyExternalEdit(const pc::Sketch& s, const std::vector<pc::Parameter>& createdParams, const QString& message);
  // Screen anchor (widget pixel) of the current selection: line / arc middle, circle rim, point, dimension label.
  bool selectionAnchor(QPointF* px) const;
  QRectF dimensionLabelRect(int constraintId) const;   // last painted label rectangle (empty if not shown)
  // options (SPEC §8 "Sketch araçları")
  void setKeepTypedDims(bool on) { keepDims_ = on; }
  bool keepTypedDims() const { return keepDims_; }
  void setSnapToGrid(bool on) { snapGrid_ = on; }
  bool snapToGrid() const { return snapGrid_; }
  void setAutoHV(bool on) { autoHV_ = on; }
  bool autoHV() const { return autoHV_; }
  void setChainLines(bool on) { chain_ = on; }
  bool chainLines() const { return chain_; }
  void setRectFromCenter(bool on) { fromCenter_ = on; }
  bool rectFromCenter() const { return fromCenter_; }
  void setGridStepOverride(double mm) { gridOverride_ = mm; update(); }   // <= 0: automatic
  double currentGridStep() const { return gridStep(); }
  void moveGrid(int steps);         // Ctrl+Right / Ctrl+Left: plane offset +/- one grid step
  QString sketchId() const;         // first word of the title ("Sketch2")

signals:
  void toolChanged(int tool);
  void statusMessage(const QString& msg);
  void solved(int dof, const QString& message, bool ok);
  void changed();
  void paramsChanged();   // a new parameter was created through a dimension dialog
  void drawStageChanged(int stage);                 // SC-UX
  void planeOffsetChanged(const QString& expr);     // SC-UX: moveGrid changed the plane offset
  void selectionChanged();                          // Seçimden parametre: the sketch selection changed
  void contextMenuRequested(const QPoint& globalPos);   // right click with Seç (the item under the cursor is selected)
  void parametersUndone(const QStringList& names);
  void parametersRedone(const std::vector<pc::Parameter>& params);

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  bool event(QEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;

private:
  struct Snap {
    QPointF p;          // sketch coords
    int pointId = -1;   // existing point snapped to
    int kind = 0;       // 0 none, 1 point, 2 grid, 3 horizontal, 4 vertical
  };
  struct Hit { int id = -1; int kind = 0; };  // kind: 1 point 2 entity 3 constraint label

  QPointF toScreen(double u, double v) const { return T_.map(QPointF(u, v)); }
  QPointF toSketch(const QPointF& s) const { return Ti_.map(s); }
  double pxPerMm() const;                // local scale of the transform (sqrt |det|)
  void updateTransform();                // standalone: from org_/scale_; overlay: from the viewer camera
  void syncViewerGrid();
  // SC-UX on-the-fly dimensions
  void setupFieldsForStage();
  void updateLiveFields();
  QPointF constrainedCursor(QPointF c) const;   // cursor with locked field values applied (sketch coords)
  bool commitDrawing(bool fromKeyboard);        // Enter / click with locks: creates the element
  void applyTypedDims(const std::vector<std::pair<pc::SkConstraintType, std::pair<std::vector<int>, std::string>>>& dims);
  int rectFirstPt_ = -1;
  std::string arcRadiusExpr_;
  QPointF ptScreen(int id) const;
  double gridStep() const;
  Snap snap(const QPointF& screen, int refPoint = -1) const;
  Hit hitTest(const QPointF& screen) const;
  int ensurePoint(const Snap& s);        // create point if not snapped
  void pushUndo();
  void finishDrawing();
  void afterEdit(bool doSolve = true);
  bool addConstraintChecked(pc::SkConstraintType t, std::vector<int> refs, const std::string& expr = "");
  double currentValue(pc::SkConstraintType t, const std::vector<int>& refs) const;
  void removeIds(const std::vector<int>& ids);
  bool isSelected(int id) const;
  QString dimLabel(const pc::SkConstraint& c) const;

  pc::Document* work_ = nullptr;
  pc::ParamValues params_;
  pc::Sketch sk_;
  pc::sketch::SolveResult solve_;
  QString title_;
  bool modified_ = false;

  double scale_ = 4.0;
  QPointF org_{300, 300};
  bool viewInit_ = false;

  Tool tool_ = Tool::Select;
  bool construction_ = false;
  std::vector<int> sel_;
  int hover_ = -1;
  QPointF mouse_;       // screen
  Snap curSnap_;
  // drawing state
  int drawStage_ = 0;
  int lastPt_ = -1, firstPt_ = -1, centerPt_ = -1, startPt_ = -1;
  QPointF rectStart_;
  // dragging
  bool panning_ = false;
  QPoint lastMouse_;
  bool dragging_ = false;
  std::vector<int> dragPts_;
  std::vector<QPointF> dragOffs_;
  int dragCircle_ = -1;
  pc::Sketch dragBackup_;
  QPoint pressPos_;
  Hit pressHit_;
  // dimension label hit regions (rebuilt on paint)
  mutable std::vector<std::pair<QRectF, int>> labelHits_;

  struct UndoStep {
    pc::Sketch sk;                         // sketch BEFORE the step (undo_) / AFTER it (redo_)
    std::vector<pc::Parameter> params;     // parameters created by the step (Seçimden parametre)
  };
  std::vector<UndoStep> undo_, redo_;

  // SC-UX state
  QPointer<pc::viewer::ShapeViewer> viewer_;
  QMetaObject::Connection camConn_;
  double fo_[3] = {0, 0, 0}, fu_[3] = {1, 0, 0}, fv_[3] = {0, 1, 0};
  bool haveFrame_ = false;
  QTransform T_, Ti_;
  bool edgeOn_ = false;
  DimensionBox* box_ = nullptr;
  ParameterRequest paramRequest_;
  bool keepDims_ = true, snapGrid_ = true, autoHV_ = true, chain_ = true, fromCenter_ = false;
  double gridOverride_ = 0;
  QPointF cursorSk_;     // last cursor position (sketch coords, snapped)
};

} // namespace pcd
