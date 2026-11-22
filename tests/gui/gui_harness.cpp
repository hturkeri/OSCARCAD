// pc_gui_harness — drives the interactive paths of the designer, the Save-as-App dialog and the runner
// through real widgets (QtTest input simulation, findChild, modal-dialog pump).
//   pc_gui_harness [--shots DIR] [--keep DIR]
// Run under: xvfb-run -a -s "-screen 0 1600x1000x24" pc_gui_harness
// Exit code = number of failed checks.
#include "MainWindow.h"
#include "MfgDialogs.h"
#include "RunnerWindow.h"
#include "pc/AppPackage.h"
#include "pc/Expression.h"
#include "pc/MfgChecks.h"
#include "pc/Model.h"
#include "pc/SaveAsAppDialog.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <nlohmann/json.hpp>

namespace {

int g_fail = 0;
QString g_shots;  // screenshot output dir ("" = none)

void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  if (!ok) ++g_fail;
}

bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (!pred()) {
    if (t.elapsed() > timeoutMs) return false;
    QApplication::processEvents(QEventLoop::AllEvents, 30);
    QTest::qWait(20);
  }
  return true;
}

void saveShot(const QImage& img, const QString& name) {
  if (g_shots.isEmpty()) return;
  QDir().mkpath(g_shots);
  const QString p = g_shots + "/" + name;
  check(img.save(p), "screenshot written: " + p);
}

// ---- modal dialog pump: modal exec() loops still process timers, so a periodic timer can see and
// drive whatever modal widget is active. Handlers must not click synchronously (a click may open a
// nested modal loop): they schedule clicks with a 0-ms single shot.
std::function<void(QWidget*)> g_onModal;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(60);
  QObject::connect(t, &QTimer::timeout, [] {
    static bool busy = false;
    if (busy || !g_onModal) return;
    QWidget* w = QApplication::activeModalWidget();
    if (!w || !w->isVisible() || w->property("pcHarnessHandled").toBool()) return;
    busy = true;
    g_onModal(w);
    busy = false;
  });
  t->start();
}
void markHandled(QWidget* w) { w->setProperty("pcHarnessHandled", true); }
template <class T>
T* childWithText(QWidget* w, const QString& text) {
  for (T* c : w->findChildren<T*>())
    if (c->text() == text) return c;
  return nullptr;
}
void clickLater(QAbstractButton* b, int ms = 0) {
  if (b) QTimer::singleShot(ms, b, [b] { QTest::mouseClick(b, Qt::LeftButton); });
}
// Generic message-box handler: confirm warnings / acknowledge info, log everything.
QStringList g_boxes;
bool handleMessageBox(QWidget* w) {
  auto* mb = qobject_cast<QMessageBox*>(w);
  if (!mb) return false;
  markHandled(mb);
  g_boxes << mb->text();
  std::printf("  [msgbox] %s\n", qPrintable(mb->text().left(160).replace('\n', ' ')));
  QAbstractButton* b = mb->button(QMessageBox::Yes);
  if (!b) b = mb->button(QMessageBox::Ok);
  if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
  clickLater(b);
  return true;
}

QImage composite(QWidget* main, QWidget* dlg) {
  QImage img = main->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const QImage d = dlg->grab().toImage();
  QPainter p(&img);
  // lower right, so the 3D view (with the candidate ghost) stays visible
  const QPoint at(std::max(0, img.width() - d.width() - 24), std::max(0, img.height() - d.height() - 36));
  p.fillRect(QRect(at + QPoint(6, 6), d.size()), QColor(0, 0, 0, 60));
  p.drawImage(at, d);
  p.setPen(QColor(120, 130, 140));
  p.drawRect(QRect(at, d.size()).adjusted(0, 0, -1, -1));
  return img;
}

