// "Teknik resimden içe aktar" on the two REAL drawings (R1-R4 integration) through the REAL designer:
//
//   for tests/drawing/real/radrad_swirler_SWC-RR-001.pdf and simplex_atomizer_ATM-PS-001.pdf (copied to a temp dir):
//   Dosya menu -> wizard, all 7 pages (file typed in, scale label, dimension table, sketch DOF, section-revolve
//   candidate first, summary warns about the title-block scale mismatch) -> Bitir -> ONE undo step -> body count,
//   volume within 3 % of the R4 ground truth, circular patterns (8 + 10 / 3 + 3), key parameters in Gruplar.
//
//   pc_gui_harness_drawing_real [--shots]   --shots writes docs/drawing_import/shots/real_*.png
//   Exit code = number of failed checks.
#include "MainWindow.h"
#include "StructurePanel.h"
#include "harness_robust.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUndoStack>
#include <QWizard>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>
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
void shotOf(QWidget* wdg, const QString& name) {
  if (!g_shots || !wdg) return;
  QDir().mkpath(PC_DRW_SHOTS_DIR);
  const QString path = QString(PC_DRW_SHOTS_DIR) + "/" + name;
  note(QString("shot %1 %2").arg(path, wdg->grab().save(path) ? "saved" : "FAILED"));
}

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
      if (!b) b = mb->button(QMessageBox::No);   // "save changes?" on Yeni: discard
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      if (m->property("dhHandled").toBool()) return;
      m->setProperty("dhHandled", true);
      note("manufacturing review dialog -> 'Yine de Uygula'");
      QPushButton* apply = nullptr;
      for (auto* b : m->findChildren<QPushButton*>()) if (b->text().remove('&') == "Yine de Uygula") apply = b;
      if (apply) pch::pressLater(apply, 50);
      else if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
      return;
    }
    if (g_onModal) g_onModal(m);
  });
  t->start();
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

struct Real {
  const char* key;        // shot / gt stem
  const char* pdf;
  const char* gt;
  QStringList params;     // must exist and be listed in Gruplar
  std::vector<int> patterns;
  QString partName;       // a body name of the result
};

