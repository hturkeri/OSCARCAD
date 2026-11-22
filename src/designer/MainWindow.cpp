#include "MainWindow.h"
#include "Import2DUi.h"   // SC-2D-IMPORT
#include "ParamSelectUi.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <QSignalBlocker>
#include "Icons.h"
#include "OptionsPanel.h"
#include "PropertiesPanel.h"
#include "Ribbon.h"
#include "Shortcuts.h"
#include "StatusBarWidgets.h"
#include "StructurePanel.h"
#include "DocumentTabBar.h"
#include "ViewsPanel.h"
#include "MfgDialogs.h"
#include "pc/SaveAsAppDialog.h"
#include <cmath>
#include <algorithm>
#include <QVector3D>
#include "pc/Expression.h"
#include "pc/NamedSelections.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QComboBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QHBoxLayout>
#include <nlohmann/json.hpp>

using namespace pc;

namespace pcd {

// ------------------------------------------------------------------------------ undo command
class DocCommand : public QUndoCommand {
public:
  DocCommand(MainWindow* w, Document before, Document after, const QString& text)
      : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)) {}
  void redo() override {
    w_->applyDocument(after_, first_);
    first_ = false;
  }
  void undo() override { w_->applyDocument(before_, false); }
private:
  MainWindow* w_;
  Document before_, after_;
  bool first_ = true;
};

namespace {
QString kindText(ParamKind k) {
  switch (k) {
    case ParamKind::Length: return "mm";
    case ParamKind::Angle: return "°";
    case ParamKind::Count: return "adet";
    default: return "-";
  }
}
QString featureSummary(const Feature& f) {
  switch (f.type) {
    case FeatureType::Sketch: {
      if (!f.sketch) return "";
      if (f.sketch->plane.kind == SketchPlaneKind::OnPlane) return QString(" [düzlem: %1]").arg(QString::fromStdString(f.sketch->plane.planeRef));
      const char* pl = f.sketch->plane.kind == SketchPlaneKind::XY ? "XY" : f.sketch->plane.kind == SketchPlaneKind::XZ ? "XZ" : f.sketch->plane.kind == SketchPlaneKind::YZ ? "YZ" : "yüz";
      return QString(" [%1]").arg(pl);
    }
    case FeatureType::Extrude: {
      QString r = QString("  (%1, %2").arg(QString::fromStdString(f.prop("distance")), QString::fromStdString(f.prop("op", "new")));
      if (!f.prop("bodyName").empty()) r += " -> " + QString::fromStdString(f.prop("bodyName"));
      return r + ")";
    }
    case FeatureType::Plane: return QString("  (%1)").arg(QString::fromStdString(f.prop("kind", "edgeNormal")));
    case FeatureType::Combine: return QString("  (%1: %2 <- %3)").arg(QString::fromStdString(f.prop("op")), QString::fromStdString(f.prop("target")), QString::fromStdString(f.prop("tools")));
    case FeatureType::Revolve: return QString("  (%1°)").arg(QString::fromStdString(f.prop("angle", "360")));
    case FeatureType::Hole: return QString("  (Ø %1)").arg(QString::fromStdString(f.prop("diameter")));
    case FeatureType::Fillet: return QString("  (r = %1, %2 kenar)").arg(QString::fromStdString(f.prop("radius"))).arg(f.refs.size());
    case FeatureType::Chamfer: return QString("  (d = %1, %2 kenar)").arg(QString::fromStdString(f.prop("distance"))).arg(f.refs.size());
    case FeatureType::Shell: return QString("  (t = %1)").arg(QString::fromStdString(f.prop("thickness")));
    case FeatureType::Pattern: return QString("  (%1 x %2)").arg(QString::fromStdString(f.prop("kind")), QString::fromStdString(f.prop("count")));
    case FeatureType::Transform: return QString("  (%1)").arg(QString::fromStdString(f.prop("bodies", "*")));
  }
  return "";
}
QString typeIconName(FeatureType t) {
  switch (t) {
    case FeatureType::Sketch: return "sketch";
    case FeatureType::Extrude: return "extrude";
    case FeatureType::Revolve: return "revolve";
    case FeatureType::Hole: return "hole";
    case FeatureType::Fillet: return "fillet";
    case FeatureType::Chamfer: return "chamfer";
    case FeatureType::Shell: return "shell";
    case FeatureType::Plane: return "plane";
    case FeatureType::Combine: return "combine";
    default: return "pattern";
  }
}
} // namespace

