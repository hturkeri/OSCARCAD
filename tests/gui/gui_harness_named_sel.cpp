// Named selections (Adlandırılmış seçimler) — GUI harness on the REAL MainWindow, driven like a SpaceClaim user:
//
//   Ctrl+N → K → R, origin, 60 Tab 40 Enter → D → P, region, 20 Enter (60×40×20 block)
//   → Ctrl+G right after the pull = parameter H (the pulled value; unchanged SpaceClaim "Create Group" behaviour)
//   → click the top face → Ctrl+G → inline editor "Grup1" → type Ust Enter (ONE undo step) → appears in Gruplar
//   → S, click the front side face → Ctrl+G → Yan
//   → sketch on the top face + rectangle + Pull = boss → sketch point + Delik (hole through all) → slot sketch across
//     the top face + Pull "Kes" (the top face is split: Ust = Bölündü, all pieces) → edge NS "Kenar" + Fillet on that
//     edge (Kenar = Kayıp, non-modal message, explicit "Yeniden bağla") → H = 30 in Gruplar → Ctrl+Z / Ctrl+Y →
//     save / reopen → Ctrl+G on a sketch dimension still makes a parameter → F2 rename + undo → right click on a face
//     "Adlandırılmış seçim oluştur" + Del + undo → Properties "Ad" → labels → Save-as-App section → tint.
//   After EVERY step: 'Ust' and 'Yan' exist, resolve to the geometrically right faces (normal / position), carry the
//   expected status, and a click on their Gruplar row selects + highlights exactly those faces in their colour.
//
//   pc_gui_harness_named_sel [--shots]     --shots writes docs/named_selections/shots/*.png
//   Exit code = number of failed checks. Also run with QT_FONT_DPI=72.
#include "MainWindow.h"
#include "NamedSelectionPanel.h"
#include "OptionsPanel.h"
#include "ParamSelectUi.h"
#include "PropertiesPanel.h"
#include "Ribbon.h"
#include "SketchEditor.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include "pc/ShapeViewer.h"
#include <BRepAdaptor_Curve.hxx>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <cmath>
#include <cstdio>
#include <functional>

#ifndef PC_NS_SHOTS_DIR
#define PC_NS_SHOTS_DIR "docs/named_selections/shots"
#endif

using pcd::SelItem;
using pc::model::NsStatus;

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

// ---- modal pump (message boxes answered Yes / OK; others -> g_onModal; every modal has a close deadline)
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
int g_unexpectedModals = 0;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (m->property("nsHandled").toBool()) return;
    if (g_onModal) { m->setProperty("nsHandled", true); g_onModal(m); return; }
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      m->setProperty("nsHandled", true);
      g_boxes << mb->text();
      ++g_unexpectedModals;
      note("message box: " + mb->text().left(120).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      m->setProperty("nsHandled", true);
      for (auto* b : m->findChildren<QPushButton*>()) if (b->text().remove('&') == "Yine de Uygula") { pch::pressLater(b, 50); return; }
      if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
      return;
    }
    ++g_unexpectedModals;
    note(QString("unexpected modal %1 '%2' -> closed").arg(m->metaObject()->className(), m->objectName()));
    m->setProperty("nsHandled", true);
    if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::reject);
  });
  t->start();
}

