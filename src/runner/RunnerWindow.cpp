#include "pc/ViewColors.h"   // SC-COLOR
#include <pc/DatumSizing.h>
#include "RunnerWindow.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QInputDialog>
#include <set>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

using pc::mfg::Severity;
using pc::mfg::Domain;

namespace pc::runner {

namespace {
const char* kRed = "#d93025";
const char* kOrange = "#e8710a";
const char* kBlue = "#5f7fa8";
const char* kGreen = "#188038";

QColor sevColor(Severity s) { return QColor(s == Severity::Error ? kRed : s == Severity::Warning ? kOrange : kBlue); }
QString sevName(Severity s) { return s == Severity::Error ? "HATA" : s == Severity::Warning ? "UYARI" : "BİLGİ"; }
QString unitOf(ParamKind k) { return k == ParamKind::Length ? " mm" : k == ParamKind::Angle ? " °" : ""; }
QString unitShort(ParamKind k) { return k == ParamKind::Length ? "mm" : k == ParamKind::Angle ? "°" : ""; }
QIcon dot(const QColor& c) {
  QPixmap pm(14, 14);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(c);
  p.setPen(Qt::NoPen);
  p.drawEllipse(1, 1, 12, 12);
  return QIcon(pm);
}
const QColor kBodyPalette[] = {QColor("#4f8fd8"), QColor("#e0883a"), QColor("#4fb477"), QColor("#c8566b"),
                               QColor("#8f6fd0"), QColor("#d1b23a"), QColor("#3fb5c0"), QColor("#8a8f98")};
QColor bodyColor(size_t i) { return kBodyPalette[i % (sizeof(kBodyPalette) / sizeof(kBodyPalette[0]))]; }
QIcon swatch(const QColor& c) {
  QPixmap pm(14, 14);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setBrush(c);
  p.setPen(QColor("#44505e"));
  p.drawRect(1, 1, 11, 11);
  return QIcon(pm);
}
QString num(double v) { return QString::number(v, 'g', 6); }

const char* kStyle = R"(
QMainWindow, QDialog { background: #eef1f5; }
QToolBar { background: #ffffff; border: none; border-bottom: 1px solid #d5dbe3; spacing: 4px; padding: 3px; }
QToolButton { padding: 4px 8px; border-radius: 4px; }
QToolButton:hover { background: #e6edf7; }
QToolButton:checked { background: #d5e3f7; border: 1px solid #8fb0dc; color: #14233a; }
QGroupBox { background: #ffffff; border: 1px solid #d5dbe3; border-radius: 8px; margin-top: 14px; padding: 8px 6px 6px 6px; font-weight: 600; color: #26323f; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QLabel { color: #26323f; }
QDoubleSpinBox, QSpinBox { padding: 2px 4px; border: 1px solid #b9c3d0; border-radius: 4px; background: #fff; min-height: 22px; }
QPushButton { padding: 5px 12px; border: 1px solid #b9c3d0; border-radius: 5px; background: #fff; }
QPushButton:hover { background: #e6edf7; }
QPushButton#primary { background: #1f6feb; color: #fff; border: 1px solid #1a5fd0; font-weight: 600; }
QPushButton#primary:disabled { background: #a9c3ef; border-color: #a9c3ef; }
QPushButton#suggest { padding: 1px 8px; font-size: 11px; border-color: #1f6feb; color: #1f6feb; background: #f2f7ff; }
QListWidget { background: #fff; border: 1px solid #d5dbe3; border-radius: 6px; outline: none; }
QListWidget::item { padding: 5px 4px; border-bottom: 1px solid #eef1f5; }
QListWidget::item:selected { background: #dbe8fb; color: #14233a; }
QTabBar::tab { padding: 5px 12px; background: #e3e8ef; border: 1px solid #d5dbe3; border-bottom: none; border-top-left-radius: 5px; border-top-right-radius: 5px; margin-right: 2px; }
QTabBar::tab:selected { background: #ffffff; font-weight: 600; }
QSlider::groove:horizontal { height: 4px; background: #cfd7e2; border-radius: 2px; }
QSlider::handle:horizontal { background: #1f6feb; width: 14px; margin: -5px 0; border-radius: 7px; }
QStatusBar { background: #ffffff; border-top: 1px solid #d5dbe3; }
)";
} // namespace

struct RunnerWindow::Row {
  ExposedParam p;
  QWidget* box = nullptr;
  QLabel* label = nullptr;
  QDoubleSpinBox* dsb = nullptr;
  QSpinBox* isb = nullptr;
  QSlider* slider = nullptr;
  QToolButton* reset = nullptr;
  QLabel* msg = nullptr;
  QPushButton* apply = nullptr;
  QWidget* msgRow = nullptr;
  std::optional<double> suggestion;
  bool sliderBusy = false;
  double value() const { return dsb ? dsb->value() : static_cast<double>(isb->value()); }
};

RunnerWindow::RunnerWindow(pc::app::AppDefinition def, QWidget* parent)
    : QMainWindow(parent), def_(std::move(def)) {
  defShared_ = std::make_shared<const pc::app::AppDefinition>(def_);
  setWindowTitle(QString::fromStdString(def_.appName + "  —  v" + def_.version));
  resize(1500, 900);
  qApp->setStyleSheet(kStyle);
  buildUi();
  debounce_.setSingleShot(true);
  debounce_.setInterval(250);
  connect(&debounce_, &QTimer::timeout, this, [this] { requestValidation(false); });
  connect(&watcher_, &QFutureWatcher<pc::mfg::ValidationResult>::finished, this, [this] { onJobFinished(); });
}

RunnerWindow::~RunnerWindow() {
  if (running_) watcher_.waitForFinished();
}

std::map<std::string, double> RunnerWindow::currentValues() const {
  std::map<std::string, double> v = extra_;
  for (auto& r : rows_) v[r->p.name] = r->value();
  return v;
}

void RunnerWindow::buildUi() {
  // ---- menu / toolbar
  auto* mFile = menuBar()->addMenu("&Dosya");
  actSTL_ = mFile->addAction("STL olarak dışa aktar…", this, [this] { exportShape(false); });
  actSTEP_ = mFile->addAction("STEP olarak dışa aktar…", this, [this] { exportShape(true); });
  actSTL_->setEnabled(def_.allowExportSTL);
  actSTEP_->setEnabled(def_.allowExportSTEP);
  mFile->addSeparator();
  mFile->addAction("Çıkış", this, &QWidget::close);
  auto* mHelp = menuBar()->addMenu("&Yardım");
  mHelp->addAction("Hakkında", this, [this] { about(); });

  auto* tb = addToolBar("Araçlar");
  tb->setMovable(false);
  tb->addAction("Varsayılanlar", this, [this] { resetAll(); });
  tb->addSeparator();
  auto* aSTL = tb->addAction("STL", this, [this] { exportShape(false); });
  aSTL->setToolTip("STL olarak dışa aktar");
  aSTL->setEnabled(def_.allowExportSTL);
  auto* aSTEP = tb->addAction("STEP", this, [this] { exportShape(true); });
  aSTEP->setToolTip("STEP olarak dışa aktar");
  aSTEP->setEnabled(def_.allowExportSTEP);
  tb->addSeparator();
  using V = pc::viewer::ShapeViewer::View;
  viewer_ = new pc::viewer::ShapeViewer;
  tb->addAction("İzometrik", this, [this] { viewer_->setView(V::Iso); });
  tb->addAction("Üst", this, [this] { viewer_->setView(V::Top); });
  tb->addAction("Ön", this, [this] { viewer_->setView(V::Front); });
  tb->addAction("Sağ", this, [this] { viewer_->setView(V::Right); });
  tb->addAction("Sığdır", this, [this] {
    viewer_->fitAll();
    if (hasShown_) try {
      const auto mp = pc::model::massProperties(shown_.model.shape);
      const double nb[6] = {mp.xmin, mp.ymin, mp.zmin, mp.xmax, mp.ymax, mp.zmax};
      for (int i = 0; i < 6; ++i) fitBox_[i] = nb[i];
      hasFitBox_ = true;
    } catch (...) {}
  });
  // v4: section view (view-only): toggle + plane preset + offset slider + flip
  tb->addSeparator();
  section_ = new pc::viewer::SectionControls(viewer_, /*overlay*/ false);
  tb->addAction(section_->toggleAction());
  tb->addWidget(section_);
  auto* sp = new QWidget;
  sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  tb->addWidget(sp);
  tb->addAction("Hakkında", this, [this] { about(); });

  // ---- left panel
  auto* left = new QWidget;
  auto* lv = new QVBoxLayout(left);
  lv->setContentsMargins(8, 8, 4, 8);
  auto* title = new QLabel(QString::fromStdString(def_.appName));
  title->setStyleSheet("font-size:18px;font-weight:700;color:#14233a;");
  title->setWordWrap(true);
  lv->addWidget(title);
  if (!def_.description.empty()) {
    auto* d = new QLabel(QString::fromStdString(def_.description));
    d->setWordWrap(true);
    d->setStyleSheet("color:#5b6775;");
    lv->addWidget(d);
  }
  auto* scroll = new QScrollArea;
  paramScroll_ = scroll;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* host = new QWidget;
  host->setStyleSheet("background: transparent;");
  buildParamPanel(host);
  scroll->setWidget(host);
  scroll->setStyleSheet("QScrollArea{background:transparent;} QScrollArea > QWidget > QWidget{background:transparent;}");
  lv->addWidget(scroll, 1);
  dirtyLbl_ = new QLabel("Değişiklikler henüz uygulanmadı");
  dirtyLbl_->setStyleSheet(QString("color:%1;font-weight:600;").arg(kOrange));
  dirtyLbl_->hide();
  lv->addWidget(dirtyLbl_);
  auto* br = new QHBoxLayout;
  updateBtn_ = new QPushButton("Güncelle");
  updateBtn_->setObjectName("primary");
  updateBtn_->setVisible(!def_.liveUpdate);
  connect(updateBtn_, &QPushButton::clicked, this, [this] { debounce_.stop(); requestValidation(false); });
  auto* defBtn = new QPushButton("Varsayılanlar");
  connect(defBtn, &QPushButton::clicked, this, [this] { resetAll(); });
  br->addWidget(updateBtn_);
  br->addWidget(defBtn);
  lv->addLayout(br);
  left->setMinimumWidth(360);

  // ---- centre
  auto* centre = new QWidget;
  auto* cv = new QVBoxLayout(centre);
  cv->setContentsMargins(4, 8, 4, 8);
  banner_ = new QFrame;
  banner_->setStyleSheet("QFrame{background:#fdecea;border:1px solid #f1a9a0;border-radius:6px;} QLabel{color:#8c1d13;font-weight:600;border:none;background:transparent;}");
  auto* bl = new QHBoxLayout(banner_);
  bannerText_ = new QLabel;
  bannerText_->setWordWrap(true);
  bannerApply_ = new QPushButton("Önerileri Uygula");
  bannerApply_->setObjectName("primary");
  connect(bannerApply_, &QPushButton::clicked, this, [this] { applySuggestionsAll(); });
  bl->addWidget(bannerText_, 1);
  bl->addWidget(bannerApply_);
  banner_->hide();
  cv->addWidget(banner_);
  viewer_->setMinimumSize(400, 300);
  cv->addWidget(viewer_, 1);
  infoLbl_ = new QLabel;
  infoLbl_->setTextFormat(Qt::RichText);
  infoLbl_->setStyleSheet("background:#fff;border:1px solid #d5dbe3;border-radius:6px;padding:6px 10px;font-size:12px;");
  infoLbl_->setWordWrap(true);
  cv->addWidget(infoLbl_);

  // ---- right panel
  auto* right = new QWidget;
  auto* rv = new QVBoxLayout(right);
  rv->setContentsMargins(4, 8, 8, 8);
  auto* rt = new QLabel("İmalat Uygunluğu");
  rt->setStyleSheet("font-size:16px;font-weight:700;color:#14233a;");
  rv->addWidget(rt);
  summary_ = new QLabel;
  summary_->setTextFormat(Qt::RichText);
  rv->addWidget(summary_);
  bodyBox_ = new QGroupBox("Gövdeler");
  auto* bbv = new QVBoxLayout(bodyBox_);
  bbv->setContentsMargins(6, 6, 6, 6);
  bbv->setSpacing(4);
  bodyList_ = new QListWidget;
  bodyList_->setMaximumHeight(130);
  bodyList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  bodyList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  bodyList_->setStyleSheet("QListWidget::item{padding:2px 2px;}");
  connect(bodyList_, &QListWidget::itemChanged, this, [this] { if (!updatingBodies_) applyVisibility(); });
  connect(bodyList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (updatingBodies_ || row < 0 || row >= static_cast<int>(shown_.model.bodies.size())) return;
    QStringList faces;
    for (auto& f : shown_.model.bodies[row].faces) faces << QString::fromStdString(f);
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces(faces);
  });
  // v4: right-click a body -> "Saydam" (translucent, to look inside without hiding it)
  bodyList_->setContextMenuPolicy(Qt::CustomContextMenu);
  bodyList_->setToolTip("Onay kutusu: görünürlük · Sağ tık: saydam");
  connect(bodyList_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
    const int row = bodyList_->row(bodyList_->itemAt(pos));
    if (row < 0 || row >= static_cast<int>(shown_.model.bodies.size())) return;
    const QString name = QString::fromStdString(shown_.model.bodies[static_cast<size_t>(row)].name);
    QMenu menu(this);
    QAction* a = menu.addAction("Saydam");
    a->setCheckable(true);
    a->setChecked(transBodies_.count(name.toStdString()) > 0);
    if (menu.exec(bodyList_->viewport()->mapToGlobal(pos)) == a) setBodyTransparent(name, a->isChecked());
  });
  bbv->addWidget(bodyList_);
  planesChk_ = new QCheckBox("Yardımcı düzlemleri göster");
  planesChk_->setChecked(false);
  connect(planesChk_, &QCheckBox::toggled, this, [this] { applyVisibility(); });
  bbv->addWidget(planesChk_);
  bodyBox_->hide();
  rv->addWidget(bodyBox_);
  tabs_ = new QTabBar;
  tabs_->setExpanding(false);
  tabs_->setDrawBase(false);
  tabs_->setTabData(tabs_->addTab("Tümü"), "all");
  if (def_.showAdditiveChecks) tabs_->setTabData(tabs_->addTab("Katmanlı"), "am");
  if (def_.showConventionalChecks) tabs_->setTabData(tabs_->addTab("Konvansiyonel"), "conv");
  connect(tabs_, &QTabBar::currentChanged, this, [this] { refreshIssueList(); });
  rv->addWidget(tabs_);
  bodyFilter_ = new QComboBox;
  bodyFilter_->addItem("Tüm gövdeler", "");
  bodyFilter_->hide();
  connect(bodyFilter_, &QComboBox::currentIndexChanged, this, [this] { if (!updatingBodies_) refreshIssueList(); });
  rv->addWidget(bodyFilter_);
  issueList_ = new QListWidget;
  issueList_->setWordWrap(true);
  issueList_->setTextElideMode(Qt::ElideNone);
  issueList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  issueList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(issueList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0) { viewer_->clearHighlights(); return; }
    auto* it = issueList_->item(row);
    QStringList faces = it->data(Qt::UserRole + 1).toStringList();
    QStringList edges = it->data(Qt::UserRole + 2).toStringList();
    viewer_->clearHighlights();
    if (!faces.isEmpty()) viewer_->setHighlightedFaces(faces);
    if (!edges.isEmpty()) viewer_->setHighlightedEdges(edges);
  });
  rv->addWidget(issueList_, 1);
  right->setMinimumWidth(320);

  auto* split = new QSplitter;
  split->addWidget(left);
  split->addWidget(centre);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setStretchFactor(2, 0);
  split->setSizes({390, 760, 360});
  setCentralWidget(split);

  status_ = new QLabel("Hazır");
  statusBar()->addWidget(status_, 1);

  connect(viewer_, &pc::viewer::ShapeViewer::facePicked, this, [this](const QString& name) {
    if (name.isEmpty()) return;
    for (int i = 0; i < issueList_->count(); ++i)
      if (issueList_->item(i)->data(Qt::UserRole + 1).toStringList().contains(name)) { issueList_->setCurrentRow(i); return; }
  });
}

void RunnerWindow::buildParamPanel(QWidget* host) {
  auto* vl = new QVBoxLayout(host);
  vl->setContentsMargins(0, 0, 4, 0);
  std::vector<ExposedParam> ps;
  try { ps = exposedParams(def_); }
  catch (const std::exception& e) { vl->addWidget(new QLabel(QString("Parametreler hesaplanamadı: ") + e.what())); }

  std::vector<std::string> groupOrder;
  std::map<std::string, QVBoxLayout*> groupLayouts;
  for (auto& p : ps) {
    if (!groupLayouts.count(p.group)) {
      auto* gb = new QGroupBox(QString::fromStdString(p.group));
      auto* gl = new QVBoxLayout(gb);
      gl->setSpacing(8);
      vl->addWidget(gb);
      groupLayouts[p.group] = gl;
      groupOrder.push_back(p.group);
    }
    auto row = std::make_unique<Row>();
    row->p = p;
    int idx = static_cast<int>(rows_.size());
    bool finite = std::isfinite(p.minV) && std::isfinite(p.maxV) && p.maxV > p.minV;
    double lo = std::isfinite(p.minV) ? p.minV : -1e9, hi = std::isfinite(p.maxV) ? p.maxV : 1e9;

    row->box = new QWidget;
    auto* bv = new QVBoxLayout(row->box);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(2);
    row->label = new QLabel(QString::fromStdString(p.label));
    row->label->setToolTip(QString::fromStdString(p.name));
    bv->addWidget(row->label);
    auto* h = new QHBoxLayout;
    h->setSpacing(6);
    if (p.kind == ParamKind::Count) {
      row->isb = new QSpinBox;
      row->isb->setRange(static_cast<int>(std::max(lo, -2e9)), static_cast<int>(std::min(hi, 2e9)));
      row->isb->setSingleStep(p.step > 0 ? static_cast<int>(p.step) : 1);
      row->isb->setValue(static_cast<int>(std::lround(p.def)));
      row->isb->setFixedWidth(96);
      h->addWidget(row->isb);
    } else {
      row->dsb = new QDoubleSpinBox;
      row->dsb->setRange(lo, hi);
      double step = p.step > 0 ? p.step : (p.kind == ParamKind::Angle ? 1.0 : (finite && hi - lo > 20 ? 1.0 : 0.1));
      row->dsb->setSingleStep(step);
      row->dsb->setDecimals(step < 0.01 ? 4 : step < 0.1 ? 3 : 2);
      row->dsb->setSuffix(unitOf(p.kind));
      row->dsb->setValue(p.def);
      row->dsb->setFixedWidth(104);
      row->dsb->setKeyboardTracking(true);
      h->addWidget(row->dsb);
    }
    row->slider = new QSlider(Qt::Horizontal);
    if (finite) {
      row->slider->setRange(0, 1000);
      double t = (p.def - lo) / (hi - lo);
      row->slider->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 1000)));
    } else {
      row->slider->setEnabled(false);
    }
    h->addWidget(row->slider, 1);
    row->reset = new QToolButton;
    row->reset->setText("↺");
    row->reset->setToolTip("Varsayılan değere dön: " + num(p.def) + unitOf(p.kind));
    h->addWidget(row->reset);
    bv->addLayout(h);

    row->msgRow = new QWidget;
    auto* mh = new QVBoxLayout(row->msgRow);
    mh->setContentsMargins(0, 0, 0, 0);
    mh->setSpacing(3);
    row->msg = new QLabel;
    row->msg->setWordWrap(true);
    row->msg->setStyleSheet("font-size:11px;");
    row->apply = new QPushButton;
    row->apply->setObjectName("suggest");
    mh->addWidget(row->msg);
    mh->addWidget(row->apply, 0, Qt::AlignLeft);
    row->msgRow->hide();
    bv->addWidget(row->msgRow);
    groupLayouts[p.group]->addWidget(row->box);

    Row* rp = row.get();
    if (rp->dsb) connect(rp->dsb, &QDoubleSpinBox::valueChanged, this, [=](double v) {
      if (rp->slider->isEnabled() && !rp->sliderBusy) {
        rp->sliderBusy = true;
        double t = (v - rp->dsb->minimum()) / (rp->dsb->maximum() - rp->dsb->minimum());
        rp->slider->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 1000)));
        rp->sliderBusy = false;
      }
      rowChanged(idx);
    });
    if (rp->isb) connect(rp->isb, &QSpinBox::valueChanged, this, [=](int v) {
      if (rp->slider->isEnabled() && !rp->sliderBusy) {
        rp->sliderBusy = true;
        double t = (v - rp->isb->minimum()) / double(rp->isb->maximum() - rp->isb->minimum());
        rp->slider->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 1000)));
        rp->sliderBusy = false;
      }
      rowChanged(idx);
    });
    connect(rp->slider, &QSlider::valueChanged, this, [=](int s) {
      if (rp->sliderBusy) return;
      rp->sliderBusy = true;
      double lo2 = rp->dsb ? rp->dsb->minimum() : rp->isb->minimum();
      double hi2 = rp->dsb ? rp->dsb->maximum() : rp->isb->maximum();
      double v = lo2 + (hi2 - lo2) * s / 1000.0;
      if (rp->dsb) {
        double st = rp->dsb->singleStep();
        v = lo2 + std::round((v - lo2) / st) * st;
        rp->dsb->setValue(std::clamp(v, lo2, hi2));
      } else {
        rp->isb->setValue(static_cast<int>(std::lround(v)));
      }
      rp->sliderBusy = false;
    });
    connect(rp->reset, &QToolButton::clicked, this, [=] { setRowValue(idx, rp->p.def, false); });
    connect(rp->apply, &QPushButton::clicked, this, [=] { applySuggestion(idx); });
    rows_.push_back(std::move(row));
  }
  // Read-only results: derived (non-exposed) parameters that carry a label, e.g. the orifice diameter computed
  // from the flow rate, flow areas, design-relation margins. Refreshed after every validation.
  for (const auto& p : def_.document.params.all())
    if (!p.exposed && !p.label.empty()) results_.push_back(p.name);
  if (!results_.empty()) {
    auto* gb = new QGroupBox("Hesaplanan değerler");
    gb->setObjectName("resultsBox");
    auto* gl = new QVBoxLayout(gb);
    resultsLbl_ = new QLabel;
    resultsLbl_->setObjectName("resultsLabel");
    resultsLbl_->setTextFormat(Qt::RichText);
    resultsLbl_->setWordWrap(true);
    resultsLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultsLbl_->setStyleSheet("font-weight:400;");
    gl->addWidget(resultsLbl_);
    resultsBox_ = gb;
    // directly under the first input group (the design inputs), so the computed sizes are close to what drives them
    vl->insertWidget(groupOrder.empty() ? vl->count() : 1, gb);
  }
  vl->addStretch(1);
}

void RunnerWindow::updateResults(const pc::ParamValues& values) {
  if (!resultsLbl_) return;
  QString h = "<table cellspacing='0' cellpadding='2' width='100%'>";
  for (const auto& n : results_) {
    const pc::Parameter* p = def_.document.params.find(n);
    auto it = values.find(n);
    if (!p || it == values.end()) continue;
    const double v = it->second;
    const bool bad = !std::isfinite(v) || v < p->minValue - 1e-12 || v > p->maxValue + 1e-12;
    const char* unit = p->kind == pc::ParamKind::Length ? " mm" : p->kind == pc::ParamKind::Angle ? "°" : "";
    h += QString("<tr><td title='%4'>%1</td><td align='right' style='white-space:nowrap;%3'><b>%2</b></td></tr>")
             .arg(QString::fromStdString(p->label).toHtmlEscaped(), QString::number(v, 'g', 4) + unit,
                  bad ? "color:#c0302a;" : "", QString::fromStdString(p->description).toHtmlEscaped());
  }
  h += "</table>";
  resultsLbl_->setText(h);
}

void RunnerWindow::setRowValue(int idx, double v, bool block) {
  Row& r = *rows_[idx];
  QSignalBlocker b1(r.dsb), b2(r.isb), b3(r.slider);
  if (!block) { b1.unblock(); b2.unblock(); b3.unblock(); }
  if (r.dsb) r.dsb->setValue(v); else r.isb->setValue(static_cast<int>(std::lround(v)));
  if (block && r.slider->isEnabled()) {
    double lo = r.dsb ? r.dsb->minimum() : r.isb->minimum(), hi = r.dsb ? r.dsb->maximum() : r.isb->maximum();
    r.slider->setValue(static_cast<int>(std::lround(std::clamp((r.value() - lo) / (hi - lo), 0.0, 1.0) * 1000)));
  }
}

bool RunnerWindow::setCliValues(const std::map<std::string, double>& v, QString* err) {
  for (auto& [k, val] : v) {
    bool found = false;
    for (auto& r : rows_) if (r->p.name == k) found = true;
    if (!found && !def_.document.params.find(k)) {
      if (err) *err = QString("Bilinmeyen parametre: ") + QString::fromStdString(k);
      return false;
    }
  }
  pendingCli_ = v;
  return true;
}

void RunnerWindow::startup() {
  waitingSettled_ = true;
  // Validate pure defaults first (establishes the "last valid model"); CLI values are applied after.
  requestValidation(!pendingCli_.empty());
}

void RunnerWindow::rowChanged(int) { scheduleUpdate(); }

void RunnerWindow::scheduleUpdate() {
  if (def_.liveUpdate) {
    debounce_.start();
    status_->setText("Değişiklik algılandı…");
  } else {
    dirtyLbl_->show();
    status_->setText("Değişiklikler bekliyor — \"Güncelle\"ye basın");
  }
}

void RunnerWindow::requestValidation(bool defaultsOnly) {
  dirtyLbl_->hide();
  std::map<std::string, double> vals;
  if (defaultsOnly) {
    for (auto& r : rows_) vals[r->p.name] = r->p.def;
  } else {
    vals = currentValues();
  }
  ++gen_;
  latest_ = toOverrides(vals);
  latestDefaultsOnly_ = defaultsOnly;
  if (running_) { pending_ = true; return; }
  startJob();
}

void RunnerWindow::startJob() {
  running_ = true;
  pending_ = false;
  jobGen_ = gen_;
  defaultsOnly_ = latestDefaultsOnly_;
  status_->setText("İmalat kuralları denetleniyor ve model güncelleniyor…");
  auto def = defShared_;
  auto ov = latest_;
  watcher_.setFuture(QtConcurrent::run([def, ov] { return validateSafe(*def, ov); }));
}

void RunnerWindow::onJobFinished() {
  running_ = false;
  if (jobGen_ != gen_) { startJob(); return; }      // stale -> drop, run the newest request
  bool wasDefaults = defaultsOnly_;
  applyResult(watcher_.result());
  if (wasDefaults && !pendingCli_.empty()) {
    auto cli = pendingCli_;
    pendingCli_.clear();
    for (auto& [k, v] : cli) {
      bool done = false;
      for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i]->p.name == k) {
        double lo = rows_[i]->dsb ? rows_[i]->dsb->minimum() : rows_[i]->isb->minimum();
        double hi = rows_[i]->dsb ? rows_[i]->dsb->maximum() : rows_[i]->isb->maximum();
        if (v < lo || v > hi) fprintf(stderr, "Uyarı: %s=%g izin verilen aralığın [%g, %g] dışında, kırpıldı.\n", k.c_str(), v, lo, hi);
        setRowValue(static_cast<int>(i), v, true);
        done = true;
      }
      if (!done) extra_[k] = v;
    }
    requestValidation(false);
    return;
  }
  if (waitingSettled_) { waitingSettled_ = false; emit settled(); }
}

void RunnerWindow::applyResult(const pc::mfg::ValidationResult& r) {
  last_ = r;
  bool ok = acceptable(def_, r);
  bool haveShape = !r.model.shape.IsNull();
  bool show = ok || (!def_.blockOnErrors && r.regenerated && r.model.ok);
  bool firstBad = false;
  if (!show && !hasShown_ && haveShape) { show = true; firstBad = true; }

  if (show) {
    viewer_->clearGhost();
    // Keep the user's camera for small changes, but re-fit when the part grew out of the box the view was last
    // fitted to (W 60 -> 100 used to crop the part at the window edge).
    bool refit = !hasShown_;
    double bb[6] = {0, 0, 0, 0, 0, 0};
    try {
      const auto mp = pc::model::massProperties(r.model.shape);
      const double nb[6] = {mp.xmin, mp.ymin, mp.zmin, mp.xmax, mp.ymax, mp.zmax};
      for (int i = 0; i < 6; ++i) bb[i] = nb[i];
      if (hasFitBox_) {
        double diag = 0;
        for (int i = 0; i < 3; ++i) diag += (fitBox_[i + 3] - fitBox_[i]) * (fitBox_[i + 3] - fitBox_[i]);
        const double tol = 0.05 * std::sqrt(diag);
        for (int i = 0; i < 3; ++i)
          if (bb[i] < fitBox_[i] - tol || bb[i + 3] > fitBox_[i + 3] + tol) refit = true;
      }
    } catch (...) {}
    viewer_->setModel(r.model.shape, r.model.faces, r.model.edges, !refit);
    if (refit) {
      viewer_->fitAll();
      for (int i = 0; i < 6; ++i) fitBox_[i] = bb[i];
      hasFitBox_ = true;
    }
    shown_ = r;
    hasShown_ = true;
    updateInfo();
    updateBodies();
  } else if (haveShape) {
    viewer_->setGhost(r.model.shape);
  } else {
    viewer_->clearGhost();
  }
  viewer_->clearHighlights();

  // Banner
  bool anySuggestion = false;
  for (auto& i : r.report.issues)
    if (i.suggestedValue && !i.params.empty() && issueVisible(def_, i))
      for (auto& rw : rows_) if (rw->p.name == i.params[0]) anySuggestion = true;
  if (!show) {
    QString t = "Parametreler imalat kurallarını ihlal ediyor — model güncellenmedi";
    if (!r.model.ok && r.regenerated)
      for (auto& s : r.model.status) if (!s.ok) { t += " (" + QString::fromStdString(s.featureId + ": " + s.message) + ")"; break; }
    bannerText_->setText(t);
    banner_->setStyleSheet("QFrame{background:#fdecea;border:1px solid #f1a9a0;border-radius:6px;} QLabel{color:#8c1d13;font-weight:600;border:none;background:transparent;}");
    bannerApply_->setVisible(anySuggestion);
    banner_->show();
  } else if (!ok) {
    bannerText_->setText(firstBad ? "Varsayılan model imalat kurallarını ihlal ediyor"
                                  : "Uyarı: parametreler imalat kurallarını ihlal ediyor (güncelleme engeli kapalı, model güncellendi)");
    banner_->setStyleSheet("QFrame{background:#fff4e5;border:1px solid #f3c27c;border-radius:6px;} QLabel{color:#8a4b00;font-weight:600;border:none;background:transparent;}");
    bannerApply_->setVisible(anySuggestion);
    banner_->show();
  } else {
    banner_->hide();
  }

  updateIndicators(r);
  updateResults(r.values);
  refreshIssueList();
  int e = 0, w = 0;
  for (auto& i : r.report.issues) if (issueVisible(def_, i)) { if (i.severity == Severity::Error) ++e; else if (i.severity == Severity::Warning) ++w; }
  status_->setText(QString("Doğrulama tamamlandı — %1 hata, %2 uyarı").arg(e).arg(w) + (show ? "" : "  |  gösterilen model son geçerli olandır"));
}

// One rule for the per-field "Öneri" button and the banner's "Önerileri uygula": the most severe issue wins;
// among equally severe ones the value farthest from the current one (it satisfies every limit, e.g. AM 0.4 and
// CNC 0.8 -> 0.8).
static std::optional<double> strictestSuggestion(const pc::app::AppDefinition& def, const pc::mfg::Report& rep,
                                                 const std::string& param, double cur) {
  std::optional<double> best;
  int bestSev = -1;
  for (auto& i : rep.issues) {
    if (!issueVisible(def, i) || !i.suggestedValue || i.params.empty() || i.params[0] != param) continue;
    const int sev = int(i.severity);
    if (!best || sev > bestSev || (sev == bestSev && std::fabs(*i.suggestedValue - cur) > std::fabs(*best - cur))) {
      best = *i.suggestedValue;
      bestSev = sev;
    }
  }
  return best;
}

void RunnerWindow::updateIndicators(const pc::mfg::ValidationResult& r) {
  struct Info { Severity sev = Severity::Info; QStringList msgs; bool any = false; std::optional<double> sug; Severity sugSev = Severity::Info; };
  std::map<std::string, Info> m;
  for (auto& i : r.report.issues) {
    if (!issueVisible(def_, i) || i.severity == Severity::Info) continue;
    for (size_t k = 0; k < std::min<size_t>(1, i.params.size()); ++k) {   // only the main driving parameter
      Info& inf = m[i.params[k]];
      if (!inf.any || i.severity > inf.sev) inf.sev = i.severity;
      inf.any = true;
      inf.msgs << QString::fromStdString(i.message);
      if (k == 0 && i.suggestedValue && (!inf.sug || i.severity > inf.sugSev)) { inf.sug = i.suggestedValue; inf.sugSev = i.severity; }
    }
  }
  for (auto& rp : rows_) {
    Row& row = *rp;
    QWidget* field = row.dsb ? static_cast<QWidget*>(row.dsb) : static_cast<QWidget*>(row.isb);
    auto it = m.find(row.p.name);
    QString cls = row.dsb ? "QDoubleSpinBox" : "QSpinBox";
    if (it == m.end()) {
      field->setStyleSheet("");
      field->setToolTip("");
      row.label->setStyleSheet("");
      row.msgRow->hide();
      row.suggestion.reset();
      continue;
    }
    Info inf = it->second;
    QColor c = sevColor(inf.sev);
    field->setStyleSheet(QString("%1{border:2px solid %2;}").arg(cls, c.name()));
    field->setToolTip(inf.msgs.join("\n"));
    row.label->setStyleSheet(QString("color:%1;font-weight:600;").arg(c.name()));
    row.msg->setStyleSheet(QString("font-size:11px;color:%1;").arg(c.name()));
    row.msg->setText(inf.msgs.first() + (inf.msgs.size() > 1 ? QString("  (+%1)").arg(inf.msgs.size() - 1) : QString()));
    row.msg->setToolTip(inf.msgs.join("\n"));
    row.suggestion.reset();
    if (inf.sug) inf.sug = strictestSuggestion(def_, r.report, row.p.name, row.value());
    if (inf.sug) {
      double lo = row.dsb ? row.dsb->minimum() : row.isb->minimum(), hi = row.dsb ? row.dsb->maximum() : row.isb->maximum();
      double s = std::clamp(*inf.sug, lo, hi);
      if (row.isb) s = std::round(s);
      row.suggestion = s;
      row.apply->setText(QString("Öneri: %1 %2 — Uygula").arg(num(s), unitShort(row.p.kind)).trimmed());
      row.apply->setVisible(true);
    } else {
      row.apply->setVisible(false);
    }
    row.msgRow->show();
  }
}

void RunnerWindow::refreshIssueList() {
  issueList_->clear();
  // v2: body filter (shown only for multi-body parts or when issues carry body names)
  {
    QString cur = bodyFilter_->currentData().toString();
    std::vector<std::string> names;
    for (auto& b : last_.model.bodies) names.push_back(b.name);
    for (auto& i : last_.report.issues)
      if (!i.body.empty() && issueVisible(def_, i) && std::find(names.begin(), names.end(), i.body) == names.end()) names.push_back(i.body);
    updatingBodies_ = true;
    bodyFilter_->clear();
    bodyFilter_->addItem("Tüm gövdeler", "");
    for (auto& n : names) bodyFilter_->addItem(QString::fromStdString(n), QString::fromStdString(n));
    int ix = bodyFilter_->findData(cur);
    bodyFilter_->setCurrentIndex(ix < 0 ? 0 : ix);
    updatingBodies_ = false;
    bodyFilter_->setVisible(names.size() > 1);
  }
  const std::string bodySel = bodyFilter_->isVisible() ? bodyFilter_->currentData().toString().toStdString() : std::string();
  int tab = tabs_->currentIndex();
  QString tabName = tabs_->tabData(tab).toString();
  bool all = tabName == "all";
  int cntAm = 0, cntConv = 0, e = 0, w = 0;
  struct Item { QString text; Severity sev; QStringList faces, edges; };
  std::vector<Item> items;
  for (auto& i : last_.report.issues) {
    if (!issueVisible(def_, i)) continue;
    if (i.domain == Domain::Additive) ++cntAm;
    else if (i.domain == Domain::Conventional) ++cntConv;
    bool inTab = all || i.domain == Domain::Parameter || i.domain == Domain::Model ||
                 (tabName == "am" && i.domain == Domain::Additive) ||
                 (tabName == "conv" && i.domain == Domain::Conventional);
    if (!bodySel.empty() && i.body != bodySel) continue;
    if (i.severity == Severity::Error) ++e; else if (i.severity == Severity::Warning) ++w;
    if (!inTab) continue;
    Item it;
    it.sev = i.severity;
    it.text = sevName(i.severity) + "  ·  " + QString::fromStdString(std::string(pc::mfg::toString(i.domain)) + "  ·  " + i.ruleId) + "\n" +
              QString::fromStdString(i.message);
    if (!i.body.empty()) it.text += "\nGövde: " + QString::fromStdString(i.body);
    if (!i.params.empty()) {
      QStringList ps;
      for (auto& p : i.params) ps << QString::fromStdString(p);
      it.text += "\nParametre: " + ps.join(", ");
    }
    if (i.suggestedValue && !i.params.empty()) it.text += "   →   Öneri: " + QString::fromStdString(i.params[0]) + " = " + num(*i.suggestedValue);
    for (auto& f : i.faces) it.faces << QString::fromStdString(f);
    for (auto& f : i.edges) it.edges << QString::fromStdString(f);
    items.push_back(it);
  }
  // Model regeneration failures
  if (last_.regenerated && !last_.model.ok && (all || true))
    for (auto& s : last_.model.status) if (!s.ok) {
      Item it; it.sev = Severity::Error;
      it.text = "HATA  ·  Model  ·  " + QString::fromStdString(s.featureId) + "\n" + QString::fromStdString(s.message);
      items.push_back(it);
      ++e;
    }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.sev > b.sev; });
  for (auto& it : items) {
    auto* li = new QListWidgetItem(dot(sevColor(it.sev)), it.text);
    QColor c = sevColor(it.sev);
    li->setForeground(QColor("#26323f"));
    li->setBackground(QColor(c.red(), c.green(), c.blue(), 22));
    li->setData(Qt::UserRole + 1, it.faces);
    li->setData(Qt::UserRole + 2, it.edges);
    issueList_->addItem(li);
  }
  for (int i = 0; i < tabs_->count(); ++i) {
    QString k = tabs_->tabData(i).toString();
    if (k == "am") tabs_->setTabText(i, QString("Katmanlı (%1)").arg(cntAm));
    else if (k == "conv") tabs_->setTabText(i, QString("Konvansiyonel (%1)").arg(cntConv));
  }
  if (summary_) {
    if (e == 0 && w == 0)
      summary_->setText(QString("<span style='color:%1;font-weight:600'>✔ İmalat kurallarına uygun</span>").arg(kGreen));
    else
      summary_->setText(QString("<span style='color:%1;font-weight:600'>● %2 hata</span> &nbsp; <span style='color:%3;font-weight:600'>● %4 uyarı</span>")
                            .arg(kRed).arg(e).arg(kOrange).arg(w));
  }
}

