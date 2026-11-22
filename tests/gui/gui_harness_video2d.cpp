// SC-VIDEO2D — integration harness for the SpaceClaim video "Convert 2D Drawings to 3D Models in Minutes with
// SpaceClaim", end to end in the REAL MainWindow, on two corpus drawings (plate, lbracket):
//
//   Dosya > İçe aktar > 2B çizim (eğri olarak) [file dialog]
//   -> Katmanlar: uncheck the layers that are not needed (Ölçüler/Notlar, GIZLI, EKSEN, CERCEVE, ...) [check boxes]
//      -> hidden curves are neither drawn nor pickable (also not as a Kadar / Up To target)
//   -> Görünüş seç [ribbon] + one click on a view -> Görünüşü yerleştir [ribbon, dialog, Uygula]: the view is folded
//      90° onto a vertical plane standing on the matching edge of the main view
//   -> click a curve, F, Enter (Doldur): closed loops -> regions (lbracket: overlapping rectangles / T-junctions)
//   -> P, U (Kadar), click a vertex of the OTHER view, Enter: the depth comes from that vertex (ref + distance checked)
//   -> Görüntü > Stil > Renk: colour the new faces; a later Pull (hole cut) keeps the colours
//   -> volumes vs tests/drawing/corpus/*/gt.json, undo / redo of the whole sequence, save / reopen identical.
//
//   pc_gui_harness_video2d [--shots]     --shots writes docs/import2d/shots/video_*.png
//   Exit code = number of failed checks. Also run with QT_FONT_DPI=72 (ctest gui_harness_video2d_dpi72).
#include "Import2D.h"
#include "Import2DUi.h"
#include "MainWindow.h"
#include "PullTool.h"
#include "Ribbon.h"
#include "SketchRegions.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <nlohmann/json.hpp>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <cmath>
#include <cstdio>
#include <functional>

#ifndef PC_V2D_SHOTS_DIR
#define PC_V2D_SHOTS_DIR "docs/import2d/shots"
#endif
#ifndef PC_CORPUS_DIR
#define PC_CORPUS_DIR "tests/drawing/corpus"
#endif

using pcd::SelItem;
namespace i2 = pcd::import2d;

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

std::function<void(QWidget*)> g_onModal;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (m->property("v2dHandled").toBool()) return;
    m->setProperty("v2dHandled", true);
    if (g_onModal) { g_onModal(m); return; }
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      note("message box: " + mb->text().left(120).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      for (auto* b : m->findChildren<QPushButton*>()) if (b->text().remove('&') == "Yine de Uygula") { pch::pressLater(b, 50); return; }
    }
    note(QString("unexpected modal %1 '%2' -> closed").arg(m->metaObject()->className(), m->objectName()));
    if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::reject);
  });
  t->start();
}
void acceptFileDialog(QWidget* m, const QString& path) {
  auto* fd = qobject_cast<QFileDialog*>(m);
  if (!fd) return;
  QTimer::singleShot(200, fd, [fd, path] {
    fd->selectFile(path);
    if (auto* le = fd->findChild<QLineEdit*>("fileNameEdit")) le->setText(path);
    QTimer::singleShot(100, fd, [fd] { QMetaObject::invokeMethod(fd, "accept"); });
  });
}

// ---- real input
void ensureActive(pcd::MainWindow& w) {
  if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
  if (QApplication::activeWindow() != &w) { w.activateWindow(); QApplication::setActiveWindow(&w); pump(30); }
}
QWidget* keyTarget(pcd::MainWindow& w) {
  ensureActive(w);
  QWidget* f = QApplication::focusWidget();
  return (f && w.isAncestorOf(f) && f->isVisible()) ? f : static_cast<QWidget*>(w.viewer());
}
void key(pcd::MainWindow& w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QTest::keyClick(keyTarget(w), Qt::Key(k), m);
  pump(60);
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
QMenu* popupMenu(const QString& objectName = QString()) {
  for (int i = 0; i < 40; ++i) {
    if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
      if (objectName.isEmpty() || m->objectName() == objectName) return m;
    pump(25);
  }
  return nullptr;
}
void closePopups() {
  for (int i = 0; i < 4 && QApplication::activePopupWidget(); ++i) { QApplication::activePopupWidget()->close(); pump(60); }
}
QToolButton* ribbonButton(pcd::MainWindow& w, QAction* a) {
  for (auto* tb : w.ribbon()->findChildren<QToolBar*>())
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(a)); b && b->isVisible()) return b;
  return nullptr;
}
bool clickRibbon(pcd::MainWindow& w, const QString& tab, QAction* a) {
  w.ribbon()->setCurrentTab(tab);
  pump(150);
  QToolButton* b = nullptr;
  for (pcd::RibbonGroup* g : w.ribbon()->groupsOf(tab))
    if (QToolButton* x = g->buttonFor(a)) b = x;
  if (!b) b = a ? ribbonButton(w, a) : nullptr;
  if (!b || !b->isVisible() || !b->isEnabled()) {
    note(QString("ribbon button for %1: %2").arg(a ? a->objectName() : "-", !b ? "missing" : !b->isVisible() ? "not visible" : "disabled"));
    return false;
  }
  QTest::mouseClick(b, Qt::LeftButton, {}, QPoint(b->width() / 2, b->height() / 2));
  pump(200);
  return true;
}
// palette swatch of the ribbon "Renk" menu (opened with a blocking exec(): the click comes from a timer)
bool ribbonColor(pcd::MainWindow& w, const QString& hex) {
  w.ribbon()->setCurrentTab("display");
  pump(150);
  QAction* a = w.findChild<QAction*>("actColor");
  QToolButton* b = a ? ribbonButton(w, a) : nullptr;
  if (!b) return false;
  bool done = false, found = false;
  QTimer::singleShot(300, qApp, [&] {
    QMenu* m = popupMenu("colorMenu");
    QToolButton* sw = m ? m->findChild<QToolButton*>("colorSwatch." + hex) : nullptr;
    found = sw != nullptr;
    if (sw) { QTest::mouseMove(sw, QPoint(5, 5)); pump(60); QTest::mouseClick(sw, Qt::LeftButton, {}, QPoint(sw->width() / 2, sw->height() / 2)); }
    else { note("palette menu / swatch missing: " + hex); closePopups(); }
    done = true;
  });
  QTest::mouseClick(b, Qt::LeftButton, {}, QPoint(b->width() / 2, b->height() / 2));
  waitFor([&] { return done; }, 5000);
  pump(250);
  return found;
}
void shot(pcd::MainWindow& w, const QString& name) {
  if (!g_shots) return;
  pump(350);
  QDir().mkpath(PC_V2D_SHOTS_DIR);
  const QString path = QString(PC_V2D_SHOTS_DIR) + "/" + name;
  note(QString("shot %1 %2").arg(path, w.grab().save(path) ? "saved" : "FAILED"));
}

