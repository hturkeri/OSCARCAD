// pc_gui_harness_qa — customer-scenario QA harness. Everything is driven through the UI (menus, toolbar buttons,
// dialogs, mouse in the sketch editor / viewer, parameter table); the Document is only READ for assertions.
//
// Scenario (customer report): parametric sketch (named dimension parameters) -> finish sketch -> extrude with a
// parameter-bound distance -> fillet with a parameter-bound radius -> change parameters -> save/reopen ->
// Save as App -> generated app: inputs, regeneration, manufacturing rule banner, STL export.
//
//   pc_gui_harness_qa [--shots DIR] [--keep DIR]
// Run under: xvfb-run -a -s "-screen 0 1600x1000x24" pc_gui_harness_qa
// Exit code = number of failed checks. The run CONTINUES after failures so one run lists every problem.
#include "MainWindow.h"
#include "harness_robust.h"
#include "RunnerWindow.h"
#include "pc/AppPackage.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/SaveAsAppDialog.h"
#include <BRepAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <gp_Lin.hxx>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <nlohmann/json.hpp>

namespace {

int g_fail = 0;
QStringList g_failures;
QString g_shots;

void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  pch::kick();
  if (!ok) { ++g_fail; g_failures << what; }
}
void note(const QString& s) { pch::hnote(s); }
void section(const QString& s) { std::printf("\n== %s\n", qPrintable(s)); std::fflush(stdout); }

void pump(int ms) { QTest::qWait(ms); }
// Polls with a timeout (never infinite); scaled by PC_HARNESS_SLOW for slow machines.
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  timeoutMs *= pch::slowFactor();
  QElapsedTimer t;
  t.start();
  while (!pred()) {
    if (t.elapsed() > timeoutMs) return false;
    QApplication::processEvents(QEventLoop::AllEvents, 30);
    QTest::qWait(20);
  }
  return true;
}
void saveShot(const QImage& img, const QString& name) {
  if (g_shots.isEmpty()) return;
  QDir().mkpath(g_shots);
  if (!img.save(g_shots + "/" + name)) std::printf("  [warn] could not write %s\n", qPrintable(name));
}
QImage composite(QWidget* main, QWidget* dlg) {
  QImage img = main->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const QImage d = dlg->grab().toImage();
  QPainter p(&img);
  const QPoint at(std::max(0, img.width() - d.width() - 24), std::max(0, img.height() - d.height() - 36));
  p.fillRect(QRect(at + QPoint(6, 6), d.size()), QColor(0, 0, 0, 60));
  p.drawImage(at, d);
  p.setPen(QColor(120, 130, 140));
  p.drawRect(QRect(at, d.size()).adjusted(0, 0, -1, -1));
  return img;
}

// ---------------------------------------------------------------- modal pump
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
template <class T> T* childWithText(QWidget* w, const QString& text);
bool handleMessageBox(QWidget* w);
void clickLater(QAbstractButton* b, int ms = 0);
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(60);
  QObject::connect(t, &QTimer::timeout, [] {
    static bool busy = false;
    if (busy) return;
    QWidget* w = QApplication::activeModalWidget();
    if (!w || !w->isVisible()) return;
    // never wait forever on a modal: past its deadline it is dumped, reported FAIL and force-closed
    if (pch::enforceDeadline(w)) return;
    if (w->property("qaHandled").toBool()) return;
    busy = true;
    w->setProperty("qaHandled", true);
    if (g_onModal) g_onModal(w);
    else if (w->inherits("pcd::ChangeReviewDialog")) {
      std::printf("  [note] manufacturing review dialog (default handler: Yine de Uygula)\n");
      clickLater(childWithText<QPushButton>(w, "Yine de Uygula"));
    } else if (!handleMessageBox(w)) {
      std::printf("  [warn] unhandled modal %s '%s'\n", w->metaObject()->className(), qPrintable(w->windowTitle()));
      std::fflush(stdout);
    }
    busy = false;
  });
  t->start();
}
template <class T>
T* childWithText(QWidget* w, const QString& text) {
  for (T* c : w->findChildren<T*>())
    if (c->text() == text || c->text().remove('&') == text) return c;
  return nullptr;
}
void clickLater(QAbstractButton* b, int ms) { pch::pressLater(b, ms); }
bool handleMessageBox(QWidget* w) {
  auto* mb = qobject_cast<QMessageBox*>(w);
  if (!mb) return false;
  g_boxes << mb->text();
  std::printf("  [msgbox] %s\n", qPrintable(mb->text().left(200).replace('\n', ' ')));
  QAbstractButton* b = mb->button(QMessageBox::Yes);
  if (!b) b = mb->button(QMessageBox::Ok);
  if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
  clickLater(b);
  return true;
}

// ---------------------------------------------------------------- UI helpers
void click(QWidget* w, QPoint p, Qt::KeyboardModifiers m = {}) {
  QTest::mouseMove(w, p);
  QTest::mouseClick(w, Qt::LeftButton, m, p);
}
QAction* findAction(QWidget* root, const QString& text) {
  for (QAction* a : root->findChildren<QAction*>())
    if (a->text() == text || a->text().remove('&') == text) return a;
  return nullptr;
}
QAction* findActionStarting(QWidget* root, const QString& text) {
  for (QAction* a : root->findChildren<QAction*>())
    if (a->text().remove('&').startsWith(text)) return a;
  return nullptr;
}
// Real mouse click on the visible toolbar button of an action (text match, optional multi-line "\n" removed).
bool clickToolButton(QWidget* root, const QString& text) {
  for (auto* tb : root->findChildren<QToolButton*>()) {
    QAction* a = tb->defaultAction();
    if (!a || !tb->isVisible()) continue;
    if (a->text().replace('\n', ' ') == text || a->text() == text) {
      if (!tb->isEnabled()) return false;
      QTest::mouseClick(tb, Qt::LeftButton);
      return true;
    }
  }
  return false;
}
QToolButton* toolButton(QWidget* root, const QString& text) {
  for (auto* tb : root->findChildren<QToolButton*>()) {
    QAction* a = tb->defaultAction();
    if (a && tb->isVisible() && (a->text() == text || a->text().replace('\n', ' ') == text)) return tb;
  }
  return nullptr;
}
// Trigger a menu entry like a user would: menu bar title -> entry. The popup is not navigated with the mouse
// (QMenu popups are unreliable under Xvfb); the QAction of the entry is triggered, exactly what the click does.
void menuTrigger(pcd::MainWindow& w, const QString& text) {
  QAction* a = findActionStarting(&w, text);
  if (!a) { check(false, "menu entry exists: " + text); return; }
  QTimer::singleShot(0, a, &QAction::trigger);
  pump(50);
}

QTableView* paramView(pcd::MainWindow& w) {
  for (auto* v : w.findChildren<QTableView*>())
    if (qobject_cast<QStandardItemModel*>(v->model())) return v;
  return nullptr;
}
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* v = paramView(w);
  if (!v) return -1;
  auto* m = qobject_cast<QStandardItemModel*>(v->model());
  for (int r = 0; r < m->rowCount(); ++r)
    if (m->item(r, 0)->text() == name) return r;
  return -1;
}
// Edit a parameter-table cell through the real delegate editor.
bool editParamCell(pcd::MainWindow& w, int row, int col, const QString& text) {
  auto* v = paramView(w);
  if (!v || row < 0) return false;
  const QModelIndex idx = v->model()->index(row, col);
  v->setCurrentIndex(idx);
  v->scrollTo(idx);
  v->edit(idx);
  pump(80);
  auto* ed = v->viewport()->findChild<QLineEdit*>();
  if (!ed) return false;
  ed->selectAll();
  QTest::keyClicks(ed, text);
  QTest::keyClick(ed, Qt::Key_Return);
  pump(150);
  return true;
}
std::string pexpr(pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<missing>");
}
double pval(const pc::Document& d, const char* n) {
  try {
    auto v = d.evaluateParams();
    auto it = v.find(n);
    return it == v.end() ? std::nan("") : it->second;
  } catch (...) { return std::nan(""); }
}
struct Box { double dx = 0, dy = 0, dz = 0, vol = 0; };
Box bbox(pcd::MainWindow& w) {
  Box b;
  if (w.modelResult().shape.IsNull()) return b;
  const auto m = pc::model::massProperties(w.modelResult().shape);
  b.dx = m.xmax - m.xmin; b.dy = m.ymax - m.ymin; b.dz = m.zmax - m.zmin; b.vol = m.volume;
  return b;
}
bool near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }
QString boxStr(const Box& b) { return QString("%1 x %2 x %3 (V=%4)").arg(b.dx, 0, 'f', 3).arg(b.dy, 0, 'f', 3).arg(b.dz, 0, 'f', 3).arg(b.vol, 0, 'f', 1); }