void RunnerWindow::updateInfo() {
  if (!hasShown_) return;
  auto mp = pc::model::massProperties(shown_.model.shape);
  double dx = mp.xmax - mp.xmin, dy = mp.ymax - mp.ymin, dz = mp.zmax - mp.zmin;
  MfgSettings ms = def_.document.mfg;
  std::string mat = activeMaterial(ms);
  double rho = densityForMaterial(mat);
  double vol_cm3 = mp.volume / 1000.0;
  QString s;
  if (mp.volume > 0) {
    s = QString("<b>Hacim:</b> %1 cm³ &nbsp;&nbsp; <b>Kütle:</b> %2 g <span style='color:#667'>(%3, %4 g/cm³)</span> &nbsp;&nbsp; <b>Sınırlayıcı kutu:</b> %5 × %6 × %7 mm")
            .arg(vol_cm3, 0, 'f', 2).arg(vol_cm3 * rho, 0, 'f', 1).arg(QString::fromStdString(mat)).arg(rho, 0, 'f', 2)
            .arg(dx, 0, 'f', 1).arg(dy, 0, 'f', 1).arg(dz, 0, 'f', 1);
    if (shown_.model.bodies.size() > 1)
      s = QString("<b>Gövde:</b> %1 &nbsp;&nbsp; <b>Toplam</b> ").arg(shown_.model.bodies.size()) + s;
    if (def_.showAdditiveChecks && ms.am.enabled && ms.am.process != AmProcess::None) {
      bool fits = dx <= ms.am.buildX + 1e-6 && dy <= ms.am.buildY + 1e-6 && dz <= ms.am.buildZ + 1e-6;
      s += QString(" &nbsp;&nbsp; <b>Yapı hacmi:</b> <span style='color:%1;font-weight:600'>%2</span> <span style='color:#667'>(%3 × %4 × %5 mm)</span>")
               .arg(fits ? kGreen : kRed).arg(fits ? "✔ sığıyor" : "✖ aşıyor").arg(ms.am.buildX, 0, 'f', 0).arg(ms.am.buildY, 0, 'f', 0).arg(ms.am.buildZ, 0, 'f', 0);
    }
  } else {
    s = "<span style='color:#667'>Model bilgisi yok</span>";
  }
  infoLbl_->setText(s);
}

