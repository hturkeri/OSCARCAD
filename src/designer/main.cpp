#include <QDir>
// persistcad [file.pcad] [--screenshot out.png] [--sketch-screenshot out.png] [--sketch-id ID]
//            [--dialog-screenshot out.png] [--dialog-feature ID] [--mfg-screenshot out.png]
//            [--plane-screenshot out.png] [--plane-dialog-screenshot out.png]
//            [--try-change NAME=EXPR --review-screenshot out.png] [--select-issue N] [--select-face NAME]
//            [--section xz|yz|xy|quarter [--section-offset PCT] [--section-flip]]   (view-only, with --screenshot)
#include "MainWindow.h"
#include "MfgDialogs.h"
#include <BRepAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <QApplication>
#include <QMap>
#include <QPainter>
#include <QEventLoop>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>

static void settle(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
  QApplication::processEvents();
}

static void mouse(QWidget* w, QEvent::Type t, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = {}) {
  QMouseEvent e(t, QPointF(p), QPointF(w->mapToGlobal(p)), b, bs, m);
  QApplication::sendEvent(w, &e);
}
static void click(QWidget* w, QPoint p, Qt::KeyboardModifiers m = {}) {
  mouse(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, m);
  mouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, m);
  mouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, m);
}
static void drag(QWidget* w, QPoint a, QPoint b) {
  mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 8; ++i) mouse(w, QEvent::MouseMove, a + (b - a) * i / 8, Qt::NoButton, Qt::LeftButton);
  mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

// First straight model edge (persistent name), "" if none.
static QString firstLineEdge(const pcd::MainWindow& w) {
  for (const auto& [name, e] : w.modelResult().edges) {
    try {
      if (BRepAdaptor_Curve(e).GetType() == GeomAbs_Line) return QString::fromStdString(name);
    } catch (...) {}
  }
  return QString();
}

// Interactive-flow smoke test: sketch -> constraints -> extrude -> hole -> fillet -> picking.
static int selfTest(pcd::MainWindow& w) {
  int fails = 0;
  auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++fails; };
  w.beginSketchOn(pc::SketchPlaneKind::XY);
  settle(300);
  auto* ed = w.sketchEditor();
  ed->setTool(pcd::SketchEditor::Tool::Rect);
  const QPointF o = QPointF(ed->width() / 2.0, ed->height() / 2.0);
  click(ed, QPoint(int(o.x()) - 200, int(o.y()) - 120));
  click(ed, QPoint(int(o.x()) + 200, int(o.y()) + 120));
  check(ed->sketch().entities.size() == 4, "rectangle drawn (4 lines)");
  ed->setTool(pcd::SketchEditor::Tool::Circle);
  click(ed, QPoint(int(o.x()), int(o.y())));
  click(ed, QPoint(int(o.x()) + 40, int(o.y())));
  check(ed->sketch().entities.size() == 5, "circle drawn");
  ed->setTool(pcd::SketchEditor::Tool::Select);
  const int nEnt = int(ed->sketch().entities.size());
  (void)nEnt;
  // select the circle edge, apply equal? -> select circle and fix radius via constraint API (no dialog)
  click(ed, QPoint(int(o.x()) + 40, int(o.y())));
  ed->applyConstraint(pc::SkConstraintType::Fixed);
  // drag a rectangle corner
  const int before = int(ed->sketch().constraints.size());
  const int cornerId = ed->sketch().points[2].id;
  const QPointF cp = ed->pointScreenPos(cornerId);
  const double x0 = ed->sketch().points[2].x;
  drag(ed, cp.toPoint(), cp.toPoint() + QPoint(30, 20));
  std::printf("  corner id %d x0=%g x1=%g y=%g screen=(%g,%g) npts=%d\n", cornerId, x0, ed->sketch().point(cornerId)->x, ed->sketch().point(cornerId)->y, cp.x(), cp.y(), int(ed->sketch().points.size()));
  check(std::fabs(ed->sketch().point(cornerId)->x - x0) > 0.2, "dragging a corner moves the rectangle (live solve)");
  std::printf("  DOF after edits: %d (%s), constraints %d->%d\n", ed->dof(), ed->lastSolve().message.c_str(), before, int(ed->sketch().constraints.size()));
  ed->undo();
  ed->redo();

  settle(200);
  w.grab().save(QDir::tempPath() + "/selftest_sketch.png");
  w.finishSketchPublic(true);
  settle(300);
  check(!w.inSketchMode(), "sketch finished");
  check(w.document().features.size() == 1, "sketch feature committed");
  w.createFeature(pc::FeatureType::Extrude);
  settle(300);
  check(w.featureDialog() != nullptr, "extrude dialog opened");
  if (w.featureDialog()) QMetaObject::invokeMethod(w.featureDialog(), "accept");
  settle(500);
  check(w.document().features.size() == 2, "extrude committed");
  for (const auto& s : w.modelResult().status) std::printf("  status %s ok=%d %s\n", s.featureId.c_str(), int(s.ok), s.message.c_str());
  check(!w.modelResult().shape.IsNull(), "model has shape");
  // pick a face in the viewer
  auto* v = w.viewer();
  QString picked;
  QObject::connect(v, &pc::viewer::ShapeViewer::facePicked, [&](const QString& n) { picked = n; });
  // SC-UX: the sketch left the camera in its plan view (looking into the hole at the centre): home view first
  // (the centre is the hole's rim, and the SpaceClaim scheme prefers an edge within 6 px: click beside it)
  v->setView(pc::viewer::ShapeViewer::View::Iso);
  v->fitAll();
  settle(300);
  click(v, QPoint(v->width() / 2 + 260, v->height() / 2));
  settle(200);
  std::printf("  picked face name: '%s'\n", qPrintable(picked));
  check(!picked.isEmpty(), "face picking returns persistent name");
  w.grab().save(QDir::tempPath() + "/selftest_3d.png");
  // arc / point tools + constraints in a throw-away sketch
  w.beginSketchOn(pc::SketchPlaneKind::XZ);
  settle(300);
  ed = w.sketchEditor();
  ed->setTool(pcd::SketchEditor::Tool::Arc);
  click(ed, QPoint(500, 400)); click(ed, QPoint(650, 400)); click(ed, QPoint(500, 250));
  check(ed->sketch().entities.size() == 1 && ed->sketch().entities[0].type == pc::SkEntityType::Arc, "arc drawn (center,start,end)");
  ed->setTool(pcd::SketchEditor::Tool::Line);
  click(ed, QPoint(300, 500)); click(ed, QPoint(420, 540)); click(ed, QPoint(560, 520));
  mouse(ed, QEvent::MouseButtonPress, QPoint(5, 5), Qt::RightButton, Qt::RightButton);
  mouse(ed, QEvent::MouseButtonRelease, QPoint(5, 5), Qt::RightButton, Qt::NoButton);
  check(ed->sketch().entities.size() == 3, "two chained lines drawn");
  ed->setTool(pcd::SketchEditor::Tool::Select);
  click(ed, QPoint(360, 520));  // select first line
  click(ed, QPoint(490, 530), Qt::ShiftModifier);  // add second line
  ed->applyConstraint(pc::SkConstraintType::Equal);
  ed->applyConstraint(pc::SkConstraintType::Horizontal);
  std::printf("  constraints now %d, dof %d: %s\n", int(ed->sketch().constraints.size()), ed->dof(), ed->lastSolve().message.c_str());
  w.grab().save(QDir::tempPath() + "/selftest_arc.png");
  w.finishSketchPublic(false);
  return fails;
}