const pc::Feature* firstSketch(const pc::Document& d) {
  for (const auto& f : d.features) if (f.type == pc::FeatureType::Sketch) return &f;
  return nullptr;
}
struct SkInfo { int dims = 0; std::set<std::string> exprs; int cons = 0; };
SkInfo sketchInfo(const pc::Document& d) {
  SkInfo i;
  if (const auto* f = firstSketch(d); f && f->sketch)
    for (const auto& c : f->sketch->constraints) {
      ++i.cons;
      if (pc::isDimensional(c.type)) { ++i.dims; i.exprs.insert(c.valueExpr); }
    }
  return i;
}
QString setStr(const std::set<std::string>& s) {
  QStringList l;
  for (auto& x : s) l << QString::fromStdString(x);
  return "{" + l.join(", ") + "}";
}

// ---------------------------------------------------------------- dimension dialog handler
bool g_dimHasShowInApp = false;
bool g_newParamHasExposed = false;
struct DimSpec { QString expr; QString newParam; QString shot; };   // newParam empty = plain expression
void armDimension(pcd::MainWindow& w, const DimSpec& s, bool* sawDialog, bool* sawNewParamUi) {
  g_onModal = [&w, s, sawDialog, sawNewParamUi](QWidget* dlg) {
    if (handleMessageBox(dlg)) return;
    if (!dlg->inherits("pcd::DimensionDialog")) { note(QString("unexpected modal %1 during dimension").arg(dlg->metaObject()->className())); return; }
    if (sawDialog) *sawDialog = true;
    auto* ee = dlg->findChild<pcd::ExprEdit*>("dimValue");
    if (!ee) ee = dlg->findChild<pcd::ExprEdit*>();
    QLineEdit* le = ee ? ee->lineEdit() : nullptr;
    if (le) {
      pch::typeInto(le, s.expr, "dimension value");   // verified; [note] + programmatic fallback if keys were dropped
      std::printf("  [dim dialog] '%s' typed -> '%s'\n", qPrintable(s.expr), qPrintable(le->text()));
    }
    if (!s.newParam.isEmpty()) {
      QCheckBox* cb = dlg->findChild<QCheckBox*>("dimNewParam");
      if (!cb) for (auto* c : dlg->findChildren<QCheckBox*>()) if (c->text().contains("parametre")) cb = c;
      QLineEdit* nm = dlg->findChild<QLineEdit*>("dimParamName");
      if (!nm) for (auto* e : dlg->findChildren<QLineEdit*>()) if (e != le && !(e->parent() && e->parent()->inherits("pcd::ExprEdit"))) nm = e;
      if (sawNewParamUi) *sawNewParamUi = cb && nm;
      if (cb && nm) {
        // click the INDICATOR: the check box spans the form row and its centre can lie outside the clickable text
        pch::tickCheckBox(cb, true, "dimension dialog 'yeni parametre' check box");
        if (!nm->isEnabled()) note("dimension dialog: name field still disabled after ticking 'yeni parametre'");
        pch::typeInto(nm, s.newParam, "dimension dialog parameter name");
      }
      // "show in app" tick, if the dialog offers one
      QCheckBox* ex = dlg->findChild<QCheckBox*>("dimParamExposed");
      if (!ex) for (auto* c : dlg->findChildren<QCheckBox*>()) if (c != cb && (c->text().contains("Uygulamada") || c->text().contains("App"))) ex = c;
      if (ex) {
        if (ex->isEnabled()) pch::tickCheckBox(ex, true, "dimension dialog 'Uygulamada göster'");
        g_dimHasShowInApp = true;
      }
    }
    QTimer::singleShot(120, dlg, [&w, dlg, s] {
      if (!s.shot.isEmpty()) saveShot(composite(&w, dlg), s.shot);
      clickLater(childWithText<QPushButton>(dlg, "Tamam"));
    });
  };
}

// ---------------------------------------------------------------- file dialog helper
bool pickFileInDialog(const QString& path, const std::function<void()>& trigger, const std::function<bool()>& done, int timeoutMs = 20000) {
  bool sawDlg = false;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    auto* fd = qobject_cast<QFileDialog*>(m);
    if (!fd) return;
    sawDlg = true;
    QTimer::singleShot(250, fd, [fd, path] {
      fd->selectFile(path);
      if (auto* le = fd->findChild<QLineEdit*>("fileNameEdit")) le->setText(path);
      QTimer::singleShot(100, fd, [fd] { QMetaObject::invokeMethod(fd, "accept"); });
    });
  };
  QTimer::singleShot(0, trigger);
  const bool ok = waitFor([&] { return done() && !QApplication::activeModalWidget(); }, timeoutMs);
  g_onModal = nullptr;
  return ok && sawDlg;
}

// ---------------------------------------------------------------- sketch geometry helpers
QPoint centerOfLine(pcd::SketchEditor* ed, int entityIdx) {
  const auto& e = ed->sketch().entities[size_t(entityIdx)];
  const QPointF a = ed->pointScreenPos(e.points[0]), b = ed->pointScreenPos(e.points[1]);
  return ((a + b) / 2.0).toPoint();
}

bool isVerticalLine(const pc::model::ModelResult& r, const QString& name) {
  auto it = r.edges.find(name.toStdString());
  if (it == r.edges.end()) return false;
  try {
    BRepAdaptor_Curve c(it->second);
    if (c.GetType() != GeomAbs_Line) return false;
    return std::fabs(std::fabs(c.Line().Direction().Z()) - 1.0) < 1e-6;
  } catch (...) { return false; }
}

void dumpSketch(pcd::SketchEditor* ed, const char* tag) {
  const auto& sk = ed->sketch();
  std::printf("  [dump %s] dof=%d points:", tag, ed->dof());
  for (const auto& p : sk.points) std::printf(" #%d(%.2f,%.2f)", p.id, p.x, p.y);
  std::printf(" | entities:");
  for (const auto& e : sk.entities) std::printf(" %d:t%d", e.id, int(e.type));
  std::printf(" | constraints:");
  for (const auto& c : sk.constraints) std::printf(" [%d%s%s]", int(c.type), c.valueExpr.empty() ? "" : "=", c.valueExpr.c_str());
  std::printf("\n");
  std::fflush(stdout);
}

// ======================================================================================= scenario
struct Result {
  QString pcad;
  QString appExe, appPkg;
};

