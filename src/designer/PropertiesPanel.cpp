// SC-UX shell (WP-A): Properties panel. See PropertiesPanel.h.
#include <QToolButton>
#include "PropertiesPanel.h"
#include "ExprEdit.h"
#include "SketchRegions.h"   // SC23 (O-P0-6)
#include <BRepTools.hxx>
#include <TopoDS_Wire.hxx>
#include "Icons.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
#include <set>

using namespace pc;

namespace pcd {

namespace {
bool isExprKey(const std::string& k) {
  static const std::set<std::string> non = {"sketch", "axis", "direction", "op", "kind", "source", "targets", "target", "tools",
                                            "bodyName", "keepTool", "keepTools", "through", "flip", "show", "largestOnly", "mfg"};
  return non.count(k) == 0;
}
bool isBoolKey(const std::string& k) {
  static const std::set<std::string> b = {"through", "flip", "show", "keepTool", "keepTools", "largestOnly", "mfg"};
  return b.count(k) > 0;
}
QString keyLabel(const std::string& k) {
  static const std::map<std::string, const char*> m = {
      {"distance", "Mesafe"}, {"radius", "Yarıçap"}, {"diameter", "Çap"}, {"depth", "Derinlik"}, {"angle", "Açı (°)"},
      {"thickness", "Kalınlık"}, {"offset", "Ofset"}, {"rotate", "Döndürme (°)"}, {"t", "Kenar konumu t (0..1)"},
      {"count", "Adet"}, {"spacing", "Aralık"}, {"draft", "Eğim açısı (°)"}, {"op", "İşlem"}, {"direction", "Yön"},
      {"kind", "Tür"}, {"axis", "Eksen"}, {"sketch", "Sketch"}, {"source", "Kaynak özellik"}, {"target", "Hedef gövde"},
      {"tools", "Kesici gövdeler"}, {"targets", "Hedef gövdeler"}, {"bodyName", "Gövde adı"}, {"through", "Tümünden geçir"},
      {"flip", "Normali ters çevir"}, {"show", "3B görünümde göster"}, {"keepTool", "Kesiciyi koru"},
      {"keepTools", "Kesicileri koru"}, {"largestOnly", "Yalnız en büyük parça"}, {"mfg", "İmalat denetimine dahil"},
      {"cbDiameter", "Havşa çapı"}, {"cbDepth", "Havşa derinliği"}, {"csDiameter", "Konik havşa çapı"},
      {"csAngle", "Konik havşa açısı (°)"}, {"tipAngle", "Uç açısı (°)"}, {"dirX", "Yön X"}, {"dirY", "Yön Y"}, {"dirZ", "Yön Z"}};
  auto it = m.find(k);
  return it != m.end() ? QString::fromUtf8(it->second) : QString::fromStdString(k);
}
ParamKind keyKind(const std::string& k) {
  if (k == "count") return ParamKind::Count;
  if (k == "angle" || k == "draft" || k == "rotate" || k == "csAngle" || k == "tipAngle") return ParamKind::Angle;
  if (k == "t" || k.rfind("dir", 0) == 0) return ParamKind::Scalar;
  return ParamKind::Length;
}
QStringList choicesFor(FeatureType t, const std::string& k) {
  if (k == "op") return t == FeatureType::Combine ? QStringList{"join", "cut", "intersect"} : QStringList{"new", "join", "cut", "intersect"};
  if (k == "direction") return t == FeatureType::Shell ? QStringList{"inward", "outward"} : QStringList{"normal", "reverse", "symmetric"};
  if (k == "kind" && t == FeatureType::Hole) return {"simple", "counterbore", "countersink"};
  if (k == "kind" && t == FeatureType::Pattern) return {"linear", "circular"};
  if (k == "kind" && t == FeatureType::Plane) return {"edgeNormal", "edgeAngle", "twoEdges", "faceOffset", "axisAngle"};
  if (k == "axis" && t == FeatureType::Pattern) return {"X", "Y", "Z"};
  return {};
}
QString num(double v, int prec = 3) { return QString::number(v, 'f', prec); }
QString planeText(const SketchPlane& p) {
  switch (p.kind) {
    case SketchPlaneKind::XY: return "XY";
    case SketchPlaneKind::XZ: return "XZ";
    case SketchPlaneKind::YZ: return "YZ";
    case SketchPlaneKind::OnFace: return "Yüz: " + QString::fromStdString(p.faceRef);
    case SketchPlaneKind::OnPlane: return "Düzlem: " + QString::fromStdString(p.planeRef);
  }
  return "?";
}
QString featureTypeText(FeatureType t) {
  switch (t) {
    case FeatureType::Sketch: return "Sketch";
    case FeatureType::Extrude: return "Extrude";
    case FeatureType::Revolve: return "Revolve";
    case FeatureType::Hole: return "Delik (Hole)";
    case FeatureType::Fillet: return "Fillet";
    case FeatureType::Chamfer: return "Chamfer";
    case FeatureType::Shell: return "Shell";
    case FeatureType::Pattern: return "Pattern";
    case FeatureType::Plane: return "Datum düzlemi";
    case FeatureType::Combine: return "Combine";
    case FeatureType::Transform: return "Taşı (Transform)";
  }
  return "?";
}
// Keys a face of a feature type makes editable directly (SPEC §9 "Özelliğe göre").
QStringList faceKeys(FeatureType t, const QString& faceName) {
  switch (t) {
    case FeatureType::Fillet: return {"radius"};
    case FeatureType::Chamfer: return {"distance"};
    case FeatureType::Hole: return {"diameter", "depth"};
    case FeatureType::Extrude: return faceName.endsWith(".end") || faceName.endsWith(".start") ? QStringList{"distance", "direction"} : QStringList{"distance"};
    case FeatureType::Revolve: return {"angle"};
    case FeatureType::Shell: return {"thickness"};
    case FeatureType::Pattern: return {"count", "spacing"};
    default: return {};
  }
}
} // namespace

PropertiesPanel::PropertiesPanel(IDesignHost* host, QWidget* parent) : QWidget(parent), host_(host) {
  setObjectName("propertiesPanel");
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  tree_ = new QTreeWidget(this);
  tree_->setObjectName("propertiesTree");
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({"Özellik", "Değer"});
  tree_->setRootIsDecorated(true);
  tree_->setIndentation(10);
  tree_->setAlternatingRowColors(true);
  tree_->setSelectionMode(QAbstractItemView::NoSelection);
  tree_->setUniformRowHeights(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
  tree_->header()->setStretchLastSection(true);
  tree_->setColumnWidth(0, 130);
  v->addWidget(tree_);
}

QTreeWidgetItem* PropertiesPanel::category(const QString& title) {
  auto* it = new QTreeWidgetItem(tree_, {title});
  QFont f = it->font(0);
  f.setBold(true);
  it->setFont(0, f);
  it->setFirstColumnSpanned(true);
  it->setBackground(0, QColor(228, 235, 243));
  it->setExpanded(true);
  return it;
}

QTreeWidgetItem* PropertiesPanel::row(QTreeWidgetItem* cat, const QString& label, const QString& value, const QString& tip) {
  auto* it = new QTreeWidgetItem(cat, {label, value});
  it->setForeground(1, QColor(95, 105, 118));   // read-only values are grey (SPEC §9)
  it->setToolTip(1, tip.isEmpty() ? value : tip);
  return it;
}

void PropertiesPanel::exprRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QString& expr,
                              std::function<bool(const QString&)> commit, ParamKind kind, const QString& hint) {
  auto* it = new QTreeWidgetItem(cat, {label});
  auto* ee = new ExprEdit(host_->expressionContext(), tree_);
  ee->setObjectName("prop." + key);
  ee->setText(expr);
  ee->setParamHint(hint, kind);
  ee->setToolTip(QString("%1: ifade veya parametre (Enter = uygula)").arg(label));
  tree_->setItemWidget(it, 1, ee);
  editors_.insert(key, ee);
  auto done = std::make_shared<bool>(false);
  auto apply = [ee, expr, commit, done] {
    if (*done || !ee->isValid() || ee->text() == expr) return;
    *done = true;
    if (!commit(ee->text())) *done = false;
  };
  connect(ee->lineEdit(), &QLineEdit::editingFinished, ee, apply);
  // parameter picker / "Yeni…" bind the field programmatically: apply once the picker handler has run
  connect(ee, &ExprEdit::textChanged, ee, [ee, apply] { if (!ee->lineEdit()->hasFocus()) QTimer::singleShot(0, ee, apply); });
}

void PropertiesPanel::comboRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QStringList& choices,
                               const QString& cur, std::function<bool(const QString&)> commit) {
  auto* it = new QTreeWidgetItem(cat, {label});
  auto* c = new QComboBox(tree_);
  c->setObjectName("prop." + key);
  c->addItems(choices);
  if (!choices.contains(cur)) c->addItem(cur);
  c->setCurrentText(cur);
  tree_->setItemWidget(it, 1, c);
  editors_.insert(key, c);
  connect(c, &QComboBox::activated, c, [c, cur, commit](int) { if (c->currentText() != cur) commit(c->currentText()); });
}