// ---- real input (same conventions as gui_harness_sc_e2e)
void ensureActive(pcd::MainWindow& w) {
  if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
  if (QApplication::activeWindow() != &w) { w.activateWindow(); QApplication::setActiveWindow(&w); pump(30); }
}
QWidget* inputTarget(pcd::MainWindow& w) { return w.inSketchMode() ? static_cast<QWidget*>(w.sketchEditor()) : static_cast<QWidget*>(w.viewer()); }
QWidget* keyTarget(pcd::MainWindow& w) {
  ensureActive(w);
  QWidget* f = QApplication::focusWidget();
  return (f && w.isAncestorOf(f) && f->isVisible()) ? f : inputTarget(w);
}
void key(pcd::MainWindow& w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QTest::keyClick(keyTarget(w), Qt::Key(k), m);
  pump(40);
}
void type(pcd::MainWindow& w, const QString& s) {
  for (const QChar c : s) {
    QTest::keyClick(keyTarget(w), c.toLatin1());
    pump(15);
  }
  pump(30);
}
void vmove(pcd::MainWindow& w, const QPointF& p) {
  QWidget* t = inputTarget(w);
  QMouseEvent mv(QEvent::MouseMove, p, t->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(20);
}
void vclick(pcd::MainWindow& w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  ensureActive(w);
  QWidget* t = inputTarget(w);
  vmove(w, p);
  QTest::mouseClick(t, Qt::LeftButton, m, p.toPoint());
  pump(80);
}
// right click as a real mouse produces it: press / release + the context-menu event
void rightClick(QWidget* t, const QPoint& p) {
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
  pump(200);
  return true;
}
void closePopups() {
  for (int i = 0; i < 4 && QApplication::activePopupWidget(); ++i) { QApplication::activePopupWidget()->close(); pump(60); }
}
QString msg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }
void shot(pcd::MainWindow& w, const QString& name, bool screen = false) {
  if (!g_shots) return;
  pump(300);
  QDir().mkpath(PC_NS_SHOTS_DIR);
  const QString path = QString(PC_NS_SHOTS_DIR) + "/" + name;
  QPixmap pm;
  if (screen) {   // popup menus are separate windows: grab the screen area of the main window
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
int countFeatures(pcd::MainWindow& w, pc::FeatureType t) {
  int n = 0;
  for (const auto& f : w.document().features) if (f.type == t) ++n;
  return n;
}
const pc::Feature* lastFeature(pcd::MainWindow& w, pc::FeatureType t) {
  const auto& fs = w.document().features;
  for (auto it = fs.rbegin(); it != fs.rend(); ++it) if (it->type == t) return &*it;
  return nullptr;
}
QPointF facePx(pcd::MainWindow& w, const QString& face) {
  double c[3], n[3];
  if (!w.viewer()->faceCentroid(face, c, n)) return QPointF(-1, -1);
  const QPointF p0 = w.viewer()->worldToScreen(c);
  for (int r = 0; r <= 300; r += 5)
    for (int k = 0; k < (r == 0 ? 1 : 24); ++k) {
      const double a = k * M_PI / 12.0;
      const QPointF p = p0 + QPointF(std::cos(a), std::sin(a)) * r;
      if (w.viewer()->pickAt(p.toPoint()) == face) return p;
    }
  return QPointF(-1, -1);
}
QPointF edgePx(pcd::MainWindow& w, const QString& edge) {
  auto it = w.modelResult().edges.find(edge.toStdString());
  if (it == w.modelResult().edges.end()) return QPointF(-1, -1);
  BRepAdaptor_Curve c(it->second);
  for (double t : {0.5, 0.4, 0.6, 0.3, 0.7, 0.25, 0.75, 0.2, 0.8}) {
    const gp_Pnt P = c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter()));
    const double a[3] = {P.X(), P.Y(), P.Z()};
    const QPointF p = w.viewer()->worldToScreen(a);
    if (w.viewer()->pickAt(p.toPoint()) == edge) return p;
  }
  return QPointF(-1, -1);
}
QString findEdge(pcd::MainWindow& w, std::function<bool(const gp_Pnt&, const gp_Pnt&)> pred) {
  for (const auto& [n, e] : w.modelResult().edges) {
    try {
      BRepAdaptor_Curve c(e);
      if (c.GetType() != GeomAbs_Line) continue;
      if (pred(c.Value(c.FirstParameter()), c.Value(c.LastParameter()))) return QString::fromStdString(n);
    } catch (...) {}
  }
  return QString();
}
// a planar face of the model with the given outward normal (and a pixel where it can be clicked)
QString faceWithNormal(pcd::MainWindow& w, double nx, double ny, double nz, bool pickable = true) {
  for (const auto& [n, f] : w.modelResult().faces) {
    const auto s = pc::model::faceSignature(w.modelResult(), n);
    if (!s.valid || s.surface != "plane") continue;
    if (std::fabs(s.nx - nx) + std::fabs(s.ny - ny) + std::fabs(s.nz - nz) > 1e-6) continue;
    if (!pickable || facePx(w, QString::fromStdString(n)).x() >= 0) return QString::fromStdString(n);
  }
  return QString();
}
QStandardItemModel* paramModel(pcd::MainWindow& w) { return qobject_cast<QStandardItemModel*>(w.parameterTable()->model()); }
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = paramModel(w);
  for (int r = 0; m && r < m->rowCount(); ++r) if (m->item(r, 0) && m->item(r, 0)->text() == name) return r;
  return -1;
}
std::string pexpr(pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<missing>");
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

// ---- named-selection panel helpers
pcd::NamedSelectionPanel* panel(pcd::MainWindow& w) { return w.namedSelectionPanel(); }
QTreeWidgetItem* nsItem(pcd::MainWindow& w, const QString& name) {
  auto* f = panel(w)->folder();
  for (int i = 0; f && i < f->childCount(); ++i) if (f->child(i)->text(0) == name) return f->child(i);
  return nullptr;
}
void showGroups(pcd::MainWindow& w) {
  w.structurePanel()->setCurrentPage("groups");
  pump(80);
}
bool clickNs(pcd::MainWindow& w, const QString& name, Qt::MouseButton b = Qt::LeftButton) {
  showGroups(w);
  QTreeWidgetItem* it = nsItem(w, name);
  if (!it) return false;
  QTreeWidget* t = panel(w)->tree();
  t->scrollToItem(it);
  const QRect r = t->visualItemRect(it);
  const QPoint p(r.left() + 40, r.center().y());
  if (b == Qt::RightButton) rightClick(t->viewport(), p);
  else { QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, p); pump(120); }
  return true;
}
QLineEdit* inlineEditor(pcd::MainWindow& w) {
  for (QLineEdit* le : panel(w)->tree()->viewport()->findChildren<QLineEdit*>()) if (le->isVisible()) return le;
  return nullptr;
}
const pc::model::NsResolution* res(pcd::MainWindow& w, const QString& n) { return w.namedSelectionManager().find(n.toStdString()); }

// Ctrl+G with the current 3D selection, then type the name into the inline editor + Enter. Returns true if the
// selection `name` exists afterwards, created by ONE undo step.
bool nameByCtrlG(pcd::MainWindow& w, const QString& name, QString* defaultName = nullptr) {
  QUndoStack* u = w.undoStack();
  const int n0 = u->index();
  w.viewer()->setFocus();
  key(w, Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return inlineEditor(w) != nullptr; }, 2000);
  QLineEdit* le = inlineEditor(w);
  check(le != nullptr && QApplication::focusWidget() == le, "Ctrl+G: the new selection's name is in inline-edit mode (focused)");
  if (defaultName) *defaultName = le ? le->text() : QString();
  check(w.structurePanel()->currentPageKey() == "groups", "Ctrl+G switches to the Gruplar tab");
  if (!le) return false;
  check(le->selectedText() == le->text(), "default name '" + le->text() + "' is selected: typing replaces it");
  type(w, name);
  key(w, Qt::Key_Return);
  waitFor([&] { return res(w, name) != nullptr; }, 2000);
  pump(100);
  check(u->index() == n0 + 1, QString("create + typed name = ONE undo step (%1 -> %2): '%3'").arg(n0).arg(u->index()).arg(u->undoText()));
  return res(w, name) != nullptr;
}

