// "Teknik resimden içe aktar" through the REAL designer (integration of the drawing import, DRW: designer hook):
//
//   the Dosya menu and the ribbon (Tasarım > İçe aktar > Teknik resim) carry the action -> the ribbon button opens the
//   modal wizard (İptal: nothing changes) -> the Dosya menu entry opens it again -> the corpus flange PDF (copied to a
//   temp dir; the wizard writes a side-car IR next to it) is typed into the file field -> all 7 pages -> Bitir ->
//   ONE undo step "Teknik resimden içe aktar" (Ctrl+Z removes everything, Ctrl+Y restores) -> Revolve + Hole + Pattern
//   with the gt volume, dimension parameters in Gruplar -> save / reopen identical -> Save as App lists them ticked
//   and the generated app's --validate-only reports them.
//
//   pc_gui_harness_drawing_import [--shots]   --shots writes docs/drawing_import/shots/*.png
//   Exit code = number of failed checks.
#include "MainWindow.h"
#include "Ribbon.h"
#include "StructurePanel.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#ifdef PC_HAVE_DRAWING_IMPORT
#include "pc/drawing/Features.h"
#endif
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QWizard>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#ifndef PC_DRW_SHOTS_DIR
#define PC_DRW_SHOTS_DIR "docs/drawing_import/shots"
#endif

namespace {
int g_fail = 0, g_pass = 0;
bool g_shots = false;
void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  if (ok) ++g_pass; else ++g_fail;
  pch::kick();
}
void note(const QString& s) { pch::hnote(s); }
void section(const QString& s) { std::printf("\n== %s\n", qPrintable(s)); std::fflush(stdout); pch::kick(); }
void pump(int ms) { QTest::qWait(ms); }
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  const int limit = timeoutMs * pch::slowFactor();
  for (int t = 0; t < limit; t += 25) {
    if (pred()) return true;
    QTest::qWait(25);
  }
  return pred();
}
template <class T> T* childWithText(QWidget* root, const QString& text) {
  for (T* c : root->findChildren<T*>()) if (c->text() == text || c->text().remove('&') == text) return c;
  return nullptr;
}
void shotOf(QWidget* wdg, const QString& name) {
  if (!g_shots || !wdg) return;
  QDir().mkpath(PC_DRW_SHOTS_DIR);
  const QString path = QString(PC_DRW_SHOTS_DIR) + "/" + name;
  note(QString("shot %1 %2").arg(path, wdg->grab().save(path) ? "saved" : "FAILED"));
}

// ---- modal pump: wizard / file / app dialogs go to g_onModal; message boxes are answered Yes / OK
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      if (mb->property("dhHandled").toBool()) return;
      mb->setProperty("dhHandled", true);
      g_boxes << mb->text();
      note("message box: " + mb->text().left(140).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b) b = mb->button(QMessageBox::No);   // "save changes?" on Yeni: not needed, the file is saved
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      if (m->property("dhHandled").toBool()) return;
      m->setProperty("dhHandled", true);
      note("manufacturing review dialog -> 'Yine de Uygula'");
      if (auto* b = childWithText<QPushButton>(m, "Yine de Uygula")) pch::pressLater(b, 50);
      else if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
      return;
    }
    if (g_onModal) g_onModal(m);
  });
  t->start();
}

int runProc(const QString& prog, const QStringList& args, QByteArray* out) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(120000)) { p.kill(); return -100; }
  if (out) *out = p.readAllStandardOutput();
  return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
}
void acceptFileDialog(QWidget* m, const QString& path) {
  auto* fd = qobject_cast<QFileDialog*>(m);
  if (!fd || fd->property("dhHandled").toBool()) return;
  fd->setProperty("dhHandled", true);
  QTimer::singleShot(200, fd, [fd, path] {
    fd->selectFile(path);
    if (auto* le = fd->findChild<QLineEdit*>("fileNameEdit")) le->setText(path);
    QTimer::singleShot(100, fd, [fd] { QMetaObject::invokeMethod(fd, "accept"); });
  });
}
void key(pcd::MainWindow& w, int k, Qt::KeyboardModifiers mod = Qt::NoModifier) {
  if (QApplication::activeWindow() != &w) { w.activateWindow(); QApplication::setActiveWindow(&w); pump(30); }
  QWidget* f = QApplication::focusWidget();
  QTest::keyClick(f && w.isAncestorOf(f) ? f : static_cast<QWidget*>(w.viewer()), Qt::Key(k), mod);
  pump(80);
}
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = qobject_cast<QStandardItemModel*>(w.parameterTable()->model());
  for (int r = 0; m && r < m->rowCount(); ++r) if (m->item(r, 0) && m->item(r, 0)->text() == name) return r;
  return -1;
}
double volumeOf(pcd::MainWindow& w) {
  if (w.modelResult().shape.IsNull()) return 0;
  return pc::model::massProperties(w.modelResult().shape).volume;
}

