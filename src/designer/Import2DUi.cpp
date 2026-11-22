// SC-2D-IMPORT — UI of the 2D-drawing-to-3D workflow. See Import2DUi.h and docs/import2d/IMPORT2D.md.
#include "Import2DUi.h"
#include "Icons.h"
#include "MainWindow.h"
#include "Ribbon.h"
#include "StructurePanel.h"
#include "pc/Regions.h"   // SC-VIDEO2D
#include "pc/SketchSolver.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <functional>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

using namespace pc;
namespace i2 = pcd::import2d;

namespace pcd {

namespace {
int patternOf(const std::string& s) { return s == "dashed" ? 1 : s == "center" ? 2 : s == "dotted" ? 3 : 0; }
QString styleText(const std::string& s) {
  if (s == "dashed") return "Kesikli";
  if (s == "center") return "Eksen";
  if (s == "dotted") return "Noktalı";
  return "Sürekli";
}
QIcon swatch(const QString& color, const std::string& style) {
  QPixmap pm(28, 14);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setPen(QColor(90, 90, 90));
  p.setBrush(QColor(color));
  p.drawRect(0, 1, 11, 11);
  QPen pen(QColor(color), 2);
  if (style == "dashed") pen.setStyle(Qt::DashLine);
  else if (style == "center") pen.setStyle(Qt::DashDotLine);
  else if (style == "dotted") pen.setStyle(Qt::DotLine);
  p.setPen(pen);
  p.drawLine(14, 7, 27, 7);
  return QIcon(pm);
}
QString fmtPt(const gp_Pnt& p) {
  auto f = [](double v) { return QString::number(std::fabs(v) < 5e-10 ? 0.0 : v, 'g', 8); };
  return QString("%1; %2; %3").arg(f(p.X()), f(p.Y()), f(p.Z()));
}
bool parsePt(const QString& s, gp_Pnt& out) {
  const QStringList l = QString(s).replace(',', ' ').replace(';', ' ').split(' ', Qt::SkipEmptyParts);
  if (l.size() != 3) return false;
  bool a, b, c;
  out = gp_Pnt(l[0].toDouble(&a), l[1].toDouble(&b), l[2].toDouble(&c));
  return a && b && c;
}
const Sketch* solvedSketch(const MainWindow* w, const Document& d, const model::ModelResult& m, const std::string& sid) {
  (void)w;
  auto it = m.solvedSketches.find(sid);
  if (it != m.solvedSketches.end()) return &it->second;
  const Feature* f = d.feature(sid);
  return f && f->sketch ? &*f->sketch : nullptr;
}
bool frameOfSketch(const model::ModelResult& m, const std::string& sid, gp_Ax3& fr) {
  for (const auto& fi : m.info) if (fi.featureId == sid && fi.type == FeatureType::Sketch) { fr = fi.sketchFrame; return true; }
  return false;
}
} // namespace

// ============================================================================================ LayerCommand
LayerCommand::LayerCommand(MainWindow* w, LayerTable before, LayerTable after, const QString& text)
    : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)) {}
void LayerCommand::undo() { apply(before_); }
void LayerCommand::redo() { apply(after_); }
void LayerCommand::apply(const LayerTable& t) {
  w_->doc_.layers = t;
  w_->exprWork_.layers = t;
  if (w_->sketching_) w_->work_.layers = t;
  w_->refreshAll(true);
}

// ============================================================================================ LayersPanel
LayersPanel::LayersPanel(Import2DController* c, QWidget* parent) : QWidget(parent), c_(c) {
  setObjectName("layersPanel");
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(3, 3, 3, 3);
  l->setSpacing(3);
  auto* row = new QHBoxLayout;
  auto* bNew = new QPushButton(makeIcon("new", 16), "Yeni katman");
  bNew->setObjectName("layersNew");
  auto* bAll = new QPushButton(makeIcon("showall", 16), "Tümünü göster");
  bAll->setObjectName("layersShowAll");
  auto* bView = new QPushButton(makeIcon("select", 16), "Görünüş seç");
  bView->setObjectName("layersViewSelect");
  bView->setCheckable(true);
  auto* bPlace = new QPushButton(makeIcon("move", 16), "Yerleştir...");
  bPlace->setObjectName("layersPlaceView");
  bPlace->setToolTip("Görünüşü yerleştir: seçili görünüşü 90° katlayıp komşu görünüşe oturt");
  row->addWidget(bNew);
  row->addWidget(bAll);
  row->addWidget(bView);
  row->addWidget(bPlace);
  connect(bView, &QPushButton::toggled, this, [this](bool on) { if (c_->viewSelectAction()) c_->viewSelectAction()->setChecked(on); });
  connect(bPlace, &QPushButton::clicked, this, [this] { c_->openPlaceDialog(); });
  viewBtn_ = bView;
  row->addStretch();
  l->addLayout(row);
  tree_ = new QTreeWidget(this);
  tree_->setObjectName("layersTree");
  tree_->setColumnCount(4);
  tree_->setHeaderLabels({"Katman", "Renk", "Çizgi", "Öğe"});
  tree_->setRootIsDecorated(false);
  tree_->setIconSize(QSize(28, 14));
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int i = 1; i < 4; ++i) tree_->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
  l->addWidget(tree_, 1);
  auto* hint = new QLabel("Gizli katmanlar çizilmez ve seçilemez. Sağ tık: renk, çizgi, yalnız bunu göster, seçimi taşı.");
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#666; font-size:11px;");
  l->addWidget(hint);
  connect(bNew, &QPushButton::clicked, this, [this] { c_->newLayer(); });
  connect(bAll, &QPushButton::clicked, this, [this] { c_->showAllLayers(); });
  connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (updating_ || col != 0) return;
    const QString name = it->data(0, Qt::UserRole).toString();
    const bool on = it->checkState(0) == Qt::Checked;
    if (c_->layerVisible(name) != on) QTimer::singleShot(0, this, [this, name, on] { c_->setLayerVisible(name, on); });
  });
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int col) {
    const QString name = it->data(0, Qt::UserRole).toString();
    if (col == 1) {
      const QColor cur(it->data(1, Qt::UserRole).toString());
      const QColor nc = QColorDialog::getColor(cur, c_->window(), "Katman rengi");
      if (nc.isValid()) c_->setLayerColor(name, nc);
      return;
    }
    if (name == kDefaultLayerName) return;
    bool ok = false;
    const QString to = QInputDialog::getText(c_->window(), "Katmanı yeniden adlandır", "Yeni ad:", QLineEdit::Normal, name, &ok);
    if (ok) c_->renameLayer(name, to.trimmed());
  });
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) {
    QTreeWidgetItem* it = tree_->itemAt(p);
    if (!it) return;
    tree_->setCurrentItem(it);
    QMenu* m = buildMenu(it->data(0, Qt::UserRole).toString());
    m->popup(tree_->viewport()->mapToGlobal(p));
  });
}

