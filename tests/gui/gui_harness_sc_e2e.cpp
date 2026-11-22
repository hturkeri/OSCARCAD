// SC-UX end-to-end acceptance harness — the REAL integrated MainWindow (WP-A shell + WP-B tools), driven like a
// SpaceClaim user in the first ten minutes (docs/spaceclaim_ux/ACCEPTANCE.md):
//
//   new document → K (XY) → R, click the origin, 60 Tab 40 Enter → D → P, click the region, 8 Enter
//   (60×40×8 = 19200 mm³, one undo step) → Ctrl+G = parameter T ("Uygulamada göster") in Gruplar → T edited in
//   Gruplar (volume follows) → Ctrl+Z / Ctrl+Y → edge click + 3 Enter = Fillet → second sketch on the top face,
//   Ø10 circle, Pull with the "Kes (Cut)" option → third sketch with a point + Delik (Hole) → X section → D →
//   save .pcad, reopen (parameters / features intact) → Uygulama Olarak Kaydet → app --validate-only reports the
//   parameters and the right bbox for --set T=…
//
// ACCEPTANCE items exercised on the real window: 3, 4, 5, 8, 12, 13, 14, 16, 17, 19, 21, 22 (Cut option), 24, 26 (left
// drag), 27, 28, 34, 36, 41, 44 (+ WP-B requests A-1 / A-2 / A-3 and the integration switches).
// Every interaction is a real key / mouse event (QTest) into the focused widget, the viewer or the sketch overlay,
// except where noted ([note] lines). Robust driving (tests/gui/harness_robust.h): bounded waits, a stall watchdog,
// modal dialogs with close deadlines.
//
//   pc_gui_harness_sc_e2e [--shots]    --shots writes docs/spaceclaim_ux/shots/final_*.png
//   Exit code = number of failed checks.
#include "DimensionBox.h"
#include "MainWindow.h"
#include "OptionsPanel.h"
#include "PropertiesPanel.h"
#include "PullMapping.h"
#include "PullTool.h"
#include "Ribbon.h"
#include "SketchEditor.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "ViewportOverlay.h"
#include "harness_robust.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVector3D>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <functional>

#ifndef PC_SC_SHOTS_DIR
#define PC_SC_SHOTS_DIR "docs/spaceclaim_ux/shots"
#endif

using pcd::SelItem;

namespace {
int g_fail = 0, g_pass = 0;
bool g_shots = false;
pcd::MainWindow* g_w = nullptr;
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

// ---- modal pump: message boxes are answered (Yes / OK), the manufacturing review is applied, other modals go to
// g_onModal; every modal has a close deadline (pch::enforceDeadline)
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
template <class T> T* childWithText(QWidget* root, const QString& text) {
  for (T* c : root->findChildren<T*>()) if (c->text() == text || c->text().remove('&') == text) return c;
  return nullptr;
}
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (m->property("e2eHandled").toBool()) return;
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      m->setProperty("e2eHandled", true);
      g_boxes << mb->text();
      note("message box: " + mb->text().left(120).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b) b = mb->button(QMessageBox::Save);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      m->setProperty("e2eHandled", true);
      note("manufacturing review dialog -> 'Yine de Uygula'");
      if (auto* b = childWithText<QPushButton>(m, "Yine de Uygula")) pch::pressLater(b, 50);
      else if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
      return;
    }
    if (g_onModal) { m->setProperty("e2eHandled", true); g_onModal(m); return; }
  });
  t->start();
}

// ---- real input
void ensureActive(pcd::MainWindow& w) {
  if (QApplication::activeModalWidget()) return;
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
  // what a real cursor would hit: a child overlay that is not transparent for the mouse (mini-toolbar, guide bar,
  // value box) would take the click. The harness reports it and clicks the model (a user would aim beside it).
  if (QWidget* c = t->childAt(p.toPoint()); c && c->isVisible())
    note(QString("click at (%1,%2) is over %3 '%4' — clicking the view").arg(p.x()).arg(p.y()).arg(c->metaObject()->className(), c->objectName()));
  vmove(w, p);
  QTest::mouseClick(t, Qt::LeftButton, m, p.toPoint());
  pump(80);
}
void drag(QWidget* t, Qt::MouseButton b, Qt::KeyboardModifiers m, QPoint from, QPoint to, int steps = 8) {
  QTest::mousePress(t, b, m, from);
  for (int i = 1; i <= steps; ++i) {
    const QPoint p = from + (to - from) * i / steps;
    QMouseEvent mv(QEvent::MouseMove, QPointF(p), t->mapToGlobal(QPointF(p)), Qt::NoButton, b, m);
    QApplication::sendEvent(t, &mv);
    pump(12);
  }
  QTest::mouseRelease(t, b, m, to);
  pump(60);
}
QString msg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }
void shot(pcd::MainWindow& w, const QString& name) {
  if (!g_shots) return;
  pump(300);
  QDir().mkpath(PC_SC_SHOTS_DIR);
  const QString path = QString(PC_SC_SHOTS_DIR) + "/" + name;
  note(QString("shot %1 %2").arg(path, w.grab().save(path) ? "saved" : "FAILED"));
}

// ---- model / view queries
double volume(pcd::MainWindow& w) {
  try { return w.modelResult().shape.IsNull() ? 0 : pc::model::massProperties(w.modelResult().shape).volume; } catch (...) { return -1; }
}
const pc::Feature* lastFeature(pcd::MainWindow& w, pc::FeatureType t) {
  const auto& fs = w.document().features;
  for (auto it = fs.rbegin(); it != fs.rend(); ++it) if (it->type == t) return &*it;
  return nullptr;
}
int countFeatures(pcd::MainWindow& w, pc::FeatureType t) {
  int n = 0;
  for (const auto& f : w.document().features) if (f.type == t) ++n;
  return n;
}
QPointF facePx(pcd::MainWindow& w, const QString& face) {
  // the centroid may lie in a hole / pocket: spiral out until the viewer reports the face itself
  double c[3], n[3];
  if (!w.viewer()->faceCentroid(face, c, n)) return QPointF(-1, -1);
  const QPointF p0 = w.viewer()->worldToScreen(c);
  for (int r = 0; r <= 240; r += 6)
    for (int k = 0; k < (r == 0 ? 1 : 16); ++k) {
      const double a = k * M_PI / 8.0;
      const QPointF p = p0 + QPointF(std::cos(a), std::sin(a)) * r;
      if (w.viewer()->pickAt(p.toPoint()) == face) return p;
    }
  return QPointF(-1, -1);
}
QPointF edgePx(pcd::MainWindow& w, const QString& edge) {
  auto it = w.modelResult().edges.find(edge.toStdString());
  if (it == w.modelResult().edges.end()) return QPointF(-1, -1);
  BRepAdaptor_Curve c(it->second);
  for (double t : {0.5, 0.4, 0.6, 0.3, 0.7, 0.25, 0.75}) {
    const gp_Pnt P = c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter()));
    const double a[3] = {P.X(), P.Y(), P.Z()};
    const QPointF p = w.viewer()->worldToScreen(a);
    if (w.viewer()->pickAt(p.toPoint()) == edge) return p;
  }
  return QPointF(-1, -1);
}
QString findEdge(pcd::MainWindow& w, std::function<bool(const gp_Pnt&, const gp_Pnt&)> pred, const QString& contains) {
  for (const auto& [n, e] : w.modelResult().edges) {
    const QString name = QString::fromStdString(n);
    if (!contains.isEmpty() && !name.contains(contains)) continue;
    try {
      BRepAdaptor_Curve c(e);
      if (c.GetType() != GeomAbs_Line) continue;
      if (pred(c.Value(c.FirstParameter()), c.Value(c.LastParameter()))) return name;
    } catch (...) {}
  }
  return QString();
}
QTableView* paramView(pcd::MainWindow& w) { return w.parameterTable(); }
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
int runProc(const QString& prog, const QStringList& args, QByteArray* out) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(120000)) { p.kill(); return -100; }
  if (out) *out = p.readAllStandardOutput();
  return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
}
// the Fill-able / file dialogs: select `path` and accept
void acceptFileDialog(QWidget* m, const QString& path) {
  auto* fd = qobject_cast<QFileDialog*>(m);
  if (!fd) return;
  QTimer::singleShot(200, fd, [fd, path] {
    fd->selectFile(path);
    if (auto* le = fd->findChild<QLineEdit*>("fileNameEdit")) le->setText(path);
    QTimer::singleShot(100, fd, [fd] { QMetaObject::invokeMethod(fd, "accept"); });
  });
}

