// SC-PULLUPTO GUI harness (docs/spaceclaim_ux/PULL_UPTO.md) — the REAL MainWindow, SpaceClaim's 2D -> 3D workflow:
//
//   two sketches: a front profile on XZ drawn like an imported drawing (separate end points, 0.02 mm gaps, an
//   overshoot) and a top outline on XY (rectangle, depth = parameter W) + an open loop (0.5 mm gap) + three squares on YZ
//   → click a front-profile curve, F, Enter: the loops become region sketch:Sketch1:r0 (gaps closed), selected
//   → P, click the "Kadar" tool guide, click the corner of the top outline, Enter: Extrude1 upTo=<that point>, the solid
//     is exactly as deep as the top outline (40) → W = 55 in Gruplar: the solid follows (55)
//   → the open loop: F reports the gap (red markers), a larger tolerance closes it, Cetvel (Ruler) pulls it 5 mm short
//     of the solid's top face
//   → Ctrl+click two of the three YZ squares, P, 10 Enter: one Extrude of exactly those two regions.
// Every interaction is a real key / mouse event (QTest) into the viewer, the tool guide bar or the Gruplar table, except
// the camera presets ([note] lines) and the document set-up (written as a .pcad and opened through MainWindow::openFile).
//   Exit code = number of failed checks.
#include "DimensionBox.h"
#include "FillTool.h"
#include "MainWindow.h"
#include "PullMapping.h"
#include "PullTool.h"
#include "SketchRegions.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Document.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QAbstractButton>
#include <QApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <cstdio>
#include <functional>

using pcd::SelItem;

namespace {
int g_fail = 0, g_pass = 0;
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
    if (m->inherits("pcd::ChangeReviewDialog")) {
      note("manufacturing review dialog -> accept");
      for (auto* b : m->findChildren<QAbstractButton*>())
        if (b->text().remove('&') == "Yine de Uygula") { pch::pressLater(b, 50); return; }
      if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
    }
  });
  t->start();
}
void ensureActive(pcd::MainWindow& w) {
  if (QApplication::activeModalWidget()) return;
  if (QApplication::activeWindow() != &w) { w.activateWindow(); QApplication::setActiveWindow(&w); pump(30); }
}
QWidget* keyTarget(pcd::MainWindow& w) {
  ensureActive(w);
  QWidget* f = QApplication::focusWidget();
  return (f && w.isAncestorOf(f) && f->isVisible()) ? f : static_cast<QWidget*>(w.viewer());
}
void key(pcd::MainWindow& w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QTest::keyClick(keyTarget(w), Qt::Key(k), m);
  pump(40);
}
void type(pcd::MainWindow& w, const QString& s) {
  for (const QChar c : s) { QTest::keyClick(keyTarget(w), c.toLatin1()); pump(15); }
  pump(30);
}
void vclick(pcd::MainWindow& w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  ensureActive(w);
  QWidget* t = w.viewer();
  QMouseEvent mv(QEvent::MouseMove, p, t->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(20);
  QTest::mouseClick(t, Qt::LeftButton, m, p.toPoint());
  pump(120);
}
QString msg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }

