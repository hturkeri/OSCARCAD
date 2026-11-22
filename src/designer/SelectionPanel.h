#pragma once
// SC-UX shell (WP-A): Selection tab of the Structure panel (SPEC §3 #28). Lists the current selection (from the
// SelectionModel), offers "Seçimi geri al" (revert) / "Temizle" and, in phase 2, "Benzerini seç".
// SC23 WP-S (S-P1-8), SpaceClaim 2023 R1 Selection panel [G] ("Selected: Face", All Types, search, Search all bodies):
//   header   "Seçili: Yüz (1)" / "Seçili: 3 öğe (karışık)" / "Seçili: yok"            objectName "selectionHeader"
//   type     "Tüm türler (All Types)", Yüz, Kenar, Gövde, Düzlem, Sketch -> SelectionModel::setFilter
//                                                                                   objectName "selectionTypeFilter"
//   search + "Tüm gövdelerde ara (Search all bodies)": shown DISABLED with the reason in the tooltip ([D] meaning)
#include "ToolApi.h"
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace pcd {

class SelectionPanel : public QWidget {
  Q_OBJECT
public:
  explicit SelectionPanel(SelectionModel* sel, QWidget* parent = nullptr);
  void refresh();
  QListWidget* list() const { return list_; }          // objectName "selectionPanelList"
  QLabel* header() const { return header_; }            // objectName "selectionHeader"
  QComboBox* typeFilter() const { return type_; }       // objectName "selectionTypeFilter"
  static QString headerText(const QList<SelItem>& items);   // "Seçili: Yüz (1)"

private:
  SelectionModel* sel_;
  QListWidget* list_ = nullptr;
  QLabel* summary_ = nullptr;
  QLabel* header_ = nullptr;
  QComboBox* type_ = nullptr;
  QLineEdit* search_ = nullptr;
  QCheckBox* allBodies_ = nullptr;
  QPushButton *revert_ = nullptr, *clear_ = nullptr, *similar_ = nullptr;
};

} // namespace pcd