double expectedSuggestion(QTableWidget* t, const QString& param) {
  // Table: Önem | Alan | Mesaj | Parametre | Önerilen. Highest severity first, then farthest.
  double best = std::nan("");
  int bestSev = -1;
  for (int r = 0; r < t->rowCount(); ++r) {
    const QString ps = t->item(r, 3)->text();
    if (ps.split(", ").value(0) != param) continue;
    bool ok = false;
    const double v = t->item(r, 4)->text().toDouble(&ok);
    if (!ok) continue;
    const QString sev = t->item(r, 0)->text();
    const int s = sev == "Hata" ? 2 : sev == "Uyarı" ? 1 : 0;
    if (s > bestSev || (s == bestSev && v > best)) { best = v; bestSev = s; }
  }
  return best;
}

// ============================================================================ (a) designer
void testDesignerReview(pcd::MainWindow& w) {
  std::printf("== designer: parameter table edit -> review dialog -> Önerileri Uygula\n");
  check(w.openFile(PC_SAMPLES_DIR "/bracket.pcad"), "open samples/bracket.pcad");
  QTest::qWait(400);
  saveShot(w.grab().toImage(), "final_designer.png");
  QTableView* pv = nullptr;
  for (auto* v : w.findChildren<QTableView*>())
    if (qobject_cast<QStandardItemModel*>(v->model())) pv = v;
  check(pv != nullptr, "parameter table found");
  if (!pv) return;
  auto* model = qobject_cast<QStandardItemModel*>(pv->model());
  int rowT = -1;
  for (int r = 0; r < model->rowCount(); ++r)
    if (model->item(r, 0)->text() == "T") rowT = r;
  check(rowT >= 0, "row for T found");
  if (rowT < 0) return;
  const double vol0 = pc::model::massProperties(w.modelResult().shape).volume;

  int reviews = 0;
  double expected = std::nan("");
  QStringList reviewRules;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    if (!m->inherits("pcd::ChangeReviewDialog")) return;
    markHandled(m);
    ++reviews;
    auto* t = m->findChild<QTableWidget*>();
    if (t) {
      for (int r = 0; r < t->rowCount(); ++r) reviewRules << t->item(r, 2)->text();
      if (reviews == 1) expected = expectedSuggestion(t, "T");
    }
    if (reviews == 1) {
      QTimer::singleShot(700, m, [&w, m] {
        saveShot(composite(&w, m), "final_designer_review.png");
        clickLater(childWithText<QPushButton>(m, "Önerileri Uygula"));
      });
    } else {
      clickLater(childWithText<QPushButton>(m, "Vazgeç"), 100);  // must not happen: suggestions should fix it
    }
  };
  // Real edit path: open the delegate editor of the "İfade" cell, type, press Enter.
  const QModelIndex idx = model->index(rowT, 1);
  pv->setCurrentIndex(idx);
  pv->edit(idx);
  QTest::qWait(100);
  auto* ed = pv->viewport()->findChild<QLineEdit*>();
  check(ed != nullptr, "inline editor opened for T expression");
  if (!ed) return;
  ed->selectAll();
  QTest::keyClicks(ed, "0.2");
  QTest::keyClick(ed, Qt::Key_Return);
  const bool done = waitFor([&] {
    const auto* p = w.document().params.find("T");
    return p && p->expression != "8" && !QApplication::activeModalWidget();
  }, 30000);
  check(done, "T committed after the review dialog");
  check(reviews == 1, QString("review dialog shown exactly once (shown %1x)").arg(reviews));
  check(reviewRules.join("|").contains("0.80") || reviewRules.join("|").contains("0.40"), "review lists the wall-thickness violations");
  const std::string texpr = w.document().params.find("T")->expression;
  check(std::isfinite(expected) && texpr == pc::formatNumber(expected),
        QString("T = suggested value (got %1, expected %2)").arg(QString::fromStdString(texpr)).arg(expected));
  const auto& mr = w.modelResult();
  const auto mp = pc::model::massProperties(mr.shape);
  check(mr.ok && !mr.shape.IsNull(), "model regenerated ok");
  check(std::fabs((mp.zmax - mp.zmin) - (expected + 20.0)) < 1e-3, QString("model height = T + Hb (%1)").arg(mp.zmax - mp.zmin));
  check(mp.volume < vol0, "volume decreased after thinner plate");
  const auto vr = pc::mfg::validateChange(w.document(), {}, w.document().mfg);
  check(!vr.report.hasErrors(), "committed document has no manufacturing errors");
  // undo / redo through the stack
  w.undoStack()->undo();
  QTest::qWait(300);
  check(w.document().params.find("T")->expression == "8", "undo restores T = 8");
  w.undoStack()->redo();
  QTest::qWait(300);
  check(w.document().params.find("T")->expression == texpr, "redo re-applies suggested T");
  g_onModal = nullptr;
}

