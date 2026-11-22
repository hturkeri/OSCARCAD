#pragma once
// SC-UX (WP-B) — Oluştur > Düzlem (Plane), SPEC §2.10: planar face -> faceOffset, one straight edge -> edgeNormal
// (t = 0.5), two straight edges -> twoEdges. The new plane stays selected and the tool switches to Move.
#include "ToolBase.h"

namespace pcd {

class PlaneTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "plane"; }
  QString title() const override { return "Düzlem (Plane)"; }
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool pick(const PickEvent& e) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool createFromSelection();

protected:
  void onActivate() override;

private:
  QString kind_ = "auto";
  bool show_ = true;
};

} // namespace pcd
