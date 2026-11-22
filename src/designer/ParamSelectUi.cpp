// "Seçimden parametre" — badge, quick popup, menus, Properties rows. See ParamSelectUi.h.
#include "ParamSelectUi.h"
#include "ExprEdit.h"
#include "Icons.h"
#include "SketchEditor.h"
#include "pc/Expression.h"
#include "pc/ShapeViewer.h"
#include <BRepAdaptor_Curve.hxx>
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

using namespace pc;

namespace pcd {

namespace {
QString fmtV(double v) { return QString::fromStdString(formatNumber(std::round(v * 1e4) / 1e4)); }
bool isPlainNumber(const QString& s, double* v = nullptr) {
  bool ok = false;
  const double d = s.trimmed().toDouble(&ok);
  if (ok && v) *v = d;
  return ok;
}
const char* kPopupStyle =
    "QFrame#quickParamPopup { background: #fbfcfe; border: 1px solid #3d7bd9; border-radius: 5px; }"
    "QLabel#quickParam\\.title { font-weight: bold; color: #1d4f91; }"
    "QLabel#quickParam\\.error { color: #c0392b; }"
    "QLabel#quickParam\\.live { color: #5f6b76; }";
const char* kBadgeStyle =
    "QFrame#paramBadge { background: rgba(255,255,255,238); border: 1px solid #3d7bd9; border-radius: 4px; }"
    "QLabel { color: #1f2d3d; }"
    "QToolButton#paramBadge\\.P, QToolButton[pbP=\"true\"] { background: #2f6fd0; color: white; font-weight: bold;"
    " border: none; border-radius: 3px; min-width: 18px; min-height: 18px; padding: 0 4px; }"
    "QToolButton#paramBadge\\.P:disabled, QToolButton[pbP=\"true\"]:disabled { background: #b8c2cf; }"
    "QToolButton#paramBadge\\.toggle, QToolButton#paramBadge\\.bind { border: 1px solid #9fb6d8; border-radius: 3px;"
    " padding: 0 3px; background: #eef3fb; }";
} // namespace

// ============================================================================================ QuickParamPopup
QuickParamPopup::QuickParamPopup(QWidget* parent) : QFrame(parent) {
  setObjectName("quickParamPopup");
  setFrameShape(QFrame::StyledPanel);
  setAutoFillBackground(true);
  setStyleSheet(kPopupStyle);
  setFocusPolicy(Qt::StrongFocus);
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(10, 8, 10, 8);
  v->setSpacing(6);
  title_ = new QLabel(this);
  title_->setObjectName("quickParam.title");
  v->addWidget(title_);
  auto* f = new QFormLayout;
  f->setContentsMargins(0, 0, 0, 0);
  f->setHorizontalSpacing(8);
  f->setVerticalSpacing(4);
  measure_ = new QComboBox(this);
  measure_->setObjectName("quickParam.measure");
  measure_->setToolTip("Hangi ölçü parametre olsun (ör. çap / yarıçap)");
  f->addRow("Ölçü", measure_);
  target_ = new QComboBox(this);
  target_->setObjectName("quickParam.target");
  target_->setToolTip("Yeni parametre oluştur ya da ölçüyü mevcut bir parametreye bağla");
  f->addRow("Bağla", target_);
  name_ = new QLineEdit(this);
  name_->setObjectName("quickParam.name");
  name_->setToolTip("Parametre adı (harf ile başlar; Gruplar panelinde görünür)");
  f->addRow("Ad", name_);
  auto* vr = new QHBoxLayout;
  vr->setSpacing(6);
  value_ = new QLineEdit(this);
  value_->setObjectName("quickParam.value");
  value_->setToolTip("Değer: sayı ya da ifade (ör. W/2 + 3). Ölçü bu değere sürülür.");
  unit_ = new QLabel(this);
  unit_->setObjectName("quickParam.unit");
  live_ = new QLabel(this);
  live_->setObjectName("quickParam.live");
  vr->addWidget(value_, 1);
  vr->addWidget(unit_);
  vr->addWidget(live_);
  f->addRow("Değer", vr);
  auto* lr = new QHBoxLayout;
  lr->setSpacing(6);
  limits_ = new QCheckBox("Min/Maks", this);
  limits_->setObjectName("quickParam.limits");
  limits_->setToolTip("Parametreye alt / üst sınır ver (öneri: ±%50). Sınır dışı değerler reddedilir.");
  min_ = new QLineEdit(this);
  min_->setObjectName("quickParam.min");
  max_ = new QLineEdit(this);
  max_->setObjectName("quickParam.max");
  for (QLineEdit* e : {min_, max_}) { e->setMaximumWidth(70); e->setEnabled(false); }
  lr->addWidget(limits_);
  lr->addWidget(min_);
  lr->addWidget(new QLabel("–", this));
  lr->addWidget(max_);
  lr->addStretch(1);
  f->addRow("Sınır", lr);
  exposed_ = new QCheckBox("Uygulamada göster", this);
  exposed_->setObjectName("quickParam.exposed");
  exposed_->setToolTip("Uygulama olarak kaydet (Save as App) bu parametreyi girdi olarak listeler");
  f->addRow(QString(), exposed_);
  v->addLayout(f);
  err_ = new QLabel(this);
  err_->setObjectName("quickParam.error");
  err_->setWordWrap(true);
  err_->hide();
  v->addWidget(err_);
  auto* b = new QHBoxLayout;
  b->addStretch(1);
  cancel_ = new QPushButton("İptal", this);
  cancel_->setObjectName("quickParam.cancel");
  ok_ = new QPushButton("Oluştur", this);
  ok_->setObjectName("quickParam.ok");
  ok_->setDefault(true);
  ok_->setToolTip("Parametreyi oluştur ve ölçüyü ona bağla (Enter)");
  b->addWidget(cancel_);
  b->addWidget(ok_);
  v->addLayout(b);
  connect(limits_, &QCheckBox::toggled, this, [this](bool on) { min_->setEnabled(on); max_->setEnabled(on); });
  connect(measure_, qOverload<int>(&QComboBox::activated), this, [this](int i) { onMeasure(measure_->itemData(i).toInt()); });
  connect(target_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) { onTarget(i); });
  connect(value_, &QLineEdit::textChanged, this, [this](const QString& t) {
    double d = 0;
    QString e;
    if (isPlainNumber(t)) live_->clear();
    else if (eval_ && eval_(t, &d, &e)) live_->setText("= " + fmtV(d));
    else live_->setText("?");
  });
  for (QLineEdit* e : {name_, value_, min_, max_}) connect(e, &QLineEdit::returnPressed, this, [this] { tryAccept(); });
  connect(ok_, &QPushButton::clicked, this, [this] { tryAccept(); });
  connect(cancel_, &QPushButton::clicked, this, [this] { emit cancelled(); });
}