// ---- model / view queries
double volume(pcd::MainWindow& w) {
  try { return w.modelResult().shape.IsNull() ? 0 : pc::model::massProperties(w.modelResult().shape).volume; } catch (...) { return -1; }
}
struct Box { double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0; };
Box bbox(const TopoDS_Shape& s) {
  Box o;
  if (s.IsNull()) return o;
  Bnd_Box b;
  BRepBndLib::AddOptimal(s, b, Standard_False, Standard_False);
  b.Get(o.x0, o.y0, o.z0, o.x1, o.y1, o.z1);
  return o;
}
QString boxStr(const Box& b) { return QString("x %1..%2 y %3..%4 z %5..%6").arg(b.x0).arg(b.x1).arg(b.y0).arg(b.y1).arg(b.z0).arg(b.z1); }
QPointF worldPx(pcd::MainWindow& w, const gp_Pnt& p) {
  const double a[3] = {p.X(), p.Y(), p.Z()};
  return w.viewer()->worldToScreen(a);
}
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
  const QPointF p0 = w.viewer()->worldToScreen(c);
  for (int r = 0; r <= 300; r += 4)
    for (int k = 0; k < (r == 0 ? 1 : 24); ++k) {
      const double a = k * M_PI / 12.0;
      const QPointF p = p0 + QPointF(std::cos(a), std::sin(a)) * r;
      if (w.viewer()->pickAt(p.toPoint()) == face && w.viewer()->edgeAt(p.toPoint()).isEmpty()) return p;
    }
  return QPointF(-1, -1);
}
QImage viewImage(pcd::MainWindow& w) { return w.viewer()->grabFramebuffer().convertToFormat(QImage::Format_RGB32); }
QColor px(const QImage& im, const QPointF& p) {
  const QPoint q = p.toPoint() * im.devicePixelRatio();
  return im.valid(q) ? im.pixelColor(q) : QColor();
}
bool isBlue(const QColor& c) { return c.blue() > c.red() + 60 && c.blue() > c.green() + 20; }
bool isRed(const QColor& c) { return c.red() > c.green() + 60 && c.red() > c.blue() + 60; }
void setView(pcd::MainWindow& w, pc::viewer::ShapeViewer::View v) {
  w.viewer()->setView(v);
  w.viewer()->fitAll();
  pump(250);
}
void isoView(pcd::MainWindow& w) { setView(w, pc::viewer::ShapeViewer::View::Iso); }
void topView(pcd::MainWindow& w) { setView(w, pc::viewer::ShapeViewer::View::Top); }

// ---- import2d helpers
pcd::Import2DController* ctl(pcd::MainWindow& w) { return w.import2d(); }
QString corpus(const QString& rel) { return QFileInfo(QString(PC_CORPUS_DIR) + "/" + rel).absoluteFilePath(); }
std::vector<const pc::Feature*> imported(pcd::MainWindow& w) {
  std::vector<const pc::Feature*> out;
  for (const auto& f : w.document().features) if (i2::isImportedSketch(f)) out.push_back(&f);
  return out;
}
const pc::Feature* layerSketch(pcd::MainWindow& w, const std::string& layer, bool onPlane) {
  for (const auto* f : imported(w))
    if (f->prop("layer") == layer && (f->sketch->plane.kind == pc::SketchPlaneKind::OnPlane) == onPlane) return f;
  return nullptr;
}
// first curve of sketch `f` whose 3D sample points satisfy pred (end points for lines)
i2::CurveRef findCurve(pcd::MainWindow& w, const pc::Feature* f, const std::function<bool(const std::vector<gp_Pnt>&, const pc::SkEntity&)>& pred) {
  if (!f || !f->sketch) return {};
  for (const auto& e : f->sketch->entities) {
    const i2::CurveRef c{f->id, e.id};
    const auto pts = i2::curvePoints(w.document(), w.modelResult(), c);
    if (!pts.empty() && pred(pts, e)) return c;
  }
  return {};
}
bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
// a pixel on curve c (sampled along the segment between its first two points / quadrant)
QPointF curvePx(pcd::MainWindow& w, const i2::CurveRef& c) {
  const QString name = QString::fromStdString(i2::curveName(c));
  const auto pts = i2::curvePoints(w.document(), w.modelResult(), c);
  if (pts.size() < 2) return QPointF(-1, -1);
  for (double t : {0.5, 0.4, 0.6, 0.3, 0.7, 0.2, 0.8}) {
    const gp_Pnt m(pts[0].XYZ() * (1 - t) + pts[1].XYZ() * t);
    const QPointF q = findPx(w, worldPx(w, m), [&](const QString& s) { return s == name; }, 5);
    if (q.x() >= 0) return q;
  }
  return QPointF(-1, -1);
}
// 3D position of sketch point ref "sketch:<sid>:p<id>"
bool sketchPointWorld(pcd::MainWindow& w, const QString& ref, gp_Pnt* out) {
  const QStringList parts = ref.split(':');
  if (parts.size() != 3 || parts[0] != "sketch" || !parts[2].startsWith('p')) return false;
  const std::string sid = parts[1].toStdString();
  auto sk = w.modelResult().solvedSketches.find(sid);
  gp_Ax3 fr;
  if (sk == w.modelResult().solvedSketches.end() || !pcd::sketchFrameOf(w.modelResult(), sid, fr)) return false;
  const pc::SkPoint* p = sk->second.point(parts[2].mid(1).toInt());
  if (!p) return false;
  *out = gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * p->x + fr.YDirection().XYZ() * p->y);
  return true;
}
QTreeWidgetItem* layerRow(pcd::MainWindow& w, const QString& n) { return ctl(w)->layersPanel()->row(n); }
void clickCheck(QTreeWidget* t, QTreeWidgetItem* it) {
  t->scrollToItem(it);
  const QRect r = t->visualItemRect(it);
  QStyleOptionViewItem opt;
  opt.initFrom(t);
  opt.rect = r;
  opt.features = QStyleOptionViewItem::HasCheckIndicator;
  const QRect ind = t->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &opt, t);
  QPoint p = ind.isValid() && ind.width() > 0 ? ind.center() : QPoint(r.left() + 10, r.center().y());
  if (!r.contains(p)) p = QPoint(r.left() + 10, r.center().y());
  QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, p);
  pump(250);
}
pcd::PullTool* pullTool(pcd::MainWindow& w) { return qobject_cast<pcd::PullTool*>(w.toolManager()->active()); }
QString msg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }
QString hexOf(pcd::MainWindow& w, SelItem::Kind k, const QString& name) {
  for (const auto& g : w.document().view.colors)
    for (const auto& m : g.members)
      if (QString::fromStdString(m.ref) == name &&
          ((k == SelItem::Kind::Face && m.kind == pc::NsMemberKind::Face) || (k == SelItem::Kind::Body && m.kind == pc::NsMemberKind::Body)))
        return QString::fromStdString(g.color);
  return {};
}
// planar faces of the model with outward normal n and centroid coordinate c[axis] == coord
QStringList facesAt(pcd::MainWindow& w, double nx, double ny, double nz, int axis, double coord) {
  QStringList out;
  for (const auto& [n, f] : w.modelResult().faces) {
    (void)f;
    const auto s = pc::model::faceSignature(w.modelResult(), n);
    if (!s.valid || s.surface != "plane") continue;
    if (std::fabs(s.nx - nx) + std::fabs(s.ny - ny) + std::fabs(s.nz - nz) > 1e-6) continue;
    const double c[3] = {s.cx, s.cy, s.cz};
    if (std::fabs(c[axis] - coord) < 1e-6) out << QString::fromStdString(n);
  }
  out.sort();
  return out;
}
std::string docJson(const pc::Document& d) { return pc::toJson(d).dump(); }

