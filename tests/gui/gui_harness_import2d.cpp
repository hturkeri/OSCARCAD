// SC-2D-IMPORT — GUI harness: "Convert 2D Drawings to 3D Models" (SpaceClaim) workflow on the REAL MainWindow.
//
//   Dosya > İçe aktar > 2B çizim (eğri olarak) with the corpus plate DXF (file dialog, true scale, layers) ->
//   Katmanlar tab (check box: hidden layer not drawn [pixel] and not pickable, context menu "Yalnız bunu göster",
//   colour, line style) -> "Görünüş seç" one click on the front view -> "Görünüşü yerleştir" dialog (fold 90° about
//   the edge facing the top view, snap, anchor pick) -> region of the top view (XY) pulled 20 mm and the placed
//   vertical view pulled 60 mm -> volumes -> body moved to a new layer and hidden -> save / reopen keeps layers,
//   curves, placement -> undo of the import / placement -> drag & drop of a DXF -> vector PDF import by line roles.
//
//   pc_gui_harness_import2d [--shots]     --shots writes docs/import2d/shots/*.png
//   Exit code = number of failed checks.
#include "Import2D.h"
#include "Import2DUi.h"
#include "MainWindow.h"
#include "Ribbon.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <QUrl>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#ifndef PC_I2D_SHOTS_DIR
#define PC_I2D_SHOTS_DIR "docs/import2d/shots"
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
    if (m->property("visHandled").toBool()) return;
    m->setProperty("visHandled", true);
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
QWidget* view(pcd::MainWindow& w) { return w.inSketchMode() ? static_cast<QWidget*>(w.sketchEditor()) : static_cast<QWidget*>(w.viewer()); }
void key(pcd::MainWindow& w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  ensureActive(w);
  QWidget* f = QApplication::focusWidget();
  QTest::keyClick((f && w.isAncestorOf(f) && f->isVisible()) ? f : view(w), Qt::Key(k), m);
  pump(60);
}
void vclick(pcd::MainWindow& w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  ensureActive(w);
  QWidget* t = view(w);
  QMouseEvent mv(QEvent::MouseMove, p, t->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(20);
  QTest::mouseClick(t, Qt::LeftButton, m, p.toPoint());
  pump(100);
}
void rightClick(QWidget* t, const QPoint& p) {
  QMouseEvent mv(QEvent::MouseMove, QPointF(p), t->mapToGlobal(QPointF(p)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(30);
  QTest::mousePress(t, Qt::RightButton, {}, p);
  QTest::mouseRelease(t, Qt::RightButton, {}, p);
  QContextMenuEvent ce(QContextMenuEvent::Mouse, p, t->mapToGlobal(p));
  QApplication::sendEvent(t, &ce);
  pump(150);
}
QMenu* popupMenu(const QString& objectName = QString()) {
  for (int i = 0; i < 40; ++i) {
    if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
      if (objectName.isEmpty() || m->objectName() == objectName) return m;
    pump(25);
  }
  return nullptr;
}
QAction* menuAction(QMenu* m, const QString& objectName) {
  if (!m) return nullptr;
  for (QAction* a : m->actions()) if (a->objectName() == objectName) return a;
  return nullptr;
}
bool clickMenuAction(QMenu* m, QAction* a) {
  if (!m || !a || !a->isEnabled()) return false;
  const QRect r = m->actionGeometry(a);
  QTest::mouseMove(m, r.center());
  pump(60);
  QTest::mouseClick(m, Qt::LeftButton, {}, r.center());
  pump(250);
  return true;
}
// Right click + choose an action. Works for menus shown with exec() (blocking inside the context-menu event) and
// with popup(): the choice is made from a timer. Returns true if the action was found enabled and clicked.
bool rightClickChoose(QWidget* t, const QPoint& p, const QString& menuName, const QString& action, bool screenshot = false,
                      std::function<void()> onMenu = nullptr) {
  bool done = false, found = false;
  QTimer::singleShot(250, qApp, [&] {
    QMenu* m = popupMenu(menuName);
    if (m && onMenu) onMenu();
    QAction* a = menuAction(m, action);
    found = a && a->isEnabled();
    if (!found) {
      QStringList names;
      if (m) for (QAction* x : m->actions()) if (!x->objectName().isEmpty()) names << x->objectName();
      QWidget* pw = QApplication::activePopupWidget();
      note(QString("menu '%1' %2, action '%3' missing/disabled; actions: %4; active popup: %5").arg(menuName, m ? "found" : "NOT found", action,
           names.join(" "), pw ? pw->objectName() + "/" + pw->metaObject()->className() : QString("none")));
    }
    if (found) clickMenuAction(m, a);
    else if (m) m->close();
    else if (QWidget* pw = QApplication::activePopupWidget()) pw->close();
    done = true;
  });
  rightClick(t, p);
  waitFor([&] { return done; }, 4000);
  pump(150);
  (void)screenshot;
  return found;
}
void closePopups() {
  for (int i = 0; i < 4 && QApplication::activePopupWidget(); ++i) { QApplication::activePopupWidget()->close(); pump(60); }
}
QToolButton* ribbonButton(pcd::MainWindow& w, QAction* a) {
  for (auto* tb : w.ribbon()->findChildren<QToolBar*>())
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(a)); b && b->isVisible()) return b;
  return nullptr;
}
void shot(pcd::MainWindow& w, const QString& name, bool screen = false) {
  if (!g_shots) return;
  pump(300);
  QDir().mkpath(PC_I2D_SHOTS_DIR);
  const QString path = QString(PC_I2D_SHOTS_DIR) + "/" + name;
  QPixmap pm;
  if (screen) {
    const QRect g = w.frameGeometry();
    pm = QGuiApplication::primaryScreen()->grabWindow(0, g.x(), g.y(), g.width(), g.height());
  } else {
    pm = w.grab();
  }
  note(QString("shot %1 %2").arg(path, pm.save(path) ? "saved" : "FAILED"));
}

// ---- model / view queries
double volume(pcd::MainWindow& w) {
  try { return w.modelResult().shape.IsNull() ? 0 : pc::model::massProperties(w.modelResult().shape).volume; } catch (...) { return -1; }
}
QPointF facePx(pcd::MainWindow& w, const QString& face) {
  double c[3], n[3];
  if (!w.viewer()->faceCentroid(face, c, n)) return QPointF(-1, -1);
  const QPointF p0 = w.viewer()->worldToScreen(c);
  for (int r = 0; r <= 300; r += 5)
    for (int k = 0; k < (r == 0 ? 1 : 24); ++k) {
      const double a = k * M_PI / 12.0;
      const QPointF p = p0 + QPointF(std::cos(a), std::sin(a)) * r;
      if (w.viewer()->pickAt(p.toPoint()) == face && w.viewer()->edgeAt(p.toPoint()).isEmpty()) return p;
    }
  return QPointF(-1, -1);
}
QString pickFace(pcd::MainWindow& w, const QPointF& p) { return w.viewer()->pickAt(p.toPoint(), pc::viewer::ShapeViewer::PickFace); }
QImage viewImage(pcd::MainWindow& w) { return w.viewer()->grabFramebuffer().convertToFormat(QImage::Format_RGB32); }
QColor px(const QImage& im, const QPointF& p) {
  const QPoint q = p.toPoint() * im.devicePixelRatio();
  return im.valid(q) ? im.pixelColor(q) : QColor();
}
int colorDist(const QColor& a, const QColor& b) { return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue()); }
// faces of the model: planar with outward normal (nx,ny,nz) and centroid coordinate c[axis] == coord
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
QStringList facesOfBody(pcd::MainWindow& w, const QString& b) {
  QStringList l;
  if (const auto* bi = w.modelResult().body(b.toStdString())) for (const auto& f : bi->faces) l << QString::fromStdString(f);
  return l;
}
QToolButton* visStatus(pcd::MainWindow& w) { return w.findChild<QToolButton*>("visStatus"); }
QString visStatusText(pcd::MainWindow& w) {
  auto* b = visStatus(w);
  return b && b->isVisible() ? b->text() : QString();
}
QTreeWidgetItem* bodyRow(pcd::MainWindow& w, const QString& body) {
  auto* bn = w.structurePanel()->bodiesNode();
  for (int i = 0; bn && i < bn->childCount(); ++i) if (bn->child(i)->data(0, Qt::UserRole).toString() == body) return bn->child(i);
  return nullptr;
}
void clickTreeItem(QTreeWidget* t, QTreeWidgetItem* it, Qt::MouseButton b = Qt::LeftButton) {
  t->scrollToItem(it);
  const QRect r = t->visualItemRect(it);
  const QPoint p(r.left() + std::min(60, r.width() / 2), r.center().y());
  if (b == Qt::RightButton) rightClick(t->viewport(), p);
  else { QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, p); pump(150); }
}
std::string pexpr(pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<missing>");
}
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = w.parameterTable()->model();
  for (int r = 0; m && r < m->rowCount(); ++r) if (m->index(r, 0).data().toString() == name) return r;
  return -1;
}
void topView(pcd::MainWindow& w) {
  w.viewer()->setView(pc::viewer::ShapeViewer::View::Top);
  w.viewer()->fitAll();
  pump(250);
}
void isoView(pcd::MainWindow& w) {
  w.viewer()->setView(pc::viewer::ShapeViewer::View::Iso);
  w.viewer()->fitAll();
  pump(250);
}


