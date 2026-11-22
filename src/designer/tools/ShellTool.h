#pragma once
// SC-UX (WP-B) — Kabuk (Shell): click the faces to open (Ctrl adds), type the wall thickness, Enter -> Shell feature.
#include "ToolBase.h"

namespace pcd {

class ShellTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "shell"; }
  QString title() const override { return "Kabuk (Shell)"; }
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool busy() const override { return valueBoxOpen(); }
  bool pick(const PickEvent& e) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool cancel() override { return cancelValueBox(); }
  bool apply(const QString& thickness);

protected:
  void onActivate() override;

private:
  QString thickness_ = "2";
  QString direction_ = "inward";
};

} // namespace pcd
