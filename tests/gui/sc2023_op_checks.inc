// SC23 operation / state checks (WP-O). Included by gui_harness_sc2023.cpp; uses its helpers (check, note, section,
// pump, waitFor, shot) and Sc23Ctx. Acceptance tests T01–T09 and T12 of docs/sc2023/ANALYSIS.md §6 (+ the P0 smoke
// checks of the ToolApi 1.1 operation hooks). Every helper here is prefixed "op" (the shell checks share the
// anonymous namespace).
#include "DimensionBox.h"
#include "PropertiesPanel.h"
#include "PullTool.h"
#include "Ribbon.h"
#include "SketchEditor.h"
#include "ViewportOverlay.h"
#include "pc/Document.h"
#include <nlohmann/json.hpp>
#include <QAction>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QUndoStack>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cmath>

namespace {
using pcd::OpState;
using pcd::SelItem;

// ---------------------------------------------------------------------------- input helpers
void opEnsureActive(pcd::MainWindow& w) {
  if (QApplication::activeModalWidget()) return;
  if (QApplication::activeWindow() != &w) { w.activateWindow(); QApplication::setActiveWindow(&w); pump(30); }
}
QWidget* opInputTarget(pcd::MainWindow& w) {
  return w.inSketchMode() ? static_cast<QWidget*>(w.sketchEditor()) : static_cast<QWidget*>(w.viewer());
}
QWidget* opKeyTarget(pcd::MainWindow& w) {
  opEnsureActive(w);
  QWidget* f = QApplication::focusWidget();
  return (f && w.isAncestorOf(f) && f->isVisible()) ? f : opInputTarget(w);
}
void opKey(pcd::MainWindow& w, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QTest::keyClick(opKeyTarget(w), Qt::Key(k), m);
  pump(40);
}
void opType(pcd::MainWindow& w, const QString& s) {
  for (const QChar c : s) {
    QTest::keyClick(opKeyTarget(w), c.toLatin1());
    pump(15);
  }
  pump(30);
}
void opMove(pcd::MainWindow& w, const QPointF& p) {
  QWidget* t = opInputTarget(w);
  QMouseEvent mv(QEvent::MouseMove, p, t->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(t, &mv);
  pump(20);
}
void opClick(pcd::MainWindow& w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  opEnsureActive(w);
  opMove(w, p);
  QTest::mouseClick(opInputTarget(w), Qt::LeftButton, m, p.toPoint());
  pump(80);
}
QString opMsg(pcd::MainWindow& w) { return w.statusBar()->currentMessage(); }
double opVolume(pcd::MainWindow& w) {
  try { return w.modelResult().shape.IsNull() ? 0 : pc::model::massProperties(w.modelResult().shape).volume; } catch (...) { return -1; }
}
std::string opDocJson(pcd::MainWindow& w) { return pc::toJson(w.document()).dump(); }
QPointF opFacePx(pcd::MainWindow& w, const QString& face) {
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
pcd::PullTool* opPull(Sc23Ctx& ctx) { return qobject_cast<pcd::PullTool*>(ctx.tools()->active()); }
pcd::OpStatus opNow(Sc23Ctx& ctx) { return ctx.tools()->active() ? ctx.tools()->active()->operation() : pcd::OpStatus(); }
QString opNodeText(QTreeWidgetItem* n) { return n ? n->text(0) : QString("<yok>"); }

// "Sketch'i Bitir" exactly as the ribbon button does it (QAction::trigger)
void opFinishButton(pcd::MainWindow& w) {
  if (QAction* a = w.findChild<QAction*>("actFinishSketch")) a->trigger();
  pump(150);
}

// a fresh empty document (Ctrl+N; the harness runs with autoYes, nothing asks)
void opNewDoc(Sc23Ctx& ctx) {
  ctx.reset();
  if (ctx.w.inSketchMode()) ctx.w.finishSketchPublic(false);
  ctx.w.viewer()->setFocus();
  opKey(ctx.w, Qt::Key_N, Qt::ControlModifier);
  if (!waitFor([&] { return ctx.w.document().features.empty(); }, 2000)) ctx.w.newDocument();
  pump(100);
  ctx.reset();
}

// K (sketch on XY of an empty document), R, click (x, y), W Tab H Enter, Esc back to sketch Seç
void opRect(pcd::MainWindow& w, double x, double y, const QString& W, const QString& H) {
  auto* ed = w.sketchEditor();
  if (!w.inSketchMode()) { w.viewer()->setFocus(); opKey(w, Qt::Key_K); }
  opKey(w, Qt::Key_R);
  opClick(w, ed->sketchToScreen(x, y));
  opType(w, W);
  opKey(w, Qt::Key_Tab);
  opType(w, H);
  opKey(w, Qt::Key_Return);
  opKey(w, Qt::Key_Escape);
}
void opCircle(pcd::MainWindow& w, double cx, double cy, const QString& D) {
  auto* ed = w.sketchEditor();
  if (!w.inSketchMode()) { w.viewer()->setFocus(); opKey(w, Qt::Key_K); }
  opKey(w, Qt::Key_C);
  opClick(w, ed->sketchToScreen(cx, cy));
  opType(w, D);
  opKey(w, Qt::Key_Return);
  opKey(w, Qt::Key_Escape);
}

// drags the Pull arrow (press on the shaft, move along it, release): Pending preview, 600 ms auto-commit armed
bool opDragArrow(pcd::MainWindow& w, pcd::PullTool* pt, double px = 60) {
  if (!pt) return false;
  QWidget* v = w.viewer();
  const QPointF b = pt->handleBase(), t = pt->handleTip();
  QPointF u = t - b;
  const double l = std::hypot(u.x(), u.y());
  if (l < 1) return false;
  u /= l;
  const QPoint from = (b + (t - b) * 0.45).toPoint();
  opEnsureActive(w);
  v->setFocus();
  QTest::mousePress(v, Qt::LeftButton, Qt::NoModifier, from);
  for (int i = 1; i <= 8; ++i) {
    const QPointF p = QPointF(from) + u * (px * i / 8.0);
    QMouseEvent mv(QEvent::MouseMove, p, v->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(v, &mv);
    pump(15);
  }
  QTest::mouseRelease(v, Qt::LeftButton, Qt::NoModifier, (QPointF(from) + u * px).toPoint());
  pump(30);
  return true;
}

// watches for any modal dialog while armed (ANALYSIS §4.5: failures / big values never open one)
struct OpModalWatch {
  QTimer t;
  bool seen = false;
  QString what;
  OpModalWatch() {
    t.setInterval(10);
    QObject::connect(&t, &QTimer::timeout, [this] {
      if (QWidget* m = QApplication::activeModalWidget(); m && m->isVisible() && !seen) { seen = true; what = m->metaObject()->className(); }
    });
    t.start();
  }
};

// sketch with one 60 x 40 rectangle finished with the button: Pull active, nothing selected
void opRectRegion(Sc23Ctx& ctx, const QString& W = "60", const QString& H = "40") {
  opNewDoc(ctx);
  opRect(ctx.w, 0, 0, W, H);
  opFinishButton(ctx.w);
}
bool opSelectRegion(Sc23Ctx& ctx, const QString& region = "sketch:Sketch1:r0") {
  if (ctx.tools()->activeId() != "pull") ctx.tools()->activate("pull");
  ctx.w.selection()->set({SelItem{SelItem::Kind::SketchRegion, region}});
  pump(60);
  pcd::PullTool* pt = opPull(ctx);
  return pt && pt->target().valid();
}

// ============================================================================ checks
void opSmoke(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("SC23 operation: OpStatus hooks (ToolApi 1.1)");
  check(pcd::kToolApiVersionMajor == 1 && pcd::kToolApiVersionMinor >= 1,
        QString("ToolApi %1.%2").arg(pcd::kToolApiVersionMajor).arg(pcd::kToolApiVersionMinor));
  pcd::ToolManager* tm = ctx.tools();
  QSignalSpy spy(tm, &pcd::ToolManager::operationChanged);
  check(tm->activate("pull"), "activate pull");
  check(spy.count() >= 1, QString("ToolManager::operationChanged emitted on activation (%1x)").arg(spy.count()));
  if (spy.count() >= 1) check(spy.last().at(0).value<pcd::OpStatus>().toolId == "pull", "OpStatus.toolId = pull");
  spy.clear();
  pcd::OpStatus inv;
  inv.state = OpState::Invalid;
  inv.toolId = "pull";
  inv.reason = "Mesafe sıfır olamaz";
  if (tm->active()) emit tm->active()->operationChanged(inv);
  check(spy.count() == 1 && spy.last().at(0).value<pcd::OpStatus>() == inv, "active tool's operationChanged is forwarded");
  pcd::ViewportHud* hud = ctx.hud();
  check(w.statusBar()->currentMessage() == inv.reason, QString("status bar: '%1'").arg(w.statusBar()->currentMessage()));
  check(hud && hud->messageText() == inv.reason && hud->messageSeverity() == pcd::ViewportHud::Severity::Error, "HUD shows the reason in red");
  pcd::OpStatus next;
  next.state = OpState::AwaitingInput;
  next.toolId = "pull";
  w.showOperationFeedback(next);
  check(hud && hud->messageText().isEmpty(), "next non-Invalid status clears the red message");
  pcd::IActiveTool* pull = tm->active();
  tm->activate("select");
  spy.clear();
  if (pull) emit pull->operationChanged(inv);
  check(spy.count() == 0, "inactive tool's operationChanged is not forwarded");
  if (hud) hud->clearMessage();
}

// T01: Ctrl+N → K → R 60 Tab 40 Enter → Sketch'i Bitir → Yüzeyler (1), Pull, Tasarım → region → 8 Enter → solid
void opT01(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T01: rectangle → Sketch'i Bitir → surface → Pull 8 → solid (Structure types)");
  opNewDoc(ctx);
  auto* sp = w.structurePanel();
  QSignalSpy ops(ctx.tools(), &pcd::ToolManager::operationChanged);
  w.viewer()->setFocus();
  opKey(w, Qt::Key_K);
  check(w.inSketchMode(), "K: sketch mode on XY");
  // sketch tools report their operation state (O-P0-1)
  opKey(w, Qt::Key_R);
  check(opNow(ctx).toolId == "sketch.rect" && opNow(ctx).state == OpState::AwaitingSelection,
        QString("Dikdörtgen waits for the first corner: %1").arg(pcd::opStateText(opNow(ctx).state)));
  opClick(w, w.sketchEditor()->sketchToScreen(0, 0));
  check(opNow(ctx).state == OpState::AwaitingInput, QString("after the first corner: %1").arg(pcd::opStateText(opNow(ctx).state)));
  opType(w, "60");
  check(opNow(ctx).state == OpState::Preview && opNow(ctx).valueText == "60" && opNow(ctx).unit == "mm",
        QString("typing 60: %1 '%2' %3").arg(pcd::opStateText(opNow(ctx).state), opNow(ctx).valueText, opNow(ctx).unit));
  opKey(w, Qt::Key_Tab);
  opType(w, "40");
  ops.clear();
  opKey(w, Qt::Key_Return);
  bool completed = false;
  for (const auto& a : ops) if (a.at(0).value<pcd::OpStatus>().state == OpState::Completed) completed = true;
  check(completed, "Enter: the sketch tool reports Completed");
  opKey(w, Qt::Key_Escape);
  check(sp->rootNode() && sp->rootNode()->text(0).endsWith("*"), "Structure root shows '*' while the sketch is unsaved: " + opNodeText(sp->rootNode()));

  opFinishButton(w);
  check(!w.inSketchMode() && w.document().feature("Sketch1"), "Sketch'i Bitir: Sketch1 committed");
  check(ctx.tools()->activeId() == "pull", "O-P1-1: Sketch'i Bitir continues in Çek (active tool: " + ctx.tools()->activeId() + ")");
  check(w.ribbon() && w.ribbon()->currentTabKey() == "design", "O-P1-1: ribbon on Tasarım (" + (w.ribbon() ? w.ribbon()->currentTabKey() : QString()) + ")");
  check(opMsg(w).contains("1 bölge → yüzey"), "O-P0-5: status: " + opMsg(w));
  check(ctx.hud() && ctx.hud()->messageText().contains("1 bölge → yüzey"), "O-P0-5: HUD: " + (ctx.hud() ? ctx.hud()->messageText() : QString()));
  check(opNodeText(sp->surfacesNode()) == "Yüzeyler (1)", "O-P0-6: " + opNodeText(sp->surfacesNode()));
  check(opNodeText(sp->curvesNode()) == "Eğriler (0)", "O-P0-6: " + opNodeText(sp->curvesNode()));
  check(opNodeText(sp->bodiesNode()) == "Gövdeler (0)", "O-P0-6: " + opNodeText(sp->bodiesNode()));
  check(sp->sketchesNode() && sp->sketchesNode()->childCount() == 1, "Sketch'ler node kept (sketchesNode())");
  if (sp->surfacesNode() && sp->surfacesNode()->childCount() == 1) {
    QTreeWidgetItem* r = sp->surfacesNode()->child(0);
    check(r->text(1) == "Yüzey" && r->data(0, Qt::UserRole).toString() == "sketch:Sketch1:r0", "surface row: type 'Yüzey', item sketch:Sketch1:r0");
    check(r->toolTip(0).contains("Çek ile katı olur"), "surface row tooltip explains the Pull");
  }
  check(opNow(ctx).state == OpState::AwaitingSelection, QString("Pull waits for a selection: %1").arg(pcd::opStateText(opNow(ctx).state)));
  shot(w, "wpo_01_end_sketch_pull.png");

  // the region: click it (fallback: selection model)
  w.viewer()->setFocus();
  opKey(w, Qt::Key_H);
  pump(150);
  const QPointF rp = opFacePx(w, "sketch:Sketch1:r0");
  if (rp.x() >= 0) opClick(w, rp);
  if (!w.selection()->contains(SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch1:r0"})) {
    note("region click missed; selecting through the model");
    opSelectRegion(ctx);
  }
  check(opNow(ctx).state == OpState::AwaitingInput && opNow(ctx).unit == "mm",
        QString("region selected: %1 (%2)").arg(pcd::opStateText(opNow(ctx).state), opNow(ctx).unit));
  auto* pp = w.propertiesPanel();
  pp->rebuild();
  check(pp->valueText("Tür") == "Yüzey", "Özellikler: Tür = " + pp->valueText("Tür"));
  check(pp->valueText("Alan") == "2400.00 mm²", "Özellikler: Alan = " + pp->valueText("Alan"));
  check(pp->valueText("Çevre") == "200.00 mm", "Özellikler: Çevre = " + pp->valueText("Çevre"));
  check(pp->valueText("Delik sayısı") == "0", "Özellikler: Delik sayısı = " + pp->valueText("Delik sayısı"));
  // options (O-P1-2, O-P1-3)
  pcd::PullTool* pt = opPull(ctx);
  bool genel = false, thick = false, unit = false;
  if (pt)
    for (const auto& o : pt->options()) {
      if (o.key == "merge") genel = o.group == "Genel";
      if (o.key == "thicken") thick = o.group == "Genel" && o.value.toBool() && !o.enabled && o.tip.contains("SC23-CR");
      if (o.key == "unit") unit = o.value.toString().contains("mm");
    }
  check(genel && thick && unit, "Options–Çek: 'Genel' (Birleştirme), Kalınlaştır checked+disabled with reason, unit 'mm'");

  const int u0 = w.undoStack()->count();
  ops.clear();
  opType(w, "8");
  check(pt && pt->dimensionBox() && pt->dimensionBox()->isEditing() && opNow(ctx).state == OpState::Preview,
        QString("typing 8: value box, %1").arg(pcd::opStateText(opNow(ctx).state)));
  opKey(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 4000);
  completed = false;
  QString doneReason;
  for (const auto& a : ops) {
    const auto st = a.at(0).value<pcd::OpStatus>();
    if (st.state == OpState::Completed) { completed = true; doneReason = st.reason; }
  }
  check(completed && !doneReason.isEmpty(), "Pull reports Completed: " + doneReason);
  check(opNow(ctx).state == OpState::AwaitingInput, QString("…then AwaitingInput again (%1)").arg(pcd::opStateText(opNow(ctx).state)));
  check(w.modelResult().bodies.size() == 1 && std::fabs(opVolume(w) - 19200) < 0.5, QString("1 body, volume %1 (19200)").arg(opVolume(w), 0, 'f', 1));
  check(w.undoStack()->count() == u0 + 1, "one undo step");
  check(opNodeText(sp->bodiesNode()) == "Gövdeler (1)" && sp->bodiesNode()->childCount() == 1 && sp->bodiesNode()->child(0)->text(1) == "Katı",
        "Gövdeler (1), type 'Katı'");
  check(opNodeText(sp->surfacesNode()) == "Yüzeyler (0)", "the pulled region is no longer a surface: " + opNodeText(sp->surfacesNode()));
  shot(w, "wpo_02_solid.png");
}

// T02: Ø20 → "15 mm" / "1.5 cm" / "0.5 in" → distance 15 / 15 / 12.7
void opT02(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T02: circle Ø20, distances with units");
  opNewDoc(ctx);
  opCircle(w, 0, 0, "20");
  opFinishButton(w);
  check(ctx.tools()->activeId() == "pull" && opMsg(w).contains("1 bölge"), "circle finished: " + opMsg(w));
  struct C { const char* text; const char* dist; double mm; };
  const C cases[] = {{"15 mm", "15", 15}, {"1.5 cm", "15", 15}, {"0.5 in", "12.7", 12.7}};
  for (const C& c : cases) {
    if (!opSelectRegion(ctx)) { check(false, "region selectable"); return; }
    w.viewer()->setFocus();
    opType(w, c.text);
    pcd::PullTool* pt = opPull(ctx);
    double live = 0;
    const bool ok = pt && pt->dimensionBox() && pt->dimensionBox()->value(0, &live);
    check(ok && std::fabs(live - c.mm) < 1e-9, QString("'%1' is valid in the box: %2 mm").arg(c.text).arg(live));
    opKey(w, Qt::Key_Return);
    waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 4000);
    const pc::Feature* ex = w.document().feature("Extrude1");
    const double want = M_PI * 100.0 * c.mm;
    check(ex && ex->prop("distance") == c.dist && std::fabs(opVolume(w) - want) < 0.05,
          QString("'%1' → distance %2, volume %3 (%4)").arg(c.text, ex ? QString::fromStdString(ex->prop("distance")) : QString("-"))
              .arg(opVolume(w), 0, 'f', 2).arg(want, 0, 'f', 2));
    w.undoStack()->undo();
    pump(100);
  }
}

// T03: open L → Eğriler (1), open ends red; P + 5 Enter → document unchanged, "açık kontur… Doldur (F)"
void opT03(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T03: open profile stays a curve");
  opNewDoc(ctx);
  auto* ed = w.sketchEditor();
  w.viewer()->setFocus();
  opKey(w, Qt::Key_K);
  opKey(w, Qt::Key_L);
  opClick(w, ed->sketchToScreen(0, 0));
  opClick(w, ed->sketchToScreen(40, 0));
  opClick(w, ed->sketchToScreen(40, 30));
  opKey(w, Qt::Key_Escape);
  opKey(w, Qt::Key_Escape);
  int lines = 0;
  for (const auto& e : ed->sketch().entities) if (e.type == pc::SkEntityType::Line && !e.construction) ++lines;
  check(lines == 2, QString("an open L of 2 lines (%1)").arg(lines));
  opFinishButton(w);
  check(!w.inSketchMode() && w.document().feature("Sketch1"), "open sketch committed as curves (never refused as a 'failed feature')");
  check(opMsg(w).contains("kapalı bölge yok") && opMsg(w).contains("2 açık uç"), "O-P0-5 summary: " + opMsg(w));
  check(ctx.hud() && ctx.hud()->messageSeverity() == pcd::ViewportHud::Severity::Warning, "HUD summary is a warning");
  auto* sp = w.structurePanel();
  check(opNodeText(sp->curvesNode()) == "Eğriler (1)" && opNodeText(sp->surfacesNode()) == "Yüzeyler (0)",
        opNodeText(sp->curvesNode()) + " / " + opNodeText(sp->surfacesNode()));
  if (sp->curvesNode() && sp->curvesNode()->childCount() == 1)
    check(sp->curvesNode()->child(0)->text(0).contains("2 açık uç") && sp->curvesNode()->child(0)->text(1) == "Eğri",
          "curve row: " + sp->curvesNode()->child(0)->text(0));
  auto* ov = w.viewer()->findChild<pcd::ViewportOverlay*>("viewportOverlay");
  check(ov && ov->gapMarkerCount() == 2, QString("open ends marked red in the view (%1)").arg(ov ? ov->gapMarkerCount() : -1));
  shot(w, "wpo_03_open_profile.png");

  const std::string before = opDocJson(w);
  const int u0 = w.undoStack()->count();
  ctx.tools()->activate("pull");
  w.selection()->set({SelItem{SelItem::Kind::Sketch, "Sketch1"}});
  pump(60);
  w.viewer()->setFocus();
  opType(w, "5");
  opKey(w, Qt::Key_Return);
  check(opDocJson(w) == before && w.undoStack()->count() == u0, "P + 5 Enter: document and undo stack unchanged");
  check(opMsg(w).contains("açık kontur") && opMsg(w).contains("Doldur (F)"), "status: " + opMsg(w));
  check(opNow(ctx).state == OpState::Invalid && opNow(ctx).reason.contains("açık kontur"), "OpStatus Invalid: " + opNow(ctx).reason);
  check(ctx.hud() && ctx.hud()->messageSeverity() == pcd::ViewportHud::Severity::Error && ctx.hud()->messageText().contains("açık kontur"),
        "HUD: red reason");
}

// T04: rectangle + Ø10 inside: 2 regions, r0 has 1 hole; pull r0 5 → (2400 − 25π)·5
void opT04(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T04: nested profile = hole");
  opNewDoc(ctx);
  opRect(w, 0, 0, "60", "40");
  opCircle(w, 30, 20, "10");
  opFinishButton(w);
  check(opMsg(w).contains("2 bölge (1 delikli, 1 delik) → yüzey"), "summary: " + opMsg(w));
  auto* sp = w.structurePanel();
  check(opNodeText(sp->surfacesNode()) == "Yüzeyler (2)", opNodeText(sp->surfacesNode()));
  opSelectRegion(ctx, "sketch:Sketch1:r0");
  auto* pp = w.propertiesPanel();
  pp->rebuild();
  check(pp->valueText("Delik sayısı") == "1", "r0 Delik sayısı = " + pp->valueText("Delik sayısı"));
  check(pp->valueText("Alan") == QString("%1 mm²").arg(2400 - 25 * M_PI, 0, 'f', 2), "r0 Alan = " + pp->valueText("Alan"));
  shot(w, "wpo_04_nested_props.png");
  pcd::PullTool* pt = opPull(ctx);
  check(pt && pt->commitValue("5"), "pull r0 by 5");
  const double want = (2400 - 25 * M_PI) * 5;
  check(std::fabs(opVolume(w) - want) < 0.05, QString("volume %1 ((2400−25π)·5 = %2)").arg(opVolume(w), 0, 'f', 2).arg(want, 0, 'f', 2));
}

// T05: "abc", "10 xyz", "0", "-8", "1e7", "10 mm"
void opT05(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T05: invalid / zero / negative / very large values, units");
  opRectRegion(ctx);
  opSelectRegion(ctx);
  OpModalWatch modal;
  w.setAutoYes(false);   // a modal would really open now (and would be seen by the watch)
  const std::string before = opDocJson(w);
  const int u0 = w.undoStack()->count();
  auto refused = [&](const QString& text, const QString& expect) {
    w.viewer()->setFocus();
    if (!text[0].isDigit() && text[0] != '-') opKey(w, Qt::Key_Space);   // Space opens the value field (SC [G])
    opType(w, text);
    pcd::PullTool* pt = opPull(ctx);
    const QString tip = pt && pt->dimensionBox() && pt->dimensionBox()->fieldWidget(0) ? pt->dimensionBox()->fieldWidget(0)->toolTip() : QString();
    opKey(w, Qt::Key_Return);
    const pcd::OpStatus st = opNow(ctx);
    check(st.state == OpState::Invalid && st.reason.contains(expect), QString("'%1' → Invalid: %2").arg(text, st.reason));
    check(opMsg(w).contains(expect), QString("'%1' → status: %2").arg(text, opMsg(w)));
    check(ctx.hud() && ctx.hud()->messageSeverity() == pcd::ViewportHud::Severity::Error && ctx.hud()->messageText().contains(expect),
          QString("'%1' → red HUD").arg(text));
    check(opDocJson(w) == before && w.undoStack()->count() == u0, QString("'%1' → nothing committed").arg(text));
    return tip;
  };
  const QString tipAbc = refused("abc", "‘abc’ bir sayı, parametre adı ya da ifade değil");
  check(tipAbc.contains("‘abc’"), "the box tooltip names the reason: " + tipAbc);
  shot(w, "wpo_05_invalid_value.png");
  opKey(w, Qt::Key_Escape);
  refused("10 xyz", "bilinmeyen birim");
  opKey(w, Qt::Key_Escape);
  refused("0", "Mesafe sıfır olamaz");
  opKey(w, Qt::Key_Escape);
  const QString tip7 = refused("1e7", "Enter'a yeniden basın");
  check(!modal.seen, "1e7: no modal dialog (" + modal.what + ")");
  opKey(w, Qt::Key_Escape);
  pump(50);
  w.setAutoYes(true);
  // the second Enter with the same text confirms (autoYes: the mfg review of such a part would be modal otherwise)
  {
    pcd::PullTool* pt = opPull(ctx);
    check(pt && !pt->commitValue("1e7"), "commitValue('1e7') first time: refused (needs confirmation)");
    check(pt && pt->commitValue("1e7"), "commitValue('1e7') second time: confirmed");
    const pc::Feature* ex = w.document().feature("Extrude1");
    double d = 0;
    if (ex) w.evaluateExpression(QString::fromStdString(ex->prop("distance")), &d);
    check(ex && std::fabs(d - 1e7) < 1e-3, "confirmed 1e7 committed: distance " + (ex ? QString::fromStdString(ex->prop("distance")) : QString("-")));
    w.undoStack()->undo();
    pump(100);
  }
  // negative = reverse
  opSelectRegion(ctx);
  w.viewer()->setFocus();
  opType(w, "-8");
  opKey(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 4000);
  const pc::Feature* ex = w.document().feature("Extrude1");
  check(ex && std::fabs(opVolume(w) - 19200) < 0.5,
        QString("'-8' → Extrude1 (distance %1, %2), volume %3 (19200)")
            .arg(ex ? QString::fromStdString(ex->prop("distance")) : QString("-"), ex ? QString::fromStdString(ex->prop("direction")) : QString())
            .arg(opVolume(w), 0, 'f', 1));
  if (ex) {
    Bnd_Box bb;
    BRepBndLib::Add(w.modelResult().shape, bb);
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    check(z1 < 1e-6 && z0 < -7.9, QString("…on the other side of the sketch plane (z %1..%2)").arg(z0).arg(z1));
  }
  w.undoStack()->undo();
  pump(100);
  opSelectRegion(ctx);
  w.viewer()->setFocus();
  opType(w, "10 mm");
  opKey(w, Qt::Key_Return);
  waitFor([&] { return w.document().feature("Extrude1") != nullptr; }, 4000);
  ex = w.document().feature("Extrude1");
  check(ex && ex->prop("distance") == "10", "'10 mm' → distance 10");
  check(!modal.seen, "T05: no modal dialog at all");
  (void)tip7;
}

// T06: several regions = one undo step; wrong kind refused; empty selection = AwaitingSelection
void opT06(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T06: multi-region, wrong kind, empty selection");
  opNewDoc(ctx);
  opRect(w, 0, 0, "20", "20");
  opRect(w, 40, 0, "20", "20");
  opFinishButton(w);
  check(opMsg(w).contains("2 bölge"), "two regions: " + opMsg(w));
  ctx.tools()->activate("pull");
  w.selection()->clear();
  pump(40);
  check(opNow(ctx).state == OpState::AwaitingSelection, QString("empty selection: %1").arg(pcd::opStateText(opNow(ctx).state)));
  w.selection()->set({SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch1:r0"}, SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch1:r1"}});
  pump(60);
  const int u0 = w.undoStack()->count();
  pcd::PullTool* pt = opPull(ctx);
  check(pt && pt->target().valid() && pt->commitValue("5"), "both regions pulled by 5");
  check(w.undoStack()->count() == u0 + 1, QString("one undo step (%1 → %2)").arg(u0).arg(w.undoStack()->count()));
  check(std::fabs(opVolume(w) - 4000) < 0.5, QString("volume %1 (2 × 20·20·5 = 4000)").arg(opVolume(w), 0, 'f', 1));
  const QString body = w.modelResult().bodies.empty() ? QString() : QString::fromStdString(w.modelResult().bodies.front().name);
  w.selection()->set({SelItem{SelItem::Kind::Body, body}});
  pump(60);
  check(opNow(ctx).state == OpState::Invalid && opNow(ctx).reason.contains("gövdeye uygulanmaz"), "a body is refused: " + opNow(ctx).reason);
  w.selection()->clear();
  pump(40);
  check(opNow(ctx).state == OpState::AwaitingSelection, "cleared selection: AwaitingSelection again");
}

// T07: preview + Esc / M / selection / K / tab / panel / window deactivation / Ctrl+Z; Enter = exactly one commit
void opT07(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T07: event contract during a preview (ANALYSIS §4.4)");
  opRectRegion(ctx);
  auto fresh = [&]() -> pcd::PullTool* {
    ctx.tools()->activate("pull");
    opSelectRegion(ctx);
    pcd::PullTool* pt = opPull(ctx);
    if (!opDragArrow(w, pt)) return nullptr;
    return pt;
  };
  const std::string doc0 = opDocJson(w);
  const int u0 = w.undoStack()->count();
  auto same = [&](const QString& what) {
    check(opDocJson(w) == doc0 && w.undoStack()->count() == u0, what + ": document and undo stack unchanged");
  };
  // Esc
  pcd::PullTool* pt = fresh();
  check(pt && opNow(ctx).state == OpState::Preview && pt->autoCommitPending(),
        QString("drag: Preview, value %1, 600 ms timer running").arg(opNow(ctx).valueText));
  shot(w, "wpo_07_preview.png");
  w.viewer()->setFocus();
  opKey(w, Qt::Key_Escape);
  pump(700);
  same("Esc");
  check(opNow(ctx).state == OpState::AwaitingInput, "Esc: back to AwaitingInput");
  // tool change (M)
  pt = fresh();
  w.viewer()->setFocus();
  opKey(w, Qt::Key_M);
  pump(700);
  same("M (tool change)");
  check(ctx.tools()->activeId() == "move", "M: Taşı active");
  // selection change
  pt = fresh();
  w.selection()->clear();
  pump(700);
  same("selection change");
  // mode change (K): the region's sketch opens; leave it unchanged
  pt = fresh();
  w.viewer()->setFocus();
  opKey(w, Qt::Key_K);
  pump(100);
  if (w.inSketchMode()) w.finishSketchPublic(false);
  pump(700);
  same("K (mode change)");
  // ribbon tab change: no effect on the tool or the preview
  pt = fresh();
  if (w.ribbon()) w.ribbon()->setCurrentTab("display");
  pump(50);
  check(ctx.tools()->activeId() == "pull" && opNow(ctx).state == OpState::Preview, "ribbon tab change: Pull and its preview stay");
  ctx.tools()->cancel();
  if (w.ribbon()) w.ribbon()->setCurrentTab("design");
  pump(700);
  same("tab change + Esc");
  // focus moves to a panel: the auto-commit stops, the preview stays, nothing is committed implicitly
  pt = fresh();
  w.structurePanel()->setCurrentPage("structure");   // the tree must be on screen to take the focus
  pump(20);
  w.structurePanel()->tree()->setFocus();
  check(QApplication::focusWidget() == w.structurePanel()->tree(), "panel focus: the Structure tree has the focus");
  pump(900);
  same("panel focus (900 ms later)");
  check(pt && pt->previewHeld() && !pt->autoCommitPending() && opNow(ctx).state == OpState::Preview,
        "panel focus: preview kept, auto-commit stopped (" + opNow(ctx).reason + ")");
  ctx.tools()->cancel();
  pump(50);
  same("panel focus + Esc");
  // window deactivation: same; then Enter commits exactly once
  pt = fresh();
  {
    QEvent deact(QEvent::WindowDeactivate);
    QApplication::sendEvent(&w, &deact);
  }
  pump(900);
  same("window deactivation (900 ms later)");
  check(pt && pt->previewHeld() && opNow(ctx).state == OpState::Preview, "window deactivation: preview kept, auto-commit stopped");
  shot(w, "wpo_07_preview_held.png");
  opEnsureActive(w);
  w.viewer()->setFocus();
  opKey(w, Qt::Key_Return);
  pump(800);
  check(w.undoStack()->count() == u0 + 1 && w.document().feature("Extrude1") && !w.document().feature("Extrude2"),
        "Enter: exactly one commit (Extrude1)");
  const std::string doc1 = opDocJson(w);
  // Ctrl+Z during a preview: the preview is cancelled first, then the last commit is undone
  ctx.tools()->activate("pull");
  w.selection()->set({SelItem{SelItem::Kind::Face, "Extrude1.end"}});
  pump(60);
  pt = opPull(ctx);
  check(pt && pt->target().valid() && opDragArrow(w, pt) && opNow(ctx).state == OpState::Preview, "a new preview on Extrude1.end");
  w.viewer()->setFocus();
  opKey(w, Qt::Key_Z, Qt::ControlModifier);
  pump(800);
  check(opDocJson(w) == doc0 && w.undoStack()->index() == u0, "Ctrl+Z in a preview: preview dropped, then one undo (back to the sketch)");
  check(opNow(ctx).state != OpState::Preview, QString("…no preview left (%1)").arg(pcd::opStateText(opNow(ctx).state)));
  w.undoStack()->redo();
  pump(100);
  check(opDocJson(w) == doc1, "redo restores the pull");
}

// T08: Pull → Ctrl+Z / Ctrl+Y: body, Structure, selection, dirty
void opT08(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T08: undo / redo after a pull");
  opRectRegion(ctx);
  opSelectRegion(ctx);
  pcd::PullTool* pt = opPull(ctx);
  check(pt && pt->commitValue("10"), "pull 10");
  auto* sp = w.structurePanel();
  check(opNodeText(sp->bodiesNode()) == "Gövdeler (1)" && opNodeText(sp->surfacesNode()) == "Yüzeyler (0)", "after the pull: 1 body, 0 surfaces");
  check(w.selection()->count() == 1 && w.selection()->items().first().kind == SelItem::Kind::Face, "the result face is selected");
  check(w.isWindowModified() && sp->rootNode()->text(0).endsWith("*"), "dirty: root " + opNodeText(sp->rootNode()));
  w.viewer()->setFocus();
  opKey(w, Qt::Key_Z, Qt::ControlModifier);
  pump(150);
  check(w.modelResult().bodies.empty() && !w.document().feature("Extrude1"), "Ctrl+Z: the solid is gone");
  check(opNodeText(sp->bodiesNode()) == "Gövdeler (0)" && opNodeText(sp->surfacesNode()) == "Yüzeyler (1)", "Ctrl+Z: Structure back to 1 surface");
  bool stale = false;
  for (const auto& it : w.selection()->items())
    if (it.kind == SelItem::Kind::Face && !w.modelResult().faces.count(it.name.toStdString())) stale = true;
  check(!stale, "no stale face left selected");
  check(sp->rootNode()->text(0).endsWith("*") == w.isWindowModified(), "root '*' follows the dirty flag");
  opKey(w, Qt::Key_Y, Qt::ControlModifier);
  pump(150);
  check(w.modelResult().bodies.size() == 1 && std::fabs(opVolume(w) - 24000) < 0.5, "Ctrl+Y: the solid is back (24000)");
  check(opNodeText(sp->bodiesNode()) == "Gövdeler (1)", "Ctrl+Y: Gövdeler (1)");
}

// T09: Add / Cut / No merge against an existing body
void opT09(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T09: Ekle / Kes / Birleştirme yok");
  opRectRegion(ctx);
  opSelectRegion(ctx);
  pcd::PullTool* pt = opPull(ctx);
  check(pt && pt->commitValue("10"), "base block 60x40x10");
  ctx.reset();
  w.beginSketchOn(pc::SketchPlaneKind::XY);
  pump(100);
  opRect(w, 10, 10, "20", "20");
  opFinishButton(w);
  const std::string sid = "Sketch2";
  struct C { const char* merge; const char* value; int bodies; double vol; };
  const C cases[] = {{"add", "15", 1, 26000}, {"cut", "5", 1, 22000}, {"nomerge", "15", 2, 30000}};
  for (const C& c : cases) {
    ctx.tools()->activate("pull");
    ctx.tools()->setOption("merge", QString(c.merge));
    opSelectRegion(ctx, "sketch:Sketch2:r0");
    pt = opPull(ctx);
    const int faces0 = int(w.modelResult().faces.size());
    check(pt && pt->commitValue(c.value), QString("%1 %2").arg(c.merge, c.value));
    check(int(w.modelResult().bodies.size()) == c.bodies && std::fabs(opVolume(w) - c.vol) < 0.5,
          QString("%1: %2 bodies (%3), volume %4 (%5)").arg(c.merge).arg(w.modelResult().bodies.size()).arg(c.bodies)
              .arg(opVolume(w), 0, 'f', 1).arg(c.vol));
    check(int(w.modelResult().faces.size()) > faces0, QString("%1: new faces (%2 → %3)").arg(c.merge).arg(faces0).arg(w.modelResult().faces.size()));
    w.undoStack()->undo();
    pump(100);
  }
  ctx.tools()->setOption("merge", QString("auto"));
  (void)sid;
}

// T12: a deliberately failing operation: no modal, document / undo unchanged, red HUD naming the feature, also in autoYes
void opT12(Sc23Ctx& ctx) {
  pcd::MainWindow& w = ctx.w;
  section("T12: failed operation keeps the previous model");
  opRectRegion(ctx);
  opSelectRegion(ctx);
  pcd::PullTool* pt = opPull(ctx);
  check(pt && pt->commitValue("10"), "base block 60x40x10");
  QString edge;
  for (const auto& [n, e] : w.modelResult().edges) { (void)e; edge = QString::fromStdString(n); break; }
  check(!edge.isEmpty(), "an edge of the block: " + edge);
  for (bool autoYes : {false, true}) {
    w.setAutoYes(autoYes);
    ctx.tools()->activate("pull");
    ctx.tools()->setOption("edge", QString("round"));
    w.selection()->set({SelItem{SelItem::Kind::Edge, edge}});
    pump(60);
    pt = opPull(ctx);
    check(pt && pt->target().valid(), "edge armed for a round");
    const std::string before = opDocJson(w);
    const int u0 = w.undoStack()->count();
    OpModalWatch modal;
    const bool ok = pt && pt->commitValue("100");
    pump(100);
    const QString tag = autoYes ? "autoYes" : "interactive";
    check(!ok, tag + ": round r=100 is refused");
    check(!modal.seen, tag + ": no modal dialog (" + modal.what + ")");
    check(opDocJson(w) == before && w.undoStack()->count() == u0, tag + ": document and undo stack unchanged");
    check(ctx.hud() && ctx.hud()->messageSeverity() == pcd::ViewportHud::Severity::Error && ctx.hud()->messageText().contains("Fillet1"),
          tag + ": red HUD names the feature: " + (ctx.hud() ? ctx.hud()->messageText() : QString()));
    check(opMsg(w).contains("Fillet1") && opMsg(w).contains("Önceki model korundu"), tag + ": status: " + opMsg(w));
    check(opNow(ctx).state == OpState::Invalid && opNow(ctx).reason.contains("Fillet1"), tag + ": OpStatus Invalid");
    if (!autoYes) shot(w, "wpo_12_failed_feature.png");
    w.viewer()->setFocus();
    opKey(w, Qt::Key_Escape);
    pump(60);
    check(ctx.hud() && ctx.hud()->messageText().isEmpty(), tag + ": the next action (Esc) clears the red message");
  }
  w.setAutoYes(true);
  ctx.tools()->setOption("edge", QString("round"));
}

void runOpChecks(Sc23Ctx& ctx) {
  opSmoke(ctx);
  opT01(ctx);
  opT02(ctx);
  opT03(ctx);
  opT04(ctx);
  opT05(ctx);
  opT06(ctx);
  opT07(ctx);
  opT08(ctx);
  opT09(ctx);
  opT12(ctx);
  ctx.w.setAutoYes(true);
  ctx.reset();
}
} // namespace