QTreeWidgetItem* LayersPanel::row(const QString& layer) const {
  for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    if (tree_->topLevelItem(i)->data(0, Qt::UserRole).toString() == layer) return tree_->topLevelItem(i);
  return nullptr;
}

void LayersPanel::rebuild() {
  updating_ = true;
  const QString cur = tree_->currentItem() ? tree_->currentItem()->data(0, Qt::UserRole).toString() : QString();
  tree_->clear();
  const Document& d = c_->window()->document();
  for (const QString& name : c_->layerNames()) {
    const Layer* L = d.layers.find(name.toStdString());
    const Layer def = [] { Layer x; x.name = kDefaultLayerName; return x; }();
    const Layer& l = L ? *L : def;
    auto* it = new QTreeWidgetItem(tree_);
    it->setData(0, Qt::UserRole, name);
    it->setText(0, name);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(0, l.visible ? Qt::Checked : Qt::Unchecked);
    it->setIcon(1, swatch(QString::fromStdString(l.color), "solid"));
    it->setData(1, Qt::UserRole, QString::fromStdString(l.color));
    it->setToolTip(1, QString::fromStdString(l.color) + " (çift tık: renk)");
    it->setIcon(2, swatch(QString::fromStdString(l.color), l.lineStyle));
    it->setText(2, styleText(l.lineStyle));
    it->setData(2, Qt::UserRole, QString::fromStdString(l.lineStyle));
    it->setText(3, QString::number(c_->layerMemberCount(name)));
    if (!l.visible) it->setForeground(0, QColor(140, 140, 140));
    if (name == cur) it->setSelected(true), tree_->setCurrentItem(it);
  }
  updating_ = false;
}

QMenu* LayersPanel::buildMenu(const QString& layer) {
  auto* m = new QMenu(this);
  m->setObjectName("layersContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  auto add = [&](const char* obj, const QString& text, std::function<void()> fn, const QString& icon = QString()) {
    QAction* a = m->addAction(icon.isEmpty() ? QIcon() : makeIcon(icon, 16), text);
    a->setObjectName(obj);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    return a;
  };
  const bool vis = c_->layerVisible(layer);
  add("layShow", "Göster", [this, layer] { c_->setLayerVisible(layer, true); }, "showall")->setEnabled(!vis);
  add("layHide", "Gizle", [this, layer] { c_->setLayerVisible(layer, false); }, "hideothers")->setEnabled(vis);
  add("layOnly", "Yalnız bunu göster", [this, layer] { c_->showOnlyLayer(layer); });
  add("layShowAll", "Tüm katmanları göster", [this] { c_->showAllLayers(); });
  m->addSeparator();
  add("layColor", "Renk...", [this, layer] {
    const Layer* L = c_->window()->document().layers.find(layer.toStdString());
    const QColor nc = QColorDialog::getColor(QColor(QString::fromStdString(L ? L->color : Layer{}.color)), c_->window(), "Katman rengi");
    if (nc.isValid()) c_->setLayerColor(layer, nc);
  });
  QMenu* st = m->addMenu("Çizgi stili");
  st->setObjectName("layStyleMenu");
  for (const char* s : {"solid", "dashed", "center", "dotted"}) {
    QAction* a = st->addAction(swatch("#1C2432", s), styleText(s));
    a->setObjectName(QString("layStyle_%1").arg(s));
    const QString style = s;
    connect(a, &QAction::triggered, this, [this, layer, style] { c_->setLayerLineStyle(layer, style); });
  }
  add("layRename", "Yeniden adlandır...", [this, layer] {
    bool ok = false;
    const QString to = QInputDialog::getText(c_->window(), "Katmanı yeniden adlandır", "Yeni ad:", QLineEdit::Normal, layer, &ok);
    if (ok) c_->renameLayer(layer, to.trimmed());
  })->setEnabled(layer != kDefaultLayerName);
  add("layNew", "Yeni katman", [this] { c_->newLayer(); }, "new");
  m->addSeparator();
  add("layMoveSel", "Seçimi bu katmana taşı", [this, layer] { c_->moveSelectionToLayer(layer); })
      ->setEnabled(!c_->window()->selection()->isEmpty());
  add("laySelect", "Katmandaki öğeleri seç", [this, layer] { c_->selectLayerCurves(layer); }, "select");
  return m;
}

