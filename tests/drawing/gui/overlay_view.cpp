// WP-2 GUI test (QPA offscreen): OverlayView — layers / role + confidence colours, zoom, selection, and every tool
// emitting the right UserEdit (the view never mutates the IR).
#include "pc/drawing/ui/OverlayView.h"
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace pc::drawing;
using namespace pc::drawing::ui;
using nlohmann::json;
Q_DECLARE_METATYPE(pc::drawing::UserEdit)

namespace {

int g_fail = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
  } while (0)

DrawingIR makeIR() {
  DrawingIR ir;
  PrimitiveSheet& s = ir.norm;
  s.sheet = {{0, 0}, {420, 297}};
  auto line = [&](int id, Vec2 a, Vec2 b, float conf) {
    Primitive p; p.id = id; p.kind = PrimKind::Line; p.p0 = a; p.p1 = b; p.conf = conf; s.prims.push_back(p);
  };
  line(1, {50, 50}, {150, 50}, 0.95f);
  line(2, {50, 60}, {50, 150}, 0.4f);        // low confidence -> red glow
  Primitive c; c.id = 3; c.kind = PrimKind::Circle; c.center = {200, 150}; c.radius = 30; c.conf = 0.7f; s.prims.push_back(c);
  Primitive a; a.id = 4; a.kind = PrimKind::Arc; a.center = {320, 150}; a.radius = 20; a.a0 = 0; a.a1 = 90; a.conf = 0.95f;
  s.prims.push_back(a);
  TextRun t; t.id = 5; t.utf8 = "Ø20 H7"; t.height = 3.5; t.conf = 0.8f;
  t.quad = {Vec2{100, 100}, Vec2{120, 100}, Vec2{120, 104}, Vec2{100, 104}};
  t.origin = t.quad[0];
  s.texts.push_back(t);
  ir.layout.border = {{10, 10}, {410, 287}};
  ir.layout.title.box = {{240, 10}, {410, 60}};
  ViewRegion v; v.id = 10; v.box = {{40, 120}, {110, 200}}; v.role = ViewRole::Front; ir.layout.views.push_back(v);
  ir.annot.roles[1] = LineRole::Visible;
  ir.annot.roles[2] = LineRole::Hidden;
  Dimension d; d.id = 20; d.anchors = {Vec2{50, 40}, Vec2{150, 40}}; d.conf = 0.9f; d.geomPrimIds = {1};
  ir.annot.dims.push_back(d);
  ir.nextId = 30;
  return ir;
}

QPoint at(OverlayView& v, double x, double y) { return v.mapFromScene(QPointF(x, -y)); }

json lastPayload(QSignalSpy& spy, std::string* op = nullptr) {
  if (spy.isEmpty()) return json();
  const auto e = spy.last().at(0).value<UserEdit>();
  if (op) *op = e.op;
  return json::parse(e.payload);
}

QRgb px(const QImage& img, double x, double y, double s) {   // sheet mm -> image pixel (sheet 420x297)
  return img.pixel(static_cast<int>(x * s), static_cast<int>((297 - y) * s));
}