static int selfTest2(pcd::MainWindow& w) {
  int fails = 0;
  auto check = [&](bool ok, const char* what) { std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++fails; };
  auto vol = [&] { return pc::model::massProperties(w.modelResult().shape).volume; };
  const double v0 = vol();
  check(w.tryParameterChange("W", "100"), "param change W=100 committed");
  settle(200);
  const double v1 = vol();
  check(v1 > v0 * 1.1, "volume grew after W increase");
  w.undoStack()->undo();
  settle(300);
  check(std::fabs(vol() - v0) < 1e-3 * v0, "undo restores volume");
  w.undoStack()->redo();
  settle(300);
  check(w.renameParam("E", "Edge"), "rename E -> Edge");
  settle(300);
  bool refs = false;
  for (const auto& f : w.document().features)
    if (f.sketch) for (const auto& c : f.sketch->constraints) if (c.valueExpr.find("Edge") != std::string::npos) refs = true;
  check(refs && !w.document().params.find("E") && w.document().params.find("Edge"), "rename rewrote sketch constraint expressions");
  bool allOk = true;
  for (const auto& s : w.modelResult().status) if (!s.ok) allOk = false;
  check(allOk && !w.modelResult().shape.IsNull(), "model still regenerates after rename");
  w.rollbackPublic(1);
  settle(300);
  const double vr = vol();
  check(vr < v1 && vr > 0, "rollback to Extrude1 shows plate only");
  w.grab().save(QDir::tempPath() + "/selftest_rollback.png");
  w.rollbackPublic(-1);
  w.rollbackPublic(int(w.document().features.size()) - 1);
  settle(300);
  w.toggleSuppressPublic("Hole1");
  settle(300);
  check(w.document().feature("Hole1")->suppressed, "suppress Hole1");
  w.undoStack()->undo();
  settle(200);
  w.deleteFeaturePublic("Fillet1");
  settle(300);
  check(w.document().feature("Fillet1") == nullptr, "delete Fillet1");
  w.grab().save(QDir::tempPath() + "/selftest_after.png");
  return fails;
}