// ============================================================================================ PlaceViewDialog
PlaceViewDialog::PlaceViewDialog(Import2DController* c, const std::vector<i2::CurveRef>& cluster, QWidget* parent)
    : QDialog(parent), c_(c), cluster_(cluster) {
  setObjectName("import2dPlaceDialog");
  setWindowTitle("Görünüşü yerleştir");
  setAttribute(Qt::WA_DeleteOnClose);
  setModal(false);
  auto* form = new QFormLayout(this);
  info_ = new QLabel;
  info_->setObjectName("placeInfo");
  info_->setWordWrap(true);
  form->addRow(info_);
  edge_ = new QComboBox;
  edge_->setObjectName("placeEdge");
  edge_->addItems({"Otomatik (komşu görünüşe bakan kenar)", "Üst kenar", "Alt kenar", "Sol kenar", "Sağ kenar"});
  form->addRow("Katlama kenarı:", edge_);
  angle_ = new QDoubleSpinBox;
  angle_->setObjectName("placeAngle");
  angle_->setRange(-180, 180);
  angle_->setDecimals(3);
  angle_->setValue(90);
  angle_->setSuffix(" °");
  form->addRow("Döndürme açısı:", angle_);
  auto anchorRow = [&](QLineEdit*& le, QPushButton*& b, const char* leName, const char* bName) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    le = new QLineEdit;
    le->setObjectName(leName);
    b = new QPushButton("Seç");
    b->setObjectName(bName);
    b->setCheckable(true);
    b->setToolTip("Görünümde bir eğriye tıklayın: tıklanan yere en yakın uç / merkez noktası alınır");
    h->addWidget(le, 1);
    h->addWidget(b);
    return w;
  };
  form->addRow("Kaynak nokta:", anchorRow(src_, pickSrc_, "placeSrc", "placePickSrc"));
  form->addRow("Hedef nokta:", anchorRow(dst_, pickDst_, "placeDst", "placePickDst"));
  auto* btns = new QHBoxLayout;
  auto* ok = new QPushButton(makeIcon("ok", 16), "Uygula");
  ok->setObjectName("placeApply");
  ok->setDefault(true);
  auto* cancel = new QPushButton("Vazgeç");
  cancel->setObjectName("placeCancel");
  btns->addStretch();
  btns->addWidget(ok);
  btns->addWidget(cancel);
  form->addRow(btns);
  connect(edge_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { recompute(); });
  connect(angle_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { recompute(); });
  connect(src_, &QLineEdit::editingFinished, this, [this] { gp_Pnt p; if (parsePt(src_->text(), p)) setAnchor(1, p); });
  connect(dst_, &QLineEdit::editingFinished, this, [this] { gp_Pnt p; if (parsePt(dst_->text(), p)) setAnchor(2, p); });
  connect(pickSrc_, &QPushButton::toggled, this, [this](bool on) {
    pickTarget_ = on ? 1 : (pickTarget_ == 1 ? 0 : pickTarget_);
    if (on) { const QSignalBlocker b(pickDst_); pickDst_->setChecked(false); c_->showStatus("Kaynak nokta: görünümde bir eğriye tıklayın.", 0); }
  });
  connect(pickDst_, &QPushButton::toggled, this, [this](bool on) {
    pickTarget_ = on ? 2 : (pickTarget_ == 2 ? 0 : pickTarget_);
    if (on) { const QSignalBlocker b(pickSrc_); pickSrc_->setChecked(false); c_->showStatus("Hedef nokta: görünümde bir eğriye tıklayın.", 0); }
  });
  connect(ok, &QPushButton::clicked, this, [this] { apply(); });
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  recompute();
}

PlaceViewDialog::~PlaceViewDialog() {
  if (c_) { c_->clearGhost(); c_->placeDialogClosed(); }
}

void PlaceViewDialog::setAnchor(int which, const gp_Pnt& p) {
  if (which == 1) { pl_.anchorSrc = p; userSrc_ = true; }
  else { pl_.anchorDst = p; userDst_ = true; }
  pickTarget_ = 0;
  { const QSignalBlocker a(pickSrc_), b(pickDst_); pickSrc_->setChecked(false); pickDst_->setChecked(false); }
  syncFields();
  updatePreview();
}

void PlaceViewDialog::recompute() {
  const MainWindow* w = c_->window();
  const gp_Pnt us = pl_.anchorSrc, ud = pl_.anchorDst;
  pl_ = i2::proposePlacement(w->document(), w->modelResult(), cluster_, c_->visibleImportedSketches(),
                             static_cast<i2::FoldEdge>(edge_->currentIndex()), angle_->value());
  if (userSrc_) pl_.anchorSrc = us;
  if (userDst_) pl_.anchorDst = ud;
  syncFields();
  updatePreview();
}

void PlaceViewDialog::syncFields() {
  info_->setText(QString("%1 eğri. %2").arg(cluster_.size()).arg(QString::fromStdString(pl_.message)));
  const QSignalBlocker a(src_), b(dst_);
  src_->setText(fmtPt(pl_.anchorSrc));
  dst_->setText(fmtPt(pl_.anchorDst));
}

void PlaceViewDialog::updatePreview() {
  if (pl_.ok) c_->setGhostCurves(cluster_, pl_.trsf());
  else c_->clearGhost();
}

