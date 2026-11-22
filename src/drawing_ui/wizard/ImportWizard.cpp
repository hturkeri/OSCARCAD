// "Dosya > Teknik resimden içe aktar…" — modal QWizard with the 7 steps of SPEC §2.12 (WP-3).
// Every correction is a UserEdit appended to ir.edits; the pipeline is re-run from the edit's stage (edits replay).
// The overlay is WP-2's OverlayView (used only through its frozen header).
#include "wizard/ImportWizard.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include "pc/drawing/Drawing.h"
#include "pc/drawing/qt/PdfRender.h"
#include "pc/drawing/ui/OverlayView.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace pc::drawing::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }
QString num(double v, int prec = 4) { return QString::number(v, 'g', prec); }

const char* tagLetter(ParamTag t) {
  switch (t) {
    case ParamTag::FromDimension: return "[D]";
    case ParamTag::Measured: return "[Ö]";
    case ParamTag::Assumed: return "[V]";
    case ParamTag::Derived: return "[T]";
  }
  return "";
}

UserEdit makeEdit(Stage s, const char* op, const nlohmann::json& payload) { return {s, op, payload.dump()}; }

QTableWidgetItem* roItem(const QString& t) {
  auto* it = new QTableWidgetItem(t);
  it->setFlags(it->flags() & ~Qt::ItemIsEditable);
  return it;
}

}  // namespace

// ===================================================================================================== page base
class WizardPageBase : public QWizardPage {
 public:
  WizardPageBase(ImportWizard* w, Stage stage, const QString& title, const QString& sub) : wiz_(w), stage_(stage) {
    setTitle(title);
    setSubTitle(sub);
    lay_ = new QVBoxLayout(this);
  }
  virtual void refresh() {}
  void initializePage() override { refresh(); }
  Stage stage() const { return stage_; }

 protected:
  DrawingIR& ir() { return wiz_->state().ir; }
  // "Bu adımdan itibaren yeniden hesapla" + status line
  void addFooter() {
    auto* h = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName("statusLabel");
    status_->setWordWrap(true);
    auto* b = new QPushButton("Bu adımdan itibaren yeniden hesapla", this);
    b->setObjectName("rerunButton");
    connect(b, &QPushButton::clicked, this, [this] { wiz_->rerunFrom(stage_); });
    h->addWidget(status_, 1);
    h->addWidget(b);
    lay_->addLayout(h);
  }
  void setStatusFromIR() {
    if (!status_) return;
    int warn = 0, err = 0;
    for (auto& d : ir().diagnostics)
      if (d.stage == static_cast<int>(stage_)) { warn += d.severity == Severity::Warning; err += d.severity == Severity::Error; }
    const auto& r = wiz_->state().last;
    status_->setText(QString("%1 — %2 uyarı, %3 hata%4")
                         .arg(labelTr(stage_)).arg(warn).arg(err)
                         .arg(r.ok() ? QString() : QString(" | son çalıştırma: ") + qs(r.message)));
  }
  OverlayView* makeOverlay(const char* name, Stage s, uint32_t layers, OverlayTool tool) {
    auto* ov = new OverlayView(this);
    ov->setObjectName(name);
    ov->setStage(s);
    ov->setLayers(layers);
    ov->setTool(tool);
    ov->setMinimumSize(360, 260);
    connect(ov, &OverlayView::editRequested, this, [this](const UserEdit& e) { wiz_->addEdit(e); });
    overlays_.push_back(ov);
    return ov;
  }
  void refreshOverlays() {
    for (auto* ov : overlays_) { ov->setIR(&ir()); ov->refresh(); }
  }
  // the overlay's first fit happens while the page is still hidden (no final size): fit again once shown
  void showEvent(QShowEvent* e) override {
    QWizardPage::showEvent(e);
    QTimer::singleShot(0, this, [this] { for (auto* ov : overlays_) ov->zoomToSheet(); });
  }

  ImportWizard* wiz_;
  Stage stage_;
  QVBoxLayout* lay_ = nullptr;
  QLabel* status_ = nullptr;
  std::vector<OverlayView*> overlays_;
};

// ===================================================================================================== 1 Kaynak
class SourcePage : public WizardPageBase {
 public:
  SourcePage(ImportWizard* w, const QString& initial)
      : WizardPageBase(w, Stage::Ingest, "1 Kaynak", "Teknik resim dosyasını ve sayfayı seçin (DXF, vektör PDF, PNG/JPG, taranmış PDF).") {
    auto* f = new QFormLayout;
    auto* h = new QHBoxLayout;
    file_ = new QLineEdit(initial, this);
    file_->setObjectName("fileEdit");
    auto* browse = new QPushButton("Gözat…", this);
    browse->setObjectName("browseButton");
    h->addWidget(file_, 1);
    h->addWidget(browse);
    f->addRow("Dosya:", h);
    kind_ = new QComboBox(this);
    kind_->setObjectName("kindCombo");
    kind_->addItem("Otomatik", -1);
    const char* kindTr[] = {"Vektör PDF", "Taranmış PDF", "DXF", "Görüntü", "Fotoğraf"};
    for (int i = 0; i < 5; ++i) kind_->addItem(kindTr[i], i);
    f->addRow("Tür:", kind_);
    page_ = new QSpinBox(this);
    page_->setObjectName("pageSpin");
    page_->setRange(1, 1);
    f->addRow("Sayfa:", page_);
    dpi_ = new QSpinBox(this);
    dpi_->setObjectName("dpiSpin");
    dpi_->setRange(0, 2400);
    dpi_->setSpecialValueText("dosyadan");
    f->addRow("DPI:", dpi_);
    photo_ = new QCheckBox("Fotoğraf (perspektif düzeltme)", this);
    photo_->setObjectName("photoCheck");
    f->addRow("", photo_);
    lay_->addLayout(f);
    info_ = new QLabel(this);
    info_->setObjectName("sniffLabel");
    info_->setWordWrap(true);
    lay_->addWidget(info_);
    thumb_ = new QLabel(this);
    thumb_->setObjectName("thumbLabel");
    thumb_->setMinimumHeight(160);
    thumb_->setAlignment(Qt::AlignCenter);
    lay_->addWidget(thumb_, 1);
    connect(browse, &QPushButton::clicked, this, [this] {
      QString fn = QFileDialog::getOpenFileName(this, "Teknik resim seç", QFileInfo(file_->text()).absolutePath(),
                                                "Teknik resim (*.dxf *.pdf *.png *.jpg *.jpeg *.tif *.tiff *.pcdrw.json);;Tüm dosyalar (*)");
      if (!fn.isEmpty()) file_->setText(fn);
    });
    connect(file_, &QLineEdit::textChanged, this, [this] { sniffFile(); emit completeChanged(); });
    sniffFile();
  }
  bool isComplete() const override { return QFileInfo(file_->text()).isFile(); }