int main(int argc, char** argv) {
  QSurfaceFormat f;
  f.setVersion(3, 3);
  f.setProfile(QSurfaceFormat::CoreProfile);
  f.setDepthBufferSize(24);
  f.setStencilBufferSize(8);
  f.setSamples(4);
  QSurfaceFormat::setDefaultFormat(f);
  QApplication app(argc, argv);
  QApplication::setApplicationName("PersistCAD");
  QApplication::setStyle("Fusion");

  QString planeShot, planeDlgShot, section;
  QString onlyBodies, viewName, viewportShot, viewportSize;   // documentation shots: body filter, std view, 3D-only image
  int sectionOffset = 0;
  bool sectionFlip = false;
  QString file, shot, sketchShot, sketchId, dialogShot, dialogFeature, mfgShot, tryChange, reviewShot, selectFace;
  int selectIssue = -1;
  bool selftest = false, selftest2 = false;
  const QStringList a = app.arguments();
  for (int i = 1; i < a.size(); ++i) {
    auto next = [&]() { return i + 1 < a.size() ? a[++i] : QString(); };
    if (a[i] == "--screenshot") shot = next();
    else if (a[i] == "--sketch-screenshot") sketchShot = next();
    else if (a[i] == "--sketch-id") sketchId = next();
    else if (a[i] == "--dialog-screenshot") dialogShot = next();
    else if (a[i] == "--dialog-feature") dialogFeature = next();
    else if (a[i] == "--mfg-screenshot") mfgShot = next();
    else if (a[i] == "--try-change") tryChange = next();
    else if (a[i] == "--review-screenshot") reviewShot = next();
    else if (a[i] == "--select-issue") selectIssue = next().toInt();
    else if (a[i] == "--select-face") selectFace = next();
    else if (a[i] == "--plane-screenshot") planeShot = next();
    else if (a[i] == "--plane-dialog-screenshot") planeDlgShot = next();
    else if (a[i] == "--only-bodies") onlyBodies = next();
    else if (a[i] == "--view") viewName = next();
    else if (a[i] == "--viewport-screenshot") viewportShot = next();
    else if (a[i] == "--viewport-size") viewportSize = next();
    else if (a[i] == "--section") section = next();
    else if (a[i] == "--section-offset") sectionOffset = next().toInt();
    else if (a[i] == "--section-flip") sectionFlip = true;
    else if (a[i] == "--selftest") selftest = true;
    else if (a[i] == "--selftest2") selftest2 = true;
    else if (!a[i].startsWith("--")) file = a[i];
  }
  const bool batch = !viewportShot.isEmpty() || selftest || selftest2 || !shot.isEmpty() || !sketchShot.isEmpty() || !dialogShot.isEmpty() || !mfgShot.isEmpty() || !reviewShot.isEmpty() || !planeShot.isEmpty() || !planeDlgShot.isEmpty();

  pcd::MainWindow w;
  w.setAutoYes(batch);
  w.show();
  if (!file.isEmpty()) w.openFile(file);
  if (!section.isEmpty()) {
    if (!w.sectionControls()->applySpec(section)) {
      std::fprintf(stderr, "--section: bilinmeyen değer '%s' (xz|yz|xy|quarter|none)\n", qPrintable(section));
      return 2;
    }
    w.sectionControls()->setOffsetPercent(sectionOffset);
    w.sectionControls()->setFlipped(sectionFlip);
  }
  if (!onlyBodies.isEmpty()) {   // show only the listed bodies (comma separated)
    QList<pcd::SelItem> keep;
    for (const QString& n : onlyBodies.split(',', Qt::SkipEmptyParts)) keep << pcd::SelItem{pcd::SelItem::Kind::Body, n};
    w.isolateItems(keep);   // = Diğerlerini gizle (one view-state step)
    w.viewer()->fitAll();
  }
  if (!viewName.isEmpty()) {
    using V = pc::viewer::ShapeViewer::View;
    const QMap<QString, V> views = {{"iso", V::Iso}, {"top", V::Top}, {"bottom", V::Bottom}, {"front", V::Front},
                                    {"back", V::Back}, {"left", V::Left}, {"right", V::Right}};
    if (!views.contains(viewName)) {
      std::fprintf(stderr, "--view: bilinmeyen değer '%s' (iso|top|bottom|front|back|left|right)\n", qPrintable(viewName));
      return 2;
    }
    w.viewer()->setView(views.value(viewName));
  }
  if (!batch) return app.exec();

  settle(700);
  if (selftest) return selfTest(w);
  if (selftest2) {
    if (file.isEmpty() || !w.document().params.find("W") || w.modelResult().shape.IsNull()) {
      std::fprintf(stderr, "--selftest2 needs a .pcad file with parameters W and E (e.g. examples/*.pcad); none/unsuitable given.\n");
      return 2;
    }
    return selfTest2(w);
  }
  if (!selectFace.isEmpty()) {
    // emulate a pick in the viewer's signal path
    QMetaObject::invokeMethod(w.viewer(), "facePicked", Q_ARG(QString, selectFace));
  }
  if (!tryChange.isEmpty()) {
    const int eq = tryChange.indexOf('=');
    if (eq > 0) {
      if (!reviewShot.isEmpty()) w.setReviewScreenshotPath(reviewShot);
      const bool ok = w.tryParameterChange(tryChange.left(eq).toStdString(), tryChange.mid(eq + 1).toStdString());
      std::printf("change %s -> %s\n", qPrintable(tryChange), ok ? "committed" : "rejected");
      settle(600);
    }
  }
  if (selectIssue >= 0) { w.selectIssueRow(selectIssue); settle(500); }
  if (!viewportShot.isEmpty()) {   // 3D view only, rendered off-screen at any size (default 2400x1800)
    int vw = 2400, vh = 1800;
    const QStringList wh = viewportSize.split('x');
    if (wh.size() == 2 && wh[0].toInt() > 0 && wh[1].toInt() > 0) { vw = wh[0].toInt(); vh = wh[1].toInt(); }
    settle(500);
    w.viewer()->fitAll();   // fit for the image's aspect ratio
    w.viewer()->renderToImage(vw, vh).save(viewportShot);
    std::printf("saved %s\n", qPrintable(viewportShot));
  }
  if (!shot.isEmpty()) {
    settle(500);
    w.grab().save(shot);
    std::printf("saved %s\n", qPrintable(shot));
  }
  if (!sketchShot.isEmpty()) {
    std::string id = sketchId.toStdString();
    if (id.empty()) {
      size_t best = 0;
      for (const auto& ft : w.document().features)
        if (ft.type == pc::FeatureType::Sketch && ft.sketch && ft.sketch->entities.size() + ft.sketch->constraints.size() >= best) {
          best = ft.sketch->entities.size() + ft.sketch->constraints.size();
          id = ft.id;
        }
    }
    w.enterSketchEdit(id);
    settle(700);
    w.grab().save(sketchShot);
    std::printf("saved %s (sketch %s)\n", qPrintable(sketchShot), id.c_str());
  }
  if (!dialogShot.isEmpty()) {
    std::string id = dialogFeature.toStdString();
    if (id.empty())
      for (const auto& ft : w.document().features)
        if (ft.type == pc::FeatureType::Extrude) { id = ft.id; break; }
    w.openFeatureEditor(id);
    settle(500);
    if (w.featureDialog()) { w.featureDialog()->grab().save(dialogShot); std::printf("saved %s\n", qPrintable(dialogShot)); }
  }
  if (!planeShot.isEmpty()) {
    // datum planes + coloured bodies: create one plane on a straight edge if the sample has none
    if (w.modelResult().datumPlanes.empty()) {
      const QString e = firstLineEdge(w);
      if (!e.isEmpty()) w.createPlaneOnEdge(e, "edgeNormal");
      settle(600);
    }
    std::printf("planes: %d bodies: %d\n", int(w.modelResult().datumPlanes.size()), int(w.modelResult().bodies.size()));
    settle(500);
    w.grab().save(planeShot);
    std::printf("saved %s\n", qPrintable(planeShot));
  }
  if (!planeDlgShot.isEmpty()) {
    w.openPlaneDialog(firstLineEdge(w), "edgeNormal");
    settle(900);
    if (w.featureDialog()) {
      w.featureDialog()->grab().save(planeDlgShot);
      std::printf("saved %s\n", qPrintable(planeDlgShot));
      QString mainPath = planeDlgShot;
      mainPath.replace(".png", "_main.png");
      QImage img = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
      const QImage d = w.featureDialog()->grab().toImage();
      QPainter p(&img);
      const QPoint at(std::max(0, img.width() - d.width() - 30), std::max(0, img.height() - d.height() - 60));
      p.fillRect(QRect(at + QPoint(6, 6), d.size()), QColor(0, 0, 0, 60));
      p.drawImage(at, d);
      p.setPen(QColor(120, 130, 140));
      p.drawRect(QRect(at, d.size()).adjusted(0, 0, -1, -1));
      p.end();
      img.save(mainPath);
      std::printf("saved %s\n", qPrintable(mainPath));
    }
  }
  if (!mfgShot.isEmpty()) {
    pcd::MfgSettingsDialog d(w.document().mfg);
    d.show();
    settle(400);
    d.grab().save(mfgShot);
    std::printf("saved %s\n", qPrintable(mfgShot));
  }
  return 0;
}