// ------------------------------------------------------------------------------ construction
// SC-UX (WP-A): the window is the SpaceClaim-like shell. Actions / menus / ribbon / docks / status bar are built in
// MainWindowShell.cpp, modes and the IDesignHost implementation live in DesignMode.cpp.
MainWindow::MainWindow() {
  setWindowTitle("PersistCAD");
  resize(1600, 1000);
  setWindowIcon(makeIcon("extrude", 32));
  selection_ = new SelectionModel(this);

  viewer_ = new viewer::ShapeViewer(this);
  viewer_->setObjectName("viewer");
  section_ = new viewer::SectionControls(viewer_, /*overlay*/ true);
  // SPEC §6.1: the sketch editor lies over the viewer (WP-B turns it into a transparent overlay); no stacked page.
  sketchEd_ = new SketchEditor(viewer_);
  sketchEd_->setObjectName("sketchEditor");
  // integration: WP-B's overlay mode (transparent child of the viewer, exact plane->screen transform from the
  // viewer camera; middle button / wheel go to the viewer, so the model can be spun while sketching)
  sketchEd_->attachToViewer(viewer_);
  sketchEd_->hide();
  viewer_->installEventFilter(this);
  // SC23 WP-S (S-P1-2): central area = viewer + the document tab strip below it ("Part1*", SpaceClaim [G])
  {
    auto* central = new QWidget(this);
    central->setObjectName("centralArea");
    central->setAttribute(Qt::WA_StyledBackground, true);
    central->setStyleSheet("QWidget#centralArea { background: #e9eef5; }");
    auto* cv = new QVBoxLayout(central);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(viewer_, 1);
    auto* strip = new QWidget(central);
    strip->setObjectName("documentStrip");
    auto* sh = new QHBoxLayout(strip);
    sh->setContentsMargins(0, 0, 0, 1);
    sh->setSpacing(0);
    docTabs_ = new DocumentTabBar(strip);
    sh->addWidget(docTabs_, 0, Qt::AlignLeft | Qt::AlignTop);
    sh->addStretch(1);
    cv->addWidget(strip, 0);
    setCentralWidget(central);
  }

  buildActions();
  buildMenus();
  createToolManager();   // before the ribbon: WP-B's tool actions are placed in it
  buildRibbon();
  buildDocks();
  buildStatusBar();
  wireSelection();
  buildHud();            // SC23: viewport HUD over viewer_ (after the tool manager and the selection exist)
  createParamSelect();
  import2d_ = new Import2DController(this);   // SC-2D-IMPORT
  // SC23 WP-S (S-P0-1): collapse order of the Görüntü groups (lower first; the 2D-drawing group made by
  // Import2DController and the colour group last, they hold the commands used most on this tab)
  for (auto [k, p] : {std::pair{"window", 1}, std::pair{"hideShow", 2}, std::pair{"section", 3}, std::pair{"visibility", 4},
                      std::pair{"style", 5}, std::pair{"import2d", 6}})
    if (RibbonGroup* g = ribbon_->group(k)) g->setCollapsePriority(p);
  // SC23 WP-S (S-P1-4): Görünümler (Views) tab, last like in SpaceClaim (Structure, Layers, Selection, Groups, Views)
  viewsPanel_ = new ViewsPanel;
  for (const char* n : {"actViewIso", "actViewTrimetric", "actViewTop", "actViewBottom", "actViewLeft", "actViewRight",
                        "actViewFront", "actViewBack"})
    viewsPanel_->addStandardView(findChild<QAction*>(n));
  viewsPanel_->setCameraHooks(
      [this]() -> QVariant {   // saved view = viewing direction (the zoom / target are kept when it is applied)
        double d[3];
        viewer_->viewDirection(d);
        return QVariantList{d[0], d[1], d[2]};
      },
      [this](const QVariant& v) {
        const QVariantList l = v.toList();
        if (l.size() != 3) return;
        const QVector3D eye = -QVector3D(float(l[0].toDouble()), float(l[1].toDouble()), float(l[2].toDouble())).normalized();
        QVector3D up(0, 0, 1);
        if (std::abs(QVector3D::dotProduct(up, eye)) > 0.98f) up = QVector3D(0, 1, 0);
        const QVector3D V = (up - eye * QVector3D::dotProduct(up, eye)).normalized();
        const QVector3D U = QVector3D::crossProduct(V, eye).normalized();
        const double o[3] = {0, 0, 0}, u[3] = {U.x(), U.y(), U.z()}, vv[3] = {V.x(), V.y(), V.z()};
        viewer_->setPlanView(o, u, vv, false);
      });
  structure_->addPage(viewsPanel_, "views", "Görünümler", makeIcon("views", 14),
                      "Görünümler (Views): standart görünümler (Ctrl+0 … Ctrl+6) ve bu oturumda kaydedilen görünümler");
  // SC23 WP-S (S-P1-9): pages wider / taller than a ≈240 px column (Gruplar, Katmanlar, ...) scroll inside the panel
  // instead of forcing the whole left column wider; tab order, keys and page objects are unchanged.
  if (auto* st = structure_->findChild<QTabWidget*>("structureTabs")) {
    const int cur = st->currentIndex();
    for (int i = 0; i < st->count(); ++i) {
      QWidget* page = st->widget(i);
      if (qobject_cast<QAbstractScrollArea*>(page)) continue;   // trees / lists scroll by themselves
      const QSize mh = page->minimumSizeHint();
      if (mh.width() <= 230 && mh.height() <= 160) continue;
      const QString text = st->tabText(i), tip = st->tabToolTip(i);
      const QIcon icon = st->tabIcon(i);
      st->removeTab(i);
      auto* sa = new QScrollArea;
      sa->setObjectName("structurePageScroll." + page->objectName());
      sa->setFrameShape(QFrame::NoFrame);
      sa->setWidgetResizable(true);
      sa->setWidget(page);
      st->insertTab(i, sa, icon, text);
      st->setTabToolTip(i, tip);
    }
    st->setCurrentIndex(cur);
  }

  // mouse: SpaceClaim navigation scheme (WP-B viewer API; the runner keeps the viewer's Legacy default)
  navFallback_ = !detectNavigationScheme();
  if (!navFallback_) {
    // one signal carries name, kind, modifiers, click count and position of every left click (SPEC §7.2 #1)
    connect(viewer_, &viewer::ShapeViewer::picked, this,
            [this](const QString& n, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos) { onViewerPicked(n, kind, mods, clicks, pos); });
    connect(viewer_, &viewer::ShapeViewer::hovered, this, [this](const QString& n, int kind) {
      if (tools_ && toolsReal_) tools_->handleHover(SelItem{selKindOfPick(n, kind), n});
    });
  } else {
    connect(viewer_, &viewer::ShapeViewer::facePicked, this, [this](const QString& n) { onFacePicked(n); });
    connect(viewer_, &viewer::ShapeViewer::edgePicked, this, [this](const QString& n) { onEdgePicked(n); });
    connect(viewer_, &viewer::ShapeViewer::datumPicked, this, [this](const QString& n) { onDatumPicked(n); });
  }
  connect(viewer_, &viewer::ShapeViewer::faceHovered, this, &MainWindow::onFaceHovered);
  // Ctrl+→ / Ctrl+← (move grid) and the ribbon offset field move the plane: the overlay follows
  connect(sketchEd_, &SketchEditor::planeOffsetChanged, this, [this](const QString& expr) {
    if (sketchOffset_ && sketchOffset_->text() != expr) {
      updatingOffset_ = true;
      sketchOffset_->setText(expr);
      updatingOffset_ = false;
    }
    syncSketchFrame();
  });
  viewer_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(viewer_, &QWidget::customContextMenuRequested, this, &MainWindow::viewerContextMenu);
  previewTimer_ = new QTimer(this);
  previewTimer_->setSingleShot(true);
  previewTimer_->setInterval(120);
  connect(previewTimer_, &QTimer::timeout, this, [this] { updatePlanePreview(); });
  connect(sketchEd_, &SketchEditor::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
  connect(sketchEd_, &SketchEditor::solved, this, [this](int dof, const QString& msg, bool ok) {
    lblDof_->setText(QString("Sketch DOF: %1").arg(dof >= 0 ? QString::number(dof) : "?"));
    lblDof_->setStyleSheet(ok ? (dof == 0 ? "color:#8ff0a4;" : "") : "color:#ffb3a8;");   // SC23: on the dark status bar
    lblDof_->setToolTip(msg);
  });
  // v6: parameters created in sketch / feature dialogs are committed to the document at once (parameter table)
  installParameterCommitHook(this, [this](const Parameter& p) { onParameterCreated(p); });
  connect(sketchEd_, &SketchEditor::changed, this, [this] { updateTitle(); });
  connect(&undo_, &QUndoStack::cleanChanged, this, [this] { updateTitle(); });
  actUndo_->setEnabled(false);
  actRedo_->setEnabled(false);

  // keyboard: one table for every shortcut (SPEC §4), unsupported SpaceClaim keys, text-input guard
  applyShortcuts(this, viewer_);
  installUnsupportedKeys(this, [this](const QString& m) { statusBar()->showMessage(m, 6000); });
  installTextInputShortcutGuard(this);
  for (QAction* a : findChildren<QAction*>())   // shortcuts must work while their ribbon tab is hidden (SPEC §1.2)
    if ((a->objectName().startsWith("tool.") || a->objectName().startsWith("act")) && a->shortcutContext() != Qt::WidgetWithChildrenShortcut &&
        !actions().contains(a))
      addAction(a);

  rebuildRecentMenu();
  newDocument();
  syncModeActions();
  refreshOptionsPanel();
  QTimer::singleShot(0, this, [this] {
    const int lw = std::clamp(int(width() * 0.22), 240, 330);   // SC23 WP-S: left column ≈ 20 % (240..330 px)
    resizeDocks({dockStructure_, dockOptions_}, {lw, lw}, Qt::Horizontal);
    resizeDocks({dockStructure_, dockOptions_}, {500, 330}, Qt::Vertical);
    resizeDocks({dockIssues_}, {140}, Qt::Vertical);
    restoreLayout();
  });
}

MainWindow::~MainWindow() { removeParameterCommitHook(this); }

// ------------------------------------------------------------------------------ doc state
void MainWindow::regenerateModel() {
  QElapsedTimer t;
  t.start();
  try {
    if (rollback_ >= 0) {
      const ParamValues vals = doc_.evaluateParams();
      model_ = model::regenerate(doc_, vals, rollback_);
    } else {
      model_ = model::regenerate(doc_, std::map<std::string, std::string>());
    }
  } catch (...) {   // std::exception or OCCT Standard_Failure
    model_ = model::ModelResult();
  }
  regenMs_ = double(t.nsecsElapsed()) / 1e6;
}

void MainWindow::applyDocument(const Document& d, bool useCache) {
  doc_ = d;
  // v6: open sketch / feature dialog work on copies; keep their parameters in step with the document so that
  // parameter-table edits re-solve the open sketch and the dialog pickers show the current values.
  if (sketching_) {
    work_ = doc_;
    sketchEd_->reloadParams();
    sketchEd_->resolve();
    sketchEd_->ensureVisible();
    if (sketchOffset_) sketchOffset_->refreshParams();
  }
  if (featDlg_ && dlgWork_) {
    dlgWork_->params = doc_.params;
    featDlg_->refreshParams();
  }
  if (useCache && pendingValid_ && rollback_ < 0) {
    model_ = pendingModel_;
    regenMs_ = 0;
  } else {
    regenerateModel();
  }
  pendingValid_ = false;
  pendingModel_ = model::ModelResult();
  selectedFace_.clear();
  exprWork_ = doc_;
  // SC-UX: drop selected items that no longer exist (undo / redo / edits)
  {
    QList<SelItem> keep;
    for (const SelItem& it : selection_->items()) {
      const std::string n = it.name.toStdString();
      bool alive = true;
      switch (it.kind) {
        case SelItem::Kind::Face: alive = model_.faces.count(n) > 0; break;
        case SelItem::Kind::Edge: alive = model_.edges.count(n) > 0; break;
        case SelItem::Kind::Body: alive = model_.body(n) != nullptr; break;
        case SelItem::Kind::Datum: case SelItem::Kind::Feature: case SelItem::Kind::Sketch: alive = doc_.feature(n) != nullptr; break;
        case SelItem::Kind::Parameter: alive = doc_.params.find(n) != nullptr; break;
        default: break;
      }
      if (alive) keep << it;
    }
    if (keep.size() != selection_->count()) { selFromLegacy_ = true; selection_->set(keep); selFromLegacy_ = false; }
  }
  refreshAll(true);
  if (sketching_) syncSketchFrame();   // parameters / the base face may have moved the plane
  if (tools_) tools_->onDocumentChanged();
  if (paramSel_) paramSel_->refresh();
  if (!useCache) QTimer::singleShot(0, this, [this] { runCheck(); });
}

bool MainWindow::commitNoValidate(Document cand, const QString& desc) {
  pendingValid_ = false;
  undo_.push(new DocCommand(this, doc_, cand, desc));
  return true;
}

void MainWindow::pushCommit(const Document& cand, const QString& desc, const mfg::ValidationResult* vr) {
  if (vr && vr->regenerated && rollback_ < 0) {
    pendingModel_ = vr->model;
    pendingValid_ = true;
  } else {
    pendingValid_ = false;
  }
  if (rollback_ >= int(cand.features.size())) rollback_ = -1;
  undo_.push(new DocCommand(this, doc_, cand, desc));
}

void MainWindow::refreshAll(bool keepCamera) {
  nsUpdate();   // named selections first: the viewer draws their labels / tints
  visUpdate();  // Gizle / Göster: hidden / transparent faces of the regenerated model
  updateViewer(keepCamera);
  if (selection_ && !selection_->isEmpty()) highlightSelection();   // updateViewer cleared every highlight
  rebuildTree();
  rebuildBodies();
  rebuildStructure();
  rebuildParams();
  updateStatus();
  updateTitle();
  schedulePanelsRefresh();
  nsRebuildPanel();
  updateVisStatus();
  if (import2d_) import2d_->refresh();   // SC-2D-IMPORT: Katmanlar
}

// ------------------------------------------------------------------------------ v6: sketch overlay in 3D
bool MainWindow::sketchShownIn3D(const std::string& id) const {
  if (import2d_ && import2d_->sketchHidden(id)) return false;   // SC-2D-IMPORT: hidden layer
  auto it = sketchShown_.find(id);
  if (it != sketchShown_.end()) return it->second;
  // default (SolidWorks-like): sketches not consumed by a solid feature are shown, consumed ones hidden
  for (const auto& f : doc_.features)
    if (!f.suppressed && f.id != id && f.prop("sketch") == id && f.prop("regions").empty()) return false;   // SC-PULLUPTO: partial region pulls keep the sketch shown
  return true;
}

int MainWindow::addSketchOverlay(TopoDS_Compound& comp, std::map<std::string, TopoDS_Edge>& names) const {
  BRep_Builder bb;
  int n = 0;
  for (const auto& fi : model_.info) {
    const Feature* f = doc_.feature(fi.featureId);
    if (!f || f->type != FeatureType::Sketch || f->suppressed || !sketchShownIn3D(f->id)) continue;
    if (sketching_ && f->id == editSketchId_) continue;   // the open sketch is drawn by the sketch overlay
    auto sit = model_.solvedSketches.find(f->id);
    if (sit == model_.solvedSketches.end()) continue;
    const Sketch& sk = sit->second;
    const gp_Ax3& fr = fi.sketchFrame;
    const gp_Pnt O = fr.Location();
    const gp_Dir X = fr.XDirection(), Y = fr.YDirection(), N = fr.Direction();
    auto P3 = [&](int pid) {
      const SkPoint* p = sk.point(pid);
      const double u = p ? p->x : 0, v = p ? p->y : 0;
      return gp_Pnt(O.XYZ() + X.XYZ() * u + Y.XYZ() * v);
    };
    for (const auto& e : sk.entities) {
      // construction lines are drawn (and pickable) too: Alt+click picks them as a revolve axis (WP-B A-1)
      try {
        TopoDS_Edge edge;
        if (e.type == SkEntityType::Line && e.points.size() == 2) {
          const gp_Pnt a = P3(e.points[0]), b = P3(e.points[1]);
          if (a.Distance(b) < 1e-9) continue;
          edge = BRepBuilderAPI_MakeEdge(a, b);
        } else if (e.type == SkEntityType::Circle && !e.points.empty() && e.radius > 1e-9) {
          edge = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(P3(e.points[0]), N, X), e.radius));
        } else if (e.type == SkEntityType::Arc && e.points.size() == 3) {
          const gp_Pnt c = P3(e.points[0]), s0 = P3(e.points[1]), s1 = P3(e.points[2]);
          const double r = c.Distance(s0);
          if (r < 1e-9) continue;
          const gp_Ax2 ax(c, N, X);
          auto ang = [&](const gp_Pnt& q) {
            const gp_Vec d(c, q);
            return std::atan2(d.Dot(gp_Vec(ax.YDirection())), d.Dot(gp_Vec(ax.XDirection())));
          };
          double a0 = ang(s0), a1 = ang(s1);
          while (a1 <= a0) a1 += 2 * M_PI;
          edge = BRepBuilderAPI_MakeEdge(gp_Circ(ax, r), a0, a1);
        } else {
          continue;
        }
        bb.Add(comp, edge);
        names["sketch:" + f->id + ":e" + std::to_string(e.id)] = edge;
        ++n;
      } catch (...) {}
    }
    // v6: dimension values of the selected / last edited sketch as labels ("H = 30")
    if (showSketchDims_ && f->id == labelSketchId_) {
      for (const auto& c : sk.constraints) {
        if (!isDimensional(c.type) || c.valueExpr.empty()) continue;
        double su = 0, sv = 0;
        int cnt = 0;
        auto addPt = [&](int pid) { if (const SkPoint* p = sk.point(pid)) { su += p->x; sv += p->y; ++cnt; } };
        for (int r : c.refs) {
          if (sk.point(r)) addPt(r);
          else if (const SkEntity* en = sk.entity(r)) {
            if (en->type == SkEntityType::Line) { for (int pid : en->points) addPt(pid); }
            else if (const SkPoint* ce = sk.point(en->points[0])) {
              const double rad = en->type == SkEntityType::Circle ? en->radius
                                 : (sk.point(en->points[1]) ? std::hypot(sk.point(en->points[1])->x - ce->x, sk.point(en->points[1])->y - ce->y) : 0);
              su += ce->x + rad * 0.7071; sv += ce->y + rad * 0.7071; ++cnt;
            }
          }
        }
        if (!cnt) continue;
        const double u = su / cnt, v = sv / cnt;
        const gp_Pnt P(O.XYZ() + X.XYZ() * u + Y.XYZ() * v);
        QString text = QString::fromStdString(c.valueExpr);
        bool num = false;
        text.toDouble(&num);
        if (!num) {
          try { text += QString(" = %1").arg(evalWith(c.valueExpr, model_.params), 0, 'g', 6); } catch (...) {}
        }
        if (c.type == SkConstraintType::Angle) text += "°";
        if (c.type == SkConstraintType::Diameter) text.prepend("Ø");
        if (c.type == SkConstraintType::Radius) text.prepend("R");
        viewer::Label3D l;
        l.pos[0] = P.X(); l.pos[1] = P.Y(); l.pos[2] = P.Z();
        l.text = text;
        sketchLabels_.push_back(l);
      }
    }
  }
  return n;
}