// (a) + (b) + (c)
void stepSketch(pcd::MainWindow& w) {
  section("a) sketch: rectangle + dimensions bound to NEW parameters W, D, Dh");
  menuTrigger(w, "Yeni");
  pump(200);
  check(w.document().features.empty(), "new part is empty");

  // Ekle > Sketch... -> plane chooser (QInputDialog) -> XY
  bool sawPlaneDlg = false;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    if (auto* id = qobject_cast<QInputDialog*>(m)) {
      sawPlaneDlg = true;
      auto* cb = id->findChild<QComboBox*>();
      if (cb) cb->setCurrentIndex(0);   // "XY düzlemi"
      clickLater(childWithText<QPushButton>(id, "OK"), 50);
      for (auto* b : id->findChildren<QPushButton*>()) if (b->text().contains("Tamam") || b->text() == "&OK") clickLater(b, 50);
    }
  };
  menuTrigger(w, "Sketch...");
  check(waitFor([&] { return w.inSketchMode(); }, 5000), "sketch mode entered (Ekle > Sketch > XY)");
  g_onModal = nullptr;
  check(sawPlaneDlg, "plane chooser dialog shown");
  pump(300);
  auto* ed = w.sketchEditor();
  if (!ed) { check(false, "sketch editor exists"); return; }

  QObject::connect(ed, &pcd::SketchEditor::statusMessage, [](const QString& m) { if (!m.isEmpty()) std::printf("  [sketch status] %s\n", qPrintable(m)); });
  // Rectangle with the toolbar button + two real mouse clicks (start on the origin, drag direction up-right)
  check(clickToolButton(&w, "Dikdörtgen"), "toolbar button 'Dikdörtgen' clicked");
  const QPointF org = ed->pointScreenPos(0);
  click(ed, org.toPoint());
  click(ed, (org + QPointF(260, -170)).toPoint());
  check(ed->sketch().entities.size() == 4, QString("rectangle drawn with 2 mouse clicks (%1 entities)").arg(ed->sketch().entities.size()));
  clickToolButton(&w, "Seç");
  if (ed->sketch().entities.size() != 4) return;

  // two dimensions, each creating a NEW named parameter from the dimension dialog
  bool dlg1 = false, newUi1 = false, dlg2 = false, newUi2 = false;
  click(ed, centerOfLine(ed, 0));
  armDimension(w, {"60", "W", "03_dim_dialog_new_param.png"}, &dlg1, &newUi1);
  check(clickToolButton(&w, "Uzunluk"), "toolbar 'Uzunluk' clicked (bottom edge)");
  pump(250);
  check(dlg1, "dimension dialog opened for W");
  check(newUi1, "dimension dialog has 'Yeni adlandırılmış parametre' checkbox + name field");
  click(ed, centerOfLine(ed, 1));
  armDimension(w, {"40", "D", ""}, &dlg2, &newUi2);
  check(clickToolButton(&w, "Uzunluk"), "toolbar 'Uzunluk' clicked (right edge)");
  pump(250);
  check(dlg2, "dimension dialog opened for D");
  g_onModal = nullptr;
  if (g_dimHasShowInApp) note("dimension dialog offers a 'Uygulamada göster' tick (used for W, D, Dh)");
  else note("dimension dialog has NO 'show in app' tick (exposure must be set in the parameter table / Save-as-App dialog)");

  // anchor the rectangle: origin point coincident with bottom and left edge
  clickToolButton(&w, "Sığdır");
  pump(150);
  for (int edge : {0, 3}) {
    QTest::keyClick(ed, Qt::Key_Escape);   // clear the selection
    click(ed, ed->pointScreenPos(0).toPoint());
    click(ed, centerOfLine(ed, edge), Qt::ControlModifier);
    clickToolButton(&w, "Çakışık");
    pump(100);
  }
  dumpSketch(ed, "after dims+anchor");
  note(QString("DOF after W, D and anchoring the corner on the origin: %1").arg(ed->dof()));

  // circle + diameter dimension bound to Dh=10; centre located by W/2, D/2
  clickToolButton(&w, "Sığdır");
  pump(100);
  QPointF far0(-1e9, 1e9);
  for (const auto& p : ed->sketch().points) {
    const QPointF s = ed->pointScreenPos(p.id);
    if (s.x() > far0.x() || far0.x() < -1e8) { if (p.x > 1) far0 = s; }
  }
  // far corner (max x, max y) in sketch coordinates
  int farId = -1; double bx = -1e9, by = -1e9;
  for (const auto& p : ed->sketch().points) if (p.x + p.y > bx + by) { bx = p.x; by = p.y; farId = p.id; }
  const QPointF farS = ed->pointScreenPos(farId), o2 = ed->pointScreenPos(0);
  const double pxPerMm = bx > 1 ? (farS.x() - o2.x()) / bx : 5.0;
  const QPointF ctr = o2 + (farS - o2) / 2.0;
  check(clickToolButton(&w, "Çember"), "toolbar 'Çember' clicked");
  click(ed, ctr.toPoint());
  click(ed, (ctr + QPointF(5.0 * pxPerMm, 0)).toPoint());
  clickToolButton(&w, "Seç");
  int circleIdx = -1;
  for (size_t i = 0; i < ed->sketch().entities.size(); ++i) if (ed->sketch().entities[i].type == pc::SkEntityType::Circle) circleIdx = int(i);
  check(circleIdx >= 0, "circle drawn with the mouse");
  if (circleIdx < 0) return;
  const auto circ = ed->sketch().entities[size_t(circleIdx)];
  const QPointF cc = ed->pointScreenPos(circ.points[0]);
  click(ed, (cc + QPointF(circ.radius * pxPerMm, 0)).toPoint());
  bool dlg3 = false;
  armDimension(w, {"10", "Dh", ""}, &dlg3, nullptr);
  check(clickToolButton(&w, "Çap"), "toolbar 'Çap' clicked (circle)");
  pump(250);
  check(dlg3, "dimension dialog opened for Dh");
  // position the circle centre: DistanceX / DistanceY from the origin with expressions W/2, D/2
  for (int k = 0; k < 2; ++k) {
    QTest::keyClick(ed, Qt::Key_Escape);
    click(ed, ed->pointScreenPos(0).toPoint());
    click(ed, ed->pointScreenPos(circ.points[0]).toPoint(), Qt::ControlModifier);
    armDimension(w, {k == 0 ? "W/2" : "D/2", "", ""}, nullptr, nullptr);
    clickToolButton(&w, k == 0 ? "Yatay mesafe" : "Dikey mesafe");
    pump(250);
    note(QString("after %1: dims=%2 dof=%3").arg(k == 0 ? "DistanceX" : "DistanceY").arg(ed->sketch().constraints.size()).arg(ed->dof()));
  }
  g_onModal = nullptr;
  dumpSketch(ed, "end of sketch");
  const int dofSk = ed->dof();
  note(QString("sketch editor DOF before finishing: %1").arg(dofSk));
  check(dofSk == 0, QString("sketch fully constrained in the editor (DOF %1)").arg(dofSk));
  int dimsIn = 0;
  std::set<std::string> exprsIn;
  for (const auto& c : ed->sketch().constraints) if (pc::isDimensional(c.type)) { ++dimsIn; exprsIn.insert(c.valueExpr); }
  check(dimsIn == 5, QString("5 dimensions present in the editor (W, D, Dh, W/2, D/2): %1 %2").arg(dimsIn).arg(setStr(exprsIn)));
  clickToolButton(&w, "Sığdır");
  pump(200);
  {
    const int rowW = paramRow(w, "W"), rowDh = paramRow(w, "Dh");
    note(QString("parameter table while still in sketch mode: W row %1, Dh row %2").arg(rowW).arg(rowDh));
    check(rowW >= 0 && rowDh >= 0, "parameters created from dimension dialogs are listed in the parameter table already while sketching");
  }
  saveShot(ed->grab().toImage(), "01_sketch_dims_before_close.png");
  saveShot(w.grab().toImage(), "01b_designer_sketch_mode.png");

  // ---- b) finish
  section("b) finish the sketch -> constraints, dimension expressions, parameters survive");
  check(clickToolButton(&w, "Sketch'i Bitir"), "toolbar 'Sketch'i Bitir' clicked");
  check(waitFor([&] { return !w.inSketchMode(); }, 5000), "sketch mode left");
  pump(400);
  // SC23 O-P1-1: the button continues in Çek (SpaceClaim 2023 R1 End Sketch Editing); Seç (S) goes back
  check(w.toolManager() && w.toolManager()->activeId() == "pull", "SC23: Sketch'i Bitir continues in Çek");
  if (QAction* sel = w.findChild<QAction*>("tool.select")) sel->trigger();
  pump(100);
  check(w.toolManager() && w.toolManager()->activeId() == "select", "SC23: Seç (S) returns to Select");
  const auto si = sketchInfo(w.document());
  check(firstSketch(w.document()) != nullptr, "Sketch feature present in the feature tree");
  check(si.cons > 0, QString("constraints kept after closing (%1)").arg(si.cons));
  check(si.dims == 5, QString("dimensional constraints kept after closing: %1 (expected 5) %2").arg(si.dims).arg(setStr(si.exprs)));
  check(si.exprs.count("W") && si.exprs.count("D") && si.exprs.count("Dh"),
        "dimension expressions W, D, Dh preserved: " + setStr(si.exprs));
  for (auto [n, v] : std::vector<std::pair<const char*, double>>{{"W", 60}, {"D", 40}, {"Dh", 10}}) {
    const auto* p = w.document().params.find(n);
    check(p != nullptr, QString("parameter %1 exists in the document").arg(n));
    check(near(pval(w.document(), n), v), QString("parameter %1 = %2 (got %3)").arg(n).arg(v).arg(pval(w.document(), n)));
    check(paramRow(w, n) >= 0, QString("parameter %1 listed in the parameter table").arg(n));
  }
  int dof = -1;
  if (const auto* f = firstSketch(w.document())) {
    auto it = w.modelResult().sketchResults.find(f->id);
    if (it != w.modelResult().sketchResults.end()) dof = it->second.dof;
  }
  check(dof == 0, QString("sketch DOF after closing = %1 (expected 0)").arg(dof));
  {
    // the closed sketch must stay visible in the 3D view (profile lines), not vanish until it is extruded
    const QImage vimg = w.viewerWidget()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    int dark = 0;
    for (int y = 0; y < vimg.height() - 140; ++y)
      for (int x = 0; x < vimg.width(); ++x) {
        const QRgb c = vimg.pixel(x, y);
        if (qRed(c) < 110 && qGreen(c) < 110 && qBlue(c) < 140) ++dark;
      }
    check(dark > 150, QString("closed sketch (profile + hole) is drawn in the 3D view (%1 dark pixels)").arg(dark));
  }
  saveShot(w.grab().toImage(), "02_designer_after_sketch.png");

  // tick "show in app" for W, D, Dh in the parameter table (real click on the check box)
  auto* pv = paramView(w);
  for (const char* n : {"W", "D", "Dh"}) {
    const int r = paramRow(w, n);
    if (r < 0 || !pv) continue;
    if (const auto* pp = w.document().params.find(n); pp && pp->exposed) continue;   // already ticked in the dimension dialog
    const QModelIndex idx = pv->model()->index(r, 6);
    pv->scrollTo(idx);
    const QRect vr = pv->visualRect(idx);
    QTest::mouseClick(pv->viewport(), Qt::LeftButton, {}, QPoint(vr.left() + 12, vr.center().y()));
    pump(120);
  }
  for (const char* n : {"W", "D", "Dh"}) {
    const auto* p = w.document().params.find(n);
    check(p && p->exposed, QString("'App'te göster' ticked for %1 via parameter table").arg(n));
  }

  // ---- c) re-open
  section("c) re-open the sketch (tree double-click): dimensions still shown with expressions; exit paths");
  auto reopen = [&]() -> bool {
    auto* tree = w.featureTree();
    QTreeWidgetItem* it = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString().startsWith("Sketch")) it = tree->topLevelItem(i);
    if (!it) return false;
    const QRect r = tree->visualItemRect(it);
    pump(200);
    const QRect rr = tree->visualItemRect(it);
    if (false) note(QString("tree dclick: item '%1' rect (%2,%3 %4x%5) enabled=%6 viewport=%7x%8 sketching=%9").arg(it->text(0)).arg(rr.x()).arg(rr.y()).arg(rr.width()).arg(rr.height()).arg(tree->isEnabled()).arg(tree->viewport()->width()).arg(tree->viewport()->height()).arg(w.inSketchMode()));
    // QTest's MouseDClick on an item view needs a preceding press on the same index (QAbstractItemView::pressedIndex)
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, rr.center());
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, rr.center());
    return waitFor([&] { return w.inSketchMode(); }, 3000);
  };
  check(reopen(), "double-click on the Sketch in the feature tree enters sketch edit");
  pump(300);
  if (w.inSketchMode()) {
    int d2 = 0;
    std::set<std::string> ex2;
    for (const auto& c : w.sketchEditor()->sketch().constraints) if (pc::isDimensional(c.type)) { ++d2; ex2.insert(c.valueExpr); }
    check(d2 == 5 && ex2.count("W") && ex2.count("D") && ex2.count("Dh"), QString("re-opened sketch still has its dimensions: %1 %2").arg(d2).arg(setStr(ex2)));
    check(w.sketchEditor()->dof() == 0, QString("re-opened sketch DOF %1").arg(w.sketchEditor()->dof()));
    clickToolButton(&w, "Sığdır");
    pump(200);
    saveShot(w.sketchEditor()->grab().toImage(), "02_sketch_dims_reopened.png");
    // exit path 1: Vazgeç (no modification -> no prompt expected)
    g_boxes.clear();
    g_onModal = [&](QWidget* m) { handleMessageBox(m); };
    check(clickToolButton(&w, "Vazgeç"), "toolbar 'Vazgeç' clicked");
    check(waitFor([&] { return !w.inSketchMode(); }, 3000), "Vazgeç leaves sketch mode");
    g_onModal = nullptr;
    check(g_boxes.isEmpty(), "Vazgeç on an unmodified sketch asks nothing");
    const auto s1 = sketchInfo(w.document());
    check(s1.dims == 5, QString("dimensions intact after Vazgeç (%1)").arg(s1.dims));
  }
  check(reopen(), "sketch re-entered a second time");
  if (w.inSketchMode()) {
    // exit path 2: Sketch'i Bitir
    check(clickToolButton(&w, "Sketch'i Bitir"), "toolbar 'Sketch'i Bitir' clicked");
    check(waitFor([&] { return !w.inSketchMode(); }, 5000), "Sketch'i Bitir leaves sketch mode");
    const auto s2 = sketchInfo(w.document());
    check(s2.dims == 5 && s2.exprs.count("W") && s2.exprs.count("Dh"), QString("dimensions intact after re-finishing (%1) %2").arg(s2.dims).arg(setStr(s2.exprs)));
    check(near(pval(w.document(), "W"), 60) && near(pval(w.document(), "Dh"), 10), "W / Dh parameters unchanged after re-finishing");
  }
  // exit path 3b: parameter-table edits made WHILE sketching must survive Sketch'i Bitir (customer: "parameters disappear")
  check(reopen(), "sketch re-entered (parameter edits while sketching)");
  if (w.inSketchMode()) {
    check(editParamCell(w, paramRow(w, "W"), 1, "70"), "W edited to 70 in the parameter table while the sketch is open");
    QPushButton* addBtn2 = childWithText<QPushButton>(&w, "Parametre ekle");
    if (addBtn2) QTest::mouseClick(addBtn2, Qt::LeftButton);
    pump(250);
    const int rp1 = paramRow(w, "P1");
    check(rp1 >= 0, "'Parametre ekle' works while sketching (P1 listed)");
    if (rp1 >= 0) editParamCell(w, rp1, 0, "Extra");
    pump(200);
    const double wInSketch = [&] {
      double mx = 0;
      for (const auto& p : w.sketchEditor()->sketch().points) mx = std::max(mx, p.x);
      return mx;
    }();
    check(near(wInSketch, 70, 0.01), QString("open sketch re-solved to W=70 (rect width %1)").arg(wInSketch));
    check(clickToolButton(&w, "Sketch'i Bitir"), "Sketch'i Bitir after parameter edits");
    check(waitFor([&] { return !w.inSketchMode(); }, 5000), "sketch closed");
    pump(400);
    check(pexpr(w, "W") == "70", QString("W edit made while sketching survived Sketch'i Bitir (W=%1)").arg(QString::fromStdString(pexpr(w, "W"))));
    check(w.document().params.find("Extra") != nullptr, "parameter added while sketching survived Sketch'i Bitir");
    // restore the scenario state: W back to 60, remove Extra
    editParamCell(w, paramRow(w, "W"), 1, "60");
    const int rx = paramRow(w, "Extra");
    if (rx >= 0) {
      paramView(w)->setCurrentIndex(paramView(w)->model()->index(rx, 0));
      if (auto* del = childWithText<QPushButton>(&w, "Seçiliyi sil")) QTest::mouseClick(del, Qt::LeftButton);
      pump(250);
    }
    check(pexpr(w, "W") == "60" && w.document().params.find("Extra") == nullptr, "scenario state restored (W=60, Extra removed)");
  }
  check(reopen(), "sketch re-entered a third time");
  if (w.inSketchMode()) {
    // exit path 3: Escape/keyboard does not exit; modified + Vazgeç -> confirmation, answer Yes -> changes discarded
    click(w.sketchEditor(), centerOfLine(w.sketchEditor(), 0));
    QTest::keyClick(w.sketchEditor(), Qt::Key_Escape);
    check(w.inSketchMode(), "Escape in sketch mode does not close the sketch (only deselects)");
    clickToolButton(&w, "Sketch'i Bitir");
    waitFor([&] { return !w.inSketchMode(); }, 5000);
  }
  check(sketchInfo(w.document()).dims == 5, "dimensions still intact at the end of step c");
}

