#include "ExprEdit.h"
#include "pc/Expression.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
#include <vector>

namespace pcd {

namespace {
const char* kNewItem = "__new__";

struct Hook { QPointer<QWidget> scope; ParameterCommitFn fn; };
std::vector<Hook>& hooks() {
  static std::vector<Hook> h;
  return h;
}
QString suggestUnique(const pc::Document* d, QString base) {
  if (base.isEmpty()) base = "P";
  QString clean;
  for (QChar ch : base) clean += (ch.isLetterOrNumber() && ch.unicode() < 128) || ch == '_' ? ch : QChar('_');
  if (clean.isEmpty() || clean[0].isDigit()) clean.prepend('P');
  if (!d->params.find(clean.toStdString())) return clean;
  for (int i = 2;; ++i) {
    const QString c = QString("%1_%2").arg(clean).arg(i);
    if (!d->params.find(c.toStdString())) return c;
  }
}
} // namespace

QString paramUnitText(pc::ParamKind k) {
  switch (k) {
    case pc::ParamKind::Length: return "mm";
    case pc::ParamKind::Angle: return "°";
    case pc::ParamKind::Count: return "adet";
    default: return "-";
  }
}
pc::ParamKind paramKindFromText(const QString& t) {
  if (t == "mm") return pc::ParamKind::Length;
  if (t == "°" || t.startsWith("der")) return pc::ParamKind::Angle;
  if (t == "adet") return pc::ParamKind::Count;
  return pc::ParamKind::Scalar;
}

void installParameterCommitHook(QWidget* scope, ParameterCommitFn fn) {
  removeParameterCommitHook(scope);
  hooks().push_back({scope, std::move(fn)});
}
void removeParameterCommitHook(QWidget* scope) {
  auto& h = hooks();
  for (size_t i = 0; i < h.size();)
    if (h[i].scope.isNull() || h[i].scope == scope) h.erase(h.begin() + long(i)); else ++i;
}
void commitNewParameter(QWidget* from, const pc::Parameter& p) {
  for (QWidget* w = from; w; w = w->parentWidget())
    for (const auto& h : hooks())
      if (h.scope == w && h.fn) { h.fn(p); return; }
}

// ------------------------------------------------------------------------------------- ExprEdit
ExprEdit::ExprEdit(pc::Document* work, QWidget* parent) : QWidget(parent), work_(work) {
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4);
  edit_ = new QLineEdit(this);
  edit_->setObjectName("exprLine");
  edit_->setMinimumWidth(90);
  edit_->setToolTip("Sayı, ifade (örn. W/2 + 3) veya parametre adı. Parametre adları otomatik tamamlanır.");
  preview_ = new QLabel(this);
  preview_->setMinimumWidth(78);
  pick_ = new QComboBox(this);
  pick_->setObjectName("paramPick");
  pick_->setToolTip("Bu alanı mevcut bir parametreye bağla (ad = değer) veya yeni parametre oluştur");
  pick_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  pick_->setMinimumContentsLength(13);
  newBtn_ = new QToolButton(this);
  newBtn_->setObjectName("paramNew");
  newBtn_->setText("Yeni…");
  newBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  newBtn_->setToolTip("Yeni parametre oluştur ve bu alanı ona bağla (ad, değer, min/maks, birim, uygulamada göster)");
  lay->addWidget(edit_, 1);
  lay->addWidget(preview_);
  lay->addWidget(pick_);
  lay->addWidget(newBtn_);
  model_ = new QStringListModel(this);
  completer_ = new QCompleter(model_, this);
  completer_->setWidget(edit_);
  completer_->setCaseSensitivity(Qt::CaseInsensitive);
  completer_->setCompletionMode(QCompleter::PopupCompletion);
  connect(edit_, &QLineEdit::textEdited, this, &ExprEdit::onTextEdited);
  connect(edit_, &QLineEdit::textChanged, this, [this] { evaluate(); syncPicker(); emit textChanged(); });
  connect(completer_, QOverload<const QString&>::of(&QCompleter::activated), this, &ExprEdit::onActivated);
  connect(newBtn_, &QToolButton::clicked, this, &ExprEdit::createParameter);
  connect(pick_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExprEdit::onPicked);
  refreshParams();
  evaluate();
}

void ExprEdit::setText(const QString& t) { edit_->setText(t); }

void ExprEdit::setAllowNewParam(bool on) {
  allowNew_ = on;
  newBtn_->setVisible(on && pick_->isVisibleTo(this));
  refreshParams();
}
void ExprEdit::setPickerVisible(bool on) {
  pick_->setVisible(on);
  newBtn_->setVisible(on && allowNew_);
}