void QuickParamPopup::setup(const QList<psel::Measure>& ms, int index, const Document& ctx, const QString& bindTo, Evaluator eval) {
  ms_ = ms;
  ctx_ = &ctx;
  eval_ = std::move(eval);
  measure_->blockSignals(true);
  measure_->clear();
  for (int i = 0; i < ms.size(); ++i)
    if (ms[i].canParam) measure_->addItem(ms[i].label + "  (" + ms[i].valueText() + ")", i);
  measure_->setCurrentIndex(std::max(0, measure_->findData(index)));
  measure_->setVisible(measure_->count() > 1);
  if (auto* f = qobject_cast<QFormLayout*>(static_cast<QVBoxLayout*>(layout())->itemAt(1)->layout()))
    f->setRowVisible(measure_, measure_->count() > 1);
  measure_->blockSignals(false);
  exposed_->setChecked(true);
  limits_->setChecked(false);
  onMeasure(index);
  if (!bindTo.isEmpty()) {
    int k = bindTo == "*" ? (target_->count() > 1 ? 1 : 0) : target_->findData(bindTo);
    if (k >= 0) target_->setCurrentIndex(k);
  }
  showError(QString());
  adjustSize();
}

void QuickParamPopup::onMeasure(int i) {
  if (i < 0 || i >= ms_.size()) return;
  const psel::Measure& m = ms_[i];
  title_->setText(QString("Parametre yap — %1%2").arg(m.label, m.where.isEmpty() ? QString() : " (" + m.where + ")"));
  name_->setText(psel::suggestName(*ctx_, m));
  // the current expression is kept when it is a formula ("W/2" -> the new parameter = W/2), else the value
  const QString cur = m.currentExpr;
  double d = 0;
  value_->setText(!cur.isEmpty() && !isPlainNumber(cur, &d) && m.boundParam.isEmpty() ? cur : fmtV(m.value));
  unit_->setText(m.unit());
  const double a = std::fabs(m.value);
  min_->setText(fmtV(m.kind == ParamKind::Angle ? m.value - 0.5 * a : std::max(0.0, m.value - 0.5 * a)));
  max_->setText(fmtV(m.value + 0.5 * a));
  // existing parameters of the same unit kind
  target_->blockSignals(true);
  target_->clear();
  target_->addItem("Yeni parametre", QString());
  if (ctx_) {
    ParamValues vals;
    try { vals = ctx_->evaluateParams(); } catch (...) {}
    for (const auto& p : ctx_->params.all()) {
      if (p.kind != m.kind) continue;
      auto it = vals.find(p.name);
      target_->addItem(QString("%1 = %2 %3").arg(QString::fromStdString(p.name), it != vals.end() ? fmtV(it->second) : QString("?"),
                                                 paramUnitText(p.kind)),
                       QString::fromStdString(p.name));
    }
  }
  if (!m.boundParam.isEmpty()) {
    const int k = target_->findData(m.boundParam);
    if (k > 0) target_->setItemText(k, target_->itemText(k) + "  (şu an bağlı)");
  }
  target_->setCurrentIndex(0);
  target_->blockSignals(false);
  onTarget(0);
  name_->setFocus();
  name_->selectAll();
}