struct Box { double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0; };
Box bbox(const TopoDS_Shape& s) {
  Box o;
  if (s.IsNull()) return o;
  Bnd_Box b;
  BRepBndLib::AddOptimal(s, b, Standard_False, Standard_False);
  b.Get(o.x0, o.y0, o.z0, o.x1, o.y1, o.z1);
  return o;
}
const pc::model::BodyInfo* bodyOf(pcd::MainWindow& w, const std::string& fid) {
  for (const auto& b : w.modelResult().bodies) if (b.createdBy == fid) return &b;
  return nullptr;
}
double vol(const TopoDS_Shape& s) {
  try { return s.IsNull() ? 0 : pc::model::massProperties(s).volume; } catch (...) { return -1; }
}
QPointF worldPx(pcd::MainWindow& w, const gp_Pnt& p) {
  const double a[3] = {p.X(), p.Y(), p.Z()};
  return w.viewer()->worldToScreen(a);
}
// a pixel near `near` where the viewer reports `pred(name)`
QPointF findPx(pcd::MainWindow& w, const QPointF& near, const std::function<bool(const QString&)>& pred, int maxR = 8) {
  for (int r = 0; r <= maxR; r += 1)
    for (int k = 0; k < (r == 0 ? 1 : 16); ++k) {
      const double a = k * M_PI / 8.0;
      const QPointF p = near + QPointF(std::cos(a), std::sin(a)) * r;
      if (pred(w.viewer()->pickAt(p.toPoint()))) return p;
    }
  return QPointF(-1, -1);
}
QPointF facePx(pcd::MainWindow& w, const QString& face) {
  double c[3], n[3];
  if (!w.viewer()->faceCentroid(face, c, n)) return QPointF(-1, -1);
  return findPx(w, w.viewer()->worldToScreen(c), [&](const QString& s) { return s == face; }, 240);
}
// a pixel on sketch curve `name` (sampled between two 3D points)
QPointF curvePx(pcd::MainWindow& w, const QString& name, const gp_Pnt& a, const gp_Pnt& b) {
  for (double t : {0.5, 0.4, 0.6, 0.3, 0.7}) {
    const QPointF p = worldPx(w, gp_Pnt(a.XYZ() * (1 - t) + b.XYZ() * t));
    const QPointF q = findPx(w, p, [&](const QString& s) { return s == name; }, 4);
    if (q.x() >= 0) return q;
  }
  return QPointF(-1, -1);
}
QStandardItemModel* paramModel(pcd::MainWindow& w) { return qobject_cast<QStandardItemModel*>(w.parameterTable()->model()); }
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = paramModel(w);
  for (int r = 0; m && r < m->rowCount(); ++r) if (m->item(r, 0) && m->item(r, 0)->text() == name) return r;
  return -1;
}

// ---- the document
struct Ids { int corner = -1; int frontBottom = -1; int openLine = -1; };
pc::Document makeDoc(Ids& ids) {
  using namespace pc;
  Document d;
  d.name = "pullupto";
  d.params.add({"W", "40"});
  // Sketch1: front profile on XZ, every line with its own end points (imported drawing): 0.02 mm gaps, one overshoot
  {
    Sketch s = Sketch::create();
    s.plane.kind = SketchPlaneKind::XZ;
    const std::vector<std::pair<double, double>> P = {{0, 0}, {60, 0}, {60, 10}, {15, 10}, {15, 30}, {0, 30}};
    for (size_t i = 0; i < P.size(); ++i) {
      auto a = P[i], b = P[(i + 1) % P.size()];
      const double dx = b.first - a.first, dy = b.second - a.second, l = std::hypot(dx, dy);
      const double gap = 0.02, over = i == 2 ? 0.03 : 0.0;
      const int id = s.addLine(a.first + dx / l * gap, a.second + dy / l * gap, b.first + dx / l * over, b.second + dy / l * over);
      if (i == 0) ids.frontBottom = id;
    }
    Feature f;
    f.id = "Sketch1";
    f.type = FeatureType::Sketch;
    f.sketch = s;
    d.features.push_back(f);
  }
  // Sketch2: top outline on XY, x 0..60, y -W..0, top-left corner on the origin
  {
    Sketch s = Sketch::create();
    s.plane.kind = SketchPlaneKind::XY;
    const auto L = s.addRectangle(0, -40, 60, 0);
    int tl = -1;
    for (int pid : s.entity(L[2])->points)
      if (std::fabs(s.point(pid)->x) < 1e-9 && std::fabs(s.point(pid)->y) < 1e-9) tl = pid;
    s.addConstraint(SkConstraintType::Coincident, {0, tl});
    s.addConstraint(SkConstraintType::Length, {L[0]}, "60");
    s.addConstraint(SkConstraintType::Length, {L[3]}, "W");
    ids.corner = s.entity(L[0])->points[0];   // (0,-40)
    // an open loop far away: square 100..120 x -40..-20 with a 0.5 mm gap
    s.addLine(100, -40, 120, -40);
    s.addLine(120, -40, 120, -20);
    s.addLine(120, -20, 100, -20);
    ids.openLine = s.addLine(100, -20, 100, -39.5);
    Feature f;
    f.id = "Sketch2";
    f.type = FeatureType::Sketch;
    f.sketch = s;
    d.features.push_back(f);
  }
  // Sketch3: three squares on YZ (u = Y, v = Z) at x = 0, y 100.. (any plane)
  {
    Sketch s = Sketch::create();
    s.plane.kind = SketchPlaneKind::YZ;
    s.addRectangle(100, 0, 110, 10);
    s.addRectangle(120, 0, 130, 10);
    s.addRectangle(140, 0, 150, 10);
    Feature f;
    f.id = "Sketch3";
    f.type = FeatureType::Sketch;
    f.sketch = s;
    d.features.push_back(f);
  }
  return d;
}

