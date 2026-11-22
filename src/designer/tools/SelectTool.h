#pragma once
// SC-UX (WP-B) — Seç (Select), the 3D selection tool (SPEC §2.11): click / Ctrl+click / double click (edge loop) /
// triple click (body), selection filter, Ctrl+A (all of the same kind), hover pre-highlight.
#include "ToolBase.h"

namespace pcd {

class SelectTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "select"; }
  QString title() const override { return "Seç (Select)"; }
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool pick(const PickEvent& e) override;
  void hover(const SelItem& item) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool cancel() override;

protected:
  void onActivate() override;

private:
  bool loops_ = true;
};

} // namespace pcd
