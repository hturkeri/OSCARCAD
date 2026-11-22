// SC-UX shell (WP-A): Selection tab. See SelectionPanel.h.
#include "SelectionPanel.h"
#include "Icons.h"
#include "StructurePanel.h"   // selKindIcon
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace pcd {

namespace {
QString capitalized(QString s) {
  if (!s.isEmpty()) s[0] = s[0].toUpper();
  return s;
}
} // namespace

QString SelectionPanel::headerText(const QList<SelItem>& items) {
  if (items.isEmpty()) return "Seçili: yok";
  bool same = true;
  for (const SelItem& it : items) same = same && it.kind == items.first().kind;
  if (same) return QString("Seçili: %1 (%2)").arg(capitalized(selKindText(items.first().kind, 1))).arg(items.size());
  return QString("Seçili: %1 öğe (karışık)").arg(items.size());
}

SelectionPanel::SelectionPanel(SelectionModel* sel, QWidget* parent) : QWidget(parent), sel_(sel) {
  setObjectName("selectionPanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(4, 4, 4, 4);
  v->setSpacing(4);
  header_ = new QLabel(this);
  header_->setObjectName("selectionHeader");
  header_->setStyleSheet("font-weight: 600; color: #1b2b40;");
  v->addWidget(header_);
  auto* filterRow = new QHBoxLayout;
  type_ = new QComboBox(this);
  type_->setObjectName("selectionTypeFilter");
  type_->setToolTip("Seçim türü filtresi (selection filter): görünümde yalnız bu tür seçilir; durum çubuğundaki filtreyle aynı");
  type_->addItem("Tüm türler (All Types)", int(SelFilter::Smart));
  for (SelFilter f : {SelFilter::Face, SelFilter::Edge, SelFilter::Body, SelFilter::Datum, SelFilter::Sketch})
    type_->addItem(selFilterText(f), int(f));
  filterRow->addWidget(type_, 1);
  search_ = new QLineEdit(this);
  search_->setObjectName("selectionSearch");
  search_->setPlaceholderText("Ara");
  search_->setEnabled(false);
  search_->setToolTip("Seçim içinde arama henüz yok: SpaceClaim'deki aramanın davranışı doğrulanmadı [D]");
  filterRow->addWidget(search_, 1);
  v->addLayout(filterRow);
  allBodies_ = new QCheckBox("Tüm gövdelerde ara (Search all bodies)", this);
  allBodies_->setObjectName("selectionSearchAllBodies");
  allBodies_->setEnabled(false);
  allBodies_->setToolTip("Pasif: 'Search all bodies' seçeneğinin anlamı doğrulanmadı [D]; PersistCAD'de karşılığı yok");
  v->addWidget(allBodies_);
  summary_ = new QLabel(this);
  summary_->setObjectName("selectionPanelSummary");
  summary_->setWordWrap(true);
  v->addWidget(summary_);
  list_ = new QListWidget(this);
  list_->setObjectName("selectionPanelList");
  list_->setIconSize(QSize(16, 16));
  list_->setAlternatingRowColors(true);
  v->addWidget(list_, 1);
  auto* row = new QHBoxLayout;
  revert_ = new QPushButton(makeIcon("undo", 16), "Seçimi geri al", this);
  revert_->setToolTip("Seçimi geri al (Revert selection): bir önceki seçimi geri yükler");
  clear_ = new QPushButton("Temizle", this);
  clear_->setToolTip("Seçimi temizle (Esc)");
  similar_ = new QPushButton("Benzerini seç", this);
  similar_->setToolTip("Benzerini seç (Select similar) — 2. fazda");
  similar_->setEnabled(false);
  for (QPushButton* b : {revert_, clear_, similar_}) row->addWidget(b);
  row->addStretch();
  v->addLayout(row);
  connect(revert_, &QPushButton::clicked, sel_, &SelectionModel::revert);
  connect(clear_, &QPushButton::clicked, sel_, &SelectionModel::clear);
  connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
    const int i = list_->row(it);
    if (i >= 0 && i < sel_->count()) sel_->set({sel_->items()[i]});
  });
  connect(type_, &QComboBox::activated, this, [this](int i) { sel_->setFilter(SelFilter(type_->itemData(i).toInt())); });
  connect(sel_, &SelectionModel::filterChanged, this, [this](SelFilter f) {
    const int i = type_->findData(int(f));
    if (i >= 0 && i != type_->currentIndex()) type_->setCurrentIndex(i);
  });
  connect(sel_, &SelectionModel::changed, this, &SelectionPanel::refresh);
  refresh();
}

void SelectionPanel::refresh() {
  list_->clear();
  for (const auto& it : sel_->items()) {
    auto* li = new QListWidgetItem(selKindIcon(it.kind), QString("%1  —  %2").arg(selKindText(it.kind), it.name), list_);
    li->setToolTip(it.name);
  }
  header_->setText(headerText(sel_->items()));
  summary_->setText(sel_->isEmpty() ? QString("Seçim yok. Görünümde ya da Yapı ağacında tıklayın (Ctrl = ekle).")
                                    : selectionSummary(sel_->items()));
  revert_->setEnabled(sel_->canRevert());
  clear_->setEnabled(!sel_->isEmpty());
  const int fi = type_->findData(int(sel_->filter()));
  if (fi >= 0 && fi != type_->currentIndex()) type_->setCurrentIndex(fi);
}

} // namespace pcd
