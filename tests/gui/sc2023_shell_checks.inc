// SC23 shell / layout checks (WP-S). Included by gui_harness_sc2023.cpp; uses its helpers (check, note, section,
// pump, waitFor, shot) and Sc23Ctx. Covers ANALYSIS §6 T10, T11, T13, T14 and the S-* items of §5.1:
// viewport HUD (S-P0-2), ribbon width management (S-P0-1), Sketch tab (S-P1-1), document tab (S-P1-2), dark status
// bar (S-P1-3), Görünümler (S-P1-4), tool guides (S-P1-5), tooltips / F1 (S-P1-6), context menu (S-P1-7), Selection
// panel (S-P1-8), panel collapse / widths / F6 (S-P1-9).
// --shots: wps_*.png (1240x800 and 1024x768 shell screenshots) next to the P0 shots.
#include "DocumentTabBar.h"
#include "HelpViewer.h"
#include "OptionsPanel.h"
#include "Ribbon.h"
#include "SelectionPanel.h"
#include "Shortcuts.h"
#include "SketchEditor.h"
#include "ViewsPanel.h"
#include <QComboBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolTip>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>

namespace {
using pcd::MainWindow;

// ---------------------------------------------------------------- helpers
QToolButton* visibleRibbonButton(MainWindow& w, const QString& text) {
  for (auto* tb : w.ribbon()->findChildren<QToolButton*>()) {
    QAction* a = tb->defaultAction();
    if (a && tb->isVisible() && (a->text() == text || QString(a->text()).replace('\n', ' ') == text)) return tb;
  }
  return nullptr;
}
// fully inside the window and not clipped by the ribbon page (reachable by the mouse)
bool reachable(MainWindow& w, QWidget* b) {
  if (!b || !b->isVisible() || !b->isEnabled()) return false;
  const QRect r(b->mapTo(&w, QPoint(0, 0)), b->size());
  const QRect rib(w.ribbon()->mapTo(&w, QPoint(0, 0)), w.ribbon()->size());
  return rib.contains(r) && r.right() < w.width() && b->visibleRegion().boundingRect() == b->rect();
}
// no squeezed / elided ribbon label: every visible button got at least its size hint, no toolbar extension shown
QStringList squeezedButtons(MainWindow& w) {
  QStringList bad;
  QWidget* page = w.ribbon()->page(w.ribbon()->currentTabKey());
  if (!page) return bad;
  for (auto* b : page->findChildren<QToolButton*>()) {
    if (!b->isVisible()) continue;
    if (b->objectName() == "qt_toolbar_ext_button") {
      QWidget* g = b->parentWidget();
      while (g && !qobject_cast<pcd::RibbonGroup*>(g)) g = g->parentWidget();
      bad << "toolbar overflow » in " + (g ? static_cast<pcd::RibbonGroup*>(g)->key() : QString("?"));
      continue;
    }
    if (b->width() + 1 < b->sizeHint().width())
      bad << QString("%1 (%2<%3)").arg(b->defaultAction() ? b->defaultAction()->text().replace('\n', ' ') : b->text())
                 .arg(b->width()).arg(b->sizeHint().width());
  }
  return bad;
}
QString groupLevels(MainWindow& w) {
  QStringList l;
  for (pcd::RibbonGroup* g : w.ribbon()->groupsOf(w.ribbon()->currentTabKey())) {
    if (g->isHidden()) continue;
    l << QString("%1=%2").arg(g->key()).arg(g->level() == pcd::RibbonGroup::Level::Expanded ? "büyük"
                                            : g->level() == pcd::RibbonGroup::Level::Compact ? "küçük" : "açılır");
  }
  return l.join(" ");
}
void resizeWindow(MainWindow& w, int W, int H) {
  w.resize(W, H);
  pump(350);
  waitFor([&] { return w.width() == W; }, 1000);
  pump(150);
}
void enterSketch(MainWindow& w) {
  w.newDocument();
  pump(200);
  w.activateWindow();
  w.viewerWidget()->setFocus();
  QTest::keyClick(w.viewerWidget(), Qt::Key_K);
  waitFor([&] { return w.inSketchMode(); }, 2000);
  pump(150);
}
void drawRect(MainWindow& w) {
  auto* ed = w.sketchEditor();
  ed->setTool(pcd::SketchEditor::Tool::Rect);
  const QPoint c(ed->width() / 2, ed->height() / 2);
  QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(-110, -60));
  pump(60);
  QTest::mouseClick(ed, Qt::LeftButton, {}, c + QPoint(110, 60));
  pump(120);
}
bool sameFrame(const double a[12], const double b[12]) {
  for (int i = 0; i < 12; ++i) if (std::abs(a[i] - b[i]) > 1e-9) return false;
  return true;
}
// a model face whose centroid pixel picks that face (visible), with that pixel
bool visibleFace(MainWindow& w, QString* name, QPoint* px) {
  for (const auto& b : w.modelResult().bodies)
    for (const auto& f : b.faces) {
      double c[3], n[3];
      const QString fn = QString::fromStdString(f);
      if (!w.viewer()->faceCentroid(fn, c, n)) continue;
      const QPoint p = w.viewer()->worldToScreen(c).toPoint();
      if (!w.viewer()->rect().adjusted(60, 120, -20, -20).contains(p)) continue;   // away from the HUD corner
      if (w.viewer()->pickAt(p, pc::viewer::ShapeViewer::PickFace) == fn) { *name = fn; *px = p; return true; }
    }
  return false;
}
QMenu* openViewMenu(MainWindow& w, const QPoint& px) {
  emit w.viewer()->customContextMenuRequested(px);
  pump(200);
  for (QWidget* tw : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(tw); m && m->isVisible() && m->objectName() == "viewContextMenu") return m;
  return nullptr;
}
QAction* menuAction(QMenu* m, const QString& objectName) {
  for (QAction* a : m->actions()) if (a->objectName() == objectName) return a;
  return nullptr;
}

