#pragma once
// SC-UX (WP-B) — internal base class of the WP-B tools (NOT part of the frozen ToolApi.h contract).
// Adds what ToolManager needs beyond IActiveTool: handle drawing on the ViewportOverlay, mouse events on the viewer
// (handle drags happen before the viewer sees the press), shared prompt / selection helpers.
#include "ToolApi.h"
#include <QPointF>
#include <QPointer>
#include <functional>
#include <QString>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

class QMouseEvent;
class QPainter;

#include "ViewportOverlay.h"

namespace pcd {
class DimensionBox;

class ToolBase : public IActiveTool {
  Q_OBJECT
public:
  explicit ToolBase(QObject* parent) : IActiveTool(parent) {}
  void activate(IDesignHost* host) override;
  void deactivate() override;
  QString prompt() const override { return prompt_; }
  // SC23 (ToolApi 1.1, ANALYSIS §4.2): the tool's operation state. Tools that track it call setOperation().
  OpStatus operation() const override;

  // ---- WP-B internal hooks
  virtual void paintOverlay(QPainter& /*p*/, const ViewportOverlay& /*o*/) {}
  virtual int handleCount() const { return 0; }
  virtual bool viewerMousePress(QMouseEvent* /*e*/) { return false; }
  virtual bool viewerMouseMove(QMouseEvent* /*e*/) { return false; }
  virtual bool viewerMouseRelease(QMouseEvent* /*e*/) { return false; }
  virtual bool isSketchTool() const { return false; }
  void setOverlay(ViewportOverlay* o) { overlay_ = o; }
  IDesignHost* host() const { return host_; }

  // "sketch:S:e3" picked as an Edge -> SketchCurve, "sketch:S:r0" as a Face -> SketchRegion, ...
  static SelItem normalize(const SelItem& it);
  DimensionBox* valueBox() const { return vbox_; }
  // Default selection semantics (SPEC §2.11): click = select, Ctrl = toggle, background = clear,
  // double click on an edge = loop of the adjacent face, triple click = body. Honours the selection filter.
  bool selectFromPick(const PickEvent& e, bool loops = true);

signals:
  void requestTool(const QString& id);       // ask the manager to switch tools (e.g. Plane -> Move)

protected:
  // One-field value entry (Shell thickness, Move distance, ...): opens a DimensionBox next to `at`, Enter calls
  // onCommit(text). Esc closes it (cancelValueBox()).
  void openValueBox(const QString& label, const QString& unit, const QString& initial, double live, const QPoint& at,
                    std::function<void(const QString&)> onCommit);
  bool valueBoxOpen() const;
  bool cancelValueBox();
  bool forwardToValueBox(QKeyEvent* e);
  static bool startsValue(const QKeyEvent* e);   // digit . - ( or Space
  virtual void onActivate() {}
  virtual void onDeactivate() {}
  void setPrompt(const QString& s);
  // SC23: sets the operation state and emits operationChanged() when it differs. Completed / Cancelled are
  // momentary: emitMomentary() sends them and then re-sends the current (lasting) state.
  void setOperation(OpState st, const QString& reason = QString(), const QString& value = QString(), const QString& unit = QString());
  void emitMomentary(OpState st, const QString& reason);
  OpStatus op_;
  void repaintOverlay();
  QPointF toScreen(const gp_Pnt& p) const;
  bool screenDir(const gp_Pnt& p, const gp_Dir& d, QPointF* base, QPointF* unit, double* pxPerMm) const;
  IDesignHost* host_ = nullptr;
  QPointer<ViewportOverlay> overlay_;
  QString prompt_;
  DimensionBox* vbox_ = nullptr;
  std::function<void(const QString&)> vboxCommit_;
};

} // namespace pcd