void QuickParamPopup::onTarget(int i) {
  const bool fresh = i <= 0;
  for (QWidget* w : {static_cast<QWidget*>(name_), static_cast<QWidget*>(value_), static_cast<QWidget*>(limits_),
                     static_cast<QWidget*>(exposed_)})
    w->setEnabled(fresh);
  min_->setEnabled(fresh && limits_->isChecked());
  max_->setEnabled(fresh && limits_->isChecked());
  ok_->setText(fresh ? "Oluştur" : "Bağla");
  if (!fresh) {
    const QString n = target_->itemData(i).toString();
    name_->setText(n);
    if (ctx_) if (const Parameter* p = ctx_->params.find(n.toStdString())) value_->setText(QString::fromStdString(p->expression));
  }
}

int QuickParamPopup::measureIndex() const {
  const int k = measure_->currentIndex();
  return k >= 0 ? measure_->itemData(k).toInt() : -1;
}
QString QuickParamPopup::bindParam() const { return target_->currentIndex() > 0 ? target_->currentData().toString() : QString(); }

Parameter QuickParamPopup::parameter() const {
  Parameter p;
  p.name = name_->text().trimmed().toStdString();
  p.expression = value_->text().trimmed().toStdString();
  const int i = measureIndex();
  p.kind = i >= 0 && i < ms_.size() ? ms_[i].kind : ParamKind::Length;
  if (limits_->isChecked()) {
    double a = 0, b = 0;
    if (isPlainNumber(min_->text(), &a)) p.minValue = a;
    if (isPlainNumber(max_->text(), &b)) p.maxValue = b;
  }
  p.exposed = exposed_->isChecked();
  if (i >= 0 && i < ms_.size()) p.description = (ms_[i].label + (ms_[i].where.isEmpty() ? QString() : " — " + ms_[i].where)).toStdString();
  return p;
}

void QuickParamPopup::showError(const QString& text) {
  err_->setText(text);
  err_->setVisible(!text.isEmpty());
  adjustSize();
}

void QuickParamPopup::tryAccept() {
  if (!bindParam().isEmpty()) { emit accepted(); return; }
  const QString n = name_->text().trimmed();
  if (ctx_) {
    const QString why = psel::checkName(*ctx_, n);
    if (!why.isEmpty()) { showError(why); name_->setFocus(); return; }
  }
  double v = 0;
  QString e;
  if (!eval_ || !eval_(value_->text().trimmed(), &v, &e)) {
    showError("Değer hesaplanamadı: " + (e.isEmpty() ? QString("geçersiz ifade") : e));
    value_->setFocus();
    return;
  }
  if (limits_->isChecked()) {
    double a = 0, b = 0;
    if (!isPlainNumber(min_->text(), &a) || !isPlainNumber(max_->text(), &b) || a > b || v < a || v > b) {
      showError("Sınırlar geçersiz: min ≤ değer ≤ maks olmalı.");
      return;
    }
  }
  emit accepted();
}

void QuickParamPopup::keyPressEvent(QKeyEvent* e) {
  if (e->key() == Qt::Key_Escape) { emit cancelled(); return; }
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { tryAccept(); return; }
  QFrame::keyPressEvent(e);
}

// ============================================================================================ controller
ParamSelectController::ParamSelectController(IDesignHost* host, Hooks hooks, QObject* parent)
    : QObject(parent), host_(host), hooks_(std::move(hooks)) {
  if (SketchEditor* ed = host_->sketchEditor()) {
    connect(ed, &SketchEditor::selectionChanged, this, &ParamSelectController::refresh);
    connect(ed, &SketchEditor::changed, this, &ParamSelectController::refresh);
    connect(ed, &SketchEditor::toolChanged, this, [this](int) { refresh(); });
    connect(ed, &SketchEditor::contextMenuRequested, this, &ParamSelectController::showSketchContextMenu);
    connect(ed, &SketchEditor::parametersUndone, this, &ParamSelectController::onSketchParamsUndone);
    connect(ed, &SketchEditor::parametersRedone, this, &ParamSelectController::onSketchParamsRedone);
  }
  if (SelectionModel* s = host_->selection()) connect(s, &SelectionModel::changed, this, &ParamSelectController::refresh);
  if (auto* v = host_->viewer()) connect(v, &pc::viewer::ShapeViewer::cameraChanged, this, &ParamSelectController::reposition);
}

