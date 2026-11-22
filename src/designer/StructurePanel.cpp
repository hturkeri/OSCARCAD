// SC-UX shell (WP-A): Structure panel. See StructurePanel.h.
#include "StructurePanel.h"
#include "Icons.h"
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pcd {

namespace {
enum Role { KindRole = Qt::UserRole + 1, NameRole = Qt::UserRole };
QIcon swatchIcon(const QColor& c) {
  QPixmap pm(16, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.darker(150), 1.2));
  p.setBrush(c);
  p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
  return QIcon(pm);
}
} // namespace

QIcon selKindIcon(SelItem::Kind k) {
  switch (k) {
    case SelItem::Kind::Face: return makeIcon("pull", 16);
    case SelItem::Kind::Edge: return makeIcon("edges", 16);
    case SelItem::Kind::Body: return makeIcon("mode3d", 16);
    case SelItem::Kind::Datum: return makeIcon("plane", 16);
    case SelItem::Kind::SketchRegion: return makeIcon("rect", 16);
    case SelItem::Kind::SketchCurve: return makeIcon("line", 16);
    case SelItem::Kind::SketchPoint: return makeIcon("point", 16);
    case SelItem::Kind::Sketch: return makeIcon("sketch", 16);
    case SelItem::Kind::Feature: return makeIcon("extrude", 16);
    case SelItem::Kind::Parameter: return makeIcon("group", 16);
  }
  return QIcon();
}

StructurePanel::StructurePanel(QWidget* parent) : QWidget(parent) {
  setObjectName("structurePanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  tabs_ = new QTabWidget(this);
  tabs_->setObjectName("structureTabs");
  tabs_->setTabPosition(QTabWidget::South);
  tabs_->setDocumentMode(true);
  tabs_->setIconSize(QSize(14, 14));
  v->addWidget(tabs_);

  tree_ = new QTreeWidget(this);
  tree_->setObjectName("structureTree");
  tree_->setHeaderHidden(true);
  tree_->setColumnCount(2);   // SC23: 0 = name, 1 = type ("Katı", "Yüzey", "Eğri")
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_->setIconSize(QSize(16, 16));
  tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);   // F2 renames a body (double click = edit)
  tree_->setUniformRowHeights(true);
  addPage(tree_, "structure", "Yapı", makeIcon("bodies", 14), "Yapı (Structure): gövdeler, düzlemler ve sketch'ler");

  connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) {
    SelItem s;
    if (updating_ || !itemSel(it, &s)) return;
    if (s.kind == SelItem::Kind::Face) return;   // hidden-face rows: check box / context menu only
    emit itemPicked(s, QApplication::keyboardModifiers() & Qt::ControlModifier);
  });
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    SelItem s;
    if (!itemSel(it, &s)) return;
    if (s.kind == SelItem::Kind::Face) return;
    if (s.kind == SelItem::Kind::Body) { tree_->editItem(it, 0); return; }
    emit itemActivated(s);
  });
  connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int) {
    SelItem s;
    if (updating_ || !itemSel(it, &s)) return;
    const bool vis = it->checkState(0) == Qt::Checked;
    if (it->data(0, Qt::UserRole + 2).toBool() != vis) {
      it->setData(0, Qt::UserRole + 2, vis);
      emit visibilityToggled(s, vis);
      return;
    }
    if (s.kind == SelItem::Kind::Body && it->text(0) != s.name) emit renameRequested(s, it->text(0).trimmed());
  });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    SelItem s;
    if (QTreeWidgetItem* it = tree_->itemAt(pos); it && itemSel(it, &s)) emit contextMenuRequested(s, tree_->viewport()->mapToGlobal(pos));
  });
}

int StructurePanel::addPage(QWidget* page, const QString& key, const QString& title, const QIcon& icon, const QString& tip) {
  if (page->objectName().isEmpty()) page->setObjectName("structureTab." + key);
  const int i = tabs_->addTab(page, icon, title);
  if (!tip.isEmpty()) tabs_->setTabToolTip(i, tip);
  pageKeys_ << key;
  return i;
}