QString ExprEdit::boundParameter() const {
  try {
    const auto id = pc::singleIdentifier(text().toStdString());
    if (id && work_->params.find(*id)) return QString::fromStdString(*id);
  } catch (...) {}
  return QString();
}

void ExprEdit::refreshParams() {
  QStringList names;
  for (const auto& p : work_->params.all()) names << QString::fromStdString(p.name);
  model_->setStringList(names);
  pc::ParamValues vals;
  try { vals = work_->evaluateParams(); } catch (...) {}
  {
    syncing_ = true;
    QSignalBlocker b(pick_);
    pick_->clear();
    pick_->addItem("— değer / ifade —", QString());
    for (const auto& p : work_->params.all()) {
      auto it = vals.find(p.name);
      const QString v = it != vals.end() ? QString::number(it->second, 'g', 7) : QString("?");
      const QString u = paramUnitText(p.kind);
      pick_->addItem(QString("%1 = %2%3").arg(QString::fromStdString(p.name), v, u == "-" ? QString() : " " + u),
                     QString::fromStdString(p.name));
      QString tip = QString::fromStdString(p.expression);
      if (!p.label.empty()) tip += "  —  " + QString::fromStdString(p.label);
      if (p.exposed) tip += "  (uygulamada gösteriliyor)";
      pick_->setItemData(pick_->count() - 1, tip, Qt::ToolTipRole);
    }
    if (allowNew_) {
      pick_->insertSeparator(pick_->count());
      pick_->addItem("Yeni parametre…", QString(kNewItem));
    }
    syncing_ = false;
  }
  evaluate();
  syncPicker();
}

void ExprEdit::syncPicker() {
  const QString b = boundParameter();
  const int idx = b.isEmpty() ? 0 : pick_->findData(b);
  if (pick_->currentIndex() == idx) return;
  syncing_ = true;
  QSignalBlocker blk(pick_);
  pick_->setCurrentIndex(idx < 0 ? 0 : idx);
  syncing_ = false;
}

void ExprEdit::onPicked(int index) {
  if (syncing_ || index < 0) return;
  const QString d = pick_->itemData(index).toString();
  if (d == kNewItem) {
    syncPicker();   // show the current binding again while the dialog is open
    createParameter();
    return;
  }
  if (d.isEmpty()) {   // unbind: replace a bare parameter name by its current value
    if (!boundParameter().isEmpty() && valid_) edit_->setText(QString::fromStdString(pc::formatNumber(value_)));
    return;
  }
  edit_->setText(d);
}

void ExprEdit::onTextEdited(const QString& t) {
  const int cur = edit_->cursorPosition();
  int s = cur;
  while (s > 0 && (t[s - 1].isLetterOrNumber() || t[s - 1] == '_')) --s;
  const QString prefix = t.mid(s, cur - s);
  if (prefix.isEmpty() || prefix[0].isDigit()) { completer_->popup()->hide(); return; }
  completer_->setCompletionPrefix(prefix);
  if (completer_->completionCount() > 0) completer_->complete();
  else completer_->popup()->hide();
}

void ExprEdit::onActivated(const QString& word) {
  QString t = edit_->text();
  const int cur = edit_->cursorPosition();
  int s = cur;
  while (s > 0 && (t[s - 1].isLetterOrNumber() || t[s - 1] == '_')) --s;
  t.replace(s, cur - s, word);
  edit_->setText(t);
  edit_->setCursorPosition(s + word.size());
}

void ExprEdit::evaluate() {
  valid_ = false;
  const std::string ex = edit_->text().trimmed().toStdString();
  if (ex.empty()) { preview_->setText(""); return; }
  try {
    pc::ParamValues vals = work_->evaluateParams();
    value_ = pc::evalWith(ex, vals);
    valid_ = true;
    preview_->setText(QString("= %1").arg(value_, 0, 'g', 7));
    preview_->setStyleSheet("color: #1b7a34;");
    preview_->setToolTip(QString());
  } catch (const std::exception& e) {
    preview_->setText("hata");
    preview_->setStyleSheet("color: #c0302a;");
    preview_->setToolTip(QString::fromUtf8(e.what()));
  }
}

void ExprEdit::createParameter() {
  const QString initial = valid_ && boundParameter().isEmpty() ? text() : (valid_ ? QString::fromStdString(pc::formatNumber(value_)) : QString("10"));
  NewParameterDialog dlg(work_, hintName_, hintKind_, initial, this);
  if (dlg.exec() != QDialog::Accepted) return;
  const pc::Parameter p = dlg.parameter();
  if (!work_->params.find(p.name)) work_->params.add(p);
  commitNewParameter(this, p);   // the owning window commits it to the real document (parameter table)
  refreshParams();
  edit_->setText(QString::fromStdString(p.name));
  syncPicker();
  emit parameterCreated(QString::fromStdString(p.name));
}