bool ParamSelectController::sketchMode() const { return host_->designMode() == DesignMode::Sketch && !host_->openSketchId().empty(); }

bool ParamSelectController::active() const {
  const QString tool = hooks_.activeToolId ? hooks_.activeToolId() : QString("select");
  if (sketchMode()) {
    SketchEditor* ed = host_->sketchEditor();
    return ed && ed->isVisible() && ed->tool() == SketchEditor::Tool::Select && (tool == "sketch.select" || tool == "select");
  }
  return host_->designMode() == DesignMode::ThreeD && tool == "select" && !host_->featureDialogOpen();
}

QString ParamSelectController::signature() const {
  QString s = sketchMode() ? "S:" + QString::fromStdString(host_->openSketchId()) + ":" : QString("3:");
  if (sketchMode()) {
    for (int id : host_->sketchEditor()->selectedIds()) s += QString::number(id) + ",";
  } else {
    for (const SelItem& it : host_->selection()->items()) s += it.name + ",";
  }
  return s + (active() ? "a" : "i");
}

bool ParamSelectController::evaluate(const QString& expr, double* v, QString* err) const {
  return host_->evaluateExpression(expr, v, err);
}

void ParamSelectController::refresh() {
  if (applying_) return;
  QList<psel::Measure> ms;
  if (active()) {
    try {
      if (sketchMode()) {
        SketchEditor* ed = host_->sketchEditor();
        ParamValues vals;
        try { vals = host_->expressionContext()->evaluateParams(); } catch (...) {}
        ms = psel::sketchMeasures(ed->sketch(), vals, ed->selectedIds());
      } else {
        ms = psel::modelMeasures(host_->document(), host_->modelResult(), host_->selection()->items());
      }
    } catch (...) { ms.clear(); }
  }
  ms_ = ms;
  const QString sig = signature();
  if (sig != sig_) {
    sig_ = sig;
    showRadius_ = false;
    if (popup_ && popup_->isVisible()) closePopup(false);
  }
  if (popup_ && popup_->isVisible() && ms_.isEmpty()) closePopup(false);
  rebuildBadge();
  emit measuresChanged();
}

QStringList ParamSelectController::displayKeys() const {
  QStringList keys, all;
  for (const auto& m : ms_) all << m.key;
  bool pairDone = false;
  for (const auto& m : ms_) {
    if (!m.alt.isEmpty() && all.contains(m.alt)) {
      if (pairDone) continue;
      pairDone = true;
      keys << (showRadius_ ? m.alt : m.key);
      continue;
    }
    keys << m.key;
  }
  while (keys.size() > 3) keys.removeLast();
  return keys;
}

int ParamSelectController::primaryIndex() const {
  for (const QString& k : displayKeys())
    for (int i = 0; i < ms_.size(); ++i)
      if (ms_[i].key == k && ms_[i].canParam) return i;
  return -1;
}