void PropertiesPanel::textRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QString& cur,
                              std::function<bool(const QString&)> commit) {
  auto* it = new QTreeWidgetItem(cat, {label});
  auto* le = new QLineEdit(cur, tree_);
  le->setObjectName("prop." + key);
  tree_->setItemWidget(it, 1, le);
  editors_.insert(key, le);
  auto done = std::make_shared<bool>(false);
  connect(le, &QLineEdit::editingFinished, le, [le, cur, commit, done] {
    if (*done || le->text().trimmed() == cur) return;
    *done = true;
    if (!commit(le->text().trimmed())) { *done = false; le->setText(cur); }
  });
}

void PropertiesPanel::checkRow(QTreeWidgetItem* cat, const QString& label, const QString& key, bool cur, std::function<void(bool)> commit) {
  auto* it = new QTreeWidgetItem(cat, {label});
  auto* cb = new QCheckBox(tree_);
  cb->setObjectName("prop." + key);
  cb->setChecked(cur);
  tree_->setItemWidget(it, 1, cb);
  editors_.insert(key, cb);
  connect(cb, &QCheckBox::toggled, cb, [commit](bool on) { commit(on); });
}

void PropertiesPanel::buttonRow(QTreeWidgetItem* cat, const QString& label, const QString& text, std::function<void()> fn) {
  auto* it = new QTreeWidgetItem(cat, {label});
  auto* b = new QPushButton(text, tree_);
  b->setObjectName("prop.button." + label);
  b->setFlat(true);
  b->setStyleSheet("QPushButton { color:#1d5fae; text-align:left; text-decoration: underline; border:none; }");
  b->setCursor(Qt::PointingHandCursor);
  tree_->setItemWidget(it, 1, b);
  connect(b, &QPushButton::clicked, b, [fn] { QTimer::singleShot(0, fn); });
}