void MainWindow::setSketchShownIn3D(const std::string& id, bool on) {
  sketchShown_[id] = on;
  updateViewer(true);
  rebuildTree();
}

std::set<std::string> MainWindow::regionSketches() const {
  // regions of the sketches shown in 3D (default: not consumed by a solid feature), up to the rollback position;
  // the sketch being edited is drawn by the sketch overlay instead
  std::set<std::string> ids;
  for (size_t i = 0; i < doc_.features.size(); ++i) {
    if (rollback_ >= 0 && int(i) > rollback_) break;
    const Feature& f = doc_.features[i];
    if (f.type != FeatureType::Sketch || f.suppressed || !f.sketch) continue;
    if (sketching_ && f.id == editSketchId_) continue;
    if (sketchShownIn3D(f.id)) ids.insert(f.id);
  }
  return ids;
}

void MainWindow::updateViewer(bool keepCamera) {
  viewer_->clearGhost();
  viewer_->clearHighlights();
  TopoDS_Compound comp;
  BRep_Builder bb;
  bb.MakeCompound(comp);
  auto edges = model_.edges;
  auto faces = model_.faces;
  sketchLabels_.clear();
  const int nSketch = addSketchOverlay(comp, edges);
  // SC-UX: closed sketch profiles as translucent blue, pickable region faces "sketch:<sid>:r<k>" (display only)
  int nRegions = 0;
  try { nRegions = appendSketchRegionFaces(doc_, model_, regionSketches(), comp, faces); } catch (...) {}
  {
    std::vector<viewer::Label3D> labels = sketchLabels_;
    appendNsLabels(labels);
    if (import2d_) import2d_->appendLabels(labels);   // SC-2D-IMPORT: drawing notes
    viewer_->setLabels(labels);
  }
  sketchOverlayEdges_ = nSketch;
  if (model_.shape.IsNull() && nSketch == 0 && nRegions == 0) viewer_->clearModel();
  else if (nSketch == 0 && nRegions == 0) viewer_->setModel(model_.shape, model_.faces, model_.edges, keepCamera);
  else {
    if (!model_.shape.IsNull()) bb.Add(comp, model_.shape);
    viewer_->setModel(comp, faces, edges, keepCamera);
  }
  if (import2d_) import2d_->decorateViewer();   // SC-2D-IMPORT: layer colours / line styles
  applyBodyAppearance();
  applyDatumPlanes();
  applySketchTransparency();   // see-through solids while sketching, opaque otherwise
}