bool PlaceViewDialog::apply() {
  if (!pl_.ok) { c_->showStatus(QString::fromStdString(pl_.message)); return false; }
  MainWindow* w = c_->window();
  Document cand = w->document();
  std::string err;
  std::vector<std::string> ids;
  if (!i2::moveCurves(cand, w->modelResult(), cluster_, pl_.trsf(), std::string(), &err, &ids)) {
    c_->showStatus("Görünüşü yerleştir: " + QString::fromStdString(err), 8000);
    return false;
  }
  c_->clearGhost();
  if (!w->commitDocument(cand, "Görünüşü yerleştir")) return false;
  w->selection()->clear();
  c_->showStatus(QString("Görünüş yerleştirildi (%1 eğri, %2).").arg(cluster_.size()).arg(QString::fromStdString(pl_.message)));
  accept();
  return true;
}

// ============================================================================================ controller
Import2DController::Import2DController(MainWindow* w) : QObject(w), w_(w) {
  setObjectName("import2dController");
  install();
}

void Import2DController::install() {
  // ---- actions
  actImport_ = new QAction(makeIcon("open", 16), "2B çizim (eğri olarak)...", w_);
  actImport_->setObjectName("actImport2D");
  actImport_->setIconText("2B çizim");
  actImport_->setToolTip("2B çizim içe aktar (eğri olarak): DXF / vektör PDF görünüşlerini katmanlarıyla birlikte 3B sahneye eğri olarak "
                         "yerleştirir (gerçek ölçek). Dosyayı pencereye sürükleyip bırakmak da olur.");
  actImport_->setEnabled(i2::available());
  connect(actImport_, &QAction::triggered, this, [this] {
    const QString p = QFileDialog::getOpenFileName(w_, "2B çizim içe aktar (eğri olarak)", QString(),
                                                   "Çizimler (*.dxf *.DXF *.pdf *.PDF);;DXF (*.dxf *.DXF);;PDF (*.pdf *.PDF)");
    if (p.isEmpty()) return;
    SketchPlane pl;   // a selected datum plane receives the drawing, otherwise XY
    const QStringList datums = w_->selection_->namesOfKind(SelItem::Kind::Datum);
    if (!datums.isEmpty()) { pl.kind = SketchPlaneKind::OnPlane; pl.planeRef = datums.last().toStdString(); }
    importFile(p, pl);
  });
  actViewSel_ = new QAction(makeIcon("select", 16), "Görünüş seç", w_);
  actViewSel_->setObjectName("actViewSelect");
  actViewSel_->setCheckable(true);
  actViewSel_->setToolTip("Görünüş seç: açıkken içe aktarılmış bir eğriye tek tık, o görünüşün birbirine değen tüm eğrilerini seçer "
                          "(Ctrl+tık: ekle).");
  actPlace_ = new QAction(makeIcon("move", 16), "Görünüşü yerleştir...", w_);
  actPlace_->setObjectName("actPlaceView");
  actPlace_->setToolTip("Görünüşü yerleştir: seçili görünüşü bir kenarı etrafında 90° katlayıp komşu görünüşe oturtur "
                        "(kaynak noktayı hedef noktaya yakalar).");
  connect(actPlace_, &QAction::triggered, this, [this] { openPlaceDialog(); });
  connect(actViewSel_, &QAction::toggled, this, [this](bool on) {
    if (panel_ && panel_->viewButton() && panel_->viewButton()->isChecked() != on) {
      const QSignalBlocker b(panel_->viewButton());
      panel_->viewButton()->setChecked(on);
    }
    showStatus(on ? "Görünüş seç: içe aktarılmış bir eğriye tıklayın (Ctrl+tık: ekle)." : QString(), on ? 0 : 1);
  });

  // ---- Dosya > İçe aktar
  if (QMenu* mf = w_->menuFile_) {
    QMenu* mi = new QMenu("İçe aktar", mf);
    mi->setObjectName("menuImport");
    mi->setIcon(makeIcon("open", 16));
    mi->addAction(actImport_);
    QAction* before = nullptr;
    for (QAction* a : mf->actions())
      if (a->menu() && a->menu()->objectName() == "menuExport") before = a;
    if (!before)
      for (QAction* a : mf->actions()) if (a->objectName() == "actionDrawingImport") before = a;
    mf->insertMenu(before, mi);
  }
  // ---- ribbon: Tasarım > 2B Çizim
  // ribbon Görüntü > "Katmanlar / 2B çizim" (SpaceClaim keeps Layers on the Display tab). Not on Tasarım: that tab has
  // no width to spare (a wider tab collapses the Düzenle / Sketch Oturumu buttons at 1500 px).
  if (w_->ribbon_) {
    RibbonGroup* g = w_->ribbon_->addGroup("display", "import2d", "Katmanlar / 2B çizim");
    auto* big = new QAction(makeIcon("open", 32), "2B çizim", w_);
    big->setObjectName("actRibbonImport2D");
    big->setToolTip(actImport_->toolTip());
    big->setEnabled(actImport_->isEnabled());
    connect(big, &QAction::triggered, actImport_, &QAction::trigger);
    g->addLarge(big);
    auto* showLayers = new QAction(makeIcon("panel", 16), "Katmanlar", w_);
    showLayers->setObjectName("actShowLayers");
    showLayers->setToolTip("Katmanlar sekmesini göster (Yapı paneli)");
    connect(showLayers, &QAction::triggered, this, [this] { if (w_->structure_) w_->structure_->setCurrentPage("layers"); });
    g->addSmall(showLayers);
    g->addSmall(actViewSel_);
    g->addSmall(actPlace_);
  }
  // ---- Yapı panel: Katmanlar tab
  if (w_->structure_) {
    panel_ = new LayersPanel(this);
    w_->structure_->addPage(panel_, "layers", "Katmanlar", makeIcon("panel", 14),
                            "Katmanlar (Layers): görünürlük, renk, çizgi stili; içe aktarılan çizim katmanları ve Katman0");
    panel_->rebuild();
  }
  // ---- drag & drop of drawings onto the window
  w_->setAcceptDrops(true);
  w_->installEventFilter(this);
}

