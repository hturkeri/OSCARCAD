// SC23 WP-S (S-P1-4): Views tab. See ViewsPanel.h.
#include "ViewsPanel.h"
#include "Icons.h"
#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pcd {

namespace {
constexpr int kActionRole = Qt::UserRole + 1;   // QAction* (standard rows)
constexpr int kCameraRole = Qt::UserRole + 2;   // saved camera (QVariant from the capture hook)
}

ViewsPanel::ViewsPanel(QWidget* parent) : QWidget(parent) {
  setObjectName("viewsPanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(3, 3, 3, 3);
  v->setSpacing(3);
  auto* row = new QHBoxLayout;
  create_ = new QPushButton(makeIcon("views", 16), "Oluştur", this);
  create_->setObjectName("viewsCreate");
  create_->setToolTip("Görünüm oluştur (Create): geçerli bakış yönünü listeye ekler (yalnız bu oturumda; belgeye kaydedilmez)");
  delete_ = new QPushButton(makeIcon("delete", 16), "Sil", this);
  delete_->setObjectName("viewsDelete");
  row->addWidget(create_);
  row->addWidget(delete_);
  row->addStretch();
  v->addLayout(row);
  list_ = new QTreeWidget(this);
  list_->setObjectName("viewsList");
  list_->setColumnCount(2);
  list_->setHeaderLabels({"Ad", "Kısayol"});
  list_->setRootIsDecorated(false);
  list_->setIconSize(QSize(16, 16));
  list_->setAlternatingRowColors(true);
  list_->header()->setStretchLastSection(false);
  list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  v->addWidget(list_, 1);
  connect(create_, &QPushButton::clicked, this, [this] { createView(); });
  connect(delete_, &QPushButton::clicked, this, [this] { deleteCurrent(); });
  connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) { applyRow(list_->indexOfTopLevelItem(it)); });
  connect(list_, &QTreeWidget::currentItemChanged, this, [this] { syncButtons(); });
  syncButtons();
}

void ViewsPanel::addStandardView(QAction* a) {
  if (!a) return;
  auto* it = new QTreeWidgetItem;
  it->setText(0, a->text());
  it->setIcon(0, a->icon());
  it->setText(1, a->shortcut().toString(QKeySequence::NativeText));
  it->setToolTip(0, a->toolTip());
  it->setData(0, kActionRole, QVariant::fromValue<QObject*>(a));
  list_->insertTopLevelItem(standard_++, it);
  // the shortcut table is applied after the panel is built: follow the action
  connect(a, &QAction::changed, list_, [it, a] { it->setText(1, a->shortcut().toString(QKeySequence::NativeText)); });
  syncButtons();
}

void ViewsPanel::setCameraHooks(std::function<QVariant()> capture, std::function<void(const QVariant&)> apply) {
  capture_ = std::move(capture);
  apply_ = std::move(apply);
  syncButtons();
}

int ViewsPanel::savedViewCount() const { return list_->topLevelItemCount() - standard_; }

QString ViewsPanel::createView() {
  if (!capture_) return QString();
  const QVariant cam = capture_();
  if (!cam.isValid()) return QString();
  QString name;
  do { name = QString("Görünüm%1").arg(nextId_++); } while (!list_->findItems(name, Qt::MatchExactly, 0).isEmpty());
  auto* it = new QTreeWidgetItem(list_);
  it->setText(0, name);
  it->setIcon(0, makeIcon("views", 16));
  it->setToolTip(0, "Kayıtlı görünüm (yalnız bu oturumda): çift tıklayın ya da Enter");
  it->setData(0, kCameraRole, cam);
  list_->setCurrentItem(it);
  syncButtons();
  return name;
}

bool ViewsPanel::deleteCurrent() {
  QTreeWidgetItem* it = list_->currentItem();
  const int row = it ? list_->indexOfTopLevelItem(it) : -1;
  if (row < standard_) return false;
  delete list_->takeTopLevelItem(row);
  syncButtons();
  return true;
}

bool ViewsPanel::applyRow(int row) {
  QTreeWidgetItem* it = list_->topLevelItem(row);
  if (!it) return false;
  if (auto* a = qobject_cast<QAction*>(it->data(0, kActionRole).value<QObject*>())) {
    a->trigger();
    return true;
  }
  const QVariant cam = it->data(0, kCameraRole);
  if (!cam.isValid() || !apply_) return false;
  apply_(cam);
  return true;
}

void ViewsPanel::syncButtons() {
  QTreeWidgetItem* it = list_->currentItem();
  const bool saved = it && list_->indexOfTopLevelItem(it) >= standard_;
  delete_->setEnabled(saved);
  delete_->setToolTip(saved ? QString("Seçili kayıtlı görünümü sil (Delete)")
                            : QString("Sil (Delete): yalnız 'Oluştur' ile kaydedilen görünümler silinebilir; standart görünümler kalır"));
  create_->setEnabled(bool(capture_));
}

} // namespace pcd
