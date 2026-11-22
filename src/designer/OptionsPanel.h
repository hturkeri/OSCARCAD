#pragma once
// SC-UX shell (WP-A): Options panel (SPEC §1.2 #10, §8). Renders the active tool's ToolOption list generically:
// group titles in bold, Bool = QCheckBox, Choice = radio buttons (<= 4 choices) or QComboBox, Expr = ExprEdit,
// Text = QLineEdit, Action = QPushButton, Info = QLabel. User edits are reported with optionChanged(); the shell
// forwards them to ToolManager::setOption (-> IActiveTool::setOption). A tool may supply its own widget
// (ToolManager::createOptionsWidget), which then replaces the generic rendering.
//
// Widget objectNames (test hooks): "opt.<key>" for every field, "opt.<key>.<choiceKey>" for radio buttons.
#include "ToolApi.h"
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <functional>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace pcd {

class OptionsPanel : public QWidget {
  Q_OBJECT
public:
  explicit OptionsPanel(QWidget* parent = nullptr);
  // Document used by Expr fields (completer / parameter picker). Called lazily at render time.
  void setExpressionContext(std::function<pc::Document*()> fn) { exprDoc_ = std::move(fn); }

  // Render `opts` under the title "Seçenekler - <toolTitle>". If the option keys/types are unchanged since the
  // last call only the values are refreshed (no widget is recreated: safe to call from an option's own signal).
  void showOptions(const QString& toolTitle, const QList<ToolOption>& opts);
  // Replace the generic rendering with a tool-provided widget (ownership taken). nullptr = clear.
  void showCustom(const QString& toolTitle, QWidget* custom);

  QString title() const;                         // "Seçenekler - Seç (Select)"
  QWidget* field(const QString& key) const;      // the editor of an option ("opt.<key>")
  QVariant value(const QString& key) const;      // current UI value (Bool / Choice key / text)
  int optionCount() const { return opts_.size(); }

signals:
  void optionChanged(const QString& key, const QVariant& value);
  void titleChanged(const QString& title);

private:
  void clearContent();
  void build();
  void refreshValues();
  QString signature(const QList<ToolOption>& opts) const;

  std::function<pc::Document*()> exprDoc_;
  QLabel* title_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  QWidget* content_ = nullptr;
  QVBoxLayout* contentLayout_ = nullptr;
  QPointer<QWidget> custom_;
  QList<ToolOption> opts_;
  QString sig_;
  QHash<QString, QWidget*> fields_;
  bool updating_ = false;
};

} // namespace pcd
