// pc_gui_harness_v3 — a parameter edited in the parameter table while a feature dialog is open must
// survive the dialog's OK (the dialog used to commit a stale document snapshot).
//   pc_gui_harness_v3      Exit code = number of failed checks.
#include "MainWindow.h"
#include "pc/Model.h"
#include <QApplication>
#include <QPushButton>
#include <QStandardItemModel>
#include <QSurfaceFormat>
#include <QTest>
#include <cstdio>
#include <functional>

namespace {
int g_fail = 0;
void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  if (!ok) ++g_fail;
}
void pump(int ms) { QTest::qWait(ms); }
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  for (int t = 0; t < timeoutMs; t += 30) {
    if (pred()) return true;
    QTest::qWait(30);
  }
  return pred();
}
std::string paramExpr(const pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<none>");
}
bool setTableExpr(pcd::MainWindow& w, const QString& name, const QString& expr) {
  for (auto* m : w.findChildren<QStandardItemModel*>())
    for (int r = 0; r < m->rowCount(); ++r) {
      auto* n = m->item(r, 0);
      if (n && n->text() == name && m->columnCount() > 2 && m->item(r, 1)) { m->item(r, 1)->setText(expr); return true; }
    }
  return false;
}
QPushButton* okButton(QWidget* dlg) {
  for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Uygula" || b->text() == "Oluştur") return b;
  return nullptr;
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
  pcd::MainWindow w;
  w.setAutoYes(true);
  w.show();
  pump(300);
  check(w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad"), "open samples/bracket.pcad");
  pump(700);
  const std::string w0 = paramExpr(w, "W");
  check(w0 != "<none>" && w0 != "123", "parameter W exists (" + QString::fromStdString(w0) + ")");

  std::string extId;
  for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Extrude) { extId = ft.id; break; }
  check(!extId.empty(), "bracket has an Extrude feature");
  w.openFeatureEditor(extId);
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "feature dialog opened");
  pump(150);
  check(setTableExpr(w, "W", "123"), "W edited in the parameter table while the dialog is open");
  check(waitFor([&] { return paramExpr(w, "W") == "123"; }, 5000), "table edit committed to the document (W = 123)");
  pump(300);
  auto* dlg = w.featureDialog();
  check(dlg != nullptr, "dialog still open after the table edit");
  QPushButton* ok = dlg ? okButton(dlg) : nullptr;
  check(ok != nullptr, "dialog OK button found");
  if (ok) QTest::mouseClick(ok, Qt::LeftButton);
  check(waitFor([&] { return w.featureDialog() == nullptr; }, 5000), "dialog closed after OK");
  pump(500);
  check(paramExpr(w, "W") == "123", "W change survives the dialog's OK (W = " + QString::fromStdString(paramExpr(w, "W")) + ")");
  check(w.document().feature(extId) != nullptr, "edited feature still present");
  bool allOk = !w.modelResult().shape.IsNull();
  for (const auto& s : w.modelResult().status) if (!s.ok) allOk = false;
  check(allOk, "model regenerates after OK");
  std::printf("%d failure(s)\n", g_fail);
  return g_fail;
}