void StructurePanel::setCurrentPage(const QString& key) {
  const int i = pageKeys_.indexOf(key);
  if (i >= 0) tabs_->setCurrentIndex(i);
}

QString StructurePanel::currentPageKey() const {
  const int i = tabs_->currentIndex();
  return i >= 0 && i < pageKeys_.size() ? pageKeys_[i] : QString();
}

bool StructurePanel::itemSel(const QTreeWidgetItem* it, SelItem* out) {
  if (!it || !it->data(0, KindRole).isValid()) return false;
  out->kind = SelItem::Kind(it->data(0, KindRole).toInt());
  out->name = it->data(0, NameRole).toString();
  return !out->name.isEmpty();
}

void StructurePanel::rebuild(const QString& docName, const QList<BodyRow>& bodies, const QList<PlaneRow>& planes,
                             const QList<SketchRow>& sketches) {
  rebuild(docName, bodies, planes, sketches, {}, {}, modified_);
}

void StructurePanel::setModified(bool modified) {
  modified_ = modified;
  if (root_) root_->setText(0, docName_ + (modified ? "*" : ""));
}

bool StructurePanel::eventFilter(QObject* o, QEvent* e) {
  if (e->type() == QEvent::ModifiedChange && o == window()) setModified(window()->isWindowModified());
  return QWidget::eventFilter(o, e);
}

