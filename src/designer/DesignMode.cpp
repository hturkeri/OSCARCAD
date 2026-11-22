// SC-UX shell (WP-A): design modes (SPEC §2.1), "Sketch düzlemi seç" state, view commands (Orient group,
// SPEC §2.12), the IDesignHost implementation (ToolApi.h) and the bridge that keeps the P0 stub ToolManager usable.
//
// Viewer API detection: WP-B adds ShapeViewer::setNavigationScheme / setLeftDragMode / setHomeView / setPlanView /
// previousView / nextView / fitToBox (SPEC §6.2, additive). This translation unit detects them at COMPILE time
// (SFINAE on pc::viewer::ShapeViewer) and calls them when present; until then a shell-side fallback is used
// (event-filter navigation remap, setView(Iso)+fitAll, axis-aligned plan view, no camera history).
#include "Icons.h"
#include "MainWindow.h"
#include "ExprEdit.h"
#include "OptionsPanel.h"
#include "Ribbon.h"
#include "StatusBarWidgets.h"
#include "ViewportHud.h"
#include "pc/Expression.h"
#include <BRepAdaptor_Surface.hxx>
#include <gp_Pln.hxx>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QStatusBar>
#include <QWheelEvent>
#include <type_traits>
#include <utility>

using namespace pc;

