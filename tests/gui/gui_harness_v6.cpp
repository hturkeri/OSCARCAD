// pc_gui_harness_v6 — customer report (Nov 2026):
//   (1) "Sketch modunda verdiğim parametreler sketch'i kapatınca kayboldu."
//   (2) "Sketch'ten 3D cisim yaptığımda bu işlem için parametre veremiyorum; parametreler seçilebilir olsun."
// Drives the real designer like a user (toolbar actions, mouse clicks in the sketch editor, modal dimension
// dialogs, the feature dialog's parameter picker / "Yeni parametre…" dialog, the parameter table, Save as App)
// and asserts both problems are fixed.
//   pc_gui_harness_v6 [--shots DIR]      Exit code = number of failed checks.
#include "ExprEdit.h"
#include "harness_robust.h"
#include "MainWindow.h"
#include "pc/AppPackage.h"
#include "pc/Model.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardItemModel>
#include <QSurfaceFormat>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <functional>
#include <nlohmann/json.hpp>

namespace {

int g_fail = 0;
QString g_shots;

void check(bool ok, const QString& what) {
  std::printf("[%s] %s\n", ok ? " ok " : "FAIL", qPrintable(what));
  std::fflush(stdout);
  pch::kick();
  if (!ok) ++g_fail;
}
void pump(int ms) { QTest::qWait(ms); }
// Polls with a timeout (never infinite). Timeouts are generous (they only cost time on failure) and scale with
// PC_HARNESS_SLOW for slow machines.
bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
  timeoutMs *= pch::slowFactor();
  for (int t = 0; t < timeoutMs; t += 30) {
    if (pred()) return true;
    QTest::qWait(30);
  }
  return pred();
}
void shot(QWidget* w, const QString& name) {
  if (g_shots.isEmpty() || !w) return;
  QDir().mkpath(g_shots);
  w->grab().save(g_shots + "/" + name);
}

// ---- modal dialog pump (modal exec() loops still run timers)
std::function<void(QWidget*)> g_onModal;
void startModalPump() {
  auto* t = new QTimer(qApp);
  t->setInterval(50);
  QObject::connect(t, &QTimer::timeout, [] {
    static bool busy = false;
    if (busy) return;
    QWidget* w = QApplication::activeModalWidget();
    if (!w || !w->isVisible()) return;
    // never wait forever on a modal: past its deadline it is dumped, reported FAIL and force-closed
    if (pch::enforceDeadline(w)) return;
    if (!g_onModal || w->property("pcHandled").toBool()) return;
    busy = true;
    g_onModal(w);
    busy = false;
  });
  t->start();
}
void markHandled(QWidget* w) { w->setProperty("pcHandled", true); }
template <class T>
T* childWithText(QWidget* w, const QString& text) {
  for (T* c : w->findChildren<T*>())
    if (c->text() == text) return c;
  return nullptr;
}
void clickLater(QAbstractButton* b, int ms = 0) { pch::pressLater(b, ms); }
QStringList g_boxes;
// Message boxes: answer with `answer` if present (default Yes/Ok).
QMessageBox::StandardButton g_boxAnswer = QMessageBox::Yes;
bool handleMessageBox(QWidget* w) {
  auto* mb = qobject_cast<QMessageBox*>(w);
  if (!mb) return false;
  markHandled(mb);
  g_boxes << mb->text();
  std::printf("  [msgbox] %s\n", qPrintable(mb->text().left(160).replace('\n', ' ')));
  QAbstractButton* b = mb->button(g_boxAnswer);
  if (!b) b = mb->button(QMessageBox::Yes);
  if (!b) b = mb->button(QMessageBox::Ok);
  if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
  clickLater(b);
  return true;
}

QAction* actionByText(QWidget* root, const QString& text) {
  for (QAction* a : root->findChildren<QAction*>())
    if (a->text() == text && a->isEnabled()) return a;
  return nullptr;
}
void trigger(QWidget* root, const QString& text) {
  QAction* a = actionByText(root, text);
  check(a != nullptr, "action '" + text + "' available");
  if (a) a->trigger();
}

