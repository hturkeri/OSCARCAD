// WP-3 GUI harness: "Teknik resimden içe aktar" wizard driven like a user (Xvfb or QPA offscreen).
//   run 1  plate4 fixture chosen through "Gözat…" (Qt file dialog, AA_DontUseNativeDialogs), all 7 pages checked,
//          a parameter renamed in the dimension table (edit replay), strategy toggled, Bitir -> ONE commit whose
//          merged document has the right volume and clash-renamed parameters.
//   run 2  shaft fixture, projection switched to ISO A (3. açı) + "yeniden hesapla", then İptal -> no commit.
//   run 3  installDrawingImportAction: menu entry, trigger opens the modal wizard, İptal.
#include "../../gui/harness_robust.h"
#include "pc/Model.h"
#include "pc/drawing/Drawing.h"
#include "pc/drawing/ui/ImportAction.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QSpinBox>
#include <QTableWidget>
#include <QWizard>
#include <cmath>
#include <set>

namespace {

int g_fail = 0, g_checks = 0;
void check(bool ok, const QString& what) {
  ++g_checks;
  std::printf("[%s] %s\n", ok ? " OK " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  if (!ok) ++g_fail;
  pch::kick();
}

QString fixture(const char* n) { return QString(PC_DRAWING_TEST_DIR) + "/fixtures/wp3/" + n + ".pcdrw.json"; }

template <class T> T* onPage(QWizard* w, const char* name) { return w->currentPage() ? w->currentPage()->findChild<T*>(name) : nullptr; }

int findRow(QTableWidget* t, int col, const QString& text) {
  if (!t) return -1;
  for (int r = 0; r < t->rowCount(); ++r) if (t->item(r, col) && t->item(r, col)->text() == text) return r;
  return -1;
}

// One scripted wizard session: `step(wizard, pageId)` is called once per page visit; returns true when it has
// pressed a button (the pump then waits for the page to change).
struct Session {
  std::function<void(QWizard*, int)> step;
  std::function<void(QFileDialog*)> fileDialog;
  std::set<int> visited;
  QPointer<QWizard> wiz;
  bool sawWizard = false;
};

QTimer* startPump(Session& s) {
  auto* t = new QTimer;
  QObject::connect(t, &QTimer::timeout, [&s] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m) return;
    if (pch::enforceDeadline(m)) return;
    if (auto* fd = qobject_cast<QFileDialog*>(m)) {
      if (!fd->property("pchHandled").toBool()) { fd->setProperty("pchHandled", true); if (s.fileDialog) s.fileDialog(fd); }
      return;
    }
    if (auto* w = qobject_cast<QWizard*>(m)) {
      if (!s.sawWizard) { s.sawWizard = true; pch::expectCloseWithin(w, 180000); }
      s.wiz = w;
      int id = w->currentId();
      if (s.visited.count(id)) return;
      s.visited.insert(id);
      s.step(w, id);
    }
  });
  t->start(50);
  return t;
}

void next(QWizard* w) { pch::pressLater(w->button(QWizard::NextButton), 20); }

}  // namespace

// Prefer a real X server (Xvfb): re-exec under xvfb-run so the OpenGL 3D preview (ShapeViewer) is exercised too.
// Falls back to the QPA platform of the environment (ctest: offscreen) when xvfb-run is missing (Windows) or
// PC_WIZARD_NO_XVFB=1.
int reexecUnderXvfb(int argc, char** argv) {
  if (qEnvironmentVariableIsSet("PC_WIZARD_HARNESS_CHILD") || qEnvironmentVariableIntValue("PC_WIZARD_NO_XVFB")) return -1;
  const QString xvfb = QStandardPaths::findExecutable("xvfb-run");
  if (xvfb.isEmpty()) return -1;
  QCoreApplication core(argc, argv);
  QStringList args{"-a", "-s", "-screen 0 1600x1000x24", QCoreApplication::applicationFilePath()};
  QProcess p;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("PC_WIZARD_HARNESS_CHILD", "1");
  env.insert("QT_QPA_PLATFORM", "xcb");
  p.setProcessEnvironment(env);
  p.setProcessChannelMode(QProcess::ForwardedChannels);
  p.start(xvfb, args);
  if (!p.waitForStarted(10000)) return -1;
  p.waitForFinished(-1);
  if (p.exitStatus() != QProcess::NormalExit) { std::printf("[FAIL] harness crashed under xvfb-run\n"); return 2; }
  std::printf("(run under xvfb-run, exit %d)\n", p.exitCode());
  return p.exitCode();
}