void run(pcd::MainWindow& w, const QString& tmp) {
  auto* tm = w.toolManager();
  auto* v = w.viewer();
  auto* undo = w.undoStack();
  Ids ids;
  const QString path = tmp + "/pullupto.pcad";
  {
    std::string err;
    check(pc::saveDocument(makeDoc(ids), path.toStdString(), &err), "document written: " + QString::fromStdString(err));
  }
  w.setAutoYes(true);
  check(w.openFile(path), "pullupto.pcad opened");
  pump(300);

  section("regions: the imported front profile is a region although its lines do not meet");
  const auto& m0 = w.modelResult();
  check(m0.solvedSketches.count("Sketch1") && m0.solvedSketches.count("Sketch2"), "both sketches regenerated");
  note("[note] camera preset Front (viewer API)");
  v->setView(pc::viewer::ShapeViewer::View::Front);
  pump(250);
  check(facePx(w, "sketch:Sketch1:r0").x() >= 0, "region sketch:Sketch1:r0 drawn and pickable (gaps 0.02 mm closed)");

  section("Doldur (F): curves -> region, selected");
  const QString c0 = pcd::sketchCurveName("Sketch1", ids.frontBottom);
  const QPointF cp = curvePx(w, c0, gp_Pnt(0, 0, 0), gp_Pnt(60, 0, 0));
  check(cp.x() >= 0, "front profile bottom curve pickable: " + c0);
  vclick(w, cp);
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchCurve, c0}), "the curve is selected");
  key(w, Qt::Key_F);
  check(tm->activeId() == "fill", "F: Doldur");
  key(w, Qt::Key_Return);
  pump(150);
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch1:r0"}) && w.selection()->count() == 1,
        "Fill: region sketch:Sketch1:r0 selected");
  check(msg(w).startsWith("Doldur: Sketch1 → 1 bölge") && msg(w).contains("boşluk kapatıldı"), "status: " + msg(w));

  section("Çek (P) + Kadar (Up To): a vertex of the top sketch");
  key(w, Qt::Key_P);
  check(tm->activeId() == "pull", "P: Çek");
  auto* pt = qobject_cast<pcd::PullTool*>(tm->active());
  check(pt && pt->target().kind == pcd::pull::Kind::NewExtrude && pt->target().regionSets.size() == 1, "target: new Extrude of the region");
  auto* gb = w.findChild<QToolButton*>("guide.upto");
  check(gb && gb->isEnabled() && gb->isVisible(), "tool guide 'Kadar' is enabled and visible");
  if (gb) QTest::mouseClick(gb, Qt::LeftButton);
  pump(80);
  check(pt && pt->activeGuide() == "upto", "click on the guide: Kadar active");
  check(msg(w).contains("Kadar") || (pt && pt->prompt().contains("Kadar")), "prompt asks for the target: " + (pt ? pt->prompt() : QString()));
  key(w, Qt::Key_H);   // Ana görünüm (iso)
  pump(250);
  const gp_Pnt corner(0, -40, 0);
  const QString pref = QString("sketch:Sketch2:e");
  QPointF kp = findPx(w, worldPx(w, corner), [&](const QString& s) { return s.startsWith(pref); }, 8);
  check(kp.x() >= 0, QString("a curve of Sketch2 at the corner pixel (%1,%2)").arg(kp.x()).arg(kp.y()));
  const size_t nf0 = w.document().features.size();
  const int u0 = undo->count();
  vclick(w, kp);
  const QString wantRef = QString("sketch:Sketch2:p%1").arg(ids.corner);
  check(pt && pt->upToRef() == wantRef, "target = the corner point: " + (pt ? pt->upToRef() : QString()));
  check(pt && std::fabs(std::fabs(pt->upToDistance()) - 40) < 1e-6, QString("distance 40 (%1)").arg(pt ? pt->upToDistance() : -1));
  check(v->ghostVisible(), "preview (ghost) shown");
  check(msg(w).startsWith("Çek (Kadar)") && msg(w).contains("40"), "status: " + msg(w));
  check(w.document().features.size() == nf0, "nothing committed before Enter");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 5000);
  const pc::Feature* ex = w.document().feature("Extrude1");
  check(ex && ex->prop("upTo") == wantRef.toStdString() && ex->prop("sketch") == "Sketch1", "Extrude1: sketch=Sketch1, upTo=" + wantRef);
  check(undo->count() == u0 + 1, "one undo step");
  Box b = bbox(w.modelResult().shape);
  check(std::fabs(b.y0 + 40) < 1e-6 && std::fabs(b.y1) < 1e-6 && std::fabs(b.z1 - 30) < 1e-6,
        QString("solid depth = top outline depth: y %1..%2 (-40..0)").arg(b.y0).arg(b.y1));
  check(pt && pt->activeGuide() == "select", "guide back to Seç after the commit");

  section("associative: W = 55 in Gruplar -> the solid follows");
  w.structurePanel()->setCurrentPage("groups");
  pump(150);
  {
    auto* pv = w.parameterTable();
    const int row = paramRow(w, "W");
    check(row >= 0, "W listed in Gruplar");
    const QModelIndex idx = pv->model()->index(row, 1);
    pv->scrollTo(idx);
    const QRect r = pv->visualRect(idx);
    QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    QTest::mouseDClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    pump(150);
    QLineEdit* ce = pv->viewport()->findChild<QLineEdit*>();
    if (!ce) { note("double click did not open the cell editor; QAbstractItemView::edit() used"); pv->edit(idx); pump(100); ce = pv->viewport()->findChild<QLineEdit*>(); }
    check(ce != nullptr, "W cell editor open");
    if (ce) {
      ce->selectAll();
      QTest::keyClicks(ce, "55");
      QTest::keyClick(ce, Qt::Key_Return);
    }
  }
  waitFor([&] { const auto* p = w.document().params.find("W"); return p && p->expression == "55"; }, 5000);
  pump(300);
  b = bbox(w.modelResult().shape);
  check(std::fabs(b.y0 + 55) < 1e-6, QString("solid follows the top sketch: y0 = %1 (-55)").arg(b.y0));
  check(w.document().feature("Extrude1")->prop("upTo") == wantRef.toStdString(), "upTo reference unchanged (persistent)");
  w.structurePanel()->setCurrentPage("structure");
  pump(100);

  section("Doldur on an open loop: the gap is shown; a larger tolerance closes it");
  v->setFocus();
  key(w, Qt::Key_S);
  key(w, Qt::Key_H);
  pump(200);
  const QString oc = pcd::sketchCurveName("Sketch2", ids.openLine);
  QPointF op = curvePx(w, oc, gp_Pnt(100, -20, 0), gp_Pnt(100, -39.5, 0));
  check(op.x() >= 0, "open-loop curve pickable: " + oc);
  vclick(w, op);
  key(w, Qt::Key_F);
  key(w, Qt::Key_Return);
  pump(150);
  auto* ft = qobject_cast<pcd::FillTool*>(tm->active());
  // Sketch2 has the closed outline (r0) and the open square: Fill selects r0 and marks the open ends of the square
  check(ft && ft->gapMarkers().size() == 2, QString("2 open ends marked (%1)").arg(ft ? int(ft->gapMarkers().size()) : -1));
  check(msg(w).contains("Açık uçlar") || msg(w).contains("açık uç"), "status names the gap: " + msg(w));
  tm->setOption("gapTol", "0.6");
  note("[note] Fill option 'Boşluk toleransı' = 0.6 set through ToolManager::setOption (Options panel field)");
  vclick(w, op);
  key(w, Qt::Key_Return);
  pump(200);
  check(w.document().feature("Sketch2")->prop("gapTol") == "0.6", "tolerance stored on the sketch (gapTol = 0.6)");
  check(ft && ft->gapMarkers().empty(), "no open ends left");
  const int nReg = [&] {
    std::vector<TopoDS_Face> f;
    gp_Ax3 fr;
    pcd::sketchRegionFaces(w.document(), w.modelResult(), "Sketch2", f, fr);
    return int(f.size());
  }();
  check(nReg == 2, QString("Sketch2 now has 2 regions (%1)").arg(nReg));

  section("Cetvel (Ruler): the square region 5 mm below the solid's top face");
  // the region of the healed square: the one whose face centre is at x ~ 110
  QString sq;
  for (int k = 0; k < nReg; ++k) {
    double c[3], n[3];
    const QString r = QString("sketch:Sketch2:r%1").arg(k);
    if (v->faceCentroid(r, c, n) && c[0] > 90) sq = r;
  }
  check(!sq.isEmpty(), "square region found: " + sq);
  key(w, Qt::Key_P);
  vclick(w, facePx(w, sq));
  check(pt && pt->target().kind == pcd::pull::Kind::NewExtrude, "square region armed");
  if (auto* rb = w.findChild<QToolButton*>("guide.ruler")) QTest::mouseClick(rb, Qt::LeftButton);
  pump(80);
  check(pt && pt->activeGuide() == "ruler", "Cetvel guide active");
  // the top face of the L profile (z = 30): a face of Extrude1 whose centroid is at z = 30
  QString top;
  for (const auto& [n, f] : w.modelResult().faces) {
    BRepAdaptor_Surface s(f, Standard_False);
    if (s.GetType() == GeomAbs_Plane && std::fabs(std::fabs(s.Plane().Axis().Direction().Z()) - 1) < 1e-9 &&
        std::fabs(s.Plane().Location().Z() - 30) < 1e-6)
      top = QString::fromStdString(n);
  }
  check(!top.isEmpty(), "top face of the solid: " + top);
  vclick(w, facePx(w, top));
  check(pt && pt->upToRef() == top && pt->dimensionBox() && pt->dimensionBox()->isEditing(), "ruler reference = " + top + ", value box open");
  type(w, "5");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude2") != nullptr; }, 5000);
  const pc::Feature* e2 = w.document().feature("Extrude2");
  check(e2 && e2->prop("upTo") == top.toStdString() && e2->prop("upToOffset") == "-5", "Extrude2: upTo top face, upToOffset -5");
  const auto* b2 = bodyOf(w, "Extrude2");
  if (!b2) for (const auto& bb : w.modelResult().bodies) note("body " + QString::fromStdString(bb.name) + " by " + QString::fromStdString(bb.createdBy));
  Box bb2 = b2 ? bbox(b2->shape) : Box{};
  check(b2 && std::fabs(bb2.z1 - 25) < 1e-6 && std::fabs(bb2.z0) < 1e-6, QString("square pulled to z = 25 (%1)").arg(bb2.z1));

  section("multi-region Pull: Ctrl+click two of three YZ squares, 10 Enter");
  key(w, Qt::Key_S);
  note("[note] camera preset Right (viewer API)");
  v->setView(pc::viewer::ShapeViewer::View::Right);
  pump(250);
  std::vector<TopoDS_Face> f3;
  gp_Ax3 fr3;
  pcd::sketchRegionFaces(w.document(), w.modelResult(), "Sketch3", f3, fr3);
  check(f3.size() == 3, QString("Sketch3: 3 regions (%1)").arg(f3.size()));
  key(w, Qt::Key_P);
  vclick(w, facePx(w, "sketch:Sketch3:r0"));
  vclick(w, facePx(w, "sketch:Sketch3:r2"), Qt::ControlModifier);
  check(w.selection()->count() == 2, QString("two regions selected (%1)").arg(w.selection()->count()));
  check(pt && pt->target().regionSets.size() == 1 && pt->target().regionSets.first().second == QList<int>({0, 2}), "target regions {0, 2}");
  const double vBefore = vol(w.modelResult().shape);
  const std::string e3id = w.document().uniqueFeatureId(pc::FeatureType::Extrude);
  type(w, "10");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature(e3id) != nullptr; }, 5000);
  const pc::Feature* e3 = w.document().feature(e3id);
  check(e3 && e3->prop("regions") == "0,2" && e3->prop("sketch") == "Sketch3", QString::fromStdString(e3id) + " regions=0,2");
  check(std::fabs(vol(w.modelResult().shape) - vBefore - 2 * 100 * 10) < 1e-3, QString("volume +2000 (%1)").arg(vol(w.modelResult().shape) - vBefore));
  check(w.sketchShownIn3D("Sketch3") && facePx(w, "sketch:Sketch3:r1").x() >= 0, "the third square stays a pickable region");
  check(facePx(w, "sketch:Sketch3:r0").x() < 0, "pulled regions are no longer drawn as regions");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return w.document().feature(e3id) == nullptr; }, 3000);
  check(w.document().feature(e3id) == nullptr, "Ctrl+Z removes the multi-region pull in one step");

  section("Doldur on the boundary edge of a hole: the hole is patched");
  {
    // a plate 40 x 40 x 5 at x 200.. with two Ø8 through holes cut by ONE Extrude (so the cut cannot just be suppressed)
    pc::Document cand = w.document();
    pc::Sketch ps = pc::Sketch::create();
    ps.addRectangle(200, 0, 240, 40);
    pc::Feature pf;
    pf.id = "SketchP";
    pf.type = pc::FeatureType::Sketch;
    pf.sketch = ps;
    cand.features.push_back(pf);
    pc::Feature pe;
    pe.id = "Plate";
    pe.type = pc::FeatureType::Extrude;
    pe.props = {{"sketch", "SketchP"}, {"distance", "5"}, {"op", "new"}, {"bodyName", "Plate"}};
    cand.features.push_back(pe);
    pc::Sketch hs = pc::Sketch::create();
    hs.plane.kind = pc::SketchPlaneKind::OnFace;
    hs.plane.faceRef = "Plate.end";
    hs.addCircle(210.0, 20.0, 4.0);
    hs.addCircle(230.0, 20.0, 4.0);
    pc::Feature hf;
    hf.id = "SketchH";
    hf.type = pc::FeatureType::Sketch;
    hf.sketch = hs;
    cand.features.push_back(hf);
    pc::Feature he;
    he.id = "Holes";
    he.type = pc::FeatureType::Extrude;
    he.props = {{"sketch", "SketchH"}, {"distance", "5"}, {"direction", "reverse"}, {"op", "cut"}, {"targets", "Plate"}};
    cand.features.push_back(he);
    note("[note] plate with two holes added through IDesignHost::commitDocument");
    check(w.commitDocument(cand, "plate"), "plate committed");
    pump(300);
  }
  const auto* plate = w.modelResult().body("Plate");
  const double vp0 = plate ? vol(plate->shape) : -1;
  check(plate && std::fabs(vp0 - (1600 * 5 - 2 * M_PI * 16 * 5)) < 1e-3, QString("plate volume %1").arg(vp0));
  // the top circle of the hole at x = 210
  QString he;
  for (const auto& [n, e] : w.modelResult().edges) {
    BRepAdaptor_Curve c(e);
    if (c.GetType() == GeomAbs_Circle && std::fabs(c.Circle().Location().X() - 210) < 1e-6 && std::fabs(c.Circle().Location().Z() - 5) < 1e-6)
      he = QString::fromStdString(n);
  }
  check(!he.isEmpty(), "hole rim edge: " + he);
  key(w, Qt::Key_S);
  note("[note] camera preset Top (viewer API)");
  v->setView(pc::viewer::ShapeViewer::View::Top);
  pump(250);
  const QPointF hp = findPx(w, worldPx(w, gp_Pnt(214, 20, 5)), [&](const QString& s) { return s == he; }, 8);
  check(hp.x() >= 0, "rim edge pickable");
  vclick(w, hp);
  check(w.selection()->contains(SelItem{SelItem::Kind::Edge, he}), "rim edge selected");
  key(w, Qt::Key_F);
  key(w, Qt::Key_Return);
  pump(300);
  plate = w.modelResult().body("Plate");
  const double vp1 = plate ? vol(plate->shape) : -1;
  check(std::fabs(vp1 - vp0 - M_PI * 16 * 5) < 1e-3, QString("one hole patched: volume +%1 (%2)").arg(vp1 - vp0).arg(M_PI * 80));
  check(msg(w).startsWith("Doldur: delik yamalandı"), "status: " + msg(w));
  check(w.document().feature("Holes") && !w.document().feature("Holes")->suppressed, "the cut stays (the other hole is kept)");
}
} // namespace

int main(int argc, char** argv) {
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  QApplication app(argc, argv);
  QApplication::setApplicationName("pc_gui_harness_pullupto");
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();
  QTemporaryDir tmp;
  pcd::MainWindow w;
  w.show();
  QTest::qWaitForWindowExposed(&w);
  w.activateWindow();
  QApplication::setActiveWindow(&w);
  QTest::qWaitForWindowActive(&w, 3000);
  pump(500);
  run(w, tmp.path());
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  w.setAutoYes(true);
  return g_fail;
}