// ============================================================================ (b) save as app
struct SavedApp { QString exe, pkg; };
SavedApp testSaveAsApp(pcd::MainWindow& w, const QString& outDir, bool overwrite = false) {
  std::printf("== designer: Save as App dialog -> standalone exe + .pcapp%s\n", overwrite ? " (overwrite existing)" : "");
  SavedApp out;
  const QString base = outDir + "/BraketApp";
  if (!overwrite) {
    QFile::remove(base);
    QFile::remove(base + ".pcapp");
  }
  g_boxes.clear();
  QAction* act = nullptr;
  for (auto* a : w.findChildren<QAction*>())
    if (a->text().startsWith("Uygulama Olarak Kaydet")) act = a;
  check(act != nullptr, "menu action 'Uygulama Olarak Kaydet (Save as App)' exists");
  if (!act) return out;
  bool sawDialog = false, finished = false;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) {
      if (g_boxes.last().startsWith("Uygulama kaydedildi")) finished = true;
      return;
    }
    if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
    markHandled(m);
    sawDialog = true;
    // Line edits in creation order: name, version, author (info group), output path (output group).
    QList<QLineEdit*> edits;
    for (auto* e : m->findChildren<QLineEdit*>())
      if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox"))) edits << e;
    QLineEdit *name = edits.value(0), *version = edits.value(1), *author = edits.value(2), *path = edits.value(3);
    if (!(name && name->text() == QString::fromStdString(w.document().name) && version && version->text() == "1.0.0" &&
          path && path->text().endsWith("_app")))
      name = nullptr;
    check(name && version && author && path, "dialog fields found (name/version/author/path)");
    if (!(name && version && author && path)) { clickLater(childWithText<QPushButton>(m, "İptal")); return; }
    name->setText("Braket Konfigüratör");
    version->setText("2.1.0");
    author->setText("QA");
    for (auto* pe : m->findChildren<QPlainTextEdit*>())
      if (!pe->isReadOnly()) pe->setPlainText("Plaka + göbek braketi; DfAM/DfM kontrollü.");
    auto* both = childWithText<QRadioButton>(m, "İkisi birden");
    check(both != nullptr, "output radio 'İkisi birden' found");
    if (both) QTest::mouseClick(both, Qt::LeftButton);
    path->setText(base);
    if (auto* lint = childWithText<QPushButton>(m, "Denetle")) QTest::mouseClick(lint, Qt::LeftButton);
    QTimer::singleShot(400, m, [m, overwrite] {
      if (!overwrite) saveShot(m->grab().toImage(), "final_saveas_dialog.png");
      clickLater(childWithText<QPushButton>(m, "Uygulamayı Kaydet"));
    });
  };
  QTimer::singleShot(0, act, &QAction::trigger);
  check(waitFor([&] { return finished && !QApplication::activeModalWidget(); }, 60000), "save-as-app finished with success message");
  check(sawDialog, "Save-as-App dialog appeared");
  if (overwrite) check(g_boxes.join("|").contains("zaten var"), "overwrite confirmation asked for existing outputs");
  else check(!g_boxes.join("|").contains("zaten var"), "no overwrite prompt for new outputs");
  g_onModal = nullptr;
