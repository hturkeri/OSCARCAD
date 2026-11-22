// SC-UX (WP-B) — Plane tool. See PlaneTool.h.
#include "PlaneTool.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <QKeyEvent>

namespace pcd {

void PlaneTool::onActivate() {
  if (host_ && host_->selection() && !host_->selection()->isEmpty() && createFromSelection()) return;
  setPrompt("Düzlem (Plane): düzlemsel bir yüze tıklayın (yüzden düzlem) ya da kenar(lar) seçip Enter "
            "(1 kenar: kenara dik, 2 kenar: iki kenardan).");
}

QList<ToolOption> PlaneTool::options() const {
  ToolOption k;
  k.key = "kind";
  k.label = "Tür";
  k.type = ToolOption::Type::Choice;
  k.choices = {"auto|Otomatik", "faceOffset|Yüzden", "edgeNormal|Kenara dik", "edgeAngle|Kenardan açılı", "twoEdges|İki kenardan"};
  k.value = kind_;
  k.group = "Düzlem";
  ToolOption s;
  s.key = "show";
  s.label = "3B'de göster";
  s.type = ToolOption::Type::Bool;
  s.value = show_;
  s.group = "Düzlem";
  ToolOption i;
  i.key = "dialog";
  i.type = ToolOption::Type::Info;
  i.label = "Diyalogla oluştur";
  i.value = QString("Oluştur > Klasik ▾ > Düzlem...");
  i.group = "Düzlem";
  return {k, s, i};
}

void PlaneTool::setOption(const QString& key, const QVariant& v) {
  if (key == "kind") kind_ = v.toString();
  else if (key == "show") show_ = v.toBool();
  else return;
  emit optionsChanged();
}

bool PlaneTool::pick(const PickEvent& e) {
  if (!host_ || e.button != Qt::LeftButton) return false;
  selectFromPick(e, false);
  const SelItem it = normalize(e.item);
  if (it.kind == SelItem::Kind::Face && !(e.modifiers & Qt::ControlModifier)) createFromSelection();
  else setPrompt(QString("Düzlem: %1 — Enter ile oluşturun.").arg(selectionSummary(host_->selection()->items())));
  return true;
}

bool PlaneTool::wantsKey(const QKeyEvent* e) const {
  return (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && host_ && !host_->selection()->isEmpty();
}

bool PlaneTool::keyPress(QKeyEvent* e) {
  if (e->key() != Qt::Key_Return && e->key() != Qt::Key_Enter) return false;
  createFromSelection();
  return true;
}

bool PlaneTool::createFromSelection() {
  if (!host_) return false;
  const auto& model = host_->modelResult();
  const QStringList faces = host_->selection()->namesOfKind(SelItem::Kind::Face);
  const QStringList edges = host_->selection()->namesOfKind(SelItem::Kind::Edge);
  pc::Feature f;
  f.type = pc::FeatureType::Plane;
  f.id = host_->document().uniqueFeatureId(pc::FeatureType::Plane);
  auto straight = [&](const QString& e) {
    auto it = model.edges.find(e.toStdString());
    if (it == model.edges.end()) return false;
    try { return BRepAdaptor_Curve(it->second).GetType() == GeomAbs_Line; } catch (...) { return false; }
  };
  if (!faces.isEmpty() && (kind_ == "auto" || kind_ == "faceOffset")) {
    auto it = model.faces.find(faces.first().toStdString());
    bool planar = false;
    if (it != model.faces.end()) { try { planar = BRepAdaptor_Surface(it->second).GetType() == GeomAbs_Plane; } catch (...) {} }
    if (!planar) { setPrompt("Düzlem: yüz düzlemsel değil."); return false; }
    f.props["kind"] = "faceOffset";
    f.refs = {faces.first().toStdString()};
  } else if (edges.size() >= 2 && (kind_ == "auto" || kind_ == "twoEdges")) {
    if (!straight(edges[0]) || !straight(edges[1])) { setPrompt("Düzlem: iki kenar düz olmalı."); return false; }
    f.props["kind"] = "twoEdges";
    f.refs = {edges[0].toStdString(), edges[1].toStdString()};
  } else if (edges.size() == 1) {
    const QString k = kind_ == "edgeAngle" ? "edgeAngle" : "edgeNormal";
    if (k == "edgeAngle" && !straight(edges[0])) { setPrompt("Düzlem: açılı düzlem için kenar düz olmalı."); return false; }
    f.props["kind"] = k.toStdString();
    f.props["t"] = "0.5";
    f.refs = {edges[0].toStdString()};
  } else {
    setPrompt("Düzlem: düzlemsel bir yüz ya da 1-2 kenar seçin.");
    return false;
  }
  if (!show_) f.props["show"] = "0";
  if (!host_->insertFeature(f, QString("Düzlem: %1").arg(QString::fromStdString(f.id)))) { setPrompt("Düzlem: oluşturulamadı."); return false; }
  const QString id = QString::fromStdString(f.id);
  host_->selection()->set({SelItem{SelItem::Kind::Datum, id}});
  const QString s = QString("Düzlem: %1 oluşturuldu — Taşı ile ötelenebilir / döndürülebilir.").arg(id);
  setPrompt(s);
  host_->showPrompt(s, 0);
  emit requestTool("move");
  return true;
}

} // namespace pcd