void MainWindow::applySketchTransparency() {
  // SPEC §6.1: while sketching the solids are see-through so the plane and the grid stay readable
  QStringList all;
  if (sketching_) for (const auto& [n, f] : model_.faces) { (void)f; all << QString::fromStdString(n); }
  else for (const auto& f : transparentFaces_) all << QString::fromStdString(f);   // "Saydam yap" (view state)
  viewer_->setTransparentFaces(all, 0.35);
}

void MainWindow::updateTitle() {
  setWindowTitle(QString("%1%2 - PersistCAD [*]").arg(QString::fromStdString(doc_.name), path_.isEmpty() ? QString() : " (" + QFileInfo(path_).fileName() + ")"));
  setWindowModified(!undo_.isClean() || (sketching_ && sketchEd_->modified()));
  if (ribbon_) ribbon_->setTitleText(QString("%1%2 - PersistCAD").arg(path_.isEmpty() ? QString::fromStdString(doc_.name) : QFileInfo(path_).fileName(),
                                                                       isWindowModified() ? "*" : ""));
  // SC23 WP-S (S-P1-2): document tab "Part1*"
  if (docTabs_) docTabs_->setDocument(path_.isEmpty() ? QString::fromStdString(doc_.name) : QFileInfo(path_).fileName(),
                                      isWindowModified(), path_);
}

