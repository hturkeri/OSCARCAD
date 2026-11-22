// SC-UX (WP-B) — 3D Select tool. See SelectTool.h.
#include "SelectTool.h"
#include "pc/ShapeViewer.h"
#include <QKeyEvent>

namespace pcd {

namespace {
const char* kFilterChoices[] = {"smart|Akıllı (Smart)", "face|Yüz (Face)", "edge|Kenar (Edge)", "body|Gövde (Body)",
                                "datum|Düzlem (Plane)", "sketch|Sketch"};
QString filterKey(SelFilter f) {
  switch (f) {
    case SelFilter::Face: return "face";
    case SelFilter::Edge: return "edge";
    case SelFilter::Body: return "body";
    case SelFilter::Datum: return "datum";
    case SelFilter::Sketch: return "sketch";
    default: return "smart";
  }
}
SelFilter filterFromKey(const QString& k) {
  if (k == "face") return SelFilter::Face;
  if (k == "edge") return SelFilter::Edge;
  if (k == "body") return SelFilter::Body;
  if (k == "datum") return SelFilter::Datum;
  if (k == "sketch") return SelFilter::Sketch;
  return SelFilter::Smart;
}
} // namespace

void SelectTool::onActivate() {
  setPrompt("Seç (Select): bir yüz, kenar, gövde ya da düzleme tıklayın. Ctrl+tık ekler/çıkarır, çift tık kenar döngüsü, "
            "üçlü tık gövde.");
}

QList<ToolOption> SelectTool::options() const {
  QList<ToolOption> o;
  ToolOption f;
  f.key = "filter";
  f.label = "Seçim filtresi (Selection filter)";
  f.type = ToolOption::Type::Choice;
  for (const char* c : kFilterChoices) f.choices << QString::fromUtf8(c);
  f.value = host_ && host_->selection() ? filterKey(host_->selection()->filter()) : QString("smart");
  f.group = "Seçim";
  o << f;
  ToolOption l;
  l.key = "loops";
  l.label = "Çift tık kenar döngüsü seçer";
  l.type = ToolOption::Type::Bool;
  l.value = loops_;
  l.group = "Seçim";
  o << l;
  return o;
}

void SelectTool::setOption(const QString& key, const QVariant& value) {
  if (key == "filter" && host_ && host_->selection()) host_->selection()->setFilter(filterFromKey(value.toString()));
  else if (key == "loops") loops_ = value.toBool();
  else return;
  emit optionsChanged();
}

bool SelectTool::pick(const PickEvent& e) {
  if (e.button != Qt::LeftButton) return false;
  return selectFromPick(e, loops_);
}

void SelectTool::hover(const SelItem& item) {
  if (host_ && host_->viewer()) host_->viewer()->setPreselection(item.name);
}

bool SelectTool::wantsKey(const QKeyEvent* e) const {
  // exactly Ctrl+A (Ctrl+Shift+A is "Uygulama olarak kaydet")
  return e->key() == Qt::Key_A && (e->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
}

bool SelectTool::keyPress(QKeyEvent* e) {
  if (!wantsKey(e) || !host_) return false;
  // Ctrl+A: every item of the kind of the current selection (faces when nothing is selected)
  SelectionModel* sel = host_->selection();
  const auto& model = host_->modelResult();
  SelItem::Kind k = sel->isEmpty() ? SelItem::Kind::Face : sel->items().last().kind;
  QList<SelItem> all;
  if (k == SelItem::Kind::Edge) for (const auto& [n, _] : model.edges) all << SelItem{k, QString::fromStdString(n)};
  else if (k == SelItem::Kind::Body) for (const auto& b : model.bodies) all << SelItem{k, QString::fromStdString(b.name)};
  else if (k == SelItem::Kind::Datum) for (const auto& [n, _] : model.datumPlanes) all << SelItem{k, QString::fromStdString(n)};
  else for (const auto& [n, _] : model.faces) all << SelItem{SelItem::Kind::Face, QString::fromStdString(n)};
  sel->set(all);
  setPrompt(QString("Seç: %1").arg(selectionSummary(all)));
  return true;
}

bool SelectTool::cancel() {
  if (host_ && host_->selection() && !host_->selection()->isEmpty()) {
    host_->selection()->clear();
    return true;
  }
  return false;
}

} // namespace pcd
