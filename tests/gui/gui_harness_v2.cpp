// pc_gui_harness_v2 — drives the v2 designer workflow through real widgets / input events:
//   pick edge (Ctrl+click) -> context menu "Bu kenara dik düzlem" -> click the datum plane -> "Bu düzlemde
//   sketch başlat" -> draw a rectangle + dimensions -> Extrude as "Ayrı gövde" -> body shows in Gövdeler ->
//   second sketch -> Extrude as "Çıkar" with a target body; plus plane dialog, bodies panel, issues column.
//   pc_gui_harness_v2 [--shots DIR]      Exit code = number of failed checks.
#include "MainWindow.h"
#include "pc/Model.h"
#include <BRepAdaptor_Curve.hxx>
#include <GeomAbs_CurveType.hxx>
#include <QAction>
#include <QContextMenuEvent>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QStatusBar>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QAbstractItemView>
#include <QSet>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {

int g_fail = 0;
QString g_shots;

void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  if (!ok) ++g_fail;
}
void saveShot(const QImage& img, const QString& name) {
  if (g_shots.isEmpty()) return;
  QDir().mkpath(g_shots);
  img.save(g_shots + "/" + name);
}
void pump(int ms) { QTest::qWait(ms); }
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (!pred()) {
    if (t.elapsed() > timeoutMs) return false;
    QApplication::processEvents(QEventLoop::AllEvents, 30);
    QTest::qWait(20);
  }
  return true;
}

// modal pump (DimensionDialog / message boxes run in nested loops)
std::function<void(QWidget*)> g_onModal;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    static bool busy = false;
    if (busy || !g_onModal) return;
    QWidget* w = QApplication::activeModalWidget();
    if (!w || !w->isVisible() || w->property("handled").toBool()) return;
    busy = true;
    w->setProperty("handled", true);
    g_onModal(w);
    busy = false;
  });
  t->start();
}

void click(QWidget* w, QPoint p, Qt::KeyboardModifiers m = {}) {
  QTest::mouseMove(w, p);
  QTest::mouseClick(w, Qt::LeftButton, m, p);
}

// SC-UX (WP-B, SPEC §7.4 / §10.3): with the SpaceClaim navigation scheme a PLAIN click picks the edge (edgePicked,
// selectedEdge()) and the edge menu opens on a RIGHT click (customContextMenuRequested); the legacy scheme keeps
// Ctrl+click -> edge menu. These helpers make the harness valid before and after WP-A switches the designer over.
bool scScheme(pc::viewer::ShapeViewer* v) { return v->navigationScheme() == pc::viewer::ShapeViewer::NavigationScheme::SpaceClaim; }
void edgeClick(pc::viewer::ShapeViewer* v, QPoint p) { click(v, p, scScheme(v) ? Qt::NoModifier : Qt::ControlModifier); }
void edgeMenu(pc::viewer::ShapeViewer* v, QPoint p) {
  edgeClick(v, p);
  if (!scScheme(v)) return;
  QContextMenuEvent ce(QContextMenuEvent::Mouse, p, v->mapToGlobal(p));
  QApplication::sendEvent(v, &ce);
}

QAction* findAction(QWidget* root, const QString& text) {
  for (QAction* a : root->findChildren<QAction*>())
    if (a->text() == text || a->text().remove('&') == text) return a;
  return nullptr;
}
// Active popup menu (non-blocking context menu) with the given object name
QMenu* popupMenu(const char* name) {
  for (QWidget* w : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(w))
      if (m->isVisible() && m->objectName() == name) return m;
  return nullptr;
}
// click a menu entry with a real mouse click on the popup
bool clickMenuAction(QMenu* m, const QString& text) {
  if (!m) return false;
  for (QAction* a : m->actions()) {
    if (a->text() == text) {
      const QRect r = m->actionGeometry(a);
      QTest::mouseClick(m, Qt::LeftButton, {}, r.center());
      return true;
    }
  }
  return false;
}

bool isLineEdge(const pc::model::ModelResult& r, const QString& name) {
  auto it = r.edges.find(name.toStdString());
  if (it == r.edges.end()) return false;
  try { return BRepAdaptor_Curve(it->second).GetType() == GeomAbs_Line; } catch (...) { return false; }
}

// Find a widget position where the viewer reports a straight edge; returns "" if none.
QString findEdgePos(pcd::MainWindow& w, QPoint& pos, const QString& avoid = QString()) {
  auto* v = w.viewer();
  for (int y = 40; y < v->height() - 40; y += 7)
    for (int x = 40; x < v->width() - 40; x += 7) {
      const QString e = v->edgeAt(QPoint(x, y));
      if (e.isEmpty() || e == avoid || !isLineEdge(w.modelResult(), e)) continue;
      // SpaceClaim scheme: a plain click picks the edge only within 6 px (occlusion-checked), edgeAt uses 9 px
      if (scScheme(v) && (v->pickAt(QPoint(x, y)) != e || !e.startsWith("edge(") || !e.contains(QChar(0x7c)))) continue;
      pos = QPoint(x, y);
      return e;
    }
  return QString();
}
// Find a position where the datum plane is on top.
bool findDatumPos(pcd::MainWindow& w, const QString& plane, QPoint& pos) {
  auto* v = w.viewer();
  for (int y = 30; y < v->height() - 30; y += 9)
    for (int x = 30; x < v->width() - 30; x += 9)
      if (v->datumAt(QPoint(x, y)) == plane) { pos = QPoint(x, y); return true; }
  return false;
}

