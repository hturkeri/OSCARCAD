// Named selections section of the Gruplar tab. See NamedSelectionPanel.h.
#include "NamedSelectionPanel.h"
#include "Icons.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pcd {

using pc::model::NsStatus;

namespace {
enum Role { IdRole = Qt::UserRole, TypeRole = Qt::UserRole + 1, MemberRole = Qt::UserRole + 2, NameRole = Qt::UserRole + 3 };
enum Type { TFolder = 0, TSel = 1, TMember = 2 };

QIcon swatch(const QColor& c, bool tinted) {
  QPixmap pm(16, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.darker(160), 1.2));
  p.setBrush(c);
  // a tag (label) shape: SpaceClaim draws named selections as tags
  QPainterPath path;
  path.moveTo(1.5, 4);
  path.lineTo(10, 4);
  path.lineTo(14.5, 8);
  path.lineTo(10, 12);
  path.lineTo(1.5, 12);
  path.closeSubpath();
  p.drawPath(path);
  p.setBrush(Qt::white);
  p.drawEllipse(QPointF(10.3, 8), 1.2, 1.2);
  if (tinted) {   // "vurgu açık": small eye-dot
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    p.drawEllipse(QPointF(4.5, 8), 1.4, 1.4);
  }
  return QIcon(pm);
}
} // namespace

QColor nsStatusColor(NsStatus s) {
  switch (s) {
    case NsStatus::Ok: return QColor(46, 150, 70);
    case NsStatus::Split: return QColor(60, 120, 205);
    case NsStatus::Merged: return QColor(30, 150, 160);
    case NsStatus::Suspect:
    case NsStatus::Partial: return QColor(225, 140, 20);
    case NsStatus::Lost:
    case NsStatus::Failed: return QColor(205, 45, 45);
    default: return QColor(140, 140, 140);
  }
}

QString nsStatusText(NsStatus s) { return QString::fromUtf8(pc::model::statusLabel(s)); }

QIcon nsStatusIcon(NsStatus s, int size) {
  QPixmap pm(size * 2, size * 2);
  pm.setDevicePixelRatio(2.0);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor c = nsStatusColor(s);
  const double r = size / 2.0 - 1;
  const QPointF m(size / 2.0, size / 2.0);
  QPen white(Qt::white, std::max(1.4, size / 9.0));
  white.setCapStyle(Qt::RoundCap);
  white.setJoinStyle(Qt::RoundJoin);
  if (s == NsStatus::Suspect || s == NsStatus::Partial) {   // warning triangle
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(QPolygonF({QPointF(m.x(), 1), QPointF(size - 1, size - 1.5), QPointF(1, size - 1.5)}));
    p.setPen(white);
    p.drawLine(QPointF(m.x(), size * 0.36), QPointF(m.x(), size * 0.62));
    p.drawPoint(QPointF(m.x(), size * 0.78));
    return QIcon(pm);
  }
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawEllipse(m, r, r);
  p.setPen(white);
  p.setBrush(Qt::NoBrush);
  switch (s) {
    case NsStatus::Ok:
      p.drawPolyline(QPolygonF({QPointF(size * 0.28, size * 0.52), QPointF(size * 0.44, size * 0.68), QPointF(size * 0.73, size * 0.34)}));
      break;
    case NsStatus::Split:   // one face, two pieces
      p.drawLine(QPointF(size * 0.28, size * 0.32), QPointF(size * 0.28, size * 0.68));
      p.drawLine(QPointF(size * 0.72, size * 0.32), QPointF(size * 0.72, size * 0.68));
      p.drawLine(QPointF(size * 0.5, size * 0.22), QPointF(size * 0.5, size * 0.78));
      break;
    case NsStatus::Merged:
      p.drawLine(QPointF(size * 0.3, size * 0.3), QPointF(size * 0.5, size * 0.5));
      p.drawLine(QPointF(size * 0.3, size * 0.7), QPointF(size * 0.5, size * 0.5));
      p.drawLine(QPointF(size * 0.5, size * 0.5), QPointF(size * 0.76, size * 0.5));
      break;
    case NsStatus::Lost:
    case NsStatus::Failed:
      p.drawLine(QPointF(size * 0.33, size * 0.33), QPointF(size * 0.67, size * 0.67));
      p.drawLine(QPointF(size * 0.67, size * 0.33), QPointF(size * 0.33, size * 0.67));
      break;
    default:   // suppressed / not yet / empty: a dash
      p.drawLine(QPointF(size * 0.3, size * 0.5), QPointF(size * 0.7, size * 0.5));
      break;
  }
  return QIcon(pm);
}