  bool validatePage() override {
    auto& st = wiz_->state();
    std::string path = ss(file_->text());
    if (path != st.path) { st.ir = DrawingIR{}; st.path = path; }
    // ingest edits (replayed by the pipeline before S1)
    auto& E = st.ir.edits;
    E.erase(std::remove_if(E.begin(), E.end(), [](const UserEdit& e) { return e.stage == Stage::Ingest; }), E.end());
    if (kind_->currentData().toInt() >= 0)
      E.push_back(makeEdit(Stage::Ingest, edit_op::setSourceKind, {{"kind", toString(static_cast<SourceKind>(kind_->currentData().toInt()))}}));
    if (page_->value() > 1) E.push_back(makeEdit(Stage::Ingest, edit_op::setPage, {{"page", page_->value() - 1}}));
    if (dpi_->value() > 0) E.push_back(makeEdit(Stage::Ingest, edit_op::setDpi, {{"dpi", dpi_->value()}}));
    st.opts.ingest.rasterIsPhoto = photo_->isChecked();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = wiz_->rerunFrom(Stage::Ingest);
    QApplication::restoreOverrideCursor();
    if (!ok) {
      info_->setText("<b>İçe aktarma başarısız:</b> " + qs(st.last.message).toHtmlEscaped());
      return false;
    }
    return true;
  }

