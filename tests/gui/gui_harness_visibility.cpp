// Gizle / Göster (SpaceClaim Hide / Hide Others / Show All) — GUI harness on the REAL MainWindow.
//
// Part: body "Govde" 60x40x20 with a cut slot (symmetric length L across Y, L=20 -> a notch, L=100 -> the slot runs
// across and SPLITS the top face) and a floating plate "Kapak" (z 30..35) above it.
//   click Kapak top (top view) -> Ctrl+H -> face not drawn (pixel), click goes through to Kapak's bottom face ->
//   Ctrl+H -> click reaches Govde's top -> Yapı "Gizli yüzler" rows -> undo / redo (no regeneration) -> body hide from
//   the Yapı row + Ctrl+H -> Ctrl+Shift+H -> Alt+H (isolate a face) -> status-bar "visStatus" click = show all ->
//   viewport right click (Gövdeyi gizle, Tümünü göster) -> Saydam yap (ribbon) -> named selection 'Dis_duvar'
//   Gizle / Göster / Diğerlerini gizle -> hidden Govde top survives L=20 -> 100 (split: all pieces hidden) ->
//   volume / mfg / STEP (all faces) unchanged -> section view -> sketch on a partly hidden body -> save / reopen.
//
//   pc_gui_harness_visibility [--shots]     --shots writes docs/visibility/shots/*.png
//   Exit code = number of failed checks. Also run with QT_FONT_DPI=72.
#include "MainWindow.h"
#include "NamedSelectionPanel.h"
#include "Ribbon.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include "pc/ShapeViewer.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#ifndef PC_VIS_SHOTS_DIR
#define PC_VIS_SHOTS_DIR "docs/visibility/shots"
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
  QDir().mkpath(PC_VIS_SHOTS_DIR);
  const QString path = QString(PC_VIS_SHOTS_DIR) + "/" + name;
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
  d.name = "GizleGoster";
  pc::Parameter L;
  L.name = "L";
  L.expression = "20";
  L.kind = pc::ParamKind::Length;
  d.params.add(L);
  auto sketch = [&](const char* id, pc::SketchPlaneKind k, const char* offset, double x0, double y0, double x1, double y1) {
    pc::Feature f;
    f.id = id;
    f.type = pc::FeatureType::Sketch;
    pc::Sketch s = pc::Sketch::create();
    s.plane.kind = k;
    s.plane.offsetExpr = offset;
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
  sketch("Sketch1", pc::SketchPlaneKind::XY, "0", 0, 0, 60, 40);
  extrude("Extrude1", "Sketch1", "20", {{"bodyName", "Govde"}});
  sketch("Sketch2", pc::SketchPlaneKind::XY, "30", 0, 0, 60, 40);
  extrude("Extrude2", "Sketch2", "5", {{"bodyName", "Kapak"}});
  // XZ: u = +X, v = +Z; symmetric length L along Y from y = 0: L = 20 -> notch y 0..10, L = 100 -> across (split)
  sketch("Sketch3", pc::SketchPlaneKind::XZ, "0", 25, 10, 35, 30);
  extrude("Extrude3", "Sketch3", "L", {{"direction", "symmetric"}, {"op", "cut"}, {"targets", "Govde"}});
  return d;
}