void RunnerWindow::updateBodies() {
  const auto& bodies = shown_.model.bodies;
  bool multi = bodies.size() > 1;
  double rho = densityForMaterial(activeMaterial(def_.document.mfg));
  auto sums = bodySummaries(shown_.model, rho);
  updatingBodies_ = true;
  bodyList_->clear();
  if (multi) {
    for (size_t i = 0; i < bodies.size(); ++i) {
      auto* it = new QListWidgetItem(swatch(bodyColor(i)),
          QString("%1  —  %2 cm³ · %3 g").arg(QString::fromStdString(bodies[i].name)).arg(sums[i].volume / 1000.0, 0, 'f', 2).arg(sums[i].massG, 0, 'f', 1));
      it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
      it->setCheckState(Qt::Checked);
      if (transBodies_.count(bodies[i].name)) it->setText(it->text() + "  (saydam)");
      bodyList_->addItem(it);
    }
    bodyList_->setFixedHeight(std::min<int>(130, 8 + 24 * static_cast<int>(bodies.size())));
  }
  updatingBodies_ = false;
  bodyBox_->setVisible(multi || !shown_.model.datumPlanes.empty());
  bodyList_->setVisible(multi);
  planesChk_->setVisible(!shown_.model.datumPlanes.empty());
  bodyBox_->setTitle(multi ? QString("Gövdeler (%1)").arg(bodies.size()) : QString("Yardımcı düzlemler"));
  applyVisibility();
}

