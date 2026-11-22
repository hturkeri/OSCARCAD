#pragma once
// SC-UX (WP-B) — wraps the SketchEditor tools (Seç, Çizgi, Dikdörtgen, Çember, Yay, Nokta, Kırp) as IActiveTool so
// that the ToolManager drives them like the 3D tools (key routing, Esc chain, Ctrl+G, Del, Options panel).
#include "SketchEditor.h"
#include "ToolBase.h"

namespace pcd {

class SketchToolAdapter : public ToolBase {
  Q_OBJECT
public:
  SketchToolAdapter(const QString& id, SketchEditor::Tool tool, const QString& title, QObject* parent);
  QString id() const override { return id_; }
  QString title() const override { return title_; }
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool busy() const override;
  bool cancel() override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  bool createGroup() override;
  bool deleteSelection() override;
  bool isSketchTool() const override { return true; }
  SketchEditor::Tool editorTool() const { return tool_; }

protected:
  void onActivate() override;
  void onDeactivate() override;

private:
  SketchEditor* ed() const;
  void reportState();                 // SC23 (O-P0-1): OpStatus from the editor's draw stage / dimension box
  QString id_, title_;
  SketchEditor::Tool tool_;
  QMetaObject::Connection msgConn_;
  QList<QMetaObject::Connection> opConns_;   // SC23
  QString errReason_;                        // SC23: last refused dimension (Enter on an invalid text)
  int stage_ = 0;
  size_t entitiesAtStart_ = 0;
};

} // namespace pcd
