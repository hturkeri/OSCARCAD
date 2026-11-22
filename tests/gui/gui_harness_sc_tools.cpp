// SC-UX acceptance harness — WP-B (tools, "TL" items of docs/spaceclaim_ux/ACCEPTANCE.md).
//
// MainWindow of this work package does not implement IDesignHost yet (WP-A does), so the harness drives the REAL
// WP-B code (ToolManager, tools, SketchEditor overlay, DimensionBox, viewer navigation) through a small in-file
// host, ScHost: a ShapeViewer (SpaceClaim navigation) + the SketchEditor attached as overlay + SelectionModel +
// ToolManager, a document that regenerates on every commit, an undo counter, single-letter shortcuts on the tool
// actions (as WP-A's Shortcuts.cpp will assign them) and the real NewParameterDialog for "Grup oluştur".
// Every interaction is a real input event (QTest mouse / key) unless noted.
//
// Usage: pc_gui_harness_sc_tools [--shots]    (--shots writes docs/spaceclaim_ux/shots/B_*.png)
#include "DimensionBox.h"
#include "ExprEdit.h"
#include "PullMapping.h"
#include "PullTool.h"
#include "SketchEditor.h"
#include "SketchRegions.h"
#include "ToolApi.h"
#include "ToolGuideBar.h"
#include "TrimLogic.h"
#include "ViewportOverlay.h"
#include "harness_robust.h"
#include "pc/Expression.h"
#include "pc/ShapeViewer.h"
#include "sc_stub_host.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <TopoDS_Compound.hxx>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <functional>

#ifndef PC_SC_SHOTS_DIR
#define PC_SC_SHOTS_DIR "docs/spaceclaim_ux/shots"
#endif
#ifndef PC_SAMPLES_DIR
#define PC_SAMPLES_DIR "samples"
#endif

using namespace pcd;

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

std::function<void(QWidget*)> g_onModal;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    QWidget* w = QApplication::activeModalWidget();
    if (!w || !w->isVisible() || w->property("tlHandled").toBool()) return;
    if (pch::enforceDeadline(w)) return;
    w->setProperty("tlHandled", true);
    if (g_onModal) g_onModal(w);
    else {
      std::printf("  [warn] unhandled modal %s '%s' -> rejected\n", w->metaObject()->className(), qPrintable(w->windowTitle()));
      if (auto* d = qobject_cast<QDialog*>(w)) QMetaObject::invokeMethod(d, "reject", Qt::QueuedConnection);
    }
  });
  t->start();
}

// =============================================================================== test host
class ScHost : public QMainWindow, public IDesignHost {
public:
  pc::viewer::ShapeViewer* view = nullptr;
  SketchEditor* ed = nullptr;
  SelectionModel sel;
  ToolManager* tm = nullptr;
  pc::Document doc, work;
  pc::model::ModelResult model;
  int undoCount = 0;
  DesignMode mode = DesignMode::ThreeD;
  std::string sketchId;
  bool sketchNew = false;
  double fo[3] = {0, 0, 0}, fu[3] = {1, 0, 0}, fv[3] = {0, 1, 0}, fn[3] = {0, 0, 1};
  QStringList emphFaces;
  QString lastMsg;

  explicit ScHost(const pc::Document& d = pc::Document()) {
    setWindowTitle("PersistCAD — SC-UX tools harness");
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    view = new pc::viewer::ShapeViewer(central);
    view->setObjectName("viewer");
    view->setNavigationScheme(pc::viewer::ShapeViewer::NavigationScheme::SpaceClaim);
    lay->addWidget(view, 1);
    setCentralWidget(central);
    statusBar()->setObjectName("statusBar");
    ed = new SketchEditor(view);
    ed->attachToViewer(view);
    ed->hide();
    tm = new ToolManager(this, this);
    tm->installKeyRouting(view);
    tm->installKeyRouting(ed);
    tm->createMiniToolbar(view);
    tm->createToolGuideBar(view);
    QObject::connect(tm, &ToolManager::promptChanged, this, [this](const QString& s) { if (!s.isEmpty()) showPrompt(s, 0); });
    QObject::connect(view, &pc::viewer::ShapeViewer::picked, this,
                     [this](const QString& n, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos) {
                       PickEvent e;
                       e.item = SelItem{kindOf(n, kind), n};
                       e.modifiers = mods;
                       e.clickCount = clicks;
                       e.viewerPos = pos;
                       tm->handlePick(e);
                     });
    QObject::connect(view, &pc::viewer::ShapeViewer::hovered, this, [this](const QString& n, int kind) { tm->handleHover(SelItem{kindOf(n, kind), n}); });
    QObject::connect(&sel, &SelectionModel::changed, this, [this] { highlight(); });
    QObject::connect(ed, &SketchEditor::statusMessage, this, [this](const QString& m) { if (!m.isEmpty()) statusBar()->showMessage(m); });
    // single-letter shortcuts as WP-A's Shortcuts.cpp assigns them (SPEC §4)
    const std::pair<const char*, const char*> keys[] = {{"select", "S"}, {"pull", "P"}, {"move", "M"}, {"fill", "F"}, {"combine", "I"},
                                                        {"sketch.line", "L"}, {"sketch.rect", "R"}, {"sketch.circle", "C"}, {"sketch.trim", "T"}};
    for (const auto& [id, k] : keys) {
      QAction* a = tm->action(id);
      a->setShortcut(QKeySequence(k));
      a->setShortcutContext(Qt::WindowShortcut);
      addAction(a);
    }
    auto hostKey = [this](const char* k, std::function<void()> fn) {
      auto* a = new QAction(this);
      a->setShortcut(QKeySequence(k));
      a->setShortcutContext(Qt::WindowShortcut);
      QObject::connect(a, &QAction::triggered, this, fn);
      addAction(a);
    };
    hostKey("K", [this] { setDesignMode(DesignMode::Sketch); });
    hostKey("D", [this] { setDesignMode(DesignMode::ThreeD); });
    hostKey("V", [this] { if (mode == DesignMode::Sketch) ed->planView(); });
    hostKey("H", [this] { view->setHomeView(); });
    setDocument(d);
    resize(1400, 900);
  }

  static SelItem::Kind kindOf(const QString& n, int kind) {
    std::string sid;
    char t = 0;
    if (parseSketchItemName(n, &sid, &t, nullptr)) return t == 'r' ? SelItem::Kind::SketchRegion : t == 'e' ? SelItem::Kind::SketchCurve : SelItem::Kind::SketchPoint;
    if (kind == pc::viewer::ShapeViewer::PickEdge) return SelItem::Kind::Edge;
    if (kind == pc::viewer::ShapeViewer::PickDatum) return SelItem::Kind::Datum;
    return SelItem::Kind::Face;
  }

  void setDocument(const pc::Document& d) {
    doc = d;
    regen();
    updateViewer(false);
    sel.clear();
    tm->onDocumentChanged();
  }
  void regen() { model = pc::model::regenerate(doc); }
  void highlight() {
    QStringList faces = emphFaces, edges;
    for (const auto& it : sel.items()) {
      if (it.kind == SelItem::Kind::Face || it.kind == SelItem::Kind::SketchRegion) faces << it.name;
      else if (it.kind == SelItem::Kind::Edge || it.kind == SelItem::Kind::SketchCurve) edges << it.name;
      else if (it.kind == SelItem::Kind::Body) {
        if (const auto* b = model.body(it.name.toStdString())) for (const auto& f : b->faces) faces << QString::fromStdString(f);
      }
    }
    view->setHighlightedFaces(faces, QColor(250, 160, 50));
    view->setHighlightedEdges(edges, QColor(255, 120, 0));
    std::vector<pc::viewer::DatumPlaneDisplay> planes;
    for (const auto& [id, dp] : model.datumPlanes) {
      pc::viewer::DatumPlaneDisplay p;
      p.name = QString::fromStdString(id);
      const gp_Ax3& f = dp.frame;
      for (int i = 0; i < 3; ++i) {
        p.origin[i] = f.Location().Coord(i + 1);
        p.u[i] = f.XDirection().Coord(i + 1);
        p.v[i] = f.YDirection().Coord(i + 1);
      }
      p.halfSize = dp.displaySize;
      p.highlighted = sel.contains(SelItem{SelItem::Kind::Datum, p.name});
      planes.push_back(p);
    }
    view->setDatumPlanes(planes);
  }
  void updateViewer(bool keepCamera) {
    view->clearGhost();
    TopoDS_Compound comp;
    BRep_Builder bb;
    bb.MakeCompound(comp);
    if (!model.shape.IsNull()) bb.Add(comp, model.shape);
    auto faces = model.faces;
    auto edges = model.edges;
    const std::set<std::string> shown = unconsumedSketches(doc);
    appendSketchRegionFaces(doc, model, shown, comp, faces);
    appendSketchCurveEdges(doc, model, shown, comp, edges);
    view->setModel(comp, faces, edges, keepCamera);
    highlight();
  }