void StructurePanel::rebuild(const QString& docName, const QList<BodyRow>& bodies, const QList<PlaneRow>& planes,
                             const QList<SketchRow>& sketches, const QList<SurfaceRow>& surfaces, const QList<CurveRow>& curves,
                             bool modified) {
  updating_ = true;
  if (!watching_ && window() && window() != this) {   // SC23: the root's "*" follows the window's modified flag
    window()->installEventFilter(this);
    watching_ = true;
  }
  // keep expansion state of the category nodes across rebuilds
  const bool bOpen = bodiesNode_ ? bodiesNode_->isExpanded() : true;
  const bool pOpen = planesNode_ ? planesNode_->isExpanded() : true;
  const bool sOpen = sketchesNode_ ? sketchesNode_->isExpanded() : true;
  const bool yOpen = surfacesNode_ ? surfacesNode_->isExpanded() : true;
  const bool cOpen = curvesNode_ ? curvesNode_->isExpanded() : true;
  tree_->clear();
  docName_ = docName;
  modified_ = modified;
  root_ = new QTreeWidgetItem(tree_, {docName + (modified ? "*" : "")});
  root_->setIcon(0, makeIcon("mode3d", 16));
  QFont bf = root_->font(0);
  bf.setBold(true);
  root_->setFont(0, bf);
  root_->setToolTip(0, "Tasarım (Design) — * = kaydedilmemiş değişiklik var");
  auto category = [&](const QString& title, const QString& icon, int n, const QString& tip) {
    auto* c = new QTreeWidgetItem(root_, {QString("%1 (%2)").arg(title).arg(n)});
    c->setIcon(0, makeIcon(icon, 16));
    c->setToolTip(0, tip);
    c->setFlags(Qt::ItemIsEnabled);
    return c;
  };
  bodiesNode_ = category("Gövdeler", "bodies", int(bodies.size()), "Katı gövdeler (onay kutusu = görünürlük)");
  for (const auto& b : bodies) {
    auto* it = new QTreeWidgetItem(bodiesNode_, {b.name});
    it->setData(0, NameRole, b.name);
    it->setData(0, KindRole, int(SelItem::Kind::Body));
    it->setData(0, Qt::UserRole + 2, b.visible);
    it->setIcon(0, swatchIcon(b.color));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    it->setCheckState(0, b.visible ? Qt::Checked : Qt::Unchecked);
    it->setToolTip(0, QString("%1 (Katı)\nHacim: %2 mm³ · %3 yüz\nOluşturan: %4\nÇift tık / F2: yeniden adlandır")
                          .arg(b.name).arg(b.volume, 0, 'f', 1).arg(b.faces).arg(b.createdBy));
    it->setText(1, "Katı");   // SC23: every PersistCAD body is a solid (no surface bodies yet, SC23-CR-1)
    it->setForeground(1, QColor(110, 110, 110));
    it->setData(0, Qt::UserRole + 5, QString("solid"));
    if (b.transparent) {   // "Saydam yap": italic + tooltip (the text stays the body name: F2 renames it)
      QFont f = it->font(0);
      f.setItalic(true);
      it->setFont(0, f);
      it->setToolTip(0, it->toolTip(0) + "\nSaydam");
    }
    if (!b.visible) it->setForeground(0, QColor(140, 140, 140));
    if (!b.hiddenFaces.isEmpty()) {
      // Gizle / Göster: the faces hidden in this body, each with a check box (tick = show again)
      auto* hf = new QTreeWidgetItem(it, {QString("Gizli yüzler (%1)").arg(b.hiddenFaces.size())});
      hf->setIcon(0, makeIcon("hide", 16));
      hf->setFlags(Qt::ItemIsEnabled);
      hf->setData(0, Qt::UserRole + 4, b.name);
      hf->setToolTip(0, "Bu gövdede gizlenen yüzler: onay kutusu ya da sağ tık > Göster ile yeniden gösterin");
      for (const auto& [fname, label] : b.hiddenFaces) {
        auto* fi = new QTreeWidgetItem(hf, {label});
        fi->setData(0, NameRole, fname);
        fi->setData(0, KindRole, int(SelItem::Kind::Face));
        fi->setData(0, Qt::UserRole + 2, false);
        fi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        fi->setCheckState(0, Qt::Unchecked);
        fi->setIcon(0, makeIcon("pull", 16));
        fi->setForeground(0, QColor(120, 120, 120));
        fi->setToolTip(0, fname + "\nGizli yüz — işaretleyin: göster");
      }
      it->setExpanded(true);
      hf->setExpanded(true);
    }
  }
  // SC23 (O-P0-6): Yüzeyler = sketch regions not pulled yet; Eğriler = sketches whose curves stay open
  surfacesNode_ = category("Yüzeyler", "surface", int(surfaces.size()),
                           "Eskiz bölgeleri (düzlemsel yüzey) — Çek ile katı olur. PersistCAD'de ayrı yüzey gövdesi yok.");
  for (const auto& r : surfaces) {
    auto* it = new QTreeWidgetItem(surfacesNode_, {QString("%1 · bölge %2").arg(r.sketch).arg(r.index + 1), "Yüzey"});
    it->setData(0, NameRole, r.name);
    it->setData(0, KindRole, int(SelItem::Kind::SketchRegion));
    it->setData(0, Qt::UserRole + 5, QString("surface"));
    it->setIcon(0, makeIcon("surface", 16));
    it->setForeground(1, QColor(110, 110, 110));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setToolTip(0, QString("%1\neskiz bölgesi — Çek ile katı olur\nAlan: %2 mm² · Çevre: %3 mm · Delik: %4")
                          .arg(r.name).arg(r.area, 0, 'f', 2).arg(r.perimeter, 0, 'f', 2).arg(r.holes));
  }
  curvesNode_ = category("Eğriler", "curve", int(curves.size()), "Kapalı bölge oluşturmayan (açık) sketch eğrileri");
  for (const auto& c : curves) {
    const QString txt = c.openEnds > 0 ? QString("%1 · %2 açık uç").arg(c.sketch).arg(c.openEnds) : QString("%1 · açık eğri").arg(c.sketch);
    auto* it = new QTreeWidgetItem(curvesNode_, {txt, "Eğri"});
    it->setData(0, NameRole, c.sketch);
    it->setData(0, KindRole, int(SelItem::Kind::Sketch));
    it->setData(0, Qt::UserRole + 5, QString("curve"));
    it->setIcon(0, makeIcon("curve", 16));
    it->setForeground(1, QColor(110, 110, 110));
    if (c.openEnds > 0) it->setForeground(0, QColor(190, 40, 40));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setToolTip(0, QString("%1: %2 eğri, %3 açık uç — katı ya da yüzey olmaz.\nKapatın ya da Doldur (F) ile boşluğu görün.")
                          .arg(c.sketch).arg(c.curves).arg(c.openEnds));
  }
  planesNode_ = category("Düzlemler", "plane", int(planes.size()), "Datum düzlemleri");
  for (const auto& p : planes) {
    auto* it = new QTreeWidgetItem(planesNode_, {p.id + (p.suppressed ? "  (bastırıldı)" : "")});
    it->setData(0, NameRole, p.id);
    it->setData(0, KindRole, int(SelItem::Kind::Datum));
    it->setIcon(0, makeIcon(p.suppressed ? "suppressed" : p.ok ? "plane" : "error", 16));
    it->setToolTip(0, QString("%1 (%2)\nÇift tık: düzlemi düzenle").arg(p.id, p.kind));
    if (p.suppressed) it->setForeground(0, QColor(140, 140, 140));
  }
  sketchesNode_ = category("Sketch'ler", "sketch", int(sketches.size()), "Sketch'ler (onay kutusu = 3B görünümde göster)");
  for (const auto& s : sketches) {
    auto* it = new QTreeWidgetItem(sketchesNode_, {QString("%1 (%2)").arg(s.id, s.plane)});
    it->setData(0, NameRole, s.id);
    it->setData(0, KindRole, int(SelItem::Kind::Sketch));
    it->setData(0, Qt::UserRole + 2, s.shown);
    it->setIcon(0, makeIcon(s.ok ? "sketch" : "error", 16));
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    it->setCheckState(0, s.shown ? Qt::Checked : Qt::Unchecked);
    it->setToolTip(0, QString("%1%2\nÇift tık: sketch'i düzenle (K)").arg(s.id, s.consumed ? "  (bir katı özellikçe kullanılıyor)" : ""));
  }
  root_->setExpanded(true);
  bodiesNode_->setExpanded(bOpen);
  planesNode_->setExpanded(pOpen);
  sketchesNode_->setExpanded(sOpen);
  surfacesNode_->setExpanded(yOpen);
  curvesNode_->setExpanded(cOpen);
  updating_ = false;
}