// ============================================================================================ shared steps
struct Run {
  QString tag;             // "plate" / "lbracket"
  int shotNo = 0;
  QString nextShot(const QString& what) { return QString("video_%1_%2_%3.png").arg(tag).arg(++shotNo, 2, 10, QChar('0')).arg(what); }
};

// 1. Dosya > İçe aktar > 2B çizim (eğri olarak) through the (non-native) file dialog
bool importDrawing(pcd::MainWindow& w, Run& r, const QString& path) {
  section(r.tag + " 1. Import " + QFileInfo(path).fileName() + " as curves (menu + file dialog)");
  auto* c = ctl(w);
  QUndoStack* undo = w.undoStack();
  const int u0 = undo->index();
  QMenu* mImport = w.findChild<QMenu*>("menuImport");
  check(mImport && mImport->actions().contains(c->importAction()), "Dosya > İçe aktar > 2B çizim (eğri olarak)");
  g_onModal = [&](QWidget* m) { acceptFileDialog(m, path); };
  c->importAction()->trigger();
  waitFor([&] { return !imported(w).empty(); }, 15000);
  g_onModal = nullptr;
  pump(300);
  check(!imported(w).empty() && undo->index() == u0 + 1, QString("imported as curves, ONE undo step (%1 sketches)").arg(imported(w).size()));
  note("layers: " + c->layerNames().join(", ") + " | " + c->lastMessage());
  isoView(w);
  shot(w, r.nextShot("imported"));
  return !imported(w).empty();
}

// 2. Katmanlar: uncheck every layer except the visible outlines (GORUNUR) and Katman0 (bodies)
QStringList hideLayers(pcd::MainWindow& w, Run& r) {
  section(r.tag + " 2. Katmanlar: hide dimensions / hidden lines / centre lines / frame (check boxes)");
  auto* c = ctl(w);
  w.structurePanel()->setCurrentPage("layers");
  pump(150);
  QStringList hidden;
  const int u0 = w.undoStack()->index();
  for (const QString& l : c->layerNames()) {
    if (l == "Katman0" || l == "GORUNUR") continue;
    QTreeWidgetItem* it = layerRow(w, l);
    if (!it) { check(false, "layer row " + l); continue; }
    clickCheck(c->layersPanel()->tree(), it);
    waitFor([&] { return !c->layerVisible(l); }, 2000);
    it = layerRow(w, l);   // the panel rebuilds its rows after every change
    check(!c->layerVisible(l) && it && it->checkState(0) == Qt::Unchecked, "unchecked: " + l);
    hidden << l;
  }
  check(w.undoStack()->index() == u0 + hidden.size(), QString("one undo step per check box (%1)").arg(hidden.size()));
  check(c->layerVisible("GORUNUR"), "GORUNUR (visible outlines) stays visible");
  // hidden curves are not pickable (edge pick at a pixel on each hidden curve)
  topView(w);
  int tested = 0, pickable = 0;
  for (const auto* f : imported(w)) {
    if (!hidden.contains(QString::fromStdString(f->prop("layer")))) continue;
    for (const auto& e : f->sketch->entities) {
      if (tested >= 12) break;
      const i2::CurveRef cr{f->id, e.id};
      const auto pts = i2::curvePoints(w.document(), w.modelResult(), cr);
      if (pts.size() < 2) continue;
      const QPointF p = worldPx(w, gp_Pnt((pts[0].XYZ() + pts[1].XYZ()) * 0.5));
      ++tested;
      if (w.viewer()->edgeAt(p.toPoint()).startsWith(QString("sketch:%1:").arg(QString::fromStdString(f->id)))) ++pickable;
    }
  }
  check(tested > 0 && pickable == 0, QString("hidden-layer curves not pickable (%1 tested, %2 pickable)").arg(tested).arg(pickable));
  w.structurePanel()->setCurrentPage("structure");
  isoView(w);
  shot(w, r.nextShot("layers_hidden"));
  return hidden;
}

