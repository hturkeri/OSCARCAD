// SC-UX acceptance harness — WP-A shell (SH). See docs/spaceclaim_ux/ACCEPTANCE.md.
//   pc_gui_harness_sc_shell [--shots]       Exit code = number of failed checks.
// Covers the SH items of the acceptance list (1-3, 12 (mode part), 20 (panel), 22 (K on a face), 23 (structure),
// 27, 28, 29, 31-41) through the real MainWindow, real key clicks / mouse events and the shell widgets.
// Works with the P0 stub ToolManager and with WP-B's real one (only ToolApi.h is used for tool state).
// --shots writes docs/spaceclaim_ux/shots/A_*.png (screenshots of the shell).
#include "MainWindow.h"
#include "OptionsPanel.h"
#include "PropertiesPanel.h"
#include "Ribbon.h"
#include "StatusBarWidgets.h"
#include "StructurePanel.h"
#include "ToolApi.h"
#include "ExprEdit.h"
#include "harness_robust.h"
#include "sc_stub_host.h"
#include "pc/Model.h"
#include <BRepAdaptor_Curve.hxx>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
int g_fail = 0;
bool g_shots = false;
void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  pch::kick();
  if (!ok) ++g_fail;
}
void note(const QString& s) { std::printf("  [note] %s\n", qPrintable(s)); std::fflush(stdout); pch::kick(); }
void section(const char* s) { std::printf("== %s\n", s); std::fflush(stdout); }
void pump(int ms) { QTest::qWait(ms); }
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  for (int t = 0; t < timeoutMs; t += 30) {
    if (pred()) return true;
    QTest::qWait(30);
  }
  return pred();
}
void shot(pcd::MainWindow& w, const char* name) {
  if (!g_shots) return;
  pump(250);
  const QString dir = QString(PC_SC_SHOTS_DIR);
  QDir().mkpath(dir);
  w.grab().save(dir + "/" + name);
  note(QString("shot %1/%2").arg(dir, name));
}

// ---- modal pump: message boxes are answered, other modals are handed to g_onModal
std::function<void(QWidget*)> g_onModal;
QStringList g_boxes;
void startModalPump() {
  auto* t = new QTimer(qApp);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (!m || m->property("scHandled").toBool()) return;
    if (pch::enforceDeadline(m)) return;
    m->setProperty("scHandled", true);
    if (auto* mb = qobject_cast<QMessageBox*>(m)) {
      g_boxes << mb->text();
      QAbstractButton* b = mb->button(QMessageBox::Yes);
      if (!b) b = mb->button(QMessageBox::Ok);
      if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
      if (b) QTimer::singleShot(0, b, &QAbstractButton::click);
      return;
    }
    if (g_onModal) g_onModal(m);
  });
  t->start(60);
}
// active popup menu (exec'd context menus): trigger the entry with `text`
void triggerPopupLater(const QString& text, int ms = 250) {
  QTimer::singleShot(ms, [text] {
    auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (!m) return;
    for (QAction* a : m->actions())
      if (a->text() == text) { m->close(); a->trigger(); return; }
    m->close();
  });
}
void closePopups() {
  for (int i = 0; i < 4; ++i)
    if (QWidget* p = QApplication::activePopupWidget()) p->close();
  for (QWidget* tw : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(tw); m && m->isVisible()) m->close();
}

void key(QWidget* w, Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QTest::keyClick(w, k, m);
  pump(60);
}
QWidget* view(pcd::MainWindow& w) { return w.viewerWidget(); }
QWidget* keyTarget(pcd::MainWindow& w) { return w.inSketchMode() ? static_cast<QWidget*>(w.sketchEditor()) : view(w); }

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
std::string pexpr(pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<missing>");
}
double volume(pcd::MainWindow& w) {
  try { return pc::model::massProperties(w.modelResult().shape).volume; } catch (...) { return -1; }
}
QString firstFaceOf(pcd::MainWindow& w, const QString& prefix) {
  for (const auto& [n, f] : w.modelResult().faces)
    if (QString::fromStdString(n).startsWith(prefix)) return QString::fromStdString(n);
  return QString();
}
QToolButton* ribbonButton(pcd::MainWindow& w, QAction* a) {
  for (auto* tb : w.ribbon()->findChildren<QToolBar*>())
    if (auto* b = qobject_cast<QToolButton*>(tb->widgetForAction(a)); b && b->isVisible()) return b;
  return nullptr;
}
QImage viewImage(pcd::MainWindow& w) { return w.viewer()->grabFramebuffer().convertToFormat(QImage::Format_RGB32); }
int diffPixels(const QImage& a, const QImage& b) {
  if (a.size() != b.size()) return a.width() * a.height();
  int n = 0;
  for (int y = 0; y < a.height(); y += 2)
    for (int x = 0; x < a.width(); x += 2) {
      const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
      if (std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q)) > 30) ++n;
    }
  return n;
}
// the viewer's signal path (a click on that face). Integrated build: the SpaceClaim scheme reports every left click
// through ShapeViewer::picked (name, kind, modifiers, click count, position); the legacy scheme through facePicked.
void emitPick(pcd::MainWindow& w, const QString& name, int kind) {
  if (w.viewer()->navigationScheme() == pc::viewer::ShapeViewer::NavigationScheme::SpaceClaim)
    emit w.viewer()->picked(name, kind, Qt::NoModifier, 1, QPoint(-1, -1));
  else if (kind == pc::viewer::ShapeViewer::PickDatum)
    QMetaObject::invokeMethod(w.viewer(), "datumPicked", Q_ARG(QString, name));
  else
    QMetaObject::invokeMethod(w.viewer(), "facePicked", Q_ARG(QString, name));
}
void pickFace(pcd::MainWindow& w, const QString& name) {
  emitPick(w, name, pc::viewer::ShapeViewer::PickFace);
  pump(120);
}
void pickDatum(pcd::MainWindow& w, const QString& name) {
  emitPick(w, name, pc::viewer::ShapeViewer::PickDatum);
  pump(150);
  closePopups();   // the legacy datum menu pops up on a click
  pump(50);
}
QString msg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }

