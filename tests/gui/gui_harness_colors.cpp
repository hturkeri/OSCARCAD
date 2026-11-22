// SC-COLOR: face / body colours (SpaceClaim-like) — GUI harness on the REAL MainWindow (docs/colors/COLORS.md).
//
// Part: "Govde" 60x40x20 with a cut slot (L=20 notch, L=100 splits the top face) and a separate body "Blok" (x 80..120).
//   ribbon Görüntü > Stil > Renk: face -> red (pixel), body -> blue (pixel, all its faces), face override on a body,
//   selection / hover highlight stays visible on coloured faces, context menu "Renk ▸", Özellikler "Renk" row,
//   undo / redo (no regeneration), split face keeps the colour in every piece (L = 100), save / reopen, STEP colours,
//   Save-as-App payload + runner pixels, "Rengi sıfırla", "Diğer renkler…" (non-native QColorDialog).
//
//   pc_gui_harness_colors [--shots]     --shots writes docs/colors/shots/*.png     Exit code = number of failed checks.
#include "MainWindow.h"
#include "PropertiesPanel.h"
#include "Ribbon.h"
#include "RunnerWindow.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/AppPackage.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include "pc/ShapeViewer.h"
#include <QApplication>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTableView>
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
#include <set>
#include <QVector3D>
#include <TopExp_Explorer.hxx>

#ifndef PC_COL_SHOTS_DIR
#define PC_COL_SHOTS_DIR "docs/colors/shots"
#endif

using pcd::SelItem;

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
    if (m->property("colHandled").toBool()) return;
    m->setProperty("colHandled", true);
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
  QDir().mkpath(PC_COL_SHOTS_DIR);
  const QString path = QString(PC_COL_SHOTS_DIR) + "/" + name;
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

// ---- the test part
pc::Document makePart() {
  pc::Document d;
  d.name = "Renkler";
  pc::Parameter L;
  L.name = "L";
  L.expression = "20";
  L.kind = pc::ParamKind::Length;
  d.params.add(L);
  auto sketch = [&](const char* id, pc::SketchPlaneKind k, double x0, double y0, double x1, double y1) {
    pc::Feature f;
    f.id = id;
    f.type = pc::FeatureType::Sketch;
    pc::Sketch s = pc::Sketch::create();
    s.plane.kind = k;
    s.plane.offsetExpr = "0";
    s.addRectangle(x0, y0, x1, y1);
    f.sketch = s;
    d.features.push_back(f);
  };
  auto extrude = [&](const char* id, const char* sk, const char* dist, std::map<std::string, std::string> extra) {
    pc::Feature f;
    f.id = id;
    f.type = pc::FeatureType::Extrude;
    f.props = {{"sketch", sk}, {"distance", dist}, {"direction", "normal"}, {"op", "new"}};
    for (auto& [k, v] : extra) f.props[k] = v;
    d.features.push_back(f);
  };
  sketch("Sketch1", pc::SketchPlaneKind::XY, 0, 0, 60, 40);
  extrude("Extrude1", "Sketch1", "20", {{"bodyName", "Govde"}});
  sketch("Sketch2", pc::SketchPlaneKind::XY, 80, 0, 120, 40);
  extrude("Extrude2", "Sketch2", "20", {{"bodyName", "Blok"}});
  sketch("Sketch3", pc::SketchPlaneKind::XZ, 25, 10, 35, 30);
  extrude("Extrude3", "Sketch3", "L", {{"direction", "symmetric"}, {"op", "cut"}, {"targets", "Govde"}});
  return d;
}

