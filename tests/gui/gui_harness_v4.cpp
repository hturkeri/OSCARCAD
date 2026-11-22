// pc_gui_harness_v4 — section view ("Kesit"): viewer rendering / capping / picking, the designer toggle +
// panel + menu, and the runner toolbar controls. Exit code = number of failed checks.
#include "MainWindow.h"
#include "RunnerWindow.h"
#include "pc/AppPackage.h"
#include "pc/Document.h"
#include "pc/SectionControls.h"
#include "pc/ShapeViewer.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QSlider>
#include <QSurfaceFormat>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <cstdio>
#include <functional>

namespace {
using pc::viewer::ShapeViewer;
using Preset = ShapeViewer::SectionPreset;
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
int diffPixels(const QImage& a, const QImage& b, int tol = 6) {
  if (a.size() != b.size()) return a.width() * a.height();
  int n = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x) {
      const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
      if (std::abs(qRed(p) - qRed(q)) > tol || std::abs(qGreen(p) - qGreen(q)) > tol || std::abs(qBlue(p) - qBlue(q)) > tol) ++n;
    }
  return n;
}
// mean colour of a (2r+1)^2 patch
QColor patch(const QImage& im, int cx, int cy, int r = 6) {
  long R = 0, G = 0, B = 0, n = 0;
  for (int y = cy - r; y <= cy + r; ++y)
    for (int x = cx - r; x <= cx + r; ++x) {
      if (x < 0 || y < 0 || x >= im.width() || y >= im.height()) continue;
      const QRgb p = im.pixel(x, y);
      R += qRed(p); G += qGreen(p); B += qBlue(p); ++n;
    }
  if (!n) return QColor();
  return QColor(int(R / n), int(G / n), int(B / n));
}
bool blueFamily(const QColor& c) { return c.blue() > c.red() + 40 && c.blue() > c.green() + 15; }
bool orangeFamily(const QColor& c) { return c.red() > c.blue() + 60 && c.red() > c.green() + 20; }
QString rgb(const QColor& c) { return QString("(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()); }

QString clickFace(ShapeViewer& v, const QPoint& p) {
  QString picked = "<no signal>";
  auto c = QObject::connect(&v, &ShapeViewer::facePicked, [&](const QString& n) { picked = n; });
  QTest::mouseMove(&v, p);
  QTest::mouseClick(&v, Qt::LeftButton, Qt::NoModifier, p);
  pump(30);
  QObject::disconnect(c);
  return picked;
}

// ---------------------------------------------------------------- A) bare viewer with two solids
void viewerChecks() {
  std::printf("--- viewer\n");
  // body A: 60x40x30 block with a through bore (r=10) along Z; body B: pin (r=5) inside the bore
  TopoDS_Shape block = BRepPrimAPI_MakeBox(60, 40, 30).Shape();
  TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 20, -1), gp_Dir(0, 0, 1)), 10, 32).Shape();
  TopoDS_Shape a = BRepAlgoAPI_Cut(block, bore).Shape();
  TopoDS_Shape b = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 20, 0), gp_Dir(0, 0, 1)), 5, 30).Shape();
  TopoDS_Compound comp;
  BRep_Builder bb;
  bb.MakeCompound(comp);
  bb.Add(comp, a);
  bb.Add(comp, b);
  std::map<std::string, TopoDS_Face> faces;
  std::map<std::string, QColor> colors;
  QStringList aFaces, bFaces;
  const QColor blue(60, 110, 200), orange(235, 150, 60);
  int i = 0;
  for (TopExp_Explorer ex(a, TopAbs_FACE); ex.More(); ex.Next()) {
    const std::string n = "A" + std::to_string(i++);
    faces[n] = TopoDS::Face(ex.Current()); colors[n] = blue; aFaces << QString::fromStdString(n);
  }
  i = 0;
  for (TopExp_Explorer ex(b, TopAbs_FACE); ex.More(); ex.Next()) {
    const std::string n = "B" + std::to_string(i++);
    faces[n] = TopoDS::Face(ex.Current()); colors[n] = orange; bFaces << QString::fromStdString(n);
  }
  const int W = 800, H = 600;
  ShapeViewer v;
  v.resize(W, H);
  v.setShowAxes(false);
  v.setModel(comp, faces, {}, false);
  v.setFaceColors(colors);
  v.show();
  check(QTest::qWaitForWindowExposed(&v), "viewer exposed");
  pump(200);
  check(v.size() == QSize(W, H), QString("viewer widget is %1x%2").arg(W).arg(H));
  int sectionSignals = 0;
  QObject::connect(&v, &ShapeViewer::sectionChanged, [&] { ++sectionSignals; });

  // Front view (camera on -Y): fit puts the bbox centre (30,20,15) in the image centre, 600 px / 51.75 mm.
  v.setView(ShapeViewer::View::Front);
  const double s = H / (2.0 * 22.5 * 1.15);
  const QPoint ctr(W / 2, H / 2), capA(int(W / 2 + 20 * s), H / 2), gap(int(W / 2 + 7.5 * s), H / 2);
  const QImage f0 = v.renderToImage(W, H);
  check(!v.sectionEnabled() && v.sectionPreset() == Preset::None, "section is off by default");
  check(blueFamily(patch(f0, ctr.x(), ctr.y())), "no section: centre shows the block's front face " + rgb(patch(f0, ctr.x(), ctr.y())));
  const QString front = clickFace(v, ctr);
  check(front.startsWith("A"), "no section: centre pick = block front face (" + front + ")");

  v.setSectionPreset(Preset::XZ);
  check(v.sectionEnabled() && v.sectionPreset() == Preset::XZ, "setSectionPreset(XZ) enables the section");
  check(std::fabs(v.sectionPosition() - 20.0) < 1e-6, QString("XZ plane through the bbox centre (Y = %1)").arg(v.sectionPosition()));
  check(sectionSignals == 1, "sectionChanged emitted");
  const QImage f1 = v.renderToImage(W, H);
  const int d01 = diffPixels(f0, f1);
  check(d01 > 2000, QString("enabling the section changes the image (%1 px)").arg(d01));
  const QColor cB = patch(f1, ctr.x(), ctr.y()), cA = patch(f1, capA.x(), capA.y()), cA0 = patch(f0, capA.x(), capA.y());
  check(orangeFamily(cB), "cap of the pin (body B) has B's colour family " + rgb(cB));
  check(blueFamily(cA), "cap of the block (body A) has A's colour family " + rgb(cA));
  check(cA.blue() < cA0.blue() && cA.blue() > cA0.blue() / 2, "cap is a darker tone than the lit face " + rgb(cA) + " vs " + rgb(cA0));
  // hatching: the cap is not a uniform colour
  int lo = 255, hi = 0;
  for (int x = capA.x() - 20; x <= capA.x() + 20; ++x) { const int bl = qBlue(f1.pixel(x, capA.y())); lo = std::min(lo, bl); hi = std::max(hi, bl); }
  check(hi - lo > 12, QString("cap is hatched (blue channel spans %1..%2)").arg(lo).arg(hi));
  // picking: caps are not pickable; through the removed half one picks the faces behind
  const QString onCap = clickFace(v, ctr), onCapA = clickFace(v, capA), inGap = clickFace(v, gap);
  check(onCap.isEmpty(), "click on the pin's cap picks nothing ('" + onCap + "')");
  check(onCapA.isEmpty(), "click on the block's cap picks nothing ('" + onCapA + "')");
  check(inGap.startsWith("A") && inGap != front, "click in the bore gap picks the bore wall behind the cut (" + inGap + "), not the removed front face");

  // generic plane == preset
  const double o[3] = {0, 20, 0}, n[3] = {0, 2, 0};
  v.setSectionPlane(true, o, n);
  check(v.sectionEnabled() && v.sectionPreset() == Preset::None, "setSectionPlane enables a generic plane");
  check(diffPixels(f1, v.renderToImage(W, H)) == 0, "generic plane (origin, normal) renders like the XZ preset");

  // hidden body: no cap, the faces behind become visible / pickable
  v.setSectionPreset(Preset::XZ);
  v.setHiddenFaces(bFaces);
  const QImage f2 = v.renderToImage(W, H);
  check(blueFamily(patch(f2, ctr.x(), ctr.y())), "hidden body gets no cap (bore wall visible) " + rgb(patch(f2, ctr.x(), ctr.y())));
  check(clickFace(v, ctr).startsWith("A"), "with the pin hidden the centre pick is the bore wall");
  v.setHiddenFaces({});

  // flip + offset
  v.setSectionPreset(Preset::XZ, 0.5, false);
  check(std::fabs(v.sectionPosition() - 30.0) < 1e-6, QString("offset 0.5 moves the plane to Y = %1").arg(v.sectionPosition()));
  const QImage f3 = v.renderToImage(W, H);
  check(blueFamily(patch(f3, ctr.x(), ctr.y())) && diffPixels(f1, f3) > 500, "offset plane beyond the bore: block cap in the centre");
  v.setSectionPreset(Preset::XZ, 0.0, true);
  { const QImage ff = v.renderToImage(W, H); const int df = diffPixels(f0, ff); if (qEnvironmentVariableIsSet("PC_V4_DUMP")) { ff.save("/tmp/sec/flip.png"); f0.save("/tmp/sec/f0.png"); f1.save("/tmp/sec/f1.png"); }
    check(df < 800, QString("flipped XZ seen from the front shows the untouched near half (only silhouette pixels differ: %1)").arg(df)); }

  // quarter cut, isometric
  v.setView(ShapeViewer::View::Iso);
  const QImage i0 = (v.setSectionPreset(Preset::None), v.renderToImage(W, H));
  v.setSectionPreset(Preset::XZ);
  const QImage i1 = v.renderToImage(W, H);
  v.setSectionPreset(Preset::Quarter);
  const QImage i2 = v.renderToImage(W, H);
  check(v.sectionEnabled() && v.sectionPreset() == Preset::Quarter, "quarter cut enabled");
  check(diffPixels(i0, i2) > 2000 && diffPixels(i1, i2) > 2000 && diffPixels(i0, i1) > diffPixels(i0, i2),
        QString("quarter cut removes less than the half cut (%1 vs %2 px)").arg(diffPixels(i0, i2)).arg(diffPixels(i0, i1)));
  v.setSectionPreset(Preset::XY);
  check(diffPixels(i0, v.renderToImage(W, H)) > 2000 && std::fabs(v.sectionPosition() - 15.0) < 1e-6, "XY section at mid height");
  v.setSectionPreset(Preset::YZ);
  check(diffPixels(i0, v.renderToImage(W, H)) > 2000 && std::fabs(v.sectionPosition() - 30.0) < 1e-6, "YZ section at mid length");

  // highlights + ghost + datum plane together with a section must still render; off restores everything
  v.setSectionPreset(Preset::Quarter);
  v.setHighlightedFaces(aFaces.mid(0, 2));
  v.setGhost(b);
  std::vector<pc::viewer::DatumPlaneDisplay> pl(1);
  pl[0].name = "P"; pl[0].origin[0] = 30; pl[0].origin[1] = 20; pl[0].origin[2] = 15; pl[0].halfSize = 25;
  v.setDatumPlanes(pl);
  check(!v.renderToImage(W, H).isNull(), "section + highlights + ghost + datum plane render");
  v.clearHighlights(); v.clearGhost(); v.setDatumPlanes({});
  v.setSectionPreset(Preset::None);
  check(!v.sectionEnabled(), "section switched off");
  check(diffPixels(i0, v.renderToImage(W, H)) == 0, "toggling the section off restores the image exactly");

  // model replaced while a preset is active: the plane follows the new bbox
  v.setSectionPreset(Preset::XZ);
  const int sig0 = sectionSignals;
  v.setModel(b, {}, {}, true);
  check(std::fabs(v.sectionPosition() - 20.0) < 1e-6 && sectionSignals == sig0 + 1, "preset re-evaluated after setModel");
  v.setModel(comp, faces, {}, true);
  v.setSectionPreset(Preset::None);

  // optional: translucent body
  v.setTransparentFaces(aFaces);
  const QImage t1 = v.renderToImage(W, H);
  check(diffPixels(i0, t1) > 2000, "translucent block changes the image");
  v.setTransparentFaces({});
  check(diffPixels(i0, v.renderToImage(W, H)) == 0, "translucency off restores the image");

  bool ok = true;
  check(ShapeViewer::sectionPresetFromString("XZ", &ok) == Preset::XZ && ok, "parse 'XZ'");
  check(ShapeViewer::sectionPresetFromString("quarter", &ok) == Preset::Quarter && ok, "parse 'quarter'");
  ShapeViewer::sectionPresetFromString("bogus", &ok);
  check(!ok, "unknown preset string rejected");
}