void MainWindow::updateStatus() {
  lblRegen_->setText(QString("Yenileme: %1 ms").arg(regenMs_, 0, 'f', 1));
  QString vol = "Hacim: -";
  if (!model_.shape.IsNull()) {
    try {
      const auto mp = model::massProperties(model_.shape);
      vol = QString("Hacim: %1 mm³").arg(mp.volume, 0, 'f', 1);
    } catch (...) {}
  }
  lblVol_->setText(vol);
  if (!sketching_ && tree_ && currentTreeFeature().empty()) lblDof_->setText("Sketch DOF: -");
  const int ne = report_.count(mfg::Severity::Error), nw = report_.count(mfg::Severity::Warning);
  int modelErr = 0;
  for (const auto& s : model_.status) if (!s.ok) ++modelErr;
  lblIssues_->setText(QString("Hata: %1   Uyarı: %2").arg(ne + modelErr).arg(nw));
  lblIssues_->setStyleSheet(ne + modelErr > 0 ? "color:#c0302a; font-weight:bold;" : nw > 0 ? "color:#b36b00;" : "");
}

std::string MainWindow::currentTreeFeature() const {
  auto items = tree_->selectedItems();
  return items.isEmpty() ? std::string() : items.first()->data(0, Qt::UserRole).toString().toStdString();
}