// ============================================================================ the flow
void run(pcd::MainWindow& w, const QString& tmpDir) {
  auto* tm = w.toolManager();
  auto* ed = w.sketchEditor();
  auto* v = w.viewer();
  auto* undo = w.undoStack();

  // -------------------------------------------------------------- integration basics
  section("integration: real ToolManager, SpaceClaim navigation, overlays");
  check(tm && tm->active() != nullptr && tm->activeId() == "select", "real ToolManager active (stub bridge off), tool = select");
  check(v->navigationScheme() == pc::viewer::ShapeViewer::NavigationScheme::SpaceClaim, "designer viewer uses the SpaceClaim navigation scheme");
  check(ed->attachedViewer() == v && ed->parentWidget() == v, "sketch editor is WP-B's overlay on the viewer");
  check(w.findChild<QWidget*>("toolGuideBar") != nullptr && w.findChild<QWidget*>("miniToolbar") != nullptr &&
            w.findChild<QWidget*>("viewportOverlay") != nullptr, "tool guide bar, mini-toolbar and viewport overlay exist");
  check(pcd::kToolApiVersionMajor == 1, QString("ToolApi %1.%2").arg(pcd::kToolApiVersionMajor).arg(pcd::kToolApiVersionMinor));

  // -------------------------------------------------------------- AC1/3: new document, K -> XY
  section("ACC 1, 3: new document, K opens a sketch on XY");
  v->setFocus();
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(w.document().features.empty() && w.document().params.all().empty(), "Ctrl+N: empty document");
  shot(w, "final_3d_empty.png");
  v->setFocus();
  key(w, Qt::Key_K);
  check(w.inSketchMode() && w.designMode() == pcd::DesignMode::Sketch, "K: Sketch mode");
  check(ed->isVisible() && ed->sketch().plane.kind == pc::SketchPlaneKind::XY, "empty document: the sketch is on XY");
  check(tm->activeId() == "sketch.select", "Sketch mode starts with Seç (sketch.select)");
  check(w.ribbon()->group("constraints") && w.ribbon()->group("constraints")->isVisible(), "contextual Kısıtlar group visible");
  check(w.document().features.empty(), "the document is unchanged until the sketch is finished");
  {   // the plan view looks at XY with v up
    const QTransform T = ed->planeToScreen();
    check(std::fabs(T.m11() * T.m21() + T.m12() * T.m22()) < 1e-6 * std::hypot(T.m11(), T.m12()) * std::hypot(T.m21(), T.m22()) + 1e-9 && T.m22() < 0,
          "the view looks straight at the sketch plane (plan view, v up)");
  }

  // -------------------------------------------------------------- AC4/5: R, origin, 60 Tab 40 Enter
  section("ACC 4, 5: R, click the origin, 60 Tab 40 Enter");
  key(w, Qt::Key_R);
  check(tm->activeId() == "sketch.rect" && ed->tool() == pcd::SketchEditor::Tool::Rect, "R: Dikdörtgen");
  vclick(w, ed->sketchToScreen(0, 0));
  check(ed->drawStage() == 1 && ed->dimensionBox() && ed->dimensionBox()->isVisible(), "first corner on the origin, dimension fields shown");
  type(w, "60");
  check(ed->dimensionEditing(), "typing opens the width field");
  key(w, Qt::Key_Tab);
  vmove(w, ed->sketchToScreen(30, 25));
  const QSizeF s1 = ed->previewSize();
  vmove(w, ed->sketchToScreen(45, 55));
  const QSizeF s2 = ed->previewSize();
  check(std::fabs(s1.width() - 60) < 1e-6 && std::fabs(s2.width() - 60) < 1e-6 && std::fabs(s1.height() - s2.height()) > 5,
        QString("AC5: locked width 60 while the height follows the cursor (%1 -> %2)").arg(s1.height()).arg(s2.height()));
  type(w, "40");
  shot(w, "final_sketch_rect_dims.png");
  key(w, Qt::Key_Return);
  {
    QStringList lens;
    int lines = 0;
    for (const auto& e : ed->sketch().entities) if (e.type == pc::SkEntityType::Line) ++lines;
    for (const auto& c : ed->sketch().constraints) if (c.type == pc::SkConstraintType::Length) lens << QString::fromStdString(c.valueExpr);
    lens.sort();
    check(lines == 4 && lens == QStringList({"40", "60"}), "AC4: 4 lines, Length {60, 40}: " + lens.join(","));
    check(ed->dof() == 0, QString("AC4: DOF %1").arg(ed->dof()));
    bool dofLabel = false;
    for (auto* l : w.statusBar()->findChildren<QLabel*>()) if (l->text() == "Sketch DOF: 0") dofLabel = true;
    check(dofLabel, "status bar: 'Sketch DOF: 0'");
  }
  shot(w, "final_sketch_rect.png");

  // -------------------------------------------------------------- AC12: D -> region
  section("ACC 12: D commits the sketch, region in 3D");
  const int u0 = undo->count();
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  check(!w.inSketchMode() && w.document().feature("Sketch1") && undo->count() == u0 + 1, "D: Sketch1 committed (one undo step), 3D mode");
  check(tm->activeId() == "select", "3D mode: tool back to Seç");
  key(w, Qt::Key_H);
  pump(200);
  QPointF rp = facePx(w, "sketch:Sketch1:r0");
  check(rp.x() >= 0, "AC12: region sketch:Sketch1:r0 drawn and pickable");

  // -------------------------------------------------------------- AC13/14: P, region, 8 Enter
  section("ACC 13, 14: P, click the region, 8 Enter -> 60x40x8");
  key(w, Qt::Key_P);
  check(tm->activeId() == "pull", "P: Çek");
  vclick(w, rp);
  auto* ov = v->findChild<pcd::ViewportOverlay*>("viewportOverlay");
  check(w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch1:r0"}), "region selected by the click");
  check(ov && ov->handleCount() == 1, "AC13: one pull arrow on the region");
  check(msg(w).startsWith("Çek"), "AC13: status prompt starts with 'Çek': " + msg(w).left(80));
  shot(w, "final_pull_armed.png");
  const int u1 = undo->count();
  type(w, "8");
  auto* pt = qobject_cast<pcd::PullTool*>(tm->active());
  check(pt && pt->dimensionBox() && pt->dimensionBox()->isEditing(), "typing 8 opens the Pull value box");
  shot(w, "final_pull_box.png");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 5000);
  const pc::Feature* ex = w.document().feature("Extrude1");
  check(ex && ex->prop("distance") == "8" && w.modelResult().bodies.size() == 1, "AC14: Extrude1 distance=8, one body");
  check(std::fabs(volume(w) - 19200) < 0.5, QString("AC14: volume %1 mm³ (19200)").arg(volume(w), 0, 'f', 1));
  check(undo->count() == u1 + 1, QString("AC14: one undo step (%1 -> %2)").arg(u1).arg(undo->count()));

  // -------------------------------------------------------------- AC16: Ctrl+G -> T, exposed
  section("ACC 16, 41: Ctrl+G after the pull -> parameter T, 'Uygulamada göster'");
  bool npSeen = false;
  QString npSuggested;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pcd::NewParameterDialog")) { note(QString("unexpected modal %1").arg(m->metaObject()->className())); return; }
    npSeen = true;
    auto* name = m->findChild<QLineEdit*>("npName");
    npSuggested = name ? name->text() : QString();
    pch::typeInto(name, "T", "npName");
    pch::tickCheckBox(m->findChild<QCheckBox*>("npExposed"), true, "npExposed");
    QPushButton* ok = nullptr;
    for (auto* b : m->findChildren<QPushButton*>()) if (b->text().remove('&') == "OK" || b->text() == "Tamam") ok = b;
    if (ok) pch::pressLater(ok, 50);
    else QTimer::singleShot(50, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  v->setFocus();
  key(w, Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return npSeen && !QApplication::activeModalWidget(); }, 8000);
  pump(300);
  g_onModal = nullptr;
  const pc::Parameter* T = w.document().params.find("T");
  check(npSeen && npSuggested == "Extrude1_distance", "Ctrl+G: NewParameterDialog suggests '" + npSuggested + "'");
  check(T && T->exposed && T->expression == "8", "T = 8, exposed ('Uygulamada göster')");
  check(w.document().feature("Extrude1") && w.document().feature("Extrude1")->prop("distance") == "T", "Extrude1.distance = T");
  w.structurePanel()->setCurrentPage("groups");
  pump(150);
  const int rT = paramRow(w, "T");
  check(rT >= 0 && paramView(w)->isVisible(), "T is listed in Gruplar");
  check(rT >= 0 && paramModel(w)->item(rT, 6) && paramModel(w)->item(rT, 6)->checkState() == Qt::Checked, "Gruplar: 'Uygulamada göster' ticked for T");
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(0);
  w.structurePanel()->setCurrentPage("structure");
  v->setFocus();
  key(w, Qt::Key_H);   // Ana görünüm: the whole solid
  pump(100);
  shot(w, "final_solid_structure.png");
  w.structurePanel()->setCurrentPage("groups");
  pump(100);
  shot(w, "final_solid_groups.png");

  // -------------------------------------------------------------- AC34: T edited in Gruplar
  section("ACC 34: change T in Gruplar -> volume follows, face names kept");
  {
    auto* pv = paramView(w);
    const QModelIndex idx = pv->model()->index(paramRow(w, "T"), 1);
    pv->scrollTo(idx);
    const QRect r = pv->visualRect(idx);
    QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    QTest::mouseDClick(pv->viewport(), Qt::LeftButton, {}, r.center());
    pump(150);
    QLineEdit* ce = pv->viewport()->findChild<QLineEdit*>();
    if (!ce) { note("double click did not open the cell editor; QAbstractItemView::edit() used"); pv->edit(idx); pump(100); ce = pv->viewport()->findChild<QLineEdit*>(); }
    check(ce != nullptr, "T expression cell editor open");
    if (ce) {
      ce->selectAll();
      QTest::keyClicks(ce, "12");
      check(tm->activeId() == "pull" || tm->activeId() == "select", "typing in the cell triggers no tool shortcut");
      QTest::keyClick(ce, Qt::Key_Return);
    }
    waitFor([&] { return pexpr(w, "T") == "12"; }, 5000);
    pump(300);
    check(pexpr(w, "T") == "12", "T = 12 through Gruplar");
    check(std::fabs(volume(w) - 60 * 40 * 12) < 0.5, QString("volume follows T: %1 (28800)").arg(volume(w), 0, 'f', 1));
    check(w.modelResult().faces.count("Extrude1.end") == 1, "face name Extrude1.end survives");
  }

  // -------------------------------------------------------------- AC36: Ctrl+Z / Ctrl+Y
  section("ACC 36: Ctrl+Z / Ctrl+Y");
  v->setFocus();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return pexpr(w, "T") == "8"; }, 3000);
  check(pexpr(w, "T") == "8" && std::fabs(volume(w) - 19200) < 0.5, "Ctrl+Z: T back to 8 (19200 mm³)");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return pexpr(w, "T") == "12"; }, 3000);
  check(pexpr(w, "T") == "12" && std::fabs(volume(w) - 28800) < 0.5, "Ctrl+Y: T = 12 again");

  // -------------------------------------------------------------- AC17: pull the top face by 2 -> T = 14
  section("ACC 17: Pull the top face by 2 -> T updated");
  w.structurePanel()->setCurrentPage("structure");
  key(w, Qt::Key_H);
  pump(200);
  v->setFocus();
  key(w, Qt::Key_P);
  w.selection()->clear();
  {
    const QPointF top = facePx(w, "Extrude1.end");
    check(top.x() >= 0, "top face Extrude1.end is pickable");
    vclick(w, top);
    check(w.selection()->contains(SelItem{SelItem::Kind::Face, "Extrude1.end"}), "clicked the top face");
    type(w, "2");
    key(w, Qt::Key_Return);
    waitFor([&] { return pexpr(w, "T") == "14"; }, 5000);
    check(pexpr(w, "T") == "14", "AC17: T = 14 (" + QString::fromStdString(pexpr(w, "T")) + ")");
    check(msg(w).contains("parametre güncellendi"), "AC17: status says 'parametre güncellendi': " + msg(w).left(90));
  }

  // -------------------------------------------------------------- AC19: edge click, 3 Enter -> Fillet
  section("ACC 19, 21: edge pull -> Fillet r=3, fillet face -> r=5");
  key(w, Qt::Key_H);
  pump(200);
  const double zTop = 14;
  const QString topEdge = findEdge(w, [&](const gp_Pnt& a, const gp_Pnt& b) {
    return std::fabs(a.Z() - zTop) < 1e-6 && std::fabs(b.Z() - zTop) < 1e-6 && std::fabs(a.Y()) < 1e-6 && std::fabs(b.Y()) < 1e-6;
  }, "Extrude1.end");
  w.selection()->clear();
  const QPointF ep = edgePx(w, topEdge);
  check(!topEdge.isEmpty() && ep.x() >= 0, "front top edge pickable with a plain click: " + topEdge);
  vclick(w, ep);
  check(w.selection()->count() == 1 && w.selection()->items().first().kind == SelItem::Kind::Edge, "plain click selects the edge (no Ctrl, no menu)");
  check(QApplication::activePopupWidget() == nullptr, "no edge context menu on a plain click");
  type(w, "3");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Fillet1") != nullptr; }, 5000);
  const pc::Feature* fl = w.document().feature("Fillet1");
  check(fl && fl->refs.size() == 1 && fl->prop("radius") == "3", "AC19: Fillet1 r=3 on one edge");
  {
    const size_t nf = w.document().features.size();
    w.selection()->clear();
    const QPointF ff = facePx(w, "Fillet1.f0");
    check(ff.x() >= 0, "fillet face Fillet1.f0 pickable");
    vclick(w, ff);
    type(w, "5");
    key(w, Qt::Key_Return);
    waitFor([&] { return w.document().feature("Fillet1") && w.document().feature("Fillet1")->prop("radius") == "5"; }, 5000);
    check(w.document().features.size() == nf && w.document().feature("Fillet1")->prop("radius") == "5", "AC21: Fillet1.radius = 5, no new feature");
  }
  // Properties of the fillet face (Options/Properties panel shot)
  {
    w.selection()->clear();
    vclick(w, facePx(w, "Fillet1.f0"));
    if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
    pump(200);
    check(w.propertiesPanel()->rowLabels().contains("Yarıçap"), "Properties: fillet face shows 'Yarıçap'");
    shot(w, "final_properties.png");
    if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(0);
  }

  // -------------------------------------------------------------- AC22 (Cut option): sketch on the top face, Ø10, Pull Cut
  section("ACC 22, 20(panel): sketch on the top face, Ø10 circle, Pull with 'Kes (Cut)' from the Options panel");
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  w.selection()->clear();
  key(w, Qt::Key_S);
  vclick(w, facePx(w, "Extrude1.end"));
  check(w.selection()->contains(SelItem{SelItem::Kind::Face, "Extrude1.end"}), "top face selected");
  key(w, Qt::Key_K);
  check(w.inSketchMode() && ed->sketch().plane.kind == pc::SketchPlaneKind::OnFace && ed->sketch().plane.faceRef == "Extrude1.end",
        "K: OnFace sketch on Extrude1.end");
  key(w, Qt::Key_C);
  check(tm->activeId() == "sketch.circle", "C: Çember");
  vclick(w, ed->sketchToScreen(15, 20));
  type(w, "10");
  // ACC 8 / WP-B A-2: Ctrl+G in the dimension field -> NewParameterDialog, the parameter reaches Gruplar and the
  // sketch's working document, the dimension is bound to it
  bool dcSeen = false;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pcd::NewParameterDialog")) return;
    dcSeen = true;
    pch::typeInto(m->findChild<QLineEdit*>("npName"), "Dc", "npName");
    QTimer::singleShot(50, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  key(w, Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return dcSeen && !QApplication::activeModalWidget(); }, 6000);
  pump(200);
  g_onModal = nullptr;
  check(dcSeen, "ACC 8: Ctrl+G in the Ø field opens NewParameterDialog");
  check(w.document().params.find("Dc") && paramRow(w, "Dc") >= 0, "ACC 8: Dc committed to the document and listed in Gruplar while sketching");
  check(w.expressionContext()->params.find("Dc") != nullptr, "A-2: Dc is in the sketch's working document (expression context)");
  if (ed->dimensionEditing() || (ed->dimensionBox() && ed->dimensionBox()->isVisible())) key(w, Qt::Key_Return);
  {
    int circles = 0;
    std::string dia;
    for (const auto& e : ed->sketch().entities) if (e.type == pc::SkEntityType::Circle) ++circles;
    for (const auto& c : ed->sketch().constraints) if (c.type == pc::SkConstraintType::Diameter) dia = c.valueExpr;
    check(circles == 1 && dia == "Dc", "Ø10 circle with Diameter = Dc (" + QString::fromStdString(dia) + ")");
  }
  shot(w, "final_sketch_on_face.png");
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  check(!w.inSketchMode() && w.document().feature("Sketch2"), "D: Sketch2 committed");
  key(w, Qt::Key_H);
  pump(200);
  const double v0 = volume(w);
  key(w, Qt::Key_P);
  const QPointF cr = facePx(w, "sketch:Sketch2:r0");
  check(cr.x() >= 0, "circle region pickable on the top face");
  vclick(w, cr);
  pump(150);
  QPointer<QComboBox> mergeCombo = w.optionsPanel()->findChild<QComboBox*>("opt.merge.combo");
  check(mergeCombo && mergeCombo->isVisible() && mergeCombo->findData("cut") >= 0, "Options panel (Seçenekler - Çek) offers 'Kes (Cut)' in Birleştirme");
  shot(w, "final_options_pull.png");
  if (mergeCombo) {   // real input: focus the combo (click), arrow keys to "Kes (Cut)"
    QTest::mouseClick(mergeCombo, Qt::LeftButton);   // opens the popup
    pump(200);
    if (QAbstractItemView* pv = mergeCombo->view(); pv && pv->isVisible()) {
      const int target = mergeCombo->findData("cut");
      for (int i = mergeCombo->currentIndex(); i < target; ++i) { QTest::keyClick(pv, Qt::Key_Down); pump(30); }
      QTest::keyClick(pv, Qt::Key_Return);
      pump(250);
    }
    if (mergeCombo && mergeCombo->currentData().toString() != "cut") {
      note("combo popup selection did not take; choosing 'Kes' programmatically");
      mergeCombo->hidePopup();
      mergeCombo->setCurrentIndex(mergeCombo->findData("cut"));
      emit mergeCombo->activated(mergeCombo->currentIndex());
      pump(150);
    }
  }
  check(tm->active() && [&] { for (const auto& o : tm->active()->options()) if (o.key == "merge") return o.value.toString() == "cut"; return false; }(),
        "Pull option merge = cut");
  // the option change rebuilds the panel: the region stays selected in Pull
  v->setFocus();
  if (!w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch2:r0"})) vclick(w, facePx(w, "sketch:Sketch2:r0"));
  type(w, "5");
  key(w, Qt::Key_Return);
  waitFor([&] { return countFeatures(w, pc::FeatureType::Extrude) == 2; }, 5000);
  const pc::Feature* cut = lastFeature(w, pc::FeatureType::Extrude);
  check(cut && cut->id != "Extrude1" && cut->prop("op") == "cut", "Pull with 'Kes': " + QString::fromStdString(cut ? cut->id + " op=" + cut->prop("op") + " dir=" + cut->prop("direction") : "none"));
  check(volume(w) < v0 - 300, QString("pocket cut: volume %1 -> %2").arg(v0, 0, 'f', 1).arg(volume(w), 0, 'f', 1));
  tm->setOption("merge", "auto");   // back to Otomatik for the rest of the session

  // -------------------------------------------------------------- hole: point sketch + Delik
  section("Hole: sketch point on the top face + Delik (ribbon) through all");
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  w.selection()->clear();
  vclick(w, facePx(w, "Extrude1.end"));
  note("selection before K: " + pcd::selectionSummary(w.selection()->items()) + ", tool " + tm->activeId());
  key(w, Qt::Key_K);
  check(w.inSketchMode() && ed->sketch().plane.faceRef == "Extrude1.end", "K: third sketch on the top face");
  if (QAction* pa = tm->action("sketch.point")) {   // ribbon Sketch > "Nokta" (no keyboard shortcut in SpaceClaim)
    QToolButton* b = nullptr;
    for (auto* tb : w.ribbon()->findChildren<QToolButton*>()) if (tb->defaultAction() == pa && tb->isVisible()) b = tb;
    if (b) QTest::mouseClick(b, Qt::LeftButton);
    else { note("Nokta ribbon button not visible; action triggered"); pa->trigger(); }
    pump(100);
  }
  check(tm->activeId() == "sketch.point", "Nokta tool");
  const size_t pts0 = ed->sketch().points.size();
  vclick(w, ed->sketchToScreen(45, 20));
  check(ed->sketch().points.size() == pts0 + 1, QString("one sketch point placed (%1 -> %2 points)").arg(pts0).arg(ed->sketch().points.size()));
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  const std::string holeSketch = lastFeature(w, pc::FeatureType::Sketch) ? lastFeature(w, pc::FeatureType::Sketch)->id : std::string();
  check(holeSketch == "Sketch3", "Sketch3 (hole position) committed");
  const double vBeforeHole = volume(w);
  bool holeDlg = false;
  QAction* actHole = w.findChild<QAction*>("actHole");
  check(actHole != nullptr, "ribbon Oluştur > Delik");
  if (actHole) {
    actHole->trigger();
    waitFor([&] { return w.featureDialog() != nullptr; }, 3000);
    if (auto* dlg = w.featureDialog()) {
      holeDlg = true;
      for (auto* cb : dlg->findChildren<QComboBox*>()) {
        const int i = cb->findData(QString::fromStdString(holeSketch));
        if (i >= 0) cb->setCurrentIndex(i);
      }
      for (auto* c : dlg->findChildren<QCheckBox*>()) if (c->text() == "Delik boydan boya") pch::tickCheckBox(c, true, "through");
      if (auto* ok = childWithText<QPushButton>(dlg, "Oluştur")) QTest::mouseClick(ok, Qt::LeftButton);
      waitFor([&] { return w.featureDialog() == nullptr; }, 5000);
      pump(400);
    }
  }
  const pc::Feature* hole = lastFeature(w, pc::FeatureType::Hole);
  check(holeDlg && hole && hole->prop("sketch") == holeSketch, "Hole1 created on Sketch3");
  check(volume(w) < vBeforeHole - 100, QString("hole removed material (%1 -> %2)").arg(vBeforeHole, 0, 'f', 1).arg(volume(w), 0, 'f', 1));
  check(w.modelResult().ok, "model regenerates without errors");
  for (int r = 0; r < w.issuesTable()->rowCount(); ++r)
    note("mfg issue: " + (w.issuesTable()->item(r, 2) ? w.issuesTable()->item(r, 2)->text() : QString()) + " " +
         (w.issuesTable()->item(r, 3) ? w.issuesTable()->item(r, 3)->text() : QString()));

  // -------------------------------------------------------------- ACC 44: Move + "Desen oluştur" on the hole -> Pattern
  section("ACC 44: M + 'Desen oluştur', hole face, X arrow -20 -> linear Pattern (count 2)");
  {
    v->setFocus();
    key(w, Qt::Key_Escape);
    key(w, Qt::Key_Escape);
    key(w, Qt::Key_H);
    key(w, Qt::Key_M);
    check(tm->activeId() == "move", "M: Taşı");
    QCheckBox* pat = nullptr;
    if (QWidget* f = w.optionsPanel()->field("pattern")) pat = qobject_cast<QCheckBox*>(f) ? qobject_cast<QCheckBox*>(f) : f->findChild<QCheckBox*>();
    check(pat && pat->isVisible(), "Options panel: 'Desen oluştur' check box");
    if (pat) pch::tickCheckBox(pat, true, "Desen oluştur");
    pump(150);
    QString holeFace;
    QPointF hp(-1, -1);
    for (const auto& [n, f] : w.modelResult().faces) {
      const QString q = QString::fromStdString(n);
      if (!q.startsWith("Hole1.")) continue;
      hp = facePx(w, q);
      if (hp.x() >= 0) { holeFace = q; break; }
    }
    check(hp.x() >= 0, "a Hole1 face is pickable: " + holeFace);
    w.selection()->clear();
    vclick(w, hp);
    check(msg(w).startsWith("Taşı + Desen"), "status: 'Taşı + Desen: Hole1 kaynağı …' (" + msg(w).left(60) + ")");
    const double vp0 = volume(w);
    type(w, "-20");
    key(w, Qt::Key_Return);
    waitFor([&] { return lastFeature(w, pc::FeatureType::Pattern) != nullptr; }, 5000);
    const pc::Feature* pf = lastFeature(w, pc::FeatureType::Pattern);
    check(pf && pf->prop("source") == "Hole1" && pf->prop("spacing") == "20" && pf->prop("count") == "2" && pf->prop("dirX") == "-1",
          "Pattern1: source Hole1, spacing 20, count 2, along -X");
    check(volume(w) < vp0 - 100, QString("second hole cut (%1 -> %2)").arg(vp0, 0, 'f', 1).arg(volume(w), 0, 'f', 1));
    if (pat && w.optionsPanel()->field("pattern")) pch::tickCheckBox(pat, false, "Desen oluştur off");
    key(w, Qt::Key_Escape);
    key(w, Qt::Key_S);
  }

  // -------------------------------------------------------------- AC27: H / ← / → camera history (R6 test hooks)
  section("ACC 27: H, ← previous view, → next view (viewer camera properties)");
  {
    v->setFocus();
    drag(v, Qt::MiddleButton, Qt::NoModifier, QPoint(800, 400), QPoint(880, 360));
    const double az0 = v->property("cameraAz").toDouble(), h0 = v->property("halfH").toDouble();
    const QVector3D t0 = v->property("target").value<QVector3D>();
    key(w, Qt::Key_H);
    const double azH = v->property("cameraAz").toDouble();
    check(std::fabs(azH - az0) > 1.0, QString("H: home view changes the camera (az %1 -> %2)").arg(az0, 0, 'f', 1).arg(azH, 0, 'f', 1));
    key(w, Qt::Key_Left);
    check(std::fabs(v->property("cameraAz").toDouble() - az0) < 1e-6 && std::fabs(v->property("halfH").toDouble() - h0) < 1e-6 * h0 &&
              (v->property("target").value<QVector3D>() - t0).length() < 1e-4,
          "←: back to the camera before H");
    key(w, Qt::Key_Right);
    check(std::fabs(v->property("cameraAz").toDouble() - azH) < 1e-6, "→: forward to the home view again");
    const double azL = v->property("cameraAz").toDouble();
    drag(v, Qt::LeftButton, Qt::NoModifier, QPoint(1500, 760), QPoint(1400, 700));
    check(std::fabs(v->property("cameraAz").toDouble() - azL) < 1e-9, "left drag on empty space does not rotate (ACC 26)");
  }

  // -------------------------------------------------------------- AC28: X section, D back
  section("ACC 28: X section view, D back to 3D");
  v->setFocus();
  key(w, Qt::Key_H);
  key(w, Qt::Key_X);
  check(w.sectionControls()->sectionOn() && w.designMode() == pcd::DesignMode::Section, "X: section mode on");
  shot(w, "final_section.png");
  key(w, Qt::Key_D);
  check(!w.sectionControls()->sectionOn() && w.designMode() == pcd::DesignMode::ThreeD, "D: section off, 3D mode");

  // -------------------------------------------------------------- save / reopen
  section("save .pcad, reopen: parameters and features intact");
  const QString pcad = tmpDir + "/e2e_part.pcad";
  const nlohmann::json before = pc::toJson(w.document());
  const double volBefore = volume(w);
  bool fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  key(w, Qt::Key_S, Qt::ControlModifier);
  waitFor([&] { return QFileInfo::exists(pcad) && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  check(fileDlg && QFileInfo::exists(pcad), "Ctrl+S: Save As dialog, file written: " + pcad);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  check(w.document().features.empty(), "Ctrl+N clears the document");
  fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  key(w, Qt::Key_O, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty() && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  pump(400);
  check(fileDlg, "Ctrl+O: open dialog");
  const nlohmann::json after = pc::toJson(w.document());
  check(before["features"].size() == 8 && after["features"] == before["features"], QString("reopened: %1 features identical").arg(int(before["features"].size())));
  if (after["parameters"] != before["parameters"]) note(QString::fromStdString("params before: " + before["parameters"].dump() + "\n  after: " + after["parameters"].dump()));
  check(!before["parameters"].empty() && after["parameters"] == before["parameters"], "reopened: parameters identical");
  const pc::Parameter* T2 = w.document().params.find("T");
  check(T2 && T2->expression == "14" && T2->exposed, "reopened: T = 14, exposed");
  check(std::fabs(volume(w) - volBefore) < 1e-3, QString("reopened: volume %1").arg(volume(w), 0, 'f', 1));
  check(w.modelResult().faces.count("Extrude1.end") && w.document().feature("Fillet1") && w.document().feature("Hole1") && w.document().feature("Pattern1"), "reopened: Extrude1.end, Fillet1, Hole1, Pattern1");

  // -------------------------------------------------------------- Save as App
  section("ACC 41: Uygulama olarak kaydet (Ctrl+Shift+A) -> app --validate-only");
  const QString base = tmpDir + "/E2EApp";
  bool appDlg = false, appDone = false, tTicked = false;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
    appDlg = true;
    pch::expectCloseWithin(m, 240000);
    if (auto* t = m->findChild<QTableWidget*>())
      for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, 1) && t->item(r, 1)->text() == "T") tTicked = t->item(r, 0) && t->item(r, 0)->checkState() == Qt::Checked;
    QList<QLineEdit*> edits;
    for (auto* e : m->findChildren<QLineEdit*>()) if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox"))) edits << e;
    if (edits.size() >= 4) edits[3]->setText(base);
    QTimer::singleShot(200, m, [m] { if (auto* b = childWithText<QPushButton>(m, "Uygulamayı Kaydet")) pch::pressLater(b); });
  };
  const int boxes0 = g_boxes.size();
  v->setFocus();
  key(w, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
  appDone = waitFor([&] {
    for (int i = boxes0; i < g_boxes.size(); ++i) if (g_boxes[i].startsWith("Uygulama kaydedildi")) return !QApplication::activeModalWidget();
    return false;
  }, 200000);
  g_onModal = nullptr;
  check(appDlg, "Save-as-App dialog opened by Ctrl+Shift+A");
  check(tTicked, "T pre-ticked as an app input");
  check(appDone, "Save-as-App finished");
#ifdef Q_OS_WIN
  const QString exe = base + ".exe";
#else
  const QString exe = base;
#endif
  check(QFileInfo::exists(exe), "app executable written");
  if (QFileInfo::exists(exe)) {
    auto bbox = [](const QByteArray& so, double d[3]) {
      try {
        auto j = nlohmann::json::parse(so.toStdString());
        d[0] = j["bbox"]["xmax"].get<double>() - j["bbox"]["xmin"].get<double>();
        d[1] = j["bbox"]["ymax"].get<double>() - j["bbox"]["ymin"].get<double>();
        d[2] = j["bbox"]["zmax"].get<double>() - j["bbox"]["zmin"].get<double>();
        return true;
      } catch (...) { return false; }
    };
    QByteArray so;
    int rc = runProc(exe, {"--validate-only"}, &so);
    double b0[3] = {0, 0, 0}, b1[3] = {0, 0, 0};
    check((rc == 0 || rc == 2) && bbox(so, b0), QString("app --validate-only runs (rc %1)").arg(rc));
    bool hasT = false;
    try {
      auto j = nlohmann::json::parse(so.toStdString());
      hasT = j.contains("parameters") && j["parameters"].contains("T") && std::fabs(j["parameters"]["T"].get<double>() - 14) < 1e-9;
    } catch (...) {}
    check(hasT, "app reports the parameters (T = 14)");
    check(std::fabs(b0[0] - 60) < 1e-3 && std::fabs(b0[1] - 40) < 1e-3 && std::fabs(b0[2] - 14) < 1e-3,
          QString("default bbox 60 x 40 x 14 (got %1 x %2 x %3)").arg(b0[0]).arg(b0[1]).arg(b0[2]));
    rc = runProc(exe, {"--validate-only", "--set", "T=20"}, &so);
    try {
      auto j = nlohmann::json::parse(so.toStdString());
      check(j["parameters"]["T"].get<double>() == 20, "--set T=20 reported in 'parameters'");
    } catch (...) { check(false, "--set T=20 JSON"); }
    check(bbox(so, b1) && std::fabs(b1[0] - 60) < 1e-3 && std::fabs(b1[1] - 40) < 1e-3 && std::fabs(b1[2] - 20) < 1e-3,
          QString("--set T=20: bbox 60 x 40 x 20 (got %1 x %2 x %3)").arg(b1[0]).arg(b1[1]).arg(b1[2]));
  }

  // -------------------------------------------------------------- ACC 24 in the real window: K without a selection
  // -> "Sketch düzlemi seç" -> XZ origin square; construction line visible / Alt+clickable in 3D -> Revolve
  section("ACC 24 (integration): plane pick XZ, rectangle + construction line, Alt+click axis, 360 -> Revolve");
  v->setFocus();
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_Escape);
  w.selection()->clear();
  key(w, Qt::Key_H);
  key(w, Qt::Key_K);
  check(w.planePickActive(), "K with nothing selected in a non-empty document: 'Sketch düzlemi seç'");
  QPoint xzPos(-1, -1);
  for (int y = 20; y < v->height() - 20 && xzPos.x() < 0; y += 6)
    for (int x = 20; x < v->width() - 20; x += 6) {
      int kind = 0;
      if (v->pickAt(QPoint(x, y), pc::viewer::ShapeViewer::PickDatum, &kind) == "XZ") { xzPos = QPoint(x, y); break; }
    }
  check(xzPos.x() >= 0, "XZ origin square is pickable");
  if (xzPos.x() >= 0) vclick(w, xzPos);
  check(w.inSketchMode() && ed->sketch().plane.kind == pc::SketchPlaneKind::XZ, "click on the XZ square: sketch on XZ");
  if (w.inSketchMode()) {
    key(w, Qt::Key_R);
    vclick(w, ed->sketchToScreen(100, 0));
    type(w, "20");
    key(w, Qt::Key_Tab);
    type(w, "20");
    key(w, Qt::Key_Return);
    key(w, Qt::Key_L);
    w.toolManager()->setOption("construction", true);
    w.toolManager()->setOption("snapGrid", true);
    vclick(w, ed->sketchToScreen(85, -10));
    vclick(w, ed->sketchToScreen(85, 30));
    key(w, Qt::Key_Escape);
    w.toolManager()->setOption("construction", false);
    int axisId = -1;
    for (const auto& e : ed->sketch().entities) if (e.construction && e.type == pc::SkEntityType::Line) axisId = e.id;
    check(axisId > 0, "construction line drawn");
    const std::string sid = w.openSketchId();
    key(w, Qt::Key_D);
    waitFor([&] { return !w.inSketchMode(); }, 3000);
    v->setView(pc::viewer::ShapeViewer::View::Front);
    v->fitAll();
    pump(200);
    const QString axisName = QString("sketch:%1:e%2").arg(QString::fromStdString(sid)).arg(axisId);
    double P[3] = {85, 0, 25};
    const QPointF ap = v->worldToScreen(P);
    check(v->pickAt(ap.toPoint()) == axisName, "the construction line is drawn and pickable in 3D (WP-B A-1): " + axisName);
    key(w, Qt::Key_P);
    vclick(w, facePx(w, QString("sketch:%1:r0").arg(QString::fromStdString(sid))));
    vclick(w, ap, Qt::AltModifier);
    auto* pt2 = qobject_cast<pcd::PullTool*>(tm->active());
    check(pt2 && pt2->target().kind == pcd::pull::Kind::NewRevolve, "Alt+click on the line: revolve handle");
    type(w, "360");
    key(w, Qt::Key_Return);
    waitFor([&] { return lastFeature(w, pc::FeatureType::Revolve) != nullptr; }, 5000);
    const pc::Feature* rv = lastFeature(w, pc::FeatureType::Revolve);
    check(rv && rv->prop("axis") == std::to_string(axisId) && rv->prop("angle") == "360", "Revolve axis = construction line, 360°");
    key(w, Qt::Key_H);
    shot(w, "final_revolve.png");
  }
}