void Import2DController::showStatus(const QString& msg, int ms) {
  lastMsg_ = msg;
  w_->statusBar()->showMessage(msg, ms);
}

bool Import2DController::eventFilter(QObject* o, QEvent* e) {
  if (o == w_ && (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove)) {
    auto* de = static_cast<QDropEvent*>(e);
    if (de->mimeData() && de->mimeData()->hasUrls())
      for (const QUrl& u : de->mimeData()->urls())
        if (u.isLocalFile() && i2::isDrawingFile(u.toLocalFile().toStdString())) { de->acceptProposedAction(); return true; }
  } else if (o == w_ && e->type() == QEvent::Drop) {
    auto* de = static_cast<QDropEvent*>(e);
    bool any = false;
    if (de->mimeData() && de->mimeData()->hasUrls())
      for (const QUrl& u : de->mimeData()->urls())
        if (u.isLocalFile() && i2::isDrawingFile(u.toLocalFile().toStdString())) {
          const QString path = u.toLocalFile();
          any = true;
          QTimer::singleShot(0, this, [this, path] { importFile(path); });   // not inside the drop handler (modal prompts)
        }
    if (any) { de->acceptProposedAction(); return true; }
  }
  return QObject::eventFilter(o, e);
}

// ---------------------------------------------------------------------------------------- import
bool Import2DController::importFile(const QString& path, const SketchPlane& plane, double scale, bool interactive) {
  if (w_->sketching_ && !w_->resolveOpenSketch()) return false;
  i2::Options o;
  o.plane = plane.kind == SketchPlaneKind::OnPlane || plane.kind == SketchPlaneKind::OnFace ? SketchPlaneKind::XY : plane.kind;
  o.scale = scale;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  i2::Result r = i2::readDrawing(path.toStdString(), o, w_->doc_);
  QApplication::restoreOverrideCursor();
  if (!r.ok) {
    showStatus("2B çizim içe aktarılamadı: " + QString::fromStdString(r.message), 10000);
    if (interactive) QMessageBox::warning(w_, "2B çizim içe aktar", QString::fromStdString(r.message));
    return false;
  }
  if (!r.scaleConfident && interactive) {
    bool ok = false;
    const double s = QInputDialog::getDouble(w_, "Çizim ölçeği",
                                             "PDF ölçeği ölçülerden / antetten belirlenemedi.\nParça mm / kağıt mm (1:2 çizim için 2):",
                                             r.scaleSuggestion, 1e-4, 1e4, 4, &ok);
    if (!ok) return false;
    o.scale = s;
    r = i2::readDrawing(path.toStdString(), o, w_->doc_);
    if (!r.ok) return false;
  }
  for (auto& f : r.sketches) {
    if (plane.kind == SketchPlaneKind::OnPlane || plane.kind == SketchPlaneKind::OnFace) {
      f.sketch->plane = plane;
    } else {
      f.sketch->plane.offsetExpr = plane.offsetExpr.empty() ? "0" : plane.offsetExpr;
    }
  }
  Document cand = w_->doc_;
  const auto ids = i2::mergeInto(cand, r);
  if (!w_->commitDocument(cand, "2B çizim içe aktar: " + QFileInfo(path).fileName())) {
    showStatus("2B çizim içe aktarılamadı (değişiklik reddedildi).");
    return false;
  }
  w_->viewer_->fitAll();
  showStatus("2B çizim içe aktarıldı — " + QString::fromStdString(r.message), 12000);
  return true;
}