void PropertiesPanel::measureRows(const QList<Hooks::MeasureRow>& rows) {
  auto* cat = category("Seçimden parametre");
  cat->setToolTip(0, "Seçili öğenin ölçüleri: 'Parametre yap' ölçüyü sürücü bir parametreye (Gruplar) bağlar");
  for (const auto& r : rows) {
    auto* it = new QTreeWidgetItem(cat, {r.label});
    auto* w = new QWidget(tree_);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 2, 0);
    h->setSpacing(6);
    auto* v = new QLabel(r.value, w);
    v->setObjectName("prop.paramValue." + r.key);
    v->setToolTip(r.value);
    v->setMinimumWidth(10);
    v->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // the value elides, the button stays readable
    h->addWidget(v, 1);
    auto* b = new QPushButton(r.button, w);
    b->setObjectName("prop.param." + r.key);
    b->setEnabled(r.enabled);
    b->setToolTip(r.tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet("QPushButton { padding: 1px 6px; }");
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    h->addWidget(b);
    tree_->setItemWidget(it, 1, w);
    editors_.insert("param." + r.key, b);
    const auto fn = r.fn;
    if (fn) connect(b, &QPushButton::clicked, b, [fn] { QTimer::singleShot(0, fn); });
  }
}

QString PropertiesPanel::valueText(const QString& label) const {
  for (int c = 0; c < tree_->topLevelItemCount(); ++c) {
    QTreeWidgetItem* cat = tree_->topLevelItem(c);
    for (int i = 0; i < cat->childCount(); ++i) {
      QTreeWidgetItem* it = cat->child(i);
      if (it->text(0) != label) continue;
      if (QWidget* w = tree_->itemWidget(it, 1)) {
        if (auto* ee = qobject_cast<ExprEdit*>(w)) return ee->text();
        if (auto* le = qobject_cast<QLineEdit*>(w)) return le->text();
        if (auto* cb = qobject_cast<QComboBox*>(w)) return cb->currentText();
        if (auto* ch = qobject_cast<QCheckBox*>(w)) return ch->isChecked() ? "1" : "0";
        if (auto* pb = qobject_cast<QPushButton*>(w)) return pb->text();
      }
      return it->text(1);
    }
  }
  return QString();
}

QStringList PropertiesPanel::rowLabels() const {
  QStringList l;
  for (int c = 0; c < tree_->topLevelItemCount(); ++c)
    for (int i = 0; i < tree_->topLevelItem(c)->childCount(); ++i) l << tree_->topLevelItem(c)->child(i)->text(0);
  return l;
}

// ---------------------------------------------------------------------------------------------- dispatch
void PropertiesPanel::rebuild() {
  tree_->clear();   // item widgets are deleted with their items
  editors_.clear();
  const SelectionModel* sel = host_->selection();
  const auto& items = sel->items();
  const QList<Hooks::MeasureRow> mrows = hooks_.measureRows ? hooks_.measureRows() : QList<Hooks::MeasureRow>();
  if (items.isEmpty() && !mrows.isEmpty()) measureRows(mrows);   // sketch selection: the measures first
  else if (items.isEmpty()) showNone();
  else if (items.size() > 1) showMulti(items);
  else {
    const SelItem& s = items.front();
    switch (s.kind) {
      case SelItem::Kind::Body: showBody(s.name); break;
      case SelItem::Kind::Face: showFace(s.name); break;
      case SelItem::Kind::Edge: showEdge(s.name); break;
      case SelItem::Kind::Datum: showDatum(s.name); break;
      case SelItem::Kind::Sketch: showSketch(s.name); break;
      case SelItem::Kind::SketchRegion: showRegion(s.name); break;
      case SelItem::Kind::Feature: showFeature(s.name); break;
      case SelItem::Kind::Parameter: showParameter(s.name); break;
      case SelItem::Kind::SketchCurve:
      case SelItem::Kind::SketchPoint: {
        auto* c = category("Sketch öğesi");
        row(c, "Ad", s.name);
        row(c, "Tür", selKindText(s.kind));
        break;
      }
    }
  }
  if (!items.isEmpty() && !mrows.isEmpty()) measureRows(mrows);
  tree_->resizeColumnToContents(0);
  tree_->setColumnWidth(0, std::clamp(tree_->columnWidth(0) + 8, 110, 190));
}