// (d) extrude + fillet
void stepFeatures(pcd::MainWindow& w) {
  section("d) Extrude distance bound to a NEW parameter T=8; Fillet radius bound to existing parameter R");
  // existing parameter R created in the parameter table first ("Parametre ekle", rename P1 -> R, expression 2)
  QPushButton* addBtn = childWithText<QPushButton>(&w, "Parametre ekle");
  check(addBtn != nullptr, "'Parametre ekle' button found");
  if (addBtn) QTest::mouseClick(addBtn, Qt::LeftButton);
  pump(250);
  int rp = paramRow(w, "P1");
  check(rp >= 0, "new parameter P1 listed");
  if (rp >= 0) {
    check(editParamCell(w, rp, 0, "R"), "P1 renamed to R via the table");
    const int rr = paramRow(w, "R");
    check(rr >= 0 && editParamCell(w, rr, 1, "2"), "R expression set to 2");
  }
  check(near(pval(w.document(), "R"), 2), QString("R = 2 (got %1)").arg(pval(w.document(), "R")));

  // Extrude through the menu
  menuTrigger(w, "Extrude...");
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "Extrude dialog opened (Ekle > Extrude)");
  auto* dlg = w.featureDialog();
  if (!dlg) return;
  pump(200);
  auto exprs = dlg->findChildren<pcd::ExprEdit*>();
  check(!exprs.isEmpty(), "Extrude dialog has an expression field for the distance");
  if (exprs.isEmpty()) return;
  pcd::ExprEdit* distEd = dlg->findChild<pcd::ExprEdit*>("expr_distance");
  if (!distEd) distEd = exprs.first();
  pch::typeInto(distEd->lineEdit(), "8", "extrude distance");
  // binding affordances next to the field: parameter picker combo ('name = value' + 'Yeni parametre…') and 'Yeni…'
  QComboBox* pick = distEd->findChild<QComboBox*>("paramPick");
  check(pick && pick->isVisible(), "parameter picker combo next to the Extrude distance");
  if (pick) {
    QStringList items;
    for (int i = 0; i < pick->count(); ++i) items << pick->itemText(i);
    bool listsAll = true;
    for (const char* n : {"W", "D", "Dh", "R"}) listsAll = listsAll && pick->findData(QString(n)) >= 0;
    check(listsAll, "picker lists the existing parameters W, D, Dh, R: " + items.join(" | "));
    check(!items.filter("Yeni parametre").isEmpty(), "picker offers 'Yeni parametre…'");
  }
  QToolButton* newBtn = distEd->findChild<QToolButton*>("paramNew");
  check(newBtn != nullptr && newBtn->isVisible() && newBtn->text().startsWith("Yeni"), "'Yeni…' (new parameter) button available next to the Extrude distance");
  bool sawName = false;
  if (newBtn) {
    g_onModal = [&](QWidget* m) {
      if (handleMessageBox(m)) return;
      if (m->inherits("pcd::NewParameterDialog")) {
        sawName = true;
        pch::typeInto(m->findChild<QLineEdit*>("npName"), "T", "new-parameter name");
        if (auto* ex = m->findChild<QCheckBox*>("npExposed")) { g_newParamHasExposed = true; pch::tickCheckBox(ex, true, "new-parameter 'Uygulamada göster'"); }
        if (auto* vv = m->findChild<pcd::ExprEdit*>("npValue")) note(QString("new-parameter dialog value field: '%1'").arg(vv->text()));
        QTimer::singleShot(150, m, [m] { saveShot(m->grab().toImage(), "04b_new_parameter_dialog.png"); clickLater(childWithText<QPushButton>(m, "Oluştur")); });
      } else note(QString("unexpected modal %1 after 'Yeni…'").arg(m->metaObject()->className()));
    };
    QTest::mouseClick(newBtn, Qt::LeftButton);   // opens the modal 'Yeni parametre' dialog (handled by the pump)
    waitFor([&] { return sawName && !QApplication::activeModalWidget(); }, 45000);
    pump(200);
    g_onModal = nullptr;
  }
  check(sawName, "'Yeni parametre' dialog shown after 'Yeni…'");
  if (pick) check(pick->currentText().startsWith("T = 8"), "picker shows the new binding 'T = 8' (got '" + (pick ? pick->currentText() : QString()) + "')");
  check(distEd->text() == "T", QString("distance field now holds 'T' (got '%1')").arg(distEd->text()));
  QPushButton* ok = childWithText<QPushButton>(dlg, "Oluştur");
  saveShot(composite(&w, dlg), "04_extrude_dialog_param_binding.png");
  if (ok) QTest::mouseClick(ok, Qt::LeftButton);
  waitFor([&] { return w.featureDialog() == nullptr; }, 5000);
  pump(500);
  const pc::Feature* ex = nullptr;
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Extrude) ex = &f;
  check(ex != nullptr, "Extrude feature created");
  if (ex) check(ex->prop("distance", "") == "T", QString("Extrude.distance expression is 'T' (got '%1')").arg(QString::fromStdString(ex->prop("distance", ""))));
  check(w.document().params.find("T") != nullptr && near(pval(w.document(), "T"), 8), QString("parameter T = 8 (got %1)").arg(pval(w.document(), "T")));
  const Box b0 = bbox(w);
  check(near(b0.dx, 60) && near(b0.dy, 40) && near(b0.dz, 8), "plate bbox 60 x 40 x 8: " + boxStr(b0));
  // the hole must really be in the part: volume = 60*40*8 - pi*5^2*8
  check(near(b0.vol, 60 * 40 * 8 - M_PI * 25 * 8, 1.0), QString("volume includes the Dh hole (V=%1, expected %2)").arg(b0.vol).arg(60 * 40 * 8 - M_PI * 25 * 8));

  // Fillet on a vertical corner edge: Ctrl+click in the viewer
  w.viewer()->setView(pc::viewer::ShapeViewer::View::Iso);
  w.viewer()->fitAll();
  pump(500);
  menuTrigger(w, "Fillet...");
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "Fillet dialog opened");
  dlg = w.featureDialog();
  if (!dlg) return;
  pump(200);
  auto* v = w.viewer();
  QPoint pos;
  QString edge;
  for (int y = 40; y < v->height() - 40 && edge.isEmpty(); y += 4)
    for (int x = 40; x < v->width() - 40; x += 4) {
      const QString e = v->edgeAt(QPoint(x, y));
      // SpaceClaim navigation scheme: a click picks the edge within 6 px (occlusion-checked); edgeAt uses 9 px
      const bool sc = v->navigationScheme() == pc::viewer::ShapeViewer::NavigationScheme::SpaceClaim;
      if (!e.isEmpty() && isVerticalLine(w.modelResult(), e) && (!sc || v->pickAt(QPoint(x, y)) == e)) { pos = QPoint(x, y); edge = e; break; }
    }
  check(!edge.isEmpty(), "a vertical plate corner edge found in the viewer: " + edge);
  if (!edge.isEmpty()) {
    click(v, pos, Qt::ControlModifier);
    pump(200);
  }
  check(!dlg->refEdges().isEmpty(), QString("edge picked into the Fillet list (%1)").arg(dlg->refEdges().join(",")));
  // choose the existing parameter R: type its first letter, take the completer entry
  auto fe = dlg->findChildren<pcd::ExprEdit*>();
  if (!fe.isEmpty()) {
    auto* le = fe.first()->lineEdit();
    // (1) type the first letter: the completer popup must list the existing parameters
    le->setFocus();
    le->selectAll();
    QTest::keyClicks(le, "R");
    pump(150);
    bool popup = false;
    for (QWidget* tw : QApplication::topLevelWidgets())
      if (tw->isVisible() && tw->inherits("QListView") && (tw->windowFlags() & Qt::Popup)) { popup = true; tw->hide(); }   // never Esc: that would reject the dialog
    check(popup, "completer popup lists existing parameters after typing 'R'");
    pump(50);
    // (2) pick from the parameter list next to the field (if the dialog has one)
    auto* pick = fe.first()->findChild<QComboBox*>("paramPick");
    if (pick) {
      const int idx = pick->findData("R");
      check(idx >= 0, "parameter picker lists R");
      if (idx >= 0) pick->setCurrentIndex(idx);
      pump(100);
    } else {
      note("no parameter picker next to the field (only typing / completer)");
      le->selectAll();
      QTest::keyClicks(le, "R");
      for (QWidget* tw : QApplication::topLevelWidgets())
        if (tw->isVisible() && tw->inherits("QListView") && (tw->windowFlags() & Qt::Popup)) tw->hide();
    }
    check(fe.first()->text() == "R", QString("radius field = 'R' (got '%1')").arg(fe.first()->text()));
  }
  ok = childWithText<QPushButton>(dlg, "Oluştur");
  if (ok) QTest::mouseClick(ok, Qt::LeftButton);
  waitFor([&] { return w.featureDialog() == nullptr; }, 5000);
  pump(600);
  const pc::Feature* fl = nullptr;
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Fillet) fl = &f;
  check(fl != nullptr, "Fillet feature created");
  if (fl) check(fl->prop("radius", "") == "R", QString("Fillet.radius expression is 'R' (got '%1')").arg(QString::fromStdString(fl->prop("radius", ""))));
  const Box b1 = bbox(w);
  check(b1.vol < b0.vol - 1.0, QString("fillet removed material (%1 -> %2)").arg(b0.vol).arg(b1.vol));
  check(w.modelResult().ok, "model regenerates OK with the fillet");
  saveShot(w.grab().toImage(), "05_designer_extrude_fillet.png");
}