// ---------------------------------------------------------------------------------------- layers
QStringList Import2DController::layerNames() const {
  QStringList l;
  const LayerTable& t = w_->doc_.layers;
  l << kDefaultLayerName;
  for (const auto& L : t.layers) if (L.name != kDefaultLayerName) l << QString::fromStdString(L.name);
  return l;
}
bool Import2DController::layerVisible(const QString& layer) const {
  const Layer* L = w_->doc_.layers.find(layer.toStdString());
  return !L || L->visible;
}
int Import2DController::layerMemberCount(const QString& layer) const {
  const std::string n = layer.toStdString();
  const Document& d = w_->doc_;
  int c = 0;
  for (const auto& f : d.features) if (f.type == FeatureType::Sketch && d.layers.layerOfSketch(f.id) == n) ++c;
  for (const auto& b : w_->model_.bodies) if (d.layers.layerOfBody(b.name) == n) ++c;
  return c;
}
QString Import2DController::layerOfSketch(const QString& sid) const {
  return QString::fromStdString(w_->doc_.layers.layerOfSketch(sid.toStdString()));
}
bool Import2DController::commitLayers(const LayerTable& t, const QString& text) {
  if (toJson(t) == toJson(w_->doc_.layers)) return false;
  w_->undo_.push(new LayerCommand(w_, w_->doc_.layers, t, text));
  return true;
}
bool Import2DController::setLayerVisible(const QString& layer, bool on) {
  LayerTable t = w_->doc_.layers;
  Layer& L = t.ensure(layer.toStdString());
  if (L.visible == on) return false;
  L.visible = on;
  // hidden curves leave the selection (not pickable either)
  if (!on) {
    QList<SelItem> keep;
    for (const SelItem& s : w_->selection_->items()) {
      if (s.name.startsWith("sketch:") && t.layerOfSketch(s.name.section(':', 1, 1).toStdString()) == L.name) continue;
      keep << s;
    }
    if (keep.size() != w_->selection_->count()) w_->selection_->set(keep);
  }
  return commitLayers(t, QString("Katman %1: %2").arg(on ? "göster" : "gizle", layer));
}
bool Import2DController::showOnlyLayer(const QString& layer) {
  LayerTable t = w_->doc_.layers;
  t.ensure(layer.toStdString());
  for (const QString& n : layerNames()) t.ensure(n.toStdString()).visible = n == layer;
  return commitLayers(t, "Yalnız bu katmanı göster: " + layer);
}
bool Import2DController::showAllLayers() {
  LayerTable t = w_->doc_.layers;
  for (auto& L : t.layers) L.visible = true;
  return commitLayers(t, "Tüm katmanları göster");
}
bool Import2DController::setLayerColor(const QString& layer, const QColor& c) {
  if (!c.isValid()) return false;
  LayerTable t = w_->doc_.layers;
  t.ensure(layer.toStdString()).color = c.name(QColor::HexRgb).toUpper().toStdString();
  return commitLayers(t, "Katman rengi: " + layer);
}
bool Import2DController::setLayerLineStyle(const QString& layer, const QString& style) {
  if (!isValidLineStyle(style.toStdString())) return false;
  LayerTable t = w_->doc_.layers;
  t.ensure(layer.toStdString()).lineStyle = style.toStdString();
  return commitLayers(t, "Katman çizgi stili: " + layer);
}
bool Import2DController::renameLayer(const QString& from, const QString& to) {
  if (from == kDefaultLayerName || to.isEmpty() || to == from) return false;
  LayerTable t = w_->doc_.layers;
  if (t.find(to.toStdString())) { showStatus("Bu adla bir katman zaten var: " + to); return false; }
  Layer* L = t.find(from.toStdString());
  if (!L) return false;
  L->name = to.toStdString();
  return commitLayers(t, QString("Katmanı yeniden adlandır: %1 -> %2").arg(from, to));
}
QString Import2DController::newLayer(const QString& name) {
  LayerTable t = w_->doc_.layers;
  const std::string n = name.isEmpty() ? t.uniqueName("Katman") : name.toStdString();
  if (t.find(n)) { showStatus("Bu adla bir katman zaten var: " + QString::fromStdString(n)); return QString(); }
  t.ensure(n);
  commitLayers(t, "Yeni katman: " + QString::fromStdString(n));
  return QString::fromStdString(n);
}
bool Import2DController::moveSelectionToLayer(const QString& layer) {
  const Document& d = w_->doc_;
  Document cand = d;
  cand.layers.ensure(layer.toStdString());
  std::map<std::string, std::vector<i2::CurveRef>> partial;
  std::map<std::string, std::set<int>> chosen;
  for (const SelItem& s : w_->selection_->items()) {
    switch (s.kind) {
      case SelItem::Kind::SketchCurve: {
        i2::CurveRef c;
        if (i2::parseCurveName(s.name.toStdString(), c)) chosen[c.sid].insert(c.eid);
        break;
      }
      case SelItem::Kind::SketchRegion: case SelItem::Kind::Sketch: case SelItem::Kind::Feature: {
        const std::string sid = s.kind == SelItem::Kind::SketchRegion ? s.name.section(':', 1, 1).toStdString() : s.name.toStdString();
        const Feature* f = d.feature(sid);
        if (f && f->type == FeatureType::Sketch) cand.layers.assignSketch(sid, layer.toStdString());
        break;
      }
      case SelItem::Kind::Body: cand.layers.assignBody(s.name.toStdString(), layer.toStdString()); break;
      case SelItem::Kind::Face: {
        const std::string b = w_->model_.bodyOfFace(s.name.toStdString());
        if (!b.empty()) cand.layers.assignBody(b, layer.toStdString());
        break;
      }
      default: break;
    }
  }
  std::vector<i2::CurveRef> refs;
  for (const auto& [sid, ids] : chosen) {
    const Feature* f = d.feature(sid);
    if (!f || !f->sketch) continue;
    if (ids.size() >= f->sketch->entities.size()) { cand.layers.assignSketch(sid, layer.toStdString()); continue; }
    for (int id : ids) refs.push_back({sid, id});
  }
  if (!refs.empty()) {
    std::string err;
    if (!i2::moveCurves(cand, w_->model_, refs, gp_Trsf(), layer.toStdString(), &err)) { showStatus("Katmana taşınamadı: " + QString::fromStdString(err)); return false; }
    const bool ok = w_->commitDocument(cand, "Seçimi katmana taşı: " + layer);
    if (ok) w_->selection_->clear();
    return ok;
  }
  return commitLayers(cand.layers, "Seçimi katmana taşı: " + layer);
}
bool Import2DController::selectLayerCurves(const QString& layer) {
  const Document& d = w_->doc_;
  QList<SelItem> sel;
  for (const auto& f : d.features) {
    if (f.type != FeatureType::Sketch || f.suppressed || d.layers.layerOfSketch(f.id) != layer.toStdString()) continue;
    if (!w_->sketchShownIn3D(f.id)) continue;
    auto it = w_->model_.solvedSketches.find(f.id);
    if (it == w_->model_.solvedSketches.end()) continue;
    for (const auto& e : it->second.entities)
      sel << SelItem{SelItem::Kind::SketchCurve, QString::fromStdString(i2::curveName({f.id, e.id}))};
  }
  for (const auto& b : w_->model_.bodies)
    if (d.layers.layerOfBody(b.name) == layer.toStdString() && d.layers.bodyVisible(b.name)) sel << SelItem{SelItem::Kind::Body, QString::fromStdString(b.name)};
  w_->selection_->set(sel);
  showStatus(QString("%1 katmanından %2 öğe seçildi.").arg(layer).arg(sel.size()));
  return !sel.isEmpty();
}