void ParamSelectController::rebuildBadge() {
  pc::viewer::ShapeViewer* v = host_->viewer();
  if (badge_) {
    badge_->hide();
    badge_->setObjectName("paramBadge.old");
    badge_->deleteLater();
    badge_ = nullptr;
  }
  if (!v || ms_.isEmpty() || (popup_ && popup_->isVisible())) return;
  auto* b = new QFrame(v);
  b->setObjectName("paramBadge");
  b->setStyleSheet(kBadgeStyle);
  b->setAutoFillBackground(false);
  auto* lay = new QVBoxLayout(b);
  lay->setContentsMargins(6, 4, 6, 4);
  lay->setSpacing(3);
  bool first = true;
  QStringList all;
  for (const auto& m : ms_) all << m.key;
  bool anyParams = !host_->expressionContext()->params.all().empty();
  for (const QString& key : displayKeys()) {
    int idx = -1;
    for (int i = 0; i < ms_.size(); ++i) if (ms_[i].key == key) { idx = i; break; }
    if (idx < 0) continue;
    const psel::Measure& m = ms_[idx];
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    auto* t = new QLabel(m.text() + (m.where.isEmpty() ? QString() : "  · " + m.where), b);
    t->setObjectName(first ? "paramBadge.text" : "paramBadge.text." + m.key);
    row->addWidget(t);
    if (!m.alt.isEmpty() && all.contains(m.alt)) {
      auto* tg = new QToolButton(b);
      tg->setObjectName("paramBadge.toggle");
      tg->setText(QString::fromUtf8("Ø⇄R"));
      tg->setToolTip("Çap / yarıçap arasında geç");
      connect(tg, &QToolButton::clicked, this, [this] {
        showRadius_ = !showRadius_;
        QTimer::singleShot(0, this, [this] { rebuildBadge(); });
      });
      row->addWidget(tg);
    }
    auto* p = new QToolButton(b);
    p->setObjectName(first ? "paramBadge.P" : "paramBadge.P." + m.key);
    p->setProperty("pbP", true);
    p->setProperty("measureIndex", idx);
    p->setText("P");
    p->setEnabled(m.canParam);
    p->setToolTip(m.canParam ? QString("Parametre yap: %1 (%2) — Ctrl+G").arg(m.label, m.valueText()) : m.why);
    connect(p, &QToolButton::clicked, this, [this, idx] { QTimer::singleShot(0, this, [this, idx] { openPopup(idx); }); });
    row->addWidget(p);
    if (first && anyParams && m.canParam) {
      auto* bd = new QToolButton(b);
      bd->setObjectName("paramBadge.bind");
      bd->setText(QString::fromUtf8("⛓"));
      bd->setToolTip("Mevcut bir parametreye bağla");
      connect(bd, &QToolButton::clicked, this, [this, idx] { QTimer::singleShot(0, this, [this, idx] { openPopup(idx, "*"); }); });
      row->addWidget(bd);
    }
    lay->addLayout(row);
    first = false;
  }
  badge_ = b;
  b->adjustSize();
  b->show();
  b->raise();
  reposition();
}

bool ParamSelectController::anchor(QPointF* out) const {
  pc::viewer::ShapeViewer* v = host_->viewer();
  if (!v) return false;
  if (sketchMode()) {
    SketchEditor* ed = host_->sketchEditor();
    QPointF p;
    if (!ed->selectionAnchor(&p)) return false;
    *out = ed->mapTo(v, p.toPoint());
    return true;
  }
  const auto& items = host_->selection()->items();
  if (items.isEmpty()) return false;
  const SelItem& it = items.last();
  if (it.kind == SelItem::Kind::Face) {
    double c[3], n[3];
    if (v->faceCentroid(it.name, c, n)) { *out = v->worldToScreen(c); return true; }
  } else if (it.kind == SelItem::Kind::Edge) {
    auto e = host_->modelResult().edges.find(it.name.toStdString());
    if (e != host_->modelResult().edges.end()) {
      try {
        BRepAdaptor_Curve cv(e->second);
        const gp_Pnt P = cv.Value(0.5 * (cv.FirstParameter() + cv.LastParameter()));
        const double a[3] = {P.X(), P.Y(), P.Z()};
        *out = v->worldToScreen(a);
        return true;
      } catch (...) {}
    }
  }
  const QPoint c = v->mapFromGlobal(QCursor::pos());
  *out = v->rect().contains(c) ? QPointF(c) : QPointF(v->width() / 2.0, v->height() / 2.0);
  return true;
}

void ParamSelectController::reposition() {
  pc::viewer::ShapeViewer* v = host_->viewer();
  QWidget* w = popup_ && popup_->isVisible() ? static_cast<QWidget*>(popup_) : static_cast<QWidget*>(badge_);
  if (!v || !w) return;
  QPointF a;
  if (!anchor(&a)) a = QPointF(v->width() / 2.0, v->height() / 2.0);
  w->adjustSize();
  QPoint p(int(a.x()) + 22, int(a.y()) - w->height() - 14);
  if (p.y() < 4) p.setY(int(a.y()) + 18);
  auto clampIn = [&] {
    p.setX(std::clamp(p.x(), 4, std::max(4, v->width() - w->width() - 4)));
    p.setY(std::clamp(p.y(), 4, std::max(4, v->height() - w->height() - 4)));
  };
  clampIn();
  // keep clear of the viewer's own overlays (sketch mini-toolbar at the top, tool guides at the right edge)
  for (QWidget* c : v->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
    if (c == w || !c->isVisible()) continue;
    const QRect r = c->geometry();
    if (!QRect(p, w->size()).intersects(r)) continue;
    if (c->objectName() == "miniToolbar") p.setY(r.bottom() + 6);
    else if (c->objectName() == "toolGuideBar") p.setX(r.left() - w->width() - 6);
    clampIn();
  }
  w->move(p);
  w->raise();
}