void MainWindow::rebuildTree() {
  const std::string keep = currentTreeFeature();
  tree_->blockSignals(true);
  tree_->clear();
  std::map<std::string, const model::FeatureStatus*> st;
  for (const auto& s : model_.status) st[s.featureId] = &s;
  QTreeWidgetItem* sel = nullptr;
  for (size_t i = 0; i < doc_.features.size(); ++i) {
    const Feature& f = doc_.features[i];
    auto* it = new QTreeWidgetItem(tree_);
    it->setData(0, Qt::UserRole, QString::fromStdString(f.id));
    QString text = QString::fromStdString(f.id) + featureSummary(f);
    QIcon ic = makeIcon(typeIconName(f.type));
    QString tip;
    const bool rolled = rollback_ >= 0 && int(i) > rollback_;
    auto sit = st.find(f.id);
    if (f.suppressed) {
      text += "  (bastırıldı)";
      ic = makeIcon("suppressed");
      it->setForeground(0, QColor(140, 140, 140));
      QFont fn = it->font(0); fn.setItalic(true); it->setFont(0, fn);
      tip = "Bastırılmış özellik";
    } else if (rolled) {
      text += "  (geri sarıldı)";
      it->setForeground(0, QColor(150, 150, 150));
      tip = "Geri sarma konumunun ötesinde";
    } else if (sit != st.end() && !sit->second->ok) {
      ic = makeIcon("error");
      it->setForeground(0, QColor(190, 30, 30));
      tip = "HATA: " + QString::fromStdString(sit->second->message);
    } else if (sit != st.end()) {
      tip = sit->second->message.empty() ? "Tamam" : "Tamam: " + QString::fromStdString(sit->second->message);
    }
    it->setText(0, text);
    it->setIcon(0, ic);
    it->setToolTip(0, tip);
    if (rollback_ >= 0 && int(i) == rollback_) { QFont fn = it->font(0); fn.setBold(true); it->setFont(0, fn); text += "  <- geri sarma"; it->setText(0, text); }
    if (f.id == keep) sel = it;
  }
  if (sel) sel->setSelected(true);
  tree_->blockSignals(false);
}

