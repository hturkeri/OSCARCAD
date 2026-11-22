#include "FeatureDialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace pc;

namespace pcd {

namespace {
QString featureTitle(FeatureType t) {
  switch (t) {
    case FeatureType::Extrude: return "Extrude (Çıkıntı / Kesme)";
    case FeatureType::Revolve: return "Revolve (Döndürme)";
    case FeatureType::Hole: return "Delik";
    case FeatureType::Fillet: return "Fillet (Kenar yuvarlatma)";
    case FeatureType::Chamfer: return "Chamfer (Pah kırma)";
    case FeatureType::Shell: return "Shell (Kabuk)";
    case FeatureType::Pattern: return "Pattern (Çoğaltma)";
    case FeatureType::Plane: return "Düzlem (datum)";
    case FeatureType::Combine: return "Gövdeleri Birleştir (Combine)";
    default: return "Özellik";
  }
}
} // namespace

FeatureDialog::FeatureDialog(Document* work, const Feature& f, bool isNew, QWidget* parent, const QStringList& bodies)
    : QDialog(parent), work_(work), f_(f), new_(isNew), bodies_(bodies) {
  setWindowTitle(QString("%1 - %2").arg(featureTitle(f.type), QString::fromStdString(f.id)));
  setWindowFlag(Qt::Tool, true);
  setModal(false);
  build();
}

ExprEdit* FeatureDialog::addExpr(const QString& key, const QString& label, const QString& def) {
  auto* e = new ExprEdit(work_, this);
  const std::string k = key.toStdString();
  e->setObjectName("expr_" + key);
  e->setParamHint(QString::fromStdString(f_.id) + "_" + key, kindForKey(key));
  e->setText(QString::fromStdString(f_.prop(k, def.toStdString())));
  form_->addRow(label, e);
  exprs_[k] = e;
  // a parameter created from one field is offered at once by every other field's picker / completer
  connect(e, &ExprEdit::parameterCreated, this, [this] { refreshParams(); });
  return e;
}

pc::ParamKind FeatureDialog::kindForKey(const QString& key) {
  static const QStringList angles = {"draft", "angle", "csAngle", "tipAngle", "rotate"};
  if (angles.contains(key)) return ParamKind::Angle;
  if (key == "count") return ParamKind::Count;
  if (key == "t" || key.startsWith("dir")) return ParamKind::Scalar;
  return ParamKind::Length;
}

void FeatureDialog::refreshParams() {
  for (auto& [k, e] : exprs_) e->refreshParams();
}
QComboBox* FeatureDialog::addCombo(const QString& key, const QString& label, const QList<QPair<QString, QString>>& items, const QString& def) {
  auto* c = new QComboBox(this);
  for (const auto& it : items) c->addItem(it.first, it.second);
  const QString cur = QString::fromStdString(f_.prop(key.toStdString(), def.toStdString()));
  const int idx = c->findData(cur);
  c->setCurrentIndex(idx >= 0 ? idx : 0);
  form_->addRow(label, c);
  combos_[key.toStdString()] = c;
  return c;
}
QCheckBox* FeatureDialog::addCheck(const QString& key, const QString& label, bool def) {
  auto* c = new QCheckBox(label, this);
  const std::string v = f_.prop(key.toStdString(), def ? "1" : "0");
  c->setChecked(v == "1" || v == "true");
  form_->addRow(c);
  checks_[key.toStdString()] = c;
  return c;
}

void FeatureDialog::fillSketchCombo(QComboBox* cb) {
  cb->clear();
  for (const auto& ft : work_->features)
    if (ft.type == FeatureType::Sketch) cb->addItem(QString::fromStdString(ft.id), QString::fromStdString(ft.id));
  const int idx = cb->findData(QString::fromStdString(f_.prop("sketch")));
  if (idx >= 0) cb->setCurrentIndex(idx);
  else if (cb->count() > 0) cb->setCurrentIndex(cb->count() - 1);   // default: last sketch
}

void FeatureDialog::fillAxisCombo() {
  if (!axisCombo_) return;
  const QString prev = axisCombo_->currentData().toString();
  axisCombo_->clear();
  axisCombo_->addItem("Sketch U ekseni", "U");
  axisCombo_->addItem("Sketch V ekseni", "V");
  const std::string sk = combos_["sketch"]->currentData().toString().toStdString();
  if (const Feature* s = work_->feature(sk); s && s->sketch)
    for (const auto& e : s->sketch->entities)
      if (e.type == SkEntityType::Line) axisCombo_->addItem(QString("Çizgi %1%2").arg(e.id).arg(e.construction ? " (yapı)" : ""), QString::number(e.id));
  const QString want = prev.isEmpty() ? QString::fromStdString(f_.prop("axis", "U")) : prev;
  const int i = axisCombo_->findData(want);
  axisCombo_->setCurrentIndex(i >= 0 ? i : 0);
}

void FeatureDialog::addRefList(const QString& hint) {
  auto* lbl = new QLabel(hint, this);
  lbl->setWordWrap(true);
  lbl->setStyleSheet("color:#375a8c;");
  form_->addRow(lbl);
  refs_ = new QListWidget(this);
  refs_->setMinimumHeight(110);
  refs_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  for (const auto& r : f_.refs) refs_->addItem(QString::fromStdString(r));
  auto* row = new QHBoxLayout;
  auto* rm = new QPushButton("Seçiliyi kaldır", this);
  auto* clr = new QPushButton("Temizle", this);
  row->addWidget(rm);
  row->addWidget(clr);
  row->addStretch();
  connect(rm, &QPushButton::clicked, this, [this] { qDeleteAll(refs_->selectedItems()); emit refsChanged(); });
  connect(clr, &QPushButton::clicked, this, [this] { refs_->clear(); emit refsChanged(); });
  form_->addRow("Seçim listesi:", refs_);
  form_->addRow(row);
}

void FeatureDialog::build() {
  form_ = new QFormLayout;
  auto* top = new QVBoxLayout(this);
  top->addLayout(form_);
  setMinimumWidth(f_.type == FeatureType::Plane ? 640 : 600);
  {
    auto* hint = new QLabel("Sayısal alanlar: sayı / ifade yazın, sağdaki listeden bir parametre seçin veya "
                            "<b>Yeni…</b> ile parametre oluşturup alanı ona bağlayın.", this);
    hint->setObjectName("paramHint");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#375a8c;");
    form_->addRow(hint);
  }

  const bool hasBody = [&] {
    for (const auto& ft : work_->features)
      if (ft.id != f_.id && !ft.suppressed && (ft.type == FeatureType::Extrude || ft.type == FeatureType::Revolve)) return true;
    return false;
  }();
  const QList<QPair<QString, QString>> ops = {{"Birleştir (Add)", "join"}, {"Çıkar (Subtract)", "cut"}, {"Kesişim (Intersect)", "intersect"}, {"Ayrı gövde (New body)", "new"}};

  switch (f_.type) {
    case FeatureType::Extrude: {
      auto* sk = new QComboBox(this);
      fillSketchCombo(sk);
      form_->addRow("Sketch:", sk);
      combos_["sketch"] = sk;
      addExpr("distance", "Mesafe (mm):", "10");
      addCombo("direction", "Yön:", {{"Normal", "normal"}, {"Ters", "reverse"}, {"Simetrik", "symmetric"}}, "normal");
      opCombo_ = addCombo("op", "İşlem:", ops, hasBody ? "join" : "new");
      addExpr("draft", "Eğim açısı (°):", "0");
      addCheck("through", "Tümünden geçir (yalnız kesme)", false);
      buildBodyOps(true);
      break;
    }
    case FeatureType::Revolve: {
      auto* sk = new QComboBox(this);
      fillSketchCombo(sk);
      form_->addRow("Sketch:", sk);
      combos_["sketch"] = sk;
      axisCombo_ = new QComboBox(this);
      form_->addRow("Eksen:", axisCombo_);
      fillAxisCombo();
      connect(sk, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillAxisCombo(); });
      addExpr("angle", "Açı (°):", "360");
      opCombo_ = addCombo("op", "İşlem:", ops, hasBody ? "join" : "new");
      buildBodyOps(true);
      break;
    }
    case FeatureType::Hole: {
      auto* sk = new QComboBox(this);
      fillSketchCombo(sk);
      form_->addRow("Delik konumları (sketch):", sk);
      combos_["sketch"] = sk;
      auto* kind = addCombo("kind", "Tür:", {{"Basit", "simple"}, {"Havşalı (counterbore)", "counterbore"}, {"Konik havşa (countersink)", "countersink"}}, "simple");
      addExpr("diameter", "Çap (mm):", "6");
      auto* thr = addCheck("through", "Delik boydan boya", false);
      auto* depth = addExpr("depth", "Derinlik (mm):", "10");
      auto* cbd = addExpr("cbDiameter", "Havşa çapı (mm):", "12");
      auto* cbh = addExpr("cbDepth", "Havşa derinliği (mm):", "4");
      auto* csd = addExpr("csDiameter", "Konik havşa çapı (mm):", "12");
      auto* csa = addExpr("csAngle", "Konik havşa açısı (°):", "90");
      auto* tip = addExpr("tipAngle", "Uç açısı (°, 0 = düz):", "118");
      auto upd = [=] {
        const QString k = kind->currentData().toString();
        depth->setEnabled(!thr->isChecked());
        tip->setEnabled(!thr->isChecked());
        cbd->setEnabled(k == "counterbore"); cbh->setEnabled(k == "counterbore");
        csd->setEnabled(k == "countersink"); csa->setEnabled(k == "countersink");
      };
      connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, upd);
      connect(thr, &QCheckBox::toggled, this, upd);
      upd();
      buildBodyOps(false);
      break;
    }
    case FeatureType::Fillet:
      addExpr("radius", "Yarıçap (mm):", "2");
      addRefList("Kenar seçmek için 3B görünümde Ctrl+tıklayın. Bir yüze normal tıklamak o yüzün tüm kenarlarını ekler (face:...).");
      break;
    case FeatureType::Chamfer:
      addExpr("distance", "Pah mesafesi (mm):", "1");
      addRefList("Kenar seçmek için 3B görünümde Ctrl+tıklayın. Bir yüze normal tıklamak o yüzün tüm kenarlarını ekler (face:...).");
      break;
    case FeatureType::Shell:
      addExpr("thickness", "Et kalınlığı (mm):", "2");
      addCombo("direction", "Yön:", {{"İçeri", "inward"}, {"Dışarı", "outward"}}, "inward");
      addRefList("Açık bırakılacak (kaldırılacak) yüzleri 3B görünümde tıklayarak seçin.");
      break;
    case FeatureType::Pattern: {
      auto* kind = addCombo("kind", "Tür:", {{"Doğrusal", "linear"}, {"Dairesel", "circular"}}, "linear");
      auto* src = new QComboBox(this);
      for (const auto& ft : work_->features)
        if (ft.type == FeatureType::Extrude || ft.type == FeatureType::Hole) src->addItem(QString::fromStdString(ft.id), QString::fromStdString(ft.id));
      const int si = src->findData(QString::fromStdString(f_.prop("source")));
      if (si >= 0) src->setCurrentIndex(si); else if (src->count()) src->setCurrentIndex(src->count() - 1);
      form_->addRow("Kaynak özellik:", src);
      combos_["source"] = src;
      addExpr("count", "Adet:", "3");
      auto* sp = addExpr("spacing", "Aralık (mm):", "20");
      auto* dx = addExpr("dirX", "Yön X:", "1");
      auto* dy = addExpr("dirY", "Yön Y:", "0");
      auto* dz = addExpr("dirZ", "Yön Z:", "0");
      auto* an = addExpr("angle", "Toplam açı (°):", "360");
      auto* ax = addCombo("axis", "Eksen:", {{"X", "X"}, {"Y", "Y"}, {"Z", "Z"}}, "Z");
      auto upd = [=] {
        const bool lin = kind->currentData().toString() == "linear";
        sp->setEnabled(lin); dx->setEnabled(lin); dy->setEnabled(lin); dz->setEnabled(lin);
        an->setEnabled(!lin); ax->setEnabled(!lin);
      };
      connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, upd);
      upd();
      buildBodyOps(false);
      break;
    }
    case FeatureType::Plane: buildPlane(); break;
    case FeatureType::Combine: buildCombine(); break;
    default: break;
  }

  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  bb->button(QDialogButtonBox::Ok)->setText(new_ ? "Oluştur" : "Uygula");
  bb->button(QDialogButtonBox::Cancel)->setText("Vazgeç");
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(bb);

  // any edit -> changed() (live preview)
  for (auto& [k, e] : exprs_) connect(e, &ExprEdit::textChanged, this, &FeatureDialog::changed);
  for (auto& [k, c] : combos_) connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FeatureDialog::changed);
  for (auto& [k, c] : checks_) connect(c, &QCheckBox::toggled, this, &FeatureDialog::changed);
}

