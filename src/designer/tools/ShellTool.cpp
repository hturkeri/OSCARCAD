// SC-UX (WP-B) — Shell tool. See ShellTool.h.
#include "ShellTool.h"
#include "pc/ShapeViewer.h"
#include <QKeyEvent>

namespace pcd {

void ShellTool::onActivate() {
  setPrompt("Kabuk (Shell): açılacak yüzlere tıklayın (Ctrl ile çoklu), kalınlığı yazıp Enter.");
}

QList<ToolOption> ShellTool::options() const {
  ToolOption t;
  t.key = "thickness";
  t.label = "Kalınlık";
  t.type = ToolOption::Type::Expr;
  t.value = thickness_;
  t.group = "Kabuk";
  t.paramHint = "Shell_thickness";
  ToolOption d;
  d.key = "direction";
  d.label = "Yön";
  d.type = ToolOption::Type::Choice;
  d.choices = {"inward|İçe", "outward|Dışa"};
  d.value = direction_;
  d.group = "Kabuk";
  return {t, d};
}

void ShellTool::setOption(const QString& key, const QVariant& v) {
  if (key == "thickness") thickness_ = v.toString();
  else if (key == "direction") direction_ = v.toString();
  else return;
  emit optionsChanged();
}

bool ShellTool::pick(const PickEvent& e) {
  if (!host_ || e.button != Qt::LeftButton) return false;
  PickEvent pe = e;
  pe.modifiers |= Qt::ControlModifier;   // faces accumulate (toggle)
  if (e.isBackground()) { host_->selection()->clear(); return true; }
  selectFromPick(pe, false);
  const int n = host_->selection()->namesOfKind(SelItem::Kind::Face).size();
  setPrompt(QString("Kabuk: %1 açık yüz — kalınlığı yazıp Enter (şimdiki: %2 mm).").arg(n).arg(thickness_));
  return true;
}

bool ShellTool::wantsKey(const QKeyEvent* e) const {
  if (valueBoxOpen()) return true;
  if (!host_ || host_->selection()->namesOfKind(SelItem::Kind::Face).isEmpty()) return false;
  return startsValue(e) || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter;
}

bool ShellTool::keyPress(QKeyEvent* e) {
  if (forwardToValueBox(e)) return true;
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { apply(thickness_); return true; }
  if (!startsValue(e)) return false;
  const QPoint at = host_ && host_->viewer() ? host_->viewer()->mapFromGlobal(QCursor::pos()) : QPoint(40, 40);
  openValueBox("Kalınlık", "mm", e->key() == Qt::Key_Space ? QString() : e->text(), thickness_.toDouble(), at,
               [this](const QString& t) { apply(t); });
  return true;
}

bool ShellTool::apply(const QString& thickness) {
  if (!host_) return false;
  const QStringList faces = host_->selection()->namesOfKind(SelItem::Kind::Face);
  double v = 0;
  if (faces.isEmpty() || !host_->evaluateExpression(thickness, &v) || v <= 0) {
    setPrompt("Kabuk: en az bir yüz ve pozitif bir kalınlık gerekir.");
    return false;
  }
  pc::Feature f;
  f.id = host_->document().uniqueFeatureId(pc::FeatureType::Shell);
  f.type = pc::FeatureType::Shell;
  f.props["thickness"] = thickness.trimmed().toStdString();
  if (direction_ != "inward") f.props["direction"] = direction_.toStdString();
  for (const auto& n : faces) f.refs.push_back(n.toStdString());
  thickness_ = thickness.trimmed();
  if (!host_->insertFeature(f, QString("Kabuk: %1").arg(QString::fromStdString(f.id)))) { setPrompt("Kabuk: uygulanmadı."); return false; }
  host_->selection()->clear();
  const QString s = QString("Kabuk: %1 oluşturuldu (%2 yüz, %3 mm).").arg(QString::fromStdString(f.id)).arg(faces.size()).arg(thickness_);
  setPrompt(s);
  host_->showPrompt(s, 0);
  return true;
}

} // namespace pcd