 private:
  void sniffFile() {
    QString fn = file_->text();
    thumb_->clear();
    if (!QFileInfo(fn).isFile()) { info_->setText("Dosya seçilmedi."); return; }
    if (fn.endsWith(".json", Qt::CaseInsensitive)) {
      info_->setText("DrawingIR dosyası (pcdrw/1): ilkeller ve düzeltmeler dosyadan okunur.");
      page_->setRange(1, 1);
      return;
    }
    SniffResult s = sniff(ss(fn));
    page_->setRange(1, std::max(1, s.pageCount));
    QString msg = s.result.ok() ? QString("Tespit edilen tür: %1, %2 sayfa").arg(toString(s.kind)).arg(s.pageCount)
                                : QString("Tür tespit edilemedi (%1); uzantıya göre denenecek").arg(qs(s.result.message));
    if (!s.message.empty()) msg += "\n" + qs(s.message);
    if (s.dpi > 0) { msg += QString("\nDPI: %1").arg(s.dpi); }
    photo_->setChecked(s.looksLikePhoto);
    info_->setText(msg);
    QImage img;
    if (fn.endsWith(".pdf", Qt::CaseInsensitive)) img = qt::renderPdfThumbnail(fn, page_->value() - 1, 240);
    else if (!fn.endsWith(".dxf", Qt::CaseInsensitive)) img = QImage(fn);
    if (!img.isNull()) thumb_->setPixmap(QPixmap::fromImage(img.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
  }
  QLineEdit* file_;
  QComboBox* kind_;
  QSpinBox *page_, *dpi_;
  QCheckBox* photo_;
  QLabel *info_, *thumb_;
};

// ===================================================================================================== 2 Sayfa düzeni
class LayoutPage : public WizardPageBase {
 public:
  explicit LayoutPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Layout, "2 Sayfa düzeni", "Çerçeve, antet ve görünüşler. Kutuları sürükleyerek düzeltin; ölçek ve projeksiyon yöntemini onaylayın.") {
    auto* split = new QSplitter(this);
    split->addWidget(makeOverlay("overlayLayout", Stage::Layout, LayerDefault, OverlayTool::EditBoxes));
    auto* side = new QWidget(split);
    auto* v = new QVBoxLayout(side);
    title_ = new QTableWidget(0, 2, side);
    title_->setObjectName("titleTable");
    title_->setHorizontalHeaderLabels({"Antet alanı", "Değer"});
    title_->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(title_);
    scale_ = new QLabel(side);
    scale_->setObjectName("scaleLabel");
    scale_->setWordWrap(true);
    v->addWidget(scale_);
    auto* sh = new QHBoxLayout;
    num_ = new QSpinBox(side); num_->setObjectName("scaleNum"); num_->setRange(1, 1000);
    den_ = new QSpinBox(side); den_->setObjectName("scaleDen"); den_->setRange(1, 1000);
    auto* apply = new QPushButton("Ölçeği uygula", side);
    apply->setObjectName("applyScaleButton");
    sh->addWidget(new QLabel("Ölçek", side)); sh->addWidget(num_); sh->addWidget(new QLabel(":", side)); sh->addWidget(den_); sh->addWidget(apply);
    v->addLayout(sh);
    proj_ = new QComboBox(side);
    proj_->setObjectName("projectionCombo");
    proj_->addItem("ISO E (1. açı)", static_cast<int>(ProjectionMethod::FirstAngle));
    proj_->addItem("ISO A (3. açı)", static_cast<int>(ProjectionMethod::ThirdAngle));
    v->addWidget(proj_);
    views_ = new QTableWidget(0, 3, side);
    views_->setObjectName("viewTable");
    views_->setHorizontalHeaderLabels({"Görünüş", "Kutu (mm)", "Rol"});
    views_->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(views_, 1);
    split->addWidget(side);
    lay_->addWidget(split, 1);
    addFooter();
    connect(apply, &QPushButton::clicked, this, [this] {
      wiz_->addEdit(makeEdit(Stage::Layout, edit_op::setScale, {{"num", num_->value()}, {"den", den_->value()}}), "*");
    });
    connect(proj_, qOverload<int>(&QComboBox::activated), this, [this](int) {
      wiz_->addEdit(makeEdit(Stage::Layout, edit_op::setProjection,
                             {{"method", toString(static_cast<ProjectionMethod>(proj_->currentData().toInt()))}}), "*");
    });
  }
  void refresh() override {
    refreshOverlays();
    const auto& L = ir().layout;
    title_->setRowCount(0);
    for (auto& [k, val] : L.title.fields) {
      int r = title_->rowCount();
      title_->insertRow(r);
      title_->setItem(r, 0, roItem(qs(k)));
      title_->setItem(r, 1, roItem(qs(val)));
    }
    QString s = QString("Antet: %1:%2 (%3)").arg(L.title.scale.num).arg(L.title.scale.den).arg(qs(L.title.scale.evidence));
    if (L.scaleFromDims.conf > 0)
      s += QString("\nÖlçülerden: %1:%2 (%3)").arg(num(L.scaleFromDims.num)).arg(num(L.scaleFromDims.den)).arg(qs(L.scaleFromDims.evidence));
    scale_->setText(s);
    QSignalBlocker b1(num_), b2(den_), b3(proj_);
    num_->setValue(std::max(1, (int)std::lround(L.title.scale.num)));
    den_->setValue(std::max(1, (int)std::lround(L.title.scale.den)));
    proj_->setCurrentIndex(ir().views.projection == ProjectionMethod::ThirdAngle ? 1 : 0);
    views_->setRowCount(0);
    for (auto& v : L.views) {
      int r = views_->rowCount();
      views_->insertRow(r);
      views_->setItem(r, 0, roItem(QString("%1 %2").arg(v.id).arg(qs(v.label))));
      views_->setItem(r, 1, roItem(QString("[%1,%2]-[%3,%4]").arg(num(v.box.lo.x)).arg(num(v.box.lo.y)).arg(num(v.box.hi.x)).arg(num(v.box.hi.y))));
      auto* cb = new QComboBox(views_);
      cb->setObjectName(QString("viewRole_%1").arg(r));
      for (int i = 0; i < (int)EnumNames<ViewRole>::count; ++i) cb->addItem(labelTr(static_cast<ViewRole>(i)), i);
      cb->setCurrentIndex(static_cast<int>(v.role));
      const int vid = v.id;
      connect(cb, qOverload<int>(&QComboBox::activated), this, [this, cb, vid](int) {
        wiz_->addEdit(makeEdit(Stage::Layout, edit_op::setViewRole, {{"viewId", vid}, {"role", toString(static_cast<ViewRole>(cb->currentData().toInt()))}}), "viewId");
      });
      views_->setCellWidget(r, 2, cb);
    }
    setStatusFromIR();
  }

 private:
  QTableWidget *title_, *views_;
  QLabel* scale_;
  QSpinBox *num_, *den_;
  QComboBox* proj_;
};

// ===================================================================================================== 3 Geometri
class GeometryPage : public WizardPageBase {
 public:
  explicit GeometryPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Normalize, "3 Geometri", "Çizgi rolleri: görünür siyah, gizli mavi kesik, eksen kırmızı, ölçü yeşil, tarama gri. Güven: yeşil ≥0.9, sarı 0.6–0.9, kırmızı <0.6.") {
    ov_ = makeOverlay("overlayGeometry", Stage::Normalize, LayerDefault | LayerRoles | LayerConfidence, OverlayTool::Select);
    lay_->addWidget(ov_, 1);
    auto* h = new QHBoxLayout;
    struct T { const char* name; const char* text; OverlayTool tool; };
    const T tools[] = {{"toolSelect", "Seç", OverlayTool::Select}, {"toolLine", "Çizgi", OverlayTool::DrawLine},
                       {"toolArc", "Yay", OverlayTool::DrawArc}, {"toolCircle", "Daire", OverlayTool::DrawCircle},
                       {"toolSnap", "Yakala", OverlayTool::Snap}};
    for (auto& t : tools) {
      auto* b = new QPushButton(t.text, this);
      b->setObjectName(t.name);
      OverlayTool tool = t.tool;
      connect(b, &QPushButton::clicked, this, [this, tool] { ov_->setTool(tool); });
      h->addWidget(b);
    }
    auto* del = new QPushButton("Seçileni sil", this);
    del->setObjectName("deleteButton");
    connect(del, &QPushButton::clicked, this, [this] {
      auto sel = ov_->selection();
      if (!sel.empty()) wiz_->addEdit(makeEdit(Stage::Normalize, edit_op::deletePrims, {{"ids", sel}}));
    });
    h->addWidget(del);
    h->addStretch(1);
    role_ = new QComboBox(this);
    role_->setObjectName("roleCombo");
    for (int i = 0; i < (int)EnumNames<LineRole>::count; ++i) role_->addItem(labelTr(static_cast<LineRole>(i)), i);
    role_->setCurrentIndex(static_cast<int>(LineRole::Visible));
    auto* setRole = new QPushButton("Rolü seçilene uygula", this);
    setRole->setObjectName("applyRoleButton");
    connect(setRole, &QPushButton::clicked, this, [this] {
      auto sel = ov_->selection();
      if (sel.empty()) return;
      wiz_->addEdit(makeEdit(Stage::Annotate, edit_op::setRole, {{"ids", sel}, {"role", toString(static_cast<LineRole>(role_->currentData().toInt()))}}));
    });
    h->addWidget(role_);
    h->addWidget(setRole);
    lay_->addLayout(h);
    counts_ = new QLabel(this);
    counts_->setObjectName("roleCounts");
    lay_->addWidget(counts_);
    addFooter();
  }
  void refresh() override {
    refreshOverlays();
    std::map<LineRole, int> n;
    int low = 0;
    for (auto& [id, r] : ir().annot.roles) ++n[r];
    for (auto& [id, c] : ir().annot.roleConf) low += c < kConfHeuristic;
    QString s = QString("%1 ilkel, %2 metin").arg(ir().norm.prims.size()).arg(ir().norm.texts.size());
    for (auto& [r, k] : n) s += QString(" | %1: %2").arg(labelTr(r)).arg(k);
    if (low) s += QString(" | düşük güven: %1").arg(low);
    counts_->setText(s);
    setStatusFromIR();
  }