// ============================================================================ tests
void testLayout(pcd::MainWindow& w) {
  section("1-2) ribbon, quick access, File button, groups, panels, status bar");
  auto* rb = w.ribbon();
  check(rb && rb->isVisible(), "ribbon visible");
  if (!rb) return;
  // SC23 WP-S (S-P1-1): Sketch tab before Tasarım, like SpaceClaim 2023 R1 [G]
  check(rb->tabTexts() == QStringList({"Sketch", "Tasarım", "Görüntü", "Ölçüm", "İmalat"}), "ribbon tabs: " + rb->tabTexts().join(" · "));
  check(rb->currentTabKey() == "design", "Tasarım tab current in 3D");
  check(!w.menuBar()->isVisible(), "menu bar hidden");
  auto* qat = w.findChild<QToolBar*>("quickAccessBar");
  int qatVisible = 0;
  QStringList qatTexts;
  if (qat)
    for (QAction* a : qat->actions())
      if (auto* b = qat->widgetForAction(a); b && b->isVisible()) { ++qatVisible; qatTexts << a->text(); }
  check(qatVisible == 5, QString("quick access bar: 5 visible buttons (%1)").arg(qatTexts.join(", ")));
  auto* fb = w.findChild<QToolButton*>("ribbonFileButton");
  check(fb && fb->isVisible() && fb->text() == "Dosya" && fb->menu() && fb->menu()->objectName() == "menuFile", "blue 'Dosya' button with the File menu");
  if (fb && fb->menu()) {
    QStringList t;
    for (QAction* a : fb->menu()->actions()) if (!a->isSeparator()) t << a->text();
    for (const char* need : {"Yeni", "Aç...", "Kaydet", "Farklı Kaydet...", "Uygulama Olarak Kaydet... (Save as App)", "Dışa Aktar", "Son dosyalar", "Ayarlar", "Hakkında", "Çıkış"})
      check(t.contains(QString::fromUtf8(need)), QString("File menu has '%1'").arg(QString::fromUtf8(need)));
  }
  // group order in Tasarım
  rb->setCurrentTab("design");
  pump(50);
  QStringList order;
  int lastX = -1;
  bool increasing = true;
  // SC23 WP-S: the sketch drawing tools moved to the Sketch tab (Oluştur); Tasarım keeps the SpaceClaim Design order
  for (const char* k : {"orient", "mode", "edit", "intersect", "create"}) {
    auto* g = w.findChild<QWidget*>(QString("ribbonGroup.") + k);
    if (!g || !g->isVisible()) { increasing = false; order << QString(k) + "(missing)"; continue; }
    const int x = g->mapTo(&w, QPoint(0, 0)).x();
    if (x <= lastX) increasing = false;
    lastX = x;
    order << static_cast<pcd::RibbonGroup*>(g)->title();
  }
  check(increasing, "Tasarım groups left to right: " + order.join(" | "));
  check(!w.findChild<QWidget*>("ribbonGroup.clipboard"), "no Pano (Clipboard) group (SPEC §3.9)");
  for (const char* id : {"select", "pull", "move", "fill"}) {
    QAction* a = w.toolManager()->action(id);
    auto* b = a ? ribbonButton(w, a) : nullptr;
    check(b && b->iconSize().width() >= 32 && b->toolButtonStyle() == Qt::ToolButtonTextUnderIcon,
          QString("'%1' is a large ribbon button (icon %2 px)").arg(a ? a->text() : id).arg(b ? b->iconSize().width() : 0));
  }
  // panels: Structure top-left, Options/Properties bottom-left, viewer centre, status bar bottom
  auto* ds = w.findChild<QDockWidget*>("dockStructure");
  auto* dopt = w.findChild<QDockWidget*>("dockOptions");
  auto* st = w.findChild<QTabWidget*>("structureTabs");
  auto* ot = w.findChild<QTabWidget*>("optionsTabs");
  check(ds && dopt && ds->isVisible() && dopt->isVisible(), "Structure and Options docks visible");
  if (ds && dopt) {
    const QRect rs = ds->geometry(), ro = dopt->geometry(), rv = QRect(view(w)->mapTo(&w, QPoint(0, 0)), view(w)->size());
    check(rs.right() < rv.left() + 5 && ro.right() < rv.left() + 5, "Structure / Options panels left of the graphics area");
    check(rs.top() < ro.top() && std::abs(rs.left() - ro.left()) < 4, "Options panel below the Structure panel");
    check(rb->mapTo(&w, QPoint(0, 0)).y() < rs.top() && rb->width() > w.width() - 20, "ribbon spans the window above the panels");
    check(w.statusBar()->geometry().top() > rv.bottom(), "status bar below the graphics area");
    check(rs.width() >= 250 && rs.width() <= 420, QString("Structure panel width %1 px (default 330)").arg(rs.width()));
  }
  if (st) {
    QStringList tabs;
    for (int i = 0; i < st->count(); ++i) tabs << st->tabText(i);
    // SC-2D-IMPORT: the Layers tab ("Katmanlar"); SC23 WP-S (S-P1-4): Views ("Görünümler") last, like SpaceClaim
    check(st->tabPosition() == QTabWidget::South && tabs == QStringList({"Yapı", "Geçmiş", "Seçim", "Gruplar", "Katmanlar", "Görünümler"}),
          "Structure tabs at the bottom: " + tabs.join(" · "));
  }
  if (ot) check(ot->tabPosition() == QTabWidget::South && ot->count() == 2 && ot->tabText(0) == "Seçenekler" && ot->tabText(1) == "Özellikler",
                "Options / Properties tabs at the bottom");
  check(ds && ds->windowTitle() == "Yapı" && dopt && dopt->windowTitle().startsWith("Seçenekler - "), "dock titles: 'Yapı', '" + (dopt ? dopt->windowTitle() : QString()) + "'");
  for (const char* n : {"quickMeasure", "selectionList", "selectionFilter", "issueCounts"})
    check(w.statusBar()->findChild<QWidget*>(n) != nullptr, QString("status bar widget '%1'").arg(n));
  check(w.findChildren<QTableView*>().size() >= 1 && paramView(w) && paramView(w)->model()->columnCount() == 10, "Gruplar = parameter table (10 columns)");
  int paramTables = 0;
  for (auto* v : w.findChildren<QTableView*>()) if (qobject_cast<QStandardItemModel*>(v->model())) ++paramTables;
  check(paramTables == 1, "exactly one QTableView with a QStandardItemModel (parameter table)");
  // every tool action has a SpaceClaim tooltip with the English term and the key
  QAction* pull = w.toolManager()->action("pull");
  check(pull && pull->toolTip().contains("(Pull)") && pull->toolTip().contains("P"), "tooltip 'Çek (Pull) … — P': " + (pull ? pull->toolTip() : QString()));
  shot(w, "A_3d_empty.png");
}