// 3. Görünüş seç (ribbon) + one click on `seed` -> Görünüşü yerleştir (ribbon, dialog, edge, Uygula)
const pc::Feature* foldView(pcd::MainWindow& w, Run& r, const i2::CurveRef& seed, i2::FoldEdge wantEdge, int wantCurves) {
  section(r.tag + " 3. Görünüş seç + Görünüşü yerleştir: fold the view onto a vertical plane");
  auto* c = ctl(w);
  QUndoStack* undo = w.undoStack();
  topView(w);
  check(clickRibbon(w, "display", c->viewSelectAction()) && c->viewSelectAction()->isChecked(), "ribbon Görüntü > Görünüş seç (checked)");
  const QPointF sp = curvePx(w, seed);
  check(sp.x() >= 0, "seed curve pickable: " + QString::fromStdString(i2::curveName(seed)));
  vclick(w, sp);
  pump(200);
  const auto sel = c->selectedCurves();
  check(int(sel.size()) == wantCurves, QString("one click selects the whole view: %1 curves (%2)").arg(sel.size()).arg(wantCurves));
  if (c->viewSelectAction()->isChecked()) clickRibbon(w, "display", c->viewSelectAction());
  shot(w, r.nextShot("view_selected"));
  const int u0 = undo->index();
  check(clickRibbon(w, "display", c->placeViewAction()), "ribbon Görüntü > Görünüşü yerleştir");
  pump(300);
  pcd::PlaceViewDialog* dlg = c->placeDialog();
  check(dlg && dlg->isVisible(), "Görünüşü yerleştir dialog open");
  if (!dlg) return nullptr;
  if (dlg->placement().edge != wantEdge) {
    // the user picks the fold edge in the dialog's combo box (keyboard on the focused combo)
    auto* combo = dlg->findChild<QComboBox*>();
    note(QString("default fold edge %1 -> choosing %2 in the combo").arg(int(dlg->placement().edge)).arg(int(wantEdge)));
    if (combo) {
      combo->setFocus();
      for (int i = 0; i < combo->count() && dlg->placement().edge != wantEdge; ++i) { QTest::keyClick(combo, Qt::Key_Down); pump(150); }
      if (dlg->placement().edge != wantEdge) for (int i = 0; i < combo->count() && dlg->placement().edge != wantEdge; ++i) { QTest::keyClick(combo, Qt::Key_Up); pump(150); }
    }
  }
  check(dlg->placement().ok && dlg->placement().edge == wantEdge, "fold edge: " + dlg->findChild<QLabel*>("placeInfo")->text());
  check(w.viewer()->ghostVisible(), "ghost preview of the folded view");
  isoView(w);
  shot(w, r.nextShot("place_dialog"));
  QTest::mouseClick(dlg->findChild<QPushButton*>("placeApply"), Qt::LeftButton);
  waitFor([&] { return undo->index() == u0 + 1; }, 8000);
  pump(300);
  check(undo->index() == u0 + 1 && !c->placeDialog(), "Uygula: ONE undo step, dialog closed");
  const pc::Feature* placed = layerSketch(w, "GORUNUR", true);
  if (!placed)
    for (const auto& f : w.document().features)
      if (f.sketch) note(QString::fromStdString(f.id + " kind=" + std::to_string(int(f.sketch->plane.kind)) + " layer=" + f.prop("layer") + " imp=" + f.prop("importedCurves")));
  check(placed != nullptr, "folded GORUNUR curves in a sketch on a (hidden) vertical datum plane");
  isoView(w);
  shot(w, r.nextShot("view_folded"));
  return placed;
}

// 4. click a curve of sketch `sid`, F, Enter -> regions of the sketch selected
QStringList fillSketch(pcd::MainWindow& w, Run& r, const i2::CurveRef& curve, int wantRegions) {
  section(r.tag + " 4. Doldur (F): closed loops of " + QString::fromStdString(curve.sid) + " -> regions");
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  const QString cn = QString::fromStdString(i2::curveName(curve));
  const QPointF p = curvePx(w, curve);
  check(p.x() >= 0, "curve pickable: " + cn);
  vclick(w, p);
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchCurve, cn}), "curve selected");
  key(w, Qt::Key_F);
  check(w.toolManager()->activeId() == "fill", "F: Doldur");
  key(w, Qt::Key_Return);
  pump(200);
  const QStringList regs = w.selection()->namesOfKind(SelItem::Kind::SketchRegion);
  check(regs.size() == wantRegions, QString("Fill: %1 regions selected (%2): %3 | %4").arg(regs.size()).arg(wantRegions).arg(regs.join(" "), msg(w)));
  shot(w, r.nextShot("filled"));
  return regs;
}