 private:
  OverlayView* ov_;
  QComboBox* role_;
  QLabel* counts_;
};

// ===================================================================================================== 4 Ölçüler
class DimsPage : public WizardPageBase {
 public:
  enum Col { CText, CValue, CTol, CKind, CMeasured, CResidual, CParam, CDriving, NCols };
  explicit DimsPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Annotate, "4 Ölçüler", "Tanınan ölçüler, bağlandıkları geometri ve parametre adları. Metni, adı ve sürücü bayrağını düzeltebilirsiniz.") {
    auto* split = new QSplitter(Qt::Vertical, this);
    ov_ = makeOverlay("overlayDims", Stage::Annotate, LayerDefault | LayerDimensions, OverlayTool::Select);
    split->addWidget(ov_);
    table_ = new QTableWidget(0, NCols, split);
    table_->setObjectName("dimTable");
    table_->setHorizontalHeaderLabels({"Metin", "Nominal", "Tolerans", "Tür", "Ölçülen", "Fark", "Parametre", "Sürücü"});
    table_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(table_);
    lay_->addWidget(split, 1);
    addFooter();
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) { onChanged(it); });
    connect(table_, &QTableWidget::currentCellChanged, this, [this](int r, int, int, int) {
      if (r < 0 || r >= (int)dimIds_.size()) return;
      if (const Dimension* d = findDimension(ir().annot, dimIds_[r])) {
        std::vector<int> ids = d->geomPrimIds;
        ids.push_back(d->id);
        ov_->highlight(ids);
      }
    });
  }
  void refresh() override {
    refreshOverlays();
    filling_ = true;
    table_->setRowCount(0);
    dimIds_.clear();
    // texts bound directly by the axisymmetric-section interpreter (R2): shown below, not twice
    std::set<int> r2Texts;
    for (auto& c : ir().sketches)
      for (auto& [k, v] : c.attrs)
        if (k.rfind("r2.constraintText.", 0) == 0 || k.rfind("r2.referenceText.", 0) == 0) r2Texts.insert(std::atoi(v.c_str()));
    for (auto& d : ir().annot.dims) {
      if (d.textId >= 0 && r2Texts.count(d.textId)) continue;
      int r = table_->rowCount();
      table_->insertRow(r);
      dimIds_.push_back(d.id);
      table_->setItem(r, CText, new QTableWidgetItem(qs(d.text.raw)));
      table_->setItem(r, CValue, roItem(num(d.text.nominal, 6)));
      QString tol;
      if (d.text.tolPlus || d.text.tolMinus) tol = QString("%1/%2").arg(d.text.tolPlus ? num(*d.text.tolPlus) : "0").arg(d.text.tolMinus ? num(*d.text.tolMinus) : "0");
      table_->setItem(r, CTol, roItem(tol));
      table_->setItem(r, CKind, roItem(toString(d.kind)));
      table_->setItem(r, CMeasured, roItem(num(d.measuredPart, 6)));
      auto* res = roItem(num(d.residual, 3));
      if (d.outOfBand) res->setForeground(Qt::red);
      table_->setItem(r, CResidual, res);
      table_->setItem(r, CParam, new QTableWidgetItem(qs(paramOf(d.id))));
      auto* drv = new QTableWidgetItem;
      drv->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
      drv->setCheckState(d.driving ? Qt::Checked : Qt::Unchecked);
      table_->setItem(r, CDriving, drv);
      if (d.conf < kConfHeuristic) table_->item(r, CText)->setBackground(QColor(255, 200, 200));
      else if (d.conf < kConfExact) table_->item(r, CText)->setBackground(QColor(255, 245, 190));
    }
    // R2: dimensions of an axisymmetric SECTION bound by the section interpreter (text -> profile constraint), and
    // the diameters it took from the transverse views / notes. Read-only here (renaming: page 5).
    auto quoted = [](const std::string& d) {
      auto a = d.find('"'), b = a == std::string::npos ? a : d.find('"', a + 1);
      return b == std::string::npos ? std::string() : d.substr(a + 1, b - a - 1);
    };
    auto addRow = [&](const std::string& text, double v, const QString& kind, const std::string& name, bool driving) {
      int r = table_->rowCount();
      table_->insertRow(r);
      dimIds_.push_back(-1);
      table_->setItem(r, CText, roItem(qs(text)));
      table_->setItem(r, CValue, roItem(num(v, 6)));
      table_->setItem(r, CTol, roItem(""));
      table_->setItem(r, CKind, roItem(kind));
      table_->setItem(r, CMeasured, roItem(""));
      table_->setItem(r, CResidual, roItem(""));
      table_->setItem(r, CParam, roItem(qs(name)));
      auto* drv = roItem("");
      drv->setCheckState(driving ? Qt::Checked : Qt::Unchecked);
      table_->setItem(r, CDriving, drv);
    };
    std::set<std::string> shown;
    for (auto& c : ir().sketches) {
      if (c.purpose != "revolve-half-profile-part") continue;
      for (auto& p : c.params) {
        if (p.tag == ParamTag::Measured || p.tag == ParamTag::Assumed || !shown.insert(p.param.name).second) continue;
        const std::string t = quoted(p.param.description);
        if (t.empty()) continue;
        const char k0 = p.param.name.empty() ? '?' : p.param.name[0];
        const QString kind = QString(k0 == 'D' ? "Çap" : k0 == 'A' ? "Açı" : k0 == 'C' ? "Pah" : "Eksenel") +
                             (p.param.description.find("kesit dışı") != std::string::npos ? " (enine görünüş/not)" : " (kesit, R2)");
        addRow(t, p.nominalValue, kind, p.param.name, p.tag == ParamTag::FromDimension);
      }
    }
    // R3: slot / groove values from the transverse views and notes ([D] parameters of the chosen candidate)
    if (const SynthesisCandidate* c = wiz_->chosen())
      for (auto& p : c->fragment.params.all()) {
        if (p.group != "Kesit özellikleri (R3)" || p.description.rfind("[D]", 0) != 0 || !shown.insert(p.name).second) continue;
        double v = 0;
        try { v = std::stod(p.expression); } catch (...) { continue; }
        addRow(p.description.substr(4), v, "Kanal/oluk (R3)", p.name, true);
      }
    filling_ = false;
    setStatusFromIR();
  }

 private:
  std::string paramOf(int dimId) {
    for (auto& c : ir().sketches) for (auto& p : c.params) if (p.dimensionId == dimId) return p.param.name;
    return {};
  }
  void onChanged(QTableWidgetItem* it) {
    if (filling_) return;
    int r = it->row();
    if (r < 0 || r >= (int)dimIds_.size() || dimIds_[r] < 0) return;
    const int id = dimIds_[r];
    if (it->column() == CText) {
      wiz_->addEdit(makeEdit(Stage::Annotate, edit_op::setDimText, {{"dimId", id}, {"utf8", ss(it->text())}}), "dimId");
    } else if (it->column() == CDriving) {
      wiz_->addEdit(makeEdit(Stage::Annotate, edit_op::setDriving, {{"dimId", id}, {"driving", it->checkState() == Qt::Checked}}), "dimId");
    } else if (it->column() == CParam) {
      std::string from = paramOf(id), to = ss(it->text().trimmed());
      int sk = -1;
      for (auto& c : ir().sketches) for (auto& p : c.params) if (p.dimensionId == id) sk = c.id;
      if (!from.empty() && from != to) wiz_->addEdit(makeEdit(Stage::Sketch, edit_op::setParamName, {{"sketchId", sk}, {"from", from}, {"to", to}}));
    }
  }
  OverlayView* ov_;
  QTableWidget* table_;
  std::vector<int> dimIds_;
  bool filling_ = false;
};