void RunnerWindow::applyVisibility() {
  const auto& m = shown_.model;
  // colours: only for multi-body parts (single body keeps the default look)
  std::map<std::string, QColor> colors;
  QStringList hidden;
  if (m.bodies.size() > 1) {
    for (size_t i = 0; i < m.bodies.size(); ++i) {
      bool vis = i >= static_cast<size_t>(bodyList_->count()) || bodyList_->item(static_cast<int>(i))->checkState() == Qt::Checked;
      for (auto& f : m.bodies[i].faces) {
        colors[f] = bodyColor(i);
        if (!vis) hidden << QString::fromStdString(f);
      }
    }
  }
  {   // SC-COLOR: face / body colours saved with the app (Document::view.colors) override the body palette
    const pc::model::ViewColorMap vc = pc::model::resolveViewColors(def_.document, m);
    for (const auto& [f, hex] : vc.face) colors[f] = QColor(QString::fromStdString(hex));
  }
  viewer_->setFaceColors(colors);
  viewer_->setHiddenFaces(hidden);
  QStringList trans;
  for (const auto& b : m.bodies)
    if (transBodies_.count(b.name)) for (auto& f : b.faces) trans << QString::fromStdString(f);
  viewer_->setTransparentFaces(trans);
  std::vector<pc::viewer::DatumPlaneDisplay> planes;
  if (planesChk_->isChecked()) {
    for (auto& [id, dp] : m.datumPlanes) {
      if (const pc::Feature* hf = def_.document.feature(id); hf && hf->prop("show", "1") == "0") continue;   // construction plane
      pc::viewer::DatumPlaneDisplay d;
      d.name = QString::fromStdString(id);
      gp_Pnt o = dp.frame.Location();
      gp_Dir u = dp.frame.XDirection(), v = dp.frame.YDirection();
      d.origin[0] = o.X(); d.origin[1] = o.Y(); d.origin[2] = o.Z();
      d.u[0] = u.X(); d.u[1] = u.Y(); d.u[2] = u.Z();
      d.v[0] = v.X(); d.v[1] = v.Y(); d.v[2] = v.Z();
      std::vector<std::string> refs;
      if (const pc::Feature* pf = def_.document.feature(id)) refs = pf->refs;
      d.halfSize = pc::viewer::datumPlaneHalfSize(m.shape, refs, m.edges, m.faces, dp.displaySize * 0.4);
      planes.push_back(d);
    }
  }
  viewer_->setDatumPlanes(planes);
}