// (e) change W, T; undo/redo; save/reopen
QString stepEditAndPersist(pcd::MainWindow& w, const QString& outDir) {
  section("e) change W and T in the parameter table; undo/redo; save .pcad");
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    if (m->inherits("pcd::ChangeReviewDialog")) {
      note("manufacturing review dialog appeared for a parameter edit");
      clickLater(childWithText<QPushButton>(m, "Yine de Uygula"));
    }
  };
  const Box before = bbox(w);
  check(editParamCell(w, paramRow(w, "W"), 1, "80"), "W expression edited to 80");
  waitFor([&] { return pexpr(w, "W") == "80"; }, 8000);
  pump(500);
  const Box afterW = bbox(w);
  check(near(afterW.dx, 80) && near(afterW.dy, 40), "bbox follows W=80: " + boxStr(afterW));
  check(afterW.vol > before.vol, "volume grew with W");
  check(editParamCell(w, paramRow(w, "T"), 1, "10"), "T expression edited to 10");
  waitFor([&] { return pexpr(w, "T") == "10"; }, 8000);
  pump(500);
  const Box afterT = bbox(w);
  check(near(afterT.dz, 10) && near(afterT.dx, 80), "bbox follows T=10: " + boxStr(afterT));
  // the dimension-driven sketch geometry must follow (hole stays centred -> volume formula)
  check(near(afterT.vol, 80 * 40 * 10 - M_PI * 25 * 10, 20.0), QString("volume consistent with W/T change (%1 vs ~%2; fillet tolerance 20)").arg(afterT.vol).arg(80 * 40 * 10 - M_PI * 25 * 10));
  // undo / redo through the Düzen menu actions
  QAction* undo = findAction(&w, "Geri Al");
  QAction* redo = findAction(&w, "Yinele");
  if (undo) undo->trigger();
  pump(600);
  check(pexpr(w, "T") == "8", QString("undo restores T=8 (got %1)").arg(QString::fromStdString(pexpr(w, "T"))));
  check(near(bbox(w).dz, 8) && near(bbox(w).dx, 80), "bbox after undo: " + boxStr(bbox(w)));
  if (redo) redo->trigger();
  pump(600);
  check(pexpr(w, "T") == "10", "redo re-applies T=10");
  check(near(bbox(w).dz, 10), "bbox after redo: " + boxStr(bbox(w)));
  g_onModal = nullptr;

  const QString pcad = outDir + "/qa_plate.pcad";
  QFile::remove(pcad);
  QAction* sa = findActionStarting(&w, "Farklı Kaydet");
  check(sa != nullptr, "File > Farklı Kaydet exists");
  const bool saved = sa && pickFileInDialog(pcad, [sa] { sa->trigger(); }, [&] { return QFileInfo::exists(pcad); });
  check(saved, "document saved through the file dialog: " + pcad);
  return pcad;
}

