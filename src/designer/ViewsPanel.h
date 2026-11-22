#pragma once
// SC23 WP-S (S-P1-4): "Görünümler" (Views) tab of the Structure panel, SpaceClaim 2023 R1 [G]:
//   [Oluştur] [Sil]
//   Ad          | Kısayol
//   İzometrik   | Ctrl+0      (standard views: QActions of the window, same shortcuts)
//   Trimetrik   | Ctrl+1
//   Üst / Alt / Sol / Sağ / Ön  Ctrl+2..6,  Arka (no shortcut; [D] what SpaceClaim lists below Front)
//   Görünüm1    |             (saved in this session only: view direction, not stored in the document [Ö])
// Double click / Enter applies a row. "Sil" deletes saved views only (standard rows: disabled, tooltip says why).
#include <QVariant>
#include <QWidget>
#include <functional>

class QAction;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pcd {

class ViewsPanel : public QWidget {
  Q_OBJECT
public:
  explicit ViewsPanel(QWidget* parent = nullptr);   // objectName "viewsPanel"
  void addStandardView(QAction* a);                 // row text = a->text(), shortcut column from a->shortcut()
  // camera hooks for saved views: capture the current camera / restore it
  void setCameraHooks(std::function<QVariant()> capture, std::function<void(const QVariant&)> apply);

  QTreeWidget* list() const { return list_; }       // objectName "viewsList"
  QPushButton* createButton() const { return create_; }   // "viewsCreate"
  QPushButton* deleteButton() const { return delete_; }   // "viewsDelete"
  int standardCount() const { return standard_; }
  int savedViewCount() const;
  QString createView();                             // saves the current camera as "GörünümN", returns its name
  bool deleteCurrent();                             // saved views only
  bool applyRow(int row);                           // triggers the action / restores the saved camera

private:
  void syncButtons();
  QTreeWidget* list_ = nullptr;
  QPushButton *create_ = nullptr, *delete_ = nullptr;
  int standard_ = 0;
  int nextId_ = 1;
  std::function<QVariant()> capture_;
  std::function<void(const QVariant&)> apply_;
};

} // namespace pcd