// ------------------------------------------------------------------------------ NewParameterDialog
NewParameterDialog::NewParameterDialog(pc::Document* work, const QString& suggestedName, pc::ParamKind kind, const QString& valueExpr,
                                       QWidget* parent)
    : QDialog(parent), work_(work) {
  setWindowTitle("Yeni parametre");
  setObjectName("newParameterDialog");
  auto* top = new QVBoxLayout(this);
  auto* intro = new QLabel("Alan bu parametreye bağlanır; parametre tablosunda hemen görünür ve oradan değiştirilebilir.", this);
  intro->setWordWrap(true);
  intro->setStyleSheet("color:#375a8c;");
  top->addWidget(intro);
  auto* form = new QFormLayout;
  top->addLayout(form);
  name_ = new QLineEdit(suggestUnique(work, suggestedName), this);
  name_->setObjectName("npName");
  name_->setToolTip("Harf veya _ ile başlayan, harf/rakam/_ içeren ad (örn. T, Kalinlik, Ext1_L)");
  form->addRow("Ad:", name_);
  value_ = new ExprEdit(work, this);
  value_->setObjectName("npValue");
  value_->setPickerVisible(false);
  value_->setText(valueExpr.isEmpty() ? QString("10") : valueExpr);
  form->addRow("Değer / ifade:", value_);
  kind_ = new QComboBox(this);
  kind_->setObjectName("npKind");
  kind_->addItem("mm (uzunluk)", "mm");
  kind_->addItem("° (açı)", "°");
  kind_->addItem("adet (sayı)", "adet");
  kind_->addItem("- (birimsiz)", "-");
  kind_->setCurrentIndex(std::max(0, kind_->findData(paramUnitText(kind))));
  form->addRow("Birim:", kind_);
  auto* rng = new QHBoxLayout;
  min_ = new QLineEdit(this);
  min_->setObjectName("npMin");
  min_->setPlaceholderText("sınırsız");
  max_ = new QLineEdit(this);
  max_->setObjectName("npMax");
  max_->setPlaceholderText("sınırsız");
  rng->addWidget(new QLabel("Min:", this));
  rng->addWidget(min_, 1);
  rng->addWidget(new QLabel("Maks:", this));
  rng->addWidget(max_, 1);
  form->addRow("Aralık:", rng);
  auto* g = new QGroupBox("Uygulama (Save as App)", this);
  auto* gf = new QFormLayout(g);
  exposed_ = new QCheckBox("Uygulamada göster (son kullanıcı değiştirebilir)", g);
  exposed_->setObjectName("npExposed");
  label_ = new QLineEdit(g);
  label_->setObjectName("npLabel");
  label_->setPlaceholderText("örn. Plaka kalınlığı (boşsa ad kullanılır)");
  group_ = new QLineEdit(g);
  group_->setObjectName("npGroup");
  group_->setPlaceholderText("örn. Boyutlar");
  gf->addRow(exposed_);
  gf->addRow("Etiket:", label_);
  gf->addRow("Grup:", group_);
  top->addWidget(g);
  desc_ = new QLineEdit(this);
  desc_->setObjectName("npDesc");
  auto* f2 = new QFormLayout;
  f2->addRow("Açıklama:", desc_);
  top->addLayout(f2);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  bb->button(QDialogButtonBox::Ok)->setText("Oluştur");
  bb->button(QDialogButtonBox::Cancel)->setText("Vazgeç");
  connect(bb, &QDialogButtonBox::accepted, this, &NewParameterDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(bb);
  setMinimumWidth(460);
  name_->setFocus();
  name_->selectAll();
}

pc::Parameter NewParameterDialog::parameter() const {
  pc::Parameter p;
  p.name = name_->text().trimmed().toStdString();
  p.expression = value_->text().toStdString();
  p.kind = paramKindFromText(kind_->currentData().toString());
  auto num = [](const QLineEdit* e, double def) {
    QString t = e->text().trimmed();
    if (t.isEmpty()) return def;
    bool ok = false;
    const double v = t.replace(',', '.').toDouble(&ok);
    return ok ? v : def;
  };
  p.minValue = num(min_, -std::numeric_limits<double>::infinity());
  p.maxValue = num(max_, std::numeric_limits<double>::infinity());
  p.exposed = exposed_->isChecked();
  p.label = label_->text().trimmed().toStdString();
  p.group = group_->text().trimmed().toStdString();
  p.description = desc_->text().trimmed().toStdString();
  return p;
}

void NewParameterDialog::accept() {
  const std::string n = name_->text().trimmed().toStdString();
  if (!pc::ParamTable::isValidName(n)) {
    QMessageBox::warning(this, "Yeni parametre", "Geçersiz parametre adı (harf veya _ ile başlamalı; harf, rakam, _ içerebilir).");
    return;
  }
  if (work_->params.find(n)) {
    QMessageBox::warning(this, "Yeni parametre", QString("'%1' adında bir parametre zaten var.").arg(QString::fromStdString(n)));
    return;
  }
  if (!value_->isValid()) {
    QMessageBox::warning(this, "Yeni parametre", "Değer / ifade geçersiz.");
    return;
  }
  try {
    for (const auto& r : pc::referencedNames(value_->text().toStdString()))
      if (r == n) { QMessageBox::warning(this, "Yeni parametre", "Parametre kendi ifadesinde kullanılamaz."); return; }
  } catch (...) {}
  for (QLineEdit* e : {min_, max_}) {
    QString t = e->text().trimmed();
    bool ok = true;
    if (!t.isEmpty()) t.replace(',', '.').toDouble(&ok);
    if (!ok) { QMessageBox::warning(this, "Yeni parametre", "Min / Maks sayı olmalı (boş = sınırsız)."); return; }
  }
  const pc::Parameter p = parameter();
  if (p.minValue > p.maxValue) { QMessageBox::warning(this, "Yeni parametre", "Min, Maks'tan büyük olamaz."); return; }
  if (value_->value() < p.minValue || value_->value() > p.maxValue) {
    QMessageBox::warning(this, "Yeni parametre", "Değer, verilen Min/Maks aralığının dışında.");
    return;
  }
  QDialog::accept();
}

// ---------------------------------------------------------------------------------
DimensionDialog::DimensionDialog(pc::Document* work, const QString& title, const QString& expr, bool isAngle, QWidget* parent)
    : QDialog(parent), work_(work), angle_(isAngle) {
  setWindowTitle(title);
  auto* form = new QFormLayout(this);
  edit_ = new ExprEdit(work, this);
  edit_->setObjectName("dimValue");
  edit_->setParamHint(isAngle ? "A" : "L", isAngle ? pc::ParamKind::Angle : pc::ParamKind::Length);
  edit_->setText(expr);
  edit_->setMinimumWidth(460);
  form->addRow(isAngle ? "Değer (derece / ifade):" : "Değer (mm / ifade):", edit_);
  auto* hint = new QLabel("Sayı yazın, sağdaki listeden bir parametre seçin ya da aşağıda yeni bir parametre oluşturun.", this);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#375a8c;");
  form->addRow(hint);
  newParam_ = new QCheckBox("Bu değerle yeni adlandırılmış parametre oluştur", this);
  newParam_->setObjectName("dimNewParam");
  name_ = new QLineEdit(this);
  name_->setObjectName("dimParamName");
  name_->setEnabled(false);
  name_->setPlaceholderText("örn. W");
  exposed_ = new QCheckBox("Uygulamada göster", this);
  exposed_->setObjectName("dimParamExposed");
  exposed_->setEnabled(false);
  connect(newParam_, &QCheckBox::toggled, name_, &QLineEdit::setEnabled);
  connect(newParam_, &QCheckBox::toggled, exposed_, &QCheckBox::setEnabled);
  form->addRow(newParam_);
  auto* nr = new QHBoxLayout;
  nr->addWidget(name_, 1);
  nr->addWidget(exposed_);
  form->addRow("Parametre adı:", nr);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  bb->button(QDialogButtonBox::Ok)->setText("Tamam");
  bb->button(QDialogButtonBox::Cancel)->setText("Vazgeç");
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  form->addRow(bb);
  edit_->lineEdit()->setFocus();
  edit_->lineEdit()->selectAll();
}

QString DimensionDialog::expression() const { return edit_->text(); }

void DimensionDialog::accept() {
  if (!edit_->isValid()) {
    QMessageBox::warning(this, "Ölçü", "İfade geçersiz.");
    return;
  }
  if (newParam_->isChecked()) {
    const std::string n = name_->text().trimmed().toStdString();
    if (!pc::ParamTable::isValidName(n) || work_->params.find(n)) {
      QMessageBox::warning(this, "Ölçü", "Parametre adı geçersiz veya zaten var.");
      return;
    }
    pc::Parameter p;
    p.name = n;
    p.expression = edit_->text().toStdString();
    p.kind = angle_ ? pc::ParamKind::Angle : pc::ParamKind::Length;
    p.exposed = exposed_->isChecked();
    work_->params.add(p);
    commitNewParameter(this, p);
    edit_->refreshParams();
    edit_->setText(QString::fromStdString(n));
  }
  QDialog::accept();
}

} // namespace pcd
