// SC23 acceptance harness (docs/sc2023/ANALYSIS.md §6, docs/sc2023/P0_CONTRACT.md).
//   pc_gui_harness_sc2023 [--shots]       Exit code = number of failed checks.
// The REAL MainWindow at 1240x800. This file (main + helpers) is shared and frozen after P0-SC23; the checks live in
//   sc2023_shell_checks.inc  -> runShellChecks(ctx)   owned by WP-S (shell/layout)
//   sc2023_op_checks.inc     -> runOpChecks(ctx)      owned by WP-O (operation/state)
// --shots writes docs/sc2023/shots/*.png (PC_SC23_SHOTS_DIR).
#include "MainWindow.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "OperationState.h"
#include "ViewportHud.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <functional>

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
void shot(QWidget& w, const QString& name) {
  if (!g_shots) return;
  pump(250);
  const QString dir = QString(PC_SC23_SHOTS_DIR);
  QDir().mkpath(dir);
  w.grab().save(dir + "/" + name);
  note(QString("shot %1/%2").arg(dir, name));
}

// message boxes are answered (Yes / Ok), the manufacturing review is accepted, other modals are closed
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (m->property("hHandled").toBool()) return;
    m->setProperty("hHandled", true);
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      note("message box: " + mb->text().left(120).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (auto* d = qobject_cast<QDialog*>(m)) {
      note(QString("modal %1 -> accept").arg(m->metaObject()->className()));
      QTimer::singleShot(50, d, &QDialog::accept);
    }
  });
  t->start();
}

// Shared context handed to both check files.
struct Sc23Ctx {
  pcd::MainWindow& w;
  QString tmpDir;
  pcd::ToolManager* tools() const { return w.toolManager(); }
  pcd::ViewportHud* hud() const { return w.findChild<pcd::ViewportHud*>("viewportHud"); }
  bool openSample(const QString& file) {
    const bool ok = w.openFile(QString(PC_SAMPLES_DIR) + "/" + file);
    pump(300);
    return ok;
  }
  // back to Select, empty selection, no HUD message (each check group starts from here)
  void reset() {
    if (tools()) { for (int i = 0; i < 6 && tools()->cancel(); ++i) {} tools()->activate("select"); }
    if (w.selection()) w.selection()->clear();
    if (hud()) hud()->clearMessage();
    pump(100);
  }
};
} // namespace

#include "sc2023_shell_checks.inc"
#include "sc2023_op_checks.inc"

int main(int argc, char** argv) {
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  QApplication::setApplicationName("pc_gui_harness_sc2023");
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();
  QTemporaryDir tmp;
  {
    pcd::MainWindow w;
    w.setAutoYes(true);
    w.resize(1240, 800);
    w.show();
    QTest::qWaitForWindowExposed(&w);
    w.activateWindow();
    QApplication::setActiveWindow(&w);
    if (!QTest::qWaitForWindowActive(&w, 3000)) note("window not active (shortcuts need the active window)");
    pump(500);
    Sc23Ctx ctx{w, tmp.path()};
    runShellChecks(ctx);
    ctx.reset();
    runOpChecks(ctx);
  }
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  return g_fail;
}