// ------------------------------------------------------------------------ v2: bodies / ops
QStringList FeatureDialog::checkedItems(const QListWidget* l) {
  QStringList out;
  if (!l) return out;
  for (int i = 0; i < l->count(); ++i)
    if (l->item(i)->checkState() == Qt::Checked) out << l->item(i)->data(Qt::UserRole).toString();
  return out;
}

QListWidget* FeatureDialog::addBodyChecklist(const QString& label, const QStringList& checked, int minH) {
  auto* l = new QListWidget(this);
  l->setObjectName("bodyList");
  l->setMinimumHeight(minH);
  l->setMaximumHeight(std::max(minH, 22 * std::min<int>(6, int(bodies_.size() + checked.size())) + 8));
  QStringList names = bodies_;
  for (const QString& c : checked) if (!names.contains(c)) names << c;   // keep stale references visible
  for (const QString& n : names) {
    auto* it = new QListWidgetItem(n, l);
    it->setData(Qt::UserRole, n);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(checked.contains(n) ? Qt::Checked : Qt::Unchecked);
  }
  form_->addRow(label, l);
  connect(l, &QListWidget::itemChanged, this, &FeatureDialog::changed);
  return l;
}

QStringList FeatureDialog::targetBodies() const { return checkedItems(targets_); }