// ---- sketch editor mouse helpers (real input events)
void clickAt(QWidget* w, QPointF p, Qt::KeyboardModifiers m = {}) {
  QTest::mouseMove(w, p.toPoint());
  QTest::mouseClick(w, Qt::LeftButton, m, p.toPoint());
}
QPointF lineMid(pcd::SketchEditor* ed, int lineId) {
  const auto* e = ed->sketch().entity(lineId);
  if (!e) return {};
  return (ed->pointScreenPos(e->points[0]) + ed->pointScreenPos(e->points[1])) / 2.0;
}
int constraintCount(const pc::Sketch& s, pc::SkConstraintType t) {
  int n = 0;
  for (const auto& c : s.constraints) n += c.type == t;
  return n;
}
std::vector<std::string> dimExprs(const pc::Sketch& s) {
  std::vector<std::string> out;
  for (const auto& c : s.constraints) if (pc::isDimensional(c.type)) out.push_back(c.valueExpr);
  return out;
}
bool hasDim(const pc::Sketch& s, const std::string& expr) {
  for (const auto& e : dimExprs(s)) if (e == expr) return true;
  return false;
}
const pc::Sketch* docSketch(const pcd::MainWindow& w, const std::string& id) {
  const auto* f = w.document().feature(id);
  return f && f->sketch ? &*f->sketch : nullptr;
}
std::string firstSketchId(const pcd::MainWindow& w) {
  for (const auto& f : w.document().features) if (f.type == pc::FeatureType::Sketch) return f.id;
  return {};
}

// The dimension dialog: type a value and optionally create a named parameter through it.
struct DimAnswer { QString value; QString newName; bool exposed = false; bool cancel = false; };
std::vector<DimAnswer> g_dimAnswers;
int g_dimDialogs = 0;
bool handleDimDialog(QWidget* m) {
  if (!m->inherits("pcd::DimensionDialog")) return false;
  markHandled(m);
  ++g_dimDialogs;
  const DimAnswer a = g_dimAnswers.empty() ? DimAnswer{} : g_dimAnswers.front();
  if (!g_dimAnswers.empty()) g_dimAnswers.erase(g_dimAnswers.begin());
  QTimer::singleShot(80, m, [m, a] {
    if (a.cancel) { clickLater(childWithText<QPushButton>(m, "Vazgeç")); return; }
    auto* ee = m->findChild<pcd::ExprEdit*>("dimValue");
    if (!ee) ee = m->findChild<pcd::ExprEdit*>();
    if (ee && !a.value.isEmpty()) pch::typeInto(ee->lineEdit(), a.value, "dimension value");
    if (!a.newName.isEmpty()) {
      auto* cb = m->findChild<QCheckBox*>("dimNewParam");
      auto* nm = m->findChild<QLineEdit*>("dimParamName");
      if (!cb || !nm) pch::hnote("dimension dialog: 'yeni adlandırılmış parametre' check box / name field not found");
      pch::tickCheckBox(cb, true, "dimension dialog 'yeni parametre' check box");
      if (nm && !nm->isEnabled()) pch::hnote("dimension dialog: name field still disabled after ticking 'yeni parametre'");
      pch::typeInto(nm, a.newName, "dimension dialog parameter name");
      pch::tickCheckBox(m->findChild<QCheckBox*>("dimParamExposed"), a.exposed, "dimension dialog 'Uygulamada göster'");
    }
    if (!g_shots.isEmpty() && !a.newName.isEmpty()) shot(m, "v6_dimension_dialog.png");
    clickLater(childWithText<QPushButton>(m, "Tamam"));
  });
  return true;
}

// Draws a rectangle with the Rect tool; returns its 4 line ids (bottom, right, top, left).
std::vector<int> drawRect(pcd::MainWindow& w, QPointF a, QPointF b) {
  auto* ed = w.sketchEditor();
  trigger(&w, "Dikdörtgen");
  const size_t n0 = ed->sketch().entities.size();
  clickAt(ed, a);
  clickAt(ed, b);
  std::vector<int> ids;
  for (size_t i = n0; i < ed->sketch().entities.size(); ++i) ids.push_back(ed->sketch().entities[i].id);
  trigger(&w, "Seç");
  return ids;
}
// Selects one line by clicking it and adds a dimension through the toolbar ("Uzunluk").
void dimensionLine(pcd::MainWindow& w, int lineId, const DimAnswer& a) {
  auto* ed = w.sketchEditor();
  clickAt(ed, QPointF(5, 5));   // clear selection
  clickAt(ed, lineMid(ed, lineId));
  g_dimAnswers.push_back(a);
  const int before = g_dimDialogs;
  QTimer::singleShot(0, &w, [&w] { trigger(&w, "Uzunluk"); });
  waitFor([&] { return g_dimDialogs > before && !QApplication::activeModalWidget(); }, 45000);   // > modal deadline
  pump(100);
}

