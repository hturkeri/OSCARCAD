#include "pc/SaveAsAppDialog.h"
#include "pc/AppLint.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace pc::appui {

namespace {
enum Col { cShow, cName, cLabel, cMin, cMax, cStep, cGroup, cCount };

QString fmt(double v) { return std::isfinite(v) ? QString::number(v, 'g', 10) : QString(); }
}

struct SaveAsAppDialog::Impl {
  Document& doc;
  QString runner;
  QString outExe, outPkg;

  QLineEdit *name, *version, *author, *outPath;
  QPlainTextEdit *desc, *lintView;
  QTableWidget* table;
  QListWidget* nsList = nullptr;   // named selections (optional section): ticked = written as STEP face names
  QCheckBox *chSTL, *chSTEP, *chBlock, *chLive, *chAM, *chCNC;
  QRadioButton *rbExe, *rbPkg, *rbBoth;

  explicit Impl(Document& d) : doc(d) {}

  // Parse table into `params` (copy of doc params). Returns error text on invalid input.
  QString readTable(ParamTable& pt) const {
    for (int r = 0; r < table->rowCount(); ++r) {
      QString pn = table->item(r, cName)->text();
      Parameter* p = pt.find(pn.toStdString());
      if (!p) continue;
      p->exposed = table->item(r, cShow)->checkState() == Qt::Checked;
      p->label = table->item(r, cLabel)->text().trimmed().toStdString();
      p->group = table->item(r, cGroup)->text().trimmed().toStdString();
      auto num = [&](int c, double def, bool& ok) {
        QString t = table->item(r, c)->text().trimmed();
        if (t.isEmpty()) { ok = true; return def; }
        return t.replace(',', '.').toDouble(&ok);
      };
      bool ok1, ok2, ok3;
      p->minValue = num(cMin, -std::numeric_limits<double>::infinity(), ok1);
      p->maxValue = num(cMax, std::numeric_limits<double>::infinity(), ok2);
      p->step = num(cStep, 0.0, ok3);
      if (!ok1 || !ok2 || !ok3) return QString("'%1' parametresinde geçersiz sayı girildi.").arg(pn);
    }
    return {};
  }

  pc::app::AppDefinition build(const ParamTable& pt) const {
    pc::app::AppDefinition a;
    a.document = doc;
    a.document.params = pt;
    applyNamedSelections(a.document);
    a.appName = name->text().trimmed().toStdString();
    a.version = version->text().trimmed().toStdString();
    a.author = author->text().trimmed().toStdString();
    a.description = desc->toPlainText().toStdString();
    a.allowExportSTL = chSTL->isChecked();
    a.allowExportSTEP = chSTEP->isChecked();
    a.blockOnErrors = chBlock->isChecked();
    a.liveUpdate = chLive->isChecked();
    a.showAdditiveChecks = chAM->isChecked();
    a.showConventionalChecks = chCNC->isChecked();
    return a;
  }

  void applyNamedSelections(Document& target) const {
    if (!nsList) return;
    for (int i = 0; i < nsList->count() && i < int(target.namedSelections.size()); ++i)
      target.namedSelections[size_t(i)].exportStep = nsList->item(i)->checkState() == Qt::Checked;
  }

  void computePaths(QString& exe, QString& pkg) const {
    QString base = outPath->text().trimmed();
    if (base.endsWith(".pcapp", Qt::CaseInsensitive)) base.chop(6);
    exe = base;
#ifdef Q_OS_WIN
    if (!exe.endsWith(".exe", Qt::CaseInsensitive)) exe += ".exe";
#endif
    pkg = base + ".pcapp";
  }
};