void RunnerWindow::applySuggestion(int idx) {
  Row& r = *rows_[idx];
  if (!r.suggestion) return;
  setRowValue(idx, *r.suggestion, true);
  // An explicit "Uygula" is a commit: validate + update now (also in non-live mode, like the banner).
  debounce_.stop();
  requestValidation(false);
}

void RunnerWindow::applySuggestionsAll() {
  // For every driving parameter pick the suggestion farthest from the current value.
  std::map<std::string, double> best;
  for (auto& r : rows_)
    if (auto s = strictestSuggestion(def_, last_.report, r->p.name, r->value())) best[r->p.name] = *s;
  bool any = false;
  for (size_t k = 0; k < rows_.size(); ++k) {
    auto it = best.find(rows_[k]->p.name);
    if (it == best.end()) continue;
    setRowValue(static_cast<int>(k), it->second, true);
    any = true;
  }
  if (any) { debounce_.stop(); requestValidation(false); }
}

void RunnerWindow::resetAll() {
  for (size_t k = 0; k < rows_.size(); ++k) setRowValue(static_cast<int>(k), rows_[k]->p.def, true);
  extra_.clear();
  debounce_.stop();
  requestValidation(false);
}

void RunnerWindow::exportShape(bool step) {
  if (!hasShown_ || shown_.model.shape.IsNull()) {
    QMessageBox::information(this, "Dışa aktar", "Dışa aktarılacak model yok.");
    return;
  }
  if ((step && !def_.allowExportSTEP) || (!step && !def_.allowExportSTL)) return;
  TopoDS_Shape toExport = shown_.model.shape;
  QString suffix;
  std::string pickedBody;   // single-body export (named STEP export filters by body name)
  if (shown_.model.bodies.size() > 1) {
    QStringList choices{"Tüm gövdeler"};
    for (auto& b : shown_.model.bodies) choices << QString::fromStdString(b.name);
    int cur = bodyList_->currentRow() + 1;     // preselect the highlighted body (0 = all)
    bool okSel = false;
    QString pick = QInputDialog::getItem(this, "Dışa aktar", "Hangi gövde dışa aktarılsın?", choices, std::max(0, cur), false, &okSel);
    if (!okSel) return;
    int k = choices.indexOf(pick);
    if (k > 0) {
      toExport = shown_.model.bodies[k - 1].shape;
      pickedBody = shown_.model.bodies[k - 1].name;
      // body names may contain ':' (split pieces "Body:2") or other characters that are invalid in file
      // names on Windows (':' even selects an NTFS alternate data stream): keep the file name portable
      QString safe = pick;
      for (QChar& ch : safe)
        if (QString("\\/:*?\"<>|").contains(ch) || ch.isSpace()) ch = '_';
      suffix = "_" + safe;
    }
  }
  QString base = QString::fromStdString(def_.appName).replace(' ', '_') + suffix;
  QString f = QFileDialog::getSaveFileName(this, step ? "STEP olarak kaydet" : "STL olarak kaydet", base + (step ? ".step" : ".stl"),
                                           step ? "STEP (*.step *.stp)" : "STL (*.stl)");
  if (f.isEmpty()) return;
  std::string err;
  // STEP: faces carry the named-selection names (boundary zones for CFD/FEA meshers)
  pc::model::StepExportOptions so;
  if (!pickedBody.empty()) so.bodies = {pickedBody};
  bool ok = step ? pc::model::exportSTEPNamed(def_.document, shown_.model, f.toStdString(), so, &err)
                 : pc::model::exportSTL(toExport, f.toStdString(), 0.05, &err);
  if (!ok) QMessageBox::critical(this, "Dışa aktar", "Dışa aktarma başarısız:\n" + QString::fromStdString(err));
  else status_->setText("Kaydedildi: " + f);
}