// ---------------------------------------------------------------- S-P0-2 HUD
void checkHud(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("S-P0-2 viewport HUD");
  pcd::ViewportHud* hud = ctx.hud();
  check(hud != nullptr, "HUD exists (objectName \"viewportHud\")");
  if (!hud) return;
  check(hud->parentWidget() == w.viewerWidget(), "HUD is a child of the viewer");
  check(w.viewportHud() == hud, "MainWindow::viewportHud() returns it");
  check(hud->testAttribute(Qt::WA_TransparentForMouseEvents), "HUD body is transparent for mouse events");
  auto* msg = w.viewerWidget()->findChild<QLabel*>("viewportHud.message");
  check(msg && msg == hud->messageLabel(), "message strip \"viewportHud.message\" found under the viewer");
  check(msg && !msg->testAttribute(Qt::WA_TransparentForMouseEvents), "message strip takes the mouse");
  check(w.findChild<QLabel*>("viewportHud.tool") != nullptr, "tool line \"viewportHud.tool\"");

  check(ctx.openSample("bracket.pcad"), "open samples/bracket.pcad");
  ctx.reset();
  check(hud->toolText() == "Seç", QString("tool text after Select: '%1'").arg(hud->toolText()));
  check(hud->isVisible() && hud->geometry().top() < 40 && hud->geometry().left() < 40,
        QString("HUD visible top-left (%1,%2)").arg(hud->geometry().left()).arg(hud->geometry().top()));
  ctx.tools()->activate("pull");
  check(waitFor([&] { return hud->toolText() == "Çek"; }, 1500), QString("tool text after Pull: '%1'").arg(hud->toolText()));
  QString face;
  if (!w.modelResult().bodies.empty() && !w.modelResult().bodies.front().faces.empty())
    face = QString::fromStdString(w.modelResult().bodies.front().faces.front());
  check(!face.isEmpty(), "bracket has a face to select");
  if (!face.isEmpty()) {
    w.selection()->set({pcd::SelItem{pcd::SelItem::Kind::Face, face}});
    const QString want = "1 " + pcd::selKindText(pcd::SelItem::Kind::Face, 1);
    check(waitFor([&] { return hud->selectionText() == want; }, 1500),
          QString("selection text: '%1' (want '%2')").arg(hud->selectionText(), want));
    auto* tl = w.findChild<QLabel*>("viewportHud.tool");
    check(tl && tl->text().contains("Çek") && tl->text().contains(want), "tool line reads 'Çek · " + want + "'");
  }
  shot(w, "p0_hud_pull.png");

  // message strip: severity icon + text, guide slot, dismiss by click, timeout, word wrap
  hud->showMessage("deneme", pcd::ViewportHud::Severity::Warning);
  check(hud->messageText() == "deneme" && hud->messageSeverity() == pcd::ViewportHud::Severity::Warning && msg && msg->isVisible(),
        "showMessage(Warning) shows the strip");
  auto* icon = msg ? msg->findChild<QLabel*>("viewportHud.messageIcon") : nullptr;
  check(icon && icon->isVisible() && icon->pixmap().width() > 0, "message strip has a severity icon (not colour only)");
  const QRect g = hud->guideArea();
  check(g.isValid() && g.top() > hud->geometry().bottom() && (!msg || g.top() > msg->geometry().bottom()),
        QString("guideArea below the HUD text: (%1,%2 %3x%4)").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
  hud->clearMessage();
  check(hud->messageText().isEmpty() && msg && !msg->isVisible(), "clearMessage hides the strip");
  check(hud->guideArea() == g, "guideArea is a fixed slot (same with and without a message)");
  hud->showMessage("kısa", pcd::ViewportHud::Severity::Info, 150);
  check(waitFor([&] { return hud->messageText().isEmpty(); }, 1500), "timed message clears itself");
  hud->showMessage("Extrude2 oluşturulamadı: geometri hatası", pcd::ViewportHud::Severity::Error);
  auto* sev = w.statusBar()->findChild<QLabel*>("statusSeverity");
  check(sev && sev->isVisible(), "HUD error -> error icon in front of the status-bar prompt");
  if (msg) QTest::mouseClick(msg, Qt::LeftButton);
  pump(50);
  check(hud->messageText().isEmpty() && sev && !sev->isVisible(), "a click on the strip dismisses the message (and the status icon)");
  hud->showMessage(QString("uzun ileti ").repeated(30), pcd::ViewportHud::Severity::Info);
  check(msg && msg->width() <= 520 && msg->height() > 30, QString("long message wraps (%1x%2)").arg(msg ? msg->width() : 0).arg(msg ? msg->height() : 0));
  hud->clearMessage();

  // the HUD follows the viewer size
  const QSize before = w.viewerWidget()->size();
  resizeWindow(w, 1100, 760);
  const QRect g2 = hud->guideArea();
  check(g2.height() != g.height() || w.viewerWidget()->size() == before, "guideArea follows the viewer resize");
  resizeWindow(w, 1240, 800);
  ctx.reset();
}

// ---------------------------------------------------------------- S-P1-5 tool guides
void checkGuides(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("S-P1-5 tool guides top-left below the HUD, two columns");
  ctx.tools()->activate("pull");
  pump(150);
  auto* bar = w.viewerWidget()->findChild<QWidget*>("toolGuideBar");
  pcd::ViewportHud* hud = ctx.hud();
  check(bar && bar->isVisible(), "Pull: tool guide strip visible");
  if (!bar || !hud) return;
  const QRect ga = hud->guideArea();
  check(bar->x() < 40 && bar->y() >= ga.top() - 1 && bar->y() <= ga.top() + 4,
        QString("guides at the viewer's top-left in the HUD guide slot (%1,%2; slot top %3)").arg(bar->x()).arg(bar->y()).arg(ga.top()));
  QList<QToolButton*> bs;
  for (auto* b : bar->findChildren<QToolButton*>()) if (b->isVisible()) bs << b;
  auto* sel = bar->findChild<QToolButton*>("guide.select");
  int minY = 1 << 20;
  QSet<int> cols;
  for (auto* b : bs) {
    minY = std::min(minY, b->y());
    if (b != sel) cols.insert(b->x());
  }
  check(sel && sel->isVisible() && sel->y() == minY, "'Seç' guide alone on the top row");
  bool selAlone = true;
  for (auto* b : bs) if (b != sel && sel && b->y() == sel->y()) selAlone = false;
  check(selAlone && cols.size() == 2, QString("other guides in two columns (%1 buttons, %2 columns)").arg(bs.size()).arg(cols.size()));
  ctx.reset();
}

// ---------------------------------------------------------------- S-P0-1 / T13 ribbon + layout at two sizes
void checkWidths(Sc23Ctx& ctx, int W, int H) {
  MainWindow& w = ctx.w;
  section(QString("T13 / S-P0-1 tool access at %1x%2").arg(W).arg(H));
  resizeWindow(w, W, H);
  ctx.openSample("bracket.pcad");
  ctx.reset();
  w.ribbon()->setCurrentTab("design");
  pump(150);
  note("Tasarım groups: " + groupLevels(w));
  for (const char* t : {"Seç", "Çek", "Taşı", "Doldur"}) {
    QToolButton* b = visibleRibbonButton(w, QString::fromUtf8(t));
    check(reachable(w, b), QString("%1x%2 Tasarım: '%3' is a visible, unclipped, enabled button").arg(W).arg(H).arg(QString::fromUtf8(t)));
  }
  const QStringList sq = squeezedButtons(w);
  check(sq.isEmpty(), QString("%1x%2 Tasarım: no squeezed / elided label %3").arg(W).arg(H).arg(sq.join(", ")));
  if (QToolButton* pull = visibleRibbonButton(w, "Çek")) {
    QTest::mouseClick(pull, Qt::LeftButton);
    check(waitFor([&] { return ctx.tools()->activeId() == "pull"; }, 1000), QString("%1x%2: mouse click on 'Çek' activates Pull").arg(W).arg(H));
  }
  if (QToolButton* fill = visibleRibbonButton(w, "Doldur")) {
    QTest::mouseClick(fill, Qt::LeftButton);
    check(waitFor([&] { return ctx.tools()->activeId() == "fill"; }, 1000), QString("%1x%2: mouse click on 'Doldur' activates Fill").arg(W).arg(H));
  }
  // groups that do not fit stay reachable: collapsed group button or the overflow "»"
  bool allReachable = true;
  for (pcd::RibbonGroup* g : w.ribbon()->groupsOf("design")) {
    if (g->isHidden()) continue;
    QWidget* probe = g->isCollapsed() ? static_cast<QWidget*>(g->collapseButton()) : g;
    const bool inside = probe && QRect(probe->mapTo(&w, QPoint(0, 0)), probe->size()).right() < w.width();
    const bool viaOverflow = w.ribbon()->overflowButton()->isVisible();
    if (!inside && !viaOverflow) allReachable = false;
  }
  check(allReachable, QString("%1x%2: every Tasarım group is on screen, collapsed into a drop-down or under »").arg(W).arg(H));
  // left column ≈ 20 %, graphics area keeps the rest
  auto* ds = w.findChild<QDockWidget*>("dockStructure");
  check(ds && ds->width() >= 240 && ds->width() <= std::max(240, int(W * 0.27)),
        QString("%1x%2: left panel column %3 px (240 .. %4)").arg(W).arg(H).arg(ds ? ds->width() : 0).arg(std::max(240, int(W * 0.27))));
  check(w.viewerWidget()->width() >= W - 340, QString("%1x%2: graphics area %3 px wide").arg(W).arg(H).arg(w.viewerWidget()->width()));
  // Options - Pull without a scroll bar (S-P1-9)
  ctx.reset();
  if (!w.modelResult().bodies.empty() && !w.modelResult().bodies.front().faces.empty())
    w.selection()->set({pcd::SelItem{pcd::SelItem::Kind::Face, QString::fromStdString(w.modelResult().bodies.front().faces.front())}});
  ctx.tools()->activate("pull");
  pump(300);
  auto* sa = w.optionsPanel() ? w.optionsPanel()->findChild<QScrollArea*>() : nullptr;
  check(sa && !sa->verticalScrollBar()->isVisible(),
        QString("%1x%2: 'Seçenekler - Çek' without a scroll bar (content %3 px, viewport %4 px)").arg(W).arg(H)
            .arg(sa && sa->widget() ? sa->widget()->sizeHint().height() : -1).arg(sa ? sa->viewport()->height() : -1));
  shot(w, QString("wps_%1_design_pull.png").arg(W));
  ctx.reset();

  // Sketch tab: auto-selected, Dikdörtgen / Sketch'i Bitir reachable, nothing squeezed
  enterSketch(w);
  check(w.inSketchMode() && w.ribbon()->currentTabKey() == "sketch", QString("%1x%2: K -> Sketch tab selected").arg(W).arg(H));
  pump(150);
  note("Sketch groups: " + groupLevels(w));
  for (const char* t : {"Dikdörtgen", "Sketch'i Bitir", "Seç"}) {
    QToolButton* b = visibleRibbonButton(w, QString::fromUtf8(t));
    check(reachable(w, b), QString("%1x%2 Sketch: '%3' is a visible, unclipped, enabled button").arg(W).arg(H).arg(QString::fromUtf8(t)));
  }
  const QStringList sq2 = squeezedButtons(w);
  check(sq2.isEmpty(), QString("%1x%2 Sketch: no squeezed / elided label %3").arg(W).arg(H).arg(sq2.join(", ")));
  if (QToolButton* r = visibleRibbonButton(w, "Dikdörtgen")) {
    QTest::mouseClick(r, Qt::LeftButton);
    check(waitFor([&] { return ctx.tools()->activeId() == "sketch.rect"; }, 1000), QString("%1x%2: click on 'Dikdörtgen' -> rectangle tool").arg(W).arg(H));
  }
  drawRect(w);
  shot(w, QString("wps_%1_sketch.png").arg(W));
  if (QToolButton* fin = visibleRibbonButton(w, "Sketch'i Bitir")) {
    QTest::mouseClick(fin, Qt::LeftButton);
    check(waitFor([&] { return !w.inSketchMode(); }, 2000), QString("%1x%2: click on 'Sketch'i Bitir' leaves the sketch").arg(W).arg(H));
    check(w.ribbon()->currentTabKey() == "design", QString("%1x%2: back in 3D -> Tasarım tab").arg(W).arg(H));
  }
  shot(w, QString("wps_%1_after_sketch.png").arg(W));
  ctx.reset();
}

// ---------------------------------------------------------------- S-P1-1 Sketch tab
void checkSketchTab(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("S-P1-1 Sketch ribbon tab");
  check(w.ribbon()->tabTexts() == QStringList({"Sketch", "Tasarım", "Görüntü", "Ölçüm", "İmalat"}),
        "ribbon tabs: " + w.ribbon()->tabTexts().join(" · "));
  w.newDocument();
  pump(150);
  check(w.ribbon()->currentTabKey() == "design", "3D: Tasarım tab is current");
  QStringList sk;
  for (pcd::RibbonGroup* g : w.ribbon()->groupsOf("sketch")) sk << g->title();
  check(sk == QStringList({"Yönlendir", "Oluştur", "Değiştir", "Kısıtlar", "Ölçüler", "Düzenle", "Sketch'i Bitir"}),
        "Sketch groups: " + sk.join(" | "));
  bool movedOut = true;
  for (pcd::RibbonGroup* g : w.ribbon()->groupsOf("design"))
    if (g->key() == "constraints" || g->key() == "dimensions" || g->key() == "sketchSession" || g->isContextual()) movedOut = false;
  check(movedOut, "no contextual sketch group left on Tasarım");
  w.ribbon()->setCurrentTab("sketch");
  pump(100);
  bool disabledNotHidden = true;
  for (const char* k : {"sketchCreate", "constraints", "dimensions", "sketchSession"}) {
    pcd::RibbonGroup* g = w.ribbon()->group(k);
    if (!g || !g->isVisible() || g->isEnabled()) disabledNotHidden = false;
  }
  check(disabledNotHidden, "3D: Sketch-tab drawing groups visible but disabled");
  // S-P1-6: the rich tooltip of a button disabled through its group says why
  if (pcd::RibbonGroup* g = w.ribbon()->group("sketchCreate")) {
    QToolButton* rect = g->buttonFor(ctx.tools()->action("sketch.rect"));
    if (rect) {
      const QPoint c = rect->rect().center();
      QHelpEvent he(QEvent::ToolTip, c, rect->mapToGlobal(c));
      QApplication::sendEvent(rect, &he);
      pump(100);
      const QString tip = QToolTip::text();
      check(tip.contains("Dikdörtgen") && tip.contains("(R)") && tip.contains("F1: yardım") && tip.contains("Sketch modunda etkin"),
            "disabled Dikdörtgen tooltip: name, key, F1 line and the reason");
      QToolTip::hideText();
    }
  }
  check(w.ribbon()->group("sketchOrient") && w.ribbon()->group("sketchOrient")->isEnabled(), "3D: Sketch > Yönlendir stays enabled (camera)");
  w.ribbon()->setCurrentTab("design");
  enterSketch(w);
  check(w.ribbon()->currentTabKey() == "sketch", "K: Sketch tab selected automatically");
  bool enabled = true;
  for (const char* k : {"sketchCreate", "constraints", "dimensions", "sketchSession"})
    enabled = enabled && w.ribbon()->group(k) && w.ribbon()->group(k)->isEnabled() && w.ribbon()->group(k)->isVisible();
  check(enabled, "sketch mode: drawing groups enabled");
  // tab change inside the sketch does not touch the tool (T11, sketch side)
  QTest::keyClick(w.sketchEditor(), Qt::Key_R);
  pump(80);
  w.ribbon()->setCurrentTab("display");
  pump(80);
  check(w.inSketchMode() && ctx.tools()->activeId() == "sketch.rect", "switching to Görüntü while sketching keeps Dikdörtgen");
  w.ribbon()->setCurrentTab("sketch");
  w.finishSketchPublic(false);
  pump(200);
  check(!w.inSketchMode() && w.ribbon()->currentTabKey() == "design", "leaving the sketch: Tasarım tab");
  ctx.reset();
}

// ---------------------------------------------------------------- T10 Plan view / camera vs sketch plane
void checkT10(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("T10 camera keys while sketching never change the sketch plane");
  enterSketch(w);
  drawRect(w);
  double f0[12], f1[12];
  check(w.openSketchFrame(f0, f0 + 3, f0 + 6, f0 + 9), "open sketch frame");
  const size_t ents = w.sketchEditor()->sketch().entities.size();
  const std::string sid = w.openSketchId();
  auto camera = [&] { return QString("%1/%2").arg(w.viewer()->cameraAz(), 0, 'f', 2).arg(w.viewer()->cameraEl(), 0, 'f', 2); };
  QStringList cams;
  cams << camera();
  QWidget* kt = w.sketchEditor();
  QTest::keyClick(kt, Qt::Key_H);
  pump(150);
  cams << camera();
  QTest::keyClick(kt, Qt::Key_V);
  pump(150);
  cams << camera();
  QTest::keyClick(kt, Qt::Key_1, Qt::ControlModifier);   // Trimetrik
  pump(150);
  cams << camera();
  // middle drag = spin (the overlay hands the middle button to the viewer)
  QWidget* v = w.viewerWidget();
  const QPoint p0(v->width() / 2, v->height() / 2);
  QTest::mousePress(v, Qt::MiddleButton, {}, p0);
  for (int i = 1; i <= 6; ++i) {
    const QPoint p = p0 + QPoint(20 * i, 8 * i);
    QMouseEvent mv(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QCoreApplication::sendEvent(v, &mv);
    pump(10);
  }
  QTest::mouseRelease(v, Qt::MiddleButton, {}, p0 + QPoint(120, 48));
  pump(150);
  cams << camera();
  note("camera az/el: " + cams.join(" → "));
  check(QSet<QString>(cams.begin(), cams.end()).size() >= 3, "H / V / Ctrl+1 / middle drag change the camera");
  check(w.openSketchFrame(f1, f1 + 3, f1 + 6, f1 + 9) && sameFrame(f0, f1), "sketch plane frame unchanged (origin, u, v, normal)");
  check(w.inSketchMode() && w.openSketchId() == sid && w.sketchEditor()->sketch().entities.size() == ents,
        "still the same sketch, entities unchanged");
  w.finishSketchPublic(false);
  pump(200);
  ctx.reset();
}

// ---------------------------------------------------------------- T11 ribbon tab != tool
void checkT11(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("T11 ribbon tab change keeps tool, selection, Options and HUD");
  ctx.openSample("bracket.pcad");
  ctx.reset();
  const QString face = QString::fromStdString(w.modelResult().bodies.front().faces.front());
  w.selection()->set({pcd::SelItem{pcd::SelItem::Kind::Face, face}});
  ctx.tools()->activate("pull");
  pump(200);
  const QString title = w.optionsPanel()->title();
  pcd::ViewportHud* hud = ctx.hud();
  const QString hudLine = hud ? hud->toolText() + "|" + hud->selectionText() : QString();
  const int undo0 = w.undoStack()->count();
  const QList<pcd::SelItem> sel0 = w.selection()->items();
  bool same = true;
  QStringList trail;
  for (const char* t : {"display", "measure", "mfg", "sketch", "design"}) {
    w.ribbon()->setCurrentTab(t);
    pump(80);
    const bool ok = ctx.tools()->activeId() == "pull" && w.selection()->items() == sel0 && w.optionsPanel()->title() == title &&
                    (!hud || hud->toolText() + "|" + hud->selectionText() == hudLine) && w.undoStack()->count() == undo0;
    trail << QString("%1:%2").arg(t, ok ? "ok" : "CHANGED");
    same = same && ok;
  }
  check(same, "tool / selection / Options title / HUD / undo unchanged across tabs: " + trail.join(" "));
  check(!w.isWindowModified(), "document not modified by tab changes");
  ctx.reset();
}

// ---------------------------------------------------------------- T14 hover vs selection, context menu
void checkT14(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("T14 hover != selection; right-click context; S-P1-7 menu items");
  ctx.openSample("bracket.pcad");
  ctx.reset();
  w.viewer()->setView(pc::viewer::ShapeViewer::View::Iso);
  pump(250);
  QString face;
  QPoint px;
  check(visibleFace(w, &face, &px), "found a visible face: " + face);
  if (face.isEmpty()) return;
  // On Windows QTest::mouseMove moves the real cursor; if the harness window is not in front (e.g. launched from
  // Explorer) the first move can be lost. Bring the window forward and retry a few times (still a real hover).
  w.raise();
  w.activateWindow();
  pump(100);
  for (int attempt = 0; attempt < 4; ++attempt) {
    QTest::mouseMove(w.viewerWidget(), px + QPoint(attempt % 2, 0));
    pump(250);
    if (w.hoverInfoText().contains(face) || w.viewer()->preselection() == face) break;
  }
  check(w.selection()->isEmpty(), "hover over a face does not select it");
  check(w.hoverInfoText().contains(face) || w.viewer()->preselection() == face,
        "hover pre-highlights / reports the face: " + w.hoverInfoText());
  // right click on an unselected face: the face becomes the selection
  QMenu* m = openViewMenu(w, px);
  check(m != nullptr, "right click opens the graphics context menu");
  if (m) {
    check(w.selection()->count() == 1 && w.selection()->items().first().name == face, "right click on an unselected face selects it");
    for (const char* n : {"ctxLastSketchPlane", "ctxInverseVisibility", "ctxLocateInTree", "ctxCut", "ctxCopy", "ctxPaste", "ctxProperties"})
      check(menuAction(m, n) != nullptr, QString("menu item %1").arg(n));
    bool viewSub = false;
    for (QAction* a : m->actions()) if (a->menu() && a->menu()->objectName() == "ctxViewMenu") viewSub = a->menu()->actions().size() >= 8;
    check(viewSub, "Görünüm (View) ▸ submenu with the standard views");
    QAction* cut = menuAction(m, "ctxCut");
    check(cut && !cut->isEnabled() && !cut->toolTip().isEmpty(), "clipboard items are disabled with a reason: " + (cut ? cut->toolTip() : QString()));
    QAction* del = nullptr;
    for (QAction* a : m->actions()) if (a->objectName() == "actDelete") del = a;
    check(del != nullptr, "Sil (Delete) in the menu");
    QAction* lsp = menuAction(m, "ctxLastSketchPlane");
    check(lsp && lsp->isEnabled(), "Son sketch düzlemini kullan enabled (bracket has sketches)");
    auto* mini = w.viewerWidget()->findChild<QWidget*>("miniToolbar");
    note(QString("mini-toolbar with the menu: %1").arg(mini && mini->isVisible() ? "visible" : "hidden (no options for this tool)"));
    shot(*m, "wps_context_menu.png");
    if (QAction* pr = menuAction(m, "ctxProperties")) { m->close(); pr->trigger(); }
    else m->close();
    pump(150);
    auto* ot = w.findChild<QTabWidget*>("optionsTabs");
    check(ot && ot->currentIndex() == 1, "Özellikler (Properties) shows the Properties tab");
    if (ot) ot->setCurrentIndex(0);
  }
  // multi-selection kept when the right-clicked face is part of it
  QString other;
  for (const auto& f : w.modelResult().bodies.front().faces) if (QString::fromStdString(f) != face) { other = QString::fromStdString(f); break; }
  w.selection()->set({pcd::SelItem{pcd::SelItem::Kind::Face, face}, pcd::SelItem{pcd::SelItem::Kind::Face, other}});
  pump(80);
  QTest::mouseMove(w.viewerWidget(), px + QPoint(1, 0));
  pump(150);
  m = openViewMenu(w, px);
  check(m && w.selection()->count() == 2, "right click on a face inside a 2-face selection keeps both");
  if (m) {
    QAction* loc = menuAction(m, "ctxLocateInTree");
    m->close();
    if (loc) loc->trigger();
    pump(150);
    check(w.structurePanel()->currentPageKey() == "structure", "Yapı ağacında bul -> Yapı page");
  }
  // inverse visibility: hide one body, invert -> it is the only one shown
  const QStringList bodies = w.bodyNames();
  if (bodies.size() >= 1) {
    w.hideItems({pcd::SelItem{pcd::SelItem::Kind::Body, bodies.first()}});
    pump(150);
    w.selection()->clear();
    QTest::mouseMove(w.viewerWidget(), QPoint(10, w.viewerWidget()->height() - 10));
    pump(100);
    m = openViewMenu(w, QPoint(w.viewerWidget()->width() - 30, w.viewerWidget()->height() - 30));
    QAction* inv = m ? menuAction(m, "ctxInverseVisibility") : nullptr;
    check(inv && inv->isEnabled(), "Görünürlüğü tersine çevir enabled while a body is hidden");
    if (m) m->close();
    const int u0 = w.undoStack()->count();
    if (inv) inv->trigger();
    pump(200);
    bool ok = w.bodyVisible(bodies.first());
    for (int i = 1; i < bodies.size(); ++i) ok = ok && !w.bodyVisible(bodies[i]);
    check(ok && w.undoStack()->count() == u0 + 1, "inverse visibility swaps shown / hidden bodies in one undo step");
    w.undoStack()->undo();
    w.undoStack()->undo();
    pump(150);
  }
  ctx.reset();
}

// ---------------------------------------------------------------- S-P1-2 / S-P1-3 / S-P1-4 / S-P1-6 / S-P1-8 / S-P1-9
void checkPanels(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  section("S-P1-2 document tab, S-P1-3 status bar");
  ctx.openSample("bracket.pcad");
  ctx.reset();
  auto* dt = w.documentTabs();
  check(dt && dt->isVisible() && dt->count() == 1, "document tab strip below the viewport");
  if (dt) {
    const int vb = w.viewerWidget()->mapTo(&w, QPoint(0, w.viewerWidget()->height())).y();
    check(dt->mapTo(&w, QPoint(0, 0)).y() >= vb - 1 && dt->mapTo(&w, QPoint(0, 0)).y() < w.statusBar()->mapTo(&w, QPoint(0, 0)).y(),
          "tab between the graphics area and the status bar");
    check(!dt->documentText().endsWith("*"), "clean document: '" + dt->documentText() + "'");
    const QStringList bodies = w.bodyNames();
    if (!bodies.isEmpty()) {
      w.hideItems({pcd::SelItem{pcd::SelItem::Kind::Body, bodies.first()}});
      pump(100);
      check(dt->documentText().endsWith("*"), "after a change: '" + dt->documentText() + "'");
      w.undoStack()->undo();
      pump(100);
      check(!dt->documentText().endsWith("*"), "after undo: '" + dt->documentText() + "'");
    }
    w.newDocument();
    pump(100);
    check(dt->documentText() == QString::fromStdString(w.document().name), "new document: '" + dt->documentText() + "'");
  }
  QStatusBar* sb = w.statusBar();
  const QColor bg = sb->grab().toImage().pixelColor(sb->width() / 2, sb->height() - 3);
  check(bg.lightness() < 110, QString("status bar is dark (%1)").arg(bg.name()));
  check(sb->findChild<QComboBox*>("selectionFilter") && sb->findChild<QLabel*>("selectionList") && sb->findChild<QWidget*>("statusPrevView"),
        "status bar: selection info, filter, view buttons kept");

  section("S-P1-4 Görünümler panel, Ctrl+0..6, Trimetrik");
  ctx.openSample("bracket.pcad");
  auto* st = w.findChild<QTabWidget*>("structureTabs");
  QStringList tabs;
  if (st) for (int i = 0; i < st->count(); ++i) tabs << st->tabText(i);
  check(!tabs.isEmpty() && tabs.last() == "Görünümler", "Structure tabs end with Görünümler: " + tabs.join(" · "));
  pcd::ViewsPanel* vp = w.viewsPanel();
  check(vp != nullptr, "MainWindow::viewsPanel()");
  if (vp) {
    QStringList rows;
    for (int i = 0; i < vp->list()->topLevelItemCount(); ++i)
      rows << vp->list()->topLevelItem(i)->text(0) + (vp->list()->topLevelItem(i)->text(1).isEmpty() ? "" : " " + vp->list()->topLevelItem(i)->text(1));
    check(rows.mid(0, 7) == QStringList({"İzometrik Ctrl+0", "Trimetrik Ctrl+1", "Üst Ctrl+2", "Alt Ctrl+3", "Sol Ctrl+4", "Sağ Ctrl+5", "Ön Ctrl+6"}),
          "rows: " + rows.join(", "));
    w.structurePanel()->setCurrentPage("views");
    pump(100);
    w.viewerWidget()->setFocus();
    QTest::keyClick(w.viewerWidget(), Qt::Key_1, Qt::ControlModifier);
    pump(150);
    const double azT = w.viewer()->cameraAz(), elT = w.viewer()->cameraEl();
    check(std::abs(azT - -62) < 1 && std::abs(elT - 22) < 1, QString("Ctrl+1 = Trimetrik (az %1, el %2)").arg(azT).arg(elT));
    QTest::keyClick(w.viewerWidget(), Qt::Key_0, Qt::ControlModifier);
    pump(150);
    check(std::abs(w.viewer()->cameraAz() - -45) < 1, "Ctrl+0 = İzometrik");
    QTest::keyClick(w.viewerWidget(), Qt::Key_2, Qt::ControlModifier);
    pump(150);
    check(std::abs(w.viewer()->cameraEl() - 90) < 1, "Ctrl+2 = Üst");
    QTest::keyClick(w.viewerWidget(), Qt::Key_1, Qt::ControlModifier);
    pump(100);
    check(!vp->deleteButton()->isEnabled(), "Sil disabled on standard views");
    QTest::mouseClick(vp->createButton(), Qt::LeftButton);
    pump(100);
    check(vp->savedViewCount() == 1 && vp->deleteButton()->isEnabled(), "Oluştur saves the current view (session only)");
    QTest::keyClick(w.viewerWidget(), Qt::Key_6, Qt::ControlModifier);   // Ön
    pump(150);
    vp->applyRow(vp->list()->topLevelItemCount() - 1);
    pump(150);
    check(std::abs(w.viewer()->cameraAz() - azT) < 1.5 && std::abs(w.viewer()->cameraEl() - elT) < 1.5,
          QString("saved view restores the direction (az %1, el %2)").arg(w.viewer()->cameraAz()).arg(w.viewer()->cameraEl()));
    shot(w, "wps_views_panel.png");
    vp->deleteCurrent();
    check(vp->savedViewCount() == 0, "Sil removes the saved view");
    w.structurePanel()->setCurrentPage("structure");   // leave the panel as found (later checks focus the tree)
    pump(50);
  }

  section("S-P1-6 tooltips and F1");
  QAction* pull = ctx.tools()->action("pull");
  const QString tt = pcd::richToolTip(pull);
  check(tt.contains("Çek (Pull)") && tt.contains("(P)") && tt.contains("bölge") && tt.contains("F1: yardım"),
        "Çek tooltip: name, key, description, F1 line: " + tt);
  const QString ts = pcd::richToolTip(ctx.tools()->action("select"));
  check(ts.contains("Seç (Select)") && ts.contains("(S)") && ts.contains("Ctrl"), "Seç tooltip has a description from the table");
  check(!pcd::richToolTip(w.findChild<QAction*>("actViewTrimetric")).isEmpty() &&
            pcd::richToolTip(w.findChild<QAction*>("actViewTrimetric")).contains("Ctrl+1"),
        "Trimetrik tooltip shows Ctrl+1");
  check(!pcd::HelpViewer::guidePath().isEmpty(), "docs/KULLANIM_KILAVUZU.md found: " + pcd::HelpViewer::guidePath());
  w.viewerWidget()->setFocus();
  QTest::keyClick(w.viewerWidget(), Qt::Key_F1);
  pump(200);
  auto* hv = w.findChild<pcd::HelpViewer*>("helpViewer");
  check(hv && hv->isVisible() && hv->loaded() && hv->browser()->toPlainText().contains("PersistCAD"), "F1 opens the user guide in the help viewer");
  check(hv && hv->findText("Görünümler"), "help viewer finds 'Görünümler'");
  if (hv) hv->close();

  section("S-P1-8 Selection panel header and type filter");
  ctx.reset();
  auto* sp = w.findChild<pcd::SelectionPanel*>("selectionPanel");
  check(sp && sp->header()->text() == "Seçili: yok", "empty selection: '" + (sp ? sp->header()->text() : QString()) + "'");
  const QString face = QString::fromStdString(w.modelResult().bodies.front().faces.front());
  w.selection()->set({pcd::SelItem{pcd::SelItem::Kind::Face, face}});
  pump(80);
  check(sp && sp->header()->text() == "Seçili: Yüz (1)", "one face: '" + (sp ? sp->header()->text() : QString()) + "'");
  if (sp) {
    QComboBox* tf = sp->typeFilter();
    tf->setCurrentIndex(tf->findData(int(pcd::SelFilter::Edge)));
    emit tf->activated(tf->currentIndex());
    pump(50);
    check(w.selection()->filter() == pcd::SelFilter::Edge, "type filter 'Kenar' -> SelectionModel filter");
    auto* sbf = w.statusBar()->findChild<QComboBox*>("selectionFilter");
    check(sbf && sbf->currentData().toInt() == int(pcd::SelFilter::Edge), "status bar filter follows");
    tf->setCurrentIndex(0);
    emit tf->activated(0);
    pump(50);
    check(w.selection()->filter() == pcd::SelFilter::Smart && tf->itemText(0).contains("All Types"), "'Tüm türler (All Types)' = Akıllı");
    auto* search = sp->findChild<QLineEdit*>("selectionSearch");
    auto* all = sp->findChild<QWidget*>("selectionSearchAllBodies");
    check(search && !search->isEnabled() && !search->toolTip().isEmpty() && all && !all->isEnabled(), "search / 'Tüm gövdelerde ara' disabled with a reason");
  }

  section("S-P1-9 panel collapse, F6 focus cycle");
  ctx.reset();
  auto* ds = w.findChild<QDockWidget*>("dockStructure");
  auto* dopt = w.findChild<QDockWidget*>("dockOptions");
  auto* col = ds ? ds->findChild<QToolButton*>("dockCollapse") : nullptr;
  check(col != nullptr, "Yapı panel header has a collapse button");
  if (col && dopt) {
    const int h0 = ds->height(), o0 = dopt->height();
    QTest::mouseClick(col, Qt::LeftButton);
    pump(250);
    check(ds->height() < 40 && dopt->height() > o0 + 100, QString("collapsed: Yapı %1 px, Seçenekler %2 -> %3 px").arg(ds->height()).arg(o0).arg(dopt->height()));
    shot(w, "wps_panel_collapsed.png");
    QTest::mouseClick(col, Qt::LeftButton);
    pump(250);
    check(ds->height() >= 100 && std::abs(ds->height() - h0) < 80, QString("expanded again: %1 px (was %2)").arg(ds->height()).arg(h0));
  }
  w.viewerWidget()->setFocus();
  pump(50);
  QStringList zones;
  auto zoneOf = [&]() -> QString {
    QWidget* f = QApplication::focusWidget();
    if (!f) return "-";
    if (f == w.viewerWidget() || w.viewerWidget()->isAncestorOf(f)) return "view";
    if (ds && ds->isAncestorOf(f)) return "structure";
    if (dopt && dopt->isAncestorOf(f)) return "options";
    if (w.ribbon()->isAncestorOf(f)) return "ribbon";
    return f->objectName();
  };
  zones << zoneOf();
  for (int i = 0; i < 4; ++i) {
    QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &w, Qt::Key_F6);
    pump(60);
    zones << zoneOf();
  }
  check(zones == QStringList({"view", "structure", "options", "ribbon", "view"}), "F6 cycles: " + zones.join(" → "));
  QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &w, Qt::Key_F6, Qt::ShiftModifier);
  pump(60);
  check(zoneOf() == "ribbon", "Shift+F6 goes back: " + zoneOf());
  w.viewerWidget()->setFocus();
  ctx.reset();
}

void runShellChecks(Sc23Ctx& ctx) {
  MainWindow& w = ctx.w;
  checkHud(ctx);
  checkGuides(ctx);
  checkSketchTab(ctx);
  checkT10(ctx);
  checkT11(ctx);
  checkT14(ctx);
  checkPanels(ctx);
  checkWidths(ctx, 1240, 800);
  checkWidths(ctx, 1024, 768);
  resizeWindow(w, 1240, 800);
  ctx.openSample("bracket.pcad");
  ctx.reset();
  shot(w, "wps_1240_overview.png");
}
} // namespace