// ===================================================================================================== 5 Sketch
class SketchPage : public WizardPageBase {
 public:
  explicit SketchPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Sketch, "5 Sketch", "Görünüş başına sketch önerisi: serbestlik derecesi (DOF), çelişkiler ve çizime göre sapma.") {
    combo_ = new QComboBox(this);
    combo_->setObjectName("sketchCombo");
    lay_->addWidget(combo_);
    auto* split = new QSplitter(this);
    ov_ = makeOverlay("overlaySketch", Stage::Sketch, LayerDefault | LayerSketch, OverlayTool::Select);
    split->addWidget(ov_);
    params_ = new QTableWidget(0, 4, split);
    params_->setObjectName("paramTable");
    params_->setHorizontalHeaderLabels({"Ad", "İfade", "Etiket", "Açıklama"});
    params_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(params_);
    lay_->addWidget(split, 1);
    dof_ = new QLabel(this);
    dof_->setObjectName("dofLabel");
    lay_->addWidget(dof_);
    auto* h = new QHBoxLayout;
    auto* close = new QPushButton("Kalan serbestlikleri ölçülen değerle kapat", this);
    close->setObjectName("closeDofButton");
    auto* reextract = new QPushButton("Kısıtları yeniden çıkar", this);
    reextract->setObjectName("reextractButton");
    edit_ = new QPushButton("Sketch editörünü aç…", this);
    edit_->setObjectName("editSketchButton");
    edit_->setEnabled(bool(wiz_->state().host.editSketch));
    h->addWidget(close); h->addWidget(reextract); h->addWidget(edit_); h->addStretch(1);
    lay_->addLayout(h);
    addFooter();
    connect(combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { showCurrent(); });
    connect(close, &QPushButton::clicked, this, [this] {
      if (auto* c = current()) wiz_->addEdit(makeEdit(Stage::Sketch, edit_op::closeDofMeasured, {{"sketchId", c->id}}));
    });
    connect(reextract, &QPushButton::clicked, this, [this] {
      auto& E = ir().edits;   // drop constraint-level edits of this stage, keep names / value modes
      E.erase(std::remove_if(E.begin(), E.end(), [](const UserEdit& e) {
                return e.op == edit_op::deleteConstraint || e.op == edit_op::replaceSketch || e.op == edit_op::closeDofMeasured;
              }), E.end());
      wiz_->rerunFrom(Stage::Sketch);
    });
    connect(edit_, &QPushButton::clicked, this, [this] {
      auto* c = current();
      auto& host = wiz_->state().host;
      if (!c || !host.editSketch) return;
      pc::Sketch sk = c->sketch;
      if (!host.editSketch(this, sk)) return;
      wiz_->addEdit(makeEdit(Stage::Sketch, edit_op::replaceSketch, {{"sketchId", c->id}, {"sketch", pc::toJson(sk)}}));
    });
    connect(params_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
      if (filling_ || it->column() != 0) return;
      auto* c = current();
      int r = it->row();
      if (!c || r >= (int)c->params.size()) return;
      std::string from = c->params[r].param.name, to = ss(it->text().trimmed());
      if (from != to) wiz_->addEdit(makeEdit(Stage::Sketch, edit_op::setParamName, {{"sketchId", c->id}, {"from", from}, {"to", to}}));
    });
  }
  void refresh() override {
    refreshOverlays();
    int keep = combo_->currentIndex();
    QSignalBlocker b(combo_);
    combo_->clear();
    for (auto& c : ir().sketches) {
      const ViewRegion* v = findView(ir().layout, c.viewId);
      combo_->addItem(QString("%1 — %2 (DOF %3)").arg(qs(c.purpose)).arg(v ? labelTr(v->role) : "-").arg(c.dof), c.id);
    }
    combo_->setCurrentIndex(std::clamp(keep, 0, std::max(0, combo_->count() - 1)));
    showCurrent();
    setStatusFromIR();
  }

 private:
  const SketchCandidate* current() {
    int id = combo_->currentData().toInt();
    for (auto& c : ir().sketches) if (c.id == id) return &c;
    return nullptr;
  }
  void showCurrent() {
    filling_ = true;
    params_->setRowCount(0);
    const SketchCandidate* c = current();
    ov_->setCurrentSketch(c ? c->id : -1);
    if (!c) { dof_->setText("Sketch yok"); filling_ = false; return; }
    for (auto& p : c->params) {
      int r = params_->rowCount();
      params_->insertRow(r);
      params_->setItem(r, 0, new QTableWidgetItem(qs(p.param.name)));
      params_->setItem(r, 1, roItem(qs(p.param.expression)));
      params_->setItem(r, 2, roItem(tagLetter(p.tag)));
      params_->setItem(r, 3, roItem(qs(p.param.description)));
    }
    QString s = QString("DOF: %1 | kapalı profil: %2 | çelişki: %3 | en büyük sapma: %4 mm")
                    .arg(c->dof).arg(c->profileClosed ? "evet" : "HAYIR").arg(c->conflicting.size()).arg(num(c->maxGeomDeviation, 3));
    if (c->dof > 0) s = "<span style='color:#b00'>" + s + "</span>";
    dof_->setText(s);
    filling_ = false;
  }
  QComboBox* combo_;
  OverlayView* ov_;
  QTableWidget* params_;
  QLabel* dof_;
  QPushButton* edit_;
  bool filling_ = false;
};