void testModes(pcd::MainWindow& w) {
  section("3, 12, 28, 29) modes K / X / D on an empty document; F = Fill; tools from 3D");
  w.newDocument();
  pump(200);
  const size_t nf = w.document().features.size();
  w.activateWindow();
  view(w)->setFocus();
  key(view(w), Qt::Key_K);
  check(waitFor([&] { return w.inSketchMode(); }, 2000), "K on an empty document opens Sketch mode");
  check(w.document().features.size() == nf, "document unchanged by K");
  check(w.sketchEditor()->isVisible() && w.sketchEditor()->parentWidget() == view(w), "sketch editor lies over the viewer (child, SPEC §6.1)");
  auto* aK = w.findChild<QAction*>("actModeSketch");
  check(aK && aK->isChecked(), "Mod: 'Sketch modu' checked");
  bool ctx = true;
  for (const char* k : {"constraints", "dimensions", "sketchSession"}) ctx = ctx && w.ribbon()->group(k) && w.ribbon()->group(k)->isVisible();
  check(ctx, "contextual groups Kısıtlar / Ölçüler / Sketch Oturumu visible");
  check(w.ribbon()->currentTabKey() == "sketch", "Sketch tab selected in sketch mode (SC23 S-P1-1)");
  for (const char* t : {"Dikdörtgen", "Seç", "Uzunluk", "Çakışık", "Çember", "Çap", "Sığdır", "Sketch'i Bitir", "Vazgeç"}) {
    bool vis = false;
    for (auto* tb : w.findChildren<QToolButton*>()) if (tb->defaultAction() && tb->defaultAction()->text() == QString::fromUtf8(t) && tb->isVisible()) vis = true;
    check(vis, QString("visible ribbon button '%1' in sketch mode").arg(QString::fromUtf8(t)));
  }
  QRect rr = w.ribbon()->geometry();
  for (const char* t : {"Vazgeç", "Sketch'i Bitir"})
    for (auto* tb : w.findChildren<QToolButton*>())
      if (tb->defaultAction() && tb->defaultAction()->text() == QString::fromUtf8(t) && tb->isVisible())
        check(tb->mapTo(&w, QPoint(tb->width(), 0)).x() <= w.width(), QString("'%1' fits in the window width").arg(QString::fromUtf8(t)));
  (void)rr;
  // R (stub bridge or real tool): rectangle by two clicks
  key(w.sketchEditor(), Qt::Key_R);
  check(w.toolManager()->activeId() == "sketch.rect", "R activates the rectangle tool (" + w.toolManager()->activeId() + ")");
  auto* ed = w.sketchEditor();
  const QPoint c(ed->width() / 2, ed->height() / 2);
  if (!w.toolManager()->active()) {   // stub: two clicks draw a rectangle (SketchEditor)
    QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(-120, -70));
    pump(60);
    QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(120, 70));
    pump(100);
  } else {
    w.sketchEditor()->setTool(pcd::SketchEditor::Tool::Rect);
    QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(-120, -70));
    QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(120, 70));
    pump(100);
  }
  check(ed->sketch().entities.size() == 4, QString("rectangle drawn (%1 entities)").arg(ed->sketch().entities.size()));
  shot(w, "A_sketch.png");
  // typing in the plane-offset field must not trigger D / P / K (ACC 37, sketch side)
  if (auto* off = w.findChild<pcd::ExprEdit*>("sketchOffset")) {
    off->lineEdit()->setFocus();
    pump(50);
    QTest::keyClicks(off->lineEdit(), "DPK");
    pump(100);
    check(w.inSketchMode() && w.toolManager()->activeId() == "sketch.rect", "typing 'DPK' in the offset field: still sketching, tool unchanged");
    off->lineEdit()->clear();
    off->setText("0");
    ed->setFocus();
    pump(50);
  }
  const int undo0 = w.undoStack()->count();
  key(ed, Qt::Key_D);
  check(waitFor([&] { return !w.inSketchMode(); }, 2000), "D commits the sketch and returns to 3D");
  check(w.document().features.size() == nf + 1 && w.document().features.back().type == pc::FeatureType::Sketch, "Sketch feature in the document");
  check(w.undoStack()->count() == undo0 + 1, "one undo step for the sketch");
  bool inHistory = false;
  for (int i = 0; i < w.featureTree()->topLevelItemCount(); ++i) inHistory |= w.featureTree()->topLevelItem(i)->text(0).startsWith("Sketch1");
  check(inHistory, "History tab lists Sketch1");
  auto* a3 = w.findChild<QAction*>("actMode3D");
  check(a3 && a3->isChecked() && w.toolManager()->activeId() == "select", "3B modu checked, active tool Seç");
  // SC23 WP-S (S-P1-1): the contextual groups live on the Sketch tab; in 3D the Tasarım tab is current and the
  // Sketch-tab groups are disabled (not hidden)
  bool ctxOff = w.ribbon()->currentTabKey() == "design";
  for (const char* k : {"constraints", "dimensions", "sketchSession"}) ctxOff = ctxOff && !w.ribbon()->group(k)->isVisible() && !w.ribbon()->group(k)->isEnabled();
  check(ctxOff, "3D: Tasarım tab current, contextual sketch groups (Sketch tab) disabled");

  // X / D / K section semantics (28)
  auto* sc = w.sectionControls();
  key(view(w), Qt::Key_X);
  check(sc->sectionOn() && w.viewer()->sectionEnabled(), "X opens the section view");
  check(sc->toggleAction()->isChecked(), "Kesit modu button checked");
  key(view(w), Qt::Key_D);
  check(!sc->sectionOn(), "D closes the section view");
  key(view(w), Qt::Key_X);
  key(view(w), Qt::Key_X);
  check(!sc->sectionOn(), "X twice: section off again");

  // F = Fill (not fit) (29)
  const QImage before = viewImage(w);
  key(view(w), Qt::Key_F);
  check(w.toolManager()->activeId() == "fill", "F activates Doldur (Fill): " + w.toolManager()->activeId());
  pump(150);
  check(diffPixels(before, viewImage(w)) < 50, "F does not change the camera");
  key(view(w), Qt::Key_S);
  check(w.toolManager()->activeId() == "select", "S activates Seç");
  // a sketch tool from 3D enters sketch mode (SPEC §2.1 [U]); then D leaves it (empty sketch discarded)
  w.newDocument();
  pump(150);
  key(view(w), Qt::Key_L);
  check(waitFor([&] { return w.inSketchMode(); }, 1500), "L in 3D on an empty document enters Sketch mode");
  key(w.sketchEditor(), Qt::Key_D);
  check(!w.inSketchMode() && w.document().features.empty(), "D with an empty sketch: discarded, back in 3D");
}