// ---- the invariant checked after every step
double g_H = 20;
NsStatus g_ustStatus = NsStatus::Ok;
bool checkFace(pcd::MainWindow& w, const std::string& f, double nx, double ny, double nz, int axis, double coord, QString* why) {
  const auto s = pc::model::faceSignature(w.modelResult(), f);
  const double c[3] = {s.cx, s.cy, s.cz};
  const bool ok = s.valid && s.surface == "plane" && std::fabs(s.nx - nx) < 1e-6 && std::fabs(s.ny - ny) < 1e-6 &&
                  std::fabs(s.nz - nz) < 1e-6 && std::fabs(c[axis] - coord) < 1e-6;
  if (!ok && why) *why += QString(" %1:n=(%2,%3,%4) c=(%5,%6,%7)").arg(QString::fromStdString(f)).arg(s.nx).arg(s.ny).arg(s.nz).arg(s.cx).arg(s.cy).arg(s.cz);
  return ok;
}
void checkNames(pcd::MainWindow& w, const QString& step, bool clickBoth = true) {
  const auto* u = res(w, "Ust");
  const auto* y = res(w, "Yan");
  check(u && y, step + ": 'Ust' and 'Yan' exist");
  if (!u || !y) return;
  QString why;
  bool topOk = !u->faces.empty();
  for (const auto& f : u->faces) topOk = checkFace(w, f, 0, 0, 1, 2, g_H, &why) && topOk;
  check(topOk && u->status == g_ustStatus,
        QString("%1: Ust = %2 face(s), all planar +Z at z=%3, status %4 (expected %5)%6")
            .arg(step).arg(u->faces.size()).arg(g_H).arg(pcd::nsStatusText(u->status), pcd::nsStatusText(g_ustStatus), why));
  why.clear();
  bool sideOk = !y->faces.empty();
  for (const auto& f : y->faces) sideOk = checkFace(w, f, 0, -1, 0, 1, 0.0, &why) && sideOk;
  check(sideOk && y->status == NsStatus::Ok && y->faces.size() == 1,
        QString("%1: Yan = 1 face, planar -Y at y=0, status %2%3").arg(step, pcd::nsStatusText(y->status), why));
  // no other face of the model is +Z at z=H outside Ust (Ust covers EVERY piece of the top face)
  std::set<std::string> topNow;
  for (const auto& [n, f] : w.modelResult().faces) {
    (void)f;
    const auto s = pc::model::faceSignature(w.modelResult(), n);
    if (s.valid && s.surface == "plane" && s.nz > 1 - 1e-9 && std::fabs(s.cz - g_H) < 1e-6) topNow.insert(n);
  }
  check(topNow == std::set<std::string>(u->faces.begin(), u->faces.end()),
        QString("%1: Ust covers exactly the faces at the top level z=%2 (%3)").arg(step).arg(g_H).arg(topNow.size()));
  // the panel shows both, with their status
  QTreeWidgetItem* iu = nsItem(w, "Ust");
  QTreeWidgetItem* iy = nsItem(w, "Yan");
  check(iu && iy && iu->text(2) == pcd::nsStatusText(u->status) && iy->text(2) == pcd::nsStatusText(y->status),
        step + ": Gruplar > Adlandırılmış Seçimler lists Ust (" + (iu ? iu->text(2) : "-") + ") and Yan (" + (iy ? iy->text(2) : "-") + ")");
  if (!clickBoth) return;
  for (const QString n : {"Ust", "Yan"}) {
    clickNs(w, n);
    const auto* r = res(w, n);   // after the click: pending timers may have re-resolved (pointers are not stable)
    if (!r) { check(false, step + ": " + n + " vanished"); continue; }
    QStringList sel = w.selection()->namesOfKind(SelItem::Kind::Face);
    QStringList exp;
    for (const auto& f : r->faces) exp << QString::fromStdString(f);
    sel.sort();
    exp.sort();
    const pc::NamedSelection* ns = nullptr;
    for (const auto& s : w.document().namedSelections) if (s.name == n.toStdString()) ns = &s;
    QStringList hl = w.lastHighlightedFaces();
    hl.sort();
    check(sel == exp && hl == exp && ns && w.lastHighlightColor() == QColor(QString::fromStdString(ns->color)),
          QString("%1: click on %2 selects + highlights its %3 face(s) in %4").arg(step, n).arg(exp.size()).arg(ns ? QString::fromStdString(ns->color) : "?"));
  }
}

