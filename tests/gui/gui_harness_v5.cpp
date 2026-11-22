// pc_gui_harness_v5 — reviewer regressions on the 09-DB01 atomizer sample in the real designer window:
//  * the Plane dialog knows kind=axisAngle (editing Dz_Slot must not turn it into an edge plane / demand an edge;
//    a new axisAngle plane can be created without picking anything)
//  * the hardware sample shows 5 bodies and no fluid volume; construction planes (show=0) stay in the model
//  * regeneration inside the GUI process is not slowed down by dirty AVX state (pc/SimdState.h)
//   pc_gui_harness_v5      Exit code = number of failed checks.
#include "FeatureDialog.h"
#include "MainWindow.h"
#include "pc/Model.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTest>
#include <QTreeWidget>
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
QPushButton* okButton(QWidget* dlg) {
  for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Uygula" || b->text() == "Oluştur") return b;
  return nullptr;
}
bool modelOk(const pcd::MainWindow& w) {
  bool ok = !w.modelResult().shape.IsNull();
  for (const auto& s : w.modelResult().status) if (!s.ok) ok = false;
  return ok;
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
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  QApplication::setStyle("Fusion");
  pcd::MainWindow w;
  w.setAutoYes(true);
  w.show();
  pump(300);
  check(w.openFile(QString(PC_SAMPLES_DIR) + "/enjektor_09DB01.pcad"), "open samples/enjektor_09DB01.pcad");
  pump(900);
  check(modelOk(w), "atomizer model regenerates in the designer");
  check(w.modelResult().bodies.size() == 5 && w.modelResult().body("AkisHacmi") == nullptr,
        QString("hardware sample: 5 bodies, no fluid volume (%1)").arg(w.modelResult().bodies.size()));
  check(w.modelResult().datumPlanes.size() == 2, "both construction planes are evaluated");
  const pc::Feature* dz = w.document().feature("Dz_Slot");
  check(dz && dz->prop("show", "1") == "0", "Dz_Slot is a hidden construction plane (show=0)");

  // GUI-process regeneration speed: paint first (Qt/GL may leave dirty AVX state), then regenerate
  w.repaint();
  pump(200);
  QElapsedTimer t;
  t.start();
  const pc::model::ModelResult r = pc::model::regenerate(w.document());
  const qint64 ms = t.elapsed();
  std::printf("       regeneration inside the GUI process: %lld ms\n", static_cast<long long>(ms));
  check(r.ok && ms < 3000, QString("regeneration in the GUI process is fast (%1 ms; was ~1800 before the SIMD fix)").arg(ms));

  // --- edit the existing axisAngle plane: kind, axis and angle must survive an unchanged OK
  w.openFeatureEditor("Dz_Slot");
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "plane dialog opened for Dz_Slot");
  if (auto* dlg = w.featureDialog()) {
    check(dlg->planeKind() == "axisAngle", "dialog shows kind axisAngle (" + dlg->planeKind() + ")");
    check(!dlg->wantsEdges() && !dlg->wantsFaces(), "axisAngle needs no edge / face pick");
    const pc::Feature nf = dlg->feature();
    check(nf.prop("kind") == "axisAngle" && nf.prop("axis") == "Z" && nf.prop("angle") == "slot_az" && nf.refs.empty(),
          "dialog round-trips kind/axis/angle (angle = " + QString::fromStdString(nf.prop("angle")) + ")");
    check(nf.prop("show", "1") == "0", "dialog keeps show=0");
    if (auto* ok = okButton(dlg)) QTest::mouseClick(ok, Qt::LeftButton);
    check(waitFor([&] { return w.featureDialog() == nullptr; }, 5000), "OK accepted without an edge reference");
  }
  pump(600);
  dz = w.document().feature("Dz_Slot");
  check(dz && dz->prop("kind") == "axisAngle" && dz->prop("angle") == "slot_az", "Dz_Slot unchanged after OK");
  check(modelOk(w) && w.modelResult().bodies.size() == 5, "model still regenerates (slot sketch still on its plane)");

  // --- create a new axisAngle plane from the generic plane dialog, no picks
  const size_t n0 = w.document().features.size();
  w.openPlaneDialog(QString(), "edgeNormal");
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "new plane dialog opened");
  if (auto* dlg = w.featureDialog()) {
    bool set = false;
    for (auto* cb : dlg->findChildren<QComboBox*>()) {
      const int i = cb->findData("axisAngle");
      if (i >= 0) { cb->setCurrentIndex(i); set = true; }
    }
    check(set, "kind combo offers axisAngle");
    pump(400);
    check(dlg->planeKind() == "axisAngle" && dlg->feature().refs.empty(), "kind switched to axisAngle");
    if (auto* ok = okButton(dlg)) QTest::mouseClick(ok, Qt::LeftButton);
    check(waitFor([&] { return w.featureDialog() == nullptr; }, 5000), "new axisAngle plane accepted without references");
  }
  pump(600);
  check(w.document().features.size() == n0 + 1, "one feature added");
  if (w.document().features.size() == n0 + 1) {
    const pc::Feature& nf = w.document().features.back();
    check(nf.type == pc::FeatureType::Plane && nf.prop("kind") == "axisAngle", "new feature is an axisAngle plane");
    check(w.modelResult().datumPlanes.count(nf.id) == 1 && modelOk(w), "new plane evaluated, model ok");
  }
  std::printf("%d failure(s)\n", g_fail);
  return g_fail;
}
