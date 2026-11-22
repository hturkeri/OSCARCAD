#include "MfgDialogs.h"
#include "Icons.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>

using namespace pc;

namespace pcd {

QString severityText(mfg::Severity s) {
  switch (s) {
    case mfg::Severity::Error: return "Hata";
    case mfg::Severity::Warning: return "Uyarı";
    default: return "Bilgi";
  }
}
QString domainText(mfg::Domain d) {
  switch (d) {
    case mfg::Domain::Additive: return "Katmanlı";
    case mfg::Domain::Conventional: return "Konvansiyonel";
    case mfg::Domain::Model: return "Model";
    default: return "Parametre";
  }
}

// ------------------------------------------------------------------------ settings dialog
struct MfgSettingsDialog::Impl {
  MfgSettings s;
  QCheckBox *amEn, *cvEn;
  QComboBox *amProc, *cvProc;
  QLineEdit *amMat, *cvMat;
  std::vector<std::pair<QDoubleSpinBox*, std::function<void(MfgSettings&, double)>>> setters;
  std::vector<std::pair<QDoubleSpinBox*, std::function<double(const MfgSettings&)>>> getters;
};

MfgSettingsDialog::~MfgSettingsDialog() { delete d; }

MfgSettingsDialog::MfgSettingsDialog(const MfgSettings& s, QWidget* parent) : QDialog(parent), d(new Impl) {
  d->s = s;
  setWindowTitle("İmalat Ayarları");
  setMinimumWidth(520);
  auto* top = new QVBoxLayout(this);
  auto* tabs = new QTabWidget(this);
  top->addWidget(tabs);

  auto addSpin = [&](QFormLayout* f, const QString& label, double mn, double mx, int dec,
                     std::function<double(const MfgSettings&)> get, std::function<void(MfgSettings&, double)> set, const QString& suffix = " mm") {
    auto* sp = new QDoubleSpinBox(this);
    sp->setRange(mn, mx);
    sp->setDecimals(dec);
    sp->setSuffix(suffix);
    sp->setValue(get(d->s));
    f->addRow(label, sp);
    d->getters.push_back({sp, get});
    d->setters.push_back({sp, set});
  };

  // ---- additive
  auto* am = new QWidget;
  auto* af = new QFormLayout(am);
  d->amEn = new QCheckBox("Katmanlı imalat denetimlerini etkinleştir");
  d->amEn->setChecked(s.am.enabled);
  af->addRow(d->amEn);
  d->amProc = new QComboBox;
  const AmProcess ams[] = {AmProcess::None, AmProcess::LPBF_Metal, AmProcess::EBM_Metal, AmProcess::DED_Metal, AmProcess::FDM_Polymer, AmProcess::SLA_Resin, AmProcess::SLS_Polymer};
  for (auto p : ams) d->amProc->addItem(toString(p), int(p));
  d->amProc->setCurrentIndex(d->amProc->findData(int(s.am.process)));
  af->addRow("Süreç (preset):", d->amProc);
  d->amMat = new QLineEdit(QString::fromStdString(s.am.material));
  af->addRow("Malzeme:", d->amMat);
#define AM(label, mn, mx, dec, field, suf) addSpin(af, label, mn, mx, dec, [](const MfgSettings& m) { return m.am.field; }, [](MfgSettings& m, double v) { m.am.field = v; }, suf)
  AM("Min. et kalınlığı:", 0, 100, 3, minWallThickness, " mm");
  AM("Min. öznitelik boyutu:", 0, 100, 3, minFeatureSize, " mm");
  AM("Min. delik çapı:", 0, 100, 3, minHoleDiameter, " mm");
  AM("Destek gerektirmeyen max yatay delik çapı:", 0, 200, 2, maxSelfSupportingHoleDia, " mm");
  AM("Min. çıkıntı (overhang) açısı:", 0, 90, 1, minOverhangAngle, " °");
  AM("Min. boşluk:", 0, 100, 3, minGap, " mm");
  AM("Max en/boy (narinlik) oranı:", 0, 1000, 1, maxAspectRatio, "");
  AM("Min. toz/reçine tahliye deliği:", 0, 100, 2, minEscapeHoleDia, " mm");
  AM("Yapı hacmi X:", 0, 5000, 1, buildX, " mm");
  AM("Yapı hacmi Y:", 0, 5000, 1, buildY, " mm");
  AM("Yapı hacmi Z:", 0, 5000, 1, buildZ, " mm");
  AM("İnşa yönü X:", -1, 1, 3, buildDirX, "");
  AM("İnşa yönü Y:", -1, 1, 3, buildDirY, "");
  AM("İnşa yönü Z:", -1, 1, 3, buildDirZ, "");
  AM("Katman kalınlığı:", 0, 5, 4, layerThickness, " mm");
#undef AM
  tabs->addTab(am, "Katmanlı imalat");

  // ---- conventional
  auto* cv = new QWidget;
  auto* cf = new QFormLayout(cv);
  d->cvEn = new QCheckBox("Konvansiyonel imalat denetimlerini etkinleştir");
  d->cvEn->setChecked(s.conv.enabled);
  cf->addRow(d->cvEn);
  d->cvProc = new QComboBox;
  const ConvProcess cvs[] = {ConvProcess::None, ConvProcess::CNC_Milling3Axis, ConvProcess::CNC_Milling5Axis, ConvProcess::CNC_Turning};
  for (auto p : cvs) d->cvProc->addItem(toString(p), int(p));
  d->cvProc->setCurrentIndex(d->cvProc->findData(int(s.conv.process)));
  cf->addRow("Süreç (preset):", d->cvProc);
  d->cvMat = new QLineEdit(QString::fromStdString(s.conv.material));
  cf->addRow("Malzeme:", d->cvMat);
  auto* drillChk = new QCheckBox("Standart matkap çaplarını tercih et");
  drillChk->setChecked(s.conv.preferStandardDrills);
  cf->addRow(drillChk);
  drillChk->setObjectName("stdDrill");
#define CV(label, mn, mx, dec, field, suf) addSpin(cf, label, mn, mx, dec, [](const MfgSettings& m) { return m.conv.field; }, [](MfgSettings& m, double v) { m.conv.field = v; }, suf)
  CV("Min. takım çapı:", 0, 100, 2, minToolDiameter, " mm");
  CV("Max takım boyu / çap:", 0, 100, 1, maxToolLengthToDia, "");
  CV("Max delik derinliği / çap:", 0, 100, 1, maxHoleDepthToDia, "");
  CV("Min. et kalınlığı:", 0, 100, 2, minWallThickness, " mm");
  CV("Max duvar yüksekliği / kalınlık:", 0, 200, 1, maxWallHeightToThickness, "");
  CV("Min. delik çapı:", 0, 100, 2, minHoleDiameter, " mm");
  CV("Tezgah / ham parça X:", 0, 5000, 1, stockX, " mm");
  CV("Tezgah / ham parça Y:", 0, 5000, 1, stockY, " mm");
  CV("Tezgah / ham parça Z:", 0, 5000, 1, stockZ, " mm");
  CV("Takım ekseni X:", -1, 1, 3, toolAxisX, "");
  CV("Takım ekseni Y:", -1, 1, 3, toolAxisY, "");
  CV("Takım ekseni Z:", -1, 1, 3, toolAxisZ, "");
#undef CV
  tabs->addTab(cv, "Konvansiyonel imalat");

  // presets: reload limits when the process changes (user driven)
  auto reloadAll = [this] {
    for (size_t i = 0; i < d->getters.size(); ++i) d->getters[i].first->setValue(d->getters[i].second(d->s));
  };
  connect(d->amProc, QOverload<int>::of(&QComboBox::activated), this, [this, reloadAll](int) {
    const AmProcess p = AmProcess(d->amProc->currentData().toInt());
    d->s.am = mfg::amPreset(p);
    d->amMat->setText(QString::fromStdString(d->s.am.material));
    d->amEn->setChecked(d->s.am.enabled);
    reloadAll();
  });
  connect(d->cvProc, QOverload<int>::of(&QComboBox::activated), this, [this, reloadAll](int) {
    const ConvProcess p = ConvProcess(d->cvProc->currentData().toInt());
    d->s.conv = mfg::convPreset(p);
    d->cvMat->setText(QString::fromStdString(d->s.conv.material));
    d->cvEn->setChecked(d->s.conv.enabled);
    reloadAll();
  });
  auto* note = new QLabel("Süreç seçildiğinde sınır değerleri o sürecin tipik (preset) değerleriyle doldurulur; sonrasında elle değiştirebilirsiniz.");
  note->setWordWrap(true);
  top->addWidget(note);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  bb->button(QDialogButtonBox::Ok)->setText("Tamam");
  bb->button(QDialogButtonBox::Cancel)->setText("Vazgeç");
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  top->addWidget(bb);
}

MfgSettings MfgSettingsDialog::settings() const {
  MfgSettings s = d->s;
  for (auto& [sp, set] : d->setters) set(s, sp->value());
  s.am.enabled = d->amEn->isChecked();
  s.am.process = AmProcess(d->amProc->currentData().toInt());
  s.am.material = d->amMat->text().toStdString();
  s.conv.enabled = d->cvEn->isChecked();
  s.conv.process = ConvProcess(d->cvProc->currentData().toInt());
  s.conv.material = d->cvMat->text().toStdString();
  if (auto* c = findChild<QCheckBox*>("stdDrill")) s.conv.preferStandardDrills = c->isChecked();
  return s;
}

// ------------------------------------------------------------------------ review dialog
ChangeReviewDialog::ChangeReviewDialog(const mfg::Report& r, const QString& what, QWidget* parent) : QDialog(parent) {
  setWindowTitle("İmalat denetimi");
  setMinimumSize(960, 440);
  auto* top = new QVBoxLayout(this);
  auto* head = new QLabel(QString("<b>%1</b> imalat kurallarını ihlal ediyor. Değişiklik henüz uygulanmadı; aday geometri 3B görünümde yarı saydam kırmızı hayalet olarak gösteriliyor.").arg(what.toHtmlEscaped()));
  head->setWordWrap(true);
  top->addWidget(head);
  auto* t = new QTableWidget(int(r.issues.size()), 5);
  t->setHorizontalHeaderLabels({"Önem", "Alan", "Mesaj", "Parametre", "Önerilen"});
  t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->verticalHeader()->hide();
  int row = 0;
  bool anySuggestion = false;
  for (const auto& is : r.issues) {
    const QString ic = is.severity == mfg::Severity::Error ? "error" : is.severity == mfg::Severity::Warning ? "warn" : "info";
    auto* it0 = new QTableWidgetItem(makeIcon(ic, 16), severityText(is.severity));
    t->setItem(row, 0, it0);
    t->setItem(row, 1, new QTableWidgetItem(domainText(is.domain)));
    t->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(is.message)));
    t->item(row, 2)->setToolTip(QString::fromStdString(is.message));
    QStringList ps;
    for (const auto& p : is.params) ps << QString::fromStdString(p);
    t->setItem(row, 3, new QTableWidgetItem(ps.join(", ")));
    t->setItem(row, 4, new QTableWidgetItem(is.suggestedValue ? QString::number(*is.suggestedValue, 'g', 6) : QString("-")));
    if (is.suggestedValue && !is.params.empty()) anySuggestion = true;
    if (is.severity == mfg::Severity::Error)
      for (int c = 0; c < 5; ++c) t->item(row, c)->setBackground(QColor(255, 232, 230));
    ++row;
  }
  t->resizeColumnsToContents();
  t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  t->setWordWrap(true);
  t->setTextElideMode(Qt::ElideNone);
  t->resizeRowsToContents();
  top->addWidget(t, 1);
  auto* row2 = new QHBoxLayout;
  auto* apply = new QPushButton("Önerileri Uygula");
  auto* force = new QPushButton("Yine de Uygula");
  auto* cancel = new QPushButton("Vazgeç");
  apply->setEnabled(anySuggestion);
  apply->setDefault(true);
  row2->addStretch();
  row2->addWidget(apply);
  row2->addWidget(force);
  row2->addWidget(cancel);
  top->addLayout(row2);
  connect(apply, &QPushButton::clicked, this, [this] { choice_ = ApplySuggestions; accept(); });
  connect(force, &QPushButton::clicked, this, [this] { choice_ = ForceApply; accept(); });
  connect(cancel, &QPushButton::clicked, this, [this] { choice_ = Cancel; reject(); });
}

} // namespace pcd
