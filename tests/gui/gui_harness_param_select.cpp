// "Seçimden parametre" GUI harness — the REAL MainWindow driven with real clicks / keys (QTest):
//
//   sketch: rectangle 60x40 -> Seç -> click the bottom line -> badge "Uzunluk = 60 mm" + P -> quick popup suggests
//   "W" -> W, Enter -> W = 60 in Gruplar, bottom Length dimension = W (driven) -> Ctrl+Z removes parameter + binding in
//   ONE step, Ctrl+Y restores -> W = 80 in Gruplar -> the sketch follows; circle (two clicks, no dimension) -> badge
//   "Ø 20" (Ø⇄R toggle) -> P -> D1 (Diameter dimension ADDED); arc -> right click -> "Parametre olarak ekle ▸
//   Yarıçap" -> R1; two small circles (Ctrl+click) -> Ctrl+G -> ONE parameter DH bound to both; the height dimension
//   label -> P -> H; top line (redundant) -> conflict box -> "Referans parametre" (never silently fails); Properties
//   "Seçimden parametre" rows; D -> parameters survive closing the sketch.
//   3D: Pull the sketch 10 -> extrude end face -> T (Extrude1.distance = T); cylindrical hole wall -> Ø -> DB (sketch
//   Diameter dimension added in Sketch1, one undo step: Ctrl+Z / Ctrl+Y); top edge -> length shows W; DB edited in
//   Gruplar -> model follows; save / reopen keeps everything; Save as App lists the exposed parameters and the
//   generated app's --validate-only reports them.
//
//   pc_gui_harness_param_select [--shots]     --shots writes docs/param_select/*.png
//   Exit code = number of failed checks.
#include "MainWindow.h"
#include "ParamSelect.h"
#include "ParamSelectUi.h"
#include "PropertiesPanel.h"
#include "SketchEditor.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "harness_robust.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/ShapeViewer.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
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
#include <QUndoStack>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <functional>

#ifndef PC_PS_SHOTS_DIR
#define PC_PS_SHOTS_DIR "docs/param_select"
#endif

using pcd::SelItem;
using pc::SkConstraintType;

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
template <class T> T* childWithText(QWidget* root, const QString& text) {
  for (T* c : root->findChildren<T*>()) if (c->text() == text || c->text().remove('&') == text) return c;
  return nullptr;
}

// ---- modal pump: the conflict box and file / app dialogs go to g_onModal; other message boxes are answered Yes / OK
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || !m->isVisible()) return;
    if (pch::enforceDeadline(m)) return;
    if (m->property("psHandled").toBool()) return;
    if (g_onModal && (m->objectName() == "paramConflictBox" || !qobject_cast<QMessageBox*>(m))) {
      m->setProperty("psHandled", true);
      g_onModal(m);
      return;
    }
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      m->setProperty("psHandled", true);
      g_boxes << mb->text();
      note("message box: " + mb->text().left(140).replace('\n', ' '));
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b) b = mb->button(QMessageBox::Save);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      pch::pressLater(b, 50);
      return;
    }
    if (m->inherits("pcd::ChangeReviewDialog")) {
      m->setProperty("psHandled", true);
      note("manufacturing review dialog -> 'Yine de Uygula'");
      if (auto* b = childWithText<QPushButton>(m, "Yine de Uygula")) pch::pressLater(b, 50);
      else if (auto* d = qobject_cast<QDialog*>(m)) QTimer::singleShot(50, d, &QDialog::accept);
    }
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
  for (const QChar c : s) { QTest::keyClick(keyTarget(w), c.toLatin1()); pump(15); }
  pump(30);
}
void vmove(pcd::MainWindow& w, const QPointF& p) {
  QWidget* t = inputTarget(w);
  QMouseEvent mv(QEvent::MouseMove, p, t->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(20);
}
void vclick(pcd::MainWindow& w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier, Qt::MouseButton b = Qt::LeftButton) {
  ensureActive(w);
  QWidget* t = inputTarget(w);
  vmove(w, p);
  QTest::mouseClick(t, b, m, p.toPoint());
  pump(90);
}
void clickButton(QAbstractButton* b) {
  if (!b) return;
  QTest::mouseClick(b, Qt::LeftButton, {}, b->rect().center());
  pump(120);
}
void shot(pcd::MainWindow& w, const QString& name) {
  if (!g_shots) return;
  pump(250);
  QDir().mkpath(PC_PS_SHOTS_DIR);
  const QString path = QString(PC_PS_SHOTS_DIR) + "/" + name;
  // modal boxes / popup menus are separate windows: take them from the screen so that they are in the picture
  QPixmap px;
  if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
    if (QScreen* sc = QGuiApplication::primaryScreen()) px = sc->grabWindow(0).copy(w.frameGeometry());
  }
  if (px.isNull()) px = w.grab();
  note(QString("shot %1 %2").arg(path, px.save(path) ? "saved" : "FAILED"));
}