const double kFlangeVolume = 155923.5265829686;   // tests/drawing/corpus/flange/flange_00/gt.json
const QStringList kDimParams{"D1", "D2", "D3", "L_top", "Dh1", "D_bc1", "n_h1"};

void run(pcd::MainWindow& w, const QString& tmpDir) {
  // ---------------------------------------------------------------- entry points
  section("menu entry + ribbon button");
  auto* menuFile = w.findChild<QMenu*>("menuFile");
  QAction* act = w.findChild<QAction*>("actionDrawingImport");
  check(menuFile && act && menuFile->actions().contains(act), "Dosya menüsü: 'Teknik resimden içe aktar…' entry");
  check(act && act->text().startsWith("Teknik resimden içe aktar"), "action text Turkish: " + (act ? act->text() : ""));
  QAction* rb = w.findChild<QAction*>("actRibbonDrawingImport");
  pcd::RibbonGroup* grp = w.ribbon() ? w.ribbon()->group("drawingImport") : nullptr;
  QToolButton* rbBtn = grp && rb ? grp->buttonFor(rb) : nullptr;
  check(grp && rbBtn && rbBtn->text() == "Teknik resim", "ribbon: Tasarım > İçe aktar > 'Teknik resim' button");

  // ribbon button -> wizard -> İptal: no change
  const int undo0 = w.undoStack()->count();
  bool sawWizard = false;
  g_onModal = [&](QWidget* m) {
    auto* wz = qobject_cast<QWizard*>(m);
    if (!wz || wz->property("dhHandled").toBool()) return;
    wz->setProperty("dhHandled", true);
    sawWizard = wz->objectName() == "drawingImportWizard";
    pch::pressLater(wz->button(QWizard::CancelButton), 200);
  };
  if (rbBtn) QTest::mouseClick(rbBtn, Qt::LeftButton);
  waitFor([&] { return sawWizard && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  check(sawWizard, "ribbon button opens the modal import wizard");
  check(w.undoStack()->count() == undo0 && w.document().features.empty(), "İptal: document and undo stack unchanged");

  // ---------------------------------------------------------------- full wizard run from the Dosya menu
  section("wizard on the corpus flange PDF (Dosya menu)");
  // vector PDF needs qpdf; builds without it (Windows nodeps) take the same sheet as DXF
#ifdef PC_HAVE_DRAWING_IMPORT
  const bool havePdf = pc::drawing::buildFeatures().qpdf;
#else
  const bool havePdf = false;
#endif
  const QString ext = havePdf ? "pdf" : "dxf";
  const QString pdf = tmpDir + "/flange_sheet." + ext;
  QFile::copy(QString(PC_DRAWING_CORPUS_DIR) + "/flange/flange_00/sheet." + ext, pdf);
  check(QFileInfo::exists(pdf), "corpus PDF copied: " + pdf);
  std::set<int> visited;
  QPointer<QWizard> wiz;
  bool finished = false;
  g_onModal = [&](QWidget* m) {
    auto* wz = qobject_cast<QWizard*>(m);
    if (!wz) return;
    if (!wiz) { wiz = wz; pch::expectCloseWithin(wz, 180000); }
    const int id = wz->currentId();
    if (visited.count(id)) return;
    visited.insert(id);
    auto page = [wz](const char* n) -> QWidget* { return wz->currentPage() ? wz->currentPage()->findChild<QWidget*>(n) : nullptr; };
    auto next = [wz](int ms) { QTimer::singleShot(ms, wz, [wz] { pch::pressLater(wz->button(QWizard::NextButton), 20); }); };
    switch (id) {
      case 0: {
        auto* fe = qobject_cast<QLineEdit*>(page("fileEdit"));
        check(fe != nullptr, "page 1 (Kaynak): file field");
        if (fe) pch::typeInto(fe, pdf, "file path");
        auto* lbl = qobject_cast<QLabel*>(page("sniffLabel"));
        check(lbl && lbl->text().contains(pdf.endsWith(".pdf") ? "VectorPdf" : "Dxf"), "page 1: source kind detected: " + (lbl ? lbl->text().left(60) : ""));
        check(wz->button(QWizard::NextButton)->isEnabled(), "page 1: İleri enabled");
        next(100);
        break;
      }
      case 1: {
        auto* vt = qobject_cast<QTableWidget*>(page("viewTable"));
        check(vt && vt->rowCount() == 2, QString("page 2 (Sayfa düzeni): two views (%1)").arg(vt ? vt->rowCount() : -1));
        auto* sc = qobject_cast<QLabel*>(page("scaleLabel"));
        check(sc && sc->text().contains("1:2"), "page 2: scale 1:2: " + (sc ? sc->text().left(80) : ""));
        QTimer::singleShot(500, wz, [wz] { shotOf(wz, "01_wizard_sayfa_duzeni.png"); });
        next(700);
        break;
      }
      case 2: next(100); break;
      case 3: {
        auto* t = qobject_cast<QTableWidget*>(page("dimTable"));
        check(t && t->rowCount() >= 6, QString("page 4 (Ölçüler): %1 dimensions").arg(t ? t->rowCount() : -1));
        QTimer::singleShot(400, wz, [wz] { shotOf(wz, "02_wizard_olculer.png"); });
        next(600);
        break;
      }
      case 4: {
        auto* dl = qobject_cast<QLabel*>(page("dofLabel"));
        check(dl && dl->text().contains("DOF: 0"), "page 5 (Sketch): fully defined: " + (dl ? dl->text().left(60) : ""));
        next(100);
        break;
      }
      case 5: {
        auto* l = qobject_cast<QListWidget*>(page("strategyList"));
        check(l && l->count() >= 2 && l->item(0)->text().contains("Döndür"), "page 6 (3B): revolve candidate first: " +
                                                                               (l && l->count() ? l->item(0)->text().left(60) : ""));
        check(page("preview") != nullptr, "page 6: OpenGL 3D preview present");
        QTimer::singleShot(1200, wz, [wz] { shotOf(wz, "03_wizard_3b_onizleme.png"); });
        next(1500);
        break;
      }
      case 6: {
        auto* mr = qobject_cast<QRadioButton*>(page("mergeRadio"));
        check(mr && mr->isChecked(), "page 7 (Özet): merge into the current document");
        QTimer::singleShot(150, wz, [wz, &finished] { finished = true; pch::pressLater(wz->button(QWizard::FinishButton), 20); });
        break;
      }
      default: break;
    }
  };
  check(menuFile && act, "trigger via the Dosya menu action");
  if (act) QTimer::singleShot(0, act, &QAction::trigger);
  waitFor([&] { return finished && !QApplication::activeModalWidget(); }, 120000);
  g_onModal = nullptr;
  pump(400);
  check(visited.size() == 7, QString("all 7 wizard pages visited (%1)").arg(visited.size()));
  check(QFileInfo::exists(pdf + ".pcdrw.json"), "side-car IR written next to the drawing");

  // ---------------------------------------------------------------- result in the designer
  section("result in the designer");
  check(w.undoStack()->count() == undo0 + 1 && w.undoStack()->undoText() == "Teknik resimden içe aktar",
        "ONE undo step: '" + w.undoStack()->undoText() + "'");
  const auto& doc = w.document();
  check(doc.feature("Revolve1") && doc.feature("Hole1") && doc.feature("Pattern1"), "features Revolve1 + Hole1 + Pattern1");
  const double v = volumeOf(w);
  check(std::fabs(v - kFlangeVolume) < 0.005 * kFlangeVolume, QString("model volume %1 mm³ (gt %2)").arg(v, 0, 'f', 1).arg(kFlangeVolume, 0, 'f', 1));
  w.structurePanel()->setCurrentPage("groups");
  pump(200);
  QStringList missing;
  for (auto& n : kDimParams) {
    const pc::Parameter* p = doc.params.find(n.toStdString());
    if (!p || !p->exposed || paramRow(w, n) < 0) missing << n;
  }
  check(missing.isEmpty(), "dimension parameters in Gruplar and exposed: " + kDimParams.join(",") + (missing.isEmpty() ? "" : "  missing " + missing.join(",")));
  if (auto* v3 = w.viewer()) v3->setFocus();
  pump(500);
  if (g_shots) {
    QDir().mkpath(PC_DRW_SHOTS_DIR);
    shotOf(&w, "04_tasarimci_sonuc.png");
  }

  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 5000);
  check(w.document().features.empty() && w.document().params.all().empty(), "Ctrl+Z removes the whole import (one step)");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty(); }, 5000);
  check(w.document().feature("Pattern1") && std::fabs(volumeOf(w) - kFlangeVolume) < 0.005 * kFlangeVolume, "Ctrl+Y restores it");

  // ---------------------------------------------------------------- save / reopen
  section("save / reopen");
  const QString pcad = tmpDir + "/flange_import.pcad";
  const nlohmann::json before = pc::toJson(w.document());
  g_onModal = [&](QWidget* m) { acceptFileDialog(m, pcad); };
  key(w, Qt::Key_S, Qt::ControlModifier);
  waitFor([&] { return QFileInfo::exists(pcad) && !QApplication::activeModalWidget(); }, 10000);
  check(QFileInfo::exists(pcad), "saved: " + pcad);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty() && !QApplication::activeModalWidget(); }, 5000);
  g_onModal = [&](QWidget* m) { acceptFileDialog(m, pcad); };
  key(w, Qt::Key_O, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty() && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  pump(300);
  const nlohmann::json after = pc::toJson(w.document());
  check(after["parameters"] == before["parameters"] && after["features"] == before["features"], "reopened: parameters and features identical");
  check(std::fabs(volumeOf(w) - kFlangeVolume) < 0.005 * kFlangeVolume, "reopened: same volume");

  // ---------------------------------------------------------------- Save as App
  section("Save as App lists the imported parameters");
  const QString base = tmpDir + "/FlansApp";
  bool appDlg = false;
  QStringList ticked;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pc::appui::SaveAsAppDialog") || m->property("dhHandled").toBool()) return;
    m->setProperty("dhHandled", true);
    appDlg = true;
    pch::expectCloseWithin(m, 240000);
    if (auto* t = m->findChild<QTableWidget*>())
      for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, 0) && t->item(r, 0)->checkState() == Qt::Checked && t->item(r, 1)) ticked << t->item(r, 1)->text();
    QList<QLineEdit*> edits;
    for (auto* e : m->findChildren<QLineEdit*>()) if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox"))) edits << e;
    if (edits.size() >= 4) edits[3]->setText(base);
    QTimer::singleShot(200, m, [m] { if (auto* b = childWithText<QPushButton>(m, "Uygulamayı Kaydet")) pch::pressLater(b); });
  };
  const int boxes0 = g_boxes.size();
  key(w, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
  const bool appDone = waitFor([&] {
    for (int i = boxes0; i < g_boxes.size(); ++i) if (g_boxes[i].startsWith("Uygulama kaydedildi")) return !QApplication::activeModalWidget();
    return false;
  }, 200000);
  g_onModal = nullptr;
  bool allTicked = true;
  for (auto& n : kDimParams) allTicked &= ticked.contains(n);
  check(appDlg && allTicked, "Save-as-App dialog lists the dimension parameters ticked: " + ticked.join(","));
  check(appDone, "Save-as-App finished");
#ifdef Q_OS_WIN
  const QString exe = base + ".exe";
#else
  const QString exe = base;
#endif
  if (QFileInfo::exists(exe)) {
    QByteArray so;
    const int rc = runProc(exe, {"--validate-only"}, &so);
    bool ok = false;
    double vol = 0;
    try {
      auto j = nlohmann::json::parse(so.toStdString());
      ok = j["parameters"].contains("D1") && j["parameters"]["D1"].get<double>() == 120 && j["parameters"].contains("n_h1");
      vol = j.value("volumeMm3", 0.0);
    } catch (...) {}
    check((rc == 0 || rc == 2) && ok && std::fabs(vol - kFlangeVolume) < 0.005 * kFlangeVolume,
          QString("app --validate-only (rc %1): D1 = 120, n_h1, volume %2").arg(rc).arg(vol, 0, 'f', 1));
  } else {
    check(false, "app executable written: " + exe);
  }
}

}  // namespace

int main(int argc, char** argv) {
#ifndef PC_HAVE_DRAWING_IMPORT
  std::printf("drawing import not built (PC_BUILD_DRAWING off / no Qt Widgets): skipped\n");
  return 0;
#endif
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  QApplication::setApplicationName("pc_gui_harness_drawing_import");
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();
  QTemporaryDir tmp;
  pcd::MainWindow w;
  w.resize(1500, 950);
  w.show();
  QTest::qWaitForWindowExposed(&w);
  w.activateWindow();
  QApplication::setActiveWindow(&w);
  QTest::qWaitForWindowActive(&w, 3000);
  pump(600);
  run(w, tmp.path());
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  w.setAutoYes(true);
  return g_fail;
}
