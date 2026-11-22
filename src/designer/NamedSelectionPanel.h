#pragma once
// Named selections (Adlandırılmış seçimler) — the lower section of the Gruplar tab (SpaceClaim "Groups" panel,
// "Named Selections" folder). Pure view: MainWindow (MainWindowNs.cpp) fills it from the NamedSelectionManager
// resolutions and performs every edit (one undo step each). docs/named_selections/MODEL.md, KULLANIM_KILAVUZU §3.6.
//
// Tree (objectName "nsTree"), columns Ad | Öge | Durum:
//   Adlandırılmış Seçimler (n)                 folder, always present
//     <colour swatch> Inlet   1 yüz   ● Tamam    one row per selection (UserRole = id), editable name (F2 / double click)
//       Extrude1.end        yüz    ● Tamam      member rows (collapsed by default), tooltip = current entities / diagnostic
// Buttons: "Adlandırılmış seçim oluştur" (nsCreate), "Seçimi değiştir" (nsReplace), "Sil" (nsDelete).
#include "pc/NamedSelections.h"
#include <QIcon>
#include <QList>
#include <QStringList>
#include <QWidget>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pcd {

QIcon nsStatusIcon(pc::model::NsStatus s, int size = 14);   // green check / blue split / orange ! / red × / grey
QColor nsStatusColor(pc::model::NsStatus s);
QString nsStatusText(pc::model::NsStatus s);                  // Turkish label ("Tamam", "Bölündü", "Kayıp", ...)

class NamedSelectionPanel : public QWidget {
  Q_OBJECT
public:
  struct MemberRow {
    int index = 0;
    QString kind;                  // "yüz" | "kenar" | "gövde"
    QString ref, diagnostic;
    QStringList current;           // current entity names
    QStringList suggestions;
    pc::model::NsStatus status = pc::model::NsStatus::Ok;
  };
  struct Row {
    QString id, name, color, diagnostic, countText;
    pc::model::NsStatus status = pc::model::NsStatus::Empty;
    bool exportStep = true, tinted = false;
    QList<MemberRow> members;
  };
  explicit NamedSelectionPanel(QWidget* parent = nullptr);

  void setRows(const QList<Row>& rows);          // keeps expansion / current row; no signals
  QTreeWidget* tree() const { return tree_; }
  QTreeWidgetItem* folder() const { return folder_; }
  QTreeWidgetItem* itemOf(const QString& id) const;
  QString idOf(const QTreeWidgetItem* it) const;   // selection row (or the selection of a member row)
  QString currentId() const;
  void setCurrentId(const QString& id);            // no signals
  void beginRename(const QString& id);             // inline editor on the name (SpaceClaim: type, Enter)
  QPushButton* button(const QString& key) const;   // "create" | "replace" | "delete"

signals:
  void activated(const QString& id);                                   // left click on a selection / member row
  void renameRequested(const QString& id, const QString& newName);     // inline edit committed
  void contextMenuRequested(const QString& id, int memberIndex, const QPoint& globalPos);   // id "" = folder / empty
  void createRequested();
  void replaceRequested(const QString& id);
  void deleteRequested(const QString& id);

private:
  QTreeWidget* tree_ = nullptr;
  QTreeWidgetItem* folder_ = nullptr;
  QPushButton *bCreate_ = nullptr, *bReplace_ = nullptr, *bDelete_ = nullptr;
  bool updating_ = false;
};

} // namespace pcd
