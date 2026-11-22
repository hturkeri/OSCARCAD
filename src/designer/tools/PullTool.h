#pragma once
// SC-UX (WP-B) — Çek (Pull), SPEC §2.3–§2.5, §6.4.
// Idle -> (selection: region | face | edge) -> Armed (yellow arrow) -> drag (ghost, live value) / type (dimension box)
// -> Enter or 600 ms after release -> one commit through the host pipeline -> Armed again on the result face.
// Alt+click on a sketch line (or the Döndür guide) turns a region pull into a Revolve about that line.
#include "DimensionBox.h"
#include "PullMapping.h"
#include "ToolBase.h"
#include "ValueInput.h"
#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>
#include <gp_Ax1.hxx>

namespace pcd {

class PullTool : public ToolBase {
  Q_OBJECT
  // SC23 (O-P1-4): secondary references of the operation ("Döndürme ekseni: sketch:S:e3", "Kadar hedefi: ...").
  // OpStatus is frozen without such a field: readers re-read this property on every operationChanged().
  Q_PROPERTY(QStringList secondaryRefs READ secondaryRefs)
public:
  explicit PullTool(QObject* parent);
  QString id() const override { return "pull"; }
  QString title() const override { return "Çek (Pull)"; }
  QList<ToolGuide> guides() const override;
  QString activeGuide() const override { return guide_; }
  void setActiveGuide(const QString& g) override;
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool busy() const override { return st_ == St::Dragging || st_ == St::Typing || st_ == St::Pending; }
  bool cancel() override;
  bool pick(const PickEvent& e) override;
  void hover(const SelItem& item) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool createGroup() override;
  void documentChanged() override;
  void populateContextMenu(QMenu* menu, const QList<SelItem>& underCursor) override;

  void paintOverlay(QPainter& p, const ViewportOverlay& o) override;
  int handleCount() const override;
  bool viewerMousePress(QMouseEvent* e) override;
  bool viewerMouseMove(QMouseEvent* e) override;
  bool viewerMouseRelease(QMouseEvent* e) override;

  // test / automation helpers
  const pull::Target& target() const { return tgt_; }
  DimensionBox* dimensionBox() const { return box_; }
  bool commitValue(const QString& text, bool absolute = false);
  double dragValue() const { return dragValue_; }
  QPointF handleTip() const;      // widget pixel of the arrow head (or of the revolve arc end)
  QPointF handleBase() const;
  // SC-PULLUPTO: Kadar (Up To) / Cetvel (Ruler) guides (docs/spaceclaim_ux/PULL_UPTO.md)
  QString upToRef() const { return upToRef_; }        // the clicked target ("" = none)
  double upToDistance() const { return upToS_; }      // its signed distance from the pulled plane
  QString resolveUpToRef(const PickEvent& e, QString* why) const;   // pick -> pc/UpTo.h reference
  bool pickUpToTarget(const PickEvent& e);            // preview (Up To) / ruler value box (Ruler)
  bool commitUpTo(const QString& offsetExpr = QString());
  // SC23 (O-P0-2, O-P1-4)
  QStringList secondaryRefs() const;
  bool previewHeld() const { return held_; }          // auto-commit stopped by a focus / window change
  bool autoCommitPending() const { return commitTimer_.isActive(); }
  ValueInput::Options valueOptions() const;           // quantity / limits of the current field
  bool eventFilter(QObject* o, QEvent* e) override;

protected:
  void onActivate() override;
  void onDeactivate() override;

private:
  enum class St { Idle, Armed, Dragging, Typing, Pending };
  void retarget();
  void computeHandle();
  void openBox(const QString& initial, int field = 0);
  void closeBox();
  void updateGhost(double value, bool force = false);
  void clearGhost();
  void armedPrompt();
  bool nearArrow(const QPointF& p, bool* onHead) const;
  bool arrowGeometry(QPointF* base, QPointF* tip, QPointF* unit, double* ppm) const;
  double stepFor(double ppm) const;
  void pruneSelection();
  bool commitResult(const pull::Result& r);           // SC-PULLUPTO: shared commit of Pull results
  void clearUpTo();
  void previewResult(const pull::Result& r);
  // SC23
  void reportState();                                 // OpStatus from st_ / tgt_ / errReason_ (ANALYSIS §4.2)
  void refuse(const QString& msg);                    // Invalid + prompt + status (no commit)
  void holdPreview();                                 // focus / window lost: stop the 600 ms timer, keep the ghost
  void dropPreviewForExternalChange();                // undo / redo / parameter edit during a preview

  St st_ = St::Idle;
  pull::Options opt_;
  pull::Target tgt_;
  QString guide_ = "select";
  bool applyOnRelease_ = false;
  bool haveHandle_ = false, haveAxis_ = false;
  gp_Pnt anchor_;
  gp_Dir dir_;
  gp_Ax1 axis_;
  double flip_ = 1.0;
  bool pressed_ = false, dragged_ = false, pressOnHead_ = false, hot_ = false;
  QPoint pressPos_;
  double dragValue_ = 0;
  QElapsedTimer ghostClock_;
  DimensionBox* box_ = nullptr;
  QString boxLabel_;          // "label|unit" the box fields were last built for (drag must not show a stale "Açı")
  QTimer commitTimer_;
  QString lastFid_, lastProp_, lastValue_;
  QMetaObject::Connection selConn_;
  bool inRetarget_ = false;
  QString upToRef_;          // SC-PULLUPTO
  double upToS_ = 0;
  // SC23
  QString errReason_;        // last refusal (Invalid) until the next action
  bool held_ = false;        // preview kept, auto-commit stopped (focus / window lost)
  bool selfChange_ = false;  // our own commit / parameter creation is changing the document
  LargeValueGate gate_;
  QPointer<QWidget> hostWin_;
  QMetaObject::Connection focusConn_;
  QStringList lastSecondary_;
};

} // namespace pcd