int countFeatures(const pc::Document& d, pc::FeatureType t) {
  int n = 0;
  for (const auto& f : d.features) if (f.type == t) ++n;
  return n;
}
double bodyVolume(pcd::MainWindow& w, const QString& body) {
  const auto* b = w.modelResult().body(body.toStdString());
  return b ? pc::model::massProperties(b->shape).volume : -1;
}
QStringList bodiesPanelNames(pcd::MainWindow& w) {
  QStringList l;
  for (int i = 0; i < w.bodiesTree()->topLevelItemCount(); ++i) l << w.bodiesTree()->topLevelItem(i)->data(0, Qt::UserRole).toString();
  return l;
}

// Draw an axis-aligned rectangle centred on the sketch origin using the Rect tool and two dimensions.
void drawRectWithDimensions(pcd::MainWindow& w, int halfWpx, int halfHpx, const QString& lenText, const QString& heightText) {
  auto* ed = w.sketchEditor();
  ed->setTool(pcd::SketchEditor::Tool::Rect);
  const QPoint c(ed->width() / 2, ed->height() / 2);
  // snap to grid/origin is handled by the editor; click opposite corners
  click(ed, c + QPoint(-halfWpx, -halfHpx));
  click(ed, c + QPoint(halfWpx, halfHpx));
  check(ed->sketch().entities.size() == 4, QString("rectangle drawn with the Rect tool (%1 entities)").arg(ed->sketch().entities.size()));
  ed->setTool(pcd::SketchEditor::Tool::Select);
  const int dofBefore = ed->dof();
  // dimension handler: type the value in the DimensionDialog and press "Tamam"
  QString nextValue;
  g_onModal = [&](QWidget* dlg) {
    auto* ee = dlg->findChild<pcd::ExprEdit*>();
    if (auto* le = ee ? ee->lineEdit() : nullptr) {
      le->setFocus();
      le->selectAll();
      const QString before = le->text();
      QTest::keyClicks(le, nextValue);
      std::printf("  [dim dialog %s] '%s' -> '%s' (typed '%s')\n", dlg->metaObject()->className(), qPrintable(before), qPrintable(le->text()), qPrintable(nextValue));
    }
    QTimer::singleShot(80, dlg, [dlg] {
      for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Tamam") { QTest::mouseClick(b, Qt::LeftButton); return; }
    });
  };
  // pick the bottom edge, apply "Uzunluk"; then the right edge
  const auto& sk = ed->sketch();
  auto lineMid = [&](int idx) {
    const auto* e = &sk.entities[size_t(idx)];
    const QPointF a = ed->pointScreenPos(e->points[0]), b = ed->pointScreenPos(e->points[1]);
    return ((a + b) / 2.0).toPoint();
  };
  // entities: bottom, right, top, left (rectangle contract)
  click(ed, lineMid(0));
  nextValue = lenText;
  if (auto* a = findAction(&w, "Uzunluk")) a->trigger();
  pump(150);
  click(ed, lineMid(1));
  nextValue = heightText;
  if (auto* a = findAction(&w, "Uzunluk")) a->trigger();
  pump(150);
  g_onModal = nullptr;
  int dims = 0;
  for (const auto& k : sk.constraints) if (pc::isDimensional(k.type)) ++dims;
  check(dims == 2, QString("two Length dimensions applied (%1)").arg(dims));
  check(ed->dof() <= dofBefore - 2, QString("DOF dropped by the dimensions (%1 -> %2)").arg(dofBefore).arg(ed->dof()));
}