// Starts a new sketch on XY through "Ekle > Sketch..." (plane picker = QInputDialog).
void newSketchXY(pcd::MainWindow& w) {
  g_onModal = [](QWidget* m) {
    if (handleMessageBox(m) || handleDimDialog(m)) return;
    if (m->inherits("QInputDialog")) {
      markHandled(m);
      QTimer::singleShot(50, m, [m] {
        if (auto* cb = m->findChild<QComboBox*>()) cb->setCurrentIndex(0);
        for (auto* b : m->findChildren<QPushButton*>()) if (b->text().contains("OK") || b->text() == "Tamam") { QTest::mouseClick(b, Qt::LeftButton); return; }
        QMetaObject::invokeMethod(m, "accept");
      });
    }
  };
  QTimer::singleShot(0, &w, [&w] { trigger(&w, "Sketch..."); });
  waitFor([&] { return w.inSketchMode() && !QApplication::activeModalWidget(); }, 5000);
  pump(200);
}

// ---------------------------------------------------------------- parameter table helpers
QStandardItemModel* paramModel(pcd::MainWindow& w) {
  for (auto* v : w.findChildren<QTableView*>())
    if (auto* m = qobject_cast<QStandardItemModel*>(v->model())) if (m->columnCount() >= 7) return m;
  return nullptr;
}
int paramRow(pcd::MainWindow& w, const QString& name) {
  auto* m = paramModel(w);
  if (!m) return -1;
  for (int r = 0; r < m->rowCount(); ++r) if (m->item(r, 0) && m->item(r, 0)->text() == name) return r;
  return -1;
}
bool setTableExpr(pcd::MainWindow& w, const QString& name, const QString& expr) {
  const int r = paramRow(w, name);
  if (r < 0) return false;
  paramModel(w)->item(r, 1)->setText(expr);
  return true;
}
std::string paramExpr(const pcd::MainWindow& w, const char* n) {
  const auto* p = w.document().params.find(n);
  return p ? p->expression : std::string("<none>");
}
int darkPixels(pcd::MainWindow& w) {
  pump(400);
  const QImage img = w.viewer()->grabFramebuffer();
  int n = 0;
  for (int y = 0; y < img.height(); y += 2)
    for (int x = 0; x < img.width(); x += 2)
      if (qGray(img.pixel(x, y)) < 110) ++n;
  return n;
}
double heightZ(const pcd::MainWindow& w) {
  if (w.modelResult().shape.IsNull()) return -1;
  const auto mp = pc::model::massProperties(w.modelResult().shape);
  return mp.zmax - mp.zmin;
}