NamedSelectionPanel::NamedSelectionPanel(QWidget* parent) : QWidget(parent) {
  setObjectName("namedSelectionPanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(3);
  auto* hdr = new QHBoxLayout;
  auto* icon = new QLabel;
  icon->setPixmap(makeIcon("namedsel", 16).pixmap(16, 16));
  auto* title = new QLabel("<b>Adlandırılmış Seçimler</b>");
  title->setObjectName("nsTitle");
  title->setToolTip("Adlandırılmış seçimler (Named Selections): yüz / kenar / gövde gruplarına kalıcı adlar.\n"
                    "Ad sonraki işlemlerle hiçbir zaman değişmez; bölünen yüzde ad tüm parçaları kapsar.");
  hdr->addWidget(icon);
  hdr->addWidget(title);
  hdr->addStretch();
  v->addLayout(hdr);

  auto* row = new QHBoxLayout;
  row->setSpacing(3);
  auto mk = [&](QPushButton*& b, const QString& name, const QString& iconKind, const QString& text, const QString& tip) {
    b = new QPushButton(makeIcon(iconKind, 16), text);
    b->setObjectName(name);
    b->setToolTip(tip);
    row->addWidget(b);
  };
  mk(bCreate_, "nsCreate", "namedsel", "Adlandırılmış seçim oluştur",
     "Adlandırılmış seçim oluştur (Create NS) — Ctrl+G: 3B görünümde seçili yüz / kenar / gövdelerden yeni bir grup.\n"
     "Varsayılan ad (Grup1 …) düzenleme kipinde açılır: adı yazıp Enter'a basın.");
  mk(bReplace_, "nsReplace", "namedsel.replace", "Seçimi değiştir",
     "Seçimi değiştir (Replace Group): seçili grubun üyelerini 3B görünümdeki geçerli seçimle değiştirir");
  mk(bDelete_, "nsDelete", "delete", "Sil", "Seçili adlandırılmış seçimi sil (Del) — geometri değişmez");
  row->addStretch();
  v->addLayout(row);
  connect(bCreate_, &QPushButton::clicked, this, [this] { emit createRequested(); });
  connect(bReplace_, &QPushButton::clicked, this, [this] {
    if (!currentId().isEmpty()) emit replaceRequested(currentId());
  });
  connect(bDelete_, &QPushButton::clicked, this, [this] {
    if (!currentId().isEmpty()) emit deleteRequested(currentId());
  });

  tree_ = new QTreeWidget(this);
  tree_->setObjectName("nsTree");
  tree_->setColumnCount(3);
  tree_->setHeaderLabels({"Ad", "Öge", "Durum"});
  tree_->setIconSize(QSize(16, 16));
  tree_->setUniformRowHeights(true);
  tree_->setRootIsDecorated(true);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::DoubleClicked);   // F2 / double click
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  v->addWidget(tree_, 1);
  folder_ = new QTreeWidgetItem(tree_, {"Adlandırılmış Seçimler (0)"});
  folder_->setData(0, TypeRole, TFolder);

  connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int) {
    if (updating_ || !it || it->data(0, TypeRole).toInt() == TFolder) return;
    emit activated(idOf(it));
  });
  connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (updating_ || col != 0 || it->data(0, TypeRole).toInt() != TSel) return;
    const QString t = it->text(0).trimmed();
    if (t == it->data(0, NameRole).toString()) return;
    emit renameRequested(it->data(0, IdRole).toString(), t);
  });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    QTreeWidgetItem* it = tree_->itemAt(pos);
    const int type = it ? it->data(0, TypeRole).toInt() : TFolder;
    if (it && type != TFolder) {
      updating_ = true;
      tree_->setCurrentItem(it);
      updating_ = false;
    }
    emit contextMenuRequested(it && type != TFolder ? idOf(it) : QString(), type == TMember ? it->data(0, MemberRole).toInt() : -1,
                              tree_->viewport()->mapToGlobal(pos));
  });
  connect(tree_, &QTreeWidget::currentItemChanged, this, [this] {
    const bool on = !currentId().isEmpty();
    bReplace_->setEnabled(on);
    bDelete_->setEnabled(on);
  });
  bReplace_->setEnabled(false);
  bDelete_->setEnabled(false);
}

QPushButton* NamedSelectionPanel::button(const QString& key) const {
  return key == "create" ? bCreate_ : key == "replace" ? bReplace_ : key == "delete" ? bDelete_ : nullptr;
}

QTreeWidgetItem* NamedSelectionPanel::itemOf(const QString& id) const {
  for (int i = 0; i < folder_->childCount(); ++i)
    if (folder_->child(i)->data(0, IdRole).toString() == id) return folder_->child(i);
  return nullptr;
}

QString NamedSelectionPanel::idOf(const QTreeWidgetItem* it) const {
  if (!it) return QString();
  const int t = it->data(0, TypeRole).toInt();
  if (t == TSel) return it->data(0, IdRole).toString();
  if (t == TMember && it->parent()) return it->parent()->data(0, IdRole).toString();
  return QString();
}