void ParamSelectController::closePopup(bool refreshAfter) {
  if (!popup_) return;
  QuickParamPopup* p = popup_;
  popup_ = nullptr;
  p->hide();
  p->setObjectName("quickParamPopup.old");
  p->deleteLater();
  popupIndex_ = -1;
  if (refreshAfter) refresh();
  else rebuildBadge();
  if (sketchMode()) host_->sketchEditor()->setFocus();
  else if (host_->viewer()) host_->viewer()->setFocus();
}

bool ParamSelectController::openPopup(int index, const QString& bindTo) {
  if (index < 0 || index >= ms_.size() || !ms_[index].canParam || !host_->viewer()) return false;
  if (popup_) closePopup(false);
  auto* p = new QuickParamPopup(host_->viewer());
  popup_ = p;
  popupIndex_ = index;
  p->setup(ms_, index, *host_->expressionContext(), bindTo,
           [this](const QString& e, double* v, QString* err) { return evaluate(e, v, err); });
  connect(p, &QuickParamPopup::cancelled, this, [this] { closePopup(); });
  connect(p, &QuickParamPopup::accepted, this, [this, p] {
    if (popup_ != p) return;
    const int i = p->measureIndex();
    const QString bind = p->bindParam();
    const Parameter prm = p->parameter();
    if (apply(i, prm, bind)) closePopup();
  });
  if (badge_) badge_->hide();
  p->show();
  p->raise();
  reposition();
  p->activateWindow();
  if (bindTo.isEmpty()) { p->nameEdit()->setFocus(); p->nameEdit()->selectAll(); }
  else if (auto* t = p->findChild<QComboBox*>("quickParam.target")) t->setFocus();
  host_->showPrompt("Parametre adı ve değeri: Enter = oluştur, Esc = vazgeç.", 8000);
  return true;
}

bool ParamSelectController::triggerPrimary() {
  if (!active()) return false;
  refresh();
  const int i = primaryIndex();
  if (i < 0) return false;
  return openPopup(i);
}

int ParamSelectController::askConflict(const psel::BindResult& r, bool failed) {
  QMessageBox mb(QMessageBox::Warning, "Parametre — aşırı kısıt",
                 failed ? QString("Ölçü bu değerle sketch'e uygulanamıyor (%1).\nParametre yine de referans olarak (sketch'i sürmeden) "
                                  "oluşturulabilir.").arg(r.message)
                        : QString("Bu ölçü sketch'i aşırı kısıtlıyor (%1).\nÇakışan kısıt: %2\n\n"
                                  "Çakışanı kaldırıp ölçüyü parametreye bağlayabilir ya da parametreyi referans (sürülen) "
                                  "olarak oluşturabilirsiniz.").arg(r.message, r.conflictText),
                 QMessageBox::NoButton, host_->hostWindow());
  mb.setObjectName("paramConflictBox");
  QAbstractButton* rep = nullptr;
  if (!failed) {
    rep = mb.addButton("Çakışanı kaldır ve bağla", QMessageBox::AcceptRole);
    rep->setObjectName("conflict.replace");
  }
  QAbstractButton* ref = mb.addButton("Referans parametre", QMessageBox::ActionRole);
  ref->setObjectName("conflict.reference");
  QAbstractButton* no = mb.addButton("Vazgeç", QMessageBox::RejectRole);
  no->setObjectName("conflict.cancel");
  mb.setDefaultButton(qobject_cast<QPushButton*>(rep ? rep : ref));
  mb.exec();
  if (rep && mb.clickedButton() == rep) return 1;
  if (mb.clickedButton() == ref) return 2;
  return 0;
}