// 5. P, U, click the target vertex `vtx` (a curve end of sketch `targetSid` near that pixel), Enter
bool pullUpTo(pcd::MainWindow& w, Run& r, const gp_Pnt& vtx, const QString& targetPrefix, double wantDist, const QString& expectRefPrefix,
              std::function<void()> beforeTarget = nullptr, const QString& merge = QString(), std::function<void()> afterP = nullptr) {
  section(r.tag + QString(" 5. Çek (P) + Kadar (U): depth from a vertex of the other view (%1 mm)").arg(wantDist));
  QUndoStack* undo = w.undoStack();
  key(w, Qt::Key_P);
  check(w.toolManager()->activeId() == "pull", "P: Çek");
  pcd::PullTool* pt = pullTool(w);
  if (afterP) afterP();
  check(pt && pt->target().kind == pcd::pull::Kind::NewExtrude, "Pull target: new Extrude of the selected region(s)");
  if (!merge.isEmpty()) {
    // Options panel: Birleştirme = Kes (combo, keyboard)
    auto* combo = w.findChild<QComboBox*>("opt.merge.combo");
    check(combo && combo->isVisible(), "Options panel: Birleştirme combo");
    if (combo) {
      combo->setFocus();
      const int want = combo->findData(merge);
      for (int i = 0; i < 6 && combo->currentIndex() < want; ++i) { QTest::keyClick(combo, Qt::Key_Down); pump(80); }
      for (int i = 0; i < 6 && combo->currentIndex() > want; ++i) { QTest::keyClick(combo, Qt::Key_Up); pump(80); }
      check(combo->currentData().toString() == merge, "Birleştirme: " + combo->currentText());
    }
    w.viewer()->setFocus();
    pump(60);
  }
  key(w, Qt::Key_U);
  check(pt && pt->activeGuide() == "upto", "U: Kadar guide active");
  isoView(w);
  if (beforeTarget) beforeTarget();
  QPointF kp = findPx(w, worldPx(w, vtx), [&](const QString& s) { return s.startsWith(targetPrefix); }, 10);
  check(kp.x() >= 0, QString("target pixel at the vertex (%1, %2, %3)").arg(vtx.X()).arg(vtx.Y()).arg(vtx.Z()));
  const int u0 = undo->index();
  vclick(w, kp);
  const QString ref = pt ? pt->upToRef() : QString();
  check(ref.startsWith(expectRefPrefix), "Up To target = " + ref + " (" + expectRefPrefix + "…)");
  gp_Pnt tp;
  if (ref.startsWith("sketch:") && sketchPointWorld(w, ref, &tp))
    check(tp.Distance(vtx) < 1e-6, QString("target is the other view's vertex (%1, %2, %3)").arg(tp.X()).arg(tp.Y()).arg(tp.Z()));
  check(pt && near(std::fabs(pt->upToDistance()), wantDist, 1e-6), QString("depth = measured distance %1 (%2)").arg(pt ? pt->upToDistance() : -1).arg(wantDist));
  check(w.viewer()->ghostVisible(), "ghost preview; status: " + msg(w));
  shot(w, r.nextShot("upto_preview"));
  key(w, Qt::Key_Return);
  waitFor([&] { return undo->index() == u0 + 1; }, 8000);
  pump(300);
  check(undo->index() == u0 + 1, "Enter: ONE undo step: " + undo->undoText());
  return undo->index() == u0 + 1;
}

// the Extrude made last
const pc::Feature* lastExtrude(pcd::MainWindow& w) {
  const pc::Feature* out = nullptr;
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Extrude) out = &f;
  return out;
}

// 6. select faces (click + Ctrl+click), Görüntü > Stil > Renk > hex
bool colorFaces(pcd::MainWindow& w, Run& r, const QStringList& faces, const QString& hex) {
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  w.selection()->set({});
  pump(100);
  bool all = true;
  for (int i = 0; i < faces.size(); ++i) {
    const QPointF p = facePx(w, faces[i]);
    if (p.x() < 0) { note("face not visible: " + faces[i]); all = false; continue; }
    vclick(w, p, i ? Qt::ControlModifier : Qt::NoModifier);
  }
  QStringList sel = w.selection()->namesOfKind(SelItem::Kind::Face);
  sel.sort();
  QStringList want = faces;
  want.sort();
  check(all && sel == want, "faces selected: " + sel.join(" "));
  const int u0 = w.undoStack()->index();
  check(ribbonColor(w, hex), "Görüntü > Stil > Renk > " + hex);
  bool stored = true;
  for (const QString& f : faces) stored &= hexOf(w, SelItem::Kind::Face, f) == hex;
  check(stored && w.undoStack()->index() == u0 + 1, "colour stored on every face, ONE undo step: " + w.undoStack()->undoText());
  w.selection()->set({});
  pump(250);
  (void)r;
  return stored;
}

// 7./8. undo the whole sequence with Ctrl+Z, redo with Ctrl+Y; save + reopen
void undoRedoSaveReopen(pcd::MainWindow& w, Run& r, int uStart, double vExp, const QString& tmp,
                        const std::function<void()>& checkColours) {
  section(r.tag + " 7. Ctrl+Z through the whole sequence, Ctrl+Y back");
  QUndoStack* undo = w.undoStack();
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  w.viewer()->setFocus();
  const std::string end = docJson(w.document());
  const int uEnd = undo->index();
  const QStringList layers = ctl(w)->layerNames();
  for (int i = uEnd; i > uStart; --i) key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(300);
  check(undo->index() == uStart, QString("undo index back to the start (%1 -> %2, %3 steps)").arg(uEnd).arg(undo->index()).arg(uEnd - uStart));
  check(w.document().features.empty() && volume(w) == 0 && ctl(w)->layerNames() == QStringList{"Katman0"} && w.document().view.colors.empty(),
        QString("empty document again (%1 features, %2 layers)").arg(w.document().features.size()).arg(ctl(w)->layerNames().size()));
  for (int i = uStart; i < uEnd; ++i) key(w, Qt::Key_Y, Qt::ControlModifier);
  pump(400);
  check(undo->index() == uEnd, "redo index back to the end");
  check(docJson(w.document()) == end, "redo: document identical to before the undo (JSON)");
  check(near(volume(w), vExp, 0.5), QString("redo: volume %1 (%2)").arg(volume(w), 0, 'f', 2).arg(vExp, 0, 'f', 2));
  check(ctl(w)->layerNames() == layers, "redo: layers " + ctl(w)->layerNames().join(", "));
  checkColours();

  section(r.tag + " 8. Save / reopen identical");
  const QString path = tmp + "/video_" + r.tag + ".pcad";
  std::string err;
  check(pc::saveDocument(w.document(), path.toStdString(), &err), "saved " + path + QString::fromStdString(err));
  w.setAutoYes(true);
  check(w.openFile(path), "reopened");
  pump(400);
  check(docJson(w.document()) == end, "reopened document identical (JSON)");
  check(near(volume(w), vExp, 0.5), QString("reopened volume %1").arg(volume(w), 0, 'f', 2));
  bool hiddenKept = true;
  for (const QString& l : layers) if (l != "Katman0" && l != "GORUNUR") hiddenKept &= !ctl(w)->layerVisible(l);
  check(hiddenKept && ctl(w)->layerVisible("GORUNUR"), "layer visibility kept");
  checkColours();
  isoView(w);
  shot(w, r.nextShot("reopened"));
}