QString NamedSelectionPanel::currentId() const { return idOf(tree_->currentItem()); }

void NamedSelectionPanel::setCurrentId(const QString& id) {
  updating_ = true;
  const QSignalBlocker b(tree_);
  if (QTreeWidgetItem* it = itemOf(id)) {
    tree_->setCurrentItem(it);
    tree_->scrollToItem(it);
  } else {
    tree_->setCurrentItem(nullptr);
    tree_->clearSelection();
  }
  const bool on = !currentId().isEmpty();
  bReplace_->setEnabled(on);
  bDelete_->setEnabled(on);
  updating_ = false;
}

void NamedSelectionPanel::beginRename(const QString& id) {
  QTreeWidgetItem* it = itemOf(id);
  if (!it) return;
  setCurrentId(id);
  tree_->setFocus(Qt::OtherFocusReason);
  tree_->editItem(it, 0);
  // typing replaces the default name (SpaceClaim: Grup1 is selected, just type)
  for (QLineEdit* le : tree_->viewport()->findChildren<QLineEdit*>())
    if (le->isVisible()) { le->selectAll(); le->setFocus(Qt::OtherFocusReason); }
}

void NamedSelectionPanel::setRows(const QList<Row>& rows) {
  updating_ = true;
  const QSignalBlocker b(tree_);
  const QString cur = currentId();
  QSet<QString> open;
  for (int i = 0; i < folder_->childCount(); ++i)
    if (folder_->child(i)->isExpanded()) open << folder_->child(i)->data(0, IdRole).toString();
  qDeleteAll(folder_->takeChildren());
  folder_->setText(0, QString("Adlandırılmış Seçimler (%1)").arg(rows.size()));
  folder_->setIcon(0, makeIcon("namedsel.folder", 16));
  QFont bf = folder_->font(0);
  bf.setBold(true);
  folder_->setFont(0, bf);
  folder_->setFlags(Qt::ItemIsEnabled);
  folder_->setToolTip(0, "Adlandırılmış seçimler. Oluştur: 3B'de yüz seçip Ctrl+G (ya da sağ tık > Adlandırılmış seçim oluştur).");
  for (const Row& r : rows) {
    auto* it = new QTreeWidgetItem(folder_, {r.name, r.countText, nsStatusText(r.status)});
    it->setData(0, IdRole, r.id);
    it->setData(0, TypeRole, TSel);
    it->setData(0, NameRole, r.name);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    it->setIcon(0, swatch(QColor(r.color), r.tinted));
    it->setIcon(2, nsStatusIcon(r.status));
    it->setForeground(2, nsStatusColor(r.status));
    QString tip = QString("%1 — %2 · %3%4\nTıkla: yüzleri seç ve vurgula · F2 / çift tık: yeniden adlandır · sağ tık: menü")
                      .arg(r.name, r.countText, nsStatusText(r.status), r.exportStep ? QString() : QString(" · STEP'e aktarılmaz"));
    if (!r.diagnostic.isEmpty()) tip += "\n" + r.diagnostic;
    for (int c = 0; c < 3; ++c) it->setToolTip(c, tip);
    if (!r.exportStep) {
      QFont f = it->font(0);
      f.setItalic(true);
      it->setFont(0, f);
    }
    for (const MemberRow& m : r.members) {
      QString shown = m.current.isEmpty() ? m.ref : m.current.join(", ");
      if (shown.size() > 60) shown = shown.left(57) + "...";
      auto* mi = new QTreeWidgetItem(it, {shown, m.kind, nsStatusText(m.status)});
      mi->setData(0, TypeRole, TMember);
      mi->setData(0, MemberRole, m.index);
      mi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      mi->setIcon(0, makeIcon(m.kind == "kenar" ? "edges" : m.kind == "gövde" ? "mode3d" : "pull", 14));
      mi->setIcon(2, nsStatusIcon(m.status, 12));
      mi->setForeground(2, nsStatusColor(m.status));
      QString mt = QString("Üye %1 (%2): %3\nŞu an: %4").arg(m.index + 1).arg(m.kind, m.ref,
                                                              m.current.isEmpty() ? QString("—") : m.current.join(", "));
      if (!m.diagnostic.isEmpty()) mt += "\n" + m.diagnostic;
      if (!m.suggestions.isEmpty()) mt += "\nÖneriler (sağ tık > Yeniden bağla): " + m.suggestions.join(", ");
      for (int c = 0; c < 3; ++c) mi->setToolTip(c, mt);
    }
    it->setExpanded(open.contains(r.id));
  }
  folder_->setExpanded(true);
  if (QTreeWidgetItem* it = itemOf(cur)) tree_->setCurrentItem(it);
  const bool on = !currentId().isEmpty();
  bReplace_->setEnabled(on);
  bDelete_->setEnabled(on);
  updating_ = false;
}

} // namespace pcd