namespace pcd {

// ============================================================================ viewer API detection
namespace detect {
template <class V, class = void> struct navScheme : std::false_type {};
template <class V>
struct navScheme<V, std::void_t<decltype(std::declval<V&>().setNavigationScheme(V::NavigationScheme::SpaceClaim))>> : std::true_type {};
template <class V, class = void> struct dragMode : std::false_type {};
template <class V>
struct dragMode<V, std::void_t<decltype(std::declval<V&>().setLeftDragMode(V::DragMode::Spin))>> : std::true_type {};
template <class V, class = void> struct homeView : std::false_type {};
template <class V> struct homeView<V, std::void_t<decltype(std::declval<V&>().setHomeView())>> : std::true_type {};
template <class V, class = void> struct history : std::false_type {};
template <class V>
struct history<V, std::void_t<decltype(std::declval<V&>().previousView()), decltype(std::declval<V&>().nextView())>> : std::true_type {};
template <class V, class = void> struct planView : std::false_type {};
template <class V>
struct planView<V, std::void_t<decltype(std::declval<V&>().setPlanView(std::declval<const double*>(), std::declval<const double*>(),
                                                                       std::declval<const double*>(), true))>> : std::true_type {};

template <class V> bool applyNavigationScheme(V* v) {
  if constexpr (navScheme<V>::value) { v->setNavigationScheme(V::NavigationScheme::SpaceClaim); return true; }
  else { (void)v; return false; }
}
template <class V> bool applyLeftDrag(V* v, int m) {
  if constexpr (dragMode<V>::value) {
    using D = typename V::DragMode;
    v->setLeftDragMode(m == 1 ? D::Spin : m == 2 ? D::Pan : m == 3 ? D::Zoom : D::None);
    return true;
  } else { (void)v; (void)m; return false; }
}
template <class V> bool home(V* v) {
  if constexpr (homeView<V>::value) { v->setHomeView(); return true; }
  else { (void)v; return false; }
}
template <class V> int stepHistory(V* v, bool back) {   // -1 = API missing, 0 = nothing to go to, 1 = done
  if constexpr (history<V>::value) return (back ? v->previousView() : v->nextView()) ? 1 : 0;
  else { (void)v; (void)back; return -1; }
}
template <class V> bool plan(V* v, const double o[3], const double u[3], const double vv[3]) {
  if constexpr (planView<V>::value) { v->setPlanView(o, u, vv, true); return true; }
  else { (void)v; (void)o; (void)u; (void)vv; return false; }
}
} // namespace detect

bool MainWindow::detectNavigationScheme() { return detect::applyNavigationScheme(viewer_); }

// ============================================================================ modes
DesignMode MainWindow::designMode() const {
  if (sketching_) return DesignMode::Sketch;
  if (section_ && section_->sectionOn()) return DesignMode::Section;
  return DesignMode::ThreeD;
}

void MainWindow::syncModeActions() {
  const DesignMode m = designMode();
  for (auto [a, on] : {std::pair{actModeSketch_, m == DesignMode::Sketch || planePick_}, std::pair{actMode3D_, m == DesignMode::ThreeD && !planePick_}}) {
    if (!a) continue;
    const QSignalBlocker b(a);
    a->setChecked(on);
  }
  if (actModeSection_) {
    const QSignalBlocker b(actModeSection_);
    actModeSection_->setChecked(m == DesignMode::Section);
  }
}

bool MainWindow::sketchPlaneFromSelection(SketchPlane* out, std::string* editId, std::string* datumId) const {
  // The SelectionModel first (tools / panels), then the legacy picks (face / datum clicked without a tool).
  QList<SelItem> items = selection_->items();
  for (auto i = items.size() - 1; i >= 0; --i) {
    const SelItem& s = items[i];
    switch (s.kind) {
      case SelItem::Kind::Face:
        if (isPlanarFace(s.name)) { out->kind = SketchPlaneKind::OnFace; out->faceRef = s.name.toStdString(); return true; }
        break;
      case SelItem::Kind::Datum: *datumId = s.name.toStdString(); return true;
      case SelItem::Kind::Sketch: *editId = s.name.toStdString(); return true;
      case SelItem::Kind::SketchRegion:
      case SelItem::Kind::SketchCurve:
      case SelItem::Kind::SketchPoint: *editId = s.name.section(':', 1, 1).toStdString(); return true;
      case SelItem::Kind::Feature: {
        const Feature* f = doc_.feature(s.name.toStdString());
        if (f && f->type == FeatureType::Sketch) { *editId = f->id; return true; }
        if (f && f->type == FeatureType::Plane) { *datumId = f->id; return true; }
        break;
      }
      default: break;
    }
  }
  return false;
}

bool MainWindow::setDesignMode(DesignMode m) {
  // a busy tool (drag, dimension entry) is cancelled first (Esc meaning, SPEC §2.1)
  if (tools_ && tools_->active() && tools_->active()->busy()) tools_->active()->cancel();
  switch (m) {
    case DesignMode::Sketch: {
      if (sketching_) { sketchEd_->setFocus(); syncModeActions(); return true; }
      if (featDlg_) featDlg_->reject();
      if (section_->sectionOn()) section_->setSectionOn(false);
      SketchPlane pl;
      std::string editId, datumId;
      if (sketchPlaneFromSelection(&pl, &editId, &datumId)) {
        cancelPlanePick();
        if (!editId.empty()) return editSketch(editId);
        if (!datumId.empty()) { beginSketchOnPlane(datumId); return sketching_; }
        return beginSketch(pl);
      }
      if (doc_.features.empty()) {   // empty document: XY at once (SPEC §2.1)
        cancelPlanePick();
        pl.kind = SketchPlaneKind::XY;
        return beginSketch(pl);
      }
      return beginSketchPlanePick();
    }
    case DesignMode::Section: {
      if (sketching_ && !finishSketch(true)) { syncModeActions(); return false; }
      cancelPlanePick();
      if (section_->sectionOn()) { section_->setSectionOn(false); notifyDesignMode(); return true; }   // X again = off
      // a selected datum plane / planar face defines the section plane, else the XZ preset
      double o[3] = {0, 0, 0}, n[3] = {0, 0, 0};
      bool custom = false;
      for (const SelItem& s : selection_->items()) {
        if (s.kind == SelItem::Kind::Datum || (s.kind == SelItem::Kind::Feature && model_.datumPlanes.count(s.name.toStdString()))) {
          auto it = model_.datumPlanes.find(s.name.toStdString());
          if (it == model_.datumPlanes.end()) continue;
          const gp_Pnt p = it->second.frame.Location();
          const gp_Dir d = it->second.frame.Direction();
          o[0] = p.X(); o[1] = p.Y(); o[2] = p.Z(); n[0] = d.X(); n[1] = d.Y(); n[2] = d.Z();
          custom = true;
        } else if (s.kind == SelItem::Kind::Face && isPlanarFace(s.name)) {
          try {
            const TopoDS_Face& f = model_.faces.at(s.name.toStdString());
            BRepAdaptor_Surface sf(f);
            const gp_Pln pln = sf.Plane();
            gp_Dir d = pln.Axis().Direction();
            if (f.Orientation() == TopAbs_REVERSED) d.Reverse();
            const gp_Pnt p = pln.Location();
            // keep the material behind the face: the kept half-space is (p - o).n >= 0, so use -normal
            o[0] = p.X(); o[1] = p.Y(); o[2] = p.Z(); n[0] = -d.X(); n[1] = -d.Y(); n[2] = -d.Z();
            custom = true;
          } catch (...) {}
        }
        if (custom) break;
      }
      section_->setSectionOn(true);
      if (custom) {
        viewer_->setSectionPlane(true, o, n);
        statusBar()->showMessage("Kesit modu: seçili düzlemden kesit (yalnızca görünüm; X ya da D ile kapanır)", 6000);
      }
      notifyDesignMode();
      return true;
    }
    case DesignMode::ThreeD: {
      if (planePick_) cancelPlanePick("Sketch düzlemi seçimi iptal edildi.");
      if (sketching_ && !finishSketch(true)) { syncModeActions(); return false; }
      if (section_->sectionOn()) section_->setSectionOn(false);
      if (tools_) tools_->activate("select");
      notifyDesignMode();
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------- "Sketch düzlemi seç"
bool MainWindow::beginSketchPlanePick() {
  if (sketching_ && !finishSketch(true)) return false;
  if (featDlg_) featDlg_->reject();
  planePick_ = true;
  planePickHoverFace_.clear();
  selection_->clear();
  applyDatumPlanes();   // adds the XY / XZ / YZ origin squares
  syncModeActions();
  statusBar()->showMessage("Sketch düzlemi seç: düzlemsel bir yüze, bir datum düzlemine ya da orijindeki XY / XZ / YZ karelerinden "
                           "birine tıklayın (Esc ya da D: iptal).");
  notifyDesignMode();
  return true;
}

void MainWindow::cancelPlanePick(const QString& msg) {
  if (!planePick_) return;
  planePick_ = false;
  planePickHoverFace_.clear();
  viewer_->setSketchPlane(false);
  applyDatumPlanes();
  syncModeActions();
  if (!msg.isEmpty()) statusBar()->showMessage(msg, 4000);
}

void MainWindow::updatePlanPreviewHover(const QString& face) {
  if (!planePick_ || face == planePickHoverFace_) return;
  planePickHoverFace_ = face;
  if (face.isEmpty() || !isPlanarFace(face)) { viewer_->setSketchPlane(false); return; }
  try {
    const TopoDS_Face& f = model_.faces.at(face.toStdString());
    const gp_Pln pln = BRepAdaptor_Surface(f).Plane();
    const gp_Ax3 ax = pln.Position();
    const gp_Pnt o = ax.Location();
    const gp_Dir u = ax.XDirection(), v = ax.YDirection();
    const double O[3] = {o.X(), o.Y(), o.Z()}, U[3] = {u.X(), u.Y(), u.Z()}, V[3] = {v.X(), v.Y(), v.Z()};
    viewer_->setSketchPlane(true, O, U, V);
  } catch (...) { viewer_->setSketchPlane(false); }
}

bool MainWindow::openSketchFrame(double origin[3], double u[3], double v[3], double normal[3]) const {
  if (!sketching_) return false;
  try {
    Document cand = sketchCandidate();
    Feature f;
    f.id = editSketchId_;
    f.type = FeatureType::Sketch;
    f.sketch = sketchEd_->sketch();
    if (Feature* ex = cand.feature(editSketchId_)) *ex = f;
    else cand.features.push_back(f);
    gp_Ax3 ax;
    if (!model::sketchFrame(cand, editSketchId_, cand.evaluateParams(), model_, ax)) return false;
    const gp_Pnt o = ax.Location();
    const gp_Dir X = ax.XDirection(), Y = ax.YDirection(), N = ax.Direction();
    const double vals[4][3] = {{o.X(), o.Y(), o.Z()}, {X.X(), X.Y(), X.Z()}, {Y.X(), Y.Y(), Y.Z()}, {N.X(), N.Y(), N.Z()}};
    for (int i = 0; i < 3; ++i) { origin[i] = vals[0][i]; u[i] = vals[1][i]; v[i] = vals[2][i]; normal[i] = vals[3][i]; }
    return true;
  } catch (...) {
    return false;
  }
}

// ============================================================================ IDesignHost
Document* MainWindow::expressionContext() {
  if (sketching_) return &work_;
  if (featDlg_ && dlgWork_) return dlgWork_;
  exprWork_ = doc_;   // a copy: parameters created through ExprEdit reach doc_ via the commit hook
  return &exprWork_;
}

bool MainWindow::editSketch(const std::string& id) {
  if (sketching_) {
    if (editSketchId_ == id) return true;
    if (!finishSketch(true)) return false;
  }
  cancelPlanePick();
  enterSketchEdit(id);
  return sketching_ && editSketchId_ == id;
}

bool MainWindow::commitDocument(const Document& cand, const QString& undoText) {
  if (featDlg_) {   // the open dialog's OK would overwrite this change with its own snapshot (see showEdgeMenu)
    statusBar()->showMessage("Önce açık özellik diyaloğunu kapatın.", 6000);
    return false;
  }
  if (rollback_ >= int(cand.features.size())) rollback_ = -1;
  return tryCommit(cand, undoText);
}

bool MainWindow::insertFeature(const Feature& f, const QString& undoText) { return insertFeatureCommit(f, undoText); }

bool MainWindow::changeParameter(const std::string& name, const std::string& expr) {
  const Parameter* p = doc_.params.find(name);
  if (!p) return false;
  if (p->expression == expr) return true;
  return tryParameterChange(name, expr);
}

bool MainWindow::deleteFeatureInteractive(const std::string& id) {
  if (!doc_.feature(id)) return false;
  const size_t before = doc_.features.size();
  deleteFeature(id);
  return doc_.features.size() < before;
}

bool MainWindow::setFeatureSuppressed(const std::string& id, bool suppressed) {
  const Feature* f = doc_.feature(id);
  if (!f) return false;
  if (f->suppressed == suppressed) return true;
  toggleSuppress(id);
  const Feature* g = doc_.feature(id);
  return g && g->suppressed == suppressed;
}

model::ModelResult MainWindow::evaluateCandidate(const Document& cand) const {
  try {
    const int extra = int(cand.features.size()) - int(doc_.features.size());
    const int stop = rollback_ >= 0 ? std::max(-1, rollback_ + std::max(0, extra)) : -1;
    return model::regenerate(cand, cand.evaluateParams(), stop);
  } catch (...) {
    return model::ModelResult();
  }
}

bool MainWindow::evaluateExpression(const QString& expr, double* value, QString* error) const {
  const Document* ctx = sketching_ ? &work_ : (featDlg_ && dlgWork_) ? dlgWork_ : &doc_;
  try {
    const ParamValues vals = ctx->evaluateParams();
    const double v = evalWith(expr.toStdString(), vals);
    if (value) *value = v;
    return true;
  } catch (const std::exception& e) {
    if (error) *error = QString::fromUtf8(e.what());
  } catch (...) {
    if (error) *error = "geçersiz ifade";
  }
  return false;
}

QString MainWindow::createParameterInteractive(const QString& suggestedName, ParamKind kind, const QString& valueExpr, QWidget* parent) {
  Document* ctx = expressionContext();
  QString name = suggestedName;
  for (int i = 2; ctx->params.find(name.toStdString()); ++i) name = QString("%1_%2").arg(suggestedName).arg(i);
  NewParameterDialog dlg(ctx, name, kind, valueExpr, parent ? parent : this);
  if (dlg.exec() != QDialog::Accepted) return QString();
  const Parameter p = dlg.parameter();
  if (!ctx->params.find(p.name)) ctx->params.add(p);
  onParameterCreated(p);
  if (!doc_.params.find(p.name)) return QString();
  selectParamRow(QString::fromStdString(p.name));
  return QString::fromStdString(p.name);
}

void MainWindow::setGhost(const TopoDS_Shape& s) {
  if (s.IsNull()) viewer_->clearGhost();
  else viewer_->setGhost(s);
}

void MainWindow::showPrompt(const QString& text, int ms) { statusBar()->showMessage(text, ms); }
void MainWindow::showQuickMeasure(const QString& text) { if (sbw_) sbw_->setQuickMeasure(text); }

// [SC23] P0 minimal feedback (ANALYSIS §4.5, §5.3 #1; WP-O refines it): Invalid / a reason goes to the status bar and
// the viewport HUD. Invalid = red and persistent until the next status that is not Invalid; Completed / Cancelled
// are momentary (2 s); other states with a reason are informational.
void MainWindow::showOperationFeedback(const OpStatus& st) {
  const bool invalid = st.state == OpState::Invalid;
  const QString text = st.reason.isEmpty() && invalid ? opStateText(st.state) : st.reason;
  if (invalid || !st.reason.isEmpty()) statusBar()->showMessage(text);
  if (!hud_) return;
  if (invalid) hud_->showMessage(text, ViewportHud::Severity::Error);
  else if (!st.reason.isEmpty()) {
    const bool momentary = st.state == OpState::Completed || st.state == OpState::Cancelled;
    hud_->showMessage(text, ViewportHud::Severity::Info, momentary ? 2000 : 0);
  } else if (hud_->messageSeverity() == ViewportHud::Severity::Error) hud_->clearMessage();   // next action
}

void MainWindow::setToolEmphasis(const QStringList& faces, const QStringList& edges) {
  toolEmphFaces_ = faces;
  toolEmphEdges_ = edges;
  highlightSelection();
}

// ============================================================================ tools (ToolManager) glue
void MainWindow::onToolChanged(const QString& id) {
  refreshOptionsPanel();
  if (tools_ && tools_->active()) {
    const QString p = tools_->active()->prompt();
    if (!p.isEmpty()) statusBar()->showMessage(p);
  }
  if (!toolsReal_) bridgeStubTool(id);
}

// P0 stub bridge: while WP-B's ToolManager is the stub (no IActiveTool objects), the sketch tool actions drive the
// SketchEditor directly, exactly like the removed sketch toolbar did. Inert as soon as tools_->active() != nullptr.
void MainWindow::bridgeStubTool(const QString& id) {
  if (bridgingTool_ || toolsReal_) return;
  static const QHash<QString, SketchEditor::Tool> map = {
      {"select", SketchEditor::Tool::Select}, {"sketch.select", SketchEditor::Tool::Select}, {"sketch.line", SketchEditor::Tool::Line},
      {"sketch.rect", SketchEditor::Tool::Rect}, {"sketch.circle", SketchEditor::Tool::Circle}, {"sketch.arc", SketchEditor::Tool::Arc},
      {"sketch.point", SketchEditor::Tool::Point}};
  if (map.contains(id)) {
    if (!sketching_ && id.startsWith("sketch.")) {
      // SpaceClaim: a sketch tool in 3D mode enters Sketch mode (SPEC §2.1 [U])
      setDesignMode(DesignMode::Sketch);
    }
    if (sketching_) {
      bridgingTool_ = true;
      sketchEd_->setTool(map.value(id));
      bridgingTool_ = false;
    }
    return;
  }
  static const QHash<QString, QString> titles = {{"pull", "Çek (Pull)"}, {"move", "Taşı (Move)"}, {"fill", "Doldur (Fill)"},
                                                 {"combine", "Birleştir (Combine)"}, {"plane", "Düzlem (Plane)"},
                                                 {"shell", "Kabuk (Shell)"}, {"sketch.trim", "Kırp (Trim Away)"}};
  if (titles.contains(id))
    statusBar()->showMessage(QString("%1: bu derlemede araç bağlı değil (WP-B). Klasik komutlar: Oluştur > Klasik.").arg(titles.value(id)), 6000);
}

void MainWindow::refreshOptionsPanel() {
  if (!options_ || !tools_) return;
  static const QHash<QString, QString> fallbackTitles = {
      {"select", "Seç (Select)"}, {"sketch.select", "Seç (Select)"}, {"pull", "Çek (Pull)"}, {"move", "Taşı (Move)"},
      {"fill", "Doldur (Fill)"}, {"combine", "Birleştir (Combine)"}, {"plane", "Düzlem (Plane)"}, {"shell", "Kabuk (Shell)"},
      {"sketch.line", "Çizgi (Line)"}, {"sketch.rect", "Dikdörtgen (Rectangle)"}, {"sketch.circle", "Çember (Circle)"},
      {"sketch.arc", "Yay (Sweep Arc)"}, {"sketch.point", "Nokta (Point)"}, {"sketch.trim", "Kırp (Trim Away)"}};
  IActiveTool* t = tools_->active();
  const QString title = t ? t->title() : fallbackTitles.value(tools_->activeId(), tools_->activeId());
  if (t) {
    if (QWidget* custom = tools_->createOptionsWidget(options_)) options_->showCustom(title, custom);
    else options_->showOptions(title, t->options());
  } else {
    // stub: the selection filter is the one option the shell itself can offer (SPEC §8 "Seç")
    QList<ToolOption> opts;
    if (tools_->activeId() == "select" || tools_->activeId() == "sketch.select") {
      ToolOption f;
      f.key = "filter";
      f.label = "Seçim filtresi";
      f.group = "Seçim";
      f.type = ToolOption::Type::Choice;
      f.choices = {"smart|Akıllı", "face|Yüz", "edge|Kenar", "body|Gövde", "datum|Düzlem", "sketch|Sketch"};
      static const char* keys[] = {"smart", "face", "edge", "body", "datum", "sketch"};
      f.value = QString(keys[int(selection_->filter())]);
      opts << f;
    }
    options_->showOptions(title, opts);
  }
}

// ============================================================================ view commands (Orient group)
void MainWindow::homeView() {
  if (sketching_ && !sketchEd_->attachedViewer()) { sketchEd_->fitView(); return; }
  if (!detect::home(viewer_)) { viewer_->setView(viewer::ShapeViewer::View::Iso); viewer_->fitAll(); }
  statusBar()->showMessage("Ana görünüm (Home)", 2500);
}

void MainWindow::planView() {
  double o[3], u[3], v[3], n[3];
  bool have = false;
  if (sketching_ && sketchEd_->attachedViewer()) {   // overlay: WP-B's plan view (v up, keeps the scale if empty)
    syncSketchFrame();
    sketchEd_->planView();
    statusBar()->showMessage("Plan görünümü (Plan View)", 2500);
    return;
  }
  if (sketching_) have = openSketchFrame(o, u, v, n);
  if (!have) {
    for (const SelItem& s : selection_->items()) {
      if (s.kind == SelItem::Kind::Face && isPlanarFace(s.name)) {
        try {
          const TopoDS_Face& f = model_.faces.at(s.name.toStdString());
          const gp_Ax3 ax = BRepAdaptor_Surface(f).Plane().Position();
          gp_Dir N = ax.Direction(), X = ax.XDirection();
          if (f.Orientation() == TopAbs_REVERSED) N.Reverse();
          const gp_Dir Y = N.Crossed(X);
          const gp_Pnt p = ax.Location();
          const double vals[4][3] = {{p.X(), p.Y(), p.Z()}, {X.X(), X.Y(), X.Z()}, {Y.X(), Y.Y(), Y.Z()}, {N.X(), N.Y(), N.Z()}};
          for (int i = 0; i < 3; ++i) { o[i] = vals[0][i]; u[i] = vals[1][i]; v[i] = vals[2][i]; n[i] = vals[3][i]; }
          have = true;
        } catch (...) {}
      } else if (s.kind == SelItem::Kind::Datum || s.kind == SelItem::Kind::Feature) {
        auto it = model_.datumPlanes.find(s.name.toStdString());
        if (it == model_.datumPlanes.end()) continue;
        const gp_Ax3& ax = it->second.frame;
        const gp_Pnt p = ax.Location();
        const gp_Dir X = ax.XDirection(), Y = ax.YDirection(), N = ax.Direction();
        const double vals[4][3] = {{p.X(), p.Y(), p.Z()}, {X.X(), X.Y(), X.Z()}, {Y.X(), Y.Y(), Y.Z()}, {N.X(), N.Y(), N.Z()}};
        for (int i = 0; i < 3; ++i) { o[i] = vals[0][i]; u[i] = vals[1][i]; v[i] = vals[2][i]; n[i] = vals[3][i]; }
        have = true;
      }
      if (have) break;
    }
  }
  if (!have) { statusBar()->showMessage("Plan görünümü: düzlemsel bir yüz, datum düzlemi seçin ya da sketch modunda kullanın (V).", 5000); return; }
  if (sketching_) sketchEd_->fitView();
  if (detect::plan(viewer_, o, u, v)) return;
  // fallback: nearest axis-aligned standard view (exact for XY / XZ / YZ planes)
  using V = viewer::ShapeViewer::View;
  const double ax = std::fabs(n[0]), ay = std::fabs(n[1]), az = std::fabs(n[2]);
  V view = V::Iso;
  if (az >= ax && az >= ay) view = n[2] > 0 ? V::Top : V::Bottom;
  else if (ay >= ax) view = n[1] < 0 ? V::Front : V::Back;
  else view = n[0] > 0 ? V::Right : V::Left;
  viewer_->setView(view);
  if (ax > 0.999 || ay > 0.999 || az > 0.999) statusBar()->showMessage("Plan görünümü (Plan View)", 2500);
  else statusBar()->showMessage("Plan görünümü: eğik düzlem için en yakın standart görünüm kullanıldı (viewer setPlanView API'si bekleniyor).", 5000);
}

void MainWindow::zoomExtents() {
  if (sketching_ && (!sketchEd_->attachedViewer() || model_.shape.IsNull())) sketchEd_->fitView();
  else viewer_->fitAll();
}

void MainWindow::zoomStep(int dir) {
  QWidget* target = sketching_ ? static_cast<QWidget*>(sketchEd_) : static_cast<QWidget*>(viewer_);
  const QPointF c(target->width() / 2.0, target->height() / 2.0);
  QWheelEvent we(c, target->mapToGlobal(c), QPoint(), QPoint(0, dir > 0 ? 120 : -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  navSynth_ = true;
  QCoreApplication::sendEvent(target, &we);
  navSynth_ = false;
}

void MainWindow::previousView() {
  const int r = detect::stepHistory(viewer_, true);
  if (r < 0) statusBar()->showMessage("Önceki görünüm: kamera geçmişi henüz yok (viewer API'si bekleniyor).", 4000);
  else if (r == 0) statusBar()->showMessage("Önceki görünüm yok.", 2500);
}

void MainWindow::nextView() {
  const int r = detect::stepHistory(viewer_, false);
  if (r < 0) statusBar()->showMessage("Sonraki görünüm: kamera geçmişi henüz yok (viewer API'si bekleniyor).", 4000);
  else if (r == 0) statusBar()->showMessage("Sonraki görünüm yok.", 2500);
}

void MainWindow::setLeftDragMode(int m) {
  leftDrag_ = m;
  for (auto [a, k] : {std::pair{actSpin_, 1}, std::pair{actPan_, 2}, std::pair{actZoomDrag_, 3}}) {
    if (!a) continue;
    const QSignalBlocker b(a);
    a->setChecked(m == k);
  }
  detect::applyLeftDrag(viewer_, m);
  static const char* names[] = {"Seç", "Çevir (Spin)", "Kaydır (Pan)", "Yakınlaştır (Zoom)"};
  statusBar()->showMessage(m == 0 ? QString("Sol sürükleme: gezinme yok (orta düğme = çevir)") : QString("Sol sürükleme: %1").arg(names[m]), 3000);
}

// SPEC §2.12 navigation on the Legacy viewer, until WP-B's NavigationScheme::SpaceClaim exists:
//   middle drag = spin (sent to the viewer as a left drag), Shift+middle = pan (the viewer's own middle drag),
//   Ctrl+middle up/down = zoom about the centre (wheel steps), wheel = zoom to the cursor (unchanged),
//   left drag in empty space = nothing unless Çevir / Kaydır / Yakınlaştır is active; Shift+left pan removed.
bool MainWindow::navigationFilter(QEvent* e) {
  if (!navFallback_ || navSynth_) return false;
  auto send = [this](QEvent::Type t, const QPointF& p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m) {
    QMouseEvent me(t, p, viewer_->mapToGlobal(p), b, bs, m);
    navSynth_ = true;
    QCoreApplication::sendEvent(viewer_, &me);
    navSynth_ = false;
  };
  auto wheel = [this](int dy) {
    const QPointF c(viewer_->width() / 2.0, viewer_->height() / 2.0);
    QWheelEvent we(c, viewer_->mapToGlobal(c), QPoint(), QPoint(0, dy), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    navSynth_ = true;
    QCoreApplication::sendEvent(viewer_, &we);
    navSynth_ = false;
  };
  switch (e->type()) {
    case QEvent::MouseButtonPress: {
      auto* me = static_cast<QMouseEvent*>(e);
      navPress_ = navLast_ = me->pos();
      navMoved_ = false;
      if (me->button() == Qt::MiddleButton) {
        if (me->modifiers() & Qt::ControlModifier) { navMode_ = 3; return true; }
        if (me->modifiers() & Qt::ShiftModifier) { navMode_ = 2; return false; }   // viewer: middle = pan
        navMode_ = 1;
        send(QEvent::MouseButtonPress, me->position(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        return true;
      }
      if (me->button() == Qt::LeftButton) {
        if (leftDrag_ == 1) { navMode_ = 1; send(QEvent::MouseButtonPress, me->position(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier); return true; }
        if (leftDrag_ == 2) { navMode_ = 2; send(QEvent::MouseButtonPress, me->position(), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier); return true; }
        if (leftDrag_ == 3) { navMode_ = 3; return true; }
        navMode_ = 4;   // plain left: press goes through (click = pick), drags are swallowed
        return false;
      }
      navMode_ = 0;
      return false;
    }
    case QEvent::MouseMove: {
      auto* me = static_cast<QMouseEvent*>(e);
      if (me->buttons() == Qt::NoButton || navMode_ == 0) return false;
      const QPoint d = me->pos() - navLast_;
      if ((me->pos() - navPress_).manhattanLength() > 4) navMoved_ = true;
      if (navMode_ == 1) {
        send(QEvent::MouseMove, me->position(), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        navLast_ = me->pos();
        return true;
      }
      if (navMode_ == 2) {
        if (me->buttons() & Qt::MiddleButton && !(me->buttons() & Qt::LeftButton)) return false;   // real middle (Shift) pan
        send(QEvent::MouseMove, me->position(), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        navLast_ = me->pos();
        return true;
      }
      if (navMode_ == 3) {
        if (std::abs(d.y()) >= 3) {
          wheel(d.y() < 0 ? 40 * std::max(1, std::abs(d.y()) / 3) : -40 * std::max(1, std::abs(d.y()) / 3));
          navLast_ = me->pos();
        }
        return true;
      }
      if (navMode_ == 4) return navMoved_;   // swallow left drags (no rotation in empty space)
      return false;
    }
    case QEvent::MouseButtonRelease: {
      auto* me = static_cast<QMouseEvent*>(e);
      const int mode = navMode_;
      navMode_ = 0;
      if (mode == 1) {
        // release with a non-left button: the viewer ends its drag without treating it as a pick
        send(QEvent::MouseButtonRelease, me->position(), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        return true;
      }
      if (mode == 2 && me->button() == Qt::LeftButton) {
        send(QEvent::MouseButtonRelease, me->position(), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        return true;
      }
      if (mode == 3) return true;
      if (mode == 4 && navMoved_) {
        send(QEvent::MouseButtonRelease, me->position(), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        return true;
      }
      return false;
    }
    default: return false;
  }
}

} // namespace pcd