// ============================================================================================ plate
// "Front" view (plan, 100 x 60, 4 holes Ø9) + "Top" view (strip 100 x 8, below it). Fold the strip up onto the plan's
// bottom edge, Fill the plan, Pull Up To the top vertex of the folded strip: depth 8 -> gt volume.
void runPlate(pcd::MainWindow& w, const QString& tmp) {
  Run r{"plate"};
  w.newDocument();
  pump(300);
  const int uStart = w.undoStack()->index();
  if (!importDrawing(w, r, corpus("plate/plate_00/sheet.dxf"))) return;
  const double gtVol = 45964.24796047382;   // tests/drawing/corpus/plate/plate_00/gt.json "volume"
  const QStringList hidden = hideLayers(w, r);
  check(hidden.contains("GIZLI") && hidden.contains("EKSEN") && hidden.contains("Ölçüler/Notlar"), "dimension, hidden and centre layers hidden");
  const pc::Feature* vis = layerSketch(w, "GORUNUR", false);
  check(vis != nullptr, "GORUNUR sketch on XY");
  if (!vis) return;
  const std::string visId = vis->id;
  // the strip's bottom line (y = 147)
  const i2::CurveRef seed = findCurve(w, vis, [](const std::vector<gp_Pnt>& p, const pc::SkEntity& e) {
    return e.type == pc::SkEntityType::Line && near(p[0].Y(), 147) && near(p[1].Y(), 147);
  });
  check(seed.sid == visId, "strip bottom line found (true DXF coordinates)");
  const pc::Feature* placed = foldView(w, r, seed, i2::FoldEdge::Top, 4);
  if (!placed) return;
  const std::string placedId = placed->id;
  {
    Box b;
    b.x0 = b.y0 = b.z0 = 1e300; b.x1 = b.y1 = b.z1 = -1e300;
    for (const auto& e : placed->sketch->entities)
      for (const auto& p : i2::curvePoints(w.document(), w.modelResult(), {placedId, e.id})) {
        b.x0 = std::min(b.x0, p.X()); b.x1 = std::max(b.x1, p.X()); b.y0 = std::min(b.y0, p.Y()); b.y1 = std::max(b.y1, p.Y());
        b.z0 = std::min(b.z0, p.Z()); b.z1 = std::max(b.z1, p.Z());
      }
    check(near(b.y0, 200) && near(b.y1, 200) && near(b.z0, 0) && near(b.z1, 8) && near(b.x0, 47) && near(b.x1, 147),
          "folded strip stands on the plan's bottom edge: " + boxStr(b));
  }
  // Fill the plan (outline + 4 circles): the plate region with 4 holes
  const i2::CurveRef outline = findCurve(w, w.document().feature(visId), [](const std::vector<gp_Pnt>& p, const pc::SkEntity& e) {
    return e.type == pc::SkEntityType::Line && near(p[0].Y(), 260) && near(p[1].Y(), 260);
  });
  isoView(w);
  // 5 regions: the plate (outer loop with 4 holes) + the 4 circles (islands, as in SpaceClaim)
  const QStringList regs5 = fillSketch(w, r, outline, 5);
  const QString plateRegion = QString("sketch:%1:r0").arg(QString::fromStdString(visId));
  check(regs5.contains(plateRegion), "the plate region is " + plateRegion);
  const QStringList regs{plateRegion};
  auto pickPlate = [&] {
    // in Pull, one click on the plate region (near a corner: the Pull arrow sits at the centroid) keeps only that region
    const QPointF pp = findPx(w, worldPx(w, gp_Pnt(52, 255, 0)), [&](const QString& n) { return n == plateRegion; }, 12);
    check(pp.x() >= 0, "plate region pickable");
    vclick(w, pp);
    check(w.selection()->namesOfKind(SelItem::Kind::SketchRegion) == regs, "Pull: only the plate region selected");
  };
  // Pull Up To the strip's top-left vertex (47, 200, 8); hidden centre lines must not be targets
  const pc::Feature* eksen = layerSketch(w, "EKSEN", false);
  const std::string eksenId = eksen ? eksen->id : std::string("?");
  auto tryHidden = [&] {
    // centre line end (49.5, 210, 0) of a hole lies on the plan region: with EKSEN hidden the click must not hit it
    pcd::PullTool* pt = pullTool(w);
    const QPointF hp = worldPx(w, gp_Pnt(49.5, 210, 0));
    const QString under = w.viewer()->edgeAt(hp.toPoint());
    check(!under.startsWith(QString("sketch:%1:").arg(QString::fromStdString(eksenId))), "hidden EKSEN curve not under the cursor: '" + under + "'");
    vclick(w, hp);
    check(pt && !pt->upToRef().contains(QString::fromStdString(eksenId)) && pt->activeGuide() == "upto",
          "click on a hidden centre line end is not an Up To target (ref '" + (pt ? pt->upToRef() : QString()) + "')");
    check(w.selection()->namesOfKind(SelItem::Kind::SketchRegion) == regs, "selection unchanged (still the plate region)");
  };
  const QString placedPfx = QString("sketch:%1:").arg(QString::fromStdString(placedId));
  if (!pullUpTo(w, r, gp_Pnt(47, 200, 8), placedPfx, 8, placedPfx + "p", tryHidden, QString(), pickPlate)) return;
  const pc::Feature* ex = lastExtrude(w);
  check(ex && ex->prop("sketch") == visId && ex->prop("upTo").rfind(placedPfx.toStdString() + "p", 0) == 0,
        QString::fromStdString(ex ? ex->id + ": sketch=" + ex->prop("sketch") + " upTo=" + ex->prop("upTo") : "no Extrude"));
  const double v = volume(w);
  check(near(v, gtVol, 0.5), QString("plate volume %1 = gt.json %2").arg(v, 0, 'f', 2).arg(gtVol, 0, 'f', 2));
  const Box b = bbox(w.modelResult().shape);
  check(near(b.z0, 0, 1e-6) && near(b.z1, 8, 1e-6) && near(b.x1 - b.x0, 100, 1e-6) && near(b.y1 - b.y0, 60, 1e-6), "solid 100 x 60 x 8: " + boxStr(b));
  isoView(w);
  shot(w, r.nextShot("pulled"));
  // 6. colour: the top face (new geometry made by Up To) blue
  section("plate 6. Renk: top face blue");
  const QStringList tops = facesAt(w, 0, 0, 1, 2, 8);
  check(tops.size() == 1, "top face (z = 8): " + tops.join(" "));
  const QString top = tops.value(0);
  colorFaces(w, r, {top}, "#0070ff");
  auto colours = [&] {
    check(hexOf(w, SelItem::Kind::Face, top) == "#0070ff", "top face colour #0070ff");
    isoView(w);
    const QPointF p = facePx(w, top);
    const QColor c = p.x() >= 0 ? px(viewImage(w), p) : QColor();
    check(isBlue(c), "PIXEL: top face drawn blue " + c.name());
  };
  colours();
  shot(w, r.nextShot("coloured"));
  undoRedoSaveReopen(w, r, uStart, gtVol, tmp, colours);
}