  // ---- IDesignHost
  QWidget* hostWindow() override { return this; }
  const pc::Document& document() const override { return doc; }
  const pc::model::ModelResult& modelResult() const override { return model; }
  int rollbackIndex() const override { return -1; }
  pc::viewer::ShapeViewer* viewer() const override { return view; }
  SketchEditor* sketchEditor() const override { return ed; }
  SelectionModel* selection() const override { return const_cast<SelectionModel*>(&sel); }
  DesignMode designMode() const override { return mode; }
  bool setDesignMode(DesignMode m) override {
    if (m == mode) return true;
    if (mode == DesignMode::Sketch && !finishSketch(true)) return false;
    if (m == DesignMode::Sketch) {
      pc::SketchPlane pl;
      const QList<SelItem> s = sel.items();
      if (!s.isEmpty() && s.first().kind == SelItem::Kind::Face) { pl.kind = pc::SketchPlaneKind::OnFace; pl.faceRef = s.first().name.toStdString(); }
      else if (!s.isEmpty() && s.first().kind == SelItem::Kind::Datum) { pl.kind = pc::SketchPlaneKind::OnPlane; pl.planeRef = s.first().name.toStdString(); }
      return beginSketch(pl);
    }
    mode = m;
    tm->onDesignModeChanged(m);
    return true;
  }
  bool beginSketch(const pc::SketchPlane& plane) override {
    if (mode == DesignMode::Sketch && !finishSketch(true)) return false;
    work = doc;
    pc::Sketch sk = pc::Sketch::create();
    sk.plane = plane;
    sketchId = work.uniqueFeatureId(pc::FeatureType::Sketch);
    sketchNew = true;
    pc::Document cand = doc;
    pc::Feature f;
    f.id = sketchId;
    f.type = pc::FeatureType::Sketch;
    f.sketch = sk;
    cand.features.push_back(f);
    gp_Ax3 fr;
    std::string err;
    if (!pc::model::sketchFrame(cand, sketchId, model.params, model, fr, &err)) { note("sketchFrame failed: " + QString::fromStdString(err)); return false; }
    return openSession(sk, fr, QString("%1 (%2)").arg(QString::fromStdString(sketchId), plane.kind == pc::SketchPlaneKind::OnFace ? "yüz" : "düzlem"));
  }
  bool openSession(const pc::Sketch& sk, const gp_Ax3& fr, const QString& title) {
    for (int i = 0; i < 3; ++i) {
      fo[i] = fr.Location().Coord(i + 1); fu[i] = fr.XDirection().Coord(i + 1);
      fv[i] = fr.YDirection().Coord(i + 1); fn[i] = fr.Direction().Coord(i + 1);
    }
    sel.clear();
    ed->setContext(&work);
    ed->setSketch(sk, title);
    ed->setPlaneFrame(fo, fu, fv);
    ed->show();
    ed->raise();
    ed->setFocus();
    QStringList all;
    for (const auto& [n, _] : model.faces) all << QString::fromStdString(n);
    view->setTransparentFaces(all, 0.35);
    mode = DesignMode::Sketch;
    tm->onDesignModeChanged(DesignMode::Sketch);
    ed->planView();
    return true;
  }
  bool editSketch(const std::string& id) override {
    const pc::Feature* f = doc.feature(id);
    if (!f || !f->sketch) return false;
    work = doc;
    sketchId = id;
    sketchNew = false;
    gp_Ax3 fr;
    if (!sketchFrameOf(model, id, fr)) return false;
    return openSession(*f->sketch, fr, QString::fromStdString(id));
  }
  bool finishSketch(bool accept) override {
    if (mode != DesignMode::Sketch) return true;
    bool ok = true;
    if (accept && ed->modified()) {
      pc::Document cand = doc;
      for (const auto& p : work.params.all())
        if (!cand.params.find(p.name) && ed->referencesParameter(p.name)) cand.params.add(p);
      const pc::Sketch sk = ed->sketch();
      if (sketchNew) {
        if (!(sk.entities.empty() && sk.points.size() <= 1)) {
          pc::Feature f;
          f.id = sketchId;
          f.type = pc::FeatureType::Sketch;
          f.sketch = sk;
          cand.features.push_back(f);
          ok = commitDocument(cand, QString("Sketch %1").arg(QString::fromStdString(sketchId)));
        }
      } else if (pc::Feature* f = cand.feature(sketchId)) {
        f->sketch = sk;
        ok = commitDocument(cand, QString("Sketch %1").arg(QString::fromStdString(sketchId)));
      }
    }
    if (!ok) return false;
    ed->hide();
    view->setTransparentFaces({});
    mode = DesignMode::ThreeD;
    tm->onDesignModeChanged(DesignMode::ThreeD);
    view->setFocus();
    return true;
  }
  std::string openSketchId() const override { return mode == DesignMode::Sketch ? sketchId : std::string(); }
  bool beginSketchPlanePick() override { showPrompt("Sketch düzlemi seç (test host: XY)", 0); return true; }
  bool openSketchFrame(double o[3], double u[3], double v[3], double n[3]) const override {
    if (mode != DesignMode::Sketch) return false;
    for (int i = 0; i < 3; ++i) { o[i] = fo[i]; u[i] = fu[i]; v[i] = fv[i]; n[i] = fn[i]; }
    return true;
  }
  pc::Document* expressionContext() override { return mode == DesignMode::Sketch ? &work : &doc; }
  bool commitDocument(const pc::Document& cand, const QString& undoText) override {
    doc = cand;
    regen();
    ++undoCount;
    for (const auto& s : model.status)
      if (!s.ok) note(QString("regen: %1: %2").arg(QString::fromStdString(s.featureId), QString::fromStdString(s.message)));
    if (mode == DesignMode::Sketch) for (const auto& p : doc.params.all()) if (!work.params.find(p.name)) work.params.add(p);
    updateViewer(true);
    tm->onDocumentChanged();
    note("commit: " + undoText);
    return true;
  }
  bool insertFeature(const pc::Feature& f, const QString& undoText) override {
    pc::Document c = doc;
    c.features.push_back(f);
    return commitDocument(c, undoText);
  }
  bool changeParameter(const std::string& name, const std::string& expr) override {
    pc::Document c = doc;
    if (!c.params.setExpression(name, expr)) return false;
    return commitDocument(c, QString("Parametre %1 = %2").arg(QString::fromStdString(name), QString::fromStdString(expr)));
  }
  bool deleteFeatureInteractive(const std::string& id) override {
    pc::Document c = doc;
    const int i = c.indexOf(id);
    if (i < 0) return false;
    c.features.erase(c.features.begin() + i);
    return commitDocument(c, QString("Sil %1").arg(QString::fromStdString(id)));
  }
  bool setFeatureSuppressed(const std::string& id, bool s) override {
    pc::Document c = doc;
    pc::Feature* f = c.feature(id);
    if (!f) return false;
    f->suppressed = s;
    return commitDocument(c, QString("Bastır %1").arg(QString::fromStdString(id)));
  }
  pc::model::ModelResult evaluateCandidate(const pc::Document& c) const override { return pc::model::regenerate(c); }
  bool evaluateExpression(const QString& e, double* v, QString* err) const override {
    try {
      const pc::Document* ctx = mode == DesignMode::Sketch ? &work : &doc;
      const double d = pc::evalWith(e.trimmed().toStdString(), ctx->evaluateParams());
      if (!std::isfinite(d)) return false;
      if (v) *v = d;
      return true;
    } catch (const std::exception& ex) {
      if (err) *err = ex.what();
      return false;
    }
  }
  QString createParameterInteractive(const QString& suggested, pc::ParamKind kind, const QString& value, QWidget* parent) override {
    NewParameterDialog dlg(expressionContext(), suggested, kind, value, parent ? parent : this);
    if (dlg.exec() != QDialog::Accepted) return QString();
    const pc::Parameter p = dlg.parameter();
    pc::Document c = doc;
    if (!c.params.find(p.name)) c.params.add(p);
    if (mode == DesignMode::Sketch && !work.params.find(p.name)) work.params.add(p);
    commitDocument(c, "Parametre ekle: " + QString::fromStdString(p.name));
    return QString::fromStdString(p.name);
  }
  void setGhost(const TopoDS_Shape& s) override { if (s.IsNull()) view->clearGhost(); else view->setGhost(s); }
  void showPrompt(const QString& t, int) override { lastMsg = t; statusBar()->showMessage(t); }
  void showQuickMeasure(const QString&) override {}
  void setToolEmphasis(const QStringList& f, const QStringList&) override { emphFaces = f; highlight(); }
  void showOperationFeedback(const pcd::OpStatus& st) override { lastOp = st; }   // [SC23]
  pcd::OpStatus lastOp;
  void openClassicEditor(const std::string&) override {}
  bool featureDialogOpen() const override { return false; }
  void refreshViewer(bool keep) override { updateViewer(keep); }