// ---- import2d helpers
void type(pcd::MainWindow& w, const QString& s) {
  for (const QChar ch : s) {
    ensureActive(w);
    QWidget* f = QApplication::focusWidget();
    QTest::keyClick((f && w.isAncestorOf(f) && f->isVisible()) ? f : view(w), ch.toLatin1());
    pump(15);
  }
  pump(30);
}
pcd::Import2DController* ctl(pcd::MainWindow& w) { return w.import2d(); }
QString corpus(const char* rel) { return QString(PC_CORPUS_DIR) + "/" + rel; }
std::vector<const pc::Feature*> imported(pcd::MainWindow& w, const std::string& group = std::string()) {
  std::vector<const pc::Feature*> out;
  for (const auto& f : w.document().features)
    if (i2::isImportedSketch(f) && (group.empty() || f.prop("importGroup") == group)) out.push_back(&f);
  return out;
}
const pc::Feature* sketchOfLayer(pcd::MainWindow& w, const std::string& layer, const std::string& group = "Cizim1") {
  for (const auto* f : imported(w, group)) if (f->prop("layer") == layer && w.document().layers.layerOfSketch(f->id) == layer) return f;
  return nullptr;
}
int curveCount(pcd::MainWindow& w, const std::string& group = std::string()) {
  int n = 0;
  for (const auto* f : imported(w, group)) n += int(f->sketch->entities.size());
  return n;
}
// lengths of the non-construction lines of a sketch (rounded to 1e-6)
std::multiset<double> lineLengths(const pc::Feature* f) {
  std::multiset<double> out;
  if (!f || !f->sketch) return out;
  const pc::Sketch& s = *f->sketch;
  for (const auto& e : s.entities) {
    if (e.type != pc::SkEntityType::Line || e.construction) continue;
    const auto *a = s.point(e.points[0]), *b = s.point(e.points[1]);
    out.insert(std::round(std::hypot(b->x - a->x, b->y - a->y) * 1e6) / 1e6);
  }
  return out;
}
bool hasLength(const std::multiset<double>& s, double v, double tol = 1e-6) {
  for (double x : s) if (std::fabs(x - v) <= tol) return true;
  return false;
}
// screen point of a curve: midpoint of a line / quadrant point of a circle, on screen
QPointF curvePx(pcd::MainWindow& w, const i2::CurveRef& c) {
  const auto pts = i2::curvePoints(w.document(), w.modelResult(), c);
  if (pts.size() < 2) return QPointF(-1, -1);
  gp_Pnt m = pts[0];
  if (pts.size() == 2) m = gp_Pnt((pts[0].XYZ() + pts[1].XYZ()) * 0.5);
  const double p[3] = {m.X(), m.Y(), m.Z()};
  return w.viewer()->worldToScreen(p);
}
// darkest / reddest pixel in a 7x7 box (lines are thin)
QColor nearColor(const QImage& im, const QPointF& p, const std::function<int(const QColor&)>& score) {
  QColor best;
  int bs = -1 << 30;
  for (int dx = -3; dx <= 3; ++dx)
    for (int dy = -3; dy <= 3; ++dy) {
      const QColor c = px(im, p + QPointF(dx, dy));
      if (!c.isValid()) continue;
      const int s = score(c);
      if (s > bs) { bs = s; best = c; }
    }
  return best;
}
int darkness(const QColor& c) { return 765 - c.red() - c.green() - c.blue(); }
int redness(const QColor& c) { return c.red() - (c.green() + c.blue()) / 2; }
// curve under the pixel (edge pick, 9 px)
QString edgeUnder(pcd::MainWindow& w, const QPointF& p) { return w.viewer()->edgeAt(p.toPoint()); }
// lines of a sketch: entity ids with their 3D points
std::vector<i2::CurveRef> curvesOf(const pc::Feature* f) {
  std::vector<i2::CurveRef> out;
  if (f && f->sketch) for (const auto& e : f->sketch->entities) out.push_back({f->id, e.id});
  return out;
}
gp_Pnt midOf(pcd::MainWindow& w, const i2::CurveRef& c) {
  const auto pts = i2::curvePoints(w.document(), w.modelResult(), c);
  if (pts.size() < 2) return gp_Pnt();
  return gp_Pnt((pts[0].XYZ() + pts[1].XYZ()) * 0.5);
}
QTreeWidgetItem* layerRow(pcd::MainWindow& w, const QString& n) { return ctl(w)->layersPanel()->row(n); }
void clickCheck(QTreeWidget* t, QTreeWidgetItem* it) {
  t->scrollToItem(it);
  const QRect r = t->visualItemRect(it);
  // the check indicator sits at the left of column 0 (after the branch indentation)
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
bool rightClickLayer(pcd::MainWindow& w, const QString& layer, const QString& action) {
  QTreeWidget* t = ctl(w)->layersPanel()->tree();
  QTreeWidgetItem* it = layerRow(w, layer);
  if (!it) return false;
  t->scrollToItem(it);
  const QRect r = t->visualItemRect(it);
  return rightClickChoose(t->viewport(), QPoint(r.left() + 40, r.center().y()), "layersContextMenu", action);
}

// ============================================================================================ the test
void run(pcd::MainWindow& w, const QString& tmp) {
  QUndoStack* undo = w.undoStack();
  auto* c = ctl(w);
  check(c != nullptr, "Import2DController exists (MainWindow::import2d)");
  if (!c) return;
  const QString plateDxf = corpus("plate/plate_00/sheet.dxf");
  check(QFileInfo::exists(plateDxf), "corpus plate DXF: " + plateDxf);

  // ------------------------------------------------------------------ 1. UI entry points
  section("1. Dosya > İçe aktar > 2B çizim (eğri olarak), ribbon, Katmanlar tab");
  QMenu* mImport = w.findChild<QMenu*>("menuImport");
  check(mImport && mImport->actions().contains(c->importAction()), "Dosya menu has 'İçe aktar' > '2B çizim (eğri olarak)...'");
  check(c->importAction()->isEnabled(), "import action enabled (drawing library built in)");
  {
    pcd::RibbonGroup* g = w.ribbon()->group("import2d");
    check(g && w.ribbon()->groupsOf("display").contains(g) && g->buttonFor(c->viewSelectAction()) && g->buttonFor(c->placeViewAction()),
          "ribbon Görüntü > Katmanlar / 2B çizim: 2B çizim, Katmanlar, Görünüş seç, Görünüşü yerleştir");
  }
  QTabWidget* st = w.structurePanel()->tabs();
  QStringList tabs;
  for (int i = 0; i < st->count(); ++i) tabs << st->tabText(i);
  check(tabs.contains("Katmanlar") && tabs.contains("Gruplar") && tabs.contains("Geçmiş"), "Yapı panel tabs: " + tabs.join(", "));
  w.structurePanel()->setCurrentPage("layers");
  pump(100);
  check(c->layerNames() == QStringList{"Katman0"} && layerRow(w, "Katman0"), "empty document: only 'Katman0'");

  // ------------------------------------------------------------------ 2. import through the menu + file dialog
  section("2. Import plate_00/sheet.dxf through the file dialog: curves in 3D, true scale, layers");
  const int u0 = undo->index();
  g_onModal = [&](QWidget* m) { acceptFileDialog(m, QFileInfo(plateDxf).absoluteFilePath()); };
  c->importAction()->trigger();
  waitFor([&] { return !imported(w).empty(); }, 15000);
  g_onModal = nullptr;
  pump(300);
  check(!imported(w).empty(), "imported sketches: " + QString::number(imported(w).size()));
  check(undo->index() == u0 + 1, QString("import = ONE undo step (%1 -> %2)").arg(u0).arg(undo->index()));
  const QStringList layers = c->layerNames();
  note("layers: " + layers.join(", ") + " | " + c->lastMessage());
  for (const char* l : {"GORUNUR", "GIZLI", "EKSEN", "Ölçüler/Notlar"}) check(layers.contains(QString::fromUtf8(l)), QString("layer '%1' created").arg(QString::fromUtf8(l)));
  check(layers.first() == "Katman0", "'Katman0' stays first");
  const pc::Feature* vis = sketchOfLayer(w, "GORUNUR");
  const pc::Feature* hid = sketchOfLayer(w, "GIZLI");
  const pc::Feature* notes = sketchOfLayer(w, "Ölçüler/Notlar");
  check(vis && hid && notes, "sketch per layer (GORUNUR, GIZLI, Ölçüler/Notlar)");
  if (!vis || !hid || !notes) return;
  const std::string visId = vis->id, hidId = hid->id, notesId = notes->id;
  check(vis->sketch->plane.kind == pc::SketchPlaneKind::XY, "curves lie on the XY plane");
  {
    const auto L = lineLengths(vis);
    int circles = 0;
    double r = 0;
    for (const auto& e : vis->sketch->entities) if (e.type == pc::SkEntityType::Circle) { ++circles; r = e.radius; }
    check(hasLength(L, 100) && hasLength(L, 60) && hasLength(L, 8), "true scale: visible lines 100, 60, 8 mm");
    check(circles == 4 && std::fabs(r - 4.5) < 1e-9, QString("4 holes Ø9 (r=%1)").arg(r));
  }
  bool consOnly = true;
  for (const auto& e : notes->sketch->entities) consOnly &= e.construction;
  check(!notes->sketch->entities.empty() && consOnly, QString("dimension curves are construction (%1)").arg(notes->sketch->entities.size()));
  check(i2::notesOf(*notes).size() >= 6 && c->noteLabelCount() >= 6, QString("texts become notes, drawn as labels (%1)").arg(c->noteLabelCount()));
  check(w.document().layers.find("GIZLI") && w.document().layers.find("GIZLI")->lineStyle == "dashed", "GIZLI layer line style 'dashed' (DXF linetype)");
  check(w.viewer()->styledEdgeCount() > 0, QString("layer styles drawn (%1 styled curves)").arg(w.viewer()->styledEdgeCount()));
  topView(w);
  shot(w, "01_imported_top.png");
  isoView(w);
  shot(w, "02_imported_iso.png");

  // ------------------------------------------------------------------ 3. layers panel: visibility (pixel + pick)
  section("3. Katmanlar: hide GIZLI with its check box -> not drawn, not pickable; undo");
  topView(w);
  i2::CurveRef hc{hidId, hid->sketch->entities.front().id};
  const QPointF hp = curvePx(w, hc);
  QImage im0 = viewImage(w);
  const int dark0 = darkness(nearColor(im0, hp, darkness));
  const QString e0 = edgeUnder(w, hp);
  check(e0 == QString::fromStdString(i2::curveName(hc)) || e0.startsWith("sketch:"), "hidden-line curve pickable before: " + e0);
  QTreeWidgetItem* gr = layerRow(w, "GIZLI");
  check(gr && gr->checkState(0) == Qt::Checked, "GIZLI row checked");
  const int u1 = undo->index();
  if (gr) clickCheck(c->layersPanel()->tree(), gr);
  waitFor([&] { return !c->layerVisible("GIZLI"); }, 3000);
  check(!c->layerVisible("GIZLI") && undo->index() == u1 + 1, "check box: GIZLI hidden, ONE undo step");
  pump(200);
  QImage im1 = viewImage(w);
  const int dark1 = darkness(nearColor(im1, hp, darkness));
  check(dark0 > 200 && dark1 < dark0 - 100, QString("hidden layer not drawn (darkness %1 -> %2)").arg(dark0).arg(dark1));
  const QString e1 = edgeUnder(w, hp);
  check(!e1.startsWith(QString("sketch:%1:").arg(QString::fromStdString(hidId))), "hidden layer not pickable (edge under cursor: '" + e1 + "')");
  check(w.sketchOverlayEdgeCount() > 0, "other layers still drawn");
  shot(w, "03_layers_gizli_hidden.png");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(200);
  check(c->layerVisible("GIZLI") && layerRow(w, "GIZLI")->checkState(0) == Qt::Checked, "Ctrl+Z: GIZLI visible again, check box follows");
  check(edgeUnder(w, hp) == e0, "pickable again after undo");

  section("3b. Context menu: Yalnız bunu göster / Tüm katmanları göster / colour / line style");
  check(rightClickLayer(w, "GORUNUR", "layOnly"), "right click GORUNUR > Yalnız bunu göster");
  pump(200);
  bool only = c->layerVisible("GORUNUR");
  for (const QString& n : c->layerNames()) if (n != "GORUNUR") only &= !c->layerVisible(n);
  check(only, "only GORUNUR visible");
  shot(w, "04_layers_only_gorunur.png");
  check(rightClickLayer(w, "GORUNUR", "layShowAll"), "Tüm katmanları göster");
  pump(200);
  bool all = true;
  for (const QString& n : c->layerNames()) all &= c->layerVisible(n);
  check(all, "every layer visible");
  // colour: GORUNUR red -> red pixels on a visible curve
  i2::CurveRef vc{visId, vis->sketch->entities.front().id};
  for (const auto& e : vis->sketch->entities) if (e.type == pc::SkEntityType::Line) { vc.eid = e.id; break; }
  check(c->setLayerColor("GORUNUR", QColor(220, 30, 30)), "Renk: GORUNUR -> #DC1E1E");
  pump(250);
  const QColor rc = nearColor(viewImage(w), curvePx(w, vc), redness);
  check(redness(rc) > 80, QString("GORUNUR curves drawn red (%1)").arg(rc.name()));
  check(layerRow(w, "GORUNUR")->data(1, Qt::UserRole).toString().toUpper() == "#DC1E1E", "panel swatch colour");
  check(w.document().layers.find("EKSEN")->lineStyle == "center", "EKSEN layer line style 'center' (DXF linetype)");
  check(c->setLayerLineStyle("EKSEN", "dotted") && w.document().layers.find("EKSEN")->lineStyle == "dotted", "Çizgi stili: EKSEN = noktalı");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  check(w.document().layers.find("EKSEN")->lineStyle == "center", "undo line style");
  check(c->renameLayer("EKSEN", "Eksenler") && c->layerNames().contains("Eksenler") && !c->layerNames().contains("EKSEN"), "Yeniden adlandır: EKSEN -> Eksenler");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  check(c->layerNames().contains("EKSEN"), "undo rename");
  shot(w, "05_layer_colours.png");

  // ------------------------------------------------------------------ 4. Görünüş seç (one click)
  section("4. 'Görünüş seç': one click on the front view selects its connected curves");
  topView(w);
  // front view = the 100 x 8 view (below the top view): its lowest visible line
  i2::CurveRef low{visId, 0};
  double ymin = 1e300;
  for (const auto& cv : curvesOf(vis)) {
    const auto pts = i2::curvePoints(w.document(), w.modelResult(), cv);
    if (pts.size() != 2) continue;
    if (std::fabs(pts[0].Y() - pts[1].Y()) < 1e-9 && pts[0].Y() < ymin) { ymin = pts[0].Y(); low = cv; }
  }
  c->viewSelectAction()->setChecked(true);
  vclick(w, curvePx(w, low));
  pump(200);
  const auto selCurves = c->selectedCurves();
  int visSel = 0, hidSel = 0;
  double sy0 = 1e300, sy1 = -1e300;
  for (const SelItem& s : selCurves) {
    i2::CurveRef r;
    if (!i2::parseCurveName(s.name.toStdString(), r)) continue;
    if (r.sid == visId) ++visSel;
    if (r.sid == hidId) ++hidSel;
    for (const auto& p : i2::curvePoints(w.document(), w.modelResult(), r)) { sy0 = std::min(sy0, p.Y()); sy1 = std::max(sy1, p.Y()); }
  }
  check(visSel == 4 && hidSel == 4, QString("front view selected: 4 visible + 4 hidden lines (%1 + %2 of %3)").arg(visSel).arg(hidSel).arg(selCurves.size()));
  check(sy1 - sy0 < 25, QString("nothing of the top view selected (y span %1)").arg(sy1 - sy0));
  c->viewSelectAction()->setChecked(false);
  shot(w, "06_view_selected.png");

  // ------------------------------------------------------------------ 5. Görünüşü yerleştir
  section("5. 'Görünüşü yerleştir': fold the front view up 90° about its top edge onto the top view's bottom edge");
  const int u2 = undo->index();
  c->placeViewAction()->trigger();
  pump(300);
  pcd::PlaceViewDialog* dlg = c->placeDialog();
  check(dlg && dlg->isVisible(), "dialog 'import2dPlaceDialog' open (non-modal)");
  if (!dlg) return;
  const i2::Placement pl0 = dlg->placement();
  check(pl0.ok && pl0.edge == i2::FoldEdge::Top, "default fold edge: top (faces the top view): " + dlg->findChild<QLabel*>("placeInfo")->text());
  // the top view's bottom edge: y of the lowest line of the remaining GORUNUR curves above the front view
  double topViewY0 = 1e300;
  for (const auto& cv : curvesOf(vis)) {
    const auto pts = i2::curvePoints(w.document(), w.modelResult(), cv);
    bool inSel = false;
    for (const SelItem& s : selCurves) if (s.name == QString::fromStdString(i2::curveName(cv))) inSel = true;
    if (inSel) continue;
    for (const auto& p : pts) topViewY0 = std::min(topViewY0, p.Y());
  }
  check(std::fabs(pl0.anchorDst.Y() - topViewY0) < 1e-6 && std::fabs(pl0.anchorDst.X() - pl0.anchorSrc.X()) < 1e-9,
        QString("snap target on the top view's bottom edge (y %1 / %2)").arg(pl0.anchorDst.Y()).arg(topViewY0));
  check(w.viewer()->ghostVisible(), "ghost preview of the folded view");
  isoView(w);
  shot(w, "07_place_dialog.png", true);
  // anchor picking: "Seç" + click on a curve end -> field updated; then the typed value restores the default
  auto* pickDst = dlg->findChild<QPushButton*>("placePickDst");
  auto* dstEdit = dlg->findChild<QLineEdit*>("placeDst");
  const QString dst0 = dstEdit->text();
  QTest::mouseClick(pickDst, Qt::LeftButton);
  pump(100);
  check(dlg->pickTarget() == 2, "'Seç' (hedef) arms anchor picking");
  topView(w);
  i2::CurveRef someTop{visId, 0};
  for (const auto& cv : curvesOf(vis)) {
    bool inSel = false;
    for (const SelItem& s : selCurves) if (s.name == QString::fromStdString(i2::curveName(cv))) inSel = true;
    const auto pts = i2::curvePoints(w.document(), w.modelResult(), cv);
    if (!inSel && pts.size() == 2) { someTop = cv; break; }
  }
  const auto stp = i2::curvePoints(w.document(), w.modelResult(), someTop);
  const double endPx[3] = {stp[0].X(), stp[0].Y(), stp[0].Z()};
  const QPointF ep = w.viewer()->worldToScreen(endPx);
  const QPointF mp = curvePx(w, someTop);
  vclick(w, ep + (mp - ep) * (4.0 / std::max(4.0, std::hypot(mp.x() - ep.x(), mp.y() - ep.y()))));   // on the curve, near its end
  pump(200);
  check(dlg->pickTarget() == 0 && dlg->placement().anchorDst.Distance(stp[0]) < 1e-6,
        "click near a curve end snaps the target anchor to that end point: " + dstEdit->text());
  dstEdit->setText(dst0);
  QMetaObject::invokeMethod(dstEdit, "editingFinished");
  pump(100);
  check(dlg->placement().anchorDst.Distance(pl0.anchorDst) < 1e-9, "typed anchor restores the default target");
  QTest::mouseClick(dlg->findChild<QPushButton*>("placeApply"), Qt::LeftButton);
  waitFor([&] { return undo->index() == u2 + 1; }, 8000);
  pump(300);
  check(undo->index() == u2 + 1 && !c->placeDialog(), QString("Uygula: ONE undo step (%1 -> %2), dialog closed (%3)").arg(u2).arg(undo->index()).arg(c->placeDialog() ? "open" : "closed"));
  // placed sketches: on a hidden datum plane, vertical, standing on the top view's bottom edge, z 0..8
  const pc::Feature* placed = nullptr;
  const pc::Feature* plane = nullptr;
  for (const auto* f : imported(w)) if (f->sketch->plane.kind == pc::SketchPlaneKind::OnPlane && f->prop("layer") == "GORUNUR") placed = f;
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Plane && f.prop("import2d") == "1") plane = &f;
  check(placed && plane && plane->prop("show") == "0", "placed GORUNUR curves in a new sketch on a hidden datum plane (" +
        QString::fromStdString(plane ? plane->id + " " + plane->prop("kind") + " axis=" + plane->prop("axis") + " angle=" + plane->prop("angle") + " offset=" + plane->prop("offset") : "-") + ")");
  if (!placed || !plane) return;
  const std::string placedId = placed->id, planeId = plane->id;
  double z0 = 1e300, z1 = -1e300, py0 = 1e300, py1 = -1e300;
  for (const auto& cv : curvesOf(placed))
    for (const auto& p : i2::curvePoints(w.document(), w.modelResult(), cv)) {
      z0 = std::min(z0, p.Z()); z1 = std::max(z1, p.Z()); py0 = std::min(py0, p.Y()); py1 = std::max(py1, p.Y());
    }
  check(std::fabs(z0) < 1e-6 && std::fabs(z1 - 8) < 1e-6, QString("folded view stands up: z %1..%2 (0..8)").arg(z0).arg(z1));
  check(std::fabs(py0 - topViewY0) < 1e-6 && std::fabs(py1 - topViewY0) < 1e-6, QString("in the plane y = %1 (top view's bottom edge)").arg(topViewY0));
  check(layerRow(w, "GIZLI") && w.document().layers.layerOfSketch(placedId) == "GORUNUR", "placed curves keep their layer");
  int placedHidden = 0;
  for (const auto* f : imported(w)) if (f->sketch->plane.kind == pc::SketchPlaneKind::OnPlane && f->prop("layer") == "GIZLI") placedHidden = int(f->sketch->entities.size());
  check(placedHidden == 4, QString("hidden lines of the view moved too (%1)").arg(placedHidden));
  isoView(w);
  shot(w, "08_view_placed.png");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(200);
  check(!w.document().feature(placedId) && !w.document().feature(planeId), "undo placement: placed sketch + datum plane removed");
  {
    const pc::Feature* v2 = w.document().feature(visId);
    check(v2 && v2->sketch->entities.size() == 12, QString("undo: front view curves back in the XY sketch (%1 entities)").arg(v2 ? v2->sketch->entities.size() : 0));
  }
  key(w, Qt::Key_Y, Qt::ControlModifier);
  pump(200);
  check(w.document().feature(placedId) != nullptr, "redo placement");

  // ------------------------------------------------------------------ 6. Fill + Pull on XY and on the vertical plane
  section("6. Regions (Fill) of the imported curves + Pull: vertical view 60 mm, then the top view 20 mm");
  const QString vregion = QString("sketch:%1:r0").arg(QString::fromStdString(placedId));
  isoView(w);
  QPointF vp = facePx(w, vregion);
  check(vp.x() >= 0, "vertical region " + vregion + " drawn and pickable");
  key(w, Qt::Key_P);
  vclick(w, vp);
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, vregion}), "Pull: vertical region selected");
  const int u3 = undo->index();
  type(w, "60");
  key(w, Qt::Key_Return);
  waitFor([&] { return undo->index() == u3 + 1; }, 8000);
  pump(300);
  check(std::fabs(volume(w) - 100 * 8 * 60) < 0.5, QString("vertical view pulled 60 mm: volume %1 (48000)").arg(volume(w), 0, 'f', 2));
  shot(w, "09_pull_vertical.png");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(300);
  key(w, Qt::Key_Escape);
  check(volume(w) == 0, "undo: no solid");
  const QString tregion = QString("sketch:%1:r0").arg(QString::fromStdString(visId));
  topView(w);
  QPointF tp = facePx(w, tregion);
  check(tp.x() >= 0, "top-view region " + tregion + " (outline with 4 holes) drawn and pickable");
  key(w, Qt::Key_P);
  vclick(w, tp);
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, tregion}), "Pull: top-view region selected");
  const int u4 = undo->index();
  type(w, "20");
  key(w, Qt::Key_Return);
  waitFor([&] { return undo->index() == u4 + 1; }, 8000);
  pump(300);
  const double vexp = (100.0 * 60.0 - 4 * M_PI * 4.5 * 4.5) * 20.0;
  check(std::fabs(volume(w) - vexp) < 0.5, QString("top view pulled 20 mm: volume %1 (%2)").arg(volume(w), 0, 'f', 2).arg(vexp, 0, 'f', 2));
  key(w, Qt::Key_Escape);
  isoView(w);
  shot(w, "10_pull_top_solid.png");

  // ------------------------------------------------------------------ 7. body to a layer, hide
  section("7. Body moved to a new layer ('Seçimi bu katmana taşı') and hidden");
  const QString body = w.bodyNames().isEmpty() ? QString() : w.bodyNames().first();
  check(!body.isEmpty() && c->layerOfSketch("x") == "Katman0", "solid body in Katman0 by default: " + body);
  const QString nl = c->newLayer("Gövdeler");
  check(nl == "Gövdeler" && layerRow(w, "Gövdeler"), "Yeni katman 'Gövdeler'");
  w.selection()->set({SelItem{SelItem::Kind::Body, body}});
  check(rightClickLayer(w, "Gövdeler", "layMoveSel"), "right click Gövdeler > Seçimi bu katmana taşı");
  check(w.document().layers.layerOfBody(body.toStdString()) == "Gövdeler", "body is in 'Gövdeler'");
  const QString aface = w.modelResult().body(body.toStdString()) ? QString::fromStdString(w.modelResult().body(body.toStdString())->faces.front()) : QString();
  check(!aface.isEmpty() && c->setLayerVisible("Gövdeler", false), "Gövdeler hidden");
  topView(w);
  bool anyBodyPick = false;
  for (int x = 300; x < 1100; x += 37) for (int y = 200; y < 700; y += 37) {
    const QString n = w.viewer()->pickAt(QPoint(x, y), pc::viewer::ShapeViewer::PickFace);
    if (!n.isEmpty() && !n.startsWith("sketch:")) anyBodyPick = true;
  }
  check(!anyBodyPick, "faces of a hidden layer are neither drawn nor pickable");
  shot(w, "11_body_layer_hidden.png");
  c->setLayerVisible("Gövdeler", true);
  // hide GIZLI for the save test
  c->setLayerVisible("GIZLI", false);

  // ------------------------------------------------------------------ 8. save / reopen
  section("8. Save / reopen keeps layers, curves, placement, notes");
  const pc::Document before = w.document();
  const QString path = tmp + "/import2d.pcad";
  std::string err;
  check(pc::saveDocument(before, path.toStdString(), &err), "saved " + path);
  {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray txt = f.readAll();
    check(txt.contains("\"layers\"") && txt.contains("importedCurves"), ".pcad has \"layers\" and imported curve sketches");
  }
  w.setAutoYes(true);
  check(w.openFile(path), "reopened");
  pump(300);
  check(c->layerNames().contains("GORUNUR") && c->layerNames().contains("Gövdeler"),
        "layers after reopen: " + c->layerNames().join(", "));
  check(!c->layerVisible("GIZLI") && layerRow(w, "GIZLI")->checkState(0) == Qt::Unchecked, "GIZLI still hidden");
  check(w.document().layers.find("GORUNUR")->color == "#DC1E1E", "GORUNUR colour kept");
  check(curveCount(w) == [&] { int n = 0; for (const auto& f : before.features) if (i2::isImportedSketch(f)) n += int(f.sketch->entities.size()); return n; }(),
        QString("curve count kept (%1)").arg(curveCount(w)));
  check(w.document().feature(placedId) && w.document().feature(placedId)->sketch->plane.kind == pc::SketchPlaneKind::OnPlane, "placed view kept");
  check(std::fabs(volume(w) - vexp) < 0.5, "solid kept");
  check(w.document().layers.layerOfBody(body.toStdString()) == "Gövdeler", "body layer kept");
  check(c->noteLabelCount() >= 6, "notes drawn after reopen");
  isoView(w);
  shot(w, "12_reopened.png");

  // ------------------------------------------------------------------ 9. undo of the import
  section("9. Undo of the import (new document)");
  w.newDocument();
  pump(200);
  const int u5 = undo->index();
  check(c->importFile(QFileInfo(corpus("lbracket/lbracket_00/sheet.dxf")).absoluteFilePath(), pc::SketchPlane(), 0, false), "lbracket DXF imported (API)");
  check(undo->index() == u5 + 1 && !imported(w).empty(), "one undo step");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(200);
  check(imported(w).empty() && c->layerNames() == QStringList{"Katman0"}, "Ctrl+Z removes every imported curve and layer");

  // ------------------------------------------------------------------ 10. drag & drop
  section("10. Drag & drop of a .dxf onto the window");
  {
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile(QFileInfo(corpus("lbracket/lbracket_00/sheet.dxf")).absoluteFilePath())});
    const QPoint at = w.viewer()->mapTo(&w, w.viewer()->rect().center());
    QDragEnterEvent de(at, Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &de);
    check(de.isAccepted(), "drag enter accepted for .dxf");
    QDropEvent dr(QPointF(at), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &dr);
    waitFor([&] { return !imported(w).empty(); }, 15000);
    check(!imported(w).empty(), QString("dropped DXF imported (%1 curves)").arg(curveCount(w)));
    QMimeData bad;
    bad.setUrls({QUrl::fromLocalFile(tmp + "/x.step")});
    QDragEnterEvent de2(at, Qt::CopyAction, &bad, Qt::LeftButton, Qt::NoModifier);
    de2.setAccepted(false);
    QApplication::sendEvent(&w, &de2);
    check(!de2.isAccepted(), "drag enter refused for other files");
  }
  isoView(w);
  shot(w, "13_dropped_lbracket.png");

  // ------------------------------------------------------------------ 11. vector PDF
  section("11. Vector PDF: layers by line role, scale from the drawing");
  w.newDocument();
  pump(200);
  const bool pdfOk = c->importFile(QFileInfo(corpus("plate/plate_00/sheet.pdf")).absoluteFilePath(), pc::SketchPlane(), 0, false);
  check(pdfOk && c->lastMessage().contains("PDF"), "plate PDF imported: " + c->lastMessage());
  const QStringList pl = c->layerNames();
  note("PDF layers: " + pl.join(", "));
  check(pl.contains("Görünür") && pl.contains("Ölçüler/Notlar"), "PDF layers by role (Görünür, Ölçüler/Notlar ...)");
  bool has100 = false;
  for (const auto* f : imported(w)) if (f->prop("layer") == "Görünür") has100 = hasLength(lineLengths(f), 100, 0.05);
  check(has100, "PDF at true scale: a 100 mm line");
  topView(w);
  shot(w, "14_pdf_import.png");
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
  QApplication::setApplicationName("pc_gui_harness_import2d");   // no layout persistence
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