// ---------------------------------------------------------------- B) designer
void designerChecks() {
  std::printf("--- designer\n");
  pcd::MainWindow w;
  w.setAutoYes(true);
  w.show();
  pump(300);
  check(w.openFile(QString(PC_SAMPLES_DIR) + "/v2_multibody.pcad"), "open samples/v2_multibody.pcad");
  pump(800);
  auto* v = w.viewer();
  auto* sc = w.sectionControls();
  check(sc != nullptr && !sc->isVisible() && !v->sectionEnabled(), "section panel hidden / section off initially");
  const std::string before = pc::toJson(w.document()).dump();
  const QImage img0 = v->renderToImage(700, 500);

  QToolButton* btn = nullptr;
  // SC23 WP-S: a ribbon group may also hold a hidden compact copy of its buttons (Ribbon.h): take the visible one
  for (auto* tb : w.findChildren<QToolBar*>())
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(sc->toggleAction())); b && b->isVisible()) btn = b;
  check(btn != nullptr && btn->isVisible(), "toolbar has the 'Kesit' toggle");
  bool inMenu = false;
  for (auto* m : w.findChildren<QMenu*>()) if (m->title().contains("Görünüm") && m->actions().contains(sc->toggleAction())) inMenu = true;
  check(inMenu, "Görünüm menu has the 'Kesit' toggle");
  if (btn) QTest::mouseClick(btn, Qt::LeftButton);
  pump(200);
  check(sc->sectionOn() && v->sectionEnabled() && v->sectionPreset() == Preset::XZ, "toolbar click enables the XZ section");
  check(sc->isVisible() && sc->parentWidget() == v, "section panel shown over the viewer");
  const QImage img1 = v->renderToImage(700, 500);
  check(diffPixels(img0, img1) > 1000, QString("designer view changes (%1 px)").arg(diffPixels(img0, img1)));

  auto* combo = sc->findChild<QComboBox*>("sectionPreset");
  auto* slider = sc->findChild<QSlider*>("sectionOffset");
  auto* flip = sc->findChild<QCheckBox*>("sectionFlip");
  check(combo && slider && flip && combo->count() == 4, "panel has preset combo (4 presets), offset slider and flip");
  if (combo && slider && flip) {
    const double p0 = v->sectionPosition();
    slider->setValue(50);
    check(v->sectionPosition() > p0 + 1e-6, QString("offset slider moves the plane (%1 -> %2 mm)").arg(p0).arg(v->sectionPosition()));
    const QImage a = v->renderToImage(700, 500);
    QTest::mouseClick(flip, Qt::LeftButton, Qt::NoModifier, QPoint(8, flip->height() / 2));
    check(sc->flipped() && diffPixels(a, v->renderToImage(700, 500)) > 1000, "flip shows the other side");
    combo->setCurrentIndex(combo->findData(int(Preset::Quarter)));
    check(v->sectionPreset() == Preset::Quarter && !slider->isEnabled() && slider->value() == 0, "combo -> çeyrek kesit (slider reset/disabled)");
    if (auto* aYZ = w.findChild<QAction*>("actSectionYZ")) {
      aYZ->trigger();
      check(v->sectionPreset() == Preset::YZ && sc->preset() == Preset::YZ && slider->isEnabled(), "menu Kesit düzlemi > YZ");
    } else check(false, "menu action actSectionYZ exists");
  }
  check(pc::toJson(w.document()).dump() == before, "section state is view-only: document unchanged");
  sc->setFlipped(false);
  sc->setSectionOn(false);
  pump(100);
  check(!v->sectionEnabled() && !sc->isVisible(), "toggle off hides the panel");
  check(diffPixels(img0, v->renderToImage(700, 500)) == 0, "designer image restored after toggling off");
  check(sc->applySpec("quarter") && v->sectionPreset() == Preset::Quarter && !sc->applySpec("bogus"), "applySpec (CLI --section) works / rejects unknown");
  sc->applySpec("none");
}