// ---- queries
pcd::ParamSelectController* ps(pcd::MainWindow& w) { return w.paramSelect(); }
QFrame* badge(pcd::MainWindow& w) {
  for (auto* f : w.viewer()->findChildren<QFrame*>("paramBadge")) if (f->isVisible()) return f;
  return nullptr;
}
QString badgeText(pcd::MainWindow& w) {
  QFrame* b = badge(w);
  if (!b) return QString();
  QStringList l;
  for (auto* lb : b->findChildren<QLabel*>()) l << lb->text();
  return l.join(" | ");
}
QToolButton* badgeP(pcd::MainWindow& w, const QString& name = "paramBadge.P") {
  QFrame* b = badge(w);
  return b ? b->findChild<QToolButton*>(name) : nullptr;
}
pcd::QuickParamPopup* popup(pcd::MainWindow& w) {
  for (auto* p : w.viewer()->findChildren<pcd::QuickParamPopup*>("quickParamPopup")) if (p->isVisible()) return p;
  return nullptr;
}
const pc::Parameter* param(pcd::MainWindow& w, const char* n) { return w.document().params.find(n); }
std::string pexpr(pcd::MainWindow& w, const char* n) {
  const auto* p = param(w, n);
  return p ? p->expression : std::string("<missing>");
}
QStandardItemModel* paramModel(pcd::MainWindow& w) { return qobject_cast<QStandardItemModel*>(w.parameterTable()->model()); }
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = paramModel(w);
  for (int r = 0; m && r < m->rowCount(); ++r) if (m->item(r, 0) && m->item(r, 0)->text() == name) return r;
  return -1;
}
const pc::SkConstraint* dimOn(const pc::Sketch& sk, SkConstraintType t, int ref) {
  for (const auto& c : sk.constraints) if (c.type == t && !c.refs.empty() && c.refs[0] == ref) return &c;
  return nullptr;
}
QString dimExpr(const pc::Sketch& sk, SkConstraintType t, int ref) {
  const pc::SkConstraint* c = dimOn(sk, t, ref);
  return c ? QString::fromStdString(c->valueExpr) : QString("<none>");
}
double lineLen(const pc::Sketch& sk, int id) {
  const pc::SkEntity* e = sk.entity(id);
  if (!e) return -1;
  const pc::SkPoint *a = sk.point(e->points[0]), *b = sk.point(e->points[1]);
  return std::hypot(b->x - a->x, b->y - a->y);
}
// line whose midpoint is nearest to (u, v)
int lineNear(const pc::Sketch& sk, double u, double v) {
  int best = -1;
  double bd = 1e9;
  for (const auto& e : sk.entities) {
    if (e.type != pc::SkEntityType::Line) continue;
    const pc::SkPoint *a = sk.point(e.points[0]), *b = sk.point(e.points[1]);
    const double d = std::hypot((a->x + b->x) / 2 - u, (a->y + b->y) / 2 - v);
    if (d < bd) { bd = d; best = e.id; }
  }
  return best;
}
std::vector<int> entitiesOf(const pc::Sketch& sk, pc::SkEntityType t) {
  std::vector<int> r;
  for (const auto& e : sk.entities) if (e.type == t) r.push_back(e.id);
  return r;
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
int runProc(const QString& prog, const QStringList& args, QByteArray* out) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(120000)) { p.kill(); return -100; }
  if (out) *out = p.readAllStandardOutput();
  return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
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
// Edits a Gruplar expression cell like a user (double click, type, Enter).
bool editParamCell(pcd::MainWindow& w, const QString& name, const QString& expr) {
  w.structurePanel()->setCurrentPage("groups");
  pump(100);
  auto* pv = w.parameterTable();
  const int row = paramRow(w, name);
  if (row < 0) return false;
  const QModelIndex idx = pv->model()->index(row, 1);
  pv->scrollTo(idx);
  const QRect r = pv->visualRect(idx);
  QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, r.center());
  QTest::mouseDClick(pv->viewport(), Qt::LeftButton, {}, r.center());
  pump(150);
  QLineEdit* ce = pv->viewport()->findChild<QLineEdit*>();
  if (!ce) { note("double click did not open the cell editor; QAbstractItemView::edit() used"); pv->edit(idx); pump(100); ce = pv->viewport()->findChild<QLineEdit*>(); }
  if (!ce) return false;
  ce->selectAll();
  QTest::keyClicks(ce, expr);
  QTest::keyClick(ce, Qt::Key_Return);
  pump(100);
  return true;
}
// P on the badge -> popup; name typed; Enter. Returns false if the popup did not open.
bool makeParam(pcd::MainWindow& w, QToolButton* p, const QString& name, QString* suggested = nullptr, const QString& shotName = QString()) {
  if (!p) return false;
  clickButton(p);
  waitFor([&] { return popup(w) != nullptr; }, 2000);
  pcd::QuickParamPopup* pp = popup(w);
  if (!pp) return false;
  if (suggested) *suggested = pp->nameEdit()->text();
  pch::typeInto(pp->nameEdit(), name, "quickParam.name");
  if (!shotName.isEmpty()) shot(w, shotName);
  QTest::keyClick(pp->nameEdit(), Qt::Key_Return);
  pump(250);
  return true;
}