void MainWindow::rebuildParams() {
  updatingParams_ = true;
  paramModel_->clear();
  paramModel_->setHorizontalHeaderLabels({"Ad", "İfade", "Değer", "Birim", "Min", "Maks", "Uygulamada göster", "Etiket", "Grup", "Açıklama"});
  if (auto* hi = paramModel_->horizontalHeaderItem(6)) hi->setToolTip("App'te göster: parametre 'Uygulama Olarak Kaydet' çıktısında giriş olur");
  std::vector<ParamError> errs;
  const auto vals = doc_.params.evaluate(&errs);
  ParamValues v = vals ? *vals : ParamValues();
  std::vector<ParamError> rangeErr;
  if (vals) rangeErr = doc_.params.checkRanges(v);
  auto mk = [](const QString& t, bool editable) {
    auto* it = new QStandardItem(t);
    it->setEditable(editable);
    return it;
  };
  for (const auto& p : doc_.params.all()) {
    auto* name = mk(QString::fromStdString(p.name), true);
    name->setData(QString::fromStdString(p.name), Qt::UserRole);
    auto* expr = mk(QString::fromStdString(p.expression), true);
    QString valText = "?";
    QString tip;
    bool bad = false;
    if (vals && v.count(p.name)) valText = QString::number(v[p.name], 'g', 8);
    else { bad = true; valText = "hata"; }
    for (const auto& e : errs) if (e.param.empty() || e.param == p.name) tip += QString::fromStdString(e.message) + "\n";
    for (const auto& e : rangeErr) if (e.param == p.name) { bad = true; tip += QString::fromStdString(e.message) + "\n"; }
    auto* val = mk(valText, false);
    if (bad) { val->setForeground(QColor(190, 30, 30)); val->setToolTip(tip.trimmed()); }
    auto* mn = mk(std::isfinite(p.minValue) ? QString::number(p.minValue, 'g', 8) : "", true);
    auto* mx = mk(std::isfinite(p.maxValue) ? QString::number(p.maxValue, 'g', 8) : "", true);
    auto* ex = mk("", false);
    ex->setCheckable(true);
    ex->setCheckState(p.exposed ? Qt::Checked : Qt::Unchecked);
    ex->setFlags((ex->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    paramModel_->appendRow({name, expr, val, mk(kindText(p.kind), true), mn, mx, ex, mk(QString::fromStdString(p.label), true),
                            mk(QString::fromStdString(p.group), true), mk(QString::fromStdString(p.description), true)});
  }
  paramView_->resizeColumnsToContents();
  paramView_->horizontalHeader()->setStretchLastSection(true);
  updatingParams_ = false;
}

void MainWindow::rebuildIssues() {
  issueTable_->blockSignals(true);
  issueTable_->setRowCount(0);
  issueTable_->setRowCount(int(report_.issues.size()));
  int row = 0;
  for (const auto& is : report_.issues) {
    const QString ic = is.severity == mfg::Severity::Error ? "error" : is.severity == mfg::Severity::Warning ? "warn" : "info";
    auto* i0 = new QTableWidgetItem(makeIcon(ic, 16), "");
    i0->setToolTip(severityText(is.severity));
    issueTable_->setItem(row, 0, i0);
    issueTable_->setItem(row, 1, new QTableWidgetItem(domainText(is.domain)));
    issueTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(is.ruleId)));
    auto* msg = new QTableWidgetItem(QString::fromStdString(is.message));
    msg->setToolTip(QString::fromStdString(is.message));
    issueTable_->setItem(row, 3, msg);
    QStringList ps;
    for (const auto& p : is.params) ps << QString::fromStdString(p);
    issueTable_->setItem(row, 4, new QTableWidgetItem(ps.join(", ")));
    issueTable_->setItem(row, 5, new QTableWidgetItem(is.suggestedValue ? QString::number(*is.suggestedValue, 'g', 6) : QString("-")));
    const QColor bg = is.severity == mfg::Severity::Error ? QColor(255, 232, 230) : is.severity == mfg::Severity::Warning ? QColor(255, 247, 222) : QColor(236, 244, 255);
    issueTable_->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(is.body)));
    for (int c = 0; c < 7; ++c) issueTable_->item(row, c)->setBackground(bg);
    auto* btn = new QPushButton("Öneriyi uygula");
    btn->setEnabled(is.suggestedValue.has_value() && !is.params.empty());
    connect(btn, &QPushButton::clicked, this, [this, row] { applySuggestion(row); });
    issueTable_->setCellWidget(row, 7, btn);
    ++row;
  }
  issueTable_->resizeColumnsToContents();
  issueTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  issueTable_->setColumnWidth(0, 28);
  issueTable_->blockSignals(false);
  updateStatus();
}

void MainWindow::selectIssueRow(int row) {
  if (row >= 0 && row < issueTable_->rowCount()) issueTable_->selectRow(row);
}

void MainWindow::runCheck() {
  try {
    auto vr = mfg::validateChange(doc_, {}, doc_.mfg);
    report_ = vr.report;
  } catch (const std::exception& e) {
    statusBar()->showMessage(QString("Denetim hatası: ") + e.what(), 5000);
  } catch (...) {
    statusBar()->showMessage("Denetim hatası", 5000);
  }
  rebuildIssues();
}

void MainWindow::openMfgSettings() {
  MfgSettingsDialog dlg(doc_.mfg, this);
  if (dlg.exec() != QDialog::Accepted) return;
  Document cand = doc_;
  cand.mfg = dlg.settings();
  commitNoValidate(cand, "İmalat ayarları");
  runCheck();
}

// ------------------------------------------------------------------------------ hover / pick
void MainWindow::onFaceHovered(const QString& name) {
  hoverFace_ = name;
  // "Yüz: Extrude1.end — Ad: Inlet" (named selections of the face, SpaceClaim-style)
  lblHover_->setText(name.isEmpty() ? "Yüz: -" : "Yüz: " + name + nsHoverSuffix(name));
  if (planePick_) updatePlanPreviewHover(name);
  if (tools_ && toolsReal_ && navFallback_) {   // SpaceClaim scheme: ShapeViewer::hovered (faces AND edges)
    SelItem it;
    it.kind = name.startsWith("sketch:") ? SelItem::Kind::SketchRegion : SelItem::Kind::Face;
    it.name = name;
    tools_->handleHover(it);
  }
}

// ------------------------------------------------------------------------------ files
void MainWindow::newDocument() {
  if (sketching_) setSketchMode(false);   // callers resolved the open sketch through maybeSave()
  if (featDlg_) featDlg_->reject();
  cancelPlanePick();
  if (section_->sectionOn()) section_->setSectionOn(false);
  selFromLegacy_ = true;
  selection_->clear();
  selFromLegacy_ = false;
  hiddenBodies_.clear();
  hiddenFaces_.clear();
  hiddenOwnFaces_.clear();
  transparentFaces_.clear();
  transparentBodies_.clear();
  nsMgr_.clear();
  nsQuiet_ = true;
  nsTint_.clear();
  nsActive_.clear();
  Document d;
  d.name = "Part1";
  sketchShown_.clear();
  undo_.clear();
  path_.clear();
  rollback_ = -1;
  report_ = mfg::Report();
  doc_ = d;
  model_ = model::ModelResult();
  regenerateModel();
  refreshAll(false);
  rebuildIssues();
  undo_.setClean();
  updateTitle();
}