#ifdef Q_OS_WIN
  const QString exeP = base + ".exe";
#else
  const QString exeP = base;
#endif
  const QFileInfo fe(exeP), fp(base + ".pcapp");
  check(fe.exists() && fe.isExecutable() && fe.size() > 1000000, "standalone executable written (" + exeP + ")");
  check(fp.exists() && fp.size() > 100, ".pcapp written");
  check(!QFileInfo::exists(base + ".tmp." + QString::number(QCoreApplication::applicationPid())), "no temp file left behind");
  out.exe = exeP;
  out.pkg = base + ".pcapp";

  std::string err;
  auto emb = pc::app::loadEmbedded(exeP.toStdString(), &err);
  check(emb.has_value(), "embedded payload loads in-process " + QString::fromStdString(err));
  auto pkg = pc::app::loadPackage(out.pkg.toStdString(), &err);
  check(pkg.has_value(), ".pcapp loads");
  if (emb && pkg) {
    check(emb->appName == "Braket Konfigüratör" && emb->version == "2.1.0" && emb->author == "QA", "embedded metadata matches dialog input");
    check(nlohmann::json(pc::app::toJson(*emb)) == nlohmann::json(pc::app::toJson(*pkg)), "embedded payload == .pcapp content");
    check(emb->document.params.find("T") && emb->document.params.find("T")->expression == w.document().params.find("T")->expression,
          "app carries the current (suggested) T");
  }
  // run the written executable's embedded payload
  auto run = [](const QString& prog, const QStringList& args, QByteArray* so) {
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(120000)) { p.kill(); return -100; }
    if (so) *so = p.readAllStandardOutput();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
  };
  QByteArray so;
  int rc = run(base, {"--validate-only"}, &so);
  check(rc == 0, QString("exe --validate-only (defaults) exit 0 (got %1)").arg(rc));
  try {
    auto j = nlohmann::json::parse(so.toStdString());
    check(j.value("app", "") == "Braket Konfigüratör" && j.value("acceptable", false), "exe reports embedded app, acceptable");
  } catch (...) { check(false, "exe --validate-only prints JSON"); }
  rc = run(base, {"--validate-only", "--set", "T=0.2"}, nullptr);
  check(rc == 2, QString("exe --validate-only --set T=0.2 exit 2 (got %1)").arg(rc));
  rc = run(QCoreApplication::applicationDirPath() + "/persistcad_runner"
#ifdef Q_OS_WIN
                                                ".exe"
#endif
           , {out.pkg, "--validate-only", "--text"}, &so);
  check(rc == 0 && so.contains("UYGUN"), "persistcad_runner app.pcapp --validate-only --text works");
  return out;
}

