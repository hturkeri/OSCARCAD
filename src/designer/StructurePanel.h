#pragma once
// SC-UX shell (WP-A): Structure panel (SPEC §1.2 #9, §3 #26-#29, #40). Left dock "dockStructure" holding a
// QTabWidget with the tabs at the bottom (QTabWidget::South):
//   Yapı (Structure)  : tree "Tasarım" > Gövdeler (visibility check boxes) > Düzlemler > Sketch'ler
//   Geçmiş (History)  : the parametric feature tree (MainWindow::featureTree(), rollback / suppress / edit) [PC]
//   Seçim (Selection) : SelectionPanel
//   Gruplar (Groups)  : the parameter table (QTableView + QStandardItemModel, columns unchanged) [PC]
// Layers and Views tabs are intentionally absent (SPEC §3.9).
#include "ToolApi.h"
#include <QColor>
#include <QIcon>
#include <QList>
#include <QWidget>

class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace pcd {

class StructurePanel : public QWidget {
  Q_OBJECT
public:
  explicit StructurePanel(QWidget* parent = nullptr);

  QTabWidget* tabs() const { return tabs_; }              // objectName "structureTabs"
  QTreeWidget* tree() const { return tree_; }             // objectName "structureTree"
  // Adds a tab page; the page gets objectName "structureTab.<key>" if it has none.
  int addPage(QWidget* page, const QString& key, const QString& title, const QIcon& icon = QIcon(), const QString& tip = QString());
  void setCurrentPage(const QString& key);
  QString currentPageKey() const;

  struct BodyRow {
    QString name, createdBy; double volume = 0; int faces = 0; QColor color; bool visible = true;
    bool transparent = false;
    QList<QPair<QString, QString>> hiddenFaces;   // Gizle / Göster: (face name, label) hidden in this body
  };
  struct PlaneRow { QString id, kind; bool ok = true, suppressed = false; };
  struct SketchRow { QString id, plane; bool shown = true, consumed = false, ok = true; };
  void rebuild(const QString& docName, const QList<BodyRow>& bodies, const QList<PlaneRow>& planes,
               const QList<SketchRow>& sketches);
  // SC23 (O-P0-6, ANALYSIS §4.6): derived display types. A "surface" is a sketch region not consumed by a solid
  // feature yet (PersistCAD has no surface bodies, SC23-CR-1); a "curve" row is a sketch whose curves stay open.
  struct SurfaceRow { QString name /*sketch:<sid>:r<k>*/, sketch; int index = 0; double area = 0, perimeter = 0; int holes = 0; };
  struct CurveRow { QString sketch; int openEnds = 0, curves = 0; };
  void rebuild(const QString& docName, const QList<BodyRow>& bodies, const QList<PlaneRow>& planes,
               const QList<SketchRow>& sketches, const QList<SurfaceRow>& surfaces, const QList<CurveRow>& curves,
               bool modified);
  // Root text: document name + "*" while the document has unsaved changes (follows the window's modified flag).
  void setModified(bool modified);
  QTreeWidgetItem* rootNode() const { return root_; }
  // Update one body's visibility check box (no signals).
  void setBodyChecked(const QString& body, bool visible);
  // Mirror the selection model in the tree (no signals).
  void syncSelection(const QList<SelItem>& items);
  // Node "Gövdeler" (children = bodies, UserRole = body name). ACC #23 alternative to bodiesTree().
  QTreeWidgetItem* bodiesNode() const { return bodiesNode_; }
  QTreeWidgetItem* planesNode() const { return planesNode_; }
  QTreeWidgetItem* sketchesNode() const { return sketchesNode_; }
  QTreeWidgetItem* surfacesNode() const { return surfacesNode_; }   // SC23 "Yüzeyler (n)", children = regions
  QTreeWidgetItem* curvesNode() const { return curvesNode_; }       // SC23 "Eğriler (n)", children = sketches
  // Gizle / Göster: the "Gizli yüzler (n)" child of a body row (children = hidden faces, unchecked; tick = show).
  QTreeWidgetItem* hiddenFacesNode(const QString& body) const;

signals:
  void itemPicked(const pcd::SelItem& item, bool add);          // click (add = Ctrl)
  void itemActivated(const pcd::SelItem& item);                 // double click (edit)
  void visibilityToggled(const pcd::SelItem& item, bool visible);
  void renameRequested(const pcd::SelItem& item, const QString& newName);
  void contextMenuRequested(const pcd::SelItem& item, const QPoint& globalPos);

private:
  static bool itemSel(const QTreeWidgetItem* it, SelItem* out);
  QTabWidget* tabs_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QTreeWidgetItem *root_ = nullptr, *bodiesNode_ = nullptr, *planesNode_ = nullptr, *sketchesNode_ = nullptr;
  QTreeWidgetItem *surfacesNode_ = nullptr, *curvesNode_ = nullptr;   // SC23
  QString docName_;
  bool modified_ = false;
  bool watching_ = false;
protected:
  bool eventFilter(QObject* o, QEvent* e) override;
private:
  QStringList pageKeys_;
  bool updating_ = false;
};

QIcon selKindIcon(SelItem::Kind k);

} // namespace pcd