bool MainWindow::openFile(const QString& path) {
  Document d;
  std::string err;
  if (!loadDocument(d, path.toStdString(), &err)) {
    QMessageBox::critical(this, "Aç", QString("Dosya açılamadı:\n%1").arg(QString::fromStdString(err)));
    return false;
  }
  if (sketching_ && !resolveOpenSketch()) return false;   // v6: never drop / keep a stale open sketch
  if (sketching_) setSketchMode(false);
  if (featDlg_) featDlg_->reject();
  cancelPlanePick();
  selFromLegacy_ = true;
  selection_->clear();
  selFromLegacy_ = false;
  sketchShown_.clear();
  undo_.clear();
  nsMgr_.clear();
  nsQuiet_ = true;
  nsTint_.clear();
  nsActive_.clear();
  doc_ = d;
  path_ = path;
  rollback_ = -1;
  regenerateModel();
  refreshAll(false);
  if (tools_) tools_->onDocumentChanged();
  undo_.setClean();
  addRecent(path);
  updateTitle();
  runCheck();
  return true;
}

void MainWindow::fileOpen() {
  if (!maybeSave()) return;
  const QString p = QFileDialog::getOpenFileName(this, "Aç", QString(), "PersistCAD (*.pcad);;Tüm dosyalar (*)");
  if (!p.isEmpty()) openFile(p);
}

bool MainWindow::fileSave() {
  // v6: an open sketch is part of what the user wants to save: commit it first (never save without it)
  if (sketching_ && !resolveOpenSketch(false)) return false;
  if (path_.isEmpty()) return fileSaveAs();
  std::string err;
  // named selections: refresh the stored signatures from unambiguous resolutions (better split-piece tracking
  // across long edit chains; no undo step needed, references are never rewritten except positional piece numbers)
  if (rollback_ < 0 && !doc_.namedSelections.empty()) {
    try { if (model::refreshSignatures(doc_, model_, model::resolveAll(doc_, model_))) exprWork_ = doc_; } catch (...) {}
  }
  if (!saveDocument(doc_, path_.toStdString(), &err)) {
    QMessageBox::critical(this, "Kaydet", QString::fromStdString(err));
    return false;
  }
  undo_.setClean();
  statusBar()->showMessage("Kaydedildi: " + path_, 4000);
  return true;
}

bool MainWindow::fileSaveAs() {
  QString p = QFileDialog::getSaveFileName(this, "Farklı Kaydet", path_.isEmpty() ? QString::fromStdString(doc_.name) + ".pcad" : path_, "PersistCAD (*.pcad)");
  if (p.isEmpty()) return false;
  if (!p.endsWith(".pcad", Qt::CaseInsensitive)) p += ".pcad";
  path_ = p;
  addRecent(p);
  const bool ok = fileSave();
  updateTitle();
  return ok;
}

bool MainWindow::maybeSave() {
  if (!resolveOpenSketch()) return false;
  if (undo_.isClean() || autoYes_) return true;
  const auto r = QMessageBox::question(this, "PersistCAD", "Kaydedilmemiş değişiklikler var. Kaydedilsin mi?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (r == QMessageBox::Cancel) return false;
  if (r == QMessageBox::Save) return fileSave();
  return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
  if (maybeSave()) { saveLayout(); e->accept(); } else e->ignore();
}

void MainWindow::addRecent(const QString& p) {
  QSettings s("PersistCAD", "Designer");
  QStringList l = s.value("recent").toStringList();
  l.removeAll(p);
  l.prepend(p);
  while (l.size() > 8) l.removeLast();
  s.setValue("recent", l);
  rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
  recentMenu_->clear();
  QSettings s("PersistCAD", "Designer");
  const QStringList l = s.value("recent").toStringList();
  for (const QString& p : l) {
    QAction* a = recentMenu_->addAction(p);
    connect(a, &QAction::triggered, this, [this, p] { if (maybeSave()) openFile(p); });
  }
  recentMenu_->setEnabled(!l.isEmpty());
}

void MainWindow::exportShape(bool step) {
  if (sketching_ && !resolveOpenSketch(false)) return;
  if (model_.shape.IsNull()) { QMessageBox::information(this, "Dışa aktar", "Dışa aktarılacak geometri yok."); return; }
  const QString p = QFileDialog::getSaveFileName(this, step ? "STEP olarak dışa aktar" : "STL olarak dışa aktar", QString::fromStdString(doc_.name) + (step ? ".step" : ".stl"),
                                                 step ? "STEP (*.step *.stp)" : "STL (*.stl)");
  if (p.isEmpty()) return;
  std::string err;
  // STEP: named selections become face names (and colours) for downstream CFD/FEA meshers
  const bool ok = step ? model::exportSTEPNamed(doc_, model_, p.toStdString(), {}, &err) : model::exportSTL(model_.shape, p.toStdString(), 0.05, &err);
  if (!ok) QMessageBox::critical(this, "Dışa aktar", QString::fromStdString(err));
  else statusBar()->showMessage("Dışa aktarıldı: " + p, 4000);
}

void MainWindow::saveAsApp() {
  if (sketching_ && !resolveOpenSketch(false)) return;   // the app must contain the sketch being edited
  if (featDlg_) featDlg_->reject();
  Document copy = appPayloadDocument();   // SC-COLOR: Gizle / Göster is cleared (the app shows the whole part), colours are kept
  copy.layers = LayerTable();   // SC-2D-IMPORT: layers are view state too
  appui::runSaveAsApp(copy, QCoreApplication::applicationDirPath() +
#ifdef Q_OS_WIN
                                       "/persistcad_runner.exe"
#else
                                       "/persistcad_runner"
#endif
                    , this);
  copy.view = doc_.view;
  if (nlohmann::json(toJson(copy)) != nlohmann::json(toJson(doc_))) commitNoValidate(copy, "App parametre ayarları");
}

} // namespace pcd