// ===================================================================================================== 6 3B
class SolidPage : public WizardPageBase {
 public:
  explicit SolidPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Synthesize, "6 3B", "Önerilen 3B yöntemleri (puan ve açıklama). Seçilen aday önizlenir.") {
    auto* split = new QSplitter(this);
    auto* left = new QWidget(split);
    auto* v = new QVBoxLayout(left);
    list_ = new QListWidget(left);
    list_->setObjectName("strategyList");
    list_->setWordWrap(true);
    v->addWidget(list_, 1);
    sketchOnly_ = new QCheckBox("Yalnız sketch", left);
    sketchOnly_->setObjectName("sketchOnlyCheck");
    v->addWidget(sketchOnly_);
    info_ = new QLabel(left);
    info_->setObjectName("candidateLabel");
    info_->setWordWrap(true);
    v->addWidget(info_);
    split->addWidget(left);
    // QOpenGLWidget is not available on the offscreen/minimal QPA (headless tests): text placeholder instead.
    const QString qpa = QGuiApplication::platformName();
    if (qpa != "offscreen" && qpa != "minimal") {
      viewer_ = new pc::viewer::ShapeViewer(split);
      viewer_->setObjectName("preview");
      viewer_->setMinimumSize(320, 260);
      split->addWidget(viewer_);
    } else {
      auto* ph = new QLabel("3B önizleme bu platformda yok (OpenGL)", split);
      ph->setObjectName("previewPlaceholder");
      ph->setAlignment(Qt::AlignCenter);
      split->addWidget(ph);
    }
    lay_->addWidget(split, 1);
    addFooter();
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
      if (filling_ || row < 0 || row >= (int)ir().synthesis.size()) return;
      choose(ir().synthesis[row].id);
    });
    connect(sketchOnly_, &QCheckBox::toggled, this, [this](bool on) {
      if (filling_) return;
      for (size_t i = 0; i < ir().synthesis.size(); ++i)
        if (on ? ir().synthesis[i].strategy == Strategy::SketchOnly : i == 0) { list_->setCurrentRow((int)i); break; }
    });
  }
  void refresh() override {
    filling_ = true;
    list_->clear();
    int cur = 0;
    const SynthesisCandidate* ch = wiz_->chosen();
    for (size_t i = 0; i < ir().synthesis.size(); ++i) {
      const auto& c = ir().synthesis[i];
      list_->addItem(QString("%1 — puan %2\n%3").arg(labelTr(c.strategy)).arg(num(c.score, 2)).arg(qs(c.explanation)));
      if (ch && c.id == ch->id) cur = (int)i;
    }
    list_->setCurrentRow(cur);
    sketchOnly_->setChecked(ch && ch->strategy == Strategy::SketchOnly);
    filling_ = false;
    showCandidate();
    setStatusFromIR();
  }

 private:
  void choose(int id) {
    auto& E = ir().edits;
    E.erase(std::remove_if(E.begin(), E.end(), [](const UserEdit& e) { return e.op == edit_op::chooseStrategy; }), E.end());
    E.push_back(makeEdit(Stage::Synthesize, edit_op::chooseStrategy, {{"candidateId", id}}));
    ir().chosenSynthesis = id;   // cheap: no re-run needed for a choice
    filling_ = true;
    for (size_t i = 0; i < ir().synthesis.size(); ++i)
      if (ir().synthesis[i].id == id) sketchOnly_->setChecked(ir().synthesis[i].strategy == Strategy::SketchOnly);
    filling_ = false;
    showCandidate();
  }
  void showCandidate() {
    const SynthesisCandidate* c = wiz_->chosen();
    if (!c) { info_->setText("Aday yok"); if (viewer_) viewer_->clearModel(); return; }
    QString s = QString("%1: %2 özellik, %3 parametre").arg(labelTr(c->strategy)).arg(c->fragment.features.size()).arg(c->fragment.params.all().size());
    if (c->volume > 0) s += QString(", hacim %1 mm³").arg(num(c->volume, 7));
    if (c->reprojectionError >= 0) s += QString(", yeniden izdüşüm hatası %1 mm").arg(num(c->reprojectionError, 3));
    for (auto& w : c->warnings) s += "\n⚠ " + qs(w);
    info_->setText(s);
    if (!viewer_) return;
    if (c->strategy == Strategy::SketchOnly || c->fragment.features.empty()) { viewer_->clearModel(); return; }
    auto mr = pc::model::regenerate(c->fragment);
    if (mr.shape.IsNull()) viewer_->clearModel();
    else viewer_->setModel(mr.shape, mr.faces, mr.edges, false);
  }
  QListWidget* list_;
  QCheckBox* sketchOnly_;
  QLabel* info_;
  pc::viewer::ShapeViewer* viewer_ = nullptr;
  bool filling_ = false;
};

