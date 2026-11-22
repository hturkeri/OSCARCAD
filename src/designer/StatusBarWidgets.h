#pragma once
// SC-UX shell (WP-A): status bar widgets (SPEC §1.2 #8, §3 #33). From left to right after the message area
// (QStatusBar::showMessage, tool prompts): hover info | quick measure | selection list | revert selection |
// errors/warnings (MainWindow label) | selection filter | ... PersistCAD labels ... | previous / next view.
#include "ToolApi.h"
#include <QObject>

class QComboBox;
class QLabel;
class QStatusBar;
class QToolButton;
class QAction;

namespace pcd {

class StatusBarWidgets : public QObject {
  Q_OBJECT
public:
  StatusBarWidgets(QStatusBar* sb, SelectionModel* sel, QObject* parent = nullptr);

  QLabel* quickMeasure() const { return quickMeasure_; }     // objectName "quickMeasure"
  QLabel* selectionList() const { return selectionList_; }   // objectName "selectionList"
  QComboBox* filter() const { return filter_; }              // objectName "selectionFilter"
  QToolButton* revertButton() const { return revert_; }      // objectName "selectionRevert"
  // Previous / next view buttons; their actions (MainWindow, shortcuts ← / →) are set by the shell.
  void setViewActions(QAction* prev, QAction* next);
  void addPermanent(QWidget* w);                             // after the filter, before the view buttons
  void setQuickMeasure(const QString& text);
  void refreshSelection();

private:
  QStatusBar* sb_;
  SelectionModel* sel_;
  QLabel* quickMeasure_ = nullptr;
  QLabel* selectionList_ = nullptr;
  QComboBox* filter_ = nullptr;
  QToolButton *revert_ = nullptr, *prev_ = nullptr, *next_ = nullptr;
};

} // namespace pcd