// ============================================================================ (c) runner
struct RunnerUi {
  pc::runner::RunnerWindow* win = nullptr;
  QLabel* status = nullptr;
  QLabel* info = nullptr;
  QPushButton* bannerApply = nullptr;
  QWidget* banner() const { return bannerApply ? bannerApply->parentWidget() : nullptr; }
  bool bannerShown() const { return banner() && banner()->isVisible(); }
  QDoubleSpinBox* spin(const QString& param) const {
    for (auto* s : win->findChildren<QDoubleSpinBox*>()) {
      QWidget* box = s->parentWidget();
      for (auto* l : box->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        if (l->toolTip() == param) return s;
    }
    return nullptr;
  }
  QPushButton* applyBtn(const QString& param) const {
    auto* s = spin(param);
    return s ? s->parentWidget()->findChild<QPushButton*>("suggest") : nullptr;
  }
  // Mark the status, run `change`, wait for the next completed validation.
  bool changeAndWait(const std::function<void()>& change, int timeoutMs = 30000) {
    status->setText("harness: bekleniyor");
    change();
    return waitFor([&] { return status->text().startsWith("Doğrulama tamamlandı"); }, timeoutMs);
  }
};

RunnerUi makeRunner(const pc::app::AppDefinition& def) {
  RunnerUi u;
  u.win = new pc::runner::RunnerWindow(def);
  bool settled = false;
  QObject::connect(u.win, &pc::runner::RunnerWindow::settled, [&settled] { settled = true; });
  u.win->show();
  u.win->startup();
  check(waitFor([&] { return settled; }, 60000), "runner settled after startup");
  for (auto* l : u.win->statusBar()->findChildren<QLabel*>())
    if (l->text().startsWith("Doğrulama")) u.status = l;
  for (auto* l : u.win->findChildren<QLabel*>())
    if (l->text().contains("Hacim:")) u.info = l;
  u.bannerApply = childWithText<QPushButton>(u.win, "Önerileri Uygula");
  check(u.status && u.info && u.bannerApply, "runner widgets located (status, info strip, banner)");
  return u;
}

bool exportVia(RunnerUi& u, const QString& actionText, const QString& path) {
  QAction* act = nullptr;
  for (auto* a : u.win->findChildren<QAction*>())
    if (a->text() == actionText) act = a;
  if (!act) return false;
  QFile::remove(path);
  bool sawDlg = false;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    auto* fd = qobject_cast<QFileDialog*>(m);
    if (!fd) return;
    markHandled(fd);
    sawDlg = true;
    // The dialog applies its initial selection after showing: set ours a little later.
    QTimer::singleShot(250, fd, [fd, path] {
      fd->selectFile(path);
      if (auto* le = fd->findChild<QLineEdit*>("fileNameEdit")) le->setText(path);
      QTimer::singleShot(100, fd, [fd] { QMetaObject::invokeMethod(fd, "accept"); });
    });
  };
  QTimer::singleShot(0, act, &QAction::trigger);
  const bool ok = waitFor([&] { return QFileInfo::exists(path) && u.status->text().startsWith("Kaydedildi") && !QApplication::activeModalWidget(); }, 30000);
  g_onModal = nullptr;
  return ok && sawDlg;
}