bool PropertiesPanel::commitFeatureProp(const std::string& fid, const std::string& key, const QString& value) {
  Document cand = host_->document();
  Feature* f = cand.feature(fid);
  if (!f) return false;
  if (f->prop(key) == value.toStdString()) return true;
  f->props[key] = value.toStdString();
  return host_->commitDocument(cand, QString("Özellikler: %1.%2 = %3").arg(QString::fromStdString(fid), QString::fromStdString(key), value));
}

void PropertiesPanel::featureFields(QTreeWidgetItem* cat, const Feature& f, const QStringList& onlyKeys) {
  const std::string fid = f.id;
  std::vector<std::string> keys;
  if (!onlyKeys.isEmpty()) {
    for (const QString& k : onlyKeys) keys.push_back(k.toStdString());
  } else {
    for (const auto& [k, v] : f.props) keys.push_back(k);
  }
  for (const std::string& k : keys) {
    const bool present = f.props.count(k) > 0;
    if (!present && onlyKeys.isEmpty()) continue;
    const QString cur = QString::fromStdString(f.prop(k));
    if (!present && (k == "depth" || k == "spacing") && cur.isEmpty()) continue;   // through-hole / circular pattern
    const QString label = keyLabel(k);
    const QString key = QString::fromStdString(k);
    if (isBoolKey(k)) {
      const bool def = k == "show" || k == "mfg";
      const bool on = present ? (cur == "1" || cur == "true") : def;
      checkRow(cat, label, key, on, [this, fid, k](bool b) { commitFeatureProp(fid, k, b ? "1" : "0"); });
    } else if (!choicesFor(f.type, k).isEmpty()) {
      comboRow(cat, label, key, choicesFor(f.type, k), cur, [this, fid, k](const QString& v) { return commitFeatureProp(fid, k, v); });
    } else if (k == "bodyName") {
      textRow(cat, label, key, cur, [this, fid, k](const QString& v) { return commitFeatureProp(fid, k, v); });
    } else if (isExprKey(k)) {
      QString shown = cur;
      exprRow(cat, label, key, shown, [this, fid, k](const QString& v) { return commitFeatureProp(fid, k, v); }, keyKind(k),
              QString::fromStdString(fid + "_" + k));
    } else {
      row(cat, label, cur);
    }
  }
}

// ---------------------------------------------------------------------------------------------- kinds
void PropertiesPanel::showNone() {
  const Document& d = host_->document();
  const auto& m = host_->modelResult();
  auto* c = category("Belge");
  row(c, "Ad", QString::fromStdString(d.name));
  const QString path = hooks_.filePath ? hooks_.filePath() : QString();
  row(c, "Dosya", path.isEmpty() ? QString("(kaydedilmedi)") : path);
  row(c, "Parametre", QString::number(d.params.all().size()));
  row(c, "Özellik", QString::number(d.features.size()));
  row(c, "Gövde", QString::number(m.bodies.size()));
  double vol = 0;
  try { if (!m.shape.IsNull()) vol = model::massProperties(m.shape).volume; } catch (...) {}
  row(c, "Toplam hacim", QString("%1 mm³").arg(vol, 0, 'f', 1));
  auto* g = category("İmalat");
  row(g, "Eklemeli süreç", QString::fromUtf8(toString(d.mfg.am.process)));
  row(g, "Konvansiyonel süreç", QString::fromUtf8(toString(d.mfg.conv.process)));
  if (hooks_.openMfgSettings) buttonRow(g, "Ayarlar", "İmalat ayarları...", hooks_.openMfgSettings);
}

// SC-COLOR: "Renk" row = swatch button with the palette menu (objectName "prop.color")
void PropertiesPanel::colorRow(QTreeWidgetItem* cat, const SelItem& item) {
  if (!hooks_.colorOf || !hooks_.colorMenu) return;
  bool inherited = false;
  const QString hex = hooks_.colorOf(item, &inherited);
  auto* it = new QTreeWidgetItem(cat, {"Renk"});
  auto* b = new QToolButton(tree_);
  b->setObjectName("prop.color");
  b->setPopupMode(QToolButton::InstantPopup);
  b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  QPixmap pm(14, 14);
  pm.fill(hex.isEmpty() ? QColor(158, 182, 212) : QColor(hex));
  b->setIcon(QIcon(pm));
  b->setText(hex.isEmpty() ? QString("Varsayılan") : hex.toUpper() + (inherited ? "  (gövdeden)" : QString()));
  b->setAutoRaise(true);
  b->setMenu(hooks_.colorMenu(b, {item}));
  tree_->setItemWidget(it, 1, b);
  editors_["color"] = b;
}