void RunnerWindow::about() {
  QString t = QString("<h3>%1</h3><p>Sürüm %2</p>").arg(QString::fromStdString(def_.appName).toHtmlEscaped(),
                                                       QString::fromStdString(def_.version).toHtmlEscaped());
  if (!def_.author.empty()) t += "<p>Yazar: " + QString::fromStdString(def_.author).toHtmlEscaped() + "</p>";
  if (!def_.description.empty()) t += "<p>" + QString::fromStdString(def_.description).toHtmlEscaped() + "</p>";
  t += "<p style='color:#667'>Bu uygulama <b>PersistCAD</b> ile oluşturuldu. Her parametre değişikliğinden önce "
       "katmanlı ve konvansiyonel imalat uygunluk kontrolleri çalıştırılır.</p>";
  QMessageBox::about(this, "Hakkında", t);
}

void RunnerWindow::setShowPlanes(bool on) { planesChk_->setChecked(on); }

void RunnerWindow::showResults() {
  if (paramScroll_ && resultsBox_) paramScroll_->ensureWidgetVisible(resultsBox_, 0, 0);
}

void RunnerWindow::setBodyTransparent(const QString& body, bool on) {
  if (on) transBodies_.insert(body.toStdString()); else transBodies_.erase(body.toStdString());
  const auto& bodies = shown_.model.bodies;
  updatingBodies_ = true;
  for (int i = 0; i < bodyList_->count() && i < static_cast<int>(bodies.size()); ++i) {
    if (bodies[static_cast<size_t>(i)].name != body.toStdString()) continue;
    QString t = bodyList_->item(i)->text();
    t.remove("  (saydam)");
    bodyList_->item(i)->setText(on ? t + "  (saydam)" : t);
  }
  updatingBodies_ = false;
  applyVisibility();
}

QImage RunnerWindow::screenshot() { return grab().toImage(); }

} // namespace pc::runner