int main(int argc, char** argv) {
  if (int rc = reexecUnderXvfb(argc, argv); rc >= 0) return rc;
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  std::printf("QPA platform: %s\n", qPrintable(QGuiApplication::platformName()));
  pch::checkFn() = [](bool ok, const QString& w) { check(ok, w); };
  pch::startWatchdog([] { return g_fail; });
  using namespace pc::drawing;

  // ------------------------------------------------------------------ run 1: plate4, full walk, commit
  {
    int commits = 0;
    pc::Document committed;
    QString undo;
    ui::DrawingImportHost host;
    host.currentDocument = [] {
      pc::Document d;
      pc::Parameter W;
      W.name = "W"; W.expression = "5";
      d.params.add(W);
      return d;
    };
    host.commit = [&](const pc::Document& d, const QString& t) { ++commits; committed = d; undo = t; };
    Session s;
    s.fileDialog = [](QFileDialog* fd) {
      check(fd->isVisible(), "file dialog opened as a Qt widget dialog (AA_DontUseNativeDialogs)");
      // type the absolute path into the dialog's file name field (selectFile depends on the async file model)
      fd->setDirectory(QFileInfo(fixture("plate4")).absolutePath());
      auto* name = fd->findChild<QLineEdit*>("fileNameEdit");
      check(name != nullptr, "file dialog has a file name field");
      pch::typeInto(name, QFileInfo(fixture("plate4")).fileName(), "file name");
      QTimer::singleShot(20, fd, [fd] { static_cast<QDialog*>(fd)->accept(); });
    };
    s.step = [](QWizard* w, int id) {
      switch (id) {
        case 0: {
          auto* fe = onPage<QLineEdit>(w, "fileEdit");
          check(fe && fe->text().isEmpty(), "page 1: file field starts empty");
          check(!w->button(QWizard::NextButton)->isEnabled(), "page 1: İleri disabled without a file");
          pch::pressLater(onPage<QPushButton>(w, "browseButton"));
          // wait until the dialog delivered the file, then continue
          auto* poll = new QTimer(w);
          QObject::connect(poll, &QTimer::timeout, w, [w, fe, poll] {
            if (!fe || !fe->text().endsWith("plate4.pcdrw.json")) return;
            poll->stop();
            check(true, "page 1: file chosen through Gözat…");
            auto* lbl = onPage<QLabel>(w, "sniffLabel");
            check(lbl && lbl->text().contains("DrawingIR"), "page 1: source kind reported");
            next(w);
          });
          poll->start(50);
          break;
        }
        case 1: {
          auto* vt = onPage<QTableWidget>(w, "viewTable");
          check(vt && vt->rowCount() == 2, QString("page 2: two views (%1)").arg(vt ? vt->rowCount() : -1));
          auto* sc = onPage<QLabel>(w, "scaleLabel");
          check(sc && sc->text().contains("1:2"), "page 2: title-block scale 1:2 shown: " + (sc ? sc->text() : ""));
          check(sc && sc->text().contains("Ölçülerden"), "page 2: scale from dimensions shown");
          auto* pj = onPage<QComboBox>(w, "projectionCombo");
          check(pj && pj->currentIndex() == 0, "page 2: projection ISO E (from the title-block symbol)");
          check(onPage<QWidget>(w, "overlayLayout") != nullptr, "page 2: overlay view present");
          next(w);
          break;
        }
        case 2: {
          auto* rc = onPage<QLabel>(w, "roleCounts");
          check(rc && rc->text().contains("ilkel"), "page 3: role statistics: " + (rc ? rc->text() : ""));
          next(w);
          break;
        }
        case 3: {
          auto* t = onPage<QTableWidget>(w, "dimTable");
          check(t && t->rowCount() == 8, QString("page 4: 8 dimensions (%1)").arg(t ? t->rowCount() : -1));
          int r = findRow(t, 6, "Dh1");
          check(r >= 0, "page 4: hole diameter parameter Dh1 listed");
          if (r >= 0) {
            t->item(r, 6)->setText("Dh_delik");   // user edit -> setParamName -> re-run from S6
            check(findRow(t, 6, "Dh_delik") >= 0, "page 4: renamed parameter survives the re-run");
          }
          next(w);
          break;
        }
        case 4: {
          auto* dl = onPage<QLabel>(w, "dofLabel");
          check(dl && dl->text().contains("DOF: 0"), "page 5: sketch fully defined: " + (dl ? dl->text() : ""));
          auto* cb = onPage<QComboBox>(w, "sketchCombo");
          check(cb && cb->count() == 2, "page 5: profile + hole sketches");
          bool found = false;
          for (int i = 0; cb && i < cb->count() && !found; ++i) {
            cb->setCurrentIndex(i);
            found = findRow(onPage<QTableWidget>(w, "paramTable"), 0, "Dh_delik") >= 0;
          }
          check(found, "page 5: Dh_delik in a sketch parameter table");
          next(w);
          break;
        }
        case 5: {
          auto* l = onPage<QListWidget>(w, "strategyList");
          check(l && l->count() >= 2, "page 6: strategy candidates listed");
          check(l && l->currentRow() == 0 && l->currentItem()->text().contains(labelTr(Strategy::ExtrudeTwoView)),
                "page 6: two-view extrude ranked first");
          auto* so = onPage<QCheckBox>(w, "sketchOnlyCheck");
          pch::tickCheckBox(so, true, "Yalnız sketch");
          check(l && l->currentItem() && l->currentItem()->text().contains(labelTr(Strategy::SketchOnly)), "page 6: Yalnız sketch selects the fallback");
          pch::tickCheckBox(so, false, "Yalnız sketch");
          check(l && l->currentRow() == 0, "page 6: unticking returns to the best candidate");
          auto* ci = onPage<QLabel>(w, "candidateLabel");
          check(ci && ci->text().contains("hacim"), "page 6: candidate volume shown");
          if (QGuiApplication::platformName() == "offscreen") check(onPage<QLabel>(w, "previewPlaceholder") != nullptr, "page 6: preview placeholder (no OpenGL)");
          else check(onPage<QWidget>(w, "preview") != nullptr && onPage<QWidget>(w, "preview")->isVisible(), "page 6: 3D preview viewer shown");
          next(w);
          break;
        }
        case 6: {
          auto* t = onPage<QTableWidget>(w, "summaryParams");
          check(t && findRow(t, 0, "Dh_delik") >= 0 && findRow(t, 0, "W") >= 0, "page 7: parameters listed");
          int r = findRow(t, 0, "W");
          check(r >= 0 && t->item(r, 2)->text() == "[D]", "page 7: W tagged [D]");
          pch::pressLater(w->button(QWizard::FinishButton), 20);
          break;
        }
      }
    };
    QTimer* pump = startPump(s);
    int res = ui::runDrawingImportWizard(nullptr, host);
    delete pump;
    check(res == QDialog::Accepted, "run 1: wizard accepted");
    check(commits == 1, QString("run 1: exactly one commit (%1)").arg(commits));
    check(undo == "Teknik resimden içe aktar", "run 1: undo text");
    check(committed.params.find("W") && committed.params.find("W")->expression == "5", "run 1: existing W untouched");
    check(committed.params.find("W_2") != nullptr, "run 1: imported W renamed to W_2");
    check(committed.params.find("Dh_delik") != nullptr, "run 1: user-renamed parameter merged");
    auto mr = pc::model::regenerate(committed);
    auto mp = pc::model::massProperties(mr.shape);
    check(mr.ok && std::abs(mp.volume - 56858.407) < 1.0, QString("run 1: merged volume %1 (ref 56858.4)").arg(mp.volume));
  }

  // ------------------------------------------------------------------ run 2: shaft, projection edit, cancel
  {
    int commits = 0;
    ui::DrawingImportHost host;
    host.currentDocument = [] { return pc::Document(); };
    host.commit = [&](const pc::Document&, const QString&) { ++commits; };
    Session s;
    s.step = [](QWizard* w, int id) {
      if (id == 0) {
        auto* fe = onPage<QLineEdit>(w, "fileEdit");
        check(fe && fe->text().endsWith("shaft.pcdrw.json"), "run 2: initial file preset");
        next(w);
      } else if (id == 1) {
        auto* pj = onPage<QComboBox>(w, "projectionCombo");
        if (pj) {
          pj->setCurrentIndex(1);
          emit pj->activated(1);    // user choice -> setProjection edit -> re-run from S3
        }
        check(pj && pj->currentIndex() == 1, "run 2: projection ISO A kept after the re-run");
        auto* sc = onPage<QLabel>(w, "scaleLabel");
        check(sc && sc->text().contains("2:1"), "run 2: scale 2:1");
        pch::pressLater(onPage<QPushButton>(w, "rerunButton"));
        QTimer::singleShot(300, w, [w] {
          auto* st = onPage<QLabel>(w, "statusLabel");
          check(st && !st->text().isEmpty(), "run 2: status after 'yeniden hesapla': " + (st ? st->text() : ""));
          pch::pressLater(w->button(QWizard::CancelButton));
        });
      }
    };
    QTimer* pump = startPump(s);
    int res = ui::runDrawingImportWizard(nullptr, host, fixture("shaft"));
    delete pump;
    check(res == QDialog::Rejected, "run 2: İptal rejects");
    check(commits == 0, "run 2: no commit after İptal");
  }

  // ------------------------------------------------------------------ run 3: menu action
  {
    QMainWindow mw;
    QMenu menu("Dosya");
    ui::DrawingImportHost host;
    host.currentDocument = [] { return pc::Document(); };
    host.commit = [](const pc::Document&, const QString&) {};
    QAction* a = ui::installDrawingImportAction(&mw, &menu, host);
    check(a && menu.actions().contains(a) && a->text().startsWith("Teknik resimden"), "run 3: menu action installed");
    Session s;
    s.step = [](QWizard* w, int id) {
      check(id == 0 && w->windowTitle() == "Teknik resimden içe aktar" && w->isModal(), "run 3: modal wizard opened from the action");
      pch::pressLater(w->button(QWizard::CancelButton), 20);
    };
    QTimer* pump = startPump(s);
    a->trigger();
    delete pump;
    check(s.sawWizard, "run 3: wizard was shown");
  }

  std::printf("%d checks, %d failure(s)\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