void stepReopen(pcd::MainWindow& w2, const QString& pcad) {
  section("e2) close + reopen: everything persists");
  QAction* open = findActionStarting(&w2, "Aç");
  check(open != nullptr, "File > Aç exists");
  bool ok = open && pickFileInDialog(pcad, [open] { open->trigger(); }, [&] { return !w2.document().features.empty(); });
  check(ok, "file reopened through the file dialog");
  pump(800);
  const auto si = sketchInfo(w2.document());
  check(si.dims == 5 && si.exprs.count("W") && si.exprs.count("D") && si.exprs.count("Dh"), QString("sketch dimensions persisted (%1) %2").arg(si.dims).arg(setStr(si.exprs)));
  for (auto [n, v] : std::vector<std::pair<const char*, double>>{{"W", 80}, {"D", 40}, {"Dh", 10}, {"T", 10}, {"R", 2}})
    check(near(pval(w2.document(), n), v), QString("parameter %1 = %2 after reopen (got %3)").arg(n).arg(v).arg(pval(w2.document(), n)));
  for (const char* n : {"W", "D", "Dh"}) {
    const auto* p = w2.document().params.find(n);
    check(p && p->exposed, QString("exposure of %1 persisted").arg(n));
  }
  const pc::Feature *ex = nullptr, *fl = nullptr;
  for (const auto& f : w2.document().features) { if (f.type == pc::FeatureType::Extrude) ex = &f; if (f.type == pc::FeatureType::Fillet) fl = &f; }
  check(ex && ex->prop("distance", "") == "T", "Extrude.distance == T persisted");
  check(fl && fl->prop("radius", "") == "R", "Fillet.radius == R persisted");
  const Box b = bbox(w2);
  check(near(b.dx, 80) && near(b.dy, 40) && near(b.dz, 10), "geometry after reopen: " + boxStr(b));
}

