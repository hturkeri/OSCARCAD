// SC-UX (WP-B) — sketch tools as IActiveTool. See SketchToolAdapter.h.
#include "SketchToolAdapter.h"
#include "DimensionBox.h"
#include <QKeyEvent>

namespace pcd {

SketchToolAdapter::SketchToolAdapter(const QString& id, SketchEditor::Tool tool, const QString& title, QObject* parent)
    : ToolBase(parent), id_(id), title_(title), tool_(tool) {}

SketchEditor* SketchToolAdapter::ed() const { return host_ ? host_->sketchEditor() : nullptr; }

void SketchToolAdapter::onActivate() {
  SketchEditor* e = ed();
  if (!e) { setPrompt(title_ + ": sketch düzenleyicisi yok."); return; }
  if (e->tool() != tool_) e->setTool(tool_);
  // the editor may change its tool itself (right click, Esc in a standalone editor, a new session): follow it
  disconnect(msgConn_);
  msgConn_ = connect(e, &SketchEditor::toolChanged, this, [this](int t) {
    if (t == int(tool_)) return;
    static const char* ids[] = {"sketch.select", "sketch.point", "sketch.line", "sketch.rect", "sketch.circle", "sketch.arc", "sketch.trim"};
    if (t >= 0 && t < 7) emit requestTool(QString::fromLatin1(ids[t]));
  });
  IDesignHost* h = host_;
  e->setParameterRequest([h, e](const QString& suggested, pc::ParamKind kind, const QString& value) -> QString {
    if (!h) return QString();
    const QString n = h->createParameterInteractive(suggested, kind, value, e);
    if (!n.isEmpty()) e->reloadParams();
    return n;
  });
  static const QMap<int, QString> hints = {
      {int(SketchEditor::Tool::Select), "Seç: sketch öğelerine tıklayın / sürükleyin (Ctrl = çoklu). Del siler."},
      {int(SketchEditor::Tool::Point), "Nokta: tıklayın ya da U Tab V yazıp Enter."},
      {int(SketchEditor::Tool::Line), "Çizgi (Line): başlangıca tıklayın; uzunluk yazın, Tab açı, Enter. Esc zinciri bitirir."},
      {int(SketchEditor::Tool::Rect), "Dikdörtgen (Rectangle): ilk köşeye tıklayın; genişlik Tab yükseklik, Enter."},
      {int(SketchEditor::Tool::Circle), "Çember (Circle): merkeze tıklayın; çapı yazıp Enter."},
      {int(SketchEditor::Tool::Arc), "Yay (Sweep Arc): merkez, başlangıç (yarıçap yazılabilir), bitiş (açı yazılabilir)."},
      {int(SketchEditor::Tool::Trim), "Kırp (Trim Away): silinecek parçaya tıklayın."}};
  setPrompt(hints.value(int(tool_)));
  // SC23 (O-P0-1): operation state of the drawing tools (ANALYSIS §4.2 "Sketch bugün" column)
  for (auto& c : opConns_) disconnect(c);
  opConns_.clear();
  errReason_.clear();
  stage_ = e->drawStage();
  entitiesAtStart_ = e->sketch().entities.size();
  opConns_ << connect(e, &SketchEditor::drawStageChanged, this, [this](int st) {
    SketchEditor* ed2 = ed();
    const int before = stage_;
    stage_ = st;
    errReason_.clear();
    reportState();
    if (!ed2) return;
    if (before == 0 && st > 0) entitiesAtStart_ = ed2->sketch().entities.size();
    if (before > 0 && st == 0 && ed2->sketch().entities.size() > entitiesAtStart_) {
      const QString what = title_.section(" (", 0, 0);
      emitMomentary(OpState::Completed, QString("%1 oluşturuldu.").arg(what));
    }
    if (st == 0) entitiesAtStart_ = ed2->sketch().entities.size();
  });
  if (DimensionBox* b = e->dimensionBox()) {
    opConns_ << connect(b, &DimensionBox::editingChanged, this, [this](bool) { reportState(); });
    opConns_ << connect(b, &DimensionBox::textEdited, this, [this](int) { errReason_.clear(); reportState(); });
    opConns_ << connect(b, &DimensionBox::invalidCommit, this, [this](int, const QString& why) {
      errReason_ = QString("%1: %2").arg(title_.section(" (", 0, 0), why);
      reportState();
    });
  }
  reportState();
}

void SketchToolAdapter::reportState() {
  SketchEditor* e = ed();
  if (!e || tool_ == SketchEditor::Tool::Select || tool_ == SketchEditor::Tool::Trim) {
    setOperation(e && tool_ == SketchEditor::Tool::Trim ? OpState::AwaitingSelection : OpState::Idle);
    return;
  }
  DimensionBox* b = e->dimensionBox();
  const bool editing = b && b->isEditing();
  const QString value = editing ? b->text(b->editingField()) : QString();
  const QString unit = editing && b->fieldKey(b->editingField()) == "A" ? QStringLiteral("°") : QStringLiteral("mm");
  if (!errReason_.isEmpty()) { setOperation(OpState::Invalid, errReason_, value, unit); return; }
  if (e->drawStage() == 0) { setOperation(OpState::AwaitingSelection); return; }
  if (editing) {
    const bool ok = b->value(b->editingField(), nullptr);
    setOperation(ok ? OpState::Preview : OpState::AwaitingInput, QString(), value, unit);
    return;
  }
  setOperation(OpState::AwaitingInput);
}

void SketchToolAdapter::onDeactivate() {
  for (auto& c : opConns_) disconnect(c);   // SC23
  opConns_.clear();
  errReason_.clear();
  setOperation(OpState::Idle);
  disconnect(msgConn_);
  if (SketchEditor* e = ed()) {
    while (e->cancelStep()) {}
  }
}

QList<ToolOption> SketchToolAdapter::options() const {
  SketchEditor* e = ed();
  QList<ToolOption> o;
  auto boolOpt = [&](const char* key, const char* label, bool v, bool mini = false) {
    ToolOption t;
    t.key = key;
    t.label = QString::fromUtf8(label);
    t.type = ToolOption::Type::Bool;
    t.value = v;
    t.group = "Sketch";
    t.onMiniToolbar = mini;
    o << t;
  };
  if (tool_ == SketchEditor::Tool::Trim) {
    ToolOption i;
    i.key = "info";
    i.type = ToolOption::Type::Info;
    i.label = "Kırp";
    i.value = QString("Silinecek parçaya tıklayın");
    i.group = "Sketch";
    return {i};
  }
  boolOpt("keepDims", "Yazılan ölçüleri kısıt olarak sakla", e ? e->keepTypedDims() : true);
  boolOpt("construction", "Yapı çizgisi", e ? e->constructionMode() : false, true);
  boolOpt("snapGrid", "Izgaraya yakala", e ? e->snapToGrid() : true);
  ToolOption g;
  g.key = "gridStep";
  g.label = "Izgara adımı (mm)";
  g.type = ToolOption::Type::Expr;
  g.value = e ? formatDim(e->currentGridStep()) : QString("1");
  g.group = "Sketch";
  o << g;
  boolOpt("autoHV", "Otomatik yatay/dikey", e ? e->autoHV() : true);
  if (tool_ == SketchEditor::Tool::Line) boolOpt("chain", "Zincirleme çiz", e ? e->chainLines() : true);
  if (tool_ == SketchEditor::Tool::Rect) boolOpt("fromCenter", "Merkezden", e ? e->rectFromCenter() : false, true);
  return o;
}

void SketchToolAdapter::setOption(const QString& key, const QVariant& v) {
  SketchEditor* e = ed();
  if (!e) return;
  if (key == "keepDims") e->setKeepTypedDims(v.toBool());
  else if (key == "construction") e->setConstructionMode(v.toBool());
  else if (key == "snapGrid") e->setSnapToGrid(v.toBool());
  else if (key == "autoHV") e->setAutoHV(v.toBool());
  else if (key == "chain") e->setChainLines(v.toBool());
  else if (key == "fromCenter") e->setRectFromCenter(v.toBool());
  else if (key == "gridStep") {
    double d = 0;
    if (host_ && host_->evaluateExpression(v.toString(), &d)) e->setGridStepOverride(d);
  } else return;
  emit optionsChanged();
}

bool SketchToolAdapter::busy() const {
  SketchEditor* e = ed();
  return e && (e->dimensionEditing() || e->drawStage() > 0);
}

bool SketchToolAdapter::cancel() {
  SketchEditor* e = ed();
  if (!e) return false;
  if (!errReason_.isEmpty()) { errReason_.clear(); reportState(); }
  if (e->cancelStep()) {
    reportState();
    emitMomentary(OpState::Cancelled, QString());   // SC23: Esc steps back (no message: the editor prompts)
    return true;
  }
  if (tool_ == SketchEditor::Tool::Select && e->hasSelection()) { e->clearSelection(); return true; }
  return false;
}

bool SketchToolAdapter::wantsKey(const QKeyEvent* k) const {
  SketchEditor* e = ed();
  return e && e->wantsKey(k);
}

bool SketchToolAdapter::keyPress(QKeyEvent* k) {
  SketchEditor* e = ed();
  return e && e->handleKey(k);
}

bool SketchToolAdapter::createGroup() {
  SketchEditor* e = ed();
  return e && e->createGroupFromBox();
}

bool SketchToolAdapter::deleteSelection() {
  SketchEditor* e = ed();
  if (!e || !e->hasSelection()) return false;
  e->deleteSelection();
  return true;
}

} // namespace pcd
