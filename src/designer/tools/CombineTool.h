#pragma once
// SC-UX (WP-B) — Birleştir (Combine), SPEC §2.9: guides "Hedef seç" -> "Kesici seç"; the mini-toolbar offers
// Çıkar (cut) / Kesişim (intersect) / Birleştir (join); Enter applies the chosen operation (default join).
#include "ToolBase.h"
#include <QStringList>

namespace pcd {

class CombineTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "combine"; }
  QString title() const override { return "Birleştir (Combine)"; }
  QList<ToolGuide> guides() const override;
  QString activeGuide() const override { return target_.isEmpty() ? "target" : "cutter"; }
  void setActiveGuide(const QString& g) override;
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool pick(const PickEvent& e) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool cancel() override;
  bool apply(const QString& op);
  QString target() const { return target_; }
  QStringList cutters() const { return cutters_; }

protected:
  void onActivate() override;
  void onDeactivate() override;

private:
  void emphasize();
  void updatePrompt();
  QString target_;
  QStringList cutters_;
  QString op_ = "join";
  bool keepTools_ = false, largestOnly_ = false;
};

} // namespace pcd