void FeatureDialog::buildBodyOps(bool withOp) {
  QStringList cur;
  for (const auto& t : QString::fromStdString(f_.prop("targets")).split(',', Qt::SkipEmptyParts)) cur << t.trimmed();
  targets_ = addBodyChecklist("Hedef gövdeler\n(boş = otomatik):", cur);
  targets_->setObjectName("targetsList");
  targets_->setToolTip("İşlemin uygulanacağı gövdeler. Hiçbiri seçilmezse aracın değdiği tüm gövdeler otomatik seçilir.");
  if (withOp) {
    keepTool_ = new QCheckBox("Aracı ayrı gövde olarak koru", this);
    keepTool_->setObjectName("keepTool");
    keepTool_->setChecked(f_.prop("keepTool") == "1");
    keepTool_->setToolTip("Yalnız Çıkar / Kesişim işlemlerinde: araç katı ayrı bir gövde olarak kalır");
    form_->addRow(keepTool_);
    bodyName_ = new QLineEdit(QString::fromStdString(f_.prop("bodyName")), this);
    bodyName_->setObjectName("bodyName");
    bodyName_->setPlaceholderText(QString::fromStdString(f_.id));
    form_->addRow("Gövde adı:", bodyName_);
    connect(opCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateOpState(); });
    connect(keepTool_, &QCheckBox::toggled, this, [this] { updateOpState(); });
    updateOpState();
  }
}