// (f) Save as App
struct Saved { QString exe, pkg; };
Saved stepSaveAsApp(pcd::MainWindow& w, const QString& outDir) {
  section("f) File > Uygulama Olarak Kaydet");
  Saved out;
  const QString base = outDir + "/PlateApp";
  QFile::remove(base);
  QFile::remove(base + ".pcapp");
  QAction* act = findActionStarting(&w, "Uygulama Olarak Kaydet");
  check(act != nullptr, "menu entry 'Uygulama Olarak Kaydet' exists");
  if (!act) return out;
  bool sawDialog = false, finished = false;
  g_boxes.clear();
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) {
      if (g_boxes.last().startsWith("Uygulama kaydedildi")) finished = true;
      return;
    }
    if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
    pch::expectCloseWithin(m, 240000);   // builds the executable while the dialog is open
    sawDialog = true;
    auto* table = m->findChild<QTableWidget*>();
    check(table != nullptr, "Save-as-App dialog has a parameter table");
    if (!table) { clickLater(childWithText<QPushButton>(m, "İptal")); return; }
    auto rowOf = [&](const QString& n) {
      for (int r = 0; r < table->rowCount(); ++r) if (table->item(r, 1) && table->item(r, 1)->text() == n) return r;
      return -1;
    };
    // listed + pre-ticked as chosen in the designer
    for (const char* n : {"W", "D", "Dh", "T", "R"}) check(rowOf(n) >= 0, QString("dialog lists parameter %1").arg(n));
    for (const char* n : {"W", "D", "Dh"}) {
      const int r = rowOf(n);
      check(r >= 0 && table->item(r, 0)->checkState() == Qt::Checked, QString("%1 pre-ticked (exposed in the designer)").arg(n));
    }
    {
      const int rt = rowOf("T"), rr2 = rowOf("R");
      const Qt::CheckState wantT = g_newParamHasExposed ? Qt::Checked : Qt::Unchecked;
      check(rt >= 0 && table->item(rt, 0)->checkState() == wantT, QString("T tick state matches what was chosen at creation (%1)").arg(g_newParamHasExposed ? "ticked" : "unticked"));
      check(rr2 >= 0 && table->item(rr2, 0)->checkState() == Qt::Unchecked, "R not ticked yet");
    }
    // tick T and R, set label / min / max / group per parameter
    struct Cfg { const char* n; const char* label; const char* mn; const char* mx; const char* grp; bool tick; };
    const Cfg cfg[] = {{"W", "Genişlik", "30", "120", "Boyutlar", false}, {"D", "Derinlik", "20", "80", "Boyutlar", false},
                       {"Dh", "Delik çapı", "4", "20", "Delik", false},   {"T", "Kalınlık", "0.1", "30", "Boyutlar", true},
                       {"R", "Köşe yarıçapı", "0.5", "5", "Boyutlar", true}};
    for (const auto& c : cfg) {
      const int r = rowOf(c.n);
      if (r < 0) continue;
      if (c.tick && table->item(r, 0)->checkState() != Qt::Checked) {
        const QRect vr = table->visualRect(table->model()->index(r, 0));
        QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, QPoint(vr.left() + 12, vr.center().y()));
        if (table->item(r, 0)->checkState() != Qt::Checked) table->item(r, 0)->setCheckState(Qt::Checked);
      }
      table->item(r, 2)->setText(c.label);
      table->item(r, 3)->setText(c.mn);
      table->item(r, 4)->setText(c.mx);
      table->item(r, 6)->setText(c.grp);
    }
    QList<QLineEdit*> edits;
    for (auto* e : m->findChildren<QLineEdit*>())
      if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox")) && !(e->parent() && e->parent()->inherits("QTableWidget")) && e->parent() != table->viewport()) edits << e;
    QLineEdit *name = edits.value(0), *version = edits.value(1), *author = edits.value(2), *path = edits.value(3);
    check(name && version && author && path, "dialog name/version/author/path fields found");
    if (!(name && path)) { clickLater(childWithText<QPushButton>(m, "İptal")); return; }
    name->setText("Plaka Konfigüratör");
    version->setText("1.0.0");
    author->setText("QA");
    auto* both = childWithText<QRadioButton>(m, "İkisi birden");
    if (both) QTest::mouseClick(both, Qt::LeftButton);
    path->setText(base);
    if (auto* lint = childWithText<QPushButton>(m, "Denetle")) QTest::mouseClick(lint, Qt::LeftButton);
    QTimer::singleShot(500, m, [m, &w] {
      saveShot(m->grab().toImage(), "06_saveas_dialog.png");
      clickLater(childWithText<QPushButton>(m, "Uygulamayı Kaydet"));
      (void)w;
    });
  };
  QTimer::singleShot(0, act, &QAction::trigger);
  check(waitFor([&] { return finished && !QApplication::activeModalWidget(); }, 180000), "Save-as-App finished with success message");
  check(sawDialog, "Save-as-App dialog appeared");
  g_onModal = nullptr;
#ifdef Q_OS_WIN
  const QString exe = base + ".exe";   // the package writer appends .exe on Windows
#else
  const QString exe = base;
#endif
  const QFileInfo fe(exe), fp(base + ".pcapp");
  check(fe.exists() && fe.isExecutable() && fe.size() > 1000000, "standalone app written: " + exe);
  check(fp.exists() && fp.size() > 100, ".pcapp written");
  out.exe = exe;
  out.pkg = base + ".pcapp";
  std::string err;
  if (auto pkg = pc::app::loadPackage(out.pkg.toStdString(), &err)) {
    std::set<std::string> ex;
    for (const auto& p : pkg->document.params.all()) if (p.exposed) ex.insert(p.name);
    check(ex == std::set<std::string>({"W", "D", "Dh", "T", "R"}), "package exposes exactly W, D, Dh, T, R: " + setStr(ex));
    const auto* t = pkg->document.params.find("T");
    check(t && t->label == "Kalınlık" && t->group == "Boyutlar" && near(t->minValue, 0.1) && near(t->maxValue, 30), "T label/group/min/max stored in the package");
  } else check(false, ".pcapp loads: " + QString::fromStdString(err));
  return out;
}

// (g) generated app
struct RunnerUi {
  pc::runner::RunnerWindow* win = nullptr;
  QLabel* status = nullptr;
  QLabel* info = nullptr;
  QPushButton* bannerApply = nullptr;
  QWidget* banner() const { return bannerApply ? bannerApply->parentWidget() : nullptr; }
  bool bannerShown() const { return banner() && banner()->isVisible(); }
  QDoubleSpinBox* spin(const QString& param) const {
    for (auto* s : win->findChildren<QDoubleSpinBox*>()) {
      QWidget* box = s->parentWidget();
      for (auto* l : box->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        if (l->toolTip() == param) return s;
    }
    return nullptr;
  }
  QLabel* label(const QString& param) const {
    for (auto* l : win->findChildren<QLabel*>())
      if (l->toolTip() == param) return l;
    return nullptr;
  }
  bool changeAndWait(const std::function<void()>& change, int timeoutMs = 40000) {
    status->setText("harness: bekleniyor");
    change();
    return waitFor([&] { return status->text().startsWith("Doğrulama tamamlandı"); }, timeoutMs);
  }
  // bbox "a × b × c mm" from the info strip
  bool infoBox(double* dx, double* dy, double* dz) const {
    const QString t = info->text();
    const QString s = t.section("Sınırlayıcı kutu:</b> ", 1).section(" mm", 0, 0);
    const QStringList p = s.split(QString::fromUtf8(" × "));
    if (p.size() != 3) return false;
    *dx = p[0].toDouble(); *dy = p[1].toDouble(); *dz = p[2].toDouble();
    return true;
  }
};

void stepApp(const Saved& s, const QString& outDir) {
  section("g) generated app: separate process");
  auto run = [](const QString& prog, const QStringList& args, QByteArray* so, int ms = 120000) {
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(ms)) { p.kill(); p.waitForFinished(2000); return -100; }
    if (so) *so = p.readAllStandardOutput();
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
  };
  const QString shot = outDir + "/app_default.png";
  QFile::remove(shot);
  int rc = run(s.exe, {"--screenshot", shot}, nullptr);
  check(rc == 0 && QFileInfo(shot).size() > 5000, QString("app --screenshot works (rc %1, %2 bytes)").arg(rc).arg(QFileInfo(shot).size()));
  if (QFileInfo::exists(shot)) { QImage img(shot); saveShot(img, "07_app_default.png"); }
  QByteArray so;
  rc = run(s.exe, {"--validate-only", "--text"}, &so);
  check(rc == 0, QString("app --validate-only --text exit 0 (got %1)").arg(rc));
  check(so.contains("UYGUN"), "validate-only text says UYGUN");
  rc = run(s.exe, {"--validate-only", "--set", "T=0.2"}, &so);
  check(rc == 2, QString("app --validate-only --set T=0.2 exits 2 (manufacturing violation) (got %1)").arg(rc));
  rc = run(s.exe, {"--validate-only", "--set", "W=90"}, nullptr);
  check(rc == 0, QString("app --validate-only --set W=90 exit 0 (got %1)").arg(rc));