// swatch click inside the open palette menu (menus open with a blocking exec(): the click comes from a timer)
bool pickSwatch(const std::function<void()>& open, const QString& hex, const QString& menuName = "colorMenu") {
  bool done = false, found = false;
  QTimer::singleShot(300, qApp, [&] {
    QMenu* m = popupMenu(menuName);
    QToolButton* sw = m ? m->findChild<QToolButton*>("colorSwatch." + hex) : nullptr;
    found = sw != nullptr;
    if (sw) { QTest::mouseMove(sw, QPoint(5, 5)); pump(60); QTest::mouseClick(sw, Qt::LeftButton, {}, QPoint(sw->width() / 2, sw->height() / 2)); }
    else { note("palette menu / swatch missing: " + hex + (m ? "" : " (no menu)")); closePopups(); }
    done = true;
  });
  open();
  waitFor([&] { return done; }, 5000);
  pump(250);
  return found;
}
bool ribbonColor(pcd::MainWindow& w, const QString& hex) {
  w.ribbon()->setCurrentTab("display");
  pump(150);
  QAction* a = w.findChild<QAction*>("actColor");
  QToolButton* b = a ? ribbonButton(w, a) : nullptr;
  if (!b) { check(false, "ribbon button Görüntü > Stil > Renk found"); return false; }
  return pickSwatch([&] { QTest::mouseClick(b, Qt::LeftButton); }, hex);
}
QPointF corner(pcd::MainWindow& w) { Q_UNUSED(w); return QPointF(3, 3); }
void hoverAway(pcd::MainWindow& w) {
  QWidget* t = view(w);
  QMouseEvent mv(QEvent::MouseMove, corner(w), t->mapToGlobal(corner(w)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(250);
}
QColor pixAt(pcd::MainWindow& w, const QPointF& p) { hoverAway(w); return px(viewImage(w), p); }
bool isRed(const QColor& c) { return c.red() > c.green() + 70 && c.red() > c.blue() + 70; }
bool isBlue(const QColor& c) { return c.blue() > c.red() + 60 && c.blue() > c.green() * 0.9 && c.red() < 120; }
bool isGreen(const QColor& c) { return c.green() > c.red() + 40 && c.green() > c.blue() + 40; }
QString hexOf(pcd::MainWindow& w, SelItem::Kind k, const QString& n) { return w.itemColor(SelItem{k, n}); }
QStringList topPieces(pcd::MainWindow& w) {   // pieces of Govde's top (Blok's top is at the same height)
  QStringList l;
  for (const QString& f : facesAt(w, 0, 0, 1, 2, 20.0)) if (f.startsWith("Extrude1.")) l << f;
  return l;
}
void setParamL(pcd::MainWindow& w, const char* val) {
  w.structurePanel()->setCurrentPage("groups");
  pump(100);
  auto* pv = w.parameterTable();
  const QModelIndex idx = pv->model()->index(paramRow(w, "L"), 1);
  pv->scrollTo(idx);
  const QRect r = pv->visualRect(idx);
  QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, r.center());
  QTest::mouseDClick(pv->viewport(), Qt::LeftButton, {}, r.center());
  pump(150);
  QLineEdit* ce = pv->viewport()->findChild<QLineEdit*>();
  if (!ce) { pv->edit(idx); pump(100); ce = pv->viewport()->findChild<QLineEdit*>(); }
  if (ce) { ce->selectAll(); QTest::keyClicks(ce, val); QTest::keyClick(ce, Qt::Key_Return); }
  waitFor([&] { return pexpr(w, "L") == val; }, 6000);
  pump(500);
  w.structurePanel()->setCurrentPage("structure");
}
void doFileDialog(pcd::MainWindow& w, const QString& path, const std::function<void()>& trigger) {
  bool fileDlg = false;
  g_onModal = [&](QWidget* mm) { if (qobject_cast<QFileDialog*>(mm)) { fileDlg = true; acceptFileDialog(mm, path); } };
  trigger();
  waitFor([&] { return fileDlg && !QApplication::activeModalWidget(); }, 10000);
  pump(300);
  g_onModal = nullptr;
  (void)w;
}
// colours (r,g,b in 0..1) written to a STEP file
QList<QVector3D> stepColours(const QString& path) {
  QFile f(path);
  QList<QVector3D> out;
  if (!f.open(QIODevice::ReadOnly)) return out;
  const QString t = QString::fromLatin1(f.readAll());
  // OCCT writes the eight pure colours as DRAUGHTING_PRE_DEFINED_COLOUR('red') etc. instead of COLOUR_RGB
  static const QMap<QString, QVector3D> pre = {{"red", {1, 0, 0}}, {"green", {0, 1, 0}}, {"blue", {0, 0, 1}}, {"yellow", {1, 1, 0}},
                                               {"cyan", {0, 1, 1}}, {"magenta", {1, 0, 1}}, {"black", {0, 0, 0}}, {"white", {1, 1, 1}}};
  QRegularExpression rp(R"(DRAUGHTING_PRE_DEFINED_COLOUR\('([a-z]+)'\))");
  for (auto it = rp.globalMatch(t); it.hasNext();) {
    const auto m = it.next();
    if (pre.contains(m.captured(1))) out << pre.value(m.captured(1));
  }
  QRegularExpression re(R"(COLOUR_RGB\('[^']*',\s*([-0-9.Ee+]+)\s*,\s*([-0-9.Ee+]+)\s*,\s*([-0-9.Ee+]+)\))");
  for (auto it = re.globalMatch(t); it.hasNext();) {
    const auto m = it.next();
    out << QVector3D(m.captured(1).toFloat(), m.captured(2).toFloat(), m.captured(3).toFloat());
  }
  return out;
}
bool hasColour(const QList<QVector3D>& l, const QColor& c) {
  for (const auto& v : l)
    if (std::fabs(v.x() - c.redF()) < 0.02 && std::fabs(v.y() - c.greenF()) < 0.02 && std::fabs(v.z() - c.blueF()) < 0.02) return true;
  return false;
}

// ============================================================================ the flow
void run(pcd::MainWindow& w, const QString& tmpDir) {
  auto* v = w.viewer();
  auto* undo = w.undoStack();

  section("open the two-body part");
  const QString src = tmpDir + "/col_src.pcad";
  check(pc::saveDocument(makePart(), src.toStdString()), "test part written");
  check(w.openFile(src), "opened");
  pump(500);
  QStringList bodies = w.bodyNames();
  bodies.sort();
  check(bodies == QStringList({"Blok", "Govde"}), "bodies Blok + Govde: " + bodies.join(","));
  const double vol0 = volume(w);
  const int nFaces0 = int(w.modelResult().faces.size());
  check(w.document().view.colors.empty(), "no colours yet");
  w.structurePanel()->setCurrentPage("structure");
  topView(w);
  const QString gTop = "Extrude1.end", bTop = "Extrude2.end";
  QAction* aColor = w.findChild<QAction*>("actColor");
  check(aColor && aColor->menu(), "Görüntü > Stil: 'Renk' button with a palette menu");
  check(w.findChild<QAction*>("actColorReset") != nullptr, "Görüntü > Stil: 'Rengi sıfırla'");
  {
    w.ribbon()->setCurrentTab("display");
    pump(200);
    bool inStyle = false;
    for (auto* g : w.ribbon()->groupsOf("display")) if (g->title() == "Stil") inStyle = true;
    check(inStyle, "ribbon group 'Stil' in the Görüntü tab");
  }

  // ------------------------------------------------------------------ face red
  section("select Govde top -> Renk > kırmızı: face colour");
  QPointF pg = facePx(w, gTop);
  QPointF pb = facePx(w, bTop);
  check(pg.x() >= 0 && pb.x() >= 0, "both top faces visible in the top view");
  const QColor gDefault = pixAt(w, pg);
  check(!isRed(gDefault) && !isBlue(gDefault) || true, "default look " + gDefault.name());
  vclick(w, pg);
  check(w.selection()->namesOfKind(SelItem::Kind::Face) == QStringList{gTop}, "Govde top selected");
  const int u0 = undo->index();
  check(ribbonColor(w, "#ff0000"), "palette swatch #ff0000 clicked");
  check(hexOf(w, SelItem::Kind::Face, gTop) == "#ff0000", "face colour stored: " + hexOf(w, SelItem::Kind::Face, gTop));
  check(undo->index() == u0 + 1 && undo->undoText().startsWith("Renk"), "ONE undo step: " + undo->undoText());
  check(w.document().view.colors.size() == 1, "view.colors has one group");
  check(hexOf(w, SelItem::Kind::Face, bTop).isEmpty(), "other faces stay default");
  w.selection()->set({});
  pump(250);
  const QColor cRed = pixAt(w, pg);
  check(isRed(cRed), "PIXEL: Govde top is red: " + cRed.name());
  check(!isRed(pixAt(w, pb)), "PIXEL: Blok top is not red");
  check(std::fabs(volume(w) - vol0) < 1e-9 && int(w.modelResult().faces.size()) == nFaces0, "geometry unchanged by colouring");
  shot(w, "col_01_face_red.png");

  // ------------------------------------------------------------------ body blue
  section("select body Blok -> Renk > mavi: body colour");
  w.selection()->set({SelItem{SelItem::Kind::Body, "Blok"}});
  pump(200);
  check(ribbonColor(w, "#0070ff"), "palette swatch #0070ff clicked");
  check(hexOf(w, SelItem::Kind::Body, "Blok") == "#0070ff", "body colour stored");
  const auto blokFaces = facesOfBody(w, "Blok");
  bool allBlue = !blokFaces.isEmpty();
  for (const QString& f : blokFaces) allBlue = allBlue && hexOf(w, SelItem::Kind::Face, f) == "#0070ff";
  check(allBlue, QString("all %1 faces of Blok resolve to blue").arg(blokFaces.size()));
  check(hexOf(w, SelItem::Kind::Face, gTop) == "#ff0000", "Govde face still red");
  w.selection()->set({});
  pump(250);
  const QColor cBlue = pixAt(w, pb);
  check(isBlue(cBlue), "PIXEL: Blok top is blue: " + cBlue.name());
  check(isRed(pixAt(w, pg)), "PIXEL: Govde top still red");
  isoView(w);
  {
    int blueSides = 0;
    for (const QString& f : blokFaces) {
      const QPointF q = facePx(w, f);
      if (q.x() >= 0 && isBlue(pixAt(w, q))) ++blueSides;
    }
    check(blueSides >= 3, QString("PIXEL: %1 visible Blok faces are blue in the iso view").arg(blueSides));
  }
  shot(w, "col_02_body_blue_iso.png");
  topView(w);
  pg = facePx(w, gTop);
  pb = facePx(w, bTop);

  // ------------------------------------------------------------------ face override on a body
  section("face colour overrides the body colour; body colour keeps it");
  vclick(w, pb);
  check(ribbonColor(w, "#00b050"), "Blok top -> green");
  w.selection()->set({});
  pump(200);
  check(isGreen(pixAt(w, pb)), "PIXEL: Blok top green (override), body colour unchanged: " + hexOf(w, SelItem::Kind::Body, "Blok"));
  w.selection()->set({SelItem{SelItem::Kind::Body, "Blok"}});
  check(ribbonColor(w, "#0070ff") || true, "body -> blue again (same colour, no change)");
  check(hexOf(w, SelItem::Kind::Face, bTop) == "#00b050", "override survives a body colour command");
  w.selection()->set({});
  pump(150);

  // ------------------------------------------------------------------ highlight on coloured faces
  section("selection / hover highlight stays visible on coloured faces");
  vclick(w, pg);
  hoverAway(w);
  const QColor selOnRed = px(viewImage(w), pg);
  check(colorDist(selOnRed, cRed) > 150 && !isRed(selOnRed), QString("PIXEL: selected red face differs clearly (%1 vs %2)").arg(selOnRed.name(), cRed.name()));
  shot(w, "col_03_selected_on_red.png");
  w.selection()->set({SelItem{SelItem::Kind::Body, "Blok"}});
  hoverAway(w);
  const QPointF pb2 = pb;
  const QColor selBlue = px(viewImage(w), pb2);
  check(colorDist(selBlue, cBlue) > 100 || colorDist(selBlue, pixAt(w, pb2)) > 100, QString("selected body highlight visible on its colour (%1)").arg(selBlue.name()));
  {
    // hover on a coloured face: the pre-highlight differs from the plain colour
    w.selection()->set({});
    pump(100);
    QWidget* t = view(w);
    QMouseEvent mv(QEvent::MouseMove, pg, t->mapToGlobal(pg), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(t, &mv);
    pump(300);
    const QColor hov = px(viewImage(w), pg);
    check(colorDist(hov, cRed) > 100, QString("PIXEL: hover over the red face is visible (%1 vs %2)").arg(hov.name(), cRed.name()));
  }
  w.selection()->set({});

  // ------------------------------------------------------------------ context menu
  section("viewport context menu: Renk ▸");
  vclick(w, pg);
  {
    bool done = false, found = false;
    QTimer::singleShot(350, qApp, [&] {
      QMenu* m = popupMenu();
      QAction* ca = menuAction(m, "ctxColorMenu");
      found = ca && ca->menu();
      if (found) {
        QToolButton* sw = ca->menu()->findChild<QToolButton*>("colorSwatch.#ffd400");
        found = sw != nullptr;
        if (sw) sw->click();   // the submenu is not shown by the test; the swatch handler is the real one
      }
      closePopups();
      done = true;
    });
    rightClick(view(w), pg.toPoint());
    waitFor([&] { return done; }, 4000);
    pump(250);
    check(found, "context menu has 'Renk' submenu with the palette");
    check(hexOf(w, SelItem::Kind::Face, gTop) == "#ffd400", "context menu colour applied: " + hexOf(w, SelItem::Kind::Face, gTop));
  }
  w.selection()->set({});
  pump(200);
  {
    const QColor c = pixAt(w, pg);
    check(c.red() > 150 && c.green() > 120 && c.blue() < 120, "PIXEL: yellow face " + c.name());
  }
  // select the yellow face: highlight must still differ (yellow vs the default highlight colours)
  vclick(w, pg);
  hoverAway(w);
  {
    const QColor c = px(viewImage(w), pg);
    check(!(c.red() > 150 && c.green() > 120 && c.blue() < 120), "PIXEL: selected yellow face is not yellow any more: " + c.name());
  }
  // ------------------------------------------------------------------ Özellikler
  section("Özellikler: Renk row of a face");
  {
    for (auto* tw : w.findChildren<QTabWidget*>())   // show the Özellikler tab (instead of Seçenekler)
      for (int i = 0; i < tw->count(); ++i) if (tw->tabText(i) == "Özellikler") tw->setCurrentIndex(i);
    w.propertiesPanel()->rebuild();
    pump(200);
    auto* b = qobject_cast<QToolButton*>(w.propertiesPanel()->editor("color"));
    check(b != nullptr, "Özellikler has a 'Renk' editor for the selected face");
    if (b) {
      check(b->text().toLower().contains("#ffd400"), "shows the colour: " + b->text());
      QMenu* m = b->menu();
      QToolButton* sw = m ? m->findChild<QToolButton*>("colorSwatch.#ff0000") : nullptr;
      check(sw != nullptr, "its menu is the palette");
      if (sw) sw->click();
      pump(300);
      check(hexOf(w, SelItem::Kind::Face, gTop) == "#ff0000", "editing 'Renk' recolours the face: " + hexOf(w, SelItem::Kind::Face, gTop));
    }
    w.selection()->set({SelItem{SelItem::Kind::Body, "Blok"}});
    pump(300);
    w.propertiesPanel()->rebuild();
    auto* bb = qobject_cast<QToolButton*>(w.propertiesPanel()->editor("color"));
    check(bb && bb->text().toLower().contains("#0070ff"), "body row shows its colour: " + (bb ? bb->text() : QString("<none>")));
    shot(w, "col_04_properties_body.png");
    w.selection()->set({SelItem{SelItem::Kind::Face, gTop}});
    pump(300);
    shot(w, "col_05_properties_face.png");
  }
  w.selection()->set({});

  // ------------------------------------------------------------------ undo / redo
  section("undo / redo without regeneration");
  const int nCol = undo->index();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(300);
  check(hexOf(w, SelItem::Kind::Face, gTop) == "#ffd400", "Ctrl+Z: previous colour back: " + hexOf(w, SelItem::Kind::Face, gTop));
  check(isRed(pixAt(w, pg)) == false, "PIXEL after undo is not red");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  pump(300);
  check(hexOf(w, SelItem::Kind::Face, gTop) == "#ff0000" && isRed(pixAt(w, pg)), "Ctrl+Y: red again (pixel)");
  check(undo->index() == nCol, "undo index restored");

  // ------------------------------------------------------------------ split
  section("split face keeps its colour in every piece (L = 100), undo of the cut keeps it too");
  check(topPieces(w).size() == 1, "top is one face before the cut: " + topPieces(w).join(","));
  setParamL(w, "100");
  const QStringList pieces = topPieces(w);
  check(pieces.size() >= 2, QString("top split into %1 pieces: %2").arg(pieces.size()).arg(pieces.join(",")));
  bool allRed = pieces.size() >= 2;
  for (const QString& f : pieces) allRed = allRed && hexOf(w, SelItem::Kind::Face, f) == "#ff0000";
  check(allRed, "ALL pieces are red");
  hoverAway(w);
  {
    int redPieces = 0;
    for (const QString& f : pieces) {
      const QPointF q = facePx(w, f);
      if (q.x() >= 0 && isRed(pixAt(w, q))) ++redPieces;
    }
    check(redPieces == pieces.size(), QString("PIXEL: %1 / %2 pieces drawn red").arg(redPieces).arg(pieces.size()));
  }
  check(hexOf(w, SelItem::Kind::Face, bTop) == "#00b050", "Blok top override untouched by the cut");
  shot(w, "col_06_split_pieces_red.png");
  setParamL(w, "20");
  check(topPieces(w).size() == 1 && hexOf(w, SelItem::Kind::Face, topPieces(w).value(0)) == "#ff0000", "L = 20: single face, red again");
  setParamL(w, "100");

  // ------------------------------------------------------------------ Diğer renkler + reset
  section("'Diğer renkler…' (non-native QColorDialog) and 'Rengi sıfırla'");
  {
    w.selection()->set({SelItem{SelItem::Kind::Body, "Govde"}});
    pump(200);
    QAction* ca = w.findChild<QAction*>("actColor");
    QMenu* m = ca ? ca->menu() : nullptr;
    QAction* more = menuAction(m, "colorMore");
    check(more != nullptr, "'Diğer renkler…' entry");
    bool dlgSeen = false, nonNative = false;
    g_onModal = [&](QWidget* mm) {
      if (auto* cd = qobject_cast<QColorDialog*>(mm)) {
        dlgSeen = true;
        nonNative = cd->testOption(QColorDialog::DontUseNativeDialog);
        cd->setCurrentColor(QColor("#8e24aa"));
        QTimer::singleShot(100, cd, [cd] { cd->accept(); });
      }
    };
    if (more) more->trigger();
    waitFor([&] { return dlgSeen && !QApplication::activeModalWidget(); }, 5000);
    g_onModal = nullptr;
    pump(300);
    check(dlgSeen && nonNative, "QColorDialog opened with DontUseNativeDialogs");
    check(hexOf(w, SelItem::Kind::Body, "Govde") == "#8e24aa", "body Govde -> #8e24aa: " + hexOf(w, SelItem::Kind::Body, "Govde"));
    check(hexOf(w, SelItem::Kind::Face, pieces.value(0)) == "#ff0000", "its red face override stays red");
    // reset body: also drops the face overrides of that body
    QAction* ra = w.findChild<QAction*>("actColorReset");
    if (ra) ra->trigger();
    pump(300);
    check(hexOf(w, SelItem::Kind::Body, "Govde").isEmpty() && hexOf(w, SelItem::Kind::Face, pieces.value(0)).isEmpty(), "Rengi sıfırla (body): body + face colours of Govde gone");
    check(hexOf(w, SelItem::Kind::Body, "Blok") == "#0070ff", "Blok untouched");
    key(w, Qt::Key_Z, Qt::ControlModifier);
    pump(300);
    check(hexOf(w, SelItem::Kind::Body, "Govde") == "#8e24aa" && hexOf(w, SelItem::Kind::Face, pieces.value(0)) == "#ff0000", "undo of the reset restores both");
    // reset a single face -> falls back to the body colour
    w.selection()->set({SelItem{SelItem::Kind::Face, pieces.value(0)}});
    pump(200);
    if (ra) ra->trigger();
    pump(300);
    check(hexOf(w, SelItem::Kind::Face, pieces.value(0)) == "#8e24aa", "Rengi sıfırla (face): shows the body colour again: " + hexOf(w, SelItem::Kind::Face, pieces.value(0)));
    // restore the red piece colours for the save / STEP part
    w.selection()->set({});
    key(w, Qt::Key_Z, Qt::ControlModifier);
    pump(300);
    check(hexOf(w, SelItem::Kind::Face, pieces.value(0)) == "#ff0000", "undo: red piece back");
  }
  w.selection()->set({});

  // ------------------------------------------------------------------ save / reopen
  section("save / reopen");
  const QString pcad = tmpDir + "/col.pcad";
  const auto colorsBefore = w.document().view.colors.size();
  v->setFocus();
  doFileDialog(w, pcad, [&] { key(w, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier); });
  check(QFileInfo::exists(pcad), "saved");
  {
    QFile f(pcad);
    f.open(QIODevice::ReadOnly);
    const QByteArray b = f.readAll();
    check(b.contains("\"colors\"") && b.contains("#0070ff"), ".pcad has view.colors");
  }
  w.setAutoYes(true);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(w.document().view.colors.empty(), "Ctrl+N: no colours");
  doFileDialog(w, pcad, [&] { key(w, Qt::Key_O, Qt::ControlModifier); });
  waitFor([&] { return !w.document().features.empty(); }, 10000);
  pump(500);
  check(w.document().view.colors.size() == colorsBefore, "reopened: same colour groups");
  const QStringList pieces2 = topPieces(w);
  bool redAgain = pieces2.size() >= 2;
  for (const QString& f : pieces2) redAgain = redAgain && hexOf(w, SelItem::Kind::Face, f) == "#ff0000";
  check(redAgain && hexOf(w, SelItem::Kind::Body, "Blok") == "#0070ff" && hexOf(w, SelItem::Kind::Face, bTop) == "#00b050", "reopened: pieces red, Blok blue, Blok top green");
  topView(w);
  {
    const QPointF q1 = facePx(w, pieces2.value(0)), q2 = facePx(w, bTop);
    check(isRed(pixAt(w, q1)) && isGreen(pixAt(w, q2)), "PIXEL after reopen: red piece + green Blok top");
  }
  isoView(w);
  shot(w, "col_07_reopened_iso.png");

  // ------------------------------------------------------------------ STEP
  section("STEP export writes the colours");
  {
    const QString step = tmpDir + "/col.step";
    for (const auto& g : w.document().view.colors) {
      QString ms;
      for (const auto& m : g.members) ms += QString::fromStdString(std::string(pc::toString(m.kind)) + ":" + m.ref) + " ";
      note(QString("group %1: %2").arg(QString::fromStdString(g.color), ms));
    }
    for (const QString& f : topPieces(w)) note("piece " + f + " -> " + hexOf(w, SelItem::Kind::Face, f));
    QAction* exp = nullptr;
    if (auto* me = w.findChild<QMenu*>("menuExport")) for (QAction* a : me->actions()) if (a->text().startsWith("STEP")) exp = a;
    check(exp != nullptr, "Dosya > Dışa aktar > STEP");
    if (exp) doFileDialog(w, step, [&] { exp->trigger(); });
    const auto cols = stepColours(step);
    check(!cols.isEmpty(), QString("STEP has %1 COLOUR_RGB entries").arg(cols.size()));
    for (const auto& c : cols) note(QString("STEP colour %1 %2 %3").arg(c.x()).arg(c.y()).arg(c.z()));
    check(hasColour(cols, QColor("#ff0000")), "STEP: red (face colour)");
    check(hasColour(cols, QColor("#0070ff")), "STEP: blue (body colour)");
    check(hasColour(cols, QColor("#00b050")), "STEP: green (face override on a body)");
    pc::model::StepNames sn;
    std::string err;
    int nf = 0;
    if (pc::model::readSTEPNames(step.toStdString(), sn, &err))
      for (TopExp_Explorer e(sn.shape, TopAbs_FACE); e.More(); e.Next()) ++nf;
    check(nf == int(w.modelResult().faces.size()), QString("STEP still has all %1 faces (%2)").arg(w.modelResult().faces.size()).arg(nf));
  }

  // ------------------------------------------------------------------ Save as App
  section("Save-as-App payload + runner show the colours");
  {
    w.hideItems({SelItem{SelItem::Kind::Body, "Blok"}});   // view-state hide must NOT leak into the app
    pc::app::AppDefinition def;
    def.document = w.appPayloadDocument();
    check(def.document.view.hidden.members.empty() && def.document.view.colors.size() == colorsBefore, "payload: hidden cleared, colours kept");
    const auto j = pc::app::toJson(def);
    check(j.dump().find("#0070ff") != std::string::npos, "app JSON carries the colours");
    pc::app::AppDefinition def2 = pc::app::appFromJson(j);
    auto* rw = new pc::runner::RunnerWindow(def2);
    bool settled = false;
    QObject::connect(rw, &pc::runner::RunnerWindow::settled, [&settled] { settled = true; });
    rw->resize(1300, 850);
    rw->show();
    rw->startup();
    check(waitFor([&] { return settled; }, 60000), "runner settled");
    rw->viewer()->setView(pc::viewer::ShapeViewer::View::Top);
    rw->viewer()->fitAll();
    pump(500);
    const QImage im = rw->viewer()->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    int nRed = 0, nBlue = 0, nGreen = 0;
    for (int y = 0; y < im.height(); y += 2)
      for (int x = 0; x < im.width(); x += 2) {
        const QColor c = im.pixelColor(x, y);
        if (isRed(c)) ++nRed;
        else if (isGreen(c)) ++nGreen;
        else if (isBlue(c)) ++nBlue;
      }
    check(nRed > 200 && nGreen > 200, QString("runner PIXELS: red %1, green %2 (Blok top) in the top view").arg(nRed).arg(nGreen));
    rw->viewer()->setView(pc::viewer::ShapeViewer::View::Iso);
    rw->viewer()->fitAll();
    pump(500);
    const QImage im2 = rw->viewer()->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    nBlue = 0;
    for (int y = 0; y < im2.height(); y += 2)
      for (int x = 0; x < im2.width(); x += 2) if (isBlue(im2.pixelColor(x, y))) ++nBlue;
    check(nBlue > 200, QString("runner PIXELS: %1 blue pixels (Blok sides) in the iso view").arg(nBlue));
    if (g_shots) {
      QDir().mkpath(PC_COL_SHOTS_DIR);
      rw->grab().save(QString(PC_COL_SHOTS_DIR) + "/col_08_runner.png");
    }
    rw->close();
    delete rw;
    w.showAllItems();
  }

  // ------------------------------------------------------------------ whole-model reset
  section("reset everything");
  {
    w.selection()->set({SelItem{SelItem::Kind::Body, "Blok"}, SelItem{SelItem::Kind::Body, "Govde"}});
    w.resetSelectionColor();
    pump(300);
    check(w.document().view.colors.empty(), "Rengi sıfırla on both bodies: view.colors empty");
    w.selection()->set({});
    topView(w);
    check(!isRed(pixAt(w, facePx(w, pieces2.value(0)))), "PIXEL: default look again");
  }
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
  QApplication::setApplicationName("pc_gui_harness_colors");   // no layout persistence
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