int runProc(const QString& prog, const QStringList& args, QByteArray* out) {
  QProcess p;
  p.start(prog, args);
  if (!p.waitForFinished(120000)) { p.kill(); return -100; }
  if (out) *out = p.readAllStandardOutput();
  return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -99;
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
  for (int i = 1; i + 1 < argc; ++i) if (QString(argv[i]) == "--shots") g_shots = argv[i + 1];
  startModalPump();
  pch::checkFn() = check;
  pch::startWatchdog([] { return g_fail; });
  QTemporaryDir tmp;

  pcd::MainWindow w;
  w.show();
  pump(400);

  // ======================================================================= (1) sketch dims survive
  std::printf("== (1) sketch: rectangle + typed dimension + named-parameter dimension -> Sketch'i Bitir\n");
  newSketchXY(w);
  check(w.inSketchMode(), "sketch mode entered via Ekle > Sketch... (XY)");
  auto* ed = w.sketchEditor();
  const QPointF c(ed->width() / 2.0, ed->height() / 2.0);
  const auto rect = drawRect(w, c + QPointF(-150, 100), c + QPointF(150, -80));
  check(rect.size() == 4, "rectangle drawn with real clicks (4 lines)");
  if (rect.size() != 4) { std::printf("%d failure(s)\n", g_fail); return g_fail ? g_fail : 1; }
  dimensionLine(w, rect[0], {"40", "", false});                // typed number
  dimensionLine(w, rect[1], {"25", "H", true});                // new named parameter H (exposed) via the dialog
  check(hasDim(ed->sketch(), "40"), "editor: bottom line Length = 40");
  check(hasDim(ed->sketch(), "H"), "editor: right line Length = H");
  check(paramExpr(w, "H") == "25", "parameter H created by the dimension dialog is in the DOCUMENT immediately (H = " + QString::fromStdString(paramExpr(w, "H")) + ")");
  check(paramRow(w, "H") >= 0, "H appears in the parameter table while still sketching");
  {
    bool inView = true;
    for (const auto& pt : ed->sketch().points)
      if (!QRectF(ed->rect()).contains(ed->pointScreenPos(pt.id))) inView = false;
    check(inView, "dimensioned sketch stays inside the sketch view (auto-fit)");
  }
  shot(&w, "v6_sketch_dims.png");
  // editing H in the parameter table while sketching re-solves the open sketch
  check(setTableExpr(w, "H", "30"), "H edited in the parameter table during sketch mode");
  check(waitFor([&] { return paramExpr(w, "H") == "30"; }, 5000), "table edit committed (H = 30)");
  pump(200);
  {
    const auto* e = ed->sketch().entity(rect[1]);
    const auto *p0 = e ? ed->sketch().point(e->points[0]) : nullptr, *p1 = e ? ed->sketch().point(e->points[1]) : nullptr;
    const double len = p0 && p1 ? std::hypot(p1->x - p0->x, p1->y - p0->y) : -1;
    check(std::fabs(len - 30) < 1e-6, QString("open sketch re-solved with H = 30 (right line %1)").arg(len));
  }
  trigger(&w, "Sketch'i Bitir");
  check(waitFor([&] { return !w.inSketchMode(); }, 8000), "Sketch'i Bitir leaves sketch mode");
  pump(300);
  const std::string sk1 = firstSketchId(w);
  const pc::Sketch* s1 = docSketch(w, sk1);
  check(s1 != nullptr, "sketch feature committed (" + QString::fromStdString(sk1) + ")");
  check(s1 && hasDim(*s1, "40") && hasDim(*s1, "H"), "both dimensions survive closing the sketch");
  check(paramExpr(w, "H") == "30", "parameter H survives closing the sketch (table edit made while sketching kept)");
  check(w.document().params.find("H") && w.document().params.find("H")->exposed, "H keeps 'Uygulamada göster'");
  // QA finding: the finished sketch must stay visible in the 3D view (it used to be an empty viewport)
  check(w.sketchOverlayEdgeCount() == 4, QString("finished sketch drawn as wires in 3D (%1 edges)").arg(w.sketchOverlayEdgeCount()));
  check(w.sketchDimLabelCount() == 2, QString("3D overlay labels the sketch's dimensions (%1 labels)").arg(w.sketchDimLabelCount()));
  if (auto* ad = w.findChild<QAction*>("actSketchDims3D")) {
    ad->setChecked(false);
    check(w.sketchDimLabelCount() == 0, "Görünüm > Sketch ölçüleri (3B) hides the labels");
    ad->setChecked(true);
  } else check(false, "action actSketchDims3D exists");
  {
    const int dark = darkPixels(w);
    check(dark > 150, QString("3D viewport shows the sketch lines after Sketch'i Bitir (%1 dark px)").arg(dark));
    shot(&w, "v6_sketch_in_3d.png");
  }

  // re-open the sketch (double-click in the tree) and check the dims are shown, then leave with Vazgeç w/o changes
  {
    auto* tree = w.featureTree();
    QTreeWidgetItem* it = tree && tree->topLevelItemCount() ? tree->topLevelItem(0) : nullptr;
    check(it != nullptr, "sketch item in feature tree");
    if (it) {
      const QRect r = tree->visualItemRect(it);
      QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, r.center());   // a double-click follows a press
      QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, r.center());
    }
    check(waitFor([&] { return w.inSketchMode(); }, 4000), "double-click on the sketch re-opens the sketch editor");
    check(hasDim(w.sketchEditor()->sketch(), "40") && hasDim(w.sketchEditor()->sketch(), "H"), "re-opened sketch shows both dimensions");
    // add a point (a real change) then Vazgeç -> confirmation; answer No -> still sketching; then Yes -> discarded
    trigger(&w, "Nokta");
    clickAt(w.sketchEditor(), c + QPointF(40, 30));
    trigger(&w, "Seç");
    check(w.sketchEditor()->modified(), "sketch modified by a new point");
    g_boxes.clear();
    g_boxAnswer = QMessageBox::No;
    trigger(&w, "Vazgeç");
    pump(400);
    const bool modified = w.sketchEditor()->modified();
    if (modified) {
      check(!g_boxes.isEmpty(), "Vazgeç with changes asks for confirmation");
      check(w.inSketchMode(), "answering 'No' keeps the sketch open");
      g_boxAnswer = QMessageBox::Yes;
      trigger(&w, "Vazgeç");
      pump(300);
    }
    g_boxAnswer = QMessageBox::Yes;
    check(!w.inSketchMode(), "Vazgeç leaves the sketch");
    const pc::Sketch* s1b = docSketch(w, sk1);
    check(s1b && hasDim(*s1b, "40") && hasDim(*s1b, "H"), "committed dimensions untouched by Vazgeç");
  }

  // undo / redo of the sketch commit
  {
    const size_t nf = w.document().features.size();
    w.undoStack()->undo();
    pump(300);
    const bool undone = w.document().features.size() + 1 == nf || !docSketch(w, sk1);
    check(undone, "undo removes the sketch commit");
    w.undoStack()->redo();
    pump(300);
    const pc::Sketch* s1c = docSketch(w, sk1);
    check(s1c && hasDim(*s1c, "40") && hasDim(*s1c, "H"), "redo restores the sketch with its dimensions");
  }

  // ======================================================================= (2) extrude distance bound to a new parameter
  std::printf("== (2) Extrude: pick / create a parameter for the distance field\n");
  QPointer<pcd::FeatureDialog> dlg;
  QTimer::singleShot(0, &w, [&w] { trigger(&w, "Extrude..."); });
  check(waitFor([&] { return w.featureDialog() != nullptr; }, 4000), "Extrude dialog opened from the toolbar");
  dlg = w.featureDialog();
  pcd::ExprEdit* dist = nullptr;
  if (dlg) for (auto* e : dlg->findChildren<pcd::ExprEdit*>()) if (e->objectName() == "expr_distance") dist = e;
  check(dist != nullptr, "distance field found");
  QComboBox* pick = dist ? dist->findChild<QComboBox*>("paramPick") : nullptr;
  check(pick && pick->isVisible(), "distance field has a visible parameter picker combo");
  if (pick) {
    bool listsH = false;
    for (int i = 0; i < pick->count(); ++i) if (pick->itemText(i).startsWith("H = 30")) listsH = true;
    check(listsH, "picker lists existing parameters as 'name = value' (H = 30)");
    bool hasNew = false;
    for (int i = 0; i < pick->count(); ++i) if (pick->itemText(i).startsWith("Yeni parametre")) hasNew = true;
    check(hasNew, "picker offers 'Yeni parametre…'");
  }
  QToolButton* newBtn = dist ? dist->findChild<QToolButton*>("paramNew") : nullptr;
  check(newBtn && newBtn->isVisible() && newBtn->text().contains("Yeni"), "visible 'Yeni…' button next to the field");
  shot(dlg, "v6_extrude_dialog.png");
  // 'Yeni parametre…' -> dialog: name T, value 12, min 1, max 50, exposed, label, group
  bool sawNewDlg = false;
  g_onModal = [&](QWidget* m) {
    if (handleMessageBox(m)) return;
    if (!m->inherits("pcd::NewParameterDialog")) return;
    markHandled(m);
    sawNewDlg = true;
    QTimer::singleShot(80, m, [m] {
      auto le = [m](const char* n) { return m->findChild<QLineEdit*>(n); };
      pch::typeInto(le("npName"), "T", "new-parameter name");
      if (auto* ee = m->findChild<pcd::ExprEdit*>("npValue")) pch::typeInto(ee->lineEdit(), "12", "new-parameter value");
      pch::typeInto(le("npMin"), "1", "new-parameter min");
      pch::typeInto(le("npMax"), "50", "new-parameter max");
      if (auto* e = le("npLabel")) e->setText(QString::fromUtf8("Kalınlık"));   // QTest::keyClicks is ASCII-only
      pch::typeInto(le("npGroup"), "Boyutlar", "new-parameter group");
      pch::tickCheckBox(m->findChild<QCheckBox*>("npExposed"), true, "new-parameter 'Uygulamada göster'");
      shot(m, "v6_new_parameter_dialog.png");
      clickLater(childWithText<QPushButton>(m, "Oluştur"));
    });
  };
  if (newBtn) QTimer::singleShot(0, newBtn, [newBtn] { QTest::mouseClick(newBtn, Qt::LeftButton); });
  check(waitFor([&] { return sawNewDlg && !QApplication::activeModalWidget(); }, 45000), "'Yeni parametre' dialog shown and accepted");
  pump(300);
  check(dist && dist->text() == "T", "distance field bound to T (" + (dist ? dist->text() : QString()) + ")");
  check(pick && pick->currentText().startsWith("T = 12"), "picker shows the binding 'T = 12'");
  check(paramRow(w, "T") >= 0, "T appears in the parameter table immediately (dialog still open)");
  {
    const auto* T = w.document().params.find("T");
    check(T && T->expression == "12" && T->exposed && T->label == "Kalınlık" && T->group == "Boyutlar" && T->minValue == 1 && T->maxValue == 50 &&
              T->kind == pc::ParamKind::Length,
          "T stored with value/min/max/unit/exposed/label/group");
  }
  // picking an existing parameter from the combo
  pcd::ExprEdit* draft = nullptr;
  if (dlg) for (auto* e : dlg->findChildren<pcd::ExprEdit*>()) if (e->objectName() == "expr_draft") draft = e;
  if (draft) {
    auto* dp = draft->findChild<QComboBox*>("paramPick");
    int iH = dp ? dp->findData("H") : -1;
    check(iH >= 0, "draft picker lists H");
    if (iH >= 0) { dp->setCurrentIndex(iH); pump(50); }
    check(draft->text() == "H", "choosing H in the draft picker writes 'H' into the field");
    // then go back to a plain number by typing
    draft->lineEdit()->selectAll();
    QTest::keyClicks(draft->lineEdit(), "0");
    check(draft->text() == "0" && dp && dp->currentIndex() == 0, "typing a number unbinds (picker back to 'değer')");
  }
  if (dlg) {
    QPushButton* ok = childWithText<QPushButton>(dlg, "Oluştur");
    check(ok != nullptr, "dialog 'Oluştur' button");
    g_onModal = [](QWidget* m) { handleMessageBox(m); };
    if (ok) QTest::mouseClick(ok, Qt::LeftButton);
  }
  check(waitFor([&] { return w.featureDialog() == nullptr; }, 15000), "extrude committed, dialog closed");
  pump(400);
  std::string extId;
  for (const auto& ft : w.document().features) if (ft.type == pc::FeatureType::Extrude) extId = ft.id;
  check(!extId.empty() && w.document().feature(extId)->prop("distance") == "T", "Extrude.distance = T in the document");
  check(std::fabs(heightZ(w) - 12) < 1e-6, QString("body height = T = 12 (got %1)").arg(heightZ(w)));
  check(w.sketchOverlayEdgeCount() == 0, "sketch consumed by the extrude is hidden in 3D by default");
  w.setSketchShownIn3D(sk1, true);
  check(w.sketchOverlayEdgeCount() == 4, "tree toggle '3B görünümde göster' shows a consumed sketch");
  w.setSketchShownIn3D(sk1, false);

  // change T in the parameter table -> model regenerates
  check(setTableExpr(w, "T", "20"), "T edited in the parameter table");
  check(waitFor([&] { return paramExpr(w, "T") == "20" && std::fabs(heightZ(w) - 20) < 1e-6; }, 15000),
        QString("changing T to 20 changes the body height (got %1)").arg(heightZ(w)));

  // double-click the extrude in the tree -> editor shows the binding
  {
    auto* tree = w.featureTree();
    QTreeWidgetItem* it = nullptr;
    for (int i = 0; tree && i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString() == extId) it = tree->topLevelItem(i);
    if (it) {
      QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, tree->visualItemRect(it).center());
      QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, tree->visualItemRect(it).center());
    }
    check(waitFor([&] { return w.featureDialog() != nullptr; }, 4000), "double-click on the Extrude re-opens its dialog");
    pcd::ExprEdit* d2 = nullptr;
    if (w.featureDialog()) for (auto* e : w.featureDialog()->findChildren<pcd::ExprEdit*>()) if (e->objectName() == "expr_distance") d2 = e;
    auto* p2 = d2 ? d2->findChild<QComboBox*>("paramPick") : nullptr;
    check(d2 && d2->text() == "T" && p2 && p2->currentText().startsWith("T = 20"), "re-opened dialog shows the binding 'T = 20'");
    if (w.featureDialog()) {
      if (auto* c = childWithText<QPushButton>(w.featureDialog(), "Vazgeç")) QTest::mouseClick(c, Qt::LeftButton);
    }
    waitFor([&] { return w.featureDialog() == nullptr; }, 3000);
  }
  // tree context menu: "Parametreleri göster"
  {
    QStringList shown;
    {
      std::string tl;
      for (int i = 0; i < w.featureTree()->topLevelItemCount(); ++i) tl += w.featureTree()->topLevelItem(i)->text(0).toStdString() + " | ";
      check(QString::fromStdString(tl).contains("(T, join") || QString::fromStdString(tl).contains("(T, new"), "tree label shows the bound parameter: " + QString::fromStdString(tl));
    }
    const QString info = w.featureParameterSummary(QString::fromStdString(extId));
    check(info.contains("T"), "'Parametreleri göster' lists T for the extrude (" + info.simplified() + ")");
    const QString infoSk = w.featureParameterSummary(QString::fromStdString(sk1));
    check(infoSk.contains("H"), "'Parametreleri göster' lists H for the sketch");
  }

  // ======================================================================= save + reopen
  std::printf("== save + reopen\n");
  const QString pcad = tmp.path() + "/v6.pcad";
  {
    std::string err;
    check(pc::saveDocument(w.document(), pcad.toStdString(), &err), "document saved");
  }
  {
    pcd::MainWindow w2;
    w2.setAutoYes(true);
    w2.show();
    pump(200);
    check(w2.openFile(pcad), "reopened in a new window");
    pump(500);
    const pc::Sketch* s = docSketch(w2, sk1);
    check(s && hasDim(*s, "40") && hasDim(*s, "H"), "dimensions survive save/reopen");
    check(paramExpr(w2, "H") == "30" && paramExpr(w2, "T") == "20", "parameters survive save/reopen");
    check(std::fabs(heightZ(w2) - 20) < 1e-6, "reopened model height = 20");
    w2.close();
  }

  // ======================================================================= leaving a sketch via other paths
  std::printf("== other ways to leave an edited sketch\n");
  {
    // 'Kaydet' while sketching -> the sketch is committed first and the file contains the new dimension
    w.setAutoYes(true);
    g_onModal = [](QWidget* m) { if (!handleMessageBox(m)) handleDimDialog(m); };
    check(w.openFile(pcad), "work on the saved file (path set)");
    pump(400);
    w.enterSketchEdit(sk1);
    pump(200);
    auto* e2 = w.sketchEditor();
    trigger(&w, "Çember");
    clickAt(e2, c + QPointF(0, 10));
    clickAt(e2, c + QPointF(25, 10));
    trigger(&w, "Seç");
    int circ = -1;
    for (const auto& en : e2->sketch().entities) if (en.type == pc::SkEntityType::Circle) circ = en.id;
    check(circ >= 0, "circle drawn in the re-opened sketch");
    if (circ >= 0) {
      const auto* en = e2->sketch().entity(circ);
      const QPointF cc = e2->pointScreenPos(en->points[0]);
      const double rpx = (e2->pointScreenPos(en->points[0]) - c).manhattanLength() >= 0 ? 0 : 0;
      (void)rpx;
      // screen radius: map the sketch radius through two point positions of the rectangle (same scale)
      const auto* l = e2->sketch().entity(rect[0]);
      const auto *a0 = e2->sketch().point(l->points[0]), *a1 = e2->sketch().point(l->points[1]);
      const double scale = (e2->pointScreenPos(l->points[1]).x() - e2->pointScreenPos(l->points[0]).x()) / (a1->x - a0->x);
      clickAt(e2, QPointF(5, 5));
      clickAt(e2, cc + QPointF(en->radius * scale, 0));
      g_dimAnswers.push_back({"8", "D", false});
      const int before = g_dimDialogs;
      QTimer::singleShot(0, &w, [&w] { trigger(&w, "Çap"); });
      waitFor([&] { return g_dimDialogs > before && !QApplication::activeModalWidget(); }, 45000);   // > modal deadline
      pump(100);
    }
    check(hasDim(e2->sketch(), "D"), "diameter dimension D added");
    check(paramExpr(w, "D") == "8", "D in the document immediately");
    check(w.isWindowModified(), "window shows unsaved changes while the sketch has edits");
    trigger(&w, "Kaydet");
    pump(500);
    check(!w.inSketchMode(), "'Kaydet' commits the open sketch first");
    pc::Document d;
    check(pc::loadDocument(d, pcad.toStdString()), "saved file loads");
    const auto* fs = d.feature(sk1);
    check(fs && fs->sketch && hasDim(*fs->sketch, "D") && d.params.find("D"), "saved file contains the new dimension and parameter D");
    // 'Yeni' while sketching never leaves a stale sketch session behind
    w.enterSketchEdit(sk1);
    pump(200);
    trigger(&w, "Nokta");
    clickAt(w.sketchEditor(), c + QPointF(-60, -20));
    trigger(&w, "Seç");
    QTimer::singleShot(0, &w, [&w] { for (auto* a : w.findChildren<QAction*>()) if (a->text() == "Yeni" && a->isEnabled()) { a->trigger(); return; } });
    pump(600);
    check(!w.inSketchMode() && w.document().features.empty(), "'Yeni' while sketching: sketch resolved, empty new document");
    check(w.openFile(pcad), "re-open the saved file for Save-as-App");
    pump(500);
  }

  // ======================================================================= (3) Save as App: exposed params pre-ticked, app changes model
  std::printf("== (3) Save as App with exposed H and T\n");
  {
    const QString base = tmp.path() + "/V6App";
    bool sawDialog = false, finished = false, hTicked = false, tTicked = false;
    g_onModal = [&](QWidget* m) {
      if (handleMessageBox(m)) { if (g_boxes.last().startsWith("Uygulama kaydedildi")) finished = true; return; }
      if (!m->inherits("pc::appui::SaveAsAppDialog")) return;
      markHandled(m);
      pch::expectCloseWithin(m, 240000);   // builds the executable while the dialog is open
      sawDialog = true;
      if (auto* t = m->findChild<QTableWidget*>()) {
        for (int r = 0; r < t->rowCount(); ++r) {
          const QString n = t->item(r, 1) ? t->item(r, 1)->text() : QString();   // columns: show | name | label ...
          const bool ticked = t->item(r, 0) && t->item(r, 0)->checkState() == Qt::Checked;
          if (n == "H") hTicked = ticked;
          if (n == "T") tTicked = ticked;
        }
      }
      QList<QLineEdit*> edits;
      for (auto* e : m->findChildren<QLineEdit*>()) if (!(e->parent() && e->parent()->inherits("QAbstractSpinBox"))) edits << e;
      if (edits.size() >= 4) edits[3]->setText(base);
      QTimer::singleShot(200, m, [m] { clickLater(childWithText<QPushButton>(m, "Uygulamayı Kaydet")); });
    };
    QTimer::singleShot(0, &w, [&w] {
      for (auto* a : w.findChildren<QAction*>()) if (a->text().startsWith("Uygulama Olarak Kaydet")) { a->trigger(); return; }
    });
    check(waitFor([&] { return finished && !QApplication::activeModalWidget(); }, 180000 * pch::slowFactor()), "Save-as-App finished");
    check(sawDialog, "Save-as-App dialog shown");
    check(hTicked, "H (sketch dimension parameter, 'Uygulamada göster') pre-ticked in Save-as-App");
    check(tTicked, "T (extrude parameter, 'Uygulamada göster') pre-ticked in Save-as-App");
    g_onModal = nullptr;
#ifdef Q_OS_WIN
    const QString exe = base + ".exe";
#else
    const QString exe = base;
#endif
    check(QFileInfo::exists(exe), "app executable written");
    auto zOf = [](const QByteArray& so) {
      try {
        auto j = nlohmann::json::parse(so.toStdString());
        if (j.contains("bbox")) return j["bbox"]["zmax"].get<double>() - j["bbox"]["zmin"].get<double>();
      } catch (...) {}
      return -1.0;
    };
    QByteArray so;
    int rc = runProc(exe, {"--validate-only"}, &so);
    const double z0 = zOf(so);
    check(rc == 0 || rc == 2, QString("app --validate-only runs (rc %1)").arg(rc));
    rc = runProc(exe, {"--validate-only", "--set", "T=33"}, &so);
    const double z1 = zOf(so);
    check(std::fabs(z0 - 20) < 1e-3 && std::fabs(z1 - 33) < 1e-3, QString("app --set T=33 changes the bbox height (%1 -> %2)").arg(z0).arg(z1));
    rc = runProc(exe, {"--validate-only", "--set", "H=45"}, &so);
    try {
      auto j = nlohmann::json::parse(so.toStdString());
      const double ylen = j["bbox"]["ymax"].get<double>() - j["bbox"]["ymin"].get<double>();
      check(std::fabs(ylen - 45) < 1e-3, QString("app --set H=45 changes the sketch-driven width (%1)").arg(ylen));
    } catch (...) { check(false, "app JSON with bbox for H=45"); }
  }

  std::printf("%d failure(s)\n", g_fail);
  return g_fail;
}