bool ParamSelectController::apply(int index, const Parameter& p0, const QString& bindTo) {
  if (index < 0 || index >= ms_.size()) return false;
  const psel::Measure m = ms_[index];
  if (!m.canParam) { host_->showPrompt(m.why, 6000); return false; }
  const bool isNew = bindTo.isEmpty();
  Parameter p = p0;
  if (!isNew) p.name = bindTo.toStdString();
  const std::string name = p.name;
  QString err;
  struct Guard {
    bool& b;
    explicit Guard(bool& x) : b(x) { b = true; }
    ~Guard() { b = false; }
  } guard(applying_);

  if (sketchMode()) {
    SketchEditor* ed = host_->sketchEditor();
    Document* ctx = host_->expressionContext();
    Document probe = *ctx;
    if (isNew) {
      if (!(err = psel::checkName(probe, QString::fromStdString(name))).isEmpty()) { if (popup_) popup_->showError(err); return false; }
      probe.params.add(p);
    } else if (!probe.params.find(name)) {
      if (popup_) popup_->showError("Parametre bulunamadı: " + bindTo);
      return false;
    }
    ParamValues vals;
    try { vals = probe.evaluateParams(); } catch (const std::exception& e) {
      if (popup_) popup_->showError(QString::fromUtf8(e.what()));
      return false;
    }
    Sketch sk = ed->sketch();
    psel::BindResult r = psel::bindInSketch(sk, vals, m, name);
    bool reference = false;
    if (r.status != psel::BindResult::Status::Ok) {
      const int a = askConflict(r, r.status == psel::BindResult::Status::Failed);
      if (a == 0) return false;
      if (a == 1) {
        sk = ed->sketch();
        r = psel::bindInSketch(sk, vals, m, name, {r.conflicting.front()});
        if (r.status != psel::BindResult::Status::Ok) {
          QMessageBox::warning(host_->hostWindow(), "Parametre", "Çakışan kısıt kaldırıldı ama ölçü yine uygulanamadı: " + r.message);
          return false;
        }
      } else {
        reference = true;
      }
    }
    if (reference && !isNew) return false;   // binding an existing parameter as "reference" changes nothing
    if (isNew) {
      if (!ctx->params.find(name)) ctx->params.add(p);
      commitNewParameter(ed, p);   // MainWindow: "Parametre ekle: <name>" (one document commit, shows up in Gruplar)
    }
    if (!reference) {
      ed->applyExternalEdit(sk, isNew ? std::vector<Parameter>{p} : std::vector<Parameter>{},
                            QString("%1 artık %2 parametresine bağlı (%3).").arg(m.label, QString::fromStdString(name), m.valueText()));
    }
    host_->showPrompt(reference ? QString("Referans parametre %1 oluşturuldu (sketch'i sürmez).").arg(QString::fromStdString(name))
                                : QString("Parametre %1: %2 ona bağlı — Gruplar'da değiştirince sketch güncellenir.")
                                      .arg(QString::fromStdString(name), m.label),
                      8000);
  } else {
    Document cand = host_->document();
    if (isNew) {
      if (!(err = psel::checkName(cand, QString::fromStdString(name))).isEmpty()) { if (popup_) popup_->showError(err); return false; }
      cand.params.add(p);
    } else if (!cand.params.find(name)) {
      return false;
    }
    try { (void)cand.evaluateParams(); } catch (const std::exception& e) {
      if (popup_) popup_->showError(QString::fromUtf8(e.what()));
      return false;
    }
    Document bound = cand;
    psel::BindResult r = psel::bindInDocument(bound, m, name);
    bool reference = false;
    if (r.status != psel::BindResult::Status::Ok) {
      const int a = askConflict(r, r.status == psel::BindResult::Status::Failed);
      if (a == 0) return false;
      if (a == 1) {
        bound = cand;
        r = psel::bindInDocument(bound, m, name, {{r.conflictSketch, {r.conflicting.front()}}});
        if (r.status != psel::BindResult::Status::Ok) {
          QMessageBox::warning(host_->hostWindow(), "Parametre", "Çakışan kısıt kaldırıldı ama ölçü yine uygulanamadı: " + r.message);
          return false;
        }
      } else {
        if (!isNew) return false;
        reference = true;
        bound = cand;
      }
    }
    const QString undoText = isNew ? QString("Parametre (seçimden): %1 = %2").arg(QString::fromStdString(name), QString::fromStdString(p.expression))
                                   : QString("Parametreye bağla: %1 -> %2").arg(m.label, QString::fromStdString(name));
    if (!host_->commitDocument(bound, undoText)) return false;
    host_->showPrompt(reference ? QString("Referans parametre %1 oluşturuldu.").arg(QString::fromStdString(name))
                                : QString("Parametre %1: %2 ona bağlı — Gruplar'da değiştirince model güncellenir.")
                                      .arg(QString::fromStdString(name), m.label),
                      8000);
  }
  if (hooks_.selectParam) hooks_.selectParam(QString::fromStdString(name));
  return true;
}

// ---- sketch-local undo / redo of a step that created parameters
void ParamSelectController::onSketchParamsUndone(const QStringList& names) {
  QUndoStack* u = hooks_.undoStack ? hooks_.undoStack() : nullptr;
  for (const QString& n : names) {
    const Document& doc = host_->document();
    const std::string s = n.toStdString();
    if (!doc.params.find(s)) continue;
    if (psel::documentUsesParameter(doc, s) || host_->sketchEditor()->referencesParameter(s)) continue;   // used elsewhere: keep
    if (u && u->index() > 0 && u->text(u->index() - 1) == "Parametre ekle: " + n) {
      u->undo();
    } else if (hooks_.commitNoValidate) {
      Document cand = doc;
      cand.params.remove(s);
      hooks_.commitNoValidate(cand, "Parametre sil: " + n);
    }
  }
  refresh();
}