void testOrient(pcd::MainWindow& w) {
  section("27) H / Z / Alt+Z / ← → and mouse navigation (shell side)");
  check(w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad"), "open samples/bracket.pcad");
  pump(500);
  auto sc = [&](const char* n) { QAction* a = w.findChild<QAction*>(n); return a ? a->shortcuts() : QList<QKeySequence>(); };
  check(sc("actHomeView").contains(QKeySequence("H")), "H = Ana görünüm");
  check(sc("actPlanView").contains(QKeySequence("V")), "V = Plan görünümü");
  check(sc("actZoomExtents").contains(QKeySequence("Z")) && sc("actZoomExtents").contains(QKeySequence("Alt+Z")), "Z and Alt+Z = Sığdır");
  check(sc("actPrevView").contains(QKeySequence(Qt::Key_Left)) && sc("actNextView").contains(QKeySequence(Qt::Key_Right)), "← / → = previous / next view");
  QAction* fit = w.findChild<QAction*>("actZoomExtents");
  check(fit && fit->text() == "Sığdır", "the Zoom Extents action keeps the text 'Sığdır'");
  view(w)->setFocus();
  key(view(w), Qt::Key_Right, Qt::ControlModifier);   // harmless
  const QImage i0 = viewImage(w);
  // wheel zooms
  QWheelEvent we(QPointF(view(w)->width() / 2.0, view(w)->height() / 2.0), view(w)->mapToGlobal(QPointF(view(w)->width() / 2.0, view(w)->height() / 2.0)),
                 QPoint(), QPoint(0, 360), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(view(w), &we);
  pump(150);
  const QImage i1 = viewImage(w);
  check(diffPixels(i0, i1) > 500, QString("wheel zooms (%1 px changed)").arg(diffPixels(i0, i1)));
  key(view(w), Qt::Key_H);
  pump(150);
  const QImage iHome = viewImage(w);
  check(diffPixels(i1, iHome) > 500, "H returns to the home view (image changed back)");
  key(view(w), Qt::Key_Z);
  pump(100);
  check(diffPixels(iHome, viewImage(w)) < 400, "Z (fit) from the home view keeps the picture");
  // middle drag = spin, Shift+middle = pan, Ctrl+middle = zoom, left drag in empty space = nothing
  auto drag = [&](Qt::MouseButton b, Qt::KeyboardModifiers m, QPoint from, QPoint to) {
    QTest::mousePress(view(w), b, m, from);
    for (int i = 1; i <= 8; ++i) {
      const QPoint p = from + (to - from) * i / 8;
      QMouseEvent mv(QEvent::MouseMove, p, view(w)->mapToGlobal(p), Qt::NoButton, b, m);
      QCoreApplication::sendEvent(view(w), &mv);
      pump(10);
    }
    QTest::mouseRelease(view(w), b, m, to);
    pump(150);
  };
  const QPoint corner(30, 30);   // empty background near the top-left corner of the viewer
  QString picked = "<none>";
  auto conn = QObject::connect(w.viewer(), &pc::viewer::ShapeViewer::facePicked, [&](const QString& n) { picked = n; });
  QImage a = viewImage(w);
  drag(Qt::LeftButton, Qt::NoModifier, corner, corner + QPoint(160, 60));
  check(diffPixels(a, viewImage(w)) < 100, "left drag in empty space does not rotate the model");
  check(picked == "<none>", "left drag does not pick (" + picked + ")");
  a = viewImage(w);
  drag(Qt::MiddleButton, Qt::NoModifier, corner, corner + QPoint(160, 60));
  const QImage spun = viewImage(w);
  check(diffPixels(a, spun) > 500, QString("middle drag spins (%1 px)").arg(diffPixels(a, spun)));
  check(picked == "<none>", "middle drag does not pick");
  drag(Qt::MiddleButton, Qt::ShiftModifier, corner, corner + QPoint(120, 0));
  const QImage panned = viewImage(w);
  check(diffPixels(spun, panned) > 500, "Shift+middle drag pans");
  drag(Qt::MiddleButton, Qt::ControlModifier, corner + QPoint(0, 200), corner + QPoint(0, 60));
  check(diffPixels(panned, viewImage(w)) > 500, "Ctrl+middle drag (up) zooms");
  // Çevir (Spin) toggle: left drag rotates while it is active
  if (QAction* spin = w.findChild<QAction*>("actSpin")) {
    spin->setChecked(true);
    a = viewImage(w);
    drag(Qt::LeftButton, Qt::NoModifier, corner, corner + QPoint(150, 40));
    check(diffPixels(a, viewImage(w)) > 500, "with 'Çevir' active the left drag rotates");
    spin->setChecked(false);
    check(w.leftDragMode() == 0, "Çevir toggled off");
  }
  QObject::disconnect(conn);
  key(view(w), Qt::Key_H);
  key(view(w), Qt::Key_Left);
  check(!msg(w).isEmpty(), "← answers (previous view or a status message): " + msg(w));
}

void testSelectionAndProperties(pcd::MainWindow& w) {
  section("32-34, 39, 40) selection, Properties panel, Groups, status bar, filter");
  if (w.document().name != "FlanschBraket") { w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad"); pump(400); }
  auto* sel = w.selection();
  // 40: status bar selection list
  pickFace(w, "Extrude1.end");
  auto* sl = w.statusBar()->findChild<QLabel*>("selectionList");
  check(sl && sl->text() == "1 yüz: Extrude1.end", "status bar selection list: '" + (sl ? sl->text() : QString()) + "'");
  for (const char* lab : {"Yenileme:", "Hacim:", "Hata:", "Sketch DOF:"}) {
    bool found = false;
    for (auto* l : w.statusBar()->findChildren<QLabel*>()) if (l->text().startsWith(QString::fromUtf8(lab))) found = true;
    check(found, QString("status bar keeps '%1'").arg(QString::fromUtf8(lab)));
  }
  // 39: selection filter "Kenar" -> a face click selects nothing
  auto* fc = w.statusBar()->findChild<QComboBox*>("selectionFilter");
  sel->clear();
  if (fc) {
    fc->setCurrentIndex(fc->findText("Kenar"));
    emit fc->activated(fc->currentIndex());
    pump(50);
  }
  check(sel->filter() == pcd::SelFilter::Edge, "status bar filter = Kenar");
  pickFace(w, "Extrude1.end");
  check(sel->isEmpty(), "filter 'Kenar': clicking a face selects nothing");
  if (fc) { fc->setCurrentIndex(0); emit fc->activated(0); pump(50); }
  check(sel->filter() == pcd::SelFilter::Smart, "filter back to Akıllı");

  // 32: body properties + rename
  auto* props = w.propertiesPanel();
  auto* ot = w.findChild<QTabWidget*>("optionsTabs");
  if (ot) ot->setCurrentIndex(1);
  w.structurePanel()->setCurrentPage("structure");
  QTreeWidgetItem* bodyItem = nullptr;
  if (auto* bn = w.structurePanel()->bodiesNode())
    for (int i = 0; i < bn->childCount(); ++i) if (bn->child(i)->text(0) == "Extrude1") bodyItem = bn->child(i);
  check(bodyItem != nullptr, "Yapı tree: Gövdeler > Extrude1");
  if (bodyItem) {
    auto* tr = w.structurePanel()->tree();
    tr->scrollToItem(bodyItem);
    const QRect r = tr->visualItemRect(bodyItem);
    QTest::mouseClick(tr->viewport(), Qt::LeftButton, {}, QPoint(r.right() - 10, r.center().y()));
    pump(200);
  }
  check(sel->count() == 1 && sel->items().first().kind == pcd::SelItem::Kind::Body, "click on the body in the Yapı tree selects the body");
  pump(100);
  const QStringList rows = props->rowLabels();
  check(rows.contains("Ad") && rows.contains("Hacim") && rows.contains("Oluşturan özellik"), "Properties: Ad, Hacim, Oluşturan özellik (" + rows.join(", ") + ")");
  check(props->valueText("Oluşturan özellik") == "Extrude1", "Properties: Oluşturan özellik = Extrude1");
  shot(w, "A_properties_body.png");
  if (auto* ne = qobject_cast<QLineEdit*>(props->editor("name"))) {
    ne->setFocus();
    pch::typeInto(ne, "Govde", "body name");
    QTest::keyClick(ne, Qt::Key_Return);
    pump(400);
  }
  check(w.bodyNames().contains("Govde"), "renaming in Properties renames the body: " + w.bodyNames().join(", "));
  // 33: fillet face -> Yarıçap editable, picker binds the parameter
  const QString ff = firstFaceOf(w, "Fillet1.");
  check(!ff.isEmpty(), "bracket has a fillet face: " + ff);
  if (!ff.isEmpty()) {
    pickFace(w, ff);
    pump(150);
    auto* re = qobject_cast<pcd::ExprEdit*>(props->editor("radius"));
    check(re != nullptr && props->rowLabels().contains("Yarıçap"), "Properties of a fillet face: editable 'Yarıçap'");
    if (re) {
      check(re->text() == "R", "Yarıçap shows the expression 'R'");
      QComboBox* pick = re->picker();
      int idx = -1;
      for (int i = 0; i < pick->count(); ++i) if (pick->itemText(i).startsWith("E =") || pick->itemText(i).startsWith("E ")) idx = i;
      check(idx >= 0, "parameter picker lists E");
      if (idx >= 0) {
        pick->setCurrentIndex(idx);
        emit pick->activated(idx);
        waitFor([&] { const auto* f = w.document().feature("Fillet1"); return f && f->prop("radius") == "E"; }, 3000);
      }
      const auto* f = w.document().feature("Fillet1");
      check(f && f->prop("radius") == "E", "picker 'E' -> Fillet1.radius == E (" + (f ? QString::fromStdString(f->prop("radius")) : QString()) + ")");
      shot(w, "A_properties_fillet.png");
      w.undoStack()->undo();
      pump(300);
      check(w.document().feature("Fillet1")->prop("radius") == "R", "undo restores radius = R");
    }
  }
  // 34: Gruplar: T = 12 keeps face names, volume changes
  w.structurePanel()->setCurrentPage("groups");
  pump(100);
  const double v0 = volume(w);
  auto* pv = paramView(w);
  const int rT = paramRow(w, "T");
  check(pv && pv->isVisible() && rT >= 0, "Gruplar tab shows the parameter table with T");
  if (pv && rT >= 0) {
    const QModelIndex idx = pv->model()->index(rT, 1);
    pv->setCurrentIndex(idx);
    pv->edit(idx);
    pump(80);
    if (auto* ed = pv->viewport()->findChild<QLineEdit*>()) {
      ed->selectAll();
      QTest::keyClicks(ed, "12");
      QTest::keyClick(ed, Qt::Key_Return);
    }
    waitFor([&] { return pexpr(w, "T") == "12"; }, 4000);
    check(pexpr(w, "T") == "12", "T = 12 through the Groups table");
    check(w.modelResult().faces.count("Extrude1.end") == 1, "face name Extrude1.end survives");
    check(std::fabs(volume(w) - v0) > 1.0, QString("volume changed (%1 -> %2)").arg(v0).arg(volume(w)));
    shot(w, "A_groups.png");
    // 37: typing "PD" in a cell editor triggers neither Çek nor 3B
    const QString tool0 = w.toolManager()->activeId();
    const QModelIndex ix = pv->model()->index(paramRow(w, "T"), 7);   // Etiket column
    pv->setCurrentIndex(ix);
    pv->edit(ix);
    pump(80);
    if (auto* ed = pv->viewport()->findChild<QLineEdit*>()) {
      QTest::keyClicks(ed, "PD");
      pump(80);
      check(w.toolManager()->activeId() == tool0 && !w.inSketchMode(), "typing 'PD' in a Groups cell: no tool / mode change");
      QTest::keyClick(ed, Qt::Key_Escape);
    } else {
      check(false, "cell editor opened");
    }
    // a focused table cell (not yet editing) also keeps letters for editing
    pv->setFocus();
    pv->setCurrentIndex(pv->model()->index(paramRow(w, "T"), 7));
    QTest::keyClick(pv, Qt::Key_P);
    pump(100);
    check(w.toolManager()->activeId() == tool0, "a letter on a focused editable cell does not trigger the P shortcut");
    if (QWidget* ed = pv->viewport()->findChild<QLineEdit*>()) QTest::keyClick(ed, Qt::Key_Escape);
    pump(50);
  }
  w.structurePanel()->setCurrentPage("structure");
}

void testSketchPlanes(pcd::MainWindow& w) {
  section("22, 31) K on a planar face (OnFace), K on a datum plane (OnPlane), K without selection (plane pick)");
  w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad");
  pump(400);
  view(w)->setFocus();
  pickFace(w, "Extrude1.end");
  key(view(w), Qt::Key_K);
  check(w.inSketchMode(), "K with a planar face selected opens a sketch");
  if (w.inSketchMode()) {
    const auto& pl = w.sketchEditor()->sketch().plane;
    check(pl.kind == pc::SketchPlaneKind::OnFace && pl.faceRef == "Extrude1.end", "sketch plane OnFace Extrude1.end");
    key(w.sketchEditor(), Qt::Key_D);
  }
  // datum plane
  QString edge;
  for (const auto& [n, e] : w.modelResult().edges) {
    try {
      if (BRepAdaptor_Curve(e).GetType() == GeomAbs_Line) { edge = QString::fromStdString(n); break; }
    } catch (...) {}
  }
  check(!edge.isEmpty() && w.createPlaneOnEdge(edge), "datum plane on " + edge);
  pump(300);
  QString plane;
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Plane) plane = QString::fromStdString(f.id);
  pickDatum(w, plane);
  check(w.selection()->contains(pcd::SelItem{pcd::SelItem::Kind::Datum, plane}), "datum click selects the plane");
  view(w)->setFocus();
  key(view(w), Qt::Key_K);
  check(w.inSketchMode(), "K with a datum plane selected opens a sketch");
  if (w.inSketchMode()) {
    const auto& pl = w.sketchEditor()->sketch().plane;
    check(pl.kind == pc::SketchPlaneKind::OnPlane && QString::fromStdString(pl.planeRef) == plane, "sketch plane OnPlane " + plane);
    key(w.sketchEditor(), Qt::Key_D);
  }
  // no selection, non-empty document: "Sketch düzlemi seç"
  w.selection()->clear();
  view(w)->setFocus();
  key(view(w), Qt::Key_K);
  check(w.planePickActive() && !w.inSketchMode(), "K without a selection: 'Sketch düzlemi seç' state");
  check(msg(w).startsWith("Sketch düzlemi seç"), "status bar explains the plane pick");
  shot(w, "A_planepick.png");
  key(view(w), Qt::Key_Escape);
  check(!w.planePickActive(), "Esc cancels the plane pick");
  key(view(w), Qt::Key_K);
  pickDatum(w, "XZ");
  check(w.inSketchMode() && w.sketchEditor()->sketch().plane.kind == pc::SketchPlaneKind::XZ, "clicking the XZ origin square opens a sketch on XZ");
  if (w.inSketchMode()) key(w.sketchEditor(), Qt::Key_D);
}

void testHistoryUndo(pcd::MainWindow& w) {
  section("35, 36) History tab: rollback + double click; Ctrl+Z / Ctrl+Y");
  w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad");
  pump(400);
  w.structurePanel()->setCurrentPage("history");
  pump(100);
  auto* tree = w.featureTree();
  QTreeWidgetItem* ex = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == "Extrude1") ex = tree->topLevelItem(i);
  check(ex && tree->isVisible(), "History tab shows Extrude1");
  if (!ex) return;
  const QRect r = tree->visualItemRect(ex);
  triggerPopupLater("Buraya kadar geri sar");
  emit tree->customContextMenuRequested(r.center());
  pump(300);
  check(w.rollbackIndex() == w.document().indexOf("Extrude1"), QString("right click > 'Buraya kadar geri sar' (rollback %1)").arg(w.rollbackIndex()));
  w.rollbackPublic(int(w.document().features.size()) - 1);
  pump(200);
  QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, r.center());
  QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, r.center());
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 2000), "double click opens the classic dialog");
  if (w.featureDialog()) { w.featureDialog()->reject(); pump(150); }
  // 36
  auto* redo = w.findChild<QAction*>("actRedo");
  check(redo && redo->shortcuts().contains(QKeySequence("Ctrl+Y")), "Yinele has Ctrl+Y");
  const size_t n0 = w.document().features.size();
  w.deleteFeaturePublic("Sketch4");   // undoable change (a point sketch)
  pump(300);
  check(w.document().features.size() == n0 - 1, "feature deleted");
  view(w)->setFocus();
  key(view(w), Qt::Key_Z, Qt::ControlModifier);
  pump(300);
  check(w.document().features.size() == n0, "Ctrl+Z undoes");
  key(view(w), Qt::Key_Y, Qt::ControlModifier);
  pump(300);
  check(w.document().features.size() == n0 - 1, "Ctrl+Y redoes");
  w.undoStack()->undo();
  pump(200);
}