// ===================================================================================================== 7 Özet
class SummaryPage : public WizardPageBase {
 public:
  explicit SummaryPage(ImportWizard* w)
      : WizardPageBase(w, Stage::Synthesize, "7 Özet", "Parametreler ([D] çizim ölçüsü, [Ö] ölçülen, [V] varsayım, [T] türetilmiş) ve uyarılar.") {
    params_ = new QTableWidget(0, 4, this);
    params_->setObjectName("summaryParams");
    params_->setHorizontalHeaderLabels({"Ad", "İfade", "Etiket", "Açıklama"});
    params_->horizontalHeader()->setStretchLastSection(true);
    lay_->addWidget(params_, 2);
    warn_ = new QListWidget(this);
    warn_->setObjectName("warningList");
    lay_->addWidget(warn_, 1);
    merge_ = new QRadioButton("Mevcut belgeye ekle", this);
    merge_->setObjectName("mergeRadio");
    merge_->setChecked(true);
    fresh_ = new QRadioButton("Yeni belge", this);
    fresh_->setObjectName("newDocRadio");
    fresh_->setEnabled(bool(wiz_->state().host.openAsNewDocument));
    auto* h = new QHBoxLayout;
    h->addWidget(merge_); h->addWidget(fresh_); h->addStretch(1);
    lay_->addLayout(h);
  }
  bool newDocument() const { return fresh_->isChecked(); }
  void refresh() override {
    params_->setRowCount(0);
    warn_->clear();
    const SynthesisCandidate* c = wiz_->chosen();
    if (c) {
      for (auto& p : c->fragment.params.all()) {
        const ParamProposal* pp = nullptr;
        for (auto& s : ir().sketches) for (auto& q : s.params) if (q.param.name == p.name) pp = &q;
        int r = params_->rowCount();
        params_->insertRow(r);
        params_->setItem(r, 0, roItem(qs(p.name)));
        params_->setItem(r, 1, roItem(qs(p.expression)));
        params_->setItem(r, 2, roItem(pp ? tagLetter(pp->tag) : "[T]"));
        params_->setItem(r, 3, roItem(qs(p.description)));
      }
      for (auto& w : c->warnings) warn_->addItem(qs(w));
    }
    for (auto& d : ir().diagnostics)
      if (d.severity != Severity::Info) warn_->addItem(QString("%1 %2: %3").arg(labelTr(static_cast<Stage>(std::max(1, d.stage)))).arg(qs(d.code)).arg(qs(d.message)));
  }

 private:
  QTableWidget* params_;
  QListWidget* warn_;
  QRadioButton *merge_, *fresh_;
};