// PULL_FACE_FIX (docs/spaceclaim_ux/PULL_FACE_FIX.md) — the user video: swirl cup (Revolve 360° + slot cuts), select
// the large planar END face, P, drag the arrow outward: the field must say "Mesafe" (not "Açı") and the body must be
// EXTENDED (Sketch on the face + Extrude join), the revolve angle stays 360°. Then -2 cuts, "Yeni gövde" adds a body.
void swirlCupPull(pcd::MainWindow& w) {
  section("PULL_FACE_FIX: swirl cup end face — P, drag / 5 Enter extends the body (video regression)");
  auto* tm = w.toolManager();
  auto* v = w.viewer();
  auto* undo = w.undoStack();
  const QString path = QString(PC_SAMPLES_DIR) + "/ref_swc_rr_001.pcad";
  w.setAutoYes(true);
  check(w.openFile(path), "ref_swc_rr_001.pcad opened");
  pump(300);
  // the largest planar Cup_Don profile face perpendicular to the axis (Z)
  QString ef;
  double A = 0;
  for (const auto& [n, f] : w.modelResult().faces) {
    if (n.rfind("Cup_Don.rev.", 0) != 0) continue;
    BRepAdaptor_Surface s(f, Standard_False);
    if (s.GetType() != GeomAbs_Plane || std::fabs(std::fabs(s.Plane().Axis().Direction().Z()) - 1) > 1e-9) continue;
    GProp_GProps g;
    BRepGProp::SurfaceProperties(f, g);
    if (g.Mass() > A) { A = g.Mass(); ef = QString::fromStdString(n); }
  }
  check(!ef.isEmpty() && A > 100, QString("end face %1, area %2 mm²").arg(ef).arg(A, 0, 'f', 1));
  const double V0 = volume(w);
  const size_t nf0 = w.document().features.size();
  v->setFocus();
  key(w, Qt::Key_S);
  v->setView(pc::viewer::ShapeViewer::View::Bottom);
  v->fitAll();
  pump(300);
  w.selection()->clear();
  const QPointF fp = facePx(w, ef);
  check(fp.x() >= 0, "end face pickable from below");
  vclick(w, fp);
  check(w.selection()->contains(SelItem{SelItem::Kind::Face, ef}), "click selects the end face");
  key(w, Qt::Key_P);
  check(tm->activeId() == "pull", "P: Çek");
  auto* pt = qobject_cast<pcd::PullTool*>(tm->active());
  if (!pt) { check(false, "Pull tool active"); return; }
  check(pt->target().kind == pcd::pull::Kind::FaceExtrude && pt->target().valid(),
        "end face -> FaceExtrude (was RevolveAngle): " + pt->target().refusal);
  check(pt->target().fieldLabel == "Mesafe", "Pull field label is 'Mesafe' (not 'Açı'): " + pt->target().fieldLabel);
  check(!msg(w).contains("Açı") && !msg(w).contains("angle"), "status prompt does not offer the angle: " + msg(w).left(100));
  // drag the arrow outward (as in the video)
  const QPointF b = pt->handleBase(), t = pt->handleTip();
  QPointF u = t - b;
  const double ul = std::hypot(u.x(), u.y());
  check(ul > 1, "pull arrow drawn on the end face");
  if (ul <= 1) return;
  u /= ul;
  const QPoint from = (b + u * 31).toPoint(), to = (b + u * 131).toPoint();
  QTest::mousePress(v, Qt::LeftButton, Qt::NoModifier, from);
  QString boxLabel;
  bool ghost = false;
  for (int i = 1; i <= 10; ++i) {
    const QPoint p = from + (to - from) * i / 10;
    QMouseEvent mv(QEvent::MouseMove, QPointF(p), v->mapToGlobal(QPointF(p)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(v, &mv);
    pump(30);
    if (v->property("ghostVisible").toBool()) ghost = true;
  }
  if (auto* fw = pt->dimensionBox() ? pt->dimensionBox()->fieldWidget(0) : nullptr)
    for (auto* l : fw->findChildren<QLabel*>()) boxLabel += l->text();
  const double dv = pt->dragValue();
  shot(w, "pullfix_swc_drag.png");
  QTest::mouseRelease(v, Qt::LeftButton, Qt::NoModifier, to);
  check(dv > 0, QString("dragging outward gives a positive distance (%1 mm)").arg(dv));
  check(boxLabel.contains("Mesafe") && !boxLabel.contains("Açı"), "value box label while dragging: '" + boxLabel + "'");
  check(ghost, "ghost preview (extruded face) while dragging");
  check(w.document().feature("Cup_Don")->prop("angle") == "360", "nothing changed the revolve angle during the drag");
  // type the exact value before the 600 ms auto-commit: 5 Enter
  const int u0 = undo->count();
  type(w, "5");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().features.size() == nf0 + 2; }, 6000);
  pump(200);
  const pc::Feature* ex = lastFeature(w, pc::FeatureType::Extrude);
  const pc::Feature* sk = lastFeature(w, pc::FeatureType::Sketch);
  check(w.document().features.size() == nf0 + 2 && ex && sk && ex->prop("sketch") == sk->id,
        "one new Sketch (on the face) + one new Extrude");
  check(ex && ex->prop("distance") == "5" && ex->prop("op") == "join" && ex->prop("pullFace") == ef.toStdString(),
        QString("Extrude distance=5, op=join, pullFace=%1").arg(ex ? QString::fromStdString(ex->prop("pullFace")) : "-"));
  check(sk && sk->sketch && sk->sketch->plane.kind == pc::SketchPlaneKind::OnFace && sk->sketch->plane.faceRef == ef.toStdString(),
        "the sketch lies on the pulled face");
  check(w.document().feature("Cup_Don")->prop("angle") == "360", "Revolve angle stays 360°");
  check(w.modelResult().bodies.size() == 1 && w.modelResult().ok, "still one body, model ok");
  check(std::fabs(volume(w) - (V0 + 5 * A)) < 1e-3 * V0, QString("volume %1 -> %2 (= +5 x %3)").arg(V0, 0, 'f', 1).arg(volume(w), 0, 'f', 1).arg(A, 0, 'f', 1));
  check(undo->count() == u0 + 1, QString("one undo step (%1 -> %2)").arg(u0).arg(undo->count()));
  check(ex && w.selection()->contains(SelItem{SelItem::Kind::Face, QString::fromStdString(ex->id) + ".end"}),
        "the moved face (new end face) stays selected");
  shot(w, "pullfix_swc_extended.png");
  // pulling the moved face again edits the same Extrude (no second feature)
  const std::string exId = ex ? ex->id : std::string("?");
  auto exDist = [&] { const pc::Feature* f = w.document().feature(exId); return f ? f->prop("distance") : std::string(); };
  type(w, "2");
  key(w, Qt::Key_Return);
  waitFor([&] { return exDist() == "7"; }, 5000);
  check(w.document().features.size() == nf0 + 2 && exDist() == "7", "pulling the new end face by 2: the same Extrude, distance 7");
  // undo both -> the original part
  undo->undo();
  undo->undo();
  pump(300);
  check(w.document().features.size() == nf0 && std::fabs(volume(w) - V0) < 1e-6 * V0, "Ctrl+Z twice: original swirl cup");
  // -2 cuts
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  w.selection()->clear();
  vclick(w, facePx(w, ef));
  key(w, Qt::Key_P);
  pt = qobject_cast<pcd::PullTool*>(tm->active());
  type(w, "-2");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().features.size() == nf0 + 2; }, 6000);
  pump(200);
  ex = lastFeature(w, pc::FeatureType::Extrude);
  check(ex && ex->prop("op") == "cut" && std::fabs(volume(w) - (V0 - 2 * A)) < 1e-3 * V0,
        QString("-2 Enter cuts: volume %1 (= -2 x area)").arg(volume(w), 0, 'f', 1));
  check(w.document().feature("Cup_Don")->prop("angle") == "360" && w.modelResult().bodies.size() == 1, "angle 360, one body");
  undo->undo();
  pump(300);
  // Birleştirme yok (No Merge) -> a new body
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  w.selection()->clear();
  vclick(w, facePx(w, ef));
  key(w, Qt::Key_P);
  tm->setOption("merge", "nomerge");
  type(w, "5");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().features.size() == nf0 + 2; }, 6000);
  pump(200);
  check(w.modelResult().bodies.size() == 2 && std::fabs(volume(w) - (V0 + 5 * A)) < 1e-3 * V0,
        QString("'Birleştirme yok': 2 bodies, volume %1").arg(volume(w), 0, 'f', 1));
  tm->setOption("merge", "auto");
  // a cylindrical revolve face offsets the radius, a cone is refused — never the angle
  undo->undo();
  pump(300);
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  w.selection()->clear();
  w.selection()->set({SelItem{SelItem::Kind::Face, "Cup_Don.rev.e62"}});
  key(w, Qt::Key_P);
  pt = qobject_cast<pcd::PullTool*>(tm->active());
  check(pt && pt->target().kind == pcd::pull::Kind::Unsupported && msg(w).contains("açısı değiştirilmez"),
        "conical face: clear message, no angle edit: " + msg(w).left(110));
  type(w, "3");
  pump(100);
  check(w.document().features.size() == nf0 && w.document().feature("Cup_Don")->prop("angle") == "360", "typing on the cone changes nothing");
  key(w, Qt::Key_Escape);
}

} // namespace

int main(int argc, char** argv) {
  QSurfaceFormat fmt;
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CoreProfile);
  fmt.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(fmt);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);  // Windows: native file dialogs cannot be driven by the harness
  QApplication app(argc, argv);
  QApplication::setApplicationName("pc_gui_harness_sc_e2e");   // no layout persistence
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();
  QTemporaryDir tmp;
  pcd::MainWindow w;
  g_w = &w;
  w.show();
  QTest::qWaitForWindowExposed(&w);
  w.activateWindow();
  QApplication::setActiveWindow(&w);
  QTest::qWaitForWindowActive(&w, 3000);
  pump(600);
  run(w, tmp.path());
  swirlCupPull(w);
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  std::fflush(stdout);
  w.setAutoYes(true);   // no "save changes?" on exit
  return g_fail;
}