// ---------------------------------------------------------------------------------------- views
std::vector<std::string> Import2DController::visibleImportedSketches() const {
  std::vector<std::string> out;
  const Document& d = w_->doc_;
  for (const auto& f : d.features) {
    if (!i2::isImportedSketch(f) || f.suppressed) continue;
    if (d.layers.layerOfSketch(f.id) == kNotesLayerName || !w_->sketchShownIn3D(f.id)) continue;
    out.push_back(f.id);
  }
  return out;
}
QList<SelItem> Import2DController::selectedCurves() const { return w_->selection_->ofKind(SelItem::Kind::SketchCurve); }

bool Import2DController::selectViewAt(const QString& curveName, bool add) {
  i2::CurveRef seed;
  if (!i2::parseCurveName(curveName.toStdString(), seed)) return false;
  const auto cl = i2::connectedCluster(w_->doc_, w_->model_, seed, visibleImportedSketches());
  QList<SelItem> sel = add ? w_->selection_->items() : QList<SelItem>();
  for (const auto& c : cl) {
    const SelItem it{SelItem::Kind::SketchCurve, QString::fromStdString(i2::curveName(c))};
    if (!sel.contains(it)) sel << it;
  }
  w_->selection_->set(sel);
  showStatus(QString("Görünüş seçildi: %1 eğri. 'Görünüşü yerleştir' ile katlayın.").arg(cl.size()));
  return true;
}

PlaceViewDialog* Import2DController::openPlaceDialog() {
  std::vector<i2::CurveRef> cl;
  for (const SelItem& s : selectedCurves()) {
    i2::CurveRef c;
    if (i2::parseCurveName(s.name.toStdString(), c)) cl.push_back(c);
  }
  if (cl.empty()) { showStatus("Önce bir görünüşün eğrilerini seçin ('Görünüş seç' ile tek tık)."); return nullptr; }
  if (placeDlg_) placeDlg_->close();
  placeDlg_ = new PlaceViewDialog(this, cl, w_);
  placeDlg_->show();
  placeDlg_->raise();
  return placeDlg_;
}

bool Import2DController::placeSelection(i2::FoldEdge edge, double angleDeg) {
  std::vector<i2::CurveRef> cl;
  for (const SelItem& s : selectedCurves()) {
    i2::CurveRef c;
    if (i2::parseCurveName(s.name.toStdString(), c)) cl.push_back(c);
  }
  if (cl.empty()) return false;
  const i2::Placement p = i2::proposePlacement(w_->doc_, w_->model_, cl, visibleImportedSketches(), edge, angleDeg);
  if (!p.ok) { showStatus(QString::fromStdString(p.message)); return false; }
  Document cand = w_->doc_;
  std::string err;
  if (!i2::moveCurves(cand, w_->model_, cl, p.trsf(), std::string(), &err)) { showStatus(QString::fromStdString(err)); return false; }
  if (!w_->commitDocument(cand, "Görünüşü yerleştir")) return false;
  w_->selection_->clear();
  return true;
}

void Import2DController::placeDialogClosed() {
  placeDlg_ = nullptr;
}

gp_Pnt Import2DController::nearestSnap(const QString& curveName, const QPoint& pos, bool* ok) const {
  if (ok) *ok = false;
  i2::CurveRef c;
  if (!i2::parseCurveName(curveName.toStdString(), c)) return gp_Pnt();
  const auto pts = i2::snapPoints(w_->doc_, w_->model_, c);
  double o[3], d[3];
  if (pts.empty() || !w_->viewer_->screenRay(QPointF(pos), o, d)) return gp_Pnt();
  const gp_Pnt O(o[0], o[1], o[2]);
  const gp_Vec D(d[0], d[1], d[2]);
  double best = 1e300;
  gp_Pnt bp;
  for (const auto& p : pts) {
    const gp_Vec v(O, p);
    const double dist = v.Crossed(D).Magnitude();
    if (dist < best) { best = dist; bp = p; }
  }
  if (ok) *ok = true;
  return bp;
}

void Import2DController::setGhostCurves(const std::vector<i2::CurveRef>& curves, const gp_Trsf& T) {
  TopoDS_Compound comp;
  BRep_Builder bb;
  bb.MakeCompound(comp);
  std::map<std::string, std::set<int>> by;
  for (const auto& c : curves) by[c.sid].insert(c.eid);
  for (const auto& [sid, ids] : by) {
    const Sketch* sk = solvedSketch(w_, w_->doc_, w_->model_, sid);
    gp_Ax3 fr;
    if (!sk || !frameOfSketch(w_->model_, sid, fr)) continue;
    // region faces of the moved curves (the ghost shows faces best) + every curve as an edge
    Sketch part = Sketch::create();
    part.points = sk->points;
    for (const auto& e : sk->entities) if (ids.count(e.id)) part.entities.push_back(e);
    try {
      // SC-VIDEO2D: the tolerant region list (v2 Fill / Pull), not the classic profile: views drawn as overlapping
      // rectangles / with T-junctions (lbracket front view) gave no ghost at all ("Dallanan kontur")
      const sketch::Profile pr = sketch::sketchRegions(part, fr).profile;
      for (const auto& f : pr.faces) bb.Add(comp, BRepBuilderAPI_Transform(f, T, true).Shape());
      for (const auto& pe : pr.edges) bb.Add(comp, BRepBuilderAPI_Transform(pe.edge, T, true).Shape());
    } catch (...) {}
  }
  w_->setGhost(comp);
}
void Import2DController::clearGhost() {
  if (w_ && w_->viewer_) w_->viewer_->clearGhost();
}