// ===================================================================================================== wizard
ImportWizard::ImportWizard(QWidget* parent, const DrawingImportHost& host, const QString& initialFile) : QWizard(parent) {
  setObjectName("drawingImportWizard");
  setWindowTitle("Teknik resimden içe aktar");
  setWizardStyle(QWizard::ClassicStyle);
  setOption(QWizard::NoBackButtonOnStartPage, true);
  setButtonText(QWizard::NextButton, "İleri >");
  setButtonText(QWizard::BackButton, "< Geri");
  setButtonText(QWizard::FinishButton, "Bitir");
  setButtonText(QWizard::CancelButton, "İptal");
  st_.host = host;
  st_.opts.ingest.tessdataDir = ss(host.tessdataDir);
  if (st_.opts.ingest.tessdataDir.empty()) {
    QString d = QApplication::applicationDirPath() + "/../share/persistcad/tessdata";
    if (QFileInfo(d + "/eng.traineddata").exists()) st_.opts.ingest.tessdataDir = ss(QFileInfo(d).absoluteFilePath());
  }
  st_.opts.ingest.pdfRasterizer = qt::makePdfRasterizer();
  pages_ = {new SourcePage(this, initialFile), new LayoutPage(this), new GeometryPage(this), new DimsPage(this),
            new SketchPage(this), new SolidPage(this), new SummaryPage(this)};
  for (int i = 0; i < (int)pages_.size(); ++i) setPage(i, pages_[i]);
  resize(1100, 760);
}

ImportWizard::~ImportWizard() = default;

bool ImportWizard::rerunFrom(Stage from) {
  if (st_.path.empty()) return false;
  if (st_.ir.lastStage < static_cast<int>(from) - 1) from = Stage::Ingest;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  st_.last = runPipeline(st_.path, st_.ir, from, Stage::Synthesize, st_.opts);
  QApplication::restoreOverrideCursor();
  refreshPages();
  return st_.ir.lastStage >= static_cast<int>(Stage::Layout);
}

void ImportWizard::addEdit(const UserEdit& e, const std::string& replaceKey) {
  auto& E = st_.ir.edits;
  if (!replaceKey.empty()) {
    nlohmann::json nj = nlohmann::json::parse(e.payload, nullptr, false);
    E.erase(std::remove_if(E.begin(), E.end(), [&](const UserEdit& o) {
              if (o.op != e.op) return false;
              if (replaceKey == "*") return true;
              nlohmann::json oj = nlohmann::json::parse(o.payload, nullptr, false);
              return oj.is_object() && nj.is_object() && oj.value(replaceKey, nlohmann::json()) == nj.value(replaceKey, nlohmann::json());
            }), E.end());
  }
  E.push_back(e);
  rerunFrom(e.stage);
}

const SynthesisCandidate* ImportWizard::chosen() const {
  for (auto& s : st_.ir.synthesis) if (s.id == st_.ir.chosenSynthesis) return &s;
  return st_.ir.synthesis.empty() ? nullptr : &st_.ir.synthesis.front();
}

void ImportWizard::refreshPages() {
  for (auto* p : pages_) p->refresh();
}

void ImportWizard::accept() {
  const SynthesisCandidate* c = chosen();
  if (!c) { QMessageBox::warning(this, windowTitle(), "Eklenecek aday yok."); return; }
  auto* summary = static_cast<SummaryPage*>(pages_[PSummary]);
  std::string err;
  const QString undoText = "Teknik resimden içe aktar";
  if (summary->newDocument() && st_.host.openAsNewDocument) {
    pc::Document fresh;
    auto it = st_.ir.layout.title.fields.find("title");
    fresh.name = it != st_.ir.layout.title.fields.end() && !it->second.empty() ? it->second : ss(QFileInfo(qs(st_.path)).baseName());
    if (mergeIntoDocument(*c, fresh, &err).empty()) { QMessageBox::warning(this, windowTitle(), "Birleştirme başarısız: " + qs(err)); return; }
    st_.host.openAsNewDocument(fresh);
  } else {
    pc::Document doc = st_.host.currentDocument ? st_.host.currentDocument() : pc::Document();
    if (mergeIntoDocument(*c, doc, &err).empty()) { QMessageBox::warning(this, windowTitle(), "Birleştirme başarısız: " + qs(err)); return; }
    if (st_.host.commit) st_.host.commit(doc, undoText);   // ONE undo step
  }
  // side-car IR next to the drawing (not when the input already is one)
  if (!QString::fromStdString(st_.path).endsWith(".json", Qt::CaseInsensitive)) {
    st_.ir.chosenSynthesis = c->id;
    saveIR(st_.ir, st_.path + kIRFileSuffix);
  }
  QWizard::accept();
}

// ===================================================================================================== entry points
int runDrawingImportWizard(QWidget* parent, const DrawingImportHost& host, const QString& initialFile) {
  ImportWizard w(parent, host, initialFile);
  w.setModal(true);
  return w.exec();
}

QAction* installDrawingImportAction(QMainWindow* mw, QMenu* fileMenu, const DrawingImportHost& host) {
  auto* a = new QAction("Teknik resimden içe aktar…", mw);
  a->setObjectName("actionDrawingImport");
  a->setToolTip("Teknik resimden (DXF, PDF, tarama) sketch ve 3B model oluştur");
  QObject::connect(a, &QAction::triggered, mw, [mw, host] { runDrawingImportWizard(mw, host); });
  if (fileMenu) fileMenu->addAction(a);
  return a;
}

}  // namespace pc::drawing::ui