// ============================================================================================ lbracket
// Front view: L drawn as two overlapping rectangles (lines cross at (55,208), T-junctions at (55,200) and (47,208)).
// Top view (below): 80 x 40 with the leg line x = 55 (T-junctions) and the hole Ø10. Left view to the right.
// Fold the front view up onto the top view's far edge (y = 155), Fill it (3 regions), Pull Up To the top view's vertex
// (127, 115): depth 40; colour the end face; Fill / select the hole circle of the top view, Pull (Kes) Up To the base
// top vertex of the solid: depth 8; colour the hole wall; volume = gt.json.
void runLbracket(pcd::MainWindow& w, const QString& tmp) {
  Run r{"lbracket"};
  w.newDocument();
  pump(300);
  const int uStart = w.undoStack()->index();
  if (!importDrawing(w, r, corpus("lbracket/lbracket_00/sheet.dxf"))) return;
  const double gtVol = 41611.68146928204;   // tests/drawing/corpus/lbracket/lbracket_00/gt.json "volume"
  const QStringList hidden = hideLayers(w, r);
  check(hidden.contains("GIZLI") && hidden.contains("EKSEN") && hidden.contains("Ölçüler/Notlar"), "dimension, hidden and centre layers hidden");
  const pc::Feature* vis = layerSketch(w, "GORUNUR", false);
  check(vis != nullptr, "GORUNUR sketch on XY");
  if (!vis) return;
  const std::string visId = vis->id;
  // seed: the L's top line (y = 260, x 47..55)
  const i2::CurveRef seed = findCurve(w, vis, [](const std::vector<gp_Pnt>& p, const pc::SkEntity& e) {
    return e.type == pc::SkEntityType::Line && near(p[0].Y(), 260) && near(p[1].Y(), 260) && std::max(p[0].X(), p[1].X()) < 60;
  });
  check(seed.sid == visId, "front view top line found (true DXF coordinates)");
  const pc::Feature* placed = foldView(w, r, seed, i2::FoldEdge::Bottom, 6);
  if (!placed) return;
  const std::string placedId = placed->id;
  {
    Box b;
    b.x0 = b.y0 = b.z0 = 1e300; b.x1 = b.y1 = b.z1 = -1e300;
    for (const auto& e : placed->sketch->entities)
      for (const auto& p : i2::curvePoints(w.document(), w.modelResult(), {placedId, e.id})) {
        b.x0 = std::min(b.x0, p.X()); b.x1 = std::max(b.x1, p.X()); b.y0 = std::min(b.y0, p.Y()); b.y1 = std::max(b.y1, p.Y());
        b.z0 = std::min(b.z0, p.Z()); b.z1 = std::max(b.z1, p.Z());
      }
    check(near(b.y0, 155) && near(b.y1, 155) && near(b.z0, 0) && near(b.z1, 60) && near(b.x0, 47) && near(b.x1, 127),
          "folded L stands on the top view's far edge (y 155), z 0..60: " + boxStr(b));
  }
  // Fill the L: overlapping rectangles -> 3 regions (tolerant arrangement; the classic profile is "Dallanan kontur")
  isoView(w);
  const i2::CurveRef lTop = findCurve(w, w.document().feature(placedId), [](const std::vector<gp_Pnt>& p, const pc::SkEntity& e) {
    return e.type == pc::SkEntityType::Line && near(p[0].Z(), 60) && near(p[1].Z(), 60);
  });
  const QStringList regs = fillSketch(w, r, lTop, 3);
  {
    pc::sketch::RegionResult rr;
    gp_Ax3 fr;
    double area = 0;
    if (pcd::sketchRegionInfo(w.document(), w.modelResult(), placedId, rr, fr))
      for (double a : rr.areas) area += a;
    check(near(area, 80 * 8 + 52 * 8, 1e-3), QString("L regions cover the L: area %1 (1056)").arg(area));
  }
  const QString topPfx = QString("sketch:%1:").arg(QString::fromStdString(visId));
  if (!pullUpTo(w, r, gp_Pnt(127, 115, 0), topPfx, 40, topPfx + "p")) return;
  const pc::Feature* ex1 = lastExtrude(w);
  check(ex1 && ex1->prop("sketch") == placedId && ex1->prop("upTo").rfind(topPfx.toStdString() + "p", 0) == 0,
        QString::fromStdString(ex1 ? ex1->id + ": sketch=" + ex1->prop("sketch") + " upTo=" + ex1->prop("upTo") + " regions=" + ex1->prop("regions") : "no Extrude"));
  const std::string ex1Id = ex1 ? ex1->id : std::string();
  check(near(volume(w), 80 * 8 * 40 + 52 * 8 * 40, 0.5), QString("L solid volume %1 (42240)").arg(volume(w), 0, 'f', 2));
  check(w.bodyNames().size() == 1, "one body: " + w.bodyNames().join(","));
  {
    const Box b = bbox(w.modelResult().shape);
    check(near(b.y0, 115, 1e-6) && near(b.y1, 155, 1e-6) && near(b.z1, 60, 1e-6), "L solid spans the top view: " + boxStr(b));
  }
  isoView(w);
  shot(w, r.nextShot("pulled"));

  section("lbracket 6. Renk: the end face made by Up To blue");
  const QString endFace = QString::fromStdString(ex1Id) + ".end";
  check(w.modelResult().faces.count(endFace.toStdString()) == 1 ||
        [&] { int n = 0; for (const auto& [k, f] : w.modelResult().faces) { (void)f; if (QString::fromStdString(k).startsWith(endFace)) ++n; } return n > 0; }(),
        "end face(s) " + endFace);
  QStringList endFaces;
  for (const auto& [k, f] : w.modelResult().faces) { (void)f; if (QString::fromStdString(k).startsWith(endFace)) endFaces << QString::fromStdString(k); }
  // the end faces of the L at y = 115 (one per region or merged): colour the visible one(s)
  setView(w, pc::viewer::ShapeViewer::View::Front);
  QStringList visibleEnd;
  for (const QString& f : endFaces) if (facePx(w, f).x() >= 0) visibleEnd << f;
  note("end faces: " + endFaces.join(" ") + " | visible from the front: " + visibleEnd.join(" "));
  colorFaces(w, r, visibleEnd, "#0070ff");
  isoView(w);
  shot(w, r.nextShot("coloured_end"));

  section("lbracket 4b/5b. The hole: region of the top view's circle, Pull (Kes) Up To the base top vertex");
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  // the circle region of the top view sketch (island inside the base region) -- picked from below
  pc::sketch::RegionResult rr;
  gp_Ax3 fr;
  QString circleRegion;
  if (pcd::sketchRegionInfo(w.document(), w.modelResult(), visId, rr, fr))
    for (size_t k = 0; k < rr.areas.size(); ++k)
      if (near(rr.areas[k], M_PI * 25, 0.5))   // polygon-approximated areas (the faces are true circles)
        circleRegion = QString("sketch:%1:r%2").arg(QString::fromStdString(visId)).arg(k);
  {
    QStringList a;
    for (double x : rr.areas) a << QString::number(x, 'f', 3);
    note("top view regions (areas): " + a.join(" "));
  }
  check(!circleRegion.isEmpty(), "circle region of the top view: " + circleRegion);
  setView(w, pc::viewer::ShapeViewer::View::Bottom);
  const QPointF cp = facePx(w, circleRegion);
  check(cp.x() >= 0, "circle region pickable from below (coplanar with the base's bottom face)");
  vclick(w, cp);
  check(w.selection()->namesOfKind(SelItem::Kind::SketchRegion) == QStringList{circleRegion}, "circle region selected: " +
        w.selection()->namesOfKind(SelItem::Kind::SketchRegion).join(" ") + " / faces " + w.selection()->namesOfKind(SelItem::Kind::Face).join(" "));
  shot(w, r.nextShot("hole_region"));
  // target: the folded front view's vertex (127, 155, 8) = top of the base (its curves stay shown after a region Pull)
  const QString frontPfx = QString("sketch:%1:").arg(QString::fromStdString(placedId));
  if (!pullUpTo(w, r, gp_Pnt(127, 155, 8), QString(), 8, frontPfx + "p", nullptr, "cut")) return;
  const pc::Feature* ex2 = lastExtrude(w);
  check(ex2 && ex2->id != ex1Id && ex2->prop("op") == "cut" && ex2->prop("regions").size() > 0,
        QString::fromStdString(ex2 ? ex2->id + ": op=" + ex2->prop("op") + " upTo=" + ex2->prop("upTo") + " regions=" + ex2->prop("regions") : "-"));
  const double v = volume(w);
  check(near(v, gtVol, 0.5), QString("lbracket volume %1 = gt.json %2").arg(v, 0, 'f', 2).arg(gtVol, 0, 'f', 2));
  check(w.bodyNames().size() == 1, "still one body");
  // colours survive the later Pull
  bool kept = true;
  for (const QString& f : visibleEnd) kept &= hexOf(w, SelItem::Kind::Face, f) == "#0070ff";
  check(kept && !visibleEnd.isEmpty(), "end face colour kept after the hole Pull");
  // colour the hole wall red (new geometry)
  QStringList holeFaces;
  for (const auto& [k, f] : w.modelResult().faces) {
    (void)f;
    const auto s = pc::model::faceSignature(w.modelResult(), k);
    if (s.valid && s.surface == "cylinder") holeFaces << QString::fromStdString(k);
  }
  note("cylindrical faces: " + holeFaces.join(" "));
  setView(w, pc::viewer::ShapeViewer::View::Iso);
  // look into the hole from above-front: zoom on the hole so its wall is visible
  QStringList visHole;
  for (const QString& f : holeFaces) if (facePx(w, f).x() >= 0) visHole << f;
  if (visHole.isEmpty()) { setView(w, pc::viewer::ShapeViewer::View::Bottom); for (const QString& f : holeFaces) if (facePx(w, f).x() >= 0) visHole << f; }
  check(!visHole.isEmpty(), "hole wall visible: " + visHole.join(" "));
  if (!visHole.isEmpty()) colorFaces(w, r, {visHole.first()}, "#ff0000");
  auto colours = [&] {
    bool e = !visibleEnd.isEmpty();
    for (const QString& f : visibleEnd) e &= hexOf(w, SelItem::Kind::Face, f) == "#0070ff";
    check(e, "end face colour #0070ff");
    check(!visHole.isEmpty() && hexOf(w, SelItem::Kind::Face, visHole.first()) == "#ff0000", "hole wall colour #ff0000");
    setView(w, pc::viewer::ShapeViewer::View::Front);
    const QPointF p = visibleEnd.isEmpty() ? QPointF(-1, -1) : facePx(w, visibleEnd.first());
    const QColor c = p.x() >= 0 ? px(viewImage(w), p) : QColor();
    check(isBlue(c), "PIXEL: end face drawn blue " + c.name());
  };
  colours();
  isoView(w);
  shot(w, r.nextShot("coloured_hole"));
  undoRedoSaveReopen(w, r, uStart, gtVol, tmp, colours);
}

} // namespace

int main(int argc, char** argv) {
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication app(argc, argv);
  QApplication::setApplicationName("pc_gui_harness_video2d");   // no layout persistence
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
  check(w.import2d() != nullptr, "Import2DController exists");
  if (w.import2d()) {
    runPlate(w, tmp.path());
    runLbracket(w, tmp.path());
  }
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  w.setAutoYes(true);
  return g_fail;
}