void ParamSelectController::onSketchParamsRedone(const std::vector<Parameter>& params) {
  QUndoStack* u = hooks_.undoStack ? hooks_.undoStack() : nullptr;
  for (const Parameter& p : params) {
    if (host_->document().params.find(p.name)) continue;
    const QString n = QString::fromStdString(p.name);
    if (u && u->canRedo() && u->redoText() == "Parametre ekle: " + n) {
      u->redo();
    } else if (hooks_.commitNoValidate) {
      Document cand = host_->document();
      cand.params.add(p);
      hooks_.commitNoValidate(cand, "Parametre ekle: " + n);
    }
  }
}

// ---- menus / Properties
void ParamSelectController::populateContextMenu(QMenu* menu) {
  refresh();
  if (ms_.isEmpty() || !menu) return;
  menu->addSeparator();
  QMenu* add = menu->addMenu(makeIcon("group", 16), "Parametre olarak ekle");
  add->setObjectName("menuAddParam");
  for (int i = 0; i < ms_.size(); ++i) {
    const psel::Measure& m = ms_[i];
    QAction* a = add->addAction(QString("%1 (%2)%3").arg(m.label, m.valueText(), m.boundParam.isEmpty() ? QString() : "  — " + m.boundParam));
    a->setObjectName("addParam." + m.key);
    a->setEnabled(m.canParam);
    if (!m.canParam) a->setToolTip(m.why);
    connect(a, &QAction::triggered, this, [this, i] { QTimer::singleShot(0, this, [this, i] { openPopup(i); }); });
  }
  const int pi = primaryIndex();
  const auto& params = host_->expressionContext()->params.all();
  if (pi >= 0 && !params.empty()) {
    QMenu* bind = menu->addMenu("Parametreye bağla");
    bind->setObjectName("menuBindParam");
    ParamValues vals;
    try { vals = host_->expressionContext()->evaluateParams(); } catch (...) {}
    for (const auto& p : params) {
      if (p.kind != ms_[pi].kind) continue;
      const QString n = QString::fromStdString(p.name);
      auto it = vals.find(p.name);
      QAction* a = bind->addAction(QString("%1 = %2 %3").arg(n, it != vals.end() ? fmtV(it->second) : QString("?"), paramUnitText(p.kind)));
      a->setObjectName("bindParam." + n);
      connect(a, &QAction::triggered, this, [this, pi, n] { QTimer::singleShot(0, this, [this, pi, n] { apply(pi, Parameter(), n); }); });
    }
    bind->addSeparator();
    QAction* nw = bind->addAction("Yeni parametre…");
    nw->setObjectName("bindParam.new");
    connect(nw, &QAction::triggered, this, [this, pi] { QTimer::singleShot(0, this, [this, pi] { openPopup(pi); }); });
  }
}

void ParamSelectController::showSketchContextMenu(const QPoint& globalPos) {
  refresh();
  if (lastMenu_) lastMenu_->close();
  auto* m = new QMenu(host_->hostWindow());
  m->setObjectName("sketchContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  SketchEditor* ed = host_->sketchEditor();
  if (!ms_.isEmpty()) {
    auto* hdr = m->addAction(ms_.first().text());
    hdr->setEnabled(false);
  }
  populateContextMenu(m);
  if (ed && ed->hasSelection()) {
    m->addSeparator();
    QAction* del = m->addAction(makeIcon("delete", 16), "Sil (Del)");
    del->setObjectName("sketchDelete");
    connect(del, &QAction::triggered, ed, [ed] { ed->deleteSelection(); });
  }
  if (m->isEmpty()) { delete m; return; }
  lastMenu_ = m;
  m->popup(globalPos);
}

QList<PropertiesPanel::Hooks::MeasureRow> ParamSelectController::propertyRows() {
  QList<PropertiesPanel::Hooks::MeasureRow> rows;
  for (int i = 0; i < ms_.size(); ++i) {
    const psel::Measure& m = ms_[i];
    PropertiesPanel::Hooks::MeasureRow r;
    r.key = m.key;
    r.label = m.label;
    r.value = m.valueText() + (m.boundParam.isEmpty() ? QString() : "  = " + m.boundParam) + (m.where.isEmpty() ? QString() : "  · " + m.where);
    r.button = m.boundParam.isEmpty() ? "Parametre yap" : "Yeni parametre";
    r.enabled = m.canParam;
    r.tip = m.canParam ? QString("%1 ölçüsünü sürücü bir parametreye bağla (Gruplar)").arg(m.label) : m.why;
    r.fn = [this, i] { openPopup(i); };
    rows << r;
  }
  return rows;
}

} // namespace pcd