  // ---- test helpers
  QString msg() const { return statusBar()->currentMessage(); }
  QPointF sketchPx(double u, double v) const { return ed->sketchToScreen(u, v); }
  QPointF worldPx(const gp_Pnt& p) const { const double a[3] = {p.X(), p.Y(), p.Z()}; return view->worldToScreen(a); }
  QPointF facePx(const QString& name) const {
    double c[3], n[3];
    if (!view->faceCentroid(name, c, n)) return QPointF(-1, -1);
    return view->worldToScreen(c);
  }
  QPointF edgePx(const QString& name, double t = 0.5) const {
    auto it = model.edges.find(name.toStdString());
    if (it == model.edges.end()) return QPointF(-1, -1);
    BRepAdaptor_Curve c(it->second);
    return worldPx(c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter())));
  }
  const pc::Feature* lastFeature(pc::FeatureType t) const {
    for (auto it = doc.features.rbegin(); it != doc.features.rend(); ++it) if (it->type == t) return &*it;
    return nullptr;
  }
  int count(pc::FeatureType t) const { int n = 0; for (const auto& f : doc.features) if (f.type == t) ++n; return n; }
  double volume() const { return model.shape.IsNull() ? 0 : pc::model::massProperties(model.shape).volume; }
};

// ---- input helpers (real events)
QWidget* inputTarget(ScHost& h) { return h.mode == DesignMode::Sketch ? static_cast<QWidget*>(h.ed) : static_cast<QWidget*>(h.view); }
// Xvfb has no window manager: after a modal dialog the main window is not re-activated by itself, and window
// shortcuts only fire in the active window
void ensureActive(ScHost& h) {
  if (QApplication::activeWindow() != &h) { h.activateWindow(); QApplication::setActiveWindow(&h); pump(30); }
}
void key(ScHost& h, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
  ensureActive(h);
  QWidget* w = QApplication::focusWidget();
  if (!w || !h.isAncestorOf(w)) w = inputTarget(h);
  QTest::keyClick(w, Qt::Key(k), m);
  pump(30);
}
void type(ScHost& h, const QString& s) {
  ensureActive(h);
  for (const QChar c : s) {
    QWidget* w = QApplication::focusWidget();
    if (!w || !h.isAncestorOf(w)) w = inputTarget(h);
    QTest::keyClick(w, c.toLatin1());
    pump(15);
  }
}
void vclick(ScHost& h, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QWidget* w = inputTarget(h);
  QTest::mouseMove(w, p.toPoint());
  pump(20);
  QTest::mouseClick(w, Qt::LeftButton, m, p.toPoint());
  pump(60);
}
void vmove(ScHost& h, const QPointF& p) {
  QWidget* w = inputTarget(h);
  QMouseEvent mv(QEvent::MouseMove, p, w->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(w, &mv);
  pump(20);
}
void drag(QWidget* w, Qt::MouseButton b, Qt::KeyboardModifiers m, QPoint from, QPoint to, int steps = 8) {
  QTest::mousePress(w, b, m, from);
  for (int i = 1; i <= steps; ++i) {
    const QPoint p = from + (to - from) * i / steps;
    QMouseEvent mv(QEvent::MouseMove, QPointF(p), w->mapToGlobal(QPointF(p)), Qt::NoButton, b, m);
    QApplication::sendEvent(w, &mv);
    pump(12);
  }
  QTest::mouseRelease(w, b, m, to);
  pump(40);
}
void shot(ScHost& h, const QString& name) {
  if (!g_shots) return;
  pump(150);
  QDir().mkpath(PC_SC_SHOTS_DIR);
  const QString path = QString(PC_SC_SHOTS_DIR) + "/" + name;
  const QImage img = h.grab().toImage();
  const bool ok = img.save(path);
  note(QString("shot %1 %2").arg(path, ok ? "saved" : "FAILED"));
  // ACCEPTANCE.md "Çalıştırma" names for the same moments
  static const QMap<QString, QString> acc = {{"B_rect_dims.png", "ac04_rect_dims.png"}, {"B_pull_armed.png", "ac13_pull_armed.png"},
                                             {"B_pull_box.png", "ac14_box.png"}, {"B_revolve.png", "ac24_revolve.png"}};
  if (acc.contains(name)) img.save(QString(PC_SC_SHOTS_DIR) + "/" + acc.value(name));
}
int lengthDims(const pc::Sketch& sk, QStringList* exprs) {
  int n = 0;
  for (const auto& c : sk.constraints)
    if (c.type == pc::SkConstraintType::Length) { ++n; if (exprs) *exprs << QString::fromStdString(c.valueExpr); }
  return n;
}
int countType(const pc::Sketch& sk, pc::SkEntityType t, bool construction = false) {
  int n = 0;
  for (const auto& e : sk.entities) if (e.type == t && e.construction == construction) ++n;
  return n;
}
QString findEdge(const ScHost& h, std::function<bool(const QString&, const gp_Pnt&, const gp_Pnt&)> pred) {
  for (const auto& [n, e] : h.model.edges) {
    BRepAdaptor_Curve c(e);
    if (pred(QString::fromStdString(n), c.Value(c.FirstParameter()), c.Value(c.LastParameter()))) return QString::fromStdString(n);
  }
  return QString();
}
// an edge pick point that the viewer resolves to `edge` (tries several parameters along the edge)
QPointF edgePickPx(ScHost& h, const QString& edge) {
  for (double t : {0.5, 0.4, 0.6, 0.3, 0.7, 0.25, 0.75}) {
    const QPointF p = h.edgePx(edge, t);
    int kind = 0;
    if (h.view->pickAt(p.toPoint(), pc::viewer::ShapeViewer::PickFace | pc::viewer::ShapeViewer::PickEdge, &kind) == edge) return p;
  }
  return QPointF(-1, -1);
}
QPointF facePickPx(ScHost& h, const QString& face) {
  // the centroid may fall into a hole / pocket: spiral outwards until the viewer reports the face itself
  const QPointF c = h.facePx(face);
  if (c.x() < 0) return c;
  for (int r = 0; r <= 240; r += 8)
    for (int k = 0; k < (r == 0 ? 1 : 12); ++k) {
      const double a = k * M_PI / 6.0;
      const QPointF p = c + QPointF(std::cos(a), std::sin(a)) * r;
      if (h.view->pickAt(p.toPoint(), pc::viewer::ShapeViewer::PickFace | pc::viewer::ShapeViewer::PickEdge) == face) return p;
    }
  return QPointF(-1, -1);
}

// =============================================================================== unit-level checks
void unitChecks() {
  section("PullMapping / TrimLogic (pure logic, stub host)");
  pc::Document br;
  std::string err;
  check(pc::loadDocument(br, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err), "bracket.pcad loads");
  test::StubDesignHost stub(br);
  pull::Options opt;
  pull::Target t = pull::analyze(stub.document(), stub.modelResult(), {SelItem{SelItem::Kind::Face, "Hole2.h0.bottom"}}, opt);
  check(t.kind == pull::Kind::HoleDepth && t.refusal.contains("türetilmiş") && t.refusal.contains("Lbore = Hb + T - 4"),
        "Hole2 bottom depth bound to derived Lbore is refused: " + t.refusal);
  t = pull::analyze(stub.document(), stub.modelResult(), {SelItem{SelItem::Kind::Face, "Extrude1.end"}}, opt);
  check(t.kind == pull::Kind::ExtrudeEnd && t.boundParam == "T" && std::fabs(t.current - 8) < 1e-9, "Extrude1.end -> distance bound to T = 8");
  auto ev = [&](const QString& s, double* v) { return stub.evaluateExpression(s, v, nullptr); };
  pull::Result r = pull::apply(stub.document(), stub.modelResult(), t, "2", false, opt, ev);
  check(r.ok && r.paramName == "T" && r.paramExpr == "10", "pulling Extrude1.end by 2 writes T = 10 (" + r.status + ")");
  t = pull::analyze(stub.document(), stub.modelResult(), {SelItem{SelItem::Kind::Face, "Fillet1.f0"}}, opt);
  check(t.kind == pull::Kind::FilletRadius && t.boundParam == "R", "Fillet1.f0 -> radius bound to R");
  t = pull::analyze(stub.document(), stub.modelResult(), {SelItem{SelItem::Kind::Face, "Extrude1.side.e5"}}, opt);
  check(t.kind == pull::Kind::FaceExtrude && t.valid() && t.fieldLabel == "Mesafe",
        "a plain planar side face is pulled by extruding the face (PULL_FACE_FIX): " + t.refusal);
  // trim: two crossing lines, trim the right part of the horizontal one
  pc::Sketch sk = pc::Sketch::create();
  const int h = sk.addLine(-10, 0, 10, 0), v = sk.addLine(0, -10, 0, 10);
  (void)v;
  pc::Sketch out;
  QString msg;
  check(trimSketchEntity(sk, h, 6, 0, out, &msg) && out.entities.size() == 2, "trim: crossing lines -> right half removed (" + msg + ")");
  const pc::SkEntity* he = out.entity(h);
  bool okEnds = he && std::fabs(out.point(he->points[0])->x + 10) < 1e-9 && std::fabs(out.point(he->points[1])->x) < 1e-9;
  check(okEnds, "trim: the remaining piece is (-10,0)-(0,0)");
  pc::Sketch c2 = pc::Sketch::create();
  const int circ = c2.addCircle(0.0, 0.0, 10.0);
  c2.addLine(-20, 0, 20, 0);
  const bool trOk = trimSketchEntity(c2, circ, 0, 10, out, &msg);
  note(QString("circle trim: ok=%1 msg=%2 cuts=%3").arg(trOk).arg(msg).arg(trimCutParameters(c2, circ).size()));
  check(trOk && out.entity(circ) && out.entity(circ)->type == pc::SkEntityType::Arc,
        "trim: a circle cut twice by a line becomes the remaining arc");
}

// =============================================================================== session 1: rectangle -> pull -> params
void session1() {
  section("Session 1 — sketch with typed dimensions, Pull, groups (ACCEPTANCE 3-5, 9, 11-14, 16-17, 19, 21-26, 39, 43, 45, 46)");
  ScHost h;
  h.show();
  QTest::qWaitForWindowExposed(&h);
  h.activateWindow();
  QApplication::setActiveWindow(&h);
  QTest::qWaitForWindowActive(&h);
  pump(300);
  h.view->setFocus();
  key(h, Qt::Key_K);
  check(h.mode == DesignMode::Sketch && h.ed->isVisible() && h.ed->parentWidget() == h.view, "K: Sketch mode, the sketch editor is an overlay on the 3D view");
  check(h.tm->activeId() == "sketch.select", "Sketch mode starts with Seç (sketch.select)");

  // ---- AC4 / AC5: R, click origin, 60 Tab 40 Enter
  key(h, Qt::Key_R);
  check(h.tm->activeId() == "sketch.rect" && h.ed->tool() == SketchEditor::Tool::Rect, "R activates Dikdörtgen");
  vclick(h, h.sketchPx(0, 0));
  check(h.ed->drawStage() == 1 && h.ed->dimensionBox()->isVisible(), "first corner on the origin; live dimension fields shown");
  type(h, "60");
  check(h.ed->dimensionEditing(), "typing a digit opens the width field");
  key(h, Qt::Key_Tab);
  check(h.ed->dimensionBox()->isLocked(0) && h.ed->dimensionBox()->editingField() == 1, "Tab locks 60 and moves to the height field");
  vmove(h, h.sketchPx(30, 25));
  const QSizeF s1 = h.ed->previewSize();
  vmove(h, h.sketchPx(45, 55));
  const QSizeF s2 = h.ed->previewSize();
  check(std::fabs(s1.width() - 60) < 1e-6 && std::fabs(s2.width() - 60) < 1e-6 && std::fabs(s1.height() - s2.height()) > 5,
        QString("AC5: locked width stays 60 (%1, %2) while the height follows the cursor (%3 -> %4)")
            .arg(s1.width()).arg(s2.width()).arg(s1.height()).arg(s2.height()));
  type(h, "40");
  shot(h, "B_rect_dims.png");
  key(h, Qt::Key_Return);
  const pc::Sketch& sk = h.ed->sketch();
  QStringList lens;
  lengthDims(sk, &lens);
  lens.sort();
  check(countType(sk, pc::SkEntityType::Line) == 4, "AC4: rectangle = 4 lines");
  check(lens == QStringList({"40", "60"}), "AC4: Length constraints {60, 40}: " + lens.join(","));
  check(h.ed->dof() == 0, QString("AC4: DOF = %1 (corner on the origin, typed dims are driving)").arg(h.ed->dof()));

  // ---- AC9: Esc chain inside the sketch
  int lineId = -1;
  for (const auto& e : sk.entities) if (e.type == pc::SkEntityType::Line) { lineId = e.id; break; }
  h.ed->selectIds({lineId});
  vclick(h, h.sketchPx(80, 10));
  type(h, "5");
  check(h.ed->dimensionEditing() && h.ed->drawStage() == 1, "Esc chain set-up: drawing a rectangle with the field open");
  key(h, Qt::Key_Escape);
  check(!h.ed->dimensionEditing() && h.ed->drawStage() == 1, "AC9 Esc 1: closes the dimension field, drawing continues");
  key(h, Qt::Key_Escape);
  check(h.ed->drawStage() == 0 && h.tm->activeId() == "sketch.rect", "AC9 Esc 2: cancels the drawing stage");
  key(h, Qt::Key_Escape);
  check(h.tm->activeId() == "sketch.select", "AC9 Esc 3: back to Seç");
  check(h.ed->hasSelection(), "AC9: the selection survived so far");
  key(h, Qt::Key_Escape);
  check(!h.ed->hasSelection() && h.mode == DesignMode::Sketch, "AC9 Esc 4: selection cleared, still in Sketch mode");
  check(h.ed->sketch().entities.size() == 4, "AC9: nothing was created by the cancelled drawing");

  // ---- AC11: V = plan view
  drag(h.ed, Qt::MiddleButton, Qt::NoModifier, QPoint(700, 400), QPoint(760, 360));
  key(h, Qt::Key_V);
  const QTransform T = h.ed->planeToScreen();
  const double dot = T.m11() * T.m21() + T.m12() * T.m22();
  const double lu = std::hypot(T.m11(), T.m12()), lv = std::hypot(T.m21(), T.m22());
  check(std::fabs(dot) < 1e-6 * lu * lv && std::fabs(lu - lv) < 1e-6 * lu && T.m22() < 0,
        QString("AC11: V -> plan view (orthogonal %1, equal scale %2/%3, v up m22=%4)").arg(dot).arg(lu).arg(lv).arg(T.m22()));
  shot(h, "B_mini_sketch.png");
  check(h.findChild<QWidget*>("miniToolbar") && h.findChild<QWidget*>("miniToolbar")->isVisible() &&
            h.findChild<QToolButton*>("mini.to3d") && h.findChild<QToolButton*>("mini.planView") && h.findChild<QToolButton*>("mini.newPlane"),
        "sketch mini-toolbar: Seç / 3B'ye Dön / Yeni sketch düzlemi / Izgarayı taşı / Plan görünümü");

  // ---- AC12: D -> region in 3D
  const int undo0 = h.undoCount;
  key(h, Qt::Key_D);
  check(h.mode == DesignMode::ThreeD && h.doc.feature("Sketch1") && h.undoCount == undo0 + 1, "D: sketch committed (one undo step), 3D mode");
  h.view->setHomeView();
  pump(200);
  const QString reg = "sketch:Sketch1:r0";
  QPointF rp = h.facePx(reg);
  int kind = 0;
  check(h.view->pickAt(rp.toPoint(), 7, &kind) == reg, "AC12: region face sketch:Sketch1:r0 is drawn and pickable");

  // ---- AC13: P + region click
  key(h, Qt::Key_P);
  vclick(h, rp);
  auto* ov = h.view->findChild<ViewportOverlay*>("viewportOverlay");
  check(h.tm->activeId() == "pull", "AC13: P activates Çek");
  check(ov && ov->handleCount() == 1, "AC13: one pull arrow handle on the region");
  check(h.msg().startsWith("Çek"), "AC13: status prompt starts with 'Çek': " + h.msg());
  shot(h, "B_pull_armed.png");

  // ---- AC14: 8 Enter
  const int undo1 = h.undoCount;
  type(h, "8");
  auto* pt = qobject_cast<PullTool*>(h.tm->active());
  check(pt && pt->dimensionBox() && pt->dimensionBox()->isEditing(), "typing 8 opens the Pull dimension box");
  shot(h, "B_pull_box.png");
  key(h, Qt::Key_Return);
  const pc::Feature* ex = h.doc.feature("Extrude1");
  check(h.model.bodies.size() == 1 && ex && ex->prop("distance") == "8", "AC14: Extrude1 distance=8, one body");
  check(std::fabs(h.volume() - 19200) < 0.5, QString("AC14: volume %1 mm3 (60x40x8 = 19200)").arg(h.volume()));
  check(h.undoCount == undo1 + 1, "AC14: one undo step");
  check(h.sel.contains(SelItem{SelItem::Kind::Face, "Extrude1.end"}), "the result face Extrude1.end stays selected (pull again)");

  // ---- AC16: Ctrl+G after the pull -> parameter T (exposed), Extrude1.distance = T
  bool dlgSeen = false, suggested = false;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pcd::NewParameterDialog")) return;
    dlgSeen = true;
    auto* name = m->findChild<QLineEdit*>("npName");
    suggested = name && name->text() == "Extrude1_distance";
    pch::typeInto(name, "T", "npName");
    pch::tickCheckBox(m->findChild<QCheckBox*>("npExposed"), true, "npExposed");
    QTimer::singleShot(0, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  key(h, Qt::Key_G, Qt::ControlModifier);
  pump(400);
  g_onModal = nullptr;
  const pc::Parameter* T1 = h.doc.params.find("T");
  check(dlgSeen && suggested, "AC16: Ctrl+G opens NewParameterDialog with 'Extrude1_distance' suggested");
  check(T1 && T1->exposed && T1->expression == "8" && h.doc.feature("Extrude1") && h.doc.feature("Extrude1")->prop("distance") == "T",
        "AC16: T = 8 (exposed) and Extrude1.distance = T");

  // ---- AC17: top face + 2 -> T = 10
  h.sel.clear();
  pump(100);
  const QPointF top = facePickPx(h, "Extrude1.end");
  vclick(h, top);
  check(h.sel.contains(SelItem{SelItem::Kind::Face, "Extrude1.end"}), "clicked the top face Extrude1.end");
  type(h, "2");
  key(h, Qt::Key_Return);
  check(h.doc.params.find("T") && h.doc.params.find("T")->expression == "10", "AC17: T = 10");
  check(h.msg().contains("parametre güncellendi"), "AC17: status says 'parametre güncellendi': " + h.msg());
  check(std::fabs(h.volume() - 24000) < 0.5, QString("AC17: volume %1 (60x40x10)").arg(h.volume()));

  // ---- AC19: plain click on an edge + 3 -> Fillet r=3
  const QString topEdge = findEdge(h, [](const QString& n, const gp_Pnt& a, const gp_Pnt& b) {
    return n.contains("Extrude1.end") && std::fabs(a.Z() - 10) < 1e-6 && std::fabs(b.Z() - 10) < 1e-6 && std::fabs(a.Y()) < 1e-6 && std::fabs(b.Y()) < 1e-6;
  });
  h.sel.clear();
  const QPointF ep = edgePickPx(h, topEdge);
  check(ep.x() >= 0, "front top edge is pickable with a plain click: " + topEdge);
  vclick(h, ep);
  check(h.sel.count() == 1 && h.sel.items().first().kind == SelItem::Kind::Edge && h.sel.items().first().name == topEdge,
        "AC19: plain click selects the edge (edge within 6 px preferred over the face)");
  shot(h, "B_pull_edge.png");
  type(h, "3");
  key(h, Qt::Key_Return);
  const pc::Feature* fl = h.doc.feature("Fillet1");
  check(fl && fl->refs.size() == 1 && fl->prop("radius") == "3", "AC19: Fillet1 r=3 on one edge");

  // ---- AC21: fillet face + 5 -> radius 5, no new feature
  const size_t nfeat = h.doc.features.size();
  h.sel.clear();
  const QPointF ff = facePickPx(h, "Fillet1.f0");
  check(ff.x() >= 0, "fillet face Fillet1.f0 is pickable");
  vclick(h, ff);
  type(h, "5");
  key(h, Qt::Key_Return);
  check(h.doc.features.size() == nfeat && h.doc.feature("Fillet1") && h.doc.feature("Fillet1")->prop("radius") == "5", "AC21: Fillet1.radius = 5, no new feature");

  // ---- AC22: sketch on the top face, Ø10 circle, D, P, -5 -> cut pocket
  h.sel.clear();
  vclick(h, facePickPx(h, "Extrude1.end"));
  key(h, Qt::Key_K);
  check(h.mode == DesignMode::Sketch && h.ed->sketch().plane.kind == pc::SketchPlaneKind::OnFace && h.ed->sketch().plane.faceRef == "Extrude1.end",
        "AC22: K with the top face selected opens an OnFace sketch on Extrude1.end");
  key(h, Qt::Key_C);
  vclick(h, h.sketchPx(30, 20));
  type(h, "10");
  key(h, Qt::Key_Return);
  check(countType(h.ed->sketch(), pc::SkEntityType::Circle) == 1, "AC22: Ø10 circle drawn");
  key(h, Qt::Key_D);
  const double v0 = h.volume();
  key(h, Qt::Key_P);
  vclick(h, h.facePx("sketch:Sketch2:r0"));
  check(h.sel.contains(SelItem{SelItem::Kind::SketchRegion, "sketch:Sketch2:r0"}), "AC22: circle region selected");
  type(h, "-5");
  key(h, Qt::Key_Return);
  const pc::Feature* cut = h.lastFeature(pc::FeatureType::Extrude);
  check(cut && cut->prop("op") == "cut" && cut->prop("direction") == "reverse" && cut->prop("distance") == "5",
        "AC22: Auto merge, negative value -> op=cut, direction=reverse, distance 5");
  check(h.volume() < v0 - 300, QString("AC22: volume decreased %1 -> %2").arg(v0).arg(h.volume()));

  // ---- AC23: No Merge -> second body
  h.sel.clear();
  key(h, Qt::Key_K);   // nothing selected -> XY (test host)
  key(h, Qt::Key_R);
  vclick(h, h.sketchPx(100, 0));
  type(h, "20");
  key(h, Qt::Key_Tab);
  type(h, "20");
  key(h, Qt::Key_Return);
  key(h, Qt::Key_D);
  key(h, Qt::Key_P);
  h.tm->setOption("merge", "nomerge");
  vclick(h, h.facePx("sketch:Sketch3:r0"));
  type(h, "5");
  key(h, Qt::Key_Return);
  const pc::Feature* nm = h.lastFeature(pc::FeatureType::Extrude);
  check(nm && nm->prop("op") == "new" && h.model.bodies.size() == 2, QString("AC23: No Merge -> op=new, %1 bodies").arg(h.model.bodies.size()));
  h.tm->setOption("merge", "auto");

  // ---- AC24: revolve about a construction line (XZ sketch), Alt+click
  h.sel.clear();
  pc::SketchPlane xz;
  xz.kind = pc::SketchPlaneKind::XZ;
  h.beginSketch(xz);
  pump(150);
  key(h, Qt::Key_R);
  vclick(h, h.sketchPx(200, 0));
  type(h, "20");
  key(h, Qt::Key_Tab);
  type(h, "20");
  key(h, Qt::Key_Return);
  key(h, Qt::Key_L);
  h.tm->setOption("construction", true);
  h.tm->setOption("snapGrid", true);
  vclick(h, h.sketchPx(185, -10));
  vclick(h, h.sketchPx(185, 30));
  key(h, Qt::Key_Escape);
  h.tm->setOption("construction", false);
  int axisId = -1;
  for (const auto& e : h.ed->sketch().entities) if (e.construction && e.type == pc::SkEntityType::Line) axisId = e.id;
  check(axisId > 0, "AC24: construction line drawn (Yapı çizgisi option)");
  key(h, Qt::Key_D);
  key(h, Qt::Key_P);
  h.view->setView(pc::viewer::ShapeViewer::View::Front);
  pump(200);
  vclick(h, h.facePx("sketch:Sketch4:r0"));
  const QString axisName = sketchCurveName("Sketch4", axisId);
  QPointF ap = h.worldPx(gp_Pnt(185, 0, 25));
  check(h.view->pickAt(ap.toPoint(), 7) == axisName, "the construction line is pickable in 3D: " + axisName);
  vclick(h, ap, Qt::AltModifier);
  auto* gbar = h.findChild<ToolGuideBar*>("toolGuideBar");
  check(gbar && gbar->checkedKey() == "revolve", "AC24: Alt+click on the line -> 'Döndür' guide active");
  check(pt && ov && pt->target().kind == pull::Kind::NewRevolve && ov->handleCount() == 1, "AC24: revolve handle shown");
  shot(h, "B_revolve.png");
  type(h, "360");
  key(h, Qt::Key_Return);
  const pc::Feature* rv = h.lastFeature(pc::FeatureType::Revolve);
  check(rv && rv->prop("axis") == std::to_string(axisId) && rv->prop("angle") == "360", "AC24: Revolve axis = construction line id, angle 360");
  h.view->setHomeView();
  pump(150);

  // ---- AC25: Tab cycles the tool guides
  h.sel.clear();
  QStringList seen;
  seen << gbar->checkedKey();
  for (int i = 0; i < 5; ++i) { key(h, Qt::Key_Tab); seen << gbar->checkedKey(); }   // SC-PULLUPTO: Kadar, Cetvel enabled
  check(seen == QStringList({"select", "direction", "revolve", "upto", "ruler", "select"}),
        "AC25: Tab cycles Seç → Yön → Döndür → Kadar → Cetvel → Seç: " + seen.join(" → "));
  shot(h, "B_guides.png");

  // ---- AC26: navigation
  auto cam = [&] { return std::make_tuple(h.view->property("cameraAz").toDouble(), h.view->property("halfH").toDouble(),
                                          h.view->property("target").value<QVector3D>()); };
  QSignalSpy camSpy(h.view, &pc::viewer::ShapeViewer::cameraChanged);
  auto c0 = cam();
  drag(h.view, Qt::MiddleButton, Qt::NoModifier, QPoint(700, 450), QPoint(780, 430));
  auto c1 = cam();
  check(std::fabs(std::get<0>(c1) - std::get<0>(c0)) > 5 && camSpy.count() > 0, "AC26: middle drag spins (cameraAz changed, cameraChanged emitted)");
  drag(h.view, Qt::MiddleButton, Qt::ShiftModifier, QPoint(700, 450), QPoint(760, 470));
  auto c2 = cam();
  check((std::get<2>(c2) - std::get<2>(c1)).length() > 1e-3 && std::fabs(std::get<0>(c2) - std::get<0>(c1)) < 1e-6, "AC26: Shift+middle pans (target moved, no spin)");
  drag(h.view, Qt::MiddleButton, Qt::ControlModifier, QPoint(700, 450), QPoint(700, 400));
  auto c3 = cam();
  check(std::get<1>(c3) < std::get<1>(c2) * 0.9, "AC26: Ctrl+middle up zooms in (halfH smaller)");
  QWheelEvent we(QPointF(700, 450), h.view->mapToGlobal(QPointF(700, 450)), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(h.view, &we);
  pump(50);
  auto c4 = cam();
  check(std::get<1>(c4) > std::get<1>(c3) * 1.1, "AC26: wheel zooms (halfH changed)");
  drag(h.view, Qt::LeftButton, Qt::NoModifier, QPoint(1250, 120), QPoint(1150, 200));
  auto c5 = cam();
  check(std::get<0>(c5) == std::get<0>(c4) && std::get<1>(c5) == std::get<1>(c4) && std::get<2>(c5) == std::get<2>(c4),
        "AC26: left drag on empty space does not move the camera");
  check(h.view->previousView(), "camera history: previous view available");
  h.view->setHomeView();
  pump(150);

  // ---- AC39: Edge filter -> clicking a face selects nothing
  key(h, Qt::Key_S);
  h.sel.clear();
  h.sel.setFilter(SelFilter::Edge);
  vclick(h, facePickPx(h, "Extrude1.end"));
  check(h.sel.isEmpty(), "AC39: filter 'Kenar' — a face click selects nothing");
  h.sel.setFilter(SelFilter::Smart);

  // ---- AC45: Fill on the fillet face
  key(h, Qt::Key_Escape);
  key(h, Qt::Key_F);
  check(h.tm->activeId() == "fill", "F activates Doldur");
  h.sel.clear();
  vclick(h, facePickPx(h, "Fillet1.f0"));
  key(h, Qt::Key_Return);
  check(!h.doc.feature("Fillet1"), "AC45: Fill on the single-edge fillet deletes Fillet1");

  // ---- AC46: Combine: target, cutter, mini-toolbar "Çıkar"
  key(h, Qt::Key_I);
  check(h.tm->activeId() == "combine", "I activates Birleştir");
  QString bodyB;
  for (const auto& b : h.model.bodies) {
    if (b.name == h.model.bodyOfFace("Extrude1.end")) continue;
    for (const auto& f : b.faces) if (QString::fromStdString(f).endsWith(".end") && bodyB.isEmpty()) bodyB = QString::fromStdString(b.name);
  }
  vclick(h, facePickPx(h, "Extrude1.end"));
  QString faceB;
  if (const auto* bb = h.model.body(bodyB.toStdString()))
    for (const auto& f : bb->faces) if (QString::fromStdString(f).endsWith(".end")) faceB = QString::fromStdString(f);
  vclick(h, facePickPx(h, faceB));
  shot(h, "B_combine.png");
  auto* cutBtn = h.findChild<QToolButton*>("mini.doCut");
  check(cutBtn && cutBtn->isVisible() && cutBtn->isEnabled(), "AC46: mini-toolbar offers Çıkar");
  if (cutBtn) QTest::mouseClick(cutBtn, Qt::LeftButton);
  pump(200);
  const pc::Feature* cb = h.lastFeature(pc::FeatureType::Combine);
  check(cb && cb->prop("op") == "cut" && !cb->prop("target").empty() && cb->prop("tools") == bodyB.toStdString(),
        "AC46: Combine op=cut target=" + QString::fromStdString(cb ? cb->prop("target") : "") + " tools=" + bodyB);
  // ---- AC43: Plane tool from a face, then Move 15 -> offset 15
  h.sel.clear();
  QAction* planeAct = h.tm->action("plane");
  planeAct->trigger();
  vclick(h, facePickPx(h, "Extrude1.end"));
  pump(100);
  const pc::Feature* pl = h.lastFeature(pc::FeatureType::Plane);
  check(pl && pl->prop("kind") == "faceOffset" && h.tm->activeId() == "move", "Düzlem: face click -> faceOffset plane, tool switches to Taşı");
  type(h, "15");
  key(h, Qt::Key_Return);
  pl = h.lastFeature(pc::FeatureType::Plane);
  check(pl && pl->prop("offset") == "15", "AC43: Move 15 -> Plane offset = 15");
  shot(h, "B_move_plane.png");

}

// =============================================================================== session 2: circle, oblique drawing, trim
void session2() {
  section("Session 2 — circle, drawing after spinning the view, trim (ACCEPTANCE 6, 10, 48)");
  ScHost h;
  h.show();
  QTest::qWaitForWindowExposed(&h);
  h.activateWindow();
  QApplication::setActiveWindow(&h);
  QTest::qWaitForWindowActive(&h);
  pump(250);
  h.view->setFocus();
  key(h, Qt::Key_K);
  key(h, Qt::Key_C);
  vclick(h, h.sketchPx(100, 0));
  type(h, "20");
  shot(h, "B_circle_dim.png");
  key(h, Qt::Key_Return);
  const pc::Sketch& sk = h.ed->sketch();
  bool dia = false;
  for (const auto& c : sk.constraints) if (c.type == pc::SkConstraintType::Diameter && c.valueExpr == "20") dia = true;
  const pc::SkEntity* circ = nullptr;
  for (const auto& e : sk.entities) if (e.type == pc::SkEntityType::Circle) circ = &e;
  check(circ && dia && std::fabs(circ->radius - 10) < 1e-6, "AC6: Ø20 circle with Diameter=20 (radius 10)");

  // AC10: spin while sketching, keep drawing on the plane
  QSignalSpy spy(h.view, &pc::viewer::ShapeViewer::cameraChanged);
  drag(h.ed, Qt::MiddleButton, Qt::NoModifier, QPoint(600, 500), QPoint(680, 430));
  check(spy.count() > 0, "AC10: middle drag in the sketch overlay rotates the model (cameraChanged)");
  const QTransform T = h.ed->planeToScreen();
  check(std::fabs(T.m11() * T.m21() + T.m12() * T.m22()) > 1e-3 * std::hypot(T.m11(), T.m12()) * std::hypot(T.m21(), T.m22()),
        "the sketch plane is now seen obliquely (non-orthogonal plane->screen map)");
  shot(h, "B_sketch_oblique.png");
  key(h, Qt::Key_L);
  h.tm->setOption("snapGrid", false);
  h.tm->setOption("autoHV", false);
  const QPoint p1(520, 560), p2(640, 520);
  const size_t n0 = h.ed->sketch().points.size();
  vclick(h, p1);
  vclick(h, p2);
  key(h, Qt::Key_Escape);
  const pc::Sketch& s2 = h.ed->sketch();
  bool okRay = s2.points.size() >= n0 + 2;
  double worst = 0;
  for (int k = 0; k < 2 && okRay; ++k) {
    const QPoint px = k == 0 ? p1 : p2;
    double o[3], d[3];
    h.view->screenRay(px, o, d);
    const double t = -o[2] / d[2];   // plane z = 0
    const double wx = o[0] + t * d[0], wy = o[1] + t * d[1];
    const pc::SkPoint& sp = s2.points[n0 + size_t(k)];
    worst = std::max(worst, std::hypot(sp.x - wx, sp.y - wy));
  }
  check(okRay && worst < 1e-3, QString("AC10: points drawn in the oblique view lie on screenRay ∩ plane (max error %1 mm)").arg(worst));
  h.tm->setOption("snapGrid", true);
  h.tm->setOption("autoHV", true);

  // AC48: trim (two crossing lines, click the right part of the horizontal one)
  key(h, Qt::Key_V);
  key(h, Qt::Key_L);
  vclick(h, h.sketchPx(-20, 40));
  vclick(h, h.sketchPx(20, 40));
  key(h, Qt::Key_Escape);
  vclick(h, h.sketchPx(0, 20));
  vclick(h, h.sketchPx(0, 60));
  key(h, Qt::Key_Escape);
  const size_t ne = h.ed->sketch().entities.size();
  key(h, Qt::Key_T);
  check(h.tm->activeId() == "sketch.trim", "T activates Kırp in Sketch mode");
  vclick(h, h.sketchPx(12, 40));
  const pc::Sketch& s3 = h.ed->sketch();
  bool trimmed = false;
  for (const auto& e : s3.entities) {
    if (e.type != pc::SkEntityType::Line) continue;
    const pc::SkPoint *a = s3.point(e.points[0]), *b = s3.point(e.points[1]);
    if (a && b && std::fabs(a->y - 40) < 1e-6 && std::fabs(b->y - 40) < 1e-6 && std::max(a->x, b->x) < 1e-6 && std::min(a->x, b->x) < -19) trimmed = true;
  }
  check(s3.entities.size() == ne && trimmed, "AC48: Kırp removed the piece right of the crossing (line now -20..0)");
  key(h, Qt::Key_Escape);
}

// =============================================================================== session 3: bracket (W/2, Ctrl+G in the box, derived refusal)
void session3() {
  section("Session 3 — bracket.pcad: expression fields, Ctrl+G in the dimension box, derived binding (ACCEPTANCE 7, 8, 18)");
  pc::Document br;
  pc::loadDocument(br, std::string(PC_SAMPLES_DIR) + "/bracket.pcad");
  ScHost h(br);
  h.show();
  QTest::qWaitForWindowExposed(&h);
  h.activateWindow();
  QApplication::setActiveWindow(&h);
  QTest::qWaitForWindowActive(&h);
  pump(300);
  h.view->setFocus();
  // AC18 first (3D): the blind bore bottom Hole2.h0.bottom is driven by Lbore = Hb + T - 4
  key(h, Qt::Key_P);
  h.view->setView(pc::viewer::ShapeViewer::View::Top);
  pump(200);
  const QString bottom = "Hole2.h0.bottom";
  const QPointF bp = facePickPx(h, bottom);
  const std::string before = pc::toJson(h.doc).dump();
  if (bp.x() >= 0) vclick(h, bp);
  else { note("Hole2.h0.bottom not pickable from the top view: selected programmatically"); h.sel.set({SelItem{SelItem::Kind::Face, bottom}}); }
  type(h, "3");
  key(h, Qt::Key_Return);
  check(pc::toJson(h.doc).dump() == before, "AC18: document unchanged");
  check(h.msg().contains("türetilmiş"), "AC18: message explains the derived binding: " + h.msg());
  key(h, Qt::Key_Escape);
  key(h, Qt::Key_Escape);
  h.sel.clear();
  h.view->setHomeView();

  // AC7: L, Space, W/2 -> Length valueExpr W/2 ; invalid "W/" refused
  pc::SketchPlane xy;
  h.beginSketch(xy);
  pump(150);
  key(h, Qt::Key_L);
  vclick(h, h.sketchPx(0, -30));
  key(h, Qt::Key_Space);
  auto* box = h.ed->dimensionBox();
  check(box->isEditing(), "AC7: Space opens the length field");
  QLineEdit* le = box->lineEdit(0);
  const bool hasW = le && le->completer() && le->completer()->model()->match(le->completer()->model()->index(0, 0), Qt::DisplayRole, "W", 1, Qt::MatchExactly).size() == 1;
  check(hasW, "AC7: the field's completer offers the parameter W");
  type(h, "W/");
  const size_t nBefore = h.ed->sketch().entities.size();
  key(h, Qt::Key_Return);
  check(h.ed->sketch().entities.size() == nBefore && box->isEditing(), "AC7: invalid 'W/' -> Enter refused, nothing created");
  type(h, "2");
  key(h, Qt::Key_Return);
  bool w2 = false;
  for (const auto& c : h.ed->sketch().constraints) if (c.type == pc::SkConstraintType::Length && c.valueExpr == "W/2") w2 = true;
  check(w2, "AC7: Length constraint valueExpr == 'W/2'");
  key(h, Qt::Key_Escape);

  // AC8: R + 30 + Ctrl+G -> NewParameterDialog, name "Gw", exposed -> parameter in the document, constraint bound
  key(h, Qt::Key_R);
  vclick(h, h.sketchPx(0, 0));
  type(h, "30");
  bool seen = false;
  QString suggested;
  g_onModal = [&](QWidget* m) {
    if (!m->inherits("pcd::NewParameterDialog")) return;
    seen = true;
    auto* nm = m->findChild<QLineEdit*>("npName");
    suggested = nm ? nm->text() : QString();
    pch::typeInto(nm, "Gw", "npName");
    pch::tickCheckBox(m->findChild<QCheckBox*>("npExposed"), true, "npExposed");
    QTimer::singleShot(0, m, [m] { QMetaObject::invokeMethod(m, "accept"); });
  };
  key(h, Qt::Key_G, Qt::ControlModifier);
  pump(400);
  g_onModal = nullptr;
  check(seen && suggested.startsWith(QString::fromStdString(h.sketchId) + "_W"), "AC8: Ctrl+G in the field opens NewParameterDialog (suggested " + suggested + ")");
  const pc::Parameter* gw = h.doc.params.find("Gw");
  check(gw && gw->exposed && gw->expression == "30", "AC8: parameter Gw = 30 (exposed) committed to the document (Gruplar)");
  key(h, Qt::Key_Tab);
  type(h, "15");
  key(h, Qt::Key_Return);
  bool bound = false;
  for (const auto& c : h.ed->sketch().constraints) if (c.type == pc::SkConstraintType::Length && c.valueExpr == "Gw") bound = true;
  check(bound, "AC8: the width constraint is bound to Gw");
  // the earlier fixed bug: parameters created while sketching must survive closing the sketch
  key(h, Qt::Key_D);
  const pc::Feature* sk = h.lastFeature(pc::FeatureType::Sketch);
  bool kept = false;
  if (sk && sk->sketch) for (const auto& c : sk->sketch->constraints) if (c.valueExpr == "Gw") kept = true;
  check(h.doc.params.find("Gw") && kept, "params created in the sketch survive 'Sketch'i Bitir' (constraint still = Gw)");
}

// =============================================================================== session 4: drag + chamfer
void session4() {
  section("Session 4 — dragging the arrow (ghost, auto-commit) and chamfer on 4 edges (ACCEPTANCE 15, 20)");
  ScHost h;
  h.show();
  QTest::qWaitForWindowExposed(&h);
  h.activateWindow();
  QApplication::setActiveWindow(&h);
  QTest::qWaitForWindowActive(&h);
  pump(250);
  h.view->setFocus();
  key(h, Qt::Key_K);
  key(h, Qt::Key_R);
  vclick(h, h.sketchPx(0, 0));
  type(h, "50");
  key(h, Qt::Key_Tab);
  type(h, "30");
  key(h, Qt::Key_Return);
  key(h, Qt::Key_D);
  h.view->setHomeView();
  pump(200);
  key(h, Qt::Key_P);
  vclick(h, h.facePx("sketch:Sketch1:r0"));
  auto* pt = qobject_cast<PullTool*>(h.tm->active());
  if (!pt) { check(false, "Pull active for the drag test"); return; }
  const QPointF b = pt->handleBase(), t = pt->handleTip();
  QPointF u = t - b;
  u /= std::hypot(u.x(), u.y());
  const QPoint from = (b + u * 40).toPoint(), to = (b + u * 140).toPoint();
  QTest::mousePress(h.view, Qt::LeftButton, Qt::NoModifier, from);
  bool ghostDuring = false;
  for (int i = 1; i <= 10; ++i) {
    const QPoint p = from + (to - from) * i / 10;
    QMouseEvent mv(QEvent::MouseMove, QPointF(p), h.view->mapToGlobal(QPointF(p)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(h.view, &mv);
    pump(40);
    if (h.view->property("ghostVisible").toBool()) ghostDuring = true;
  }
  shot(h, "B_pull_drag.png");
  QTest::mouseRelease(h.view, Qt::LeftButton, Qt::NoModifier, to);
  check(ghostDuring, "AC15: ghost preview visible while dragging the arrow");
  check(!h.doc.feature("Extrude1"), "AC15: nothing committed at release (600 ms window)");
  pump(1000);
  const pc::Feature* ex = h.doc.feature("Extrude1");
  double dist = 0;
  if (ex) dist = std::atof(ex->prop("distance").c_str());
  check(ex && dist > 0, QString("AC15: Extrude1 committed ~600 ms after release, distance %1 > 0").arg(dist));
  check(!h.view->property("ghostVisible").toBool(), "ghost cleared after the commit");

  // AC20: 4 vertical edges, Pah, 1
  auto vertical = [&](const QString& n, const gp_Pnt& a, const gp_Pnt& b2) {
    return n.contains(".side.") && !n.contains(".end") && !n.contains(".start") && std::fabs(a.X() - b2.X()) < 1e-6 && std::fabs(a.Y() - b2.Y()) < 1e-6;
  };
  QStringList vert;
  for (const auto& [n, e] : h.model.edges) {
    BRepAdaptor_Curve c(e);
    if (vertical(QString::fromStdString(n), c.Value(c.FirstParameter()), c.Value(c.LastParameter()))) vert << QString::fromStdString(n);
  }
  check(vert.size() == 4, QString("4 vertical edges found (%1)").arg(vert.size()));
  h.sel.clear();
  int clicked = 0;
  for (const QString& e : vert) {
    const QPointF p = edgePickPx(h, e);
    if (p.x() < 0) { h.sel.add(SelItem{SelItem::Kind::Edge, e}); note("hidden vertical edge added to the selection programmatically: " + e); continue; }
    vclick(h, p, Qt::ControlModifier);
    ++clicked;
  }
  check(clicked >= 3 && h.sel.count() == 4, QString("Ctrl+click selected %1 visible vertical edges (4 in the selection)").arg(clicked));
  h.tm->setOption("edge", "chamfer");
  type(h, "1");
  key(h, Qt::Key_Return);
  const pc::Feature* ch = h.lastFeature(pc::FeatureType::Chamfer);
  check(ch && ch->refs.size() == 4 && ch->prop("distance") == "1", "AC20: one Chamfer, 4 edges, distance 1");
  h.tm->setOption("edge", "round");
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);  // Windows: native file dialogs cannot be driven by the harness
  QApplication app(argc, argv);
  for (int i = 1; i < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = true;
  static_assert(pcd::kToolApiVersionMajor == 1, "SC-UX ToolApi major version");
  pch::checkFn() = [](bool ok, const QString& s) { check(ok, s); };
  pch::startWatchdog([] { return g_fail; });
  startModalPump();
  {
    test::StubDesignHost host;
    QObject owner;
    ToolManager tm(&host, &owner);
    check(tm.action("select") && tm.activeId() == "select" && tm.action("pull")->text() == "Çek" && tm.sketchActions().size() == 7 &&
              tm.constraintActions().size() == 11 && tm.dimensionActions().size() == 7,
          "ToolManager: actions with SPEC texts (stub host)");
    check(tm.activate("pull") && tm.activeId() == "pull" && tm.active() && tm.active()->title() == "Çek (Pull)", "ToolManager: activate(pull) without a viewer");
    check(tm.cancel() && tm.activeId() == "select", "ToolManager: Esc from an idle Pull returns to Seç");
  }
  unitChecks();
  session1();
  session2();
  session3();
  session4();
  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