// ---------------------------------------------------------------------------------------- hooks
void Import2DController::refresh() {
  if (panel_) panel_->rebuild();
  if (placeDlg_) {   // undo / redo removed the curves of the open placement: close it
    for (const auto& c : placeDlg_->cluster()) {
      const Feature* f = w_->doc_.feature(c.sid);
      if (!f || !f->sketch || !f->sketch->entity(c.eid)) { placeDlg_->close(); break; }
    }
  }
}
bool Import2DController::sketchHidden(const std::string& sid) const { return !w_->doc_.layers.sketchVisible(sid); }
void Import2DController::appendHiddenFaces(QStringList& hidden) const {
  if (w_->doc_.layers.empty()) return;
  for (const auto& b : w_->model_.bodies)
    if (!w_->doc_.layers.bodyVisible(b.name))
      for (const auto& f : b.faces) hidden << QString::fromStdString(f);
}
void Import2DController::appendLabels(std::vector<viewer::Label3D>& labels) const {
  noteLabels_ = 0;
  for (const auto& f : w_->doc_.features) {
    if (!i2::isImportedSketch(f) || f.suppressed || f.prop("notes").empty() || !w_->sketchShownIn3D(f.id)) continue;
    gp_Ax3 fr;
    if (!frameOfSketch(w_->model_, f.id, fr)) continue;
    for (const auto& n : i2::notesOf(f)) {
      viewer::Label3D l;
      const gp_Pnt p(fr.Location().XYZ() + fr.XDirection().XYZ() * n.u + fr.YDirection().XYZ() * n.v);
      l.pos[0] = p.X(); l.pos[1] = p.Y(); l.pos[2] = p.Z();
      l.text = QString::fromStdString(n.text);
      labels.push_back(l);
      ++noteLabels_;
    }
  }
}
void Import2DController::decorateViewer() {
  std::map<std::string, viewer::EdgeStyle> styles;
  const Document& d = w_->doc_;
  if (!d.layers.empty()) {
    const Layer defL;
    for (const auto& f : d.features) {
      if (f.type != FeatureType::Sketch || !f.sketch) continue;
      const Layer* L = d.layers.find(d.layers.layerOfSketch(f.id));
      if (!L || (L->color == defL.color && L->lineStyle == "solid")) continue;
      viewer::EdgeStyle st;
      st.color = QColor(QString::fromStdString(L->color));
      st.pattern = patternOf(L->lineStyle);
      for (const auto& e : f.sketch->entities) styles[i2::curveName({f.id, e.id})] = st;
    }
  }
  w_->viewer_->setEdgeStyles(styles);
}
bool Import2DController::handlePick(const QString& name, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos) {
  (void)kind;
  if (placeDlg_ && placeDlg_->pickTarget() && clicks == 1) {
    bool ok = false;
    const gp_Pnt p = nearestSnap(name, pos, &ok);
    if (ok) {
      const int which = placeDlg_->pickTarget();
      placeDlg_->setAnchor(which, p);
      showStatus(QString("%1 nokta: %2").arg(which == 1 ? "Kaynak" : "Hedef", fmtPt(p)));
    } else {
      showStatus("Bir eğriye tıklayın (uç / merkez noktası alınır).");
    }
    return true;
  }
  if (actViewSel_ && actViewSel_->isChecked() && clicks == 1 && name.startsWith("sketch:")) {
    i2::CurveRef c;
    if (i2::parseCurveName(name.toStdString(), c)) return selectViewAt(name, mods & Qt::ControlModifier);
  }
  return false;
}
bool Import2DController::showCurveMenu(const QString& edge, const QPoint& globalPos) {
  i2::CurveRef c;
  if (!i2::parseCurveName(edge.toStdString(), c)) return false;
  const Feature* f = w_->doc_.feature(c.sid);
  if (!f || !i2::isImportedSketch(*f)) return false;
  const SelItem it{SelItem::Kind::SketchCurve, edge};
  if (!w_->selection_->contains(it)) w_->selection_->set({it});
  auto* m = new QMenu(w_);
  m->setObjectName("import2dCurveMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  QAction* a = m->addAction(makeIcon("select", 16), "Görünüş seç");
  a->setObjectName("ctxViewSelect");
  connect(a, &QAction::triggered, this, [this, edge] { selectViewAt(edge); });
  a = m->addAction(makeIcon("move", 16), "Görünüşü yerleştir...");
  a->setObjectName("ctxPlaceView");
  connect(a, &QAction::triggered, this, [this] { openPlaceDialog(); });
  m->addSeparator();
  const QString layer = layerOfSketch(QString::fromStdString(c.sid));
  a = m->addAction(QString("'%1' katmanını gizle").arg(layer));
  a->setObjectName("ctxHideLayer");
  connect(a, &QAction::triggered, this, [this, layer] { setLayerVisible(layer, false); });
  QMenu* mv = m->addMenu("Katmana taşı");
  mv->setObjectName("ctxMoveToLayer");
  for (const QString& n : layerNames()) {
    QAction* x = mv->addAction(n);
    x->setObjectName("ctxMoveTo:" + n);
    x->setEnabled(n != layer);
    connect(x, &QAction::triggered, this, [this, n] { moveSelectionToLayer(n); });
  }
  m->popup(globalPos);
  return true;
}

} // namespace pcd