SaveAsAppDialog::SaveAsAppDialog(Document& doc, const QString& runnerExePath, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>(doc)) {
  d->runner = runnerExePath;
  setWindowTitle("Uygulama Olarak Kaydet (Save as App)");
  resize(1000, 880);

  auto* root = new QVBoxLayout(this);

  // --- General info
  auto* gInfo = new QGroupBox("Uygulama bilgileri");
  auto* form = new QFormLayout(gInfo);
  d->name = new QLineEdit(QString::fromStdString(doc.name));
  d->version = new QLineEdit("1.0.0");
  d->author = new QLineEdit;
  d->desc = new QPlainTextEdit;
  d->desc->setFixedHeight(46);
  form->addRow("Uygulama adı:", d->name);
  form->addRow("Sürüm:", d->version);
  form->addRow("Yazar:", d->author);
  form->addRow("Açıklama:", d->desc);
  root->addWidget(gInfo);

  // --- Parameter table
  auto* gPar = new QGroupBox("Parametreler (uygulamada son kullanıcıya açılacaklar)");
  auto* vp = new QVBoxLayout(gPar);
  d->table = new QTableWidget(static_cast<int>(doc.params.all().size()), cCount);
  d->table->setHorizontalHeaderLabels({"Uygulamada göster", "Parametre", "Etiket", "Min", "Maks", "Adım", "Grup"});
  d->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  d->table->horizontalHeader()->setStretchLastSection(true);
  d->table->verticalHeader()->setVisible(false);
  d->table->setAlternatingRowColors(true);
  int row = 0;
  for (const auto& p : doc.params.all()) {
    auto mk = [&](int c, const QString& t, bool editable) {
      auto* it = new QTableWidgetItem(t);
      if (!editable) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
      d->table->setItem(row, c, it);
      return it;
    };
    auto* chk = mk(cShow, "", false);
    chk->setFlags((chk->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    chk->setCheckState(p.exposed ? Qt::Checked : Qt::Unchecked);
    mk(cName, QString::fromStdString(p.name), false);
    mk(cLabel, QString::fromStdString(p.label.empty() ? p.name : p.label), true);
    mk(cMin, fmt(p.minValue), true);
    mk(cMax, fmt(p.maxValue), true);
    mk(cStep, p.step > 0 ? fmt(p.step) : QString(), true);
    mk(cGroup, QString::fromStdString(p.group), true);
    d->table->item(row, cName)->setToolTip(QString::fromStdString(p.expression + "  —  " + p.description));
    ++row;
  }
  d->table->setColumnWidth(cShow, 130);
  d->table->setColumnWidth(cName, 90);
  d->table->setColumnWidth(cLabel, 230);
  d->table->setColumnWidth(cMin, 80);
  d->table->setColumnWidth(cMax, 80);
  d->table->setColumnWidth(cStep, 70);
  d->table->setMinimumHeight(250);
  vp->addWidget(d->table);
  auto* hint = new QLabel("Min/Maks boş bırakılırsa sınırsız kabul edilir (önerilmez). Adım boşsa otomatik seçilir.");
  hint->setStyleSheet("color:#667;");
  vp->addWidget(hint);
  root->addWidget(gPar, 1);

  // --- Named selections (optional: only when the document has some). They are always carried to the app; the
  // tick decides whether the app's STEP export writes the name onto the faces (CFD / FEA zone names).
  if (!doc.namedSelections.empty()) {
    auto* gNs = new QGroupBox(QString("Adlandırılmış seçimler (%1) — uygulamaya taşınır").arg(doc.namedSelections.size()));
    gNs->setObjectName("saNamedSelections");
    auto* nv = new QVBoxLayout(gNs);
    d->nsList = new QListWidget;
    d->nsList->setObjectName("saNamedSelectionList");
    d->nsList->setMaximumHeight(96);
    for (const auto& ns : doc.namedSelections) {
      auto* it = new QListWidgetItem(QString::fromStdString(ns.name));
      it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
      it->setCheckState(ns.exportStep ? Qt::Checked : Qt::Unchecked);
      QPixmap pm(12, 12);
      pm.fill(QColor(QString::fromStdString(ns.color)));
      it->setIcon(QIcon(pm));
      it->setToolTip(QString("%1 üye. İşaretli: uygulamanın STEP dışa aktarımında yüzler bu adı taşır.").arg(ns.members.size()));
      d->nsList->addItem(it);
    }
    auto* nh = new QLabel("İşaretli seçimler STEP dışa aktarımında yüz adı (bölge adı) olarak yazılır. Adlar uygulamada "
                          "parametre değişse de korunur.");
    nh->setWordWrap(true);
    nh->setStyleSheet("color:#667;");
    nv->addWidget(d->nsList);
    nv->addWidget(nh);
    root->addWidget(gNs);
  }

  // --- Options
  auto* gOpt = new QGroupBox("Seçenekler");
  auto* og = new QGridLayout(gOpt);
  d->chSTL = new QCheckBox("STL dışa aktarmaya izin ver");
  d->chSTEP = new QCheckBox("STEP dışa aktarmaya izin ver");
  d->chBlock = new QCheckBox("Hatalarda güncellemeyi engelle");
  d->chLive = new QCheckBox("Canlı güncelleme");
  d->chAM = new QCheckBox("Katmanlı imalat (AM) kontrollerini göster");
  d->chCNC = new QCheckBox("Konvansiyonel (CNC) kontrollerini göster");
  for (auto* c : {d->chSTL, d->chSTEP, d->chBlock, d->chLive, d->chAM, d->chCNC}) c->setChecked(true);
  d->chLive->setChecked(true);
  og->addWidget(d->chSTL, 0, 0);
  og->addWidget(d->chSTEP, 0, 1);
  og->addWidget(d->chBlock, 1, 0);
  og->addWidget(d->chLive, 1, 1);
  og->addWidget(d->chAM, 2, 0);
  og->addWidget(d->chCNC, 2, 1);
  root->addWidget(gOpt);

  // --- Output
  auto* gOut = new QGroupBox("Çıktı");
  auto* ov = new QVBoxLayout(gOut);
  auto* rr = new QHBoxLayout;
  d->rbExe = new QRadioButton("Tek dosya çalıştırılabilir");
  d->rbPkg = new QRadioButton(".pcapp paketi");
  d->rbBoth = new QRadioButton("İkisi birden");
  d->rbExe->setChecked(true);
  rr->addWidget(d->rbExe);
  rr->addWidget(d->rbPkg);
  rr->addWidget(d->rbBoth);
  rr->addStretch();
  ov->addLayout(rr);
  auto* pr = new QHBoxLayout;
  d->outPath = new QLineEdit(QDir::homePath() + "/" + QString::fromStdString(doc.name).replace(' ', '_') + "_app");
  auto* browse = new QPushButton("Gözat…");
  pr->addWidget(new QLabel("Dosya:"));
  pr->addWidget(d->outPath, 1);
  pr->addWidget(browse);
  ov->addLayout(pr);
  root->addWidget(gOut);

  // --- Lint results
  d->lintView = new QPlainTextEdit;
  d->lintView->setReadOnly(true);
  d->lintView->setFixedHeight(90);
  d->lintView->setPlaceholderText("Denetim sonuçları burada görünür.");
  auto* lintRow = new QHBoxLayout;
  auto* lintBtn = new QPushButton("Denetle");
  lintRow->addWidget(new QLabel("Denetim:"));
  lintRow->addStretch();
  lintRow->addWidget(lintBtn);
  root->addLayout(lintRow);
  root->addWidget(d->lintView);

  auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
  bb->button(QDialogButtonBox::Cancel)->setText("İptal");
  auto* save = bb->addButton("Uygulamayı Kaydet", QDialogButtonBox::AcceptRole);
  save->setDefault(true);
  root->addWidget(bb);

  connect(browse, &QPushButton::clicked, this, [this] {
    QString f = QFileDialog::getSaveFileName(this, "Uygulama dosyası", d->outPath->text());
    if (!f.isEmpty()) d->outPath->setText(f);
  });

  auto runLint = [this]() -> std::vector<std::string> {
    ParamTable pt = d->doc.params;
    QString e = d->readTable(pt);
    if (!e.isEmpty()) return {std::string(pc::app::kLintError) + e.toStdString()};
    return pc::app::lintApp(d->build(pt));
  };
  auto showLint = [this](const std::vector<std::string>& l) {
    if (l.empty()) { d->lintView->setPlainText("Sorun bulunamadı."); return; }
    QStringList s;
    for (auto& x : l) s << QString::fromStdString(x);
    d->lintView->setPlainText(s.join("\n"));
  };
  connect(lintBtn, &QPushButton::clicked, this, [=] { showLint(runLint()); });

  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(bb, &QDialogButtonBox::accepted, this, [=] {
    if (d->name->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, "Uygulama Olarak Kaydet", "Uygulama adı boş olamaz.");
      return;
    }
    ParamTable pt = d->doc.params;
    QString te = d->readTable(pt);
    if (!te.isEmpty()) { QMessageBox::warning(this, "Uygulama Olarak Kaydet", te); return; }
    auto def = d->build(pt);
    auto lint = pc::app::lintApp(def);
    showLint(lint);
    if (pc::app::lintHasErrors(lint)) {
      QStringList errs;
      for (auto& s : lint) if (pc::app::lintIsError(s)) errs << QString::fromStdString(s);
      QMessageBox::critical(this, "Uygulama Olarak Kaydet",
                            "Aşağıdaki hatalar giderilmeden uygulama kaydedilemez:\n\n" + errs.join("\n"));
      return;
    }
    if (!lint.empty()) {
      QStringList ws;
      for (auto& s : lint) ws << QString::fromStdString(s);
      auto r = QMessageBox::question(this, "Uygulama Olarak Kaydet",
                                     "Uyarılar var:\n\n" + ws.join("\n") + "\n\nYine de kaydedilsin mi?",
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (r != QMessageBox::Yes) return;
    }
    bool wantExe = d->rbExe->isChecked() || d->rbBoth->isChecked();
    bool wantPkg = d->rbPkg->isChecked() || d->rbBoth->isChecked();
    QString exe, pkg;
    d->computePaths(exe, pkg);
    if (d->outPath->text().trimmed().isEmpty()) {
      QMessageBox::warning(this, "Uygulama Olarak Kaydet", "Çıktı dosya yolu belirtilmedi.");
      return;
    }
    // Never silently replace the runner itself or an existing file (e.g. "part.pcad" typed as output).
    if (wantExe && QFileInfo::exists(d->runner) && QFileInfo(exe).absoluteFilePath() == QFileInfo(d->runner).absoluteFilePath()) {
      QMessageBox::critical(this, "Uygulama Olarak Kaydet", "Çıktı dosyası çalıştırıcının (persistcad_runner) kendisi olamaz.");
      return;
    }
    QStringList existing;
    if (wantExe && QFileInfo::exists(exe)) existing << exe;
    if (wantPkg && QFileInfo::exists(pkg)) existing << pkg;
    if (!existing.isEmpty() &&
        QMessageBox::question(this, "Uygulama Olarak Kaydet", "Aşağıdaki dosyalar zaten var, üzerine yazılsın mı?\n\n" + existing.join("\n"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
      return;
    std::string err;
    QString wroteExe, wrotePkg;
    if (wantExe) {
      if (!QFileInfo::exists(d->runner)) {
        QMessageBox::critical(this, "Uygulama Olarak Kaydet",
                              "Çalıştırıcı (persistcad_runner) bulunamadı:\n" + d->runner);
        return;
      }
      if (!pc::app::buildStandaloneApp(def, d->runner.toUtf8().toStdString(), exe.toUtf8().toStdString(), &err)) {
        QMessageBox::critical(this, "Uygulama Olarak Kaydet", "Çalıştırılabilir oluşturulamadı:\n" + QString::fromStdString(err));
        return;
      }
      wroteExe = exe;
    }
    if (wantPkg) {
      if (!pc::app::savePackage(def, pkg.toStdString(), &err)) {
        QMessageBox::critical(this, "Uygulama Olarak Kaydet", ".pcapp yazılamadı:\n" + QString::fromStdString(err));
        return;
      }
      wrotePkg = pkg;
    }
    // Success: write exposure edits back into the document.
    d->doc.params = pt;
    d->applyNamedSelections(d->doc);
    d->outExe = wroteExe;
    d->outPkg = wrotePkg;
    accept();
  });
}

SaveAsAppDialog::~SaveAsAppDialog() = default;
QString SaveAsAppDialog::writtenExecutable() const { return d->outExe; }
QString SaveAsAppDialog::writtenPackage() const { return d->outPkg; }

bool runSaveAsApp(Document& doc, const QString& runnerExePath, QWidget* parent) {
  SaveAsAppDialog dlg(doc, runnerExePath, parent);
  if (dlg.exec() != QDialog::Accepted) return false;
  QString msg = "Uygulama kaydedildi:";
  if (!dlg.writtenExecutable().isEmpty()) msg += "\n" + dlg.writtenExecutable();
  if (!dlg.writtenPackage().isEmpty()) msg += "\n" + dlg.writtenPackage();
  QMessageBox::information(parent, "Uygulama Olarak Kaydet", msg);
  return true;
}

} // namespace pc::appui