void importOne(pcd::MainWindow& w, const QString& tmpDir, const Real& R) {
  section(QString("wizard on %1 (Dosya menu)").arg(R.pdf));
  nlohmann::json gt;
  {
    std::ifstream f(std::string(PC_DRAWING_REAL_DIR) + "/ground_truth/" + R.gt);
    gt = nlohmann::json::parse(f, nullptr, false);
  }
  check(!gt.is_discarded() && gt.contains("volume"), QString("ground truth %1 read").arg(R.gt));
  const QString pdf = tmpDir + "/" + R.pdf;
  QFile::remove(pdf);
  QFile::copy(QString(PC_DRAWING_REAL_DIR) + "/" + R.pdf, pdf);
  check(QFileInfo::exists(pdf), "PDF copied: " + pdf);
  QAction* act = w.findChild<QAction*>("actionDrawingImport");
  const int undo0 = w.undoStack()->index();
  std::set<int> visited;
  QPointer<QWizard> wiz;
  bool finished = false;
  const QString k = R.key;
  g_onModal = [&](QWidget* m) {
    auto* wz = qobject_cast<QWizard*>(m);
    if (!wz) return;
    if (!wiz) { wiz = wz; wz->resize(1400, 900); pch::expectCloseWithin(wz, 300000); }
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
        check(lbl && lbl->text().contains("VectorPdf"), "page 1: vector PDF detected: " + (lbl ? lbl->text().left(60) : ""));
        next(100);
        break;
      }
      case 1: {
        auto* vt = qobject_cast<QTableWidget*>(page("viewTable"));
        check(vt && vt->rowCount() >= 1, QString("page 2 (Sayfa düzeni): %1 view(s)").arg(vt ? vt->rowCount() : -1));
        auto* sc = qobject_cast<QLabel*>(page("scaleLabel"));
        note("page 2 scale label: " + (sc ? sc->text().left(100) : QString("-")));
        next(300);
        break;
      }
      case 2: next(100); break;
      case 3: {
        auto* t = qobject_cast<QTableWidget*>(page("dimTable"));
        check(t && t->rowCount() >= 10, QString("page 4 (Ölçüler): %1 dimensions (glyph text recovered, R1)").arg(t ? t->rowCount() : -1));
        QTimer::singleShot(500, wz, [wz, k] { shotOf(wz, "real_" + k + "_wizard_olculer.png"); });
        next(800);
        break;
      }
      case 4: {
        auto* dl = qobject_cast<QLabel*>(page("dofLabel"));
        note("page 5 dof: " + (dl ? dl->text().left(80) : QString("-")));
        next(100);
        break;
      }
      case 5: {
        auto* l = qobject_cast<QListWidget*>(page("strategyList"));
        const QString first = l && l->count() ? l->item(0)->text() : QString();
        check(l && l->count() >= 2 && (first.contains("Kesit") || first.contains("kesit")),
              "page 6 (3B): section revolve (R2+R3) candidate first: " + first.left(80));
        QTimer::singleShot(1500, wz, [wz, k] { shotOf(wz, "real_" + k + "_wizard_3b.png"); });
        next(1900);
        break;
      }
      case 6: {
        auto* wl = qobject_cast<QListWidget*>(page("warningList"));
        bool scaleWarn = false;
        for (int i = 0; wl && i < wl->count(); ++i) scaleWarn |= wl->item(i)->text().contains("Antet ölçeği");
        check(scaleWarn, "page 7 (Özet): title-block scale mismatch warning kept");
        auto* mr = qobject_cast<QRadioButton*>(page("mergeRadio"));
        check(mr && mr->isChecked(), "page 7: merge into the current document");
        QTimer::singleShot(150, wz, [wz, &finished] { finished = true; pch::pressLater(wz->button(QWizard::FinishButton), 20); });
        break;
      }
      default: break;
    }
  };
  if (act) QTimer::singleShot(0, act, &QAction::trigger);
  waitFor([&] { return finished && !QApplication::activeModalWidget(); }, 300000);
  g_onModal = nullptr;
  pump(600);
  check(visited.size() == 7, QString("all 7 wizard pages visited (%1)").arg(visited.size()));

  section("result in the designer: " + k);
  note(QString("features after Bitir: %1, message boxes: %2").arg(w.document().features.size()).arg(g_boxes.join(" | ").left(300)));
  check(w.undoStack()->index() == undo0 + 1 && w.undoStack()->undoText() == "Teknik resimden içe aktar",
        "ONE undo step: '" + w.undoStack()->undoText() + "'");
  const auto& doc = w.document();
  const auto& mres = w.modelResult();
  const int nb = (int)mres.bodies.size();
  check(nb == gt["body_count"].get<int>(), QString("bodies %1 (gt %2)").arg(nb).arg(gt["body_count"].get<int>()));
  bool named = false;
  for (auto& b : mres.bodies) named |= QString::fromStdString(b.name) == R.partName;
  check(named, "body named after the drawing label: " + R.partName);
  const double v = mres.shape.IsNull() ? 0 : pc::model::massProperties(mres.shape).volume, gv = gt["volume"].get<double>();
  check(std::fabs(v - gv) < 0.03 * gv, QString("volume %1 mm³ vs gt %2 (%3 %)").arg(v, 0, 'f', 2).arg(gv, 0, 'f', 2).arg(100 * (v - gv) / gv, 0, 'f', 2));
  std::multiset<int> pats;
  auto vals = doc.evaluateParams();
  for (auto& f : doc.features)
    if (!f.suppressed && f.type == pc::FeatureType::Pattern && f.prop("kind") == "circular")
      pats.insert((int)std::lround(pc::evalWith(f.prop("count", "1"), vals)));
  QStringList ps;
  for (int c : pats) ps << QString::number(c);
  check(pats == std::multiset<int>(R.patterns.begin(), R.patterns.end()), "circular patterns (slots/grooves): " + ps.join(","));
  w.structurePanel()->setCurrentPage("groups");
  pump(300);
  QStringList missing;
  for (auto& n : R.params) if (!doc.params.find(n.toStdString()) || paramRow(w, n) < 0) missing << n;
  check(missing.isEmpty(), "parameters in Gruplar: " + R.params.join(",") + (missing.isEmpty() ? "" : "  missing " + missing.join(",")));

  if (auto* v3 = w.viewer()) {
    v3->setView(pc::viewer::ShapeViewer::View::Iso);
    v3->fitAll();
    v3->setFocus();
  }
  pump(600);
  shotOf(&w, "real_" + k + "_3b.png");
  if (auto* v3 = w.viewer()) {   // half section through the axis (view only): venturi / flare, the two bodies
    const double o[3] = {0, 0, 0}, nrm[3] = {0, 1, 0};
    v3->setSectionPlane(true, o, nrm);
    v3->setView(pc::viewer::ShapeViewer::View::Iso);
    v3->fitAll();
    pump(600);
    if (g_shots) {
      QDir().mkpath(PC_DRW_SHOTS_DIR);
      const QString p = QString(PC_DRW_SHOTS_DIR) + "/real_" + k + "_3b_kesit.png";
      note("shot " + p + (v3->renderToImage(1100, 800).save(p) ? " saved" : " FAILED"));
    }
    v3->setSectionPlane(false);
    pump(200);
  }
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 5000);
  check(w.document().features.empty(), "Ctrl+Z removes the whole import (one step)");
}

void run(pcd::MainWindow& w, const QString& tmpDir) {
  const Real kReal[] = {
      {"swc_rr_001", "radrad_swirler_SWC-RR-001.pdf", "swc_rr_001.json",
       {"D_50", "D_24", "D_34", "D_14", "D_27", "D_43", "D_47", "N_pri", "N_sec", "W_pri", "OFS_pri", "OFS_sec"}, {8, 10}, "Part1"},
      {"atm_ps_001", "simplex_atomizer_ATM-PS-001.pdf", "atm_ps_001.json",
       {"D_8", "D_5", "D_3p2", "D_0p8", "L_4p2", "L_5p8", "N_slt", "N_grv", "W_slt", "W_grv", "OFS_slt"}, {3, 3}, "SWIRLER_INSERT"},
  };
  const QString only = qEnvironmentVariable("PC_DRW_REAL_ONLY");
  for (auto& R : kReal) if (only.isEmpty() || only == R.key) importOne(w, tmpDir, R);
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
  QApplication::setApplicationName("pc_gui_harness_drawing_real");
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