// Configure + accept a feature dialog: returns true when it committed.
struct ExtrudeSetup {
  QString distance = "6";
  QString opData = "new";       // new | join | cut | intersect
  QString bodyName;
  QStringList targets;          // body names to check
  bool keepTool = false;
  QString direction;            // normal | reverse | symmetric
};
bool runExtrudeDialog(pcd::MainWindow& w, const ExtrudeSetup& s, QString* log = nullptr) {
  w.createFeature(pc::FeatureType::Extrude);
  if (!waitFor([&] { return w.featureDialog() != nullptr; }, 3000)) return false;
  auto* dlg = w.featureDialog();
  pump(100);
  // distance = first ExprEdit
  auto exprs = dlg->findChildren<pcd::ExprEdit*>();
  if (exprs.isEmpty()) return false;
  exprs.first()->lineEdit()->selectAll();
  QTest::keyClicks(exprs.first()->lineEdit(), s.distance);
  QComboBox* op = nullptr;
  QComboBox* dir = nullptr;
  for (auto* c : dlg->findChildren<QComboBox*>()) {
    if (c->findData("join") >= 0 && c->findData("new") >= 0) op = c;
    if (c->findData("symmetric") >= 0) dir = c;
  }
  if (!op) return false;
  // Turkish labels
  QStringList labels;
  for (int i = 0; i < op->count(); ++i) labels << op->itemText(i);
  if (log) *log = labels.join(" | ");
  op->setCurrentIndex(op->findData(s.opData));
  if (dir && !s.direction.isEmpty()) dir->setCurrentIndex(dir->findData(s.direction));
  pump(50);
  auto* targets = dlg->findChild<QListWidget*>("targetsList");
  auto* keep = dlg->findChild<QCheckBox*>("keepTool");
  auto* name = dlg->findChild<QLineEdit*>("bodyName");
  if (!targets || !keep || !name) return false;
  for (int i = 0; i < targets->count(); ++i)
    if (s.targets.contains(targets->item(i)->data(Qt::UserRole).toString())) targets->item(i)->setCheckState(Qt::Checked);
  if (s.keepTool && keep->isEnabled()) keep->setChecked(true);
  if (!s.bodyName.isEmpty() && name->isEnabled()) { name->setText(s.bodyName); }
  QPushButton* ok = nullptr;
  for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Oluştur") ok = b;
  if (!ok) return false;
  QTest::mouseClick(ok, Qt::LeftButton);
  return waitFor([&] { return w.featureDialog() == nullptr; }, 5000);
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
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);  // Windows: native file dialogs cannot be driven by the harness
  QApplication app(argc, argv);
  QApplication::setStyle("Fusion");
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) g_shots = argv[++i];
  startModalPump();

  pcd::MainWindow w;
  w.setAutoYes(true);   // never block on the change-review dialog / save prompts
  w.show();
  pump(300);
  check(w.openFile(QString(PC_SAMPLES_DIR) + "/bracket.pcad"), "open samples/bracket.pcad");
  pump(700);
  auto* v = w.viewer();
  check(!w.modelResult().shape.IsNull() && w.modelResult().bodies.size() == 1, "bracket regenerates as one body");
  check(w.bodiesTree()->topLevelItemCount() == 1, "Gövdeler panel lists the single body");
  check(w.issuesTable()->horizontalHeaderItem(6) && w.issuesTable()->horizontalHeaderItem(6)->text() == "Gövde", "issues table has a 'Gövde' column");
  const int nPlane0 = countFeatures(w.document(), pc::FeatureType::Plane);
  const double volMain = bodyVolume(w, QString::fromStdString(w.modelResult().bodies[0].name));

  // ---------------------------------------------------------------- 1. edge pick -> plane
  std::printf("== edge -> datum plane\n");
  QPoint ep;
  const QString edge = findEdgePos(w, ep);
  check(!edge.isEmpty(), "a straight edge is hit-testable in the viewer: " + edge);
  QString pickedEdge;
  QObject::connect(v, &pc::viewer::ShapeViewer::edgePicked, [&](const QString& n) { pickedEdge = n; });
  edgeMenu(v, ep);
  pump(200);
  check(pickedEdge == edge && w.selectedEdge() == edge, "Ctrl+click (SpaceClaim scheme: click) emits edgePicked and selects the edge (" + pickedEdge + " / " + w.selectedEdge() + ")");
  QMenu* em = popupMenu("edgeContextMenu");
  check(em != nullptr, "edge context menu is shown");
  if (em) {
    QStringList acts;
    for (auto* a : em->actions()) acts << a->text();
    check(acts.contains("Bu kenara dik düzlem") && acts.contains("Bu kenardan açılı düzlem") && acts.contains("İki kenardan düzlem"),
          "menu offers dik / açılı / iki kenardan düzlem");
    QImage shot = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    {
      QPainter p(&shot);
      p.drawImage(em->geometry().topLeft() - w.mapToGlobal(QPoint(0, 0)), em->grab().toImage());
    }
    saveShot(shot, "v2_edge_menu.png");
  }
  check(clickMenuAction(em, "Bu kenara dik düzlem"), "click 'Bu kenara dik düzlem'");
  pump(600);
  check(countFeatures(w.document(), pc::FeatureType::Plane) == nPlane0 + 1, "Plane feature added to the document");
  std::string planeId;
  for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Plane) planeId = ft.id;
  const pc::Feature* pf = w.document().feature(planeId);
  check(pf && pf->prop("kind") == "edgeNormal" && pf->refs.size() == 1 && pf->refs[0] == edge.toStdString() && pf->prop("t") == "0.5",
        "plane = edgeNormal, t=0.5, ref = picked edge");
  check(w.modelResult().datumPlanes.count(planeId) == 1, "model evaluated the datum plane");
  if (auto it = w.modelResult().datumPlanes.find(planeId); it != w.modelResult().datumPlanes.end()) {
    // origin must lie on the picked edge's midpoint line: just sanity check it is finite and near the part
    check(std::isfinite(it->second.frame.Location().X()), "datum plane frame is finite");
  }
  saveShot(w.grab().toImage(), "v2_plane_created.png");

  // ---------------------------------------------------------------- 2. click plane -> sketch
  std::printf("== sketch on the datum plane\n");
  QPoint dp;
  check(findDatumPos(w, QString::fromStdString(planeId), dp), "datum plane is hit-testable (pick buffer)");
  QString pickedDatum;
  QObject::connect(v, &pc::viewer::ShapeViewer::datumPicked, [&](const QString& n) { pickedDatum = n; });
  click(v, dp);
  pump(200);
  check(pickedDatum == QString::fromStdString(planeId), "click emits datumPicked(plane)");
  if (scScheme(v)) {   // SpaceClaim scheme: the datum menu is on the right button
    QContextMenuEvent ce(QContextMenuEvent::Mouse, dp, v->mapToGlobal(dp));
    QApplication::sendEvent(v, &ce);
    pump(200);
  }
  QMenu* dm = popupMenu("datumContextMenu");
  check(dm != nullptr, "datum context menu is shown");
  check(clickMenuAction(dm, "Bu düzlemde sketch başlat"), "click 'Bu düzlemde sketch başlat'");
  pump(400);
  check(w.inSketchMode(), "sketch editor opened");
  check(w.sketchEditor()->sketch().plane.kind == pc::SketchPlaneKind::OnPlane && w.sketchEditor()->sketch().plane.planeRef == planeId,
        "sketch is an OnPlane sketch referencing the plane");
  drawRectWithDimensions(w, 60, 40, "30", "20");
  saveShot(w.grab().toImage(), "v2_sketch_on_plane.png");
  w.finishSketchPublic(true);
  pump(500);
  check(!w.inSketchMode(), "sketch finished");
  std::string sk1;
  for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Sketch) sk1 = ft.id;
  check(w.document().feature(sk1) && w.document().feature(sk1)->sketch->plane.kind == pc::SketchPlaneKind::OnPlane, "committed sketch keeps OnPlane/planeRef");
  check(w.modelResult().datumPlanes.count(planeId) == 1, "datum plane still shown after returning to 3D");

  // ---------------------------------------------------------------- 3. extrude as separate body
  std::printf("== extrude as 'Ayrı gövde'\n");
  const int nBodies0 = int(w.modelResult().bodies.size());
  QString opLabels;
  const bool ok1 = runExtrudeDialog(w, {"6", "new", "Kulak", {}, false, "normal"}, &opLabels);
  check(ok1, "Extrude dialog accepted (op = Ayrı gövde, body name 'Kulak')");
  check(opLabels == "Birleştir (Add) | Çıkar (Subtract) | Kesişim (Intersect) | Ayrı gövde (New body)", "İşlem labels: " + opLabels);
  pump(600);
  check(int(w.modelResult().bodies.size()) == nBodies0 + 1, QString("model has one more body (%1)").arg(w.modelResult().bodies.size()));
  check(bodiesPanelNames(w).contains("Kulak"), "body 'Kulak' appears in the Gövdeler panel: " + bodiesPanelNames(w).join(","));
  check(w.modelResult().body("Kulak") != nullptr && !w.modelResult().body("Kulak")->faces.empty(), "ModelResult body 'Kulak' has faces");
  check(std::fabs(bodyVolume(w, QString::fromStdString(w.modelResult().bodies[0].name)) - volMain) < 1e-3 * volMain, "main body volume unchanged by 'Ayrı gövde'");
  const double volKulak = bodyVolume(w, "Kulak");
  for (const auto& ft : w.document().features) {
    if (ft.type == pc::FeatureType::Extrude) std::printf("  %s distance=%s op=%s\n", ft.id.c_str(), ft.prop("distance").c_str(), ft.prop("op").c_str());
    if (ft.sketch && ft.sketch->plane.kind == pc::SketchPlaneKind::OnPlane) {
      for (const auto& k : ft.sketch->constraints) if (pc::isDimensional(k.type)) std::printf("  %s dim %s = '%s'\n", ft.id.c_str(), pc::toString(k.type), k.valueExpr.c_str());
      for (const auto& p : ft.sketch->points) std::printf("   pt %d (%g,%g)\n", p.id, p.x, p.y);
    }
  }
  // expected volume = rectangle area * distance (30 x 20 x 6) when the dimensions solved
  check(std::fabs(volKulak - 30 * 20 * 6) < 1.0, QString("Kulak volume %1 == 30*20*6").arg(volKulak, 0, 'f', 2));
  // volume column in the bodies panel
  {
    bool found = false;
    for (int i = 0; i < w.bodiesTree()->topLevelItemCount(); ++i) {
      auto* it = w.bodiesTree()->topLevelItem(i);
      if (it->data(0, Qt::UserRole).toString() == "Kulak") found = std::fabs(it->text(1).toDouble() - volKulak) < 0.5;
    }
    check(found, "Gövdeler panel shows the body volume");
  }
  saveShot(w.grab().toImage(), "v2_separate_body.png");

  // ---------------------------------------------------------------- 4. subtract with target
  std::printf("== extrude as 'Çıkar' with a target body\n");
  w.beginSketchOnPlane(planeId);
  pump(400);
  check(w.inSketchMode(), "second sketch on the same datum plane");
  drawRectWithDimensions(w, 60, 45, "10", "8");  // large enough on high-DPI screens
  w.finishSketchPublic(true);
  pump(500);
  const int nBodies1 = int(w.modelResult().bodies.size());
  ExtrudeSetup cut;
  cut.distance = "4";
  cut.opData = "cut";
  cut.targets = {"Kulak"};
  cut.direction = "symmetric";
  const bool ok2 = runExtrudeDialog(w, cut);
  check(ok2, "Extrude dialog accepted (op = Çıkar, target = Kulak)");
  pump(600);
  const pc::Feature* cutF = nullptr;
  for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Extrude) cutF = &ft;
  check(cutF && cutF->prop("op") == "cut" && cutF->prop("targets") == "Kulak", "feature stores op=cut, targets=Kulak");
  check(int(w.modelResult().bodies.size()) == nBodies1, "body count unchanged by the subtraction");
  const double volKulak2 = bodyVolume(w, "Kulak");
  check(volKulak2 > 0 && volKulak - volKulak2 > 10, QString("Kulak volume reduced by the cut (%1 -> %2)").arg(volKulak, 0, 'f', 1).arg(volKulak2, 0, 'f', 1));
  check(std::fabs(bodyVolume(w, QString::fromStdString(w.modelResult().bodies[0].name)) - volMain) < 1e-3 * volMain, "main body untouched (target was Kulak only)");
  saveShot(w.grab().toImage(), "v2_subtract_target.png");
  // all features regenerated without errors
  bool allOk = true;
  for (const auto& s : w.modelResult().status) if (!s.ok) { allOk = false; std::printf("  status %s: %s\n", s.featureId.c_str(), s.message.c_str()); }
  check(allOk, "all features regenerate without errors");

  // ---------------------------------------------------------------- 5. bodies panel
  std::printf("== bodies panel\n");
  {
    const QImage before = v->renderToImage(500, 380);
    // uncheck 'Kulak' through the checkbox in the panel (visibility)
    QTreeWidgetItem* kit = nullptr;
    for (int i = 0; i < w.bodiesTree()->topLevelItemCount(); ++i)
      if (w.bodiesTree()->topLevelItem(i)->data(0, Qt::UserRole).toString() == "Kulak") kit = w.bodiesTree()->topLevelItem(i);
    check(kit != nullptr, "Kulak item in panel");
    if (kit) {
      kit->setCheckState(0, Qt::Unchecked);
      pump(200);
      const QImage hidden = v->renderToImage(500, 380);
      check(hidden != before, "unchecking the visibility box hides the body in the viewer");
      kit->setCheckState(0, Qt::Checked);
      pump(200);
      check(v->renderToImage(500, 380) == before, "checking it again restores the view");
      w.bodiesTree()->setCurrentItem(kit);
      pump(200);
      check(w.statusBar()->currentMessage().contains("Kulak"), "selecting a body highlights it (status shows body info)");
    }
    // rename: bodyName is written on the creating feature and references are rewritten
    check(w.renameBody("Kulak", "Tutamak"), "rename Kulak -> Tutamak");
    pump(500);
    bool named = false;
    for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Extrude && ft.prop("bodyName") == "Tutamak") named = true;
    check(named, "creating feature carries bodyName=Tutamak");
    bool refsFixed = false;
    for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Extrude && ft.prop("op") == "cut") refsFixed = ft.prop("targets") == "Tutamak";
    check(refsFixed, "cut feature's targets follow the rename");
    check(w.modelResult().body("Tutamak") != nullptr && w.modelResult().body("Kulak") == nullptr, "model body renamed");
    check(bodiesPanelNames(w).contains("Tutamak"), "panel shows the new name");
  }

  // ---------------------------------------------------------------- 6. plane dialog with live preview
  std::printf("== plane dialog\n");
  {
    const int nPl = countFeatures(w.document(), pc::FeatureType::Plane);
    QAction* ins = nullptr;
    for (QAction* a : w.findChildren<QAction*>()) if (a->text() == "Düzlem...") ins = a;
    check(ins != nullptr, "Ekle > Düzlem... action exists");
    if (ins) ins->trigger();
    check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "plane dialog opened");
    if (auto* dlg = w.featureDialog()) {
      auto* kind = dlg->findChild<QComboBox*>();
      check(kind && kind->findData("edgeNormal") >= 0 && kind->findData("edgeAngle") >= 0 && kind->findData("twoEdges") >= 0 && kind->findData("faceOffset") >= 0,
            "kind combo has the four plane kinds");
      QPoint ep2;
      const QString e2 = findEdgePos(w, ep2, edge);
      edgeClick(v, ep2);
      pump(300);
      auto* r1 = dlg->findChild<QLineEdit*>("refEdge1");
      check(r1 && r1->text() == e2 && !e2.isEmpty(), "edge picked in the viewer fills 'Kenar 1': " + e2);
      check(popupMenu("edgeContextMenu") == nullptr, "no context menu while the dialog collects edges");
      check(waitFor([&] { auto* l = dlg->findChild<QLabel*>("previewLabel"); return l && l->text().contains("tamam"); }, 4000), "live preview reports success");
      saveShot(w.grab().toImage(), "v2_plane_dialog_preview.png");
      saveShot(dlg->grab().toImage(), "v2_plane_dialog.png");
      // edgeAngle needs the angle field
      kind->setCurrentIndex(kind->findData("edgeAngle"));
      pump(400);
      auto* pl = dlg->findChild<QLabel*>("previewLabel");
      check(pl && pl->text().contains("tamam"), "edgeAngle preview ok: " + (pl ? pl->text() : QString()));
      kind->setCurrentIndex(kind->findData("edgeNormal"));
      pump(200);
      QPushButton* ok = nullptr;
      for (auto* b : dlg->findChildren<QPushButton*>()) if (b->text() == "Oluştur") ok = b;
      if (ok) QTest::mouseClick(ok, Qt::LeftButton);
      waitFor([&] { return w.featureDialog() == nullptr; }, 4000);
      pump(400);
      check(countFeatures(w.document(), pc::FeatureType::Plane) == nPl + 1, "plane created from the dialog");
    }
    // two-edge plane through the context menu flow
    pc::Feature* dummy = nullptr; (void)dummy;
    QString a, b;
    {
      // two straight edges bounding the same planar face
      const auto& edges = w.modelResult().edges;
      for (const auto& [n1, e1] : edges) {
        if (!isLineEdge(w.modelResult(), QString::fromStdString(n1))) continue;
        for (const auto& [n2, e2x] : edges) {
          if (n1 >= n2 || !isLineEdge(w.modelResult(), QString::fromStdString(n2))) continue;
          // share a face name: "edge(A|B)" and "edge(A|C)"
          const QString q1 = QString::fromStdString(n1), q2 = QString::fromStdString(n2);
          const QStringList f1 = q1.mid(5, q1.size() - 6).split('|'), f2 = q2.mid(5, q2.size() - 6).split('|');
          for (const auto& x : f1) if (f2.contains(x) && x.endsWith(".end") && f1.size() == 2 && f2.size() == 2) { a = q1; b = q2; }
          if (!a.isEmpty()) break;
        }
        if (!a.isEmpty()) break;
      }
    }
    if (!a.isEmpty()) {
      const int nPl2 = countFeatures(w.document(), pc::FeatureType::Plane);
      if (scScheme(v)) {   // SpaceClaim scheme: plain click selects, the edge menu is on the right button
        emit v->picked(a, pc::viewer::ShapeViewer::PickEdge, Qt::NoModifier, 1, QPoint(-1, -1));
        pump(100);
        w.showEdgeMenu(a, v->mapToGlobal(QPoint(40, 40)));
      } else {
        QMetaObject::invokeMethod(v, "edgePicked", Q_ARG(QString, a));
      }
      pump(200);
      QMenu* m2 = popupMenu("edgeContextMenu");
      check(clickMenuAction(m2, "İki kenardan düzlem"), "choose 'İki kenardan düzlem'");
      pump(100);
      if (scScheme(v)) emit v->picked(b, pc::viewer::ShapeViewer::PickEdge, Qt::NoModifier, 1, QPoint(-1, -1));
      else QMetaObject::invokeMethod(v, "edgePicked", Q_ARG(QString, b));
      pump(600);
      const pc::Feature* last = nullptr;
      for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Plane) last = &ft;
      check(countFeatures(w.document(), pc::FeatureType::Plane) == nPl2 + 1 && last && last->prop("kind") == "twoEdges" && last->refs.size() == 2,
            "second edge completes a twoEdges plane");
    } else {
      std::printf("  (no coplanar edge pair found; twoEdges flow skipped)\n");
    }
  }

  // ---------------------------------------------------------------- 7. Combine
  std::printf("== combine dialog\n");
  {
    QAction* comb = nullptr;
    for (QAction* a : w.findChildren<QAction*>()) if (a->text() == "Gövdeleri Birleştir (Combine)...") comb = a;
    check(comb != nullptr, "Ekle > Gövdeleri Birleştir (Combine) action exists");
    if (comb) comb->trigger();
    check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "combine dialog opened");
    if (auto* dlg = w.featureDialog()) {
      auto* tb = dlg->findChild<QComboBox*>("targetBody");
      auto* tl = dlg->findChild<QListWidget*>("toolsList");
      check(tb && tl && tb->count() == int(w.modelResult().bodies.size()), "target combo + tools checklist list every body");
      saveShot(dlg->grab().toImage(), "v2_combine_dialog.png");
      dlg->reject();
      pump(100);
    }
  }

  saveShot(w.grab().toImage(), "v2_final.png");

  // ---------------------------------------------------------------- 8. review regressions (v2 review)
  std::printf("== review regressions\n");
  g_onModal = [](QWidget* m) { if (auto* mb = qobject_cast<QMessageBox*>(m)) mb->accept(); };
  {
    // (a) undo/redo of a datum plane created from the edge menu
    const int nPl = countFeatures(w.document(), pc::FeatureType::Plane);
    const size_t nDp = w.modelResult().datumPlanes.size();
    QString anyEdge;
    for (const auto& [n, e] : w.modelResult().edges) if (isLineEdge(w.modelResult(), QString::fromStdString(n))) { anyEdge = QString::fromStdString(n); break; }
    check(!anyEdge.isEmpty() && w.createPlaneOnEdge(anyEdge), "plane created on " + anyEdge);
    check(w.modelResult().datumPlanes.size() == nDp + 1, "datum plane evaluated");
    w.undoStack()->undo();
    pump(200);
    check(countFeatures(w.document(), pc::FeatureType::Plane) == nPl && w.modelResult().datumPlanes.size() == nDp, "undo removes the plane");
    w.undoStack()->redo();
    pump(200);
    check(countFeatures(w.document(), pc::FeatureType::Plane) == nPl + 1 && w.modelResult().datumPlanes.size() == nDp + 1, "redo restores the plane");

    // (b) while a modeless feature dialog is open, edge-menu planes / body renames are refused instead of being
    //     silently dropped by the dialog's OK (it commits its own document snapshot)
    w.createFeature(pc::FeatureType::Extrude);
    check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "extrude dialog open");
    const int nFeat = int(w.document().features.size());
    check(!w.createPlaneOnEdge(anyEdge) && int(w.document().features.size()) == nFeat, "plane from edge menu refused while a dialog is open");
    const QString b0 = QString::fromStdString(w.modelResult().bodies.back().name);
    check(!w.renameBody(b0, "Yeniad") && w.modelResult().body(b0.toStdString()) != nullptr, "body rename refused while a dialog is open");
    if (auto* d = w.featureDialog()) d->reject();
    pump(200);
  }
  {
    // (c) dialogs keep v2 props they have no widget for (Pattern op/bodyName, Hole keepTool/bodyName, Shell targets)
    pc::Document doc = w.document();
    auto roundTrip = [&](pc::FeatureType t, std::map<std::string, std::string> props) {
      pc::Feature f;
      f.type = t;
      f.id = doc.uniqueFeatureId(t);
      f.props = props;
      pcd::FeatureDialog dlg(&doc, f, false, &w, QStringList{"A", "B"});
      return dlg.feature();
    };
    pc::Feature p = roundTrip(pc::FeatureType::Pattern, {{"kind", "linear"}, {"count", "2"}, {"spacing", "10"}, {"op", "new"}, {"bodyName", "Pimler"}, {"targets", "A"}});
    check(p.prop("op") == "new" && p.prop("bodyName") == "Pimler" && p.prop("targets") == "A", "Pattern dialog keeps op/bodyName/targets");
    pc::Feature h = roundTrip(pc::FeatureType::Hole, {{"diameter", "5"}, {"keepTool", "1"}, {"bodyName", "Tapa"}});
    check(h.prop("keepTool") == "1" && h.prop("bodyName") == "Tapa", "Hole dialog keeps keepTool/bodyName");
    pc::Feature s = roundTrip(pc::FeatureType::Shell, {{"thickness", "2"}, {"targets", "B"}});
    check(s.prop("targets") == "B", "Shell dialog keeps targets");
    pc::Feature e = roundTrip(pc::FeatureType::Extrude, {{"distance", "5"}, {"op", "cut"}, {"targets", "A"}, {"keepTool", "1"}, {"bodyName", "K"}});
    check(e.prop("op") == "cut" && e.prop("targets") == "A" && e.prop("keepTool") == "1" && e.prop("bodyName") == "K", "Extrude dialog round-trips its body options");
  }
  {
    // (d) sample: editing an early feature only offers bodies that exist before it; split-piece rename
    check(w.openFile(QString(PC_SAMPLES_DIR) + "/v2_multibody.pcad"), "open samples/v2_multibody.pcad");
    pump(300);
    if (!g_shots.isEmpty()) {   // documentation screenshots on the v2 sample (only with --shots)
      // edge context menu on the plate's top edge
      QPoint ep;
      const QString want = "edge(Extrude1.end|Extrude1.side.e5)";
      bool found = false;
      for (int y = 40; y < v->height() - 40 && !found; y += 4)
        for (int x = 40; x < v->width() - 40; x += 4)
          if (v->edgeAt(QPoint(x, y)) == want) { ep = QPoint(x, y); found = true; break; }
      if (found) {
        edgeMenu(v, ep);
        pump(400);
        if (QMenu* em = popupMenu("edgeContextMenu")) {
          QImage shot = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
          QPainter p(&shot);
          const QPoint at = v->mapTo(&w, ep) + QPoint(6, 6);
          const QImage mi = em->grab().toImage();
          p.fillRect(QRect(at + QPoint(3, 3), mi.size()), QColor(0, 0, 0, 50));
          p.drawImage(at, mi);
          p.end();
          saveShot(shot, "final_v2_plane_menu.png");
          em->close();
          pump(100);
        }
      }
      // Extrude dialog with the İşlem list open + target bodies
      w.openFeatureEditor("Extrude5");
      if (waitFor([&] { return w.featureDialog() != nullptr; }, 3000)) {
        pcd::FeatureDialog* dlg = w.featureDialog();
        pump(300);
        QComboBox* op = nullptr;
        for (auto* c : dlg->findChildren<QComboBox*>()) if (c->findData("cut") >= 0 && c->findData("new") >= 0) op = c;
        if (op) {
          op->showPopup();
          pump(400);
          QWidget* pop = op->view()->window();
          const QImage di = dlg->grab().toImage();
          const QImage pi = pop->grab().toImage();
          const QRect dr(dlg->mapToGlobal(QPoint(0, 0)), di.size());
          const QRect pr(pop->mapToGlobal(QPoint(0, 0)), pi.size());
          const QRect all = dr.united(pr).adjusted(-8, -8, 12, 12);
          QImage img(all.size(), QImage::Format_RGB32);
          img.fill(QColor(236, 239, 243));
          QPainter p(&img);
          p.fillRect(QRect(dr.topLeft() - all.topLeft() + QPoint(4, 4), di.size()), QColor(0, 0, 0, 40));
          p.drawImage(dr.topLeft() - all.topLeft(), di);
          p.setPen(QColor(150, 158, 168));
          p.drawRect(QRect(dr.topLeft() - all.topLeft(), di.size()).adjusted(0, 0, -1, -1));
          p.fillRect(QRect(pr.topLeft() - all.topLeft() + QPoint(3, 3), pi.size()), QColor(0, 0, 0, 50));
          p.drawImage(pr.topLeft() - all.topLeft(), pi);
          p.end();
          saveShot(img, "final_v2_op_dialog.png");
          op->hidePopup();
        }
        dlg->reject();
        pump(200);
      }
    }
    w.openFeatureEditor("Extrude2");
    check(waitFor([&] { return w.featureDialog() != nullptr; }, 3000), "Extrude2 editor open");
    if (auto* d = w.featureDialog()) {
      QStringList items;
      if (auto* tl = d->findChild<QListWidget*>("targetsList")) for (int i = 0; i < tl->count(); ++i) items << tl->item(i)->text();
      check(items.contains("Braket") && !items.contains("Tab") && !items.contains("Tapa"), "Extrude2 targets list only has earlier bodies: " + items.join(","));
      d->reject();
      pump(200);
    }
    // a hidden body's edges are not pickable
    w.setBodyVisible("Braket", false);
    pump(100);
    QSet<QString> braketFaces;
    if (const auto* bb = w.modelResult().body("Braket")) for (const auto& fn : bb->faces) braketFaces.insert(QString::fromStdString(fn));
    QString braketEdge;
    int hits = 0;
    for (int y = 40; y < v->height() - 40 && braketEdge.isEmpty(); y += 9)
      for (int x = 40; x < v->width() - 40; x += 9) {
        const QString en = v->edgeAt(QPoint(x, y));
        if (en.isEmpty()) continue;
        ++hits;
        const QStringList fs = en.mid(5, en.size() - 6).split('|');
        bool all = en.startsWith("edge(");
        for (const auto& fn : fs) if (!braketFaces.contains(fn)) all = false;
        if (all) { braketEdge = en; break; }
      }
    check(!braketFaces.isEmpty() && hits > 0 && braketEdge.isEmpty(), "edges of the hidden body 'Braket' are not hit-testable " + braketEdge);
    w.setBodyVisible("Braket", true);

    // split body: cut the Tab in two, then rename one piece -> the stem (creator bodyName) is renamed and
    // references to the pieces follow
    pc::Document d = w.document();
    pc::Feature sk;
    sk.id = "SketchR";
    sk.type = pc::FeatureType::Sketch;
    pc::Sketch s = pc::Sketch::create();
    s.plane.kind = pc::SketchPlaneKind::OnPlane;
    s.plane.planeRef = "Plane2";
    s.addRectangle(-2, -60, 2, 60);
    sk.sketch = s;
    d.features.push_back(sk);
    pc::Feature cut;
    cut.id = "ExtrudeR";
    cut.type = pc::FeatureType::Extrude;
    cut.props = {{"sketch", "SketchR"}, {"distance", "40"}, {"direction", "symmetric"}, {"op", "cut"}, {"targets", "Tab"}};
    d.features.push_back(cut);
    const QString tmp = QDir::tempPath() + "/pc_review_split.pcad";
    check(pc::saveDocument(d, tmp.toStdString()), "save split doc");
    check(w.openFile(tmp), "open split doc");
    pump(300);
    const auto* t1 = w.modelResult().body("Tab:1");
    const auto* t2 = w.modelResult().body("Tab:2");
    if (t1 && t2) {
      w.setBodyVisible("Tab:2", false);
      check(w.renameBody("Tab:2", "Kanat"), "rename split piece Tab:2 -> Kanat");
      pump(300);
      const pc::Feature* ex4 = w.document().feature("Extrude4");
      const pc::Feature* cmb = w.document().feature("Combine1");
      check(ex4 && ex4->prop("bodyName") == "Kanat", "creator bodyName renamed");
      check(w.modelResult().body("Kanat:1") && w.modelResult().body("Kanat:2") && !w.modelResult().body("Tab:2"), "pieces follow: Kanat:1, Kanat:2");
      check(cmb && cmb->prop("target") == "Kanat", "Combine target follows the rename: " + QString::fromStdString(cmb ? cmb->prop("target") : ""));
      const pc::Feature* er = w.document().feature("ExtrudeR");
      check(er && er->prop("targets") == "Kanat", "cut targets follow the rename");
      bool hiddenKept = false;
      for (int i = 0; i < w.bodiesTree()->topLevelItemCount(); ++i) {
        auto* it = w.bodiesTree()->topLevelItem(i);
        if (it->data(0, Qt::UserRole).toString() == "Kanat:2") hiddenKept = it->checkState(0) == Qt::Unchecked;
      }
      check(hiddenKept, "hidden piece stays hidden after the rename");
    } else {
      check(false, "split doc produced pieces Tab:1/Tab:2: " + bodiesPanelNames(w).join(","));
    }
    QFile::remove(tmp);
  }
  g_onModal = nullptr;

  w.close();
  std::printf("\n%s: %d failure(s)\n", g_fail ? "FAILED" : "PASSED", g_fail);
  return g_fail;
}
