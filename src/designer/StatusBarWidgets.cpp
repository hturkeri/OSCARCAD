// SC-UX shell (WP-A): status bar widgets. See StatusBarWidgets.h.
#include "StatusBarWidgets.h"
#include "Icons.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QStatusBar>
#include <QToolButton>

namespace pcd {

namespace {
QLabel* panelLabel(const QString& name, const QString& text, QWidget* parent) {
  auto* l = new QLabel(text, parent);
  l->setObjectName(name);
  l->setFrameStyle(QFrame::Panel | QFrame::Sunken);
  l->setContentsMargins(6, 0, 6, 0);
  return l;
}
} // namespace

StatusBarWidgets::StatusBarWidgets(QStatusBar* sb, SelectionModel* sel, QObject* parent) : QObject(parent), sb_(sb), sel_(sel) {
  quickMeasure_ = panelLabel("quickMeasure", "", sb);
  quickMeasure_->setToolTip("Hızlı ölçüm (Quick measure): seçimin boyutu / alanı / uzunluğu");
  quickMeasure_->setMinimumWidth(120);
  quickMeasure_->hide();
  selectionList_ = panelLabel("selectionList", "Seçim yok", sb);
  selectionList_->setToolTip("Seçim listesi (Selection)");
  selectionList_->setMinimumWidth(150);
  selectionList_->setMaximumWidth(380);
  revert_ = new QToolButton(sb);
  revert_->setObjectName("selectionRevert");
  revert_->setIcon(makeIcon("undo", 16));
  revert_->setAutoRaise(true);
  revert_->setToolTip("Seçimi geri al (Revert selection)");
  filter_ = new QComboBox(sb);
  filter_->setObjectName("selectionFilter");
  filter_->setToolTip("Seçim filtresi (Selection filter)");
  for (SelFilter f : {SelFilter::Smart, SelFilter::Face, SelFilter::Edge, SelFilter::Body, SelFilter::Datum, SelFilter::Sketch})
    filter_->addItem(selFilterText(f), int(f));
  filter_->setFocusPolicy(Qt::NoFocus);
  sb->addPermanentWidget(quickMeasure_);
  sb->addPermanentWidget(selectionList_);
  sb->addPermanentWidget(revert_);
  sb->addPermanentWidget(filter_);
  connect(filter_, &QComboBox::activated, this, [this](int i) { sel_->setFilter(SelFilter(filter_->itemData(i).toInt())); });
  connect(sel_, &SelectionModel::filterChanged, this, [this](SelFilter f) {
    const int i = filter_->findData(int(f));
    if (i >= 0 && i != filter_->currentIndex()) filter_->setCurrentIndex(i);
  });
  connect(revert_, &QToolButton::clicked, sel_, &SelectionModel::revert);
  connect(sel_, &SelectionModel::changed, this, &StatusBarWidgets::refreshSelection);
  refreshSelection();
}

void StatusBarWidgets::addPermanent(QWidget* w) { sb_->addPermanentWidget(w); }

void StatusBarWidgets::setViewActions(QAction* prev, QAction* next) {
  for (auto [btn, act, name] : {std::tuple{&prev_, prev, "statusPrevView"}, std::tuple{&next_, next, "statusNextView"}}) {
    if (!act) continue;
    *btn = new QToolButton(sb_);
    (*btn)->setObjectName(name);
    (*btn)->setDefaultAction(act);
    (*btn)->setAutoRaise(true);
    (*btn)->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sb_->addPermanentWidget(*btn);
  }
}

void StatusBarWidgets::setQuickMeasure(const QString& text) {
  quickMeasure_->setText(text);
  quickMeasure_->setVisible(!text.isEmpty());
}

void StatusBarWidgets::refreshSelection() {
  const QString s = selectionSummary(sel_->items());
  selectionList_->setText(s.isEmpty() ? QString("Seçim yok") : s);
  QStringList all;
  for (const auto& it : sel_->items()) all << it.name;
  selectionList_->setToolTip(all.isEmpty() ? QString("Seçim listesi (Selection)") : all.join("\n"));
  revert_->setEnabled(sel_->canRevert());
}

} // namespace pcd