void testMisc(pcd::MainWindow& w) {
  section("38, 41, 23, Ctrl+H, Ctrl+G, display / measure / mfg tabs");
  view(w)->setFocus();
  key(view(w), Qt::Key_B);
  check(msg(w).contains("yok"), "B (Blend): '" + msg(w) + "'");
  key(view(w), Qt::Key_C, Qt::ControlModifier);
  check(msg(w).contains("yok"), "Ctrl+C: '" + msg(w) + "'");
  // 41: Save as App reachable (File menu, İmalat tab, Ctrl+Shift+A)
  QAction* app = w.findChild<QAction*>("actSaveAsApp");
  check(app && app->text().startsWith("Uygulama Olarak Kaydet") && app->shortcuts().contains(QKeySequence("Ctrl+Shift+A")), "Uygulama Olarak Kaydet (Ctrl+Shift+A)");
  w.ribbon()->setCurrentTab("mfg");
  pump(80);
  check(app && ribbonButton(w, app) != nullptr, "İmalat tab shows 'Uygulama olarak kaydet'");
  QAction* chk = w.findChild<QAction*>("actCheckNow");
  check(chk && ribbonButton(w, chk) && chk->shortcuts().contains(QKeySequence(Qt::Key_F5)), "İmalat: Şimdi Denetle (F5)");
  shot(w, "A_tab_mfg.png");
  // shortcuts work while another tab is shown
  w.selection()->clear();
  view(w)->setFocus();
  key(view(w), Qt::Key_P);
  check(w.toolManager()->activeId() == "pull", "P works while the İmalat tab is shown");
  key(view(w), Qt::Key_S);
  w.ribbon()->setCurrentTab("display");
  pump(80);
  for (const char* n : {"actShowPlanes", "actSketchDims3D", "actShowEdges", "actShowAxes"}) {
    QAction* a = w.findChild<QAction*>(n);
    check(a && ribbonButton(w, a), QString("Görüntü tab: %1").arg(n));
  }
  check(w.findChild<QMenu*>("menuSectionPlane") != nullptr, "menuSectionPlane kept");
  bool inViewMenu = false;
  for (auto* m : w.findChildren<QMenu*>()) if (m->title().contains("Görünüm") && m->actions().contains(w.sectionControls()->toggleAction())) inViewMenu = true;
  check(inViewMenu, "'Görünüm' menu has the Kesit toggle");
  shot(w, "A_tab_display.png");
  w.ribbon()->setCurrentTab("measure");
  pump(80);
  QAction* mass = w.findChild<QAction*>("actMassProperties");
  check(mass && ribbonButton(w, mass), "Ölçüm tab: Kütle özellikleri");
  if (mass) {
    mass->trigger();
    pump(200);
    auto* dlg = w.findChild<QMessageBox*>("massPropertiesDialog");
    check(dlg && dlg->text().contains("Toplam hacim"), "mass properties window");
    if (dlg) dlg->close();
  }
  shot(w, "A_tab_measure.png");
  w.ribbon()->setCurrentTab("design");

  // Ctrl+H / Alt+H / Ctrl+Shift+H
  w.openFile(QString(PC_SAMPLES_DIR) + "/v2_multibody.pcad");
  pump(500);
  const QStringList bodies = w.bodyNames();
  auto* bn = w.structurePanel()->bodiesNode();
  check(bn && bn->childCount() == bodies.size() && bodies.size() >= 2, QString("23) Yapı > Gövdeler lists %1 bodies").arg(bodies.size()));
  if (bodies.size() >= 2) {
    w.selectBody(bodies[0]);
    view(w)->setFocus();
    key(view(w), Qt::Key_H, Qt::ControlModifier);
    check(!w.bodyVisible(bodies[0]) && w.bodyVisible(bodies[1]), "Ctrl+H hides the selected body");
    if (bn) {
      bool unchecked = false;
      for (int i = 0; i < bn->childCount(); ++i) if (bn->child(i)->text(0) == bodies[0]) unchecked = bn->child(i)->checkState(0) == Qt::Unchecked;
      check(unchecked, "Yapı tree check box follows");
    }
    key(view(w), Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    check(w.bodyVisible(bodies[0]) && w.bodyVisible(bodies[1]), "Ctrl+Shift+H shows all");
    w.selectBody(bodies[1]);
    key(view(w), Qt::Key_H, Qt::AltModifier);
    check(!w.bodyVisible(bodies[0]) && w.bodyVisible(bodies[1]), "Alt+H hides the others");
    key(view(w), Qt::Key_H, Qt::ControlModifier | Qt::ShiftModifier);
    // structure check box toggles visibility
    if (bn && bn->childCount()) {
      bn->child(0)->setCheckState(0, Qt::Unchecked);
      pump(80);
      check(!w.bodyVisible(bn->child(0)->text(0)), "unticking a body in the Yapı tree hides it");
      bn->child(0)->setCheckState(0, Qt::Checked);
    }
  }
  shot(w, "A_multibody.png");
  // Ctrl+G outside a tool with nothing selected: new parameter dialog (Groups). (With faces / edges / bodies
  // selected Ctrl+G creates a named selection — gui_harness_named_sel.)
  w.selection()->clear();
  const size_t np = w.document().params.all().size();
  g_onModal = [](QWidget* m) {
    if (m->objectName() != "newParameterDialog") return;
    if (auto* n = m->findChild<QLineEdit*>("npName")) n->setText("ScG");
    if (auto* ex = m->findChild<QCheckBox*>("npExposed")) ex->setChecked(true);
    QTimer::singleShot(50, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  view(w)->setFocus();
  key(view(w), Qt::Key_G, Qt::ControlModifier);
  waitFor([&] { return w.document().params.all().size() > np; }, 3000);
  g_onModal = nullptr;
  const auto* p = w.document().params.find("ScG");
  check(p && p->exposed, "Ctrl+G: 'Grup oluştur' creates parameter ScG (Uygulamada göster)");
  check(w.structurePanel()->currentPageKey() == "groups" && paramRow(w, "ScG") >= 0, "Gruplar tab shows the new parameter");
  w.structurePanel()->setCurrentPage("structure");
}

void testOptionsPanel() {
  section("20 (panel) / 8) Options panel renders ToolOption lists generically");
  pcd::OptionsPanel op;
  op.resize(330, 400);
  QList<pcd::ToolOption> opts;
  pcd::ToolOption merge;
  merge.key = "merge"; merge.label = "Birleştirme"; merge.group = "Birleştirme"; merge.type = pcd::ToolOption::Type::Choice;
  merge.choices = {"auto|Otomatik", "add|Ekle (Add)", "cut|Kes (Cut)", "new|Birleştirme yok (No Merge)", "intersect|Kesişim"};
  merge.value = "auto";
  pcd::ToolOption edge;
  edge.key = "edge"; edge.label = "Kenar"; edge.group = "Kenar"; edge.type = pcd::ToolOption::Type::Choice;
  edge.choices = {"round|Yuvarlat (Round)", "chamfer|Pah (Chamfer)"};
  edge.value = "chamfer";
  pcd::ToolOption both;
  both.key = "bothSides"; both.label = "İki yöne çek (Pull Both Sides)"; both.group = "Yön"; both.type = pcd::ToolOption::Type::Bool; both.value = false;
  pcd::ToolOption grp;
  grp.key = "createGroup"; grp.label = "Grup oluştur (Ctrl+G)"; grp.group = "Ölçü"; grp.type = pcd::ToolOption::Type::Action; grp.icon = "group";
  pcd::ToolOption info;
  info.key = "info"; info.type = pcd::ToolOption::Type::Info; info.value = "Silinecek parçaya tıklayın";
  opts << merge << edge << both << grp << info;
  QStringList changes;
  QObject::connect(&op, &pcd::OptionsPanel::optionChanged, [&](const QString& k, const QVariant& v) { changes << k + "=" + v.toString(); });
  op.showOptions("Çek (Pull)", opts);
  op.show();
  pump(100);
  check(op.title() == "Seçenekler - Çek (Pull)", "title 'Seçenekler - Çek (Pull)'");
  check(op.field("merge") && op.field("merge")->findChild<QComboBox*>(), "5 choices -> combo box");
  auto* ch = op.findChild<QRadioButton*>("opt.edge.chamfer");
  check(ch && ch->isChecked() && op.value("edge").toString() == "chamfer", "edge radio shows 'chamfer'");
  auto* rd = op.findChild<QRadioButton*>("opt.edge.round");
  if (rd) { QTest::mouseClick(rd, Qt::LeftButton, {}, QPoint(8, rd->height() / 2)); pump(50); }
  check(changes.contains("edge=round"), "clicking 'Yuvarlat' reports edge=round (" + changes.join(", ") + ")");
  if (auto* cb = qobject_cast<QCheckBox*>(op.field("bothSides"))) { pch::tickCheckBox(cb, true, "both sides"); pump(30); }
  check(changes.contains("bothSides=true"), "check box reports bothSides=true");
  if (auto* b = qobject_cast<QAbstractButton*>(op.field("createGroup"))) b->click();
  check(changes.contains("createGroup="), "action button reports createGroup");
  // same shape -> values refreshed in place (no widget recreated)
  QWidget* before = op.field("edge");
  opts[1].value = "round";
  op.showOptions("Çek (Pull)", opts);
  check(op.field("edge") == before && op.value("edge").toString() == "round", "same option shape: values refreshed in place");
}

void testStubHostTools() {
  section("ToolApi contract (stub host + ToolManager)");
  pcd::test::StubDesignHost host;
  QObject owner;
  pcd::ToolManager tm(&host, &owner);
  check(tm.action("select") && tm.activeId() == "select", "ToolManager: 'select' action, active select");
  check(tm.editActions().size() == 4 && tm.constraintActions().size() == 11 && tm.dimensionActions().size() == 7, "tool action groups 4 / 11 / 7");
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
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();

  testStubHostTools();
  testOptionsPanel();
  {
    pcd::MainWindow w;
    w.setAutoYes(true);
    w.show();
    w.activateWindow();
    if (!QTest::qWaitForWindowActive(&w, 3000)) note("window not active (shortcuts need the active window)");
    pump(500);
    testLayout(w);
    testModes(w);
    testOrient(w);
    testSelectionAndProperties(w);
    testSketchPlanes(w);
    testHistoryUndo(w);
    testMisc(w);
    if (g_shots) {
      // final showcase shots: 3D with a selection + sketch mode on the bracket
      w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad");
      pump(400);
      pickFace(w, "Extrude1.end");
      if (auto* ot = w.findChild<QTabWidget*>("optionsTabs")) ot->setCurrentIndex(1);
      shot(w, "A_3d.png");
      w.enterSketchEdit("Sketch1");
      pump(300);
      shot(w, "A_sketch_bracket.png");
      w.finishSketchPublic(false);
    }
    w.close();
  }
  std::printf("\n%s: %d failed check(s)\n", g_fail ? "FAILED" : "PASSED", g_fail);
  return g_fail;
}