void FeatureDialog::updateOpState() {
  if (!opCombo_) return;
  const QString op = opCombo_->currentData().toString();
  const bool isNew = op == "new";
  const bool cutLike = op == "cut" || op == "intersect";
  if (targets_) { targets_->setEnabled(!isNew); }
  if (keepTool_) keepTool_->setEnabled(cutLike);
  if (bodyName_) bodyName_->setEnabled(isNew || (cutLike && keepTool_ && keepTool_->isChecked()));
}

void FeatureDialog::buildCombine() {
  const QList<QPair<QString, QString>> ops = {{"Birleştir (Add)", "join"}, {"Çıkar (Subtract)", "cut"}, {"Kesişim (Intersect)", "intersect"}};
  addCombo("op", "İşlem:", ops, "join");
  targetCombo_ = new QComboBox(this);
  targetCombo_->setObjectName("targetBody");
  for (const QString& b : bodies_) targetCombo_->addItem(b, b);
  const int ti = targetCombo_->findData(QString::fromStdString(f_.prop("target")));
  if (ti >= 0) targetCombo_->setCurrentIndex(ti);
  form_->addRow("Hedef gövde:", targetCombo_);
  QStringList cur;
  for (const auto& t : QString::fromStdString(f_.prop("tools")).split(',', Qt::SkipEmptyParts)) cur << t.trimmed();
  tools_ = addBodyChecklist("Araç gövdeler:", cur, 90);
  tools_->setObjectName("toolsList");
  keepTools_ = new QCheckBox("Araç gövdeleri koru", this);
  keepTools_->setObjectName("keepTools");
  keepTools_->setChecked(f_.prop("keepTools") == "1");
  form_->addRow(keepTools_);
  auto sync = [this] {   // the target body cannot also be a tool
    const QString t = targetCombo_->currentData().toString();
    for (int i = 0; i < tools_->count(); ++i) {
      auto* it = tools_->item(i);
      const bool self = it->data(Qt::UserRole).toString() == t;
      if (self) it->setCheckState(Qt::Unchecked);
      it->setFlags(self ? (it->flags() & ~Qt::ItemIsEnabled) : (it->flags() | Qt::ItemIsEnabled));
    }
  };
  connect(targetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
  sync();
}

// ------------------------------------------------------------------------ v2: plane
QString FeatureDialog::planeKind() const { return kindCombo_ ? kindCombo_->currentData().toString() : QString(); }

void FeatureDialog::buildPlane() {
  kindCombo_ = addCombo("kind", "Tür:",
                        {{"Kenara dik düzlem (edgeNormal)", "edgeNormal"}, {"Kenardan açılı düzlem (edgeAngle)", "edgeAngle"},
                         {"İki kenardan düzlem (twoEdges)", "twoEdges"}, {"Yüzden ofset düzlem (faceOffset)", "faceOffset"},
                         {"Eksenden geçen açılı düzlem (axisAngle)", "axisAngle"}},
                        "edgeNormal");
  // axisAngle: plane through a world axis, rotated by "angle" about it (no model references needed)
  planeAxisCombo_ = addCombo("axis", "Eksen (axisAngle):", {{"X", "X"}, {"Y", "Y"}, {"Z", "Z"}}, "Z");
  auto* hint = new QLabel("Seç düğmesine basın, sonra 3B görünümde kenara Ctrl+tıklayın (yüz için normal tıklama).", this);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#375a8c;");
  form_->addRow(hint);
  const char* labels[3] = {"Kenar 1:", "Kenar 2:", "Yüz:"};
  const char* names[3] = {"refEdge1", "refEdge2", "refFace"};
  QStringList r;
  for (const auto& x : f_.refs) r << QString::fromStdString(x);
  QStringList init;
  const QString k = QString::fromStdString(f_.prop("kind", "edgeNormal"));
  if (k == "edgeNormal") init = {r.value(0), "", ""};
  else if (k == "edgeAngle") init = {r.value(0), "", r.value(1)};
  else if (k == "twoEdges") init = {r.value(0), r.value(1), ""};
  else if (k == "axisAngle") init = {"", "", ""};
  else init = {"", "", r.value(0)};
  for (int i = 0; i < 3; ++i) {
    refEdit_[i] = new QLineEdit(init[i], this);
    refEdit_[i]->setObjectName(names[i]);
    refEdit_[i]->setReadOnly(true);
    refEdit_[i]->setPlaceholderText(i < 2 ? "(kenar seçilmedi)" : "(yüz seçilmedi / otomatik)");
    pickBtn_[i] = new QPushButton("Seç", this);
    pickBtn_[i]->setObjectName(QString("pick%1").arg(i));
    pickBtn_[i]->setCheckable(true);
    auto* row = new QHBoxLayout;
    row->addWidget(refEdit_[i], 1);
    row->addWidget(pickBtn_[i]);
    form_->addRow(labels[i], row);
    connect(pickBtn_[i], &QPushButton::clicked, this, [this, i] {
      pickTarget_ = i;
      for (int j = 0; j < 3; ++j) pickBtn_[j]->setChecked(j == i);
    });
    connect(refEdit_[i], &QLineEdit::textChanged, this, &FeatureDialog::changed);
  }
  addExpr("t", "Kenar boyunca konum t (0..1):", "0.5");
  addExpr("angle", "Açı (°):", "45");
  addExpr("offset", "Öteleme (mm):", "0");
  addExpr("rotate", "Döndürme (°):", "0");
  addCheck("flip", "Normali ters çevir", false);
  addCheck("show", "3B görünümde göster (kapalıysa yalnız seçiliyken görünür)", true);
  previewLbl_ = new QLabel(this);
  previewLbl_->setObjectName("previewLabel");
  previewLbl_->setWordWrap(true);
  form_->addRow(previewLbl_);
  connect(kindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { setPlaneFields(); });
  setPlaneFields();
}

void FeatureDialog::setPlaneFields() {
  const QString k = planeKind();
  const bool ax = k == "axisAngle";
  const bool e1 = k != "faceOffset" && !ax, e2 = k == "twoEdges", fc = k == "edgeAngle" || k == "faceOffset";
  const bool en[3] = {e1, e2, fc};
  for (int i = 0; i < 3; ++i) {
    refEdit_[i]->setEnabled(en[i]);
    pickBtn_[i]->setEnabled(en[i]);
    if (!en[i]) refEdit_[i]->clear();
  }
  exprs_["t"]->setEnabled(k == "edgeNormal" || k == "edgeAngle");
  exprs_["angle"]->setEnabled(k == "edgeAngle" || ax);
  if (planeAxisCombo_) planeAxisCombo_->setEnabled(ax);
  pickTarget_ = e1 ? 0 : 2;
  for (int i : {0, 1, 2}) if (en[i] && refEdit_[i]->text().isEmpty()) { pickTarget_ = i; break; }
  if (ax) pickTarget_ = -1;   // nothing to pick
  for (int j = 0; j < 3; ++j) pickBtn_[j]->setChecked(j == pickTarget_);
  emit refsChanged();
  emit changed();
}

void FeatureDialog::planeEdgeFilled(int slot) {
  // after edge 1 of a twoEdges plane, pick edge 2 next
  int next = -1;
  if (slot == 0 && planeKind() == "twoEdges" && refEdit_[1]->text().isEmpty()) next = 1;
  if (next >= 0) { pickTarget_ = next; for (int j = 0; j < 3; ++j) pickBtn_[j]->setChecked(j == next); }
}

void FeatureDialog::setPreviewStatus(const QString& text, bool ok) {
  if (!previewLbl_) return;
  previewLbl_->setText(text);
  previewLbl_->setStyleSheet(ok ? "color:#157a2c;" : "color:#c0302a;");
}

bool FeatureDialog::wantsFaces() const {
  if (f_.type == FeatureType::Plane) { const QString k = planeKind(); return k == "edgeAngle" || k == "faceOffset"; }
  return f_.type == FeatureType::Shell || f_.type == FeatureType::Fillet || f_.type == FeatureType::Chamfer;
}
bool FeatureDialog::wantsEdges() const {
  if (f_.type == FeatureType::Plane) return planeKind() != "faceOffset" && planeKind() != "axisAngle";
  return f_.type == FeatureType::Fillet || f_.type == FeatureType::Chamfer;
}

void FeatureDialog::addFaceRef(const QString& name) {
  if (f_.type == FeatureType::Plane) {
    if (name.isEmpty() || !wantsFaces()) return;
    refEdit_[2]->setText(name);
    emit refsChanged();
    return;
  }
  if (!refs_ || name.isEmpty()) return;
  const QString entry = (f_.type == FeatureType::Shell) ? name : "face:" + name;
  if (refs_->findItems(entry, Qt::MatchExactly).isEmpty()) refs_->addItem(entry);
  emit refsChanged();
}
void FeatureDialog::addEdgeRef(const QString& name) {
  if (f_.type == FeatureType::Plane) {
    if (name.isEmpty() || !wantsEdges()) return;
    int slot = pickTarget_;
    if (slot < 0 || slot > 1) slot = 0;
    if (slot == 1 && planeKind() != "twoEdges") slot = 0;
    refEdit_[slot]->setText(name);
    planeEdgeFilled(slot);
    emit refsChanged();
    return;
  }
  if (!refs_ || name.isEmpty() || f_.type == FeatureType::Shell) return;
  if (refs_->findItems(name, Qt::MatchExactly).isEmpty()) refs_->addItem(name);
  emit refsChanged();
}
QStringList FeatureDialog::refFaces() const {
  QStringList l;
  if (f_.type == FeatureType::Plane) { if (refEdit_[2] && !refEdit_[2]->text().isEmpty()) l << refEdit_[2]->text(); return l; }
  if (!refs_) return l;
  for (int i = 0; i < refs_->count(); ++i) {
    const QString t = refs_->item(i)->text();
    if (f_.type == FeatureType::Shell) l << t;
    else if (t.startsWith("face:")) l << t.mid(5);
  }
  return l;
}
QStringList FeatureDialog::refEdges() const {
  QStringList l;
  if (f_.type == FeatureType::Plane) {
    for (int i = 0; i < 2; ++i) if (refEdit_[i] && refEdit_[i]->isEnabled() && !refEdit_[i]->text().isEmpty()) l << refEdit_[i]->text();
    return l;
  }
  if (!refs_ || f_.type == FeatureType::Shell) return l;
  for (int i = 0; i < refs_->count(); ++i)
    if (!refs_->item(i)->text().startsWith("face:")) l << refs_->item(i)->text();
  return l;
}

void FeatureDialog::accept() {
  for (const auto& [k, e] : exprs_) {
    if (e->isEnabled() && !e->isValid()) {
      QMessageBox::warning(this, "Özellik", QString("'%1' alanındaki ifade geçersiz.").arg(QString::fromStdString(k)));
      return;
    }
  }
  if (f_.type == FeatureType::Plane) {
    const QString k = planeKind();
    QString miss;
    if (k != "faceOffset" && k != "axisAngle" && refEdit_[0]->text().isEmpty()) miss = "Kenar seçin (3B görünümde Ctrl+tıklama).";
    if (k == "twoEdges" && refEdit_[1]->text().isEmpty()) miss = "İkinci kenarı da seçin.";
    if (k == "faceOffset" && refEdit_[2]->text().isEmpty()) miss = "Düzlemsel bir yüz seçin.";
    if (!miss.isEmpty()) { QMessageBox::warning(this, "Düzlem", miss); return; }
  }
  if (f_.type == FeatureType::Combine) {
    if (!targetCombo_ || targetCombo_->count() == 0 || checkedItems(tools_).isEmpty()) {
      QMessageBox::warning(this, "Combine", "Bir hedef gövde ve en az bir araç gövde seçin.");
      return;
    }
  }
  if (combos_.count("sketch") && combos_["sketch"]->count() == 0) {
    QMessageBox::warning(this, "Özellik", "Önce bir sketch oluşturmalısınız.");
    return;
  }
  if ((f_.type == FeatureType::Fillet || f_.type == FeatureType::Chamfer) && refs_->count() == 0) {
    QMessageBox::warning(this, "Özellik", "En az bir kenar veya yüz seçin.");
    return;
  }
  if (f_.type == FeatureType::Shell && refs_->count() == 0) {
    if (QMessageBox::question(this, "Shell", "Açık yüz seçilmedi: kapalı (içi boş) kabuk oluşturulacak. Devam?") != QMessageBox::Yes) return;
  }
  QDialog::accept();
}

Feature FeatureDialog::feature() const {
  Feature out = f_;
  out.props.clear();
  for (const auto& [k, e] : exprs_) if (e->isEnabled()) out.props[k] = e->text().toStdString();
  for (const auto& [k, c] : combos_) if (c->isEnabled()) out.props[k] = c->currentData().toString().toStdString();
  for (const auto& [k, c] : checks_) out.props[k] = c->isChecked() ? "1" : "0";
  if (axisCombo_) out.props["axis"] = axisCombo_->currentData().toString().toStdString();
  out.refs.clear();
  if (f_.type == FeatureType::Plane) {
    const QString k = planeKind();
    auto add = [&](int i) { if (!refEdit_[i]->text().isEmpty()) out.refs.push_back(refEdit_[i]->text().toStdString()); };
    if (k == "edgeNormal") add(0);
    else if (k == "edgeAngle") { add(0); add(2); }
    else if (k == "twoEdges") { add(0); add(1); }
    else if (k == "faceOffset") add(2);
    // axisAngle: no refs
  }
  if (f_.type == FeatureType::Combine) {
    out.props["target"] = targetCombo_->currentData().toString().toStdString();
    out.props["tools"] = checkedItems(tools_).join(",").toStdString();
    out.props["keepTools"] = keepTools_->isChecked() ? "1" : "0";
  }
  // v2 body props that this dialog has no widget for (Pattern/Hole: op, bodyName, keepTool; Shell: targets)
  // are carried over unchanged: editing e.g. a Pattern(op=new, bodyName=X) must not rename/merge its body.
  for (const char* k : {"op", "targets", "bodyName", "keepTool"}) {
    const bool managed = std::string(k) == "op" ? combos_.count("op") > 0
                         : std::string(k) == "targets" ? targets_ != nullptr
                         : std::string(k) == "bodyName" ? bodyName_ != nullptr
                                                        : keepTool_ != nullptr;
    if (!managed && f_.props.count(k) && !out.props.count(k)) out.props[k] = f_.props.at(k);
  }
  if (targets_) {
    out.props.erase("targets");
    if (bodyName_) out.props.erase("bodyName");
    if (keepTool_) out.props.erase("keepTool");
    const QStringList t = checkedItems(targets_);
    if (targets_->isEnabled() && !t.isEmpty()) out.props["targets"] = t.join(",").toStdString();
    if (keepTool_ && keepTool_->isEnabled() && keepTool_->isChecked()) out.props["keepTool"] = "1";
    if (bodyName_ && bodyName_->isEnabled() && !bodyName_->text().trimmed().isEmpty())
      out.props["bodyName"] = bodyName_->text().trimmed().toStdString();
  }
  if (refs_) for (int i = 0; i < refs_->count(); ++i) out.refs.push_back(refs_->item(i)->text().toStdString());
  return out;
}

} // namespace pcd