QTreeWidgetItem* StructurePanel::hiddenFacesNode(const QString& body) const {
  if (!bodiesNode_) return nullptr;
  for (int i = 0; i < bodiesNode_->childCount(); ++i) {
    QTreeWidgetItem* it = bodiesNode_->child(i);
    if (it->data(0, NameRole).toString() != body) continue;
    for (int k = 0; k < it->childCount(); ++k)
      if (it->child(k)->data(0, Qt::UserRole + 4).toString() == body) return it->child(k);
  }
  return nullptr;
}

void StructurePanel::setBodyChecked(const QString& body, bool visible) {
  if (!bodiesNode_) return;
  updating_ = true;
  for (int i = 0; i < bodiesNode_->childCount(); ++i) {
    QTreeWidgetItem* it = bodiesNode_->child(i);
    if (it->data(0, NameRole).toString() != body) continue;
    it->setData(0, Qt::UserRole + 2, visible);
    it->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    it->setForeground(0, visible ? tree_->palette().color(QPalette::Text) : QColor(140, 140, 140));
  }
  updating_ = false;
}

void StructurePanel::syncSelection(const QList<SelItem>& items) {
  updating_ = true;
  const QSignalBlocker b(tree_);
  tree_->clearSelection();
  QTreeWidgetItem* last = nullptr;
  for (QTreeWidgetItem* cat : {bodiesNode_, surfacesNode_, planesNode_, sketchesNode_}) {
    if (!cat) continue;
    for (int i = 0; i < cat->childCount(); ++i) {
      QTreeWidgetItem* it = cat->child(i);
      SelItem s;
      if (!itemSel(it, &s)) continue;
      bool on = items.contains(s);
      // a Feature selection of a plane / sketch (History tab) mirrors onto the same node
      if (!on && (s.kind == SelItem::Kind::Datum || s.kind == SelItem::Kind::Sketch)) on = items.contains(SelItem{SelItem::Kind::Feature, s.name});
      if (on) { it->setSelected(true); last = it; }
    }
  }
  if (last) tree_->scrollToItem(last);
  updating_ = false;
}

} // namespace pcd