void PropertiesPanel::showBody(const QString& name) {
  const auto& m = host_->modelResult();
  const model::BodyInfo* b = m.body(name.toStdString());
  if (!b) { showNone(); return; }
  auto* g = category("Genel");
  textRow(g, "Ad", "name", name, [this, name](const QString& to) { return hooks_.renameBody && hooks_.renameBody(name, to); });
  if (hooks_.bodyVisible && hooks_.setBodyVisible)
    checkRow(g, "Görünür", "visible", hooks_.bodyVisible(name), [this, name](bool on) { hooks_.setBodyVisible(name, on); });
  row(g, "Yüz sayısı", QString::number(b->faces.size()));
  colorRow(g, SelItem{SelItem::Kind::Body, name});   // SC-COLOR
  nsSection({SelItem{SelItem::Kind::Body, name}});
  auto* p = category("Fiziksel");
  try {
    const auto mp = model::massProperties(b->shape);
    row(p, "Hacim", QString("%1 mm³").arg(mp.volume, 0, 'f', 1));
    row(p, "Yüzey alanı", QString("%1 mm²").arg(mp.area, 0, 'f', 1));
    row(p, "Ağırlık merkezi", QString("(%1, %2, %3)").arg(num(mp.cx, 2), num(mp.cy, 2), num(mp.cz, 2)));
    row(p, "Sınır kutusu", QString("%1 × %2 × %3 mm").arg(num(mp.xmax - mp.xmin, 2), num(mp.ymax - mp.ymin, 2), num(mp.zmax - mp.zmin, 2)));
  } catch (...) {
    row(p, "Hacim", "?");
  }
  auto* h = category("Geçmiş");
  const std::string creator = b->createdBy;
  buttonRow(h, "Oluşturan özellik", QString::fromStdString(creator), [this, creator] { if (hooks_.showInHistory) hooks_.showInHistory(creator); });
  std::set<std::string> mod;
  for (const auto& fn : b->faces) {
    const auto dot = fn.find('.');
    if (dot != std::string::npos && fn.substr(0, dot) != creator) mod.insert(fn.substr(0, dot));
  }
  QStringList ml;
  for (const auto& x : mod) ml << QString::fromStdString(x);
  row(h, "Değiştiren özellikler", ml.isEmpty() ? QString("—") : ml.join(", "));
  if (const Feature* cf = host_->document().feature(creator)) {
    auto* im = category("İmalat");
    const bool inc = cf->prop("mfg", "1") != "0";
    checkRow(im, "İmalat denetimine dahil", "mfg", inc, [this, creator](bool on) { commitFeatureProp(creator, "mfg", on ? "1" : "0"); });
  }
}

void PropertiesPanel::showFace(const QString& name) {
  const auto& m = host_->modelResult();
  auto fit = m.faces.find(name.toStdString());
  auto* g = category("Genel");
  auto* nameEd = new QLineEdit(name, tree_);
  nameEd->setReadOnly(true);
  nameEd->setObjectName("prop.persistentName");
  nameEd->setToolTip("Kalıcı ad (kopyalanabilir)");
  tree_->setItemWidget(new QTreeWidgetItem(g, {"Kalıcı ad"}), 1, nameEd);
  if (fit == m.faces.end()) { row(g, "Durum", "Modelde bulunamadı"); return; }
  try {
    BRepAdaptor_Surface s(fit->second);
    QString type = "Serbest";
    switch (s.GetType()) {
      case GeomAbs_Plane: {
        type = "Düzlem";
        gp_Dir n = s.Plane().Axis().Direction();
        if (fit->second.Orientation() == TopAbs_REVERSED) n.Reverse();
        row(g, "Normal", QString("(%1, %2, %3)").arg(num(n.X()), num(n.Y()), num(n.Z())));
        break;
      }
      case GeomAbs_Cylinder: {
        type = "Silindir";
        const gp_Cylinder c = s.Cylinder();
        row(g, "Yarıçap (geometri)", QString("%1 mm").arg(c.Radius(), 0, 'f', 3));
        const gp_Dir a = c.Axis().Direction();
        row(g, "Eksen", QString("(%1, %2, %3)").arg(num(a.X()), num(a.Y()), num(a.Z())));
        break;
      }
      case GeomAbs_Cone: type = "Koni"; break;
      case GeomAbs_Sphere: type = "Küre"; break;
      case GeomAbs_Torus: type = "Torus"; break;
      default: break;
    }
    row(g, "Tür", type);
    GProp_GProps gp;
    BRepGProp::SurfaceProperties(fit->second, gp);
    row(g, "Alan", QString("%1 mm²").arg(gp.Mass(), 0, 'f', 2));
  } catch (...) {}
  const QString body = QString::fromStdString(m.bodyOfFace(name.toStdString()));
  row(g, "Gövde", body.isEmpty() ? QString("—") : body);
  colorRow(g, SelItem{SelItem::Kind::Face, name});   // SC-COLOR
  nsSection({SelItem{SelItem::Kind::Face, name}});
  const QString fid = name.section('.', 0, 0);
  const Feature* f = host_->document().feature(fid.toStdString());
  if (!f) return;
  auto* h = category(QString("Özellik: %1").arg(fid));
  const std::string sfid = f->id;
  buttonRow(h, "Oluşturan özellik", fid + "  (" + featureTypeText(f->type) + ")", [this, sfid] { if (hooks_.showInHistory) hooks_.showInHistory(sfid); });
  featureFields(h, *f, faceKeys(f->type, name));
}

