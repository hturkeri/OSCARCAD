#pragma once
// SC23 WP-S (S-P1-2): document tab below the viewport, SpaceClaim 2023 R1 [G] ("Design1*" under the graphics area;
// "*" after a change). PersistCAD has one document per window, so the bar has exactly one tab; it follows
// MainWindow::updateTitle() (name + "*" while the document is modified, full path in the tooltip).
#include <QTabBar>

namespace pcd {

class DocumentTabBar : public QTabBar {
  Q_OBJECT
public:
  explicit DocumentTabBar(QWidget* parent = nullptr);   // objectName "documentTabs"
  void setDocument(const QString& name, bool modified, const QString& path = QString());
  QString documentText() const;                          // "Part1*"
  bool modified() const { return modified_; }

private:
  bool modified_ = false;
};

} // namespace pcd