// ---------------------------------------------------------------- C) runner
void runnerChecks() {
  std::printf("--- runner\n");
  pc::app::AppDefinition def;
  std::string err;
  check(pc::loadDocument(def.document, std::string(PC_SAMPLES_DIR) + "/v2_multibody.pcad", &err), "load v2_multibody for the runner");
  def.appName = "Kesit testi";
  pc::runner::RunnerWindow win(def);
  bool settled = false;
  QObject::connect(&win, &pc::runner::RunnerWindow::settled, [&settled] { settled = true; });
  win.resize(1400, 900);
  win.show();
  win.startup();
  check(waitFor([&] { return settled; }, 60000), "runner settled");
  pump(500);
  auto* sc = win.sectionControls();
  auto* v = win.viewer();
  check(sc && sc->isVisible() && !sc->isEnabled(), "runner toolbar shows the (disabled) section controls");
  QToolButton* btn = nullptr;
  for (auto* tb : win.findChildren<QToolBar*>())
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(sc->toggleAction()))) btn = b;
  check(btn != nullptr, "runner toolbar has the 'Kesit' toggle");
  const QImage r0 = v->renderToImage(700, 500);
  if (btn) QTest::mouseClick(btn, Qt::LeftButton);
  pump(200);
  check(sc->isEnabled() && v->sectionEnabled(), "runner: toggle enables the section");
  const QImage r1 = v->renderToImage(700, 500);
  check(diffPixels(r0, r1) > 1000, QString("runner view changes (%1 px)").arg(diffPixels(r0, r1)));
  auto* combo = sc->findChild<QComboBox*>("sectionPreset");
  auto* slider = sc->findChild<QSlider*>("sectionOffset");
  check(combo && slider, "runner: preset combo + offset slider");
  if (combo && slider) {
    combo->setCurrentIndex(combo->findData(int(Preset::XY)));
    const double p0 = v->sectionPosition();
    slider->setValue(-40);
    check(v->sectionPreset() == Preset::XY && v->sectionPosition() < p0, "runner: XY preset + offset");
  }
  if (btn) QTest::mouseClick(btn, Qt::LeftButton);
  pump(200);
  check(!v->sectionEnabled() && diffPixels(r0, v->renderToImage(700, 500)) == 0, "runner: toggle off restores the image");
  // optional per-body transparency
  win.setBodyTransparent("__none__", true);
  win.setBodyTransparent("__none__", false);
  check(diffPixels(r0, v->renderToImage(700, 500)) == 0, "runner: transparency of an unknown body is a no-op");
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
  viewerChecks();
  designerChecks();
  runnerChecks();
  std::printf("%d failure(s)\n", g_fail);
  return g_fail;
}