void testRunner(const QString& pkgPath, const QString& outDir) {
  std::printf("== runner: live mode\n");
  std::string err;
  auto def = pc::app::loadPackage(pkgPath.toStdString(), &err);
  check(def.has_value(), "load .pcapp for the runner");
  if (!def) return;
  def->liveUpdate = true;
  RunnerUi u = makeRunner(*def);
  if (!u.status || !u.info || !u.bannerApply) return;
  check(!u.bannerShown(), "no banner with valid defaults");
  saveShot(u.win->grab().toImage(), "harness_runner_ok.png");

  // C1: violating value via the spin box -> banner + field indicator + per-field apply button
  auto* sDb = u.spin("Dbore");
  check(sDb != nullptr, "spin box for Dbore found");
  if (!sDb) return;
  const QString info0 = u.info->text();
  check(u.changeAndWait([&] { sDb->setValue(1.5); }), "validation completed after Dbore = 1.5");
  check(u.bannerShown(), "violation banner shown");
  check(u.status->text().contains("son geçerli"), "status says the last valid model is shown");
  check(sDb->styleSheet().contains("border:2px solid"), "Dbore field indicator (coloured border)");
  QPushButton* ap = u.applyBtn("Dbore");
  check(ap && ap->isVisible(), "per-field suggestion button visible: " + (ap ? ap->text() : QString("-")));
  check(u.info->text() == info0, "info strip unchanged (model not updated on violation)");
  saveShot(u.win->grab().toImage(), "harness_runner_violation.png");
  if (!ap) return;
  bool okNum = false;
  const double sug = ap->text().section(' ', 1, 1).toDouble(&okNum);
  check(okNum && sug > 1.5, QString("suggested Dbore parsed (%1)").arg(sug));
  check(u.changeAndWait([&] { QTest::mouseClick(ap, Qt::LeftButton); }), "validation after per-field Uygula");
  check(std::fabs(sDb->value() - sug) < 1e-9, QString("Dbore = suggestion (%1)").arg(sDb->value()));
  check(!u.bannerShown() || !u.status->text().contains("son geçerli"), "model accepted after applying suggestion");
  check(!sDb->styleSheet().contains(QString("#d93025")), "Dbore error indicator cleared");
  check(u.info->text() != info0, "info strip updated (new model shown)");

  // C2: burst of edits -> stale validations dropped, final state reflects the last value
  auto* sT = u.spin("T");
  check(sT != nullptr, "spin box for T found");
  if (!sT) return;
  // Dbore = 1.7 is only valid for the current bore depth (Lbore = Hb + T - 4): restore the default first.
  check(u.changeAndWait([&] { sDb->setValue(10.0); }), "validation after Dbore = 10");
  check(u.changeAndWait([&] {
    for (double v : {0.2, 0.3, 5.0, 0.25, 9.0}) { sT->setValue(v); QApplication::processEvents(); }
  }), "validation after a burst of T edits");
  QTest::qWait(600);  // any trailing stale job would land here
  check(std::fabs(sT->value() - 9.0) < 1e-9 && !u.bannerShown(), "final state = last edit (T = 9), no banner");
  check(u.info->text().contains("× 29.0 mm"), "info strip shows bbox height 29.0 (T 9 + Hb 20)");

  // C3: banner "Önerileri Uygula"
  check(u.changeAndWait([&] { sT->setValue(0.2); }), "validation after T = 0.2");
  check(u.bannerShown() && u.bannerApply->isVisible(), "banner with 'Önerileri Uygula' for T = 0.2");
  check(u.changeAndWait([&] { QTest::mouseClick(u.bannerApply, Qt::LeftButton); }), "validation after banner apply");
  check(std::fabs(sT->value() - 0.8) < 1e-9, QString("banner apply sets T to the strictest suggestion 0.8 (got %1)").arg(sT->value()));
  check(!u.bannerShown(), "banner hidden after suggestions applied");

  // C4: STL / STEP export of the shown model
  const QString stl = outDir + "/runner_export.stl", stp = outDir + "/runner_export.step";
  check(exportVia(u, "STL olarak dışa aktar…", stl), "STL export via menu + file dialog");
  {
    QFile f(stl);
    bool valid = false;
    if (f.open(QIODevice::ReadOnly)) {
      const QByteArray b = f.readAll();
      if (b.size() >= 84) {
        quint32 n = 0;
        std::memcpy(&n, b.constData() + 80, 4);
        valid = n > 100 && qint64(b.size()) == 84 + 50 * qint64(n);
        double zmin = 1e9, zmax = -1e9;
        for (quint32 i = 0; valid && i < n; ++i)
          for (int k = 0; k < 3; ++k) {
            float z;
            std::memcpy(&z, b.constData() + 84 + 50 * i + 12 + 12 * k + 8, 4);
            zmin = std::min(zmin, double(z)); zmax = std::max(zmax, double(z));
          }
        check(valid && std::fabs((zmax - zmin) - 20.8) < 1e-3, QString("binary STL valid, %1 triangles, height %2").arg(n).arg(zmax - zmin));
      }
    }
    if (!valid) check(false, "binary STL structure");
  }
  check(exportVia(u, "STEP olarak dışa aktar…", stp), "STEP export via menu + file dialog");
  {
    STEPControl_Reader rd;
    bool ok = rd.ReadFile(stp.toStdString().c_str()) == IFSelect_RetDone && rd.TransferRoots() > 0;
    double vol = 0;
    if (ok) {
      GProp_GProps g;
      BRepGProp::VolumeProperties(rd.OneShape(), g);
      vol = g.Mass();
    }
    // info strip: "Hacim: X cm³"
    const QString t = u.info->text();
    const double shownCm3 = t.section("Hacim:</b> ", 1).section(' ', 0, 0).toDouble();
    check(ok && std::fabs(vol / 1000.0 - shownCm3) < 0.01, QString("STEP re-reads, volume %1 cm³ == shown %2 cm³").arg(vol / 1000.0).arg(shownCm3));
  }
  saveShot(u.win->grab().toImage(), "harness_runner_after.png");
  delete u.win;

  // C5: non-live mode with "Güncelle"
  std::printf("== runner: non-live mode\n");
  auto def2 = *def;
  def2.liveUpdate = false;
  RunnerUi v = makeRunner(def2);
  if (!v.status) return;
  auto* upd = childWithText<QPushButton>(v.win, "Güncelle");
  check(upd && upd->isVisible(), "'Güncelle' button visible in non-live mode");
  auto* vDb = v.spin("Dbore");
  if (!upd || !vDb) return;
  v.status->setText("harness");
  vDb->setValue(1.5);
  QTest::qWait(900);
  check(v.status->text().contains("bekliyor") && !v.bannerShown(), "edit does not validate until 'Güncelle'");
  QLabel* dirty = childWithText<QLabel>(v.win, "Değişiklikler henüz uygulanmadı");
  check(dirty && dirty->isVisible(), "pending-changes label shown");
  check(v.changeAndWait([&] { QTest::mouseClick(upd, Qt::LeftButton); }), "validation after 'Güncelle'");
  check(v.bannerShown() && !(dirty && dirty->isVisible()), "banner shown after 'Güncelle', pending label hidden");
  QPushButton* ap2 = v.applyBtn("Dbore");
  check(ap2 && ap2->isVisible(), "per-field Uygula in non-live mode");
  if (ap2) {
    check(v.changeAndWait([&] { QTest::mouseClick(ap2, Qt::LeftButton); }), "per-field Uygula validates immediately in non-live mode");
    check(!v.bannerShown(), "non-live: suggestion applied and accepted");
  }
  delete v.win;

  // C6: destroying a window while a validation runs must not crash
  std::printf("== runner: destroy while validating\n");
  {
    auto* w = new pc::runner::RunnerWindow(*def);
    w->show();
    w->startup();
    QApplication::processEvents();
    delete w;
    QTest::qWait(200);
    check(true, "window destroyed during validation without crash");
  }
}

} // namespace