// ============================================================================ the flow
void run(pcd::MainWindow& w, const QString& tmpDir) {
  auto* v = w.viewer();
  auto* undo = w.undoStack();
  auto* st = w.structurePanel();

  section("open the two-body part");
  const QString src = tmpDir + "/vis_src.pcad";
  check(pc::saveDocument(makePart(), src.toStdString()), "test part written");
  check(w.openFile(src), "opened " + src);
  pump(500);
  QStringList bodies = w.bodyNames();
  bodies.sort();
  check(bodies == QStringList({"Govde", "Kapak"}), "two bodies Govde + Kapak: " + bodies.join(","));
  const double vol0 = volume(w);
  check(std::fabs(vol0 - (60 * 40 * 20 - 10 * 10 * 10 + 60 * 40 * 5)) < 1e-3, QString("volume %1 = 47000 + 12000").arg(vol0));
  check(w.hiddenFaceNames().isEmpty() && visStatusText(w).isEmpty(), "nothing hidden, no status indicator");
  const int issues0 = w.issuesTable()->rowCount();
  const int nFaces0 = int(w.modelResult().faces.size());
  st->setCurrentPage("structure");
  key(w, Qt::Key_S);

  // ------------------------------------------------------------------ hide a face, click through
  section("select Kapak top -> Ctrl+H: not drawn, not pickable, click goes through");
  topView(w);
  const QString kTop = "Extrude2.end", kBot = "Extrude2.start";
  const QPointF p = facePx(w, kTop);
  check(p.x() >= 0, QString("Kapak top visible in the top view at (%1,%2)").arg(p.x()).arg(p.y()));
  const QImage im0 = viewImage(w);
  const QColor c0 = px(im0, p);
  vclick(w, p);
  check(w.selection()->namesOfKind(SelItem::Kind::Face) == QStringList{kTop}, "click selects Kapak top: " + w.selection()->namesOfKind(SelItem::Kind::Face).join(","));
  const int u0 = undo->index();
  const int regen0 = undo->count();
  v->setFocus();
  key(w, Qt::Key_H, Qt::ControlModifier);
  pump(200);
  check(w.faceHidden(kTop) && w.hiddenFaceNames() == QStringList{kTop}, "Ctrl+H hid the selected FACE only: " + w.hiddenFaceNames().join(","));
  check(w.bodyVisible("Kapak") && w.bodyVisible("Govde"), "bodies stay visible (context-sensitive Ctrl+H)");
  check(undo->index() == u0 + 1 && undo->undoText().startsWith("Gizle"), "one undo step: " + undo->undoText());
  check(!w.selection()->contains(SelItem{SelItem::Kind::Face, kTop}), "the hidden face left the selection");
  check(!v->displayedFaceNames().isEmpty(), "viewer still shows the model");
  check(pickFace(w, p) == kBot, "pick at that pixel now hits the face behind (Kapak bottom): " + pickFace(w, p));
  check(visStatusText(w) == "1 gizli yüz, 0 gizli gövde", "status bar: " + visStatusText(w));
  vclick(w, p);
  check(w.selection()->namesOfKind(SelItem::Kind::Face) == QStringList{kBot}, "a real click selects the inner face: " + w.selection()->namesOfKind(SelItem::Kind::Face).join(","));
  key(w, Qt::Key_H, Qt::ControlModifier);
  pump(200);
  const QString gTopHit = pickFace(w, p);
  check(w.faceHidden(kBot) && gTopHit.startsWith("Extrude1.end"), "Ctrl+H again: the click now reaches Govde's top: " + gTopHit);
  check(visStatusText(w) == "2 gizli yüz, 0 gizli gövde", "status bar: " + visStatusText(w));
  {
    const QColor c1 = px(viewImage(w), p);
    check(colorDist(c0, c1) > 40, QString("pixel there now shows Govde's colour (%1 -> %2): both plate faces are not drawn").arg(c0.name(), c1.name()));
  }
  check(undo->count() == regen0 + 2, QString("two hide steps on the undo stack (%1)").arg(undo->count()));
  // Yapı: Kapak > Gizli yüzler (2), unchecked rows
  QTreeWidgetItem* hf = st->hiddenFacesNode("Kapak");
  check(hf && hf->childCount() == 2 && hf->child(0)->checkState(0) == Qt::Unchecked && hf->text(0) == "Gizli yüzler (2)",
        "Yapı: Kapak > 'Gizli yüzler (2)' with unchecked rows");
  shot(w, "vis_01_faces_hidden_click_through.png");
  check(std::fabs(volume(w) - vol0) < 1e-6 && int(w.modelResult().faces.size()) == nFaces0, "volume / face count unchanged by hiding");
  check(w.document().features.size() == 6, "features unchanged");

  section("undo / redo (no regeneration)");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(200);
  check(!w.faceHidden(kBot) && w.faceHidden(kTop), "Ctrl+Z: Kapak bottom visible again, top still hidden");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  pump(200);
  check(w.faceHidden(kBot) && w.faceHidden(kTop), "Ctrl+Y: hidden again");

  section("Yapı: tick a hidden-face row = show it; right click > Göster");
  hf = st->hiddenFacesNode("Kapak");
  QTreeWidgetItem* rowBot = nullptr;
  for (int i = 0; hf && i < hf->childCount(); ++i) if (hf->child(i)->data(0, Qt::UserRole).toString() == kBot) rowBot = hf->child(i);
  check(rowBot != nullptr, "row for Kapak bottom");
  if (rowBot) {
    rowBot->setCheckState(0, Qt::Checked);   // = clicking its check box
    waitFor([&] { return !w.faceHidden(kBot); }, 2000);
    check(!w.faceHidden(kBot), "ticking the row shows the face");
  }
  hf = st->hiddenFacesNode("Kapak");
  check(hf && hf->childCount() == 1, "Gizli yüzler (1)");
  if (hf && hf->childCount()) {
    QTreeWidget* t = st->tree();
    t->scrollToItem(hf->child(0));
    const QRect r = t->visualItemRect(hf->child(0));
    check(rightClickChoose(t->viewport(), QPoint(r.left() + 30, r.center().y()), "structureContextMenu", "structShowFace"),
          "right click on the row > Göster");
    check(!w.faceHidden(kTop) && w.hiddenFaceNames().isEmpty(), "everything visible again");
    check(st->hiddenFacesNode("Kapak") == nullptr, "no 'Gizli yüzler' node any more");
  }

  // ------------------------------------------------------------------ bodies
  section("select a body in Yapı -> Ctrl+H hides the BODY");
  if (QTreeWidgetItem* r = bodyRow(w, "Kapak")) clickTreeItem(st->tree(), r);
  check(w.selection()->items() == QList<SelItem>{SelItem{SelItem::Kind::Body, "Kapak"}}, "Kapak selected");
  v->setFocus();
  key(w, Qt::Key_H, Qt::ControlModifier);
  pump(200);
  check(!w.bodyVisible("Kapak") && w.bodyVisible("Govde"), "Ctrl+H hid the body Kapak");
  check(bodyRow(w, "Kapak") && bodyRow(w, "Kapak")->checkState(0) == Qt::Unchecked, "Yapı check box unticked");
  check(visStatusText(w) == "0 gizli yüz, 1 gizli gövde", "status bar: " + visStatusText(w));
  check(pickFace(w, p).startsWith("Extrude1.end"), "top view: Govde's top is pickable through the hidden plate");
  shot(w, "vis_02_body_hidden.png");
  key(w, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
  pump(200);
  check(w.hiddenFaceNames().isEmpty() && visStatusText(w).isEmpty(), "Ctrl+Shift+H: Tümünü göster");

  section("Alt+H with a face selected: isolate the face (Diğerlerini gizle)");
  vclick(w, p);
  check(w.selection()->namesOfKind(SelItem::Kind::Face) == QStringList{kTop}, "Kapak top selected");
  key(w, Qt::Key_H, Qt::AltModifier);
  pump(200);
  check(!w.bodyVisible("Govde") && w.bodyVisible("Kapak"), "Govde hidden as a body");
  check(w.hiddenFacesOfBody("Kapak").size() == facesOfBody(w, "Kapak").size() - 1 && !w.faceHidden(kTop),
        QString("Kapak: every face but the top hidden (%1)").arg(w.hiddenFacesOfBody("Kapak").size()));
  isoView(w);
  shot(w, "vis_03_hide_others_face.png");
  check(visStatus(w) && visStatus(w)->isVisible(), "status bar indicator: " + visStatusText(w));
  if (auto* b = visStatus(w)) QTest::mouseClick(b, Qt::LeftButton);
  pump(250);
  check(w.hiddenFaceNames().isEmpty(), "clicking the status-bar indicator shows all");

  section("ribbon: Tasarım > Gövde / Görünürlük and Görüntü > Gizle / Göster");
  auto* aHide = w.findChild<QAction*>("actHideBody");
  auto* aOthers = w.findChild<QAction*>("actHideOthers");
  auto* aAll = w.findChild<QAction*>("actShowAll");
  auto* aTr = w.findChild<QAction*>("actTransparent");
  check(aHide && aOthers && aAll && aTr, "actions Gizle / Diğerlerini gizle / Tümünü göster / Saydam yap");
  check(aHide && aHide->shortcut() == QKeySequence("Ctrl+H") && aOthers->shortcut() == QKeySequence("Alt+H") &&
            aAll->shortcut() == QKeySequence("Ctrl+Shift+H"), "shortcuts Ctrl+H / Alt+H / Ctrl+Shift+H");
  w.ribbon()->setCurrentTab("display");
  pump(150);
  check(w.ribbon()->group("hideShow") && aHide && ribbonButton(w, aHide), "Görüntü > 'Gizle / Göster' group with Gizle");
  w.ribbon()->setCurrentTab("design");
  pump(150);
  check(ribbonButton(w, aTr) != nullptr && w.ribbon()->group("body"), "Tasarım > Gövde / Görünürlük has Saydam yap");

  section("Saydam yap (ribbon) on a body, undo");
  auto clearSel = [&] {
    v->setFocus();
    key(w, Qt::Key_Escape);
    if (!w.selection()->isEmpty()) w.selection()->set({});
    pump(150);
  };
  isoView(w);
  clearSel();
  const QPointF pkt = facePx(w, kTop);
  const QColor cOpaque = px(viewImage(w), pkt);
  if (QTreeWidgetItem* r = bodyRow(w, "Kapak")) clickTreeItem(st->tree(), r);
  if (auto* b = ribbonButton(w, aTr)) QTest::mouseClick(b, Qt::LeftButton);
  pump(250);
  check(w.faceTransparent(kTop) && !w.faceHidden(kTop), "Kapak transparent (still visible, pickable)");
  check(pickFace(w, facePx(w, kTop)) == kTop, "transparent face stays pickable");
  clearSel();
  const QColor cTrans = px(viewImage(w), pkt);
  check(colorDist(cOpaque, cTrans) > 20, QString("pixel on the plate changes when transparent (%1 -> %2)").arg(cOpaque.name(), cTrans.name()));
  shot(w, "vis_04_transparent.png");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  pump(200);
  check(!w.faceTransparent(kTop), "Ctrl+Z: opaque again");

  // ------------------------------------------------------------------ viewport context menu
  section("viewport right click: Gövdeyi gizle / Tümünü göster");
  isoView(w);
  QPointF pk = facePx(w, kTop);
  bool hasAll = false;
  check(rightClickChoose(v, pk.toPoint(), "viewContextMenu", "ctxHideBody", false, [&] {
          QMenu* m = popupMenu("viewContextMenu");
          hasAll = m && menuAction(m, "ctxHide") && menuAction(m, "ctxHideOthers") && menuAction(m, "ctxTransparent");
          shot(w, "vis_05_context_menu.png", true);
        }), "right click on Kapak > 'Gövdeyi gizle: Kapak'");
  check(hasAll, "menu has Gizle / Diğerlerini gizle / Gövdeyi gizle / Saydam yap");
  check(!w.bodyVisible("Kapak"), "Kapak hidden from the context menu");
  const QString gSide = facesAt(w, 0, -1, 0, 1, 0.0).value(0);
  QPointF pg = facePx(w, gSide);
  check(pg.x() >= 0, "Govde front face visible: " + gSide);
  check(rightClickChoose(v, pg.toPoint(), "viewContextMenu", "ctxShowAll"), "right click > 'Tümünü göster'");
  check(w.bodyVisible("Kapak") && w.hiddenFaceNames().isEmpty(), "all shown");
  // face via the menu: right click on Govde's front face > Gizle
  pg = facePx(w, gSide);
  check(rightClickChoose(v, pg.toPoint(), "viewContextMenu", "ctxHide"), "right click on a face > Gizle");
  check(w.faceHidden(gSide) && w.hiddenFaceNames().size() == 1, "the face under the cursor is hidden");
  QPointF pt = facePx(w, facesAt(w, 0, 0, 1, 2, 20.0).value(0));
  check(rightClickChoose(v, pt.toPoint(), "viewContextMenu", "ctxShowBodyFaces"), "right click on the same body > 'Bu gövdenin gizli yüzlerini göster (1)'");
  check(w.hiddenFaceNames().isEmpty(), "shown");

  // ------------------------------------------------------------------ named selection
  section("named selection 'Dis_duvar': Gizle / Göster / Diğerlerini gizle");
  if (QTreeWidgetItem* r = bodyRow(w, "Kapak")) clickTreeItem(st->tree(), r);
  v->setFocus();
  key(w, Qt::Key_G, Qt::ControlModifier);
  QLineEdit* le = nullptr;
  waitFor([&] {
    for (QLineEdit* e : w.namedSelectionPanel()->tree()->viewport()->findChildren<QLineEdit*>()) if (e->isVisible()) le = e;
    return le != nullptr;
  }, 2000);
  if (le) { le->selectAll(); QTest::keyClicks(le, "Dis_duvar"); QTest::keyClick(le, Qt::Key_Return); }
  waitFor([&] { return w.namedSelectionManager().find("Dis_duvar") != nullptr; }, 2000);
  pump(200);
  check(w.namedSelectionManager().find("Dis_duvar") != nullptr, "named selection Dis_duvar (body Kapak)");
  auto nsMenu = [&](const QString& act) {
    QTreeWidgetItem* it = nullptr;
    auto* f = w.namedSelectionPanel()->folder();
    for (int i = 0; f && i < f->childCount(); ++i) if (f->child(i)->text(0) == "Dis_duvar") it = f->child(i);
    if (!it) return false;
    st->setCurrentPage("groups");
    pump(100);
    QTreeWidget* t = w.namedSelectionPanel()->tree();
    t->scrollToItem(it);
    const QRect r = t->visualItemRect(it);
    return rightClickChoose(t->viewport(), QPoint(r.left() + 40, r.center().y()), "nsContextMenu", act);
  };
  check(nsMenu("ns.hide"), "Adlandırılmış Seçimler > Dis_duvar > Gizle");
  check(!w.bodyVisible("Kapak") && w.bodyVisible("Govde"), "Dis_duvar hidden (see inside)");
  check(nsMenu("ns.show"), "> Göster");
  check(w.bodyVisible("Kapak") && w.hiddenFaceNames().isEmpty(), "Dis_duvar shown again");
  check(nsMenu("ns.hideOthers"), "> Diğerlerini gizle");
  check(w.bodyVisible("Kapak") && !w.bodyVisible("Govde"), "only Dis_duvar visible");
  key(w, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
  pump(200);

  // ------------------------------------------------------------------ split by a parameter change
  section("hidden Govde top survives L = 20 -> 100 (the slot splits it)");
  if (QTreeWidgetItem* r = bodyRow(w, "Kapak")) clickTreeItem(st->tree(), r);
  v->setFocus();
  key(w, Qt::Key_H, Qt::ControlModifier);   // hide the plate to reach Govde's top
  pump(200);
  isoView(w);
  QStringList tops = facesAt(w, 0, 0, 1, 2, 20.0);
  check(tops.size() == 1, "Govde top is one face before: " + tops.join(","));
  key(w, Qt::Key_S);
  vclick(w, facePx(w, tops.value(0)));
  check(w.selection()->namesOfKind(SelItem::Kind::Face) == QStringList{tops.value(0)}, "Govde top selected");
  key(w, Qt::Key_H, Qt::ControlModifier);
  pump(200);
  check(w.faceHidden(tops.value(0)), "Govde top hidden");
  const double volL20 = volume(w);
  {
    st->setCurrentPage("groups");
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
    check(ce != nullptr, "L cell editor open");
    if (ce) { ce->selectAll(); QTest::keyClicks(ce, "100"); QTest::keyClick(ce, Qt::Key_Return); }
    waitFor([&] { return pexpr(w, "L") == "100"; }, 6000);
    pump(400);
    st->setCurrentPage("structure");
  }
  check(pexpr(w, "L") == "100", "L = 100");
  tops = facesAt(w, 0, 0, 1, 2, 20.0);
  bool allHidden = tops.size() >= 2;
  for (const QString& f : tops) allHidden = allHidden && w.faceHidden(f);
  check(allHidden, QString("the top is split into %1 pieces, ALL hidden: %2").arg(tops.size()).arg(tops.join(",")));
  check(std::fabs(volume(w) - (volL20 - 10 * 10 * 30)) < 1e-3, QString("geometry did change (slot across): %1").arg(volume(w)));
  isoView(w);
  shot(w, "vis_06_split_face_stays_hidden.png");
  const QStringList hiddenAfterSplit = w.hiddenFaceNames();

  section("visibility does not change volume / mfg / STEP");
  const double volHidden = volume(w);
  const int issuesHidden = w.issuesTable()->rowCount();
  key(w, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
  pump(300);
  check(std::fabs(volume(w) - volHidden) < 1e-9, "volume identical with / without hidden items");
  check(w.issuesTable()->rowCount() == issuesHidden, QString("mfg issue rows identical (%1)").arg(issuesHidden));
  (void)issues0;
  key(w, Qt::Key_Z, Qt::ControlModifier);   // undo Tümünü göster: hidden again
  pump(300);
  check(w.hiddenFaceNames() == hiddenAfterSplit, "Ctrl+Z restores the hidden set");
  const QString step = tmpDir + "/vis.step";
  bool fileDlg = false;
  g_onModal = [&](QWidget* mm) { if (qobject_cast<QFileDialog*>(mm)) { fileDlg = true; acceptFileDialog(mm, step); } };
  QAction* exp = nullptr;
  if (auto* me = w.findChild<QMenu*>("menuExport")) for (QAction* a : me->actions()) if (a->text().startsWith("STEP")) exp = a;
  check(exp != nullptr, "Dosya > Dışa aktar > STEP");
  if (exp) exp->trigger();
  waitFor([&] { return QFileInfo::exists(step) && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  {
    pc::model::StepNames sn;
    std::string err;
    int nf = 0;
    if (QFileInfo::exists(step) && pc::model::readSTEPNames(step.toStdString(), sn, &err))
      for (TopExp_Explorer e(sn.shape, TopAbs_FACE); e.More(); e.Next()) ++nf;
    check(fileDlg && nf == int(w.modelResult().faces.size()),
          QString("STEP export contains ALL %1 faces (%2 read back) while %3 are hidden").arg(w.modelResult().faces.size()).arg(nf).arg(w.hiddenFaceNames().size()));
  }

  section("section view and sketch mode with hidden items");
  key(w, Qt::Key_X);
  pump(300);
  check(w.sectionControls()->sectionOn(), "X: section view on");
  check(w.hiddenFaceNames() == hiddenAfterSplit, "hidden faces stay hidden in the section view");
  shot(w, "vis_07_section_with_hidden.png");
  key(w, Qt::Key_X);
  pump(200);
  if (w.sectionControls()->sectionOn()) w.sectionControls()->setSectionOn(false);
  // sketch on Govde's front face while its top pieces are hidden
  isoView(w);
  key(w, Qt::Key_S);
  const QString front = facesAt(w, 0, -1, 0, 1, 0.0).value(0);
  QPointF pf = facePx(w, front);
  vclick(w, pf);
  key(w, Qt::Key_K);
  pump(300);
  check(w.inSketchMode(), "K on a face of the partly hidden body: sketch mode");
  check(w.hiddenFaceNames() == hiddenAfterSplit, "hidden faces unchanged while sketching");
  shot(w, "vis_08_sketch_on_partly_hidden_body.png");
  w.finishSketchPublic(false);
  pump(300);
  check(!w.inSketchMode() && w.hiddenFaceNames() == hiddenAfterSplit, "back in 3D, still hidden");

  // ------------------------------------------------------------------ save / reopen
  section("save / reopen restores the view state");
  const QString pcad = tmpDir + "/vis.pcad";
  fileDlg = false;
  g_onModal = [&](QWidget* mm) { if (qobject_cast<QFileDialog*>(mm)) { fileDlg = true; acceptFileDialog(mm, pcad); } };
  v->setFocus();
  key(w, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);   // Farklı Kaydet
  waitFor([&] { return QFileInfo::exists(pcad) && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  check(fileDlg && QFileInfo::exists(pcad), "saved " + pcad);
  {
    QFile f(pcad);
    f.open(QIODevice::ReadOnly);
    check(f.readAll().contains("\"view\""), ".pcad has a \"view\" section");
  }
  w.setAutoYes(true);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(w.hiddenFaceNames().isEmpty() && visStatusText(w).isEmpty(), "Ctrl+N: nothing hidden");
  fileDlg = false;
  g_onModal = [&](QWidget* mm) { if (qobject_cast<QFileDialog*>(mm)) { fileDlg = true; acceptFileDialog(mm, pcad); } };
  key(w, Qt::Key_O, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty() && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  pump(400);
  check(fileDlg && w.hiddenFaceNames() == hiddenAfterSplit, "reopened: same hidden faces " + w.hiddenFaceNames().join(","));
  check(!w.bodyVisible("Kapak"), "reopened: Kapak hidden");
  check(visStatusText(w).contains("gizli gövde"), "status bar after reopen: " + visStatusText(w));
  isoView(w);
  shot(w, "vis_09_reopened.png");
  key(w, Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
  pump(200);
  check(w.hiddenFaceNames().isEmpty(), "Tümünü göster after reopen");
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
  QApplication::setApplicationName("pc_gui_harness_visibility");   // no layout persistence
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