void PropertiesPanel::showEdge(const QString& name) {
  const auto& m = host_->modelResult();
  auto* g = category("Genel");
  auto* nameEd = new QLineEdit(name, tree_);
  nameEd->setReadOnly(true);
  nameEd->setObjectName("prop.persistentName");
  tree_->setItemWidget(new QTreeWidgetItem(g, {"Kalıcı ad"}), 1, nameEd);
  auto eit = m.edges.find(name.toStdString());
  if (eit == m.edges.end()) { row(g, "Durum", "Modelde bulunamadı"); return; }
  try {
    BRepAdaptor_Curve c(eit->second);
    QString type = "Eğri";
    if (c.GetType() == GeomAbs_Line) type = "Doğru";
    else if (c.GetType() == GeomAbs_Circle) { type = "Çember / yay"; row(g, "Yarıçap", QString("%1 mm").arg(c.Circle().Radius(), 0, 'f', 3)); }
    else if (c.GetType() == GeomAbs_Ellipse) type = "Elips";
    else if (c.GetType() == GeomAbs_BSplineCurve) type = "B-spline";
    row(g, "Tür", type);
    row(g, "Uzunluk", QString("%1 mm").arg(GCPnts_AbscissaPoint::Length(c), 0, 'f', 3));
  } catch (...) {}
  QStringList adj;
  for (const auto& [fn, face] : m.faces) {
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next())
      if (ex.Current().IsSame(eit->second)) { adj << QString::fromStdString(fn); break; }
    if (adj.size() >= 4) break;
  }
  row(g, "Komşu yüzler", adj.isEmpty() ? QString("—") : adj.join("\n"));
  const QString fid = name.section('.', 0, 0);
  if (const Feature* f = host_->document().feature(fid.toStdString())) {
    const bool round = f->type == FeatureType::Fillet || f->type == FeatureType::Chamfer;
    row(g, "Fillet / Chamfer kenarı", round ? QString("Evet (%1)").arg(fid) : QString("Hayır"));
  }
  if (!name.startsWith("sketch:")) nsSection({SelItem{SelItem::Kind::Edge, name}});
}

void PropertiesPanel::showDatum(const QString& id) {
  const Feature* f = host_->document().feature(id.toStdString());
  if (!f) { showNone(); return; }
  auto* g = category("Datum düzlemi");
  row(g, "Ad", id);
  Feature shown = *f;
  // make the common plane fields editable even when not stored yet (defaults per Feature.h)
  for (const char* k : {"offset", "rotate"}) if (!shown.props.count(k)) shown.props[k] = "0";
  const std::string kind = f->prop("kind", "edgeNormal");
  QStringList keys = {"kind", "offset", "rotate"};
  if (kind == "edgeAngle" || kind == "axisAngle") keys << "angle";
  if (kind == "edgeNormal" || kind == "edgeAngle") keys << "t";
  if (kind == "axisAngle") keys << "axis";
  keys << "flip" << "show";
  featureFields(g, shown, keys);
  QStringList refs;
  for (const auto& r : f->refs) refs << QString::fromStdString(r);
  row(g, "Referanslar", refs.isEmpty() ? QString("—") : refs.join("\n"));
}

void PropertiesPanel::showSketch(const QString& id) {
  const Document& d = host_->document();
  const Feature* f = d.feature(id.toStdString());
  if (!f || !f->sketch) { showNone(); return; }
  const Sketch& sk = *f->sketch;
  auto* g = category("Sketch");
  row(g, "Ad", id);
  row(g, "Düzlem", planeText(sk.plane));
  const std::string sid = f->id;
  exprRow(g, "Düzlem ofseti", "offsetExpr", QString::fromStdString(sk.plane.offsetExpr), [this, sid](const QString& v) {
    Document cand = host_->document();
    Feature* cf = cand.feature(sid);
    if (!cf || !cf->sketch) return false;
    cf->sketch->plane.offsetExpr = v.toStdString();
    return host_->commitDocument(cand, QString("Özellikler: %1 düzlem ofseti = %2").arg(QString::fromStdString(sid), v));
  }, ParamKind::Length, QString::fromStdString(sid + "_offset"));
  int dims = 0;
  for (const auto& c : sk.constraints) if (isDimensional(c.type)) ++dims;
  row(g, "Eleman / kısıt / ölçü", QString("%1 / %2 / %3").arg(sk.entities.size()).arg(int(sk.constraints.size()) - dims).arg(dims));
  const auto& m = host_->modelResult();
  if (auto it = m.sketchResults.find(sid); it != m.sketchResults.end())
    row(g, "DOF", QString("%1  %2").arg(it->second.dof).arg(QString::fromStdString(it->second.message)));
  QStringList users;
  for (const auto& x : d.features) if (x.prop("sketch") == sid) users << QString::fromStdString(x.id);
  row(g, "Kullanan özellikler", users.isEmpty() ? QString("—") : users.join(", "));
  if (hooks_.sketchShown && hooks_.setSketchShown)
    checkRow(g, "3B'de göster", "show3d", hooks_.sketchShown(sid), [this, sid](bool on) { hooks_.setSketchShown(sid, on); });
  buttonRow(g, "Düzenle", "Sketch'i düzenle (K)", [this, sid] { host_->editSketch(sid); });
}