// ============================================================================ the flow
void run(pcd::MainWindow& w, const QString& tmpDir) {
  auto* tm = w.toolManager();
  auto* ed = w.sketchEditor();
  auto* v = w.viewer();
  auto* undo = w.undoStack();

  // ------------------------------------------------------------------ block
  section("block: K, R origin 60 Tab 40 Enter, D, P region 20 Enter");
  v->setFocus();
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(panel(w) && panel(w)->folder() && panel(w)->folder()->text(0) == "Adlandırılmış Seçimler (0)",
        "Gruplar has the 'Adlandırılmış Seçimler' folder (empty)");
  key(w, Qt::Key_K);
  check(w.inSketchMode(), "K: sketch on XY");
  key(w, Qt::Key_R);
  vclick(w, ed->sketchToScreen(0, 0));
  type(w, "60");
  key(w, Qt::Key_Tab);
  vmove(w, ed->sketchToScreen(30, 25));
  type(w, "40");
  key(w, Qt::Key_Return);
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  key(w, Qt::Key_H);
  pump(200);
  key(w, Qt::Key_P);
  vclick(w, facePx(w, "sketch:Sketch1:r0"));
  type(w, "20");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 5000);
  check(std::fabs(volume(w) - 48000) < 0.5, QString("60x40x20 block (%1 mm³)").arg(volume(w), 0, 'f', 1));

  // ------------------------------------------------------------------ Ctrl+G right after the pull = parameter
  section("Ctrl+G right after the pull: parameter H (Create Group of the pulled value)");
  bool npSeen = false;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pcd::NewParameterDialog")) { note(QString("modal %1").arg(m->metaObject()->className())); return; }
    npSeen = true;
    pch::typeInto(m->findChild<QLineEdit*>("npName"), "H", "npName");
    QTimer::singleShot(50, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  v->setFocus();
  key(w, Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return npSeen && !QApplication::activeModalWidget(); }, 6000);
  pump(300);
  g_onModal = nullptr;
  check(npSeen && pexpr(w, "H") == "20" && w.document().feature("Extrude1")->prop("distance") == "H",
        "Ctrl+G after the pull still makes the parameter H = 20 (Extrude1.distance = H)");
  check(w.document().namedSelections.empty(), "... and no named selection");

  // ------------------------------------------------------------------ Ust: top face + Ctrl+G (Pull still active)
  section("click the top face -> Ctrl+G -> type Ust Enter");
  check(tm->activeId() == "pull", "Pull tool still active (SpaceClaim keeps the tool)");
  const QPointF top = facePx(w, "Extrude1.end");
  vclick(w, top);
  check(w.selection()->items() == QList<SelItem>{SelItem{SelItem::Kind::Face, "Extrude1.end"}}, "top face Extrude1.end selected");
  QString def;
  const bool ust = nameByCtrlG(w, "Ust", &def);
  check(def == "Grup1", "default name Grup1 (SpaceClaim Group1): " + def);
  check(ust && res(w, "Ust")->faces == std::vector<std::string>{"Extrude1.end"}, "Ust = Extrude1.end");
  check(undo->undoText() == "Adlandırılmış seçim oluştur: Ust", "undo text: " + undo->undoText());
  check(nsItem(w, "Ust") && panel(w)->folder()->text(0) == "Adlandırılmış Seçimler (1)", "Ust appears in Gruplar > Adlandırılmış Seçimler (1)");
  check(nsItem(w, "Ust") && nsItem(w, "Ust")->text(1) == "1 yüz", "row shows '1 yüz'");
  check(w.parameterTable()->isVisible() && paramRow(w, "H") >= 0, "Parametreler section (H) still shown above");

  // ------------------------------------------------------------------ Yan: S, side face, Ctrl+G
  section("S, click the front side face -> Ctrl+G -> Yan");
  v->setFocus();
  key(w, Qt::Key_S);
  check(tm->activeId() == "select", "S: Seç");
  const QString side = faceWithNormal(w, 0, -1, 0);
  check(!side.isEmpty(), "front side face (-Y) is visible: " + side);
  vclick(w, facePx(w, side));
  check(w.selection()->items() == QList<SelItem>{SelItem{SelItem::Kind::Face, side}}, "side face selected");
  check(nameByCtrlG(w, "Yan", &def) && def == "Grup1", "Yan created (default was " + def + ")");
  g_H = 20;
  checkNames(w, "named");
  shot(w, "01_named_ust_yan.png");

  // hover + Properties
  v->setFocus();
  vmove(w, facePx(w, "Extrude1.end"));
  pump(150);
  check(w.hoverInfoText() == QString::fromUtf8("Yüz: Extrude1.end — Ad: Ust"), "hover status bar: '" + w.hoverInfoText() + "'");
  w.selection()->clear();
  vclick(w, facePx(w, "Extrude1.end"));
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
  pump(200);
  auto* pn = qobject_cast<QLineEdit*>(w.propertiesPanel()->editor("nsName"));
  check(pn && pn->text() == "Ust", "Özellikler: Adlandırılmış seçim > Ad = " + (pn ? pn->text() : QString("-")));
  shot(w, "02_properties_ad.png");
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(0);

  // ------------------------------------------------------------------ boss on the top face
  section("sketch on the top face, rectangle, Pull -> boss (new material on top)");
  v->setFocus();
  key(w, Qt::Key_Escape);
  w.selection()->clear();
  vclick(w, facePx(w, "Extrude1.end"));
  key(w, Qt::Key_K);
  check(w.inSketchMode() && ed->sketch().plane.faceRef == "Extrude1.end", "K: sketch on the top face");
  key(w, Qt::Key_R);
  vclick(w, ed->sketchToScreen(10, 10));
  type(w, "15");
  key(w, Qt::Key_Tab);
  vmove(w, ed->sketchToScreen(20, 25));
  type(w, "10");
  key(w, Qt::Key_Return);
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  key(w, Qt::Key_H);
  pump(150);
  key(w, Qt::Key_P);
  const std::string bossSketch = lastFeature(w, pc::FeatureType::Sketch)->id;
  const QPointF br = facePx(w, QString::fromStdString("sketch:" + bossSketch + ":r0"));
  check(br.x() >= 0, "boss region pickable");
  vclick(w, br);
  const double vb = volume(w);
  type(w, "10");
  key(w, Qt::Key_Return);
  waitFor([&] { return countFeatures(w, pc::FeatureType::Extrude) == 2; }, 5000);
  check(std::fabs(volume(w) - vb - 1500) < 1, QString("boss 15x10x10 added (%1 -> %2)").arg(vb, 0, 'f', 0).arg(volume(w), 0, 'f', 0));
  checkNames(w, "boss");

  // ------------------------------------------------------------------ hole through the top
  section("point on the top face + Delik through all");
  v->setFocus();
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  w.selection()->clear();
  vclick(w, facePx(w, "Extrude1.end"));
  key(w, Qt::Key_K);
  if (QAction* pa = tm->action("sketch.point")) {
    QToolButton* b = nullptr;
    for (auto* tb : w.ribbon()->findChildren<QToolButton*>()) if (tb->defaultAction() == pa && tb->isVisible()) b = tb;
    if (b) QTest::mouseClick(b, Qt::LeftButton);
    else pa->trigger();
    pump(100);
  }
  vclick(w, ed->sketchToScreen(45, 20));
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  const std::string holeSketch = lastFeature(w, pc::FeatureType::Sketch)->id;
  if (QAction* actHole = w.findChild<QAction*>("actHole")) {
    actHole->trigger();
    waitFor([&] { return w.featureDialog() != nullptr; }, 3000);
    if (auto* dlg = w.featureDialog()) {
      for (auto* cb : dlg->findChildren<QComboBox*>()) {
        const int i = cb->findData(QString::fromStdString(holeSketch));
        if (i >= 0) cb->setCurrentIndex(i);
      }
      for (auto* c : dlg->findChildren<QCheckBox*>()) if (c->text() == "Delik boydan boya") pch::tickCheckBox(c, true, "through");
      for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text().remove('&') == "Oluştur") { QTest::mouseClick(b, Qt::LeftButton); break; }
      waitFor([&] { return w.featureDialog() == nullptr; }, 5000);
      pump(300);
    }
  }
  check(lastFeature(w, pc::FeatureType::Hole) != nullptr, "Hole1 through the top face");
  // MODEL.md: a holed face is the same face -> Ok (not Split)
  checkNames(w, "hole (holed face stays Tamam)");

  // ------------------------------------------------------------------ slot across the top -> Split
  section("slot sketch across the whole top face + Pull 'Kes' 5 -> top face split in two");
  v->setFocus();
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  w.selection()->clear();
  vclick(w, facePx(w, "Extrude1.end"));
  key(w, Qt::Key_K);
  key(w, Qt::Key_R);
  vclick(w, ed->sketchToScreen(-5, 30));
  type(w, "70");
  key(w, Qt::Key_Tab);
  vmove(w, ed->sketchToScreen(30, 36));
  type(w, "4");
  key(w, Qt::Key_Return);
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  key(w, Qt::Key_H);
  pump(150);
  const std::string slotSketch = lastFeature(w, pc::FeatureType::Sketch)->id;
  key(w, Qt::Key_P);
  vclick(w, facePx(w, QString::fromStdString("sketch:" + slotSketch + ":r0")));
  tm->setOption("merge", "cut");
  note("Pull option merge=cut set through ToolManager::setOption (the Options combo is covered by gui_harness_sc_e2e)");
  v->setFocus();
  if (!w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, QString::fromStdString("sketch:" + slotSketch + ":r0")}))
    vclick(w, facePx(w, QString::fromStdString("sketch:" + slotSketch + ":r0")));
  const int modals0 = g_unexpectedModals;
  type(w, "5");
  key(w, Qt::Key_Return);
  waitFor([&] { return countFeatures(w, pc::FeatureType::Extrude) == 3; }, 5000);
  tm->setOption("merge", "auto");
  pump(100);
  g_ustStatus = NsStatus::Split;
  check(res(w, "Ust") && res(w, "Ust")->faces.size() == 2, QString("Ust now covers BOTH pieces (%1)").arg(res(w, "Ust") ? res(w, "Ust")->faces.size() : 0));
  check(msg(w).contains("'Ust'") && msg(w).contains(QString::fromUtf8("Bölündü")), "non-modal status message: " + msg(w).left(140));
  check(g_unexpectedModals == modals0 && !QApplication::activeModalWidget(), "no dialog for the status change");
  auto* nsl = w.statusBar()->findChild<QLabel*>("nsStatus");
  check(nsl && nsl->isVisible() && nsl->text().startsWith("Ad. seçim: 2"), "status bar indicator: " + (nsl ? nsl->text() : QString("-")));
  checkNames(w, "slot (split)");
  clickNs(w, "Ust");
  shot(w, "03_split_highlight.png");

  // ------------------------------------------------------------------ edge NS + fillet -> Kayıp, explicit re-bind
  section("edge named 'Kenar', Fillet on it -> Kenar Kayıp (non-modal), Yeniden bağla by explicit choice");
  key(w, Qt::Key_H);
  pump(150);
  v->setFocus();
  key(w, Qt::Key_S);
  const QString frontTop = findEdge(w, [](const gp_Pnt& a, const gp_Pnt& b) {
    return std::fabs(a.Z() - 20) < 1e-6 && std::fabs(b.Z() - 20) < 1e-6 && std::fabs(a.Y()) < 1e-6 && std::fabs(b.Y()) < 1e-6;
  });
  w.selection()->clear();
  const QPointF ep = edgePx(w, frontTop);
  check(!frontTop.isEmpty() && ep.x() >= 0, "front top edge pickable: " + frontTop);
  vclick(w, ep);
  check(w.selection()->items() == QList<SelItem>{SelItem{SelItem::Kind::Edge, frontTop}}, "edge selected");
  check(nameByCtrlG(w, "Kenar") && res(w, "Kenar")->edges == std::vector<std::string>{frontTop.toStdString()}, "Kenar = that edge");
  checkNames(w, "edge named");
  v->setFocus();
  key(w, Qt::Key_P);
  w.selection()->clear();
  vclick(w, edgePx(w, frontTop));
  const int modals1 = g_unexpectedModals;
  type(w, "3");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Fillet1") != nullptr; }, 5000);
  pump(100);
  check(w.document().feature("Fillet1") && w.document().feature("Fillet1")->prop("radius") == "3", "Fillet1 r=3 on the front top edge");
  check(res(w, "Kenar") && res(w, "Kenar")->status == NsStatus::Lost, "Kenar: " + (res(w, "Kenar") ? pcd::nsStatusText(res(w, "Kenar")->status) : QString("-")) +
                                                                            " — " + (res(w, "Kenar") ? QString::fromStdString(res(w, "Kenar")->diagnostic) : QString()));
  check(msg(w).contains("'Kenar'") && msg(w).contains(QString::fromUtf8("Kayıp")), "non-modal message: " + msg(w).left(140));
  check(g_unexpectedModals == modals1 && !QApplication::activeModalWidget(), "never a blocking dialog");
  check(nsItem(w, "Kenar") && nsItem(w, "Kenar")->text(2) == QString::fromUtf8("Kayıp"), "panel: Kenar Kayıp");
  checkNames(w, "fillet (Ust split, Yan trimmed)");
  {
    const auto s = pc::model::faceSignature(w.modelResult(), res(w, "Yan")->faces.front());
    check(std::fabs(s.cz - 8.5) < 1e-3, QString("Yan is the trimmed side face (centroid z %1 = (20-3)/2)").arg(s.cz));
  }
  // re-bind: select the back top edge, right click Kenar > Yeniden bağla > Üye 1 > Seçili ögeye bağla
  const QString backTop = findEdge(w, [](const gp_Pnt& a, const gp_Pnt& b) {
    return std::fabs(a.Z() - 20) < 1e-6 && std::fabs(b.Z() - 20) < 1e-6 && std::fabs(a.Y() - 40) < 1e-6 && std::fabs(b.Y() - 40) < 1e-6;
  });
  v->setFocus();
  key(w, Qt::Key_S);
  w.selection()->clear();
  const QPointF bp = edgePx(w, backTop);
  check(!backTop.isEmpty() && bp.x() >= 0, "back top edge pickable: " + backTop);
  vclick(w, bp);
  clickNs(w, "Kenar", Qt::RightButton);
  QMenu* cm = popupMenu("nsContextMenu");
  check(cm != nullptr, "right click on Kenar: context menu");
  QStringList texts;
  if (cm) for (QAction* a : cm->actions()) if (!a->isSeparator()) texts << a->text().section('\t', 0, 0);
  check(texts.contains("Yeniden adlandır") && texts.contains("Sil") && texts.contains("Seçimi değiştir") && texts.contains("Seçili ögeleri ekle") &&
            texts.contains("Seçili ögeleri çıkar") && texts.contains("Renk") && texts.contains("STEP'e aktar") && texts.contains("Yeniden bağla") &&
            texts.contains("Vurguyu göster"),
        "menu: " + texts.join(" | "));
  QAction* rbA = nullptr;
  if (cm) for (QAction* a : cm->actions()) if (a->menu() && a->menu()->objectName() == "ns.rebindMenu") rbA = a;
  check(rbA && rbA->isEnabled(), "'Yeniden bağla' enabled for the lost member");
  bool rebound = false;
  if (rbA) {
    clickMenuAction(cm, rbA);
    QMenu* rb = rbA->menu();
    pump(200);
    QAction* memberA = rb && !rb->actions().isEmpty() ? rb->actions().first() : nullptr;
    if (memberA && memberA->menu()) {
      clickMenuAction(rb, memberA);
      pump(200);
      QMenu* sub = memberA->menu();
      shot(w, "04_rebind_menu.png", true);
      QAction* selA = menuAction(sub, "ns.rebind.selection");
      check(selA && selA->text().contains(backTop), "member menu offers 'Seçili ögeye bağla: " + backTop + "'");
      rebound = clickMenuAction(sub, selA);
    }
  }
  closePopups();
  waitFor([&] { return res(w, "Kenar") && res(w, "Kenar")->status == NsStatus::Ok; }, 2000);
  check(rebound && res(w, "Kenar") && res(w, "Kenar")->status == NsStatus::Ok && res(w, "Kenar")->edges == std::vector<std::string>{backTop.toStdString()},
        "explicit re-bind: Kenar = back top edge, Tamam");
  checkNames(w, "re-bind");

  // ------------------------------------------------------------------ H = 30 in Gruplar
  section("change the first extrude depth H = 30 in Gruplar");
  showGroups(w);
  {
    auto* pv = w.parameterTable();
    const QModelIndex idx = pv->model()->index(paramRow(w, "H"), 1);
    pv->scrollTo(idx);
    const QRect r = pv->visualRect(idx);
    QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    QTest::mouseDClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    pump(150);
    QLineEdit* ce = pv->viewport()->findChild<QLineEdit*>();
    if (!ce) { pv->edit(idx); pump(100); ce = pv->viewport()->findChild<QLineEdit*>(); }
    check(ce != nullptr, "H cell editor open");
    if (ce) {
      ce->selectAll();
      QTest::keyClicks(ce, "30");
      QTest::keyClick(ce, Qt::Key_Return);
    }
    waitFor([&] { return pexpr(w, "H") == "30"; }, 5000);
    pump(300);
  }
  check(pexpr(w, "H") == "30", "H = 30");
  g_H = 30;
  checkNames(w, "H=30");
  check(std::fabs(pc::model::faceSignature(w.modelResult(), res(w, "Yan")->faces.front()).cz - 13.5) < 1e-3, "Yan follows (centroid z = 13.5)");

  // ------------------------------------------------------------------ undo / redo
  section("Ctrl+Z / Ctrl+Y");
  v->setFocus();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return pexpr(w, "H") == "20"; }, 4000);
  g_H = 20;
  checkNames(w, "undo (H=20)");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return pexpr(w, "H") == "30"; }, 4000);
  g_H = 30;
  checkNames(w, "redo (H=30)");

  // ------------------------------------------------------------------ save / reopen
  section("save / reopen");
  const QString pcad = tmpDir + "/named_sel.pcad";
  bool fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  v->setFocus();
  key(w, Qt::Key_S, Qt::ControlModifier);
  waitFor([&] { return QFileInfo::exists(pcad) && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  check(fileDlg && QFileInfo::exists(pcad), "saved " + pcad);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(w.document().namedSelections.empty() && panel(w)->folder()->childCount() == 0, "Ctrl+N: no named selections");
  fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  key(w, Qt::Key_O, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty() && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  pump(400);
  check(fileDlg && w.document().namedSelections.size() == 3, QString("reopened: %1 named selections").arg(w.document().namedSelections.size()));
  checkNames(w, "reopened");
  check(res(w, "Kenar") && res(w, "Kenar")->status == NsStatus::Ok, "reopened: Kenar Tamam");

  // ------------------------------------------------------------------ Ctrl+G on a sketch dimension = parameter
  section("Ctrl+G on a sketch dimension still makes a parameter (popup)");
  {
    w.structurePanel()->setCurrentPage("structure");
    pump(100);
    QTreeWidgetItem* skItem = nullptr;
    if (auto* sn = w.structurePanel()->sketchesNode())
      for (int i = 0; i < sn->childCount(); ++i) if (sn->child(i)->data(0, Qt::UserRole).toString() == "Sketch1") skItem = sn->child(i);
    check(skItem != nullptr, "Yapı > Sketch'ler > Sketch1");
    if (skItem) {
      QTreeWidget* st = w.structurePanel()->tree();
      if (skItem->parent() && !skItem->parent()->isExpanded()) {   // expand "Sketch'ler" (click on its arrow)
        const QRect pr = st->visualItemRect(skItem->parent());
        QTest::mouseDClick(st->viewport(), Qt::LeftButton, {}, QPoint(pr.left() + 40, pr.center().y()));
        pump(100);
      }
      st->scrollToItem(skItem);
      const QRect r = st->visualItemRect(skItem);
      // SpaceClaim: select the sketch in the Structure tree, then K (Sketch mode) edits it
      QTest::mouseClick(st->viewport(), Qt::LeftButton, {}, QPoint(r.left() + 40, r.center().y()));
      pump(150);
      check(w.selection()->items() == QList<SelItem>{SelItem{SelItem::Kind::Sketch, "Sketch1"}}, "Sketch1 selected in the Yapı tree");
      key(w, Qt::Key_K);
      waitFor([&] { return w.inSketchMode(); }, 3000);
    }
    check(w.inSketchMode() && w.openSketchId() == "Sketch1", "K: editing Sketch1 (" + msg(w) + ")");
    ed->setFocus();
    ed->clearSelection();
    ed->fitView();
    pump(200);
    const pc::SkConstraint* hdim = nullptr;
    for (const auto& c : ed->sketch().constraints) if (c.type == pc::SkConstraintType::Length && c.valueExpr == "40") hdim = &c;
    const QRectF lr = hdim ? ed->dimensionLabelRect(hdim->id) : QRectF();
    check(!lr.isEmpty(), "the 40 dimension label is painted");
    if (!lr.isEmpty()) vclick(w, lr.center());
    ed->setFocus();
    const size_t nsBefore = w.document().namedSelections.size();
    key(w, Qt::Key_G, Qt::ControlModifier);
    pcd::QuickParamPopup* pp = nullptr;
    waitFor([&] {
      for (auto* p : w.viewer()->findChildren<pcd::QuickParamPopup*>("quickParamPopup")) if (p->isVisible()) pp = p;
      return pp != nullptr;
    }, 2000);
    check(pp != nullptr, "Ctrl+G on the dimension opens the parameter popup");
    if (pp) {
      pch::typeInto(pp->nameEdit(), "B", "quickParam.name");
      QTest::keyClick(pp->nameEdit(), Qt::Key_Return);
      pump(250);
    }
    check(pexpr(w, "B") == "40" && w.document().namedSelections.size() == nsBefore, "parameter B = 40, no named selection created");
    if (w.inSketchMode()) {
      key(w, Qt::Key_D);
      waitFor([&] { return !w.inSketchMode(); }, 3000);
      pump(200);
    }
    check(!w.inSketchMode() && w.document().namedSelections.size() == nsBefore, "D: sketch finished, still the same named selections");
  }
  checkNames(w, "sketch dimension -> parameter");

  // ------------------------------------------------------------------ F2 rename + undo
  section("F2 rename Yan -> YanDuvar, Ctrl+Z restores");
  clickNs(w, "Yan");
  const int u0 = undo->index();
  panel(w)->tree()->setFocus();
  QTest::keyClick(panel(w)->tree(), Qt::Key_F2);
  pump(150);
  QLineEdit* fe = inlineEditor(w);
  check(fe && fe->text() == "Yan", "F2 opens the inline editor on Yan");
  if (fe) {
    fe->selectAll();
    type(w, "YanDuvar");
    key(w, Qt::Key_Return);
  }
  waitFor([&] { return res(w, "YanDuvar") != nullptr; }, 2000);
  check(res(w, "YanDuvar") && !res(w, "Yan") && undo->index() == u0 + 1 && nsItem(w, "YanDuvar"), "renamed (one undo step): " + undo->undoText());
  check(res(w, "YanDuvar") && res(w, "YanDuvar")->faces.size() == 1, "same face after the rename");
  // duplicate name is refused without a dialog
  clickNs(w, "YanDuvar");
  QTest::keyClick(panel(w)->tree(), Qt::Key_F2);
  pump(150);
  if ((fe = inlineEditor(w))) {
    fe->selectAll();
    type(w, "ust");
    key(w, Qt::Key_Return);
    pump(200);
  }
  check(res(w, "YanDuvar") && undo->index() == u0 + 1 && !QApplication::activeModalWidget(), "duplicate (case-insensitive) 'ust' refused, non-modal: " + msg(w).left(100));
  v->setFocus();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return res(w, "Yan") != nullptr; }, 2000);
  check(res(w, "Yan") && !res(w, "YanDuvar"), "Ctrl+Z: Yan again");
  checkNames(w, "rename undone");

  // ------------------------------------------------------------------ right click a face -> create; Del; undo
  section("right click on a face: 'Adlandırılmış seçim oluştur'; Del deletes; Ctrl+Z / Ctrl+Y");
  key(w, Qt::Key_H);
  pump(150);
  v->setFocus();
  w.selection()->clear();
  const QString rightFace = faceWithNormal(w, 1, 0, 0);
  check(!rightFace.isEmpty(), "+X side face visible: " + rightFace);
  const QPointF rp = facePx(w, rightFace);
  vmove(w, rp);
  rightClick(v, rp.toPoint());
  QMenu* vm = popupMenu("viewContextMenu");
  QAction* cr = menuAction(vm, "actNsCreateHere");
  check(vm && cr, "viewport menu has 'Adlandırılmış seçim oluştur'");
  const int u1 = undo->index();
  if (vm && cr) clickMenuAction(vm, cr);
  closePopups();
  waitFor([&] { return inlineEditor(w) != nullptr; }, 2000);
  QLineEdit* le2 = inlineEditor(w);
  check(le2 && le2->text() == "Grup1", "new selection Grup1 in inline-edit mode");
  if (le2) { QTest::keyClick(le2, Qt::Key_Escape); pump(150); }
  check(res(w, "Grup1") && res(w, "Grup1")->faces == std::vector<std::string>{rightFace.toStdString()} && undo->index() == u1 + 1,
        "Esc keeps the default name Grup1 (one step)");
  clickNs(w, "Grup1");
  panel(w)->tree()->setFocus();
  QTest::keyClick(panel(w)->tree(), Qt::Key_Delete);
  pump(200);
  check(!res(w, "Grup1") && w.document().feature("Extrude1") && undo->index() == u1 + 2, "Del in the panel deletes Grup1 (geometry untouched)");
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return res(w, "Grup1") != nullptr; }, 2000);
  check(res(w, "Grup1") != nullptr, "Ctrl+Z restores Grup1");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return res(w, "Grup1") == nullptr; }, 2000);
  check(!res(w, "Grup1"), "Ctrl+Y deletes it again");
  // context menu "Sil" on Kenar
  clickNs(w, "Kenar", Qt::RightButton);
  QMenu* km = popupMenu("nsContextMenu");
  clickMenuAction(km, menuAction(km, "ns.delete"));
  closePopups();
  waitFor([&] { return res(w, "Kenar") == nullptr; }, 2000);
  check(!res(w, "Kenar"), "context menu 'Sil' deletes Kenar");
  checkNames(w, "create / delete");

  // ------------------------------------------------------------------ Properties "Ad"
  section("Özellikler > Ad: type a name for a selected face");
  v->setFocus();
  key(w, Qt::Key_S);
  w.selection()->clear();
  vclick(w, facePx(w, rightFace));
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
  pump(250);
  auto* adEd = qobject_cast<QLineEdit*>(w.propertiesPanel()->editor("nsName"));
  check(adEd && adEd->text().isEmpty(), "face in no selection: empty 'Ad'");
  if (adEd) {
    QTest::mouseClick(adEd, Qt::LeftButton);
    QTest::keyClicks(adEd, "Sag");
    QTest::keyClick(adEd, Qt::Key_Return);
    pump(300);
  }
  check(res(w, "Sag") && res(w, "Sag")->faces == std::vector<std::string>{rightFace.toStdString()}, "Properties 'Ad' = Sag creates the selection");
  adEd = qobject_cast<QLineEdit*>(w.propertiesPanel()->editor("nsName"));
  check(adEd && adEd->text() == "Sag", "Properties shows Ad = Sag");
  if (adEd) {   // over-typing renames a selection that consists of exactly this face
    adEd->selectAll();
    QTest::keyClicks(adEd, "SagDuvar");
    QTest::keyClick(adEd, Qt::Key_Return);
    pump(300);
  }
  check(res(w, "SagDuvar") && !res(w, "Sag"), "over-typing 'Ad' renames it (SagDuvar)");
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(0);
  checkNames(w, "properties Ad");

  // ------------------------------------------------------------------ labels, tint, Save as App
  section("Görünüm > Adlandırılmış seçim etiketleri; Vurguyu göster; Save-as-App section");
  QAction* lab = w.findChild<QAction*>("actNsLabels");
  check(lab && lab->text() == "Adlandırılmış seçim etiketleri" && !lab->isChecked(), "Görünüm toggle exists (off by default)");
  if (lab) lab->trigger();
  pump(200);
  QStringList lt = w.namedSelectionLabelTexts();
  check(lt.contains("Ust") && lt.contains("Yan") && lt.contains("SagDuvar"), "labels drawn: " + lt.join(", "));
  clickNs(w, "Ust", Qt::RightButton);
  QMenu* tm2 = popupMenu("nsContextMenu");
  clickMenuAction(tm2, menuAction(tm2, "ns.tint"));
  closePopups();
  check(w.namedSelectionTinted("Ust"), "'Vurguyu göster' tints Ust");
  w.selection()->clear();
  key(w, Qt::Key_H);
  pump(200);
  shot(w, "05_labels_tint.png");
  if (lab) lab->trigger();
  bool saSeen = false;
  QStringList saNames;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
    saSeen = true;
    if (auto* l = m->findChild<QListWidget*>("saNamedSelectionList"))
      for (int i = 0; i < l->count(); ++i) saNames << l->item(i)->text() + (l->item(i)->checkState() == Qt::Checked ? "+" : "-");
    if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(80, d, &QDialog::reject);
  };
  v->setFocus();
  key(w, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
  waitFor([&] { return saSeen && !QApplication::activeModalWidget(); }, 6000);
  g_onModal = nullptr;
  check(saSeen && saNames == QStringList({"Ust+", "Yan+", "SagDuvar+"}), "Save-as-App lists the named selections (STEP names ticked): " + saNames.join(", "));
  checkNames(w, "final");
  showGroups(w);
  clickNs(w, "Ust");
  shot(w, "06_final_groups.png");
  check(g_unexpectedModals == 0, QString("no unexpected dialogs (%1)").arg(g_unexpectedModals));
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
  QApplication::setApplicationName("pc_gui_harness_named_sel");   // no layout persistence
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
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
  pump(600);
  run(w, tmp.path());
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  w.setAutoYes(true);
  return g_fail;
}
