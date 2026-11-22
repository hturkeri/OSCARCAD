#pragma once
// SC-UX (WP-B) — Doldur (Fill), SPEC §2.8: the parametric meaning of "fill" is undoing the feature that made the
// selected face: a fillet / chamfer face drops its edge from the feature (empty -> feature deleted), a cut
// Extrude / Revolve is suppressed (or deleted), a single-location hole is deleted, a shell face is closed again.
// Click the face(s), Enter applies (F with a face already selected + Enter works too).
#include "ToolBase.h"
#include <gp_Pnt.hxx>
#include <string>
#include <vector>

namespace pcd {

class FillTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "fill"; }
  QString title() const override { return "Doldur (Fill)"; }
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool pick(const PickEvent& e) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool cancel() override;
  bool applyFill();                       // Enter
  QString describe(const QString& face) const;   // info line for the Options panel
  // SC-PULLUPTO (docs/spaceclaim_ux/PULL_UPTO.md): sketch curves / regions / a sketch -> its closed loops become
  // selectable regions (tolerant: gaps, T-junctions, overlaps, nested loops); open loops -> the gaps are marked.
  // Model edges bounding a hole of a body -> the hole is patched.
  bool fillSketch(const std::string& sid, QString* msg, QList<SelItem>* regions);
  bool fillEdges(const QStringList& edges, QString* msg);
  const std::vector<gp_Pnt>& gapMarkers() const { return gaps_; }
  void paintOverlay(QPainter& p, const ViewportOverlay& o) override;

protected:
  void onActivate() override;
  void onDeactivate() override;

private:
  bool fillFace(const QString& face, QString* msg);
  QString mode_ = "suppress";            // cut features: suppress | remove
  QString gapTol_;                        // SC-PULLUPTO: "" = the sketch's own / automatic tolerance
  std::vector<gp_Pnt> gaps_;              // open ends shown in the view
  QStringList fillSketches() const;       // sketches referenced by the selection
  QMetaObject::Connection selConn_;
};

} // namespace pcd