int main(int argc, char** argv) {
  QSurfaceFormat f;
  f.setVersion(3, 3);
  f.setProfile(QSurfaceFormat::CoreProfile);
  f.setDepthBufferSize(24);
  f.setStencilBufferSize(8);
  f.setSamples(4);
  QSurfaceFormat::setDefaultFormat(f);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);  // Windows: native file dialogs cannot be driven by the harness
  QApplication app(argc, argv);
  QApplication::setStyle("Fusion");
  QString keep;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) g_shots = argv[++i];
    else if (!std::strcmp(argv[i], "--keep") && i + 1 < argc) keep = argv[++i];
  }
  QTemporaryDir tmp;
  const QString outDir = keep.isEmpty() ? tmp.path() : keep;
  QDir().mkpath(outDir);
  startModalPump();

  SavedApp saved;
  {
    pcd::MainWindow w;
    w.show();
    QTest::qWait(300);
    testDesignerReview(w);
    saved = testSaveAsApp(w, outDir);
    if (!saved.exe.isEmpty()) saved = testSaveAsApp(w, outDir, true);
    w.setAutoYes(true);  // no "save changes?" prompt on close
    w.close();
  }
  if (!saved.pkg.isEmpty()) testRunner(saved.pkg, outDir);
  std::printf("\n%s: %d failure(s)\n", g_fail ? "FAILED" : "PASSED", g_fail);
  return g_fail;
}