// ============================================================================ the flow
void run(pcd::MainWindow& w, const QString& tmpDir) {
  auto* tm = w.toolManager();
  auto* ed = w.sketchEditor();
  auto* v = w.viewer();
  auto* undo = w.undoStack();
  check(ps(w) != nullptr, "MainWindow has the Seçimden parametre controller");

  // ---------------------------------------------------------------- rectangle
  section("sketch: rectangle 60 x 40, Seç, click the bottom line -> badge with the length and P");
  v->setFocus();
  key(w, Qt::Key_N, Qt::ControlModifier);
  key(w, Qt::Key_K);
  check(w.inSketchMode(), "K: sketch mode");
  key(w, Qt::Key_R);
  vclick(w, ed->sketchToScreen(0, 0));
  type(w, "60");
  key(w, Qt::Key_Tab);
  type(w, "40");
  key(w, Qt::Key_Return);
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  check(tm->activeId() == "sketch.select" && ed->tool() == pcd::SketchEditor::Tool::Select, "S: Seç in the sketch");
  ed->fitView();
  pump(100);
  const int bottom = lineNear(ed->sketch(), 30, 0), right = lineNear(ed->sketch(), 60, 20), top = lineNear(ed->sketch(), 30, 40);
  check(bottom > 0 && std::fabs(lineLen(ed->sketch(), bottom) - 60) < 1e-6, "rectangle drawn (bottom line 60)");
  vclick(w, ed->sketchToScreen(22, 0));
  check(ed->selectedIds() == std::vector<int>{bottom}, "click selects the bottom line");
  waitFor([&] { return badge(w) != nullptr; }, 1500);
  check(badge(w) && badgeText(w).contains("Uzunluk") && badgeText(w).contains("60 mm"), "badge next to the line: " + badgeText(w));
  check(badgeP(w) && badgeP(w)->isEnabled(), "badge has an enabled P button");
  {
    QPointF a;
    ed->selectionAnchor(&a);
    const QRect br = badge(w) ? badge(w)->geometry() : QRect();
    check(br.isValid() && QLineF(br.center(), a).length() < 220, QString("badge is placed next to the selection (%1 px)").arg(QLineF(br.center(), a).length(), 0, 'f', 0));
  }
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
  waitFor([&] { return w.propertiesPanel()->findChild<QPushButton*>("prop.param.L") != nullptr; }, 1500);
  auto* propBtn = w.propertiesPanel()->findChild<QPushButton*>("prop.param.L");
  check(propBtn && propBtn->text() == "Parametre yap" && w.propertiesPanel()->rowLabels().contains("Uzunluk"),
        "Özellikler: 'Seçimden parametre' row 'Uzunluk' with 'Parametre yap'");
  shot(w, "01_badge_line.png");

  // ---------------------------------------------------------------- P -> W
  section("P -> quick popup (suggests W), name W, Enter -> W = 60, bottom dimension driven by W");
  const int u0 = undo->count();
  QString sug;
  check(makeParam(w, badgeP(w), "W", &sug, "02_popup_W.png"), "P opens the inline popup (no dialog)");
  check(!QApplication::activeModalWidget(), "no modal dialog was used");
  check(sug == "W", "popup suggests the rectangle side name 'W': " + sug);
  check(popup(w) == nullptr, "Enter closes the popup");
  const pc::Parameter* W = param(w, "W");
  check(W && W->expression == "60" && W->exposed, "W = 60 created ('Uygulamada göster' on by default)");
  check(dimExpr(ed->sketch(), SkConstraintType::Length, bottom) == "W", "bottom Length dimension = W: " + dimExpr(ed->sketch(), SkConstraintType::Length, bottom));
  check(paramRow(w, "W") >= 0 && w.structurePanel()->currentPageKey() == "groups", "W listed in Gruplar (shown at once)");
  check(undo->count() == u0 + 1, "the parameter is one document commit");
  check(badgeText(w).contains("W"), "badge now shows the binding: " + badgeText(w));
  shot(w, "03_W_in_groups.png");

  section("Ctrl+Z / Ctrl+Y in the sketch: parameter + binding as ONE step");
  ed->setFocus();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return param(w, "W") == nullptr; }, 2000);
  check(!param(w, "W") && dimExpr(ed->sketch(), SkConstraintType::Length, bottom) == "60", "Ctrl+Z: W removed and the dimension is 60 again");
  check(paramRow(w, "W") < 0, "Gruplar no longer lists W");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return param(w, "W") != nullptr; }, 2000);
  check(param(w, "W") && dimExpr(ed->sketch(), SkConstraintType::Length, bottom) == "W", "Ctrl+Y: W and its binding back");

  section("W = 80 in Gruplar -> the sketch follows");
  check(editParamCell(w, "W", "80"), "W cell edited");
  waitFor([&] { return pexpr(w, "W") == "80"; }, 4000);
  pump(200);
  check(pexpr(w, "W") == "80" && std::fabs(lineLen(ed->sketch(), bottom) - 80) < 1e-6 && std::fabs(lineLen(ed->sketch(), top) - 80) < 1e-6,
        QString("bottom / top lines are 80 now (%1)").arg(lineLen(ed->sketch(), bottom)));
  ed->fitView();
  pump(100);

  // ---------------------------------------------------------------- circle -> D1
  section("circle (no dimension) -> badge Ø 20, Ø⇄R toggle, P -> D1 adds the Diameter dimension");
  ed->setFocus();
  key(w, Qt::Key_C);
  vclick(w, ed->sketchToScreen(110, 20));
  vclick(w, ed->sketchToScreen(120, 20));
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  std::vector<int> circles = entitiesOf(ed->sketch(), pc::SkEntityType::Circle);
  const int big = circles.empty() ? -1 : circles.back();
  check(big > 0 && dimExpr(ed->sketch(), SkConstraintType::Diameter, big) == "<none>", "circle r=10 drawn without a dimension");
  ed->fitView();
  pump(100);
  vclick(w, ed->sketchToScreen(120, 20));
  check(ed->selectedIds() == std::vector<int>{big}, "click on the rim selects the circle");
  check(badgeText(w).contains(QString::fromUtf8("Ø 20 mm")), "badge: " + badgeText(w));
  shot(w, "04_badge_circle.png");
  clickButton(badge(w) ? badge(w)->findChild<QToolButton*>("paramBadge.toggle") : nullptr);
  check(badgeText(w).contains("Yarıçap = 10 mm"), "Ø⇄R toggles to the radius: " + badgeText(w));
  clickButton(badge(w) ? badge(w)->findChild<QToolButton*>("paramBadge.toggle") : nullptr);
  check(badgeText(w).contains(QString::fromUtf8("Ø 20")), "and back to Ø");
  check(makeParam(w, badgeP(w), "D1", &sug), "P -> popup");
  check(sug == "D1", "suggested name D1: " + sug);
  check(pexpr(w, "D1") == "20" && dimExpr(ed->sketch(), SkConstraintType::Diameter, big) == "D1", "D1 = 20, Diameter dimension added = D1");

  // ---------------------------------------------------------------- arc -> R1 (context menu)
  section("arc -> right click 'Parametre olarak ekle > Yarıçap' -> R1");
  if (QAction* a = tm->action("sketch.arc")) { note("Yay: ribbon action triggered"); a->trigger(); }
  pump(60);
  vclick(w, ed->sketchToScreen(110, 60));
  vclick(w, ed->sketchToScreen(125, 60));
  vclick(w, ed->sketchToScreen(110, 75));
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  const std::vector<int> arcs = entitiesOf(ed->sketch(), pc::SkEntityType::Arc);
  const int arc = arcs.empty() ? -1 : arcs.back();
  check(arc > 0, "arc drawn");
  ed->fitView();
  pump(100);
  const QPointF arcMid = ed->sketchToScreen(110 + 15 * std::cos(M_PI / 4), 60 + 15 * std::sin(M_PI / 4));
  vclick(w, arcMid, Qt::NoModifier, Qt::RightButton);
  waitFor([&] { return ps(w)->lastMenu() && ps(w)->lastMenu()->isVisible(); }, 1500);
  QMenu* menu = ps(w)->lastMenu();
  check(menu && ed->selectedIds() == std::vector<int>{arc}, "right click selects the arc and opens the context menu");
  check(badgeText(w).contains("Yarıçap = 15 mm") && badgeText(w).contains("Yay açısı = 90"), "badge: radius + sweep angle: " + badgeText(w));
  QMenu* sub = menu ? menu->findChild<QMenu*>("menuAddParam") : nullptr;
  QAction* actR = nullptr;
  if (sub) for (QAction* a : sub->actions()) if (a->objectName() == "addParam.R") actR = a;
  QAction* actSweep = nullptr;
  if (sub) for (QAction* a : sub->actions()) if (a->objectName() == "addParam.sweep") actSweep = a;
  check(sub && actR && actR->text().startsWith("Yarıçap (15 mm)"), "menu 'Parametre olarak ekle > Yarıçap (15 mm)'");
  check(actSweep && !actSweep->isEnabled(), "sweep angle listed but not parametrizable (explained)");
  if (menu && sub && actR) {
    // real keyboard navigation would be platform dependent: open the submenu and trigger the item
    menu->setActiveAction(sub->menuAction());
    pump(150);
    sub->setActiveAction(actR);
    pump(100);
    shot(w, "05_context_menu_arc.png");
    actR->trigger();
    menu->close();
  }
  waitFor([&] { return popup(w) != nullptr; }, 2000);
  if (auto* pp = popup(w)) {
    check(pp->nameEdit()->text() == "R1", "popup suggests R1: " + pp->nameEdit()->text());
    QTest::keyClick(pp->nameEdit(), Qt::Key_Return);
    pump(250);
  } else check(false, "popup after the menu item");
  check(pexpr(w, "R1") == "15" && dimExpr(ed->sketch(), SkConstraintType::Radius, arc) == "R1", "R1 = 15, arc Radius = R1");
  // the open arc would make the profile invalid for the 3D part: turn it into construction geometry
  vclick(w, arcMid);
  if (QAction* c = tm->action("sketch.construction")) {
    c->setChecked(true);
    pump(60);
    ed->clearSelection();
    c->setChecked(false);
  }
  check(ed->sketch().entity(arc) && ed->sketch().entity(arc)->construction, "arc is construction geometry (Yapı çizgisi)");

  // ---------------------------------------------------------------- two holes -> one parameter (Ctrl+G)
  section("two small circles, Ctrl+click both -> Ctrl+G -> ONE parameter DH for both diameters");
  key(w, Qt::Key_C);
  vclick(w, ed->sketchToScreen(20, 20));
  vclick(w, ed->sketchToScreen(24, 20));
  vclick(w, ed->sketchToScreen(60, 20));
  vclick(w, ed->sketchToScreen(64, 20));
  vclick(w, ed->sketchToScreen(40, 20));     // third circle: stays unparametrized (3D hole test)
  vclick(w, ed->sketchToScreen(43, 20));
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  circles = entitiesOf(ed->sketch(), pc::SkEntityType::Circle);
  check(circles.size() == 4, QString("4 circles in the sketch (%1)").arg(circles.size()));
  int h1 = -1, h2 = -1, h3 = -1;
  for (int id : circles) {
    const pc::SkEntity* e = ed->sketch().entity(id);
    const pc::SkPoint* c = ed->sketch().point(e->points[0]);
    if (std::fabs(c->x - 20) < 1e-6) h1 = id;
    if (std::fabs(c->x - 60) < 1e-6) h2 = id;
    if (std::fabs(c->x - 40) < 1e-6) h3 = id;
  }
  check(h1 > 0 && h2 > 0 && h3 > 0, "hole circles found");
  ed->fitView();
  pump(100);
  vclick(w, ed->sketchToScreen(24, 20));
  vclick(w, ed->sketchToScreen(64, 20), Qt::ControlModifier);
  check(ed->selectedIds().size() == 2, "Ctrl+click: two circles selected");
  const double dh = 2 * ed->sketch().entity(h1)->radius;
  const QString dhs = QString::fromStdString(pc::formatNumber(dh));
  check(badgeText(w).contains(QString::fromUtf8("Ø %1 mm").arg(dhs)) && badgeText(w).contains("2 eğri"), "badge: one diameter for both: " + badgeText(w));
  shot(w, "06_two_holes.png");
  ed->setFocus();
  key(w, Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return popup(w) != nullptr; }, 2000);
  check(popup(w) != nullptr, "Ctrl+G opens the popup for the selected diameters");
  if (auto* pp = popup(w)) {
    pch::typeInto(pp->nameEdit(), "DH", "quickParam.name");
    QTest::keyClick(pp->nameEdit(), Qt::Key_Return);
    pump(250);
  }
  check(pexpr(w, "DH") == dhs.toStdString() && dimExpr(ed->sketch(), SkConstraintType::Diameter, h1) == "DH" &&
            dimExpr(ed->sketch(), SkConstraintType::Diameter, h2) == "DH",
        "DH = " + dhs + " drives both hole diameters (the second circle " + QString::fromStdString(pc::formatNumber(2 * ed->sketch().entity(h2)->radius)) + " now too)");
  check(editParamCell(w, "DH", "9"), "DH = 9 in Gruplar");
  waitFor([&] { return pexpr(w, "DH") == "9"; }, 3000);
  pump(150);
  check(std::fabs(ed->sketch().entity(h1)->radius - 4.5) < 1e-6 && std::fabs(ed->sketch().entity(h2)->radius - 4.5) < 1e-6, "both holes follow DH = 9");

  // ---------------------------------------------------------------- dimension label -> H
  section("click the height dimension label -> P -> H");
  ed->setFocus();
  ed->clearSelection();
  ed->fitView();
  pump(200);
  const pc::SkConstraint* hdim = nullptr;
  for (const auto& c : ed->sketch().constraints) if (c.type == SkConstraintType::Length && c.valueExpr == "40") hdim = &c;
  const int hline = hdim ? hdim->refs[0] : right;
  QRectF lr = hdim ? ed->dimensionLabelRect(hdim->id) : QRectF();
  check(hdim && !lr.isEmpty(), "height dimension label painted");
  if (!lr.isEmpty()) vclick(w, lr.center());
  check(hdim && ed->selectedIds() == std::vector<int>{hdim->id}, "click on the label selects the dimension");
  check(badgeText(w).contains("Uzunluk = 40 mm") && badge(w) && badge(w)->findChild<QToolButton*>("paramBadge.bind"),
        "badge for the label (+ bind to an existing parameter): " + badgeText(w));
  check(makeParam(w, badgeP(w), "H", &sug), "P on the label badge");
  check(sug == "H", "suggests H for the vertical side: " + sug);
  check(pexpr(w, "H") == "40" && dimExpr(ed->sketch(), SkConstraintType::Length, hline) == "H", "H = 40 drives the existing height dimension");

  // ---------------------------------------------------------------- over-constraint
  section("top line (already fixed by W) -> conflict box: 'Referans parametre' (never silent)");
  vclick(w, ed->sketchToScreen(30, 40));
  check(ed->selectedIds() == std::vector<int>{top}, "top line selected");
  bool boxSeen = false;
  QString boxText;
  g_onModal = [&](QWidget* m) {
    if (m->objectName() != "paramConflictBox") return;
    boxSeen = true;
    if (auto* mb = qobject_cast<QMessageBox*>(m)) boxText = mb->text();
    shot(w, "07_conflict.png");
    pch::pressLater(m->findChild<QAbstractButton*>("conflict.reference"), 80);
  };
  makeParam(w, badgeP(w), "Wt");
  waitFor([&] { return boxSeen && !QApplication::activeModalWidget(); }, 4000);
  g_onModal = nullptr;
  pump(200);
  check(boxSeen && boxText.contains("aşırı kısıt") && boxText.contains("Uzunluk W"), "conflict box names the conflicting constraint: " + boxText.left(160).replace('\n', ' '));
  check(param(w, "Wt") && dimExpr(ed->sketch(), SkConstraintType::Length, top) == "<none>" && pexpr(w, "W") == "80",
        "reference parameter Wt created, sketch unchanged, W still drives");

  section("other vertical side -> conflict box: 'Çakışanı kaldır ve bağla' replaces H by H2; Ctrl+Z restores (one step)");
  {
    const int vl = lineNear(ed->sketch(), 0, 20), vr = lineNear(ed->sketch(), 80, 20);
    const int other = vl == hline ? vr : vl;
    const QPointF op = other == vl ? ed->sketchToScreen(0, 12) : ed->sketchToScreen(80, 12);
    vclick(w, op);
    check(ed->selectedIds() == std::vector<int>{other}, "the other vertical side selected");
    bool seen = false;
    QString txt;
    g_onModal = [&](QWidget* m) {
      if (m->objectName() != "paramConflictBox") return;
      seen = true;
      if (auto* mb = qobject_cast<QMessageBox*>(m)) txt = mb->text();
      pch::pressLater(m->findChild<QAbstractButton*>("conflict.replace"), 80);
    };
    makeParam(w, badgeP(w), "H2");
    waitFor([&] { return seen && !QApplication::activeModalWidget(); }, 4000);
    g_onModal = nullptr;
    pump(200);
    check(seen && txt.contains("Uzunluk H"), "conflict names the H dimension first: " + txt.section('\n', 1, 1));
    check(pexpr(w, "H2") == "40" && dimExpr(ed->sketch(), SkConstraintType::Length, other) == "H2" &&
              dimExpr(ed->sketch(), SkConstraintType::Length, hline) == "<none>",
          "replace: H2 drives this side, the H dimension was removed (only that one)");
    int vertical = 0;
    for (const auto& c : ed->sketch().constraints) if (c.type == SkConstraintType::Vertical) ++vertical;
    check(vertical == 2, QString("the Vertical constraints are kept (%1)").arg(vertical));
    ed->setFocus();
    key(w, Qt::Key_Z, Qt::ControlModifier);
    waitFor([&] { return param(w, "H2") == nullptr; }, 2000);
    check(!param(w, "H2") && dimExpr(ed->sketch(), SkConstraintType::Length, hline) == "H" &&
              dimExpr(ed->sketch(), SkConstraintType::Length, other) == "<none>",
          "Ctrl+Z: H2 gone, H dimension back (one step)");
    ed->clearSelection();
  }

  // ---------------------------------------------------------------- leave the sketch
  section("D: the parameters and bindings survive closing the sketch");
  key(w, Qt::Key_D);
  waitFor([&] { return !w.inSketchMode(); }, 3000);
  const pc::Feature* s1 = w.document().feature("Sketch1");
  check(!w.inSketchMode() && s1 && s1->sketch, "Sketch1 committed");
  if (s1 && s1->sketch) {
    const pc::Sketch& sk = *s1->sketch;
    check(dimExpr(sk, SkConstraintType::Length, bottom) == "W" && dimExpr(sk, SkConstraintType::Diameter, big) == "D1" &&
              dimExpr(sk, SkConstraintType::Radius, arc) == "R1" && dimExpr(sk, SkConstraintType::Diameter, h1) == "DH" &&
              dimExpr(sk, SkConstraintType::Length, hline) == "H",
          "Sketch1 dimensions: W, D1, R1, DH (x2), H");
  }
  for (const char* n : {"W", "D1", "R1", "DH", "H", "Wt"}) check(paramRow(w, n) >= 0, QString("Gruplar lists %1").arg(n));

  // ---------------------------------------------------------------- 3D
  section("3D: Pull the sketch 10, end face -> T (Extrude1.distance = T)");
  w.structurePanel()->setCurrentPage("structure");
  key(w, Qt::Key_H);
  pump(200);
  key(w, Qt::Key_P);
  const QPointF reg = facePx(w, "sketch:Sketch1:r0");
  check(reg.x() >= 0, "sketch region pickable");
  vclick(w, reg);
  type(w, "10");
  key(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 6000);
  check(w.document().feature("Extrude1") != nullptr, "Extrude1 (10)");
  key(w, Qt::Key_Escape);
  key(w, Qt::Key_S);
  key(w, Qt::Key_H);
  pump(200);
  check(tm->activeId() == "select", "Seç in 3D");
  w.selection()->clear();
  QString endFace;
  QPointF endPx(-1, -1);
  {   // the separate Ø circle splits the end face into pieces: take the rectangle's piece
    const double P[3] = {6, 34, 10};
    endPx = v->worldToScreen(P);
    endFace = v->pickAt(endPx.toPoint());
  }
  check(endFace.startsWith("Extrude1.end"), "end face of the rectangle under (6, 34, 10): " + endFace);
  vclick(w, endPx);
  check(w.selection()->contains(SelItem{SelItem::Kind::Face, endFace}), "end face selected");
  check(badgeText(w).contains("Mesafe = 10 mm"), "3D badge: " + badgeText(w));
  shot(w, "08_badge_3d_face.png");
  check(makeParam(w, badgeP(w), "T", &sug), "P on the 3D badge");
  check(sug == "T1", "suggested T1: " + sug);
  check(pexpr(w, "T") == "10" && w.document().feature("Extrude1")->prop("distance") == "T", "Extrude1.distance = T (10)");

  section("3D: cylindrical hole wall (no dimension) -> Ø 6 -> DB adds Sketch1 Diameter, one undo step");
  const QString wall = QString("Extrude1.side.e%1").arg(h3);
  key(w, Qt::Key_H);
  pump(150);
  w.selection()->clear();
  const QPointF wp = facePx(w, wall);
  check(wp.x() >= 0, "hole wall pickable: " + wall);
  vclick(w, wp);
  check(w.selection()->contains(SelItem{SelItem::Kind::Face, wall}), "hole wall selected");
  const double d3 = 2 * w.document().feature("Sketch1")->sketch->entity(h3)->radius;
  const QString d3s = QString::fromStdString(pc::formatNumber(d3));
  check(badgeText(w).contains(QString::fromUtf8("Ø %1 mm").arg(d3s)), "badge: " + badgeText(w));
  if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
  waitFor([&] { return w.propertiesPanel()->findChild<QPushButton*>("prop.param.D") != nullptr; }, 1500);
  check(w.propertiesPanel()->findChild<QPushButton*>("prop.param.D") != nullptr, "Özellikler: 'Parametre yap' for Çap in 3D");
  shot(w, "09_badge_cylinder.png");
  const int u3 = undo->count();
  // through the Properties button this time
  clickButton(w.propertiesPanel()->findChild<QPushButton*>("prop.param.D"));
  waitFor([&] { return popup(w) != nullptr; }, 2000);
  if (auto* pp = popup(w)) {
    pch::typeInto(pp->nameEdit(), "DB", "quickParam.name");
    pch::tickCheckBox(pp->findChild<QCheckBox*>("quickParam.limits"), true, "limits");
    check(pp->findChild<QLineEdit*>("quickParam.min")->text() == QString::fromStdString(pc::formatNumber(d3 * 0.5)) &&
              pp->findChild<QLineEdit*>("quickParam.max")->text() == QString::fromStdString(pc::formatNumber(d3 * 1.5)),
          "min/max suggested ±50 %: " + pp->findChild<QLineEdit*>("quickParam.min")->text() + " .. " + pp->findChild<QLineEdit*>("quickParam.max")->text());
    shot(w, "10_popup_3d.png");
    QTest::keyClick(pp->nameEdit(), Qt::Key_Return);
    pump(400);
  } else check(false, "popup from Properties");
  const pc::Feature* s1b = w.document().feature("Sketch1");
  check(pexpr(w, "DB") == d3s.toStdString() && s1b && dimExpr(*s1b->sketch, SkConstraintType::Diameter, h3) == "DB",
        "DB = " + d3s + ", Sketch1 got Diameter = DB");
  check(param(w, "DB") && std::fabs(param(w, "DB")->minValue - 0.5 * d3) < 1e-9 && std::fabs(param(w, "DB")->maxValue - 1.5 * d3) < 1e-9, "DB range ±50 %");
  check(undo->count() == u3 + 1, QString("one undo step (%1 -> %2)").arg(u3).arg(undo->count()));
  v->setFocus();
  key(w, Qt::Key_Z, Qt::ControlModifier);
  waitFor([&] { return param(w, "DB") == nullptr; }, 3000);
  check(!param(w, "DB") && dimExpr(*w.document().feature("Sketch1")->sketch, SkConstraintType::Diameter, h3) == "<none>",
        "Ctrl+Z removes DB and the dimension in one step");
  key(w, Qt::Key_Y, Qt::ControlModifier);
  waitFor([&] { return param(w, "DB") != nullptr; }, 3000);
  check(param(w, "DB") && dimExpr(*w.document().feature("Sketch1")->sketch, SkConstraintType::Diameter, h3) == "DB", "Ctrl+Y brings both back");
  const QString dbNew = QString::fromStdString(pc::formatNumber(d3 * 1.2));
  check(editParamCell(w, "DB", dbNew), "DB = " + dbNew + " in Gruplar (inside its range)");
  waitFor([&] { return pexpr(w, "DB") == dbNew.toStdString(); }, 4000);
  pump(300);
  {
    double r = -1;
    auto it = w.modelResult().faces.find(wall.toStdString());
    if (it != w.modelResult().faces.end()) {
      BRepAdaptor_Surface s(it->second);
      if (s.GetType() == GeomAbs_Cylinder) r = s.Cylinder().Radius();
    }
    check(std::fabs(r - 0.6 * d3) < 1e-6, QString("the hole in the model follows DB = %1 (r = %2)").arg(dbNew).arg(r));
  }

  section("3D: a top edge -> its length is W");
  w.structurePanel()->setCurrentPage("structure");
  key(w, Qt::Key_H);
  pump(150);
  QString topEdge;
  for (const auto& [n, e] : w.modelResult().edges) {
    const QString name = QString::fromStdString(n);
    if (name.contains("Extrude1.end") && name.contains(QString("Extrude1.side.e%1|").arg(bottom))) topEdge = name;
    if (name.contains("Extrude1.end") && name.endsWith(QString("Extrude1.side.e%1)").arg(bottom))) topEdge = name;
  }
  w.selection()->clear();
  const QPointF ep = edgePx(w, topEdge);
  check(!topEdge.isEmpty() && ep.x() >= 0, "front top edge pickable: " + topEdge);
  vclick(w, ep);
  check(badgeText(w).contains("W = 80 mm"), "edge badge shows the binding W: " + badgeText(w));
  w.selection()->clear();

  // ---------------------------------------------------------------- save / reopen
  section("save / reopen");
  const QString pcad = tmpDir + "/param_select.pcad";
  const nlohmann::json before = pc::toJson(w.document());
  bool fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  v->setFocus();
  key(w, Qt::Key_S, Qt::ControlModifier);
  waitFor([&] { return QFileInfo::exists(pcad) && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  check(fileDlg && QFileInfo::exists(pcad), "saved: " + pcad);
  key(w, Qt::Key_N, Qt::ControlModifier);
  waitFor([&] { return w.document().features.empty(); }, 3000);
  fileDlg = false;
  g_onModal = [&](QWidget* m) { if (qobject_cast<QFileDialog*>(m)) { fileDlg = true; acceptFileDialog(m, pcad); } };
  key(w, Qt::Key_O, Qt::ControlModifier);
  waitFor([&] { return !w.document().features.empty() && !QApplication::activeModalWidget(); }, 10000);
  g_onModal = nullptr;
  pump(300);
  const nlohmann::json after = pc::toJson(w.document());
  check(fileDlg && after["parameters"] == before["parameters"] && after["features"] == before["features"], "reopened: parameters and features identical");
  check(pexpr(w, "W") == "80" && pexpr(w, "DB") == dbNew.toStdString() && pexpr(w, "T") == "10" && param(w, "W") && param(w, "W")->exposed,
        "reopened: W = 80, DB = " + dbNew + ", T = 10, W exposed");
  check(w.document().feature("Sketch1") && dimExpr(*w.document().feature("Sketch1")->sketch, SkConstraintType::Length, bottom) == "W",
        "reopened: bottom dimension still = W");

  // ---------------------------------------------------------------- Save as App
  section("Save as App lists the parameters; app --validate-only reports them");
  const QString base = tmpDir + "/ParamSelApp";
  bool appDlg = false, wTicked = false, dbTicked = false, wtTicked = true;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
    appDlg = true;
    pch::expectCloseWithin(m, 240000);
    if (auto* t = m->findChild<QTableWidget*>())
      for (int r = 0; r < t->rowCount(); ++r) {
        const QString n = t->item(r, 1) ? t->item(r, 1)->text() : QString();
        const bool on = t->item(r, 0) && t->item(r, 0)->checkState() == Qt::Checked;
        if (n == "W") wTicked = on;
        if (n == "DB") dbTicked = on;
      }
    QList<QLineEdit*> edits;
    for (auto* e : m->findChildren<QLineEdit*>()) if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox"))) edits << e;
    if (edits.size() >= 4) edits[3]->setText(base);
    QTimer::singleShot(200, m, [m] { if (auto* b = childWithText<QPushButton>(m, "Uygulamayı Kaydet")) pch::pressLater(b); });
  };
  (void)wtTicked;
  const int boxes0 = g_boxes.size();
  v->setFocus();
  key(w, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
  const bool appDone = waitFor([&] {
    for (int i = boxes0; i < g_boxes.size(); ++i) if (g_boxes[i].startsWith("Uygulama kaydedildi")) return !QApplication::activeModalWidget();
    return false;
  }, 200000);
  g_onModal = nullptr;
  check(appDlg && wTicked && dbTicked, "Save-as-App dialog lists W and DB as app inputs (ticked)");
  check(appDone, "Save-as-App finished");
#ifdef Q_OS_WIN
  const QString exe = base + ".exe";
#else
  const QString exe = base;
#endif
  if (QFileInfo::exists(exe)) {
    QByteArray so;
    const int rc = runProc(exe, {"--validate-only"}, &so);
    const QByteArray so0 = so;
    bool ok = false;
    try {
      auto j = nlohmann::json::parse(so.toStdString());
      ok = j["parameters"].contains("W") && j["parameters"]["W"].get<double>() == 80 && j["parameters"].contains("DB") &&
           std::fabs(j["parameters"]["DB"].get<double>() - 1.2 * d3) < 1e-9 && j["parameters"].contains("D1");
    } catch (...) {}
    check((rc == 0 || rc == 2) && ok, QString("app --validate-only (rc %1) reports W = 80, DB = %2, D1").arg(rc).arg(dbNew));
    double vol0 = -1, vol1 = -1;
    try { vol0 = nlohmann::json::parse(so0.toStdString())["volumeMm3"].get<double>(); } catch (...) {}
    const int rc2 = runProc(exe, {"--validate-only", "--set", "W=70"}, &so);
    try { vol1 = nlohmann::json::parse(so.toStdString())["volumeMm3"].get<double>(); } catch (...) {}
    // the rectangle (H = 40, T = 10) loses 10 mm of width: 4000 mm³
    check((rc2 == 0 || rc2 == 2) && std::fabs((vol0 - vol1) - 4000) < 1, QString("--set W=70: volume %1 -> %2 mm³ (-4000)").arg(vol0).arg(vol1));
  } else {
    check(false, "app executable written: " + exe);
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
  QApplication::setApplicationName("pc_gui_harness_param_select");
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