  section("g2) generated app: in-process runner window");
  std::string err;
  auto def = pc::app::loadPackage(s.pkg.toStdString(), &err);
  check(def.has_value(), "load .pcapp");
  if (!def) return;
  def->liveUpdate = true;
  RunnerUi u;
  u.win = new pc::runner::RunnerWindow(*def);
  bool settled = false;
  QObject::connect(u.win, &pc::runner::RunnerWindow::settled, [&settled] { settled = true; });
  u.win->show();
  u.win->startup();
  check(waitFor([&] { return settled; }, 90000), "runner settled after startup");
  for (auto* l : u.win->statusBar()->findChildren<QLabel*>()) if (l->text().startsWith("Doğrulama")) u.status = l;
  for (auto* l : u.win->findChildren<QLabel*>()) if (l->text().contains("Hacim:")) u.info = l;
  u.bannerApply = childWithText<QPushButton>(u.win, "Önerileri Uygula");
  check(u.status && u.info && u.bannerApply, "runner widgets located");
  if (!(u.status && u.info && u.bannerApply)) { delete u.win; return; }
  pump(500);

  struct Exp { const char* n; const char* label; double def, lo, hi; };
  const Exp exps[] = {{"W", "Genişlik", 80, 30, 120}, {"D", "Derinlik", 40, 20, 80}, {"Dh", "Delik çapı", 10, 4, 20}, {"T", "Kalınlık", 10, 0.1, 30}, {"R", "Köşe yarıçapı", 2, 0.5, 5}};
  for (const auto& e : exps) {
    auto* sp = u.spin(e.n);
    check(sp != nullptr, QString("input widget for %1 exists").arg(e.n));
    if (!sp) continue;
    auto* lb = u.label(e.n);
    check(lb && lb->text() == e.label, QString("%1 label '%2' (got '%3')").arg(e.n, e.label, lb ? lb->text() : "-"));
    check(near(sp->value(), e.def), QString("%1 default value %2 (got %3)").arg(e.n).arg(e.def).arg(sp->value()));
    check(near(sp->minimum(), e.lo) && near(sp->maximum(), e.hi), QString("%1 range [%2, %3] (got [%4, %5])").arg(e.n).arg(e.lo).arg(e.hi).arg(sp->minimum()).arg(sp->maximum()));
    QWidget* gb = sp->parentWidget();
    while (gb && !gb->inherits("QGroupBox")) gb = gb->parentWidget();
    const QString want = (QString(e.n) == "Dh") ? "Delik" : "Boyutlar";
    check(gb && qobject_cast<QGroupBox*>(gb) && qobject_cast<QGroupBox*>(gb)->title() == want, QString("%1 sits in group '%2'").arg(e.n, want));
  }
  double dx = 0, dy = 0, dz = 0;
  check(u.infoBox(&dx, &dy, &dz) && near(dx, 80, 0.06) && near(dy, 40, 0.06) && near(dz, 10, 0.06), QString("app default bbox 80x40x10 (got %1 x %2 x %3)").arg(dx).arg(dy).arg(dz));

  // W via the spin box -> regenerates
  auto* sW = u.spin("W");
  if (sW) {
    check(u.changeAndWait([&] { sW->setValue(100); }), "validation completed after W=100");
    check(u.infoBox(&dx, &dy, &dz) && near(dx, 100, 0.06) && near(dy, 40, 0.06) && near(dz, 10, 0.06), QString("bbox follows W=100 (got %1 x %2 x %3)").arg(dx).arg(dy).arg(dz));
    check(!u.bannerShown(), "no banner for W=100");
    saveShot(u.win->grab().toImage(), "08_app_after_W100.png");
  }
  // Dh via the spin box -> volume changes
  auto* sDh = u.spin("Dh");
  if (sDh) {
    const QString i0 = u.info->text();
    check(u.changeAndWait([&] { sDh->setValue(16); }), "validation completed after Dh=16");
    check(u.info->text() != i0 || u.bannerShown(), "info strip reacts to Dh=16 (hole diameter is wired through the sketch dimension)");
    u.changeAndWait([&] { sDh->setValue(10); });
  }
  // T violating the manufacturing rule
  auto* sT = u.spin("T");
  if (sT) {
    check(u.changeAndWait([&] { sT->setValue(0.2); }), "validation completed after T=0.2");
    check(u.bannerShown() && u.bannerApply->isVisible(), "violation banner with 'Önerileri Uygula' shown for T=0.2");
    saveShot(u.win->grab().toImage(), "09_app_violation_T.png");
    check(u.changeAndWait([&] { QTest::mouseClick(u.bannerApply, Qt::LeftButton); }), "validation completed after applying the suggestion");
    check(sT->value() > 0.2 + 1e-9, QString("T moved to the suggested value (%1)").arg(sT->value()));
    check(!u.bannerShown(), "banner gone after applying the suggestion");
    u.changeAndWait([&] { sT->setValue(10); });
  }
  // STL export via menu + file dialog; compare the STL bbox with W=100,T=10
  const QString stl = outDir + "/qa_export.stl";
  QFile::remove(stl);
  QAction* aStl = nullptr;
  for (auto* a : u.win->findChildren<QAction*>()) if (a->text().startsWith("STL")) aStl = a;
  check(aStl != nullptr, "STL export action exists");
  if (aStl) {
    const bool ok = pickFileInDialog(stl, [aStl] { aStl->trigger(); }, [&] { return QFileInfo::exists(stl) && u.status->text().startsWith("Kaydedildi"); }, 30000);
    check(ok, "STL exported through the file dialog");
    QFile f(stl);
    if (f.open(QIODevice::ReadOnly)) {
      const QByteArray b = f.readAll();
      quint32 n = 0;
      if (b.size() >= 84) std::memcpy(&n, b.constData() + 80, 4);
      double mn[3] = {1e9, 1e9, 1e9}, mx[3] = {-1e9, -1e9, -1e9};
      const bool valid = n > 100 && qint64(b.size()) == 84 + 50 * qint64(n);
      for (quint32 i = 0; valid && i < n; ++i)
        for (int k = 0; k < 3; ++k)
          for (int c = 0; c < 3; ++c) {
            float x;
            std::memcpy(&x, b.constData() + 84 + 50 * i + 12 + 12 * k + 4 * c, 4);
            mn[c] = std::min(mn[c], double(x)); mx[c] = std::max(mx[c], double(x));
          }
      check(valid && near(mx[0] - mn[0], 100, 0.01) && near(mx[1] - mn[1], 40, 0.01) && near(mx[2] - mn[2], 10, 0.01),
            QString("STL valid (%1 tris), bbox %2 x %3 x %4 (expected 100 x 40 x 10)").arg(n).arg(mx[0] - mn[0]).arg(mx[1] - mn[1]).arg(mx[2] - mn[2]));
    } else check(false, "STL file readable");
  }
  delete u.win;
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
#ifdef PC_QA_SHOTS_DIR
  g_shots = PC_QA_SHOTS_DIR;
#endif
  QString keep;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) g_shots = argv[++i];
    else if (!std::strcmp(argv[i], "--no-shots")) g_shots.clear();
    else if (!std::strcmp(argv[i], "--keep") && i + 1 < argc) keep = argv[++i];
  }
  QTemporaryDir tmp;
  const QString outDir = keep.isEmpty() ? tmp.path() : keep;
  QDir().mkpath(outDir);
  startModalPump();
  pch::checkFn() = check;
  pch::startWatchdog([] { return g_fail; });

  QString pcad;
  {
    pcd::MainWindow w;
    w.resize(1500, 900);
    w.show();
    pump(400);
    stepSketch(w);
    stepFeatures(w);
    pcad = stepEditAndPersist(w, outDir);
    w.setAutoYes(true);
    w.close();
  }
  Saved saved;
  {
    pcd::MainWindow w2;
    w2.resize(1500, 900);
    w2.show();
    pump(300);
    if (!pcad.isEmpty() && QFileInfo::exists(pcad)) {
      stepReopen(w2, pcad);
      saved = stepSaveAsApp(w2, outDir);
    } else check(false, "no .pcad written -> cannot reopen / Save as App");
    w2.setAutoYes(true);
    w2.close();
  }
  if (!saved.exe.isEmpty()) stepApp(saved, outDir);

  std::printf("\n%s: %d failure(s)\n", g_fail ? "FAILED" : "PASSED", g_fail);
  for (const auto& s : g_failures) std::printf("  - %s\n", qPrintable(s));
  return g_fail;
}