json findOp(QSignalSpy& spy, int from, const std::string& op) {
  for (int i = from; i < spy.count(); ++i) {
    const auto e = spy.at(i).at(0).value<UserEdit>();
    if (e.op == op) return json::parse(e.payload);
  }
  return json();
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  qRegisterMetaType<UserEdit>("pc::drawing::UserEdit");
  qRegisterMetaType<std::vector<int>>("std::vector<int>");
  qRegisterMetaType<std::vector<Vec2>>("std::vector<pc::drawing::Vec2>");
  DrawingIR ir = makeIR();
  OverlayView view;
  view.resize(900, 650);
  view.setIR(&ir);
  view.show();
  CHECK(QTest::qWaitForWindowExposed(&view));
  view.zoomToSheet();
  CHECK(view.layers() == LayerDefault);

  // ---- rendering: roles + confidence glow
  {
    const QImage img = view.renderToImage(840);   // 2 px / mm
    CHECK(img.width() == 840 && std::abs(img.height() - 594) <= 1);
    const QRgb l1 = px(img, 100, 50, 2);
    CHECK(qGray(l1) < 120);                                  // visible line: dark
    const QRgb glow = px(img, 51.2, 100, 2);
    CHECK(qRed(glow) > qGreen(glow) + 30);                   // conf 0.4 -> red glow next to the line
    const QRgb cglow = px(img, 231.2, 150, 2);
    CHECK(qRed(cglow) > qBlue(cglow) + 30 && qGreen(cglow) > qBlue(cglow) + 30);   // conf 0.7 -> yellow
    CHECK(qGray(px(img, 320 + 14.14, 150 + 14.14, 2)) < 160);   // arc 0..90 CCW: upper-right quadrant drawn
    CHECK(qGray(px(img, 320 + 14.14, 150 - 14.14, 2)) > 230);   // lower-right quadrant empty
    if (const char* dbg = std::getenv("PC_WP2_DEBUG_DIR")) img.save(QString::fromUtf8(dbg) + "/overlay_view.png");
    const QRgb empty = px(img, 300, 250, 2);
    CHECK(qGray(empty) > 240);
    view.setLayers(LayerDefault & ~LayerPrimitives & ~LayerConfidence);
    const QImage img2 = view.renderToImage(840);
    CHECK(qGray(px(img2, 100, 50, 2)) > 200);                // primitives hidden
    view.setLayers(LayerDefault | LayerDimensions);
    // background placement: 100x100 px black image, 1 px = 1 mm, top-left at sheet (300, 280)
    QImage bg(100, 100, QImage::Format_Grayscale8);
    bg.fill(0);
    view.setBackground(bg, Affine2{1, 0, 0, -1, 300, 280});
    const QImage img3 = view.renderToImage(840);
    CHECK(qGray(px(img3, 350, 230, 2)) < 200);
    CHECK(qGray(px(img3, 350, 170, 2)) > 240);
    view.clearBackground();
  }

  QSignalSpy selSpy(&view, &OverlayView::selectionChanged);
  QSignalSpy editSpy(&view, &OverlayView::editRequested);
  QSignalSpy pickSpy(&view, &OverlayView::geometryPicked);
  QSignalSpy actSpy(&view, &OverlayView::elementActivated);
  QWidget* vp = view.viewport();

  // ---- selection (click, ctrl-click, rubber band), delete -> UserEdits
  view.setTool(OverlayTool::Select);
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 100, 50));
  CHECK(view.selection() == std::vector<int>{1});
  CHECK(selSpy.count() == 1);
  QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, at(view, 50, 100));
  CHECK((view.selection() == std::vector<int>{1, 2}));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 110, 102));
  CHECK(view.selection() == std::vector<int>{5});         // text box
  view.setFocus();
  QTest::keyClick(&view, Qt::Key_Delete);
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::deleteTexts);
    CHECK(p["ids"] == json::array({5}));
    CHECK(editSpy.last().at(0).value<UserEdit>().stage == Stage::Normalize);
  }
  QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, at(view, 40, 30));
  QTest::mouseMove(vp, at(view, 100, 80));
  QTest::mouseMove(vp, at(view, 160, 160));
  QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, at(view, 160, 160));
  {
    auto s = view.selection();
    CHECK(std::find(s.begin(), s.end(), 1) != s.end() && std::find(s.begin(), s.end(), 2) != s.end());
    CHECK(std::find(s.begin(), s.end(), 3) == s.end());
  }
  const int before = static_cast<int>(editSpy.count());
  QTest::keyClick(&view, Qt::Key_Delete);
  {
    const json p = findOp(editSpy, before, edit_op::deletePrims);
    CHECK(p.is_object() && p["ids"] == json::array({1, 2}));
    CHECK(findOp(editSpy, before, edit_op::deleteDimension).value("dimId", -1) == 20);
  }
  view.setSelection({3});
  view.refresh();                                          // selection survives refresh
  CHECK(view.selection() == std::vector<int>{3});
  QTest::mouseDClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 230, 150));
  CHECK(!actSpy.isEmpty() && actSpy.last().at(0).toInt() == 3);

  // ---- zoom / pan
  {
    const double s0 = view.transform().m11();
    QWheelEvent we(QPointF(at(view, 200, 150)), view.mapToGlobal(at(view, 200, 150)), QPoint(), QPoint(0, 240),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(vp, &we);
    CHECK(view.transform().m11() > s0 * 1.4);
    view.zoomToBox({{40, 40}, {160, 160}}, 5);
    CHECK(view.transform().m11() > s0 * 2);
    view.zoomToSheet();
    CHECK(std::abs(view.transform().m11() - s0) < 1e-6 * s0 + 1e-9);
  }

  const double tolMm = 1.5;
  auto near = [&](const json& v, double x, double y) {
    return std::abs(v[0].get<double>() - x) < tolMm && std::abs(v[1].get<double>() - y) < tolMm;
  };
  // ---- draw tools
  view.setStage(Stage::Normalize);
  view.setTool(OverlayTool::DrawLine);
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 300, 100));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 380, 100));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::addPrim && p["kind"] == "Line");
    CHECK(near(p["p0"], 300, 100) && near(p["p1"], 380, 100));
  }
  view.setTool(OverlayTool::DrawCircle);
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 300, 220));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 320, 220));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::addPrim && p["kind"] == "Circle");
    CHECK(std::abs(p["radius"].get<double>() - 20) < tolMm);
  }
  view.setTool(OverlayTool::DrawArc);   // start (330,200) through (300,230) end (270,200): upper half, CCW 0..180
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 330, 200));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 300, 230));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 270, 200));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::addPrim && p["kind"] == "Arc");
    CHECK(near(p["center"], 300, 200));
    CHECK(std::abs(p["a0"].get<double>() - 0) < 5 || std::abs(p["a0"].get<double>() - 360) < 5);
    CHECK(std::abs(p["a1"].get<double>() - p["a0"].get<double>() - 180) < 5);
  }

  // ---- boxes: new view, move view, resize title block
  view.setStage(Stage::Layout);
  view.setTool(OverlayTool::EditBoxes);
  QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, at(view, 150, 230));
  QTest::mouseMove(vp, at(view, 180, 250));
  QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, at(view, 200, 270));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::addView);
    CHECK(near(p["box"]["lo"], 150, 230) && near(p["box"]["hi"], 200, 270));
    CHECK(editSpy.last().at(0).value<UserEdit>().stage == Stage::Layout);
  }
  QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, at(view, 75, 160));
  QTest::mouseMove(vp, at(view, 80, 165));
  QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, at(view, 85, 170));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::setViewBox && p["viewId"] == 10);
    CHECK(near(p["box"]["lo"], 50, 130) && near(p["box"]["hi"], 120, 210));
  }
  QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, at(view, 240, 60));   // title block top-left corner
  QTest::mouseMove(vp, at(view, 235, 65));
  QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, at(view, 230, 70));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::setTitleBlockBox);
    CHECK(near(p["box"]["lo"], 230, 10) && near(p["box"]["hi"], 410, 70));
  }

  // ---- snap: drag line 1's end (150,50) onto the circle-free point (160, 55)
  view.setStage(Stage::Normalize);
  view.setTool(OverlayTool::Snap);
  QTest::mousePress(vp, Qt::LeftButton, Qt::NoModifier, at(view, 150, 50));
  QTest::mouseMove(vp, at(view, 155, 52));
  QTest::mouseRelease(vp, Qt::LeftButton, Qt::NoModifier, at(view, 160, 55));
  {
    std::string op;
    const json p = lastPayload(editSpy, &op);
    CHECK(op == edit_op::snapPoint && p["id"] == 1 && p["end"] == 1);
    CHECK(near(p["to"], 160, 55));
  }

  // ---- pick geometry for a dimension binding (two clicks)
  view.setTool(OverlayTool::PickGeometry);
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 100, 50));
  QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, at(view, 170, 150));
  CHECK(pickSpy.count() == 1);
  if (!pickSpy.isEmpty()) {
    const auto ids = pickSpy.last().at(0).value<std::vector<int>>();
    CHECK((ids == std::vector<int>{1, 3}));
  }

  // ---- the view never mutates the IR
  CHECK(ir.norm.prims.size() == 4 && ir.norm.texts.size() == 1 && ir.layout.views.size() == 1);
  view.setIR(nullptr);
  CHECK(!view.renderToImage(100).isNull());
  std::printf("overlay_view: %d failure(s), %lld edits\n", g_fail, static_cast<long long>(editSpy.count()));
  return g_fail ? 1 : 0;
}
