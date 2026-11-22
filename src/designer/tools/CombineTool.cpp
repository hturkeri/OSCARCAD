// SC-UX (WP-B) — Combine tool. See CombineTool.h.
#include "CombineTool.h"
#include <QKeyEvent>

namespace pcd {

QList<ToolGuide> CombineTool::guides() const {
  return {{"target", "Hedef seç", "target", "Hedef seç (Select Target): kesilecek / birleştirilecek gövde"},
          {"cutter", "Kesici seç", "cutter", "Kesici seç (Select Cutter): araç gövde(ler) (Ctrl ile çoklu)"}};
}

void CombineTool::setActiveGuide(const QString& g) {
  if (g == "target") { target_.clear(); cutters_.clear(); emphasize(); updatePrompt(); emit guidesChanged(); }
}

QList<ToolOption> CombineTool::options() const {
  QList<ToolOption> o;
  ToolOption op;
  op.key = "op";
  op.label = "İşlem";
  op.type = ToolOption::Type::Choice;
  op.choices = {"cut|Çıkar (Subtract)", "intersect|Kesişim (Intersect)", "join|Birleştir (Merge)"};
  op.value = op_;
  op.group = "Birleştir";
  o << op;
  const char* acts[][2] = {{"doCut", "Çıkar"}, {"doIntersect", "Kesişim"}, {"doJoin", "Birleştir"}};
  for (auto& a : acts) {
    ToolOption t;
    t.key = a[0];
    t.label = QString::fromUtf8(a[1]);
    t.type = ToolOption::Type::Action;
    t.group = "Birleştir";
    t.onMiniToolbar = true;
    t.enabled = !target_.isEmpty() && !cutters_.isEmpty();
    o << t;
  }
  ToolOption k;
  k.key = "keepTools";
  k.label = "Kesiciyi koru";
  k.type = ToolOption::Type::Bool;
  k.value = keepTools_;
  k.group = "Birleştir";
  o << k;
  ToolOption l;
  l.key = "largestOnly";
  l.label = "Yalnız en büyük parça";
  l.type = ToolOption::Type::Bool;
  l.value = largestOnly_;
  l.group = "Birleştir";
  o << l;
  return o;
}

void CombineTool::setOption(const QString& key, const QVariant& v) {
  if (key == "op") op_ = v.toString();
  else if (key == "keepTools") keepTools_ = v.toBool();
  else if (key == "largestOnly") largestOnly_ = v.toBool();
  else if (key == "doCut") { apply("cut"); return; }
  else if (key == "doIntersect") { apply("intersect"); return; }
  else if (key == "doJoin") { apply("join"); return; }
  else return;
  emit optionsChanged();
}

void CombineTool::onActivate() {
  target_.clear();
  cutters_.clear();
  // a body already selected becomes the target
  if (host_ && host_->selection()) {
    for (const auto& it : host_->selection()->items()) {
      QString b;
      if (it.kind == SelItem::Kind::Body) b = it.name;
      else if (it.kind == SelItem::Kind::Face) b = QString::fromStdString(host_->modelResult().bodyOfFace(it.name.toStdString()));
      if (b.isEmpty()) continue;
      if (target_.isEmpty()) target_ = b;
      else if (b != target_ && !cutters_.contains(b)) cutters_ << b;
    }
  }
  emphasize();
  updatePrompt();
}

void CombineTool::onDeactivate() {
  if (host_) host_->setToolEmphasis({}, {});
}

void CombineTool::updatePrompt() {
  if (target_.isEmpty()) setPrompt("Birleştir (Combine): hedef gövdeye tıklayın.");
  else if (cutters_.isEmpty()) setPrompt(QString("Birleştir: hedef %1 — kesici gövdeye tıklayın (Ctrl ile çoklu).").arg(target_));
  else setPrompt(QString("Birleştir: %1 ← %2 — mini araç çubuğundan Çıkar / Kesişim / Birleştir, Enter = %3.")
                     .arg(target_, cutters_.join(", "), op_));
}

void CombineTool::emphasize() {
  if (!host_) return;
  QStringList faces;
  for (const auto& b : host_->modelResult().bodies) {
    const QString n = QString::fromStdString(b.name);
    if (n == target_ || cutters_.contains(n))
      for (const auto& f : b.faces) faces << QString::fromStdString(f);
  }
  host_->setToolEmphasis(faces, {});
  emit optionsChanged();
  emit guidesChanged();
}

bool CombineTool::pick(const PickEvent& e) {
  if (!host_ || e.button != Qt::LeftButton) return false;
  if (e.isBackground()) return true;
  const SelItem it = normalize(e.item);
  QString body;
  if (it.kind == SelItem::Kind::Body) body = it.name;
  else if (it.kind == SelItem::Kind::Face) body = QString::fromStdString(host_->modelResult().bodyOfFace(it.name.toStdString()));
  else if (it.kind == SelItem::Kind::Edge && it.name.startsWith("edge("))
    body = QString::fromStdString(host_->modelResult().bodyOfFace(it.name.mid(5, it.name.lastIndexOf(')') - 5).section('|', 0, 0).toStdString()));
  if (body.isEmpty()) { setPrompt("Birleştir: bir gövdeye tıklayın."); return true; }
  if (target_.isEmpty() || (body == target_ && cutters_.isEmpty())) {
    target_ = body;
  } else if (body != target_) {
    if (e.modifiers & Qt::ControlModifier) { if (cutters_.contains(body)) cutters_.removeAll(body); else cutters_ << body; }
    else cutters_ = QStringList{body};
  }
  host_->selection()->set({SelItem{SelItem::Kind::Body, body}});
  emphasize();
  updatePrompt();
  return true;
}

bool CombineTool::wantsKey(const QKeyEvent* e) const {
  return (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !target_.isEmpty() && !cutters_.isEmpty();
}

bool CombineTool::keyPress(QKeyEvent* e) {
  if (e->key() != Qt::Key_Return && e->key() != Qt::Key_Enter) return false;
  apply(op_);
  return true;
}

bool CombineTool::cancel() {
  if (!cutters_.isEmpty()) { cutters_.clear(); emphasize(); updatePrompt(); return true; }
  if (!target_.isEmpty()) { target_.clear(); emphasize(); updatePrompt(); return true; }
  return false;
}

bool CombineTool::apply(const QString& op) {
  if (!host_ || target_.isEmpty() || cutters_.isEmpty()) { setPrompt("Birleştir: önce hedef ve kesici gövdeleri seçin."); return false; }
  pc::Feature f;
  f.id = host_->document().uniqueFeatureId(pc::FeatureType::Combine);
  f.type = pc::FeatureType::Combine;
  f.props["op"] = op.toStdString();
  f.props["target"] = target_.toStdString();
  f.props["tools"] = cutters_.join(",").toStdString();
  if (keepTools_) f.props["keepTools"] = "1";
  if (largestOnly_) f.props["largestOnly"] = "1";
  const QString undo = QString("Birleştir: %1 %2 %3").arg(target_, op, cutters_.join(","));
  if (!host_->insertFeature(f, undo)) { setPrompt("Birleştir: uygulanmadı."); return false; }
  const QString s = QString("Birleştir: %1 oluşturuldu (%2: %3 ← %4).").arg(QString::fromStdString(f.id), op, target_, cutters_.join(", "));
  target_.clear();
  cutters_.clear();
  emphasize();
  setPrompt(s);
  host_->showPrompt(s, 0);
  return true;
}

} // namespace pcd
