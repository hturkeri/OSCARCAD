// SC-UX shell (WP-A): ribbon widgets. See Ribbon.h.
#include "Ribbon.h"
#include "Icons.h"
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QEvent>
#include <QTimer>
#include <algorithm>

namespace pcd {

namespace {
// SpaceClaim 2025-like palette: pale blue-grey ribbon body, white selected tab, blue File button,
// orange "active tool" (SPEC §1.2 #4).
const char* kRibbonStyle = R"(
pcd--Ribbon { background: #e9eef5; }
QWidget#ribbonHeader { background: #dfe6ef; }
QStackedWidget#ribbonPages { background: #f8fafc; border-top: 1px solid #b8c4d3; border-bottom: 1px solid #b8c4d3; }
QWidget[ribbonPage="true"] { background: #f8fafc; }
QToolButton#ribbonFileButton { background: #1d5fae; color: white; font-weight: bold; border: none;
  padding: 3px 14px 3px 14px; margin: 0px 4px 0px 0px; }
QToolButton#ribbonFileButton:hover { background: #2b74c9; }
QToolButton#ribbonFileButton::menu-indicator { image: none; width: 0px; }
QTabBar#ribbonTabBar { background: transparent; }
QTabBar#ribbonTabBar::tab { background: transparent; color: #24364d; padding: 4px 16px 4px 16px; margin: 2px 1px 0px 1px;
  border: 1px solid transparent; border-bottom: none; }
QTabBar#ribbonTabBar::tab:hover { background: #eef3f9; border-color: #c7d2df; }
QTabBar#ribbonTabBar::tab:selected { background: #f8fafc; border-color: #b8c4d3; color: #0d3f7a; }
pcd--RibbonGroup { background: transparent; border: none; border-right: 1px solid #d3dbe5; }
QLabel#ribbonGroupTitle { color: #5a6b80; font-size: 10px; padding: 0px 2px 1px 2px; }
pcd--RibbonGroup[contextual="true"] QLabel#ribbonGroupTitle { color: #7a4b00; background: #fbe7bf; }
QToolBar[ribbonBar="true"] { background: transparent; border: none; spacing: 1px; padding: 0px; }
QToolBar[ribbonBar="true"] QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 1px 3px 1px 3px; color: #1b2b40; }
QToolBar[ribbonBar="true"] QToolButton:hover { background: #dde9f7; border-color: #9fbbe0; }
QToolBar[ribbonBar="true"] QToolButton:pressed { background: #c8dcf3; }
QToolBar[ribbonBar="true"] QToolButton:checked { background: #f7c97a; border: 1px solid #e09a2b; }
QToolBar[ribbonBar="true"] QToolButton[ribbonToggle="true"]:checked { background: #d9e7f7; border: 1px solid #93b4dc; }
QToolBar[ribbonBar="true"] QToolButton:disabled { color: #9aa6b4; }
QToolBar#quickAccessBar { background: transparent; border: none; spacing: 0px; padding: 0px 6px 0px 2px; }
QToolBar#quickAccessBar QToolButton { border: 1px solid transparent; padding: 1px; }
QToolBar#quickAccessBar QToolButton:hover { background: #eef3f9; border-color: #9fbbe0; }
QLabel#ribbonTitle { color: #3b4c61; padding-right: 10px; }
QToolButton[ribbonCollapse="true"] { border: 1px solid transparent; border-radius: 2px; padding: 1px 3px 1px 3px; color: #1b2b40; }
QToolButton[ribbonCollapse="true"]:hover { background: #dde9f7; border-color: #9fbbe0; }
QToolButton[ribbonCollapse="true"]::menu-indicator { image: none; width: 0px; }
QToolButton#ribbonOverflow { background: #e4ebf4; border: 1px solid #b8c4d3; border-radius: 2px; color: #24364d; font-weight: bold; }
QToolButton#ribbonOverflow:hover { background: #dde9f7; border-color: #9fbbe0; }
QToolButton#ribbonOverflow::menu-indicator { image: none; width: 0px; }
)";
} // namespace

// ============================================================================ RibbonGroup
RibbonGroup::RibbonGroup(const QString& key, const QString& title, QWidget* parent) : QFrame(parent), key_(key) {
  setObjectName("ribbonGroup." + key);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(3, 2, 3, 0);
  v->setSpacing(0);
  content_ = new QWidget(this);
  content_->setObjectName("ribbonGroupContent");
  row_ = new QHBoxLayout(content_);
  row_->setContentsMargins(0, 0, 0, 0);
  row_->setSpacing(2);
  v->addWidget(content_, 1, Qt::AlignLeft | Qt::AlignTop);
  title_ = new QLabel(title, this);
  title_->setObjectName("ribbonGroupTitle");
  title_->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
  v->addWidget(title_, 0);
  // SC23 WP-S: never squeezed (no elided labels); the Ribbon shrinks groups level by level instead
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

QString RibbonGroup::title() const { return title_->text(); }

QToolBar* RibbonGroup::newBar(bool large, bool iconOnly, QHBoxLayout* row) {
  auto* tb = new QToolBar(row == row_ ? content_ : compact_);
  tb->setProperty("ribbonBar", true);
  tb->setMovable(false);
  tb->setFloatable(false);
  tb->setOrientation(large ? Qt::Horizontal : Qt::Vertical);
  tb->setIconSize(large ? QSize(32, 32) : QSize(16, 16));
  tb->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
  tb->setContextMenuPolicy(Qt::PreventContextMenu);
  // SC-VIDEO2D: bars created after the ribbon was polished (Görüntü > Katmanlar / 2B çizim, made by Import2DController)
  // kept QToolBarLayout's default 4 px margins although kRibbonStyle says "padding: 0": their column of 3 small buttons
  // needed 76 px of the 69 px page and "Görünüşü yerleştir" fell into the toolbar's overflow chevron (not clickable).
  // A per-bar sheet re-applies the margins immediately (bars that already had 0 are unchanged).
  tb->setStyleSheet("QToolBar { padding: 0px; border: none; }");
  // SC23: the sheet alone is not always applied before the first layout (bars made on a hidden page / later by
  // other controllers): a column of three 22 px buttons must never fold its third button into the » extension
  if (tb->layout()) { tb->layout()->setContentsMargins(0, 0, 0, 0); tb->layout()->setSpacing(1); }
  row->addWidget(tb, 0, Qt::AlignTop);
  return tb;
}

static void tuneMenuButton(QToolButton* b, QAction* a) {
  if (!b) return;
  if (a->menu()) b->setPopupMode(QToolButton::InstantPopup);
  // a checkable action outside an exclusive group is a view toggle (light blue), not an "active tool" (orange)
  if (a->isCheckable() && !a->actionGroup()) b->setProperty("ribbonToggle", true);
}

QToolButton* RibbonGroup::addLarge(QAction* a) {
  if (!largeBar_) largeBar_ = newBar(true, false, row_);
  smallBar_ = nullptr;
  largeBar_->addAction(a);
  hasLarge_ = true;
  items_ << Item{a, true, false, false};
  if (!order_.contains(a)) order_ << a;
  auto* b = qobject_cast<QToolButton*>(largeBar_->widgetForAction(a));
  if (b) {
    b->setMinimumWidth(46);
    b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    tuneMenuButton(b, a);
    buttons_.insert(a, b);
  }
  return b;
}

QToolButton* RibbonGroup::addSmall(QAction* a, bool iconOnly) {
  if (!smallBar_ || smallCount_ >= 3 || smallIconOnly_ != iconOnly) {
    smallBar_ = newBar(false, iconOnly, row_);
    smallIconOnly_ = iconOnly;
    smallCount_ = 0;
  }
  largeBar_ = nullptr;
  smallBar_->addAction(a);
  ++smallCount_;
  items_ << Item{a, false, iconOnly, false};
  if (!order_.contains(a)) order_ << a;
  auto* b = qobject_cast<QToolButton*>(smallBar_->widgetForAction(a));
  if (b) {
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setFixedHeight(22);
    tuneMenuButton(b, a);
    buttons_.insert(a, b);
  }
  return b;
}

void RibbonGroup::newColumn() {
  smallBar_ = nullptr;
  largeBar_ = nullptr;
  items_ << Item{nullptr, false, false, true};
}

void RibbonGroup::addWidget(QWidget* w) {
  newColumn();
  hasWidget_ = true;
  w->setParent(content_);
  row_->addWidget(w, 0, Qt::AlignVCenter);
}

void RibbonGroup::addSeparator() {
  newColumn();
  auto* line = new QFrame(content_);
  line->setFrameShape(QFrame::VLine);
  line->setStyleSheet("color:#dbe2ea;");
  row_->addWidget(line);
}

QToolButton* RibbonGroup::buttonFor(const QAction* a) const {
  if (level_ == Level::Compact && compactButtons_.contains(a)) return compactButtons_.value(a);
  return buttons_.value(a, nullptr);
}
QList<QToolButton*> RibbonGroup::buttons() const {
  QList<QToolButton*> l;
  for (QAction* a : order_) if (QToolButton* b = buttonFor(a)) l << b;
  return l;
}

void RibbonGroup::setContextual(bool on) {
  contextual_ = on;
  setProperty("contextual", on);
  style()->unpolish(this);
  style()->polish(this);
  title_->style()->unpolish(title_);
  title_->style()->polish(title_);
}

// ---- SC23 WP-S: levels
void RibbonGroup::buildCompact() {
  if (compact_) return;
  compact_ = new QWidget(this);
  compact_->setObjectName("ribbonGroupCompact");
  auto* row = new QHBoxLayout(compact_);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(2);
  QToolBar* bar = nullptr;
  int count = 0;
  bool barIconOnly = false;
  for (const Item& it : items_) {
    if (it.column) { bar = nullptr; continue; }
    const bool iconOnly = it.large ? false : it.iconOnly;
    if (!bar || count >= 3 || barIconOnly != iconOnly) {
      bar = newBar(false, iconOnly, row);
      barIconOnly = iconOnly;
      count = 0;
    }
    bar->addAction(it.a);
    ++count;
    if (auto* b = qobject_cast<QToolButton*>(bar->widgetForAction(it.a))) {
      b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      b->setFixedHeight(22);
      tuneMenuButton(b, it.a);
      // two-line large labels ("Sketch'i\nBitir") read as one line beside a small icon
      QAction* a = it.a;
      auto oneLine = [b, a] { b->setText(a->iconText().replace('\n', ' ')); };
      oneLine();
      connect(a, &QAction::changed, b, oneLine, Qt::QueuedConnection);   // after QToolButton re-reads iconText()
      connect(a, &QAction::toggled, b, oneLine, Qt::QueuedConnection);
      compactButtons_.insert(it.a, b);
    }
  }
  row->addStretch(1);
  static_cast<QVBoxLayout*>(layout())->insertWidget(1, compact_, 1, Qt::AlignLeft | Qt::AlignTop);
  compact_->hide();
}

void RibbonGroup::setCollapsedIcon(const QIcon& icon) {
  collapsedIcon_ = icon;
  if (collapseBtn_) collapseBtn_->setIcon(icon);
}

void RibbonGroup::ensureCollapseButton() {
  if (collapseBtn_) return;
  collapseBtn_ = new QToolButton(this);
  collapseBtn_->setObjectName("ribbonCollapse." + key_);
  collapseBtn_->setProperty("ribbonCollapse", true);
  collapseBtn_->setText(title_->text() + QString::fromUtf8(" ▾"));
  QIcon ic = collapsedIcon_;
  if (ic.isNull())
    for (QAction* a : order_) if (!a->icon().isNull()) { ic = a->icon(); break; }
  collapseBtn_->setIcon(ic);
  collapseBtn_->setIconSize(QSize(32, 32));
  collapseBtn_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  collapseBtn_->setAutoRaise(true);
  collapseBtn_->setPopupMode(QToolButton::InstantPopup);
  collapseBtn_->setMinimumWidth(52);
  collapseBtn_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  QStringList names;
  for (QAction* a : order_) names << a->text().replace('\n', ' ');
  collapseBtn_->setToolTip(QString("%1: %2").arg(title_->text(), names.join(", ")));
  menu_ = new QMenu(collapseBtn_);
  menu_->setObjectName("ribbonCollapseMenu." + key_);
  menu_->setToolTipsVisible(true);
  connect(menu_, &QMenu::aboutToShow, this, [this] { fillMenu(); });
  collapseBtn_->setMenu(menu_);
  fillMenu();
  static_cast<QVBoxLayout*>(layout())->insertWidget(0, collapseBtn_, 1, Qt::AlignHCenter | Qt::AlignTop);
  collapseBtn_->hide();
}

void RibbonGroup::fillMenu() {
  if (!menu_) return;
  menu_->clear();
  for (const Item& it : items_) {
    if (it.column) continue;
    menu_->addAction(it.a);   // the same QAction: enabled / checked state and shortcut follow
  }
  if (hasWidget_) {
    QAction* note = menu_->addAction(QString::fromUtf8("(alanlar için pencereyi genişletin)"));
    note->setEnabled(false);
  }
}

int RibbonGroup::widthAt(Level l) const {
  auto* self = const_cast<RibbonGroup*>(this);
  QWidget* w = content_;
  if (l == Level::Compact && canCompact()) { self->buildCompact(); w = compact_; }
  if (l == Level::Collapsed) { self->ensureCollapseButton(); w = collapseBtn_; }
  const int cw = w->layout() ? w->layout()->sizeHint().width() : w->sizeHint().width();
  const int tw = title_->sizeHint().width();
  const QMargins m = layout()->contentsMargins();
  return std::max(cw, tw) + m.left() + m.right() + 1;   // + the right border line
}

void RibbonGroup::setLevel(Level l) {
  if (int(l) > int(maxLevel_)) l = maxLevel_;
  if (l == Level::Compact && !canCompact()) l = Level::Expanded;
  const int w = widthAt(l);
  if (l != level_) {
    level_ = l;
    content_->setVisible(l == Level::Expanded);
    if (compact_) compact_->setVisible(l == Level::Compact);
    if (collapseBtn_) collapseBtn_->setVisible(l == Level::Collapsed);
    title_->setVisible(true);
  }
  if (width() != w || minimumWidth() != w) setFixedWidth(w);
}

// ============================================================================ Ribbon
Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
  setObjectName("ribbon");
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet(kRibbonStyle);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName("ribbonHeader");
  header->setAttribute(Qt::WA_StyledBackground, true);
  auto* h = new QHBoxLayout(header);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(0);
  fileBtn_ = new QToolButton(header);
  fileBtn_->setObjectName("ribbonFileButton");
  fileBtn_->setText("Dosya");
  fileBtn_->setToolTip("Dosya menüsü (File) — Alt+F");
  fileBtn_->setPopupMode(QToolButton::InstantPopup);
  fileBtn_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  h->addWidget(fileBtn_);
  qat_ = new QToolBar(header);
  qat_->setObjectName("quickAccessBar");
  qat_->setWindowTitle("Hızlı erişim");
  qat_->setMovable(false);
  qat_->setFloatable(false);
  qat_->setIconSize(QSize(16, 16));
  qat_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  h->addWidget(qat_);
  auto* sep = new QFrame(header);
  sep->setFrameShape(QFrame::VLine);
  sep->setStyleSheet("color:#b8c4d3;");
  h->addWidget(sep);
  h->addSpacing(6);
  tabs_ = new QTabBar(header);
  tabs_->setObjectName("ribbonTabBar");
  tabs_->setDrawBase(false);
  tabs_->setExpanding(false);
  tabs_->setDocumentMode(true);
  tabs_->setUsesScrollButtons(false);
  tabs_->setElideMode(Qt::ElideNone);
  h->addWidget(tabs_, 0, Qt::AlignBottom);
  h->addStretch(1);
  title_ = new QLabel(header);
  title_->setObjectName("ribbonTitle");
  h->addWidget(title_);
  v->addWidget(header);

  pages_ = new QStackedWidget(this);
  pages_->setObjectName("ribbonPages");
  pages_->setFixedHeight(kPageHeight);
  // never force the main window wider than the screen: groups beyond the right edge are clipped instead
  pages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  pages_->setMinimumWidth(0);
  v->addWidget(pages_);
  pages_->installEventFilter(this);
  // SC23 WP-S (S-P0-1): "»" lists the commands of groups that do not fit even when collapsed
  overflow_ = new QToolButton(this);
  overflow_->setObjectName("ribbonOverflow");
  overflow_->setText(QString::fromUtf8("»"));
  overflow_->setToolTip(QString::fromUtf8("Sığmayan gruplar (pencereyi genişletin ya da buradan seçin)"));
  overflow_->setPopupMode(QToolButton::InstantPopup);
  overflowMenu_ = new QMenu(overflow_);
  overflowMenu_->setObjectName("ribbonOverflowMenu");
  overflowMenu_->setToolTipsVisible(true);
  overflow_->setMenu(overflowMenu_);
  connect(overflowMenu_, &QMenu::aboutToShow, this, [this] {
    overflowMenu_->clear();
    for (const auto& g : overflowGroups_) {
      if (!g) continue;
      QMenu* sub = overflowMenu_->addMenu(g->title());
      for (QAction* a : g->actions()) sub->addAction(a);
      sub->setEnabled(g->isEnabled());
    }
  });
  overflow_->hide();
  connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
    if (i < 0 || i >= keys_.size()) return;
    QWidget* page = pageByKey_.value(keys_[i]);
    // groups filled while their page was hidden may carry stale size hints: re-layout before showing
    for (RibbonGroup* g : groupsByTab_.value(keys_[i])) {
      for (QToolBar* tb : g->findChildren<QToolBar*>()) { tb->updateGeometry(); if (tb->layout()) tb->layout()->invalidate(); }
      if (g->layout()) g->layout()->invalidate();
      g->updateGeometry();
    }
    if (page && page->layout()) page->layout()->invalidate();
    pages_->setCurrentWidget(page);
    fitGroups();
    emit currentTabChanged(keys_[i]);
  });
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void Ribbon::setFileMenu(QMenu* m) { fileBtn_->setMenu(m); }

int Ribbon::addTab(const QString& key, const QString& title) {
  auto* page = new QWidget(pages_);
  page->setObjectName("ribbonTab." + key);
  page->setProperty("ribbonPage", true);
  page->setAttribute(Qt::WA_StyledBackground, true);
  page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* h = new QHBoxLayout(page);
  h->setContentsMargins(2, 1, 2, 1);
  h->setSpacing(0);
  h->addStretch(1);   // groups are inserted before this trailing stretch
  pages_->addWidget(page);
  pageByKey_.insert(key, page);
  keys_ << key;
  const int idx = tabs_->addTab(title);
  return idx;
}

RibbonGroup* Ribbon::addGroup(const QString& tabKey, const QString& key, const QString& title) {
  QWidget* page = pageByKey_.value(tabKey);
  if (!page) return nullptr;
  auto* g = new RibbonGroup(key, title, page);
  auto* h = static_cast<QHBoxLayout*>(page->layout());
  h->insertWidget(h->count() - 1, g);
  groups_.insert(key, g);
  groupsByTab_[tabKey] << g;
  g->installEventFilter(this);   // show / hide / content changes -> fit again
  scheduleFit();
  return g;
}

void Ribbon::addStretch(const QString& tabKey) {
  if (QWidget* page = pageByKey_.value(tabKey)) {
    auto* h = static_cast<QHBoxLayout*>(page->layout());
    h->insertStretch(h->count() - 1, 1);
  }
}

QList<RibbonGroup*> Ribbon::groupsOf(const QString& tabKey) const { return groupsByTab_.value(tabKey); }

QStringList Ribbon::tabTexts() const {
  QStringList l;
  for (int i = 0; i < tabs_->count(); ++i) l << tabs_->tabText(i);
  return l;
}

QString Ribbon::currentTabKey() const {
  const int i = tabs_->currentIndex();
  return i >= 0 && i < keys_.size() ? keys_[i] : QString();
}

void Ribbon::setCurrentTab(const QString& key) {
  const int i = keys_.indexOf(key);
  if (i >= 0) tabs_->setCurrentIndex(i);
}

QWidget* Ribbon::page(const QString& key) const { return pageByKey_.value(key, nullptr); }

void Ribbon::setTitleText(const QString& t) { title_->setText(t); }

void Ribbon::setTabToolTip(const QString& key, const QString& tip) {
  const int i = keys_.indexOf(key);
  if (i >= 0) tabs_->setTabToolTip(i, tip);
}

// ---- SC23 WP-S (S-P0-1): width management
QSize Ribbon::minimumSizeHint() const {
  // never forces the window wider: groups shrink / overflow instead
  const int h = (tabs_ ? tabs_->sizeHint().height() : 24) + kPageHeight;
  return QSize(240, h);
}

void Ribbon::scheduleFit() {
  if (fitQueued_) return;
  fitQueued_ = true;
  QTimer::singleShot(0, this, [this] { fitQueued_ = false; fitGroups(); });
}

bool Ribbon::eventFilter(QObject* o, QEvent* e) {
  if (!fitting_) {
    if (o == pages_ && e->type() == QEvent::Resize) fitGroups();
    else if (qobject_cast<RibbonGroup*>(o) && (e->type() == QEvent::ShowToParent || e->type() == QEvent::HideToParent ||
                                               e->type() == QEvent::LayoutRequest))
      scheduleFit();
  }
  return QWidget::eventFilter(o, e);
}

QList<RibbonGroup*> Ribbon::fitGroups() {
  overflowGroups_.clear();
  const QString key = currentTabKey();
  QWidget* page = pageByKey_.value(key);
  if (!page || fitting_ || !isVisible() || pages_->width() < 50) return {};   // measured once shown (Resize)
  fitting_ = true;
  using L = RibbonGroup::Level;
  QList<RibbonGroup*> gs;
  for (RibbonGroup* g : groupsByTab_.value(key)) if (!g->isHidden()) gs << g;
  const QMargins pm = page->layout() ? page->layout()->contentsMargins() : QMargins();
  const int avail = std::max(0, pages_->width() - pm.left() - pm.right());
  QHash<RibbonGroup*, L> lv;
  int total = 0;
  for (RibbonGroup* g : gs) { lv[g] = L::Expanded; total += g->widthAt(L::Expanded); }
  // collapse order: lower priority first; equal priority -> the right-most group first
  QList<RibbonGroup*> order;
  for (RibbonGroup* g : gs) if (g->collapsePriority() >= 0) order << g;
  std::stable_sort(order.begin(), order.end(), [&gs](RibbonGroup* a, RibbonGroup* b) {
    if (a->collapsePriority() != b->collapsePriority()) return a->collapsePriority() < b->collapsePriority();
    return gs.indexOf(a) > gs.indexOf(b);
  });
  for (RibbonGroup* g : order) {
    if (total <= avail) break;
    if (!g->canCompact() || g->maxLevel() == L::Expanded) continue;
    const int w0 = g->widthAt(L::Expanded), w1 = g->widthAt(L::Compact);
    if (w1 < w0) { total -= w0 - w1; lv[g] = L::Compact; }
  }
  for (RibbonGroup* g : order) {
    if (total <= avail) break;
    if (g->maxLevel() != L::Collapsed) continue;
    const int cur = g->widthAt(lv[g]), w2 = g->widthAt(L::Collapsed);
    if (w2 < cur) { total -= cur - w2; lv[g] = L::Collapsed; }
  }
  bool changed = false;
  for (RibbonGroup* g : gs) {
    if (g->level() != lv[g]) changed = true;
    g->setLevel(lv[g]);
  }
  // still too wide: the groups beyond the right edge (minus the "»" button) are listed in the overflow menu
  const int chevronW = 22;
  if (total > avail) {
    int x = 0;
    for (RibbonGroup* g : gs) {
      const int w = g->widthAt(lv[g]);
      if (x + w > avail - chevronW) overflowGroups_ << g;
      x += w;
    }
  }
  if (!overflowGroups_.isEmpty()) {
    const QRect pr = pages_->geometry();
    overflow_->setGeometry(pr.right() - chevronW, pr.top() + 3, chevronW, pr.height() - 6);
    overflow_->show();
    overflow_->raise();
  } else {
    overflow_->hide();
  }
  if (page->layout()) page->layout()->activate();
  fitting_ = false;
  if (changed) emit groupsFitted();
  QList<RibbonGroup*> out;
  for (const auto& g : overflowGroups_) if (g) out << g;
  return out;
}

} // namespace pcd