void PropertiesPanel::showRegion(const QString& name) {
  auto* g = category("Sketch bölgesi");
  const QString sid = name.section(':', 1, 1);
  // SC23 (O-P0-6): what the region is and its measures (ANALYSIS §2.2 Properties, §4.6)
  row(g, "Tür", "Yüzey", "Eskiz bölgesi (düzlemsel yüzey): Çek (P) ile katı olur. PersistCAD'de ayrı yüzey gövdesi yok (SC23-CR-1).");
  row(g, "Sketch", sid);
  row(g, "Bölge sırası", name.section(':', 2, 2).mid(1));
  row(g, "Ad", name);
  {
    pc::sketch::RegionResult rr;
    gp_Ax3 fr;
    std::string ssid;
    char ty = 0;
    int k = -1;
    if (parseSketchItemName(name, &ssid, &ty, &k) && ty == 'r' && sketchRegionInfo(host_->document(), host_->modelResult(), ssid, rr, fr) &&
        k >= 0 && k < int(rr.profile.faces.size())) {
      double area = k < int(rr.areas.size()) ? rr.areas[size_t(k)] : 0.0;
      try {   // exact B-rep area (the arrangement's areas are polygon estimates)
        GProp_GProps ga;
        BRepGProp::SurfaceProperties(rr.profile.faces[size_t(k)], ga);
        if (ga.Mass() > 0) area = ga.Mass();
      } catch (...) {}
      double per = 0;
      try {
        const TopoDS_Wire w = BRepTools::OuterWire(rr.profile.faces[size_t(k)]);
        GProp_GProps gp;
        if (!w.IsNull()) { BRepGProp::LinearProperties(w, gp); per = gp.Mass(); }
      } catch (...) {}
      const int holes = k < int(rr.holes.size()) ? rr.holes[size_t(k)] : 0;
      row(g, "Alan", QString("%1 mm²").arg(area, 0, 'f', 2), "Net alan (delikler çıkarılmış)");
      row(g, "Çevre", QString("%1 mm").arg(per, 0, 'f', 2), "Dış sınırın uzunluğu");
      row(g, "Delik sayısı", QString::number(holes), "Bölgenin iç döngüleri (çekilince boşluk olur)");
    }
  }
  const std::string s = sid.toStdString();
  buttonRow(g, "Düzenle", "Sketch'i düzenle (K)", [this, s] { host_->editSketch(s); });
}

void PropertiesPanel::showFeature(const QString& id) {
  const Feature* f = host_->document().feature(id.toStdString());
  if (!f) { showNone(); return; }
  if (f->type == FeatureType::Plane) { showDatum(id); return; }
  auto* g = category(QString("%1 (%2)").arg(id, featureTypeText(f->type)));
  const std::string fid = f->id;
  if (f->type == FeatureType::Sketch) {
    tree_->clear();
    editors_.clear();
    showSketch(id);
    return;
  }
  featureFields(g, *f, {});
  if (!f->refs.empty()) {
    QStringList refs;
    for (const auto& r : f->refs) refs << QString::fromStdString(r);
    row(g, "Referanslar", QString("%1 adet").arg(refs.size()), refs.join("\n"));
  }
  auto* s = category("Durum");
  checkRow(s, "Bastırılmış", "suppressed", f->suppressed, [this, fid](bool on) { host_->setFeatureSuppressed(fid, on); });
  QString st = "—";
  for (const auto& x : host_->modelResult().status)
    if (x.featureId == fid) st = x.ok ? (x.message.empty() ? QString("Tamam") : "Tamam: " + QString::fromStdString(x.message)) : "HATA: " + QString::fromStdString(x.message);
  row(s, "Durum", st);
  if (hooks_.parameterSummary) {
    const QString ps = hooks_.parameterSummary(id);
    row(s, "Parametreler", ps.isEmpty() ? QString("—") : QString("%1 adet").arg(ps.count('\n') + 1), ps);
  }
  buttonRow(s, "Klasik diyalog", "Klasik diyalogda aç...", [this, fid] { host_->openClassicEditor(fid); });
}

void PropertiesPanel::showParameter(const QString& name) {
  const Document& d = host_->document();
  const Parameter* p = d.params.find(name.toStdString());
  if (!p) { showNone(); return; }
  const std::string n = p->name;
  auto commitParam = [this, n](std::function<void(Parameter&)> mod, const QString& desc) {
    Document cand = host_->document();
    Parameter* q = cand.params.find(n);
    if (!q) return false;
    mod(*q);
    return host_->commitDocument(cand, desc);
  };
  auto* g = category("Parametre (Grup)");
  row(g, "Ad", name);
  exprRow(g, "İfade", "expression", QString::fromStdString(p->expression),
          [this, n](const QString& v) { return host_->changeParameter(n, v.toStdString()); }, p->kind, QString());
  double val = 0;
  QString err;
  row(g, "Değer", host_->evaluateExpression(name, &val, &err) ? QString::number(val, 'g', 8) : "hata: " + err);
  comboRow(g, "Birim", "kind", {"mm", "°", "adet", "-"}, paramUnitText(p->kind), [commitParam, name](const QString& v) {
    return commitParam([v](Parameter& q) { q.kind = paramKindFromText(v); }, "Parametre " + name + " birimi");
  });
  auto limit = [](double x) { return std::isfinite(x) ? QString::number(x, 'g', 8) : QString(); };
  for (int which = 0; which < 2; ++which) {
    const bool isMin = which == 0;
    textRow(g, isMin ? "Min" : "Maks", isMin ? "min" : "max", limit(isMin ? p->minValue : p->maxValue),
            [commitParam, name, isMin](const QString& v) {
              double x = isMin ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
              if (!v.isEmpty()) {
                bool ok = false;
                x = QString(v).replace(',', '.').toDouble(&ok);
                if (!ok) return false;
              }
              return commitParam([x, isMin](Parameter& q) { (isMin ? q.minValue : q.maxValue) = x; }, "Parametre " + name + (isMin ? " min" : " maks"));
            });
  }
  checkRow(g, "Uygulamada göster", "exposed", p->exposed, [commitParam, name](bool on) {
    commitParam([on](Parameter& q) { q.exposed = on; }, "Parametre " + name + " uygulamada göster");
  });
  struct TF { const char* label; const char* key; std::string Parameter::*field; };
  for (const TF& tf : {TF{"Etiket", "label", &Parameter::label}, TF{"Grup", "group", &Parameter::group}, TF{"Açıklama", "description", &Parameter::description}}) {
    auto field = tf.field;
    textRow(g, tf.label, tf.key, QString::fromStdString((*p).*field), [commitParam, name, field](const QString& v) {
      return commitParam([v, field](Parameter& q) { q.*field = v.toStdString(); }, "Parametre " + name);
    });
  }
  // usages
  QStringList used;
  auto mentions = [&](const std::string& expr) {
    try { for (const auto& r : referencedNames(expr)) if (r == n) return true; } catch (...) {}
    return false;
  };
  for (const auto& f : d.features) {
    bool u = false;
    for (const auto& [k, v] : f.props) if (isExprKey(k) && mentions(v)) u = true;
    if (f.sketch) {
      if (mentions(f.sketch->plane.offsetExpr)) u = true;
      for (const auto& c : f.sketch->constraints) if (!c.valueExpr.empty() && mentions(c.valueExpr)) u = true;
    }
    if (u) used << QString::fromStdString(f.id);
  }
  for (const auto& q : d.params.all()) if (q.name != n && mentions(q.expression)) used << "parametre " + QString::fromStdString(q.name);
  row(g, "Kullanıldığı yerler", used.isEmpty() ? QString("—") : used.join(", "), used.join("\n"));
}

void PropertiesPanel::showMulti(const QList<SelItem>& items) {
  auto* g = category(QString("%1 öğe seçili").arg(items.size()));
  std::map<int, int> perKind;
  for (const auto& it : items) ++perKind[int(it.kind)];
  for (const auto& [k, n] : perKind) row(g, selKindText(SelItem::Kind(k)), QString::number(n));
  bool nameable = true;
  for (const auto& it : items)
    if (!(it.kind == SelItem::Kind::Face || it.kind == SelItem::Kind::Body || (it.kind == SelItem::Kind::Edge && !it.name.startsWith("sketch:")))) nameable = false;
  if (nameable) nsSection(items);
  else row(g, "Ad", "Çeşitli (Varies)");
}

void PropertiesPanel::nsSection(const QList<SelItem>& items) {
  if (!hooks_.namedSelectionsOf || !hooks_.nameItems) return;
  const QStringList names = hooks_.namedSelectionsOf(items);
  auto* c = category("Adlandırılmış seçim");
  const bool body = items.size() == 1 && items.front().kind == SelItem::Kind::Body;   // the body has its own "Ad"
  textRow(c, body ? "Seçim adı" : "Ad", "nsName", names.join(", "), [this, items](const QString& to) { return hooks_.nameItems(items, to); });
  if (QWidget* e = editors_.value("nsName")) {
    if (auto* le = qobject_cast<QLineEdit*>(e)) le->setPlaceholderText("ad yazın + Enter (ör. Inlet)");
    e->setToolTip("Adlandırılmış seçim (Named Selection) adı. Yeni ad yazın + Enter: seçili öğelerden adlandırılmış seçim\n"
                  "oluşturur (ad varsa öğeleri ona ekler). Yalnız bu öğelerden oluşan seçimin adını değiştirmek için\n"
                  "üzerine yazın. Boş bırakmak öğeleri seçimlerinden çıkarır. Ctrl+G = Gruplar panelinde oluştur.");
  }
}

} // namespace pcd
