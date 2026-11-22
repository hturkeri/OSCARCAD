// SC-UX ToolManager (WP-B). Replaces the P0 stub (same file name: src/designer/sources.cmake globs tools/*.cpp).
//
// Owns every tool action (objectName "tool.<id>", SPEC §1.3 texts, no shortcuts — WP-A's Shortcuts.cpp assigns
// them) and every IActiveTool. Exactly one tool is active. Keys reach the active tool before the QAction shortcuts
// through installKeyRouting() (Tab / Space / digits / Enter / Esc / arrows / Ctrl+G / Del); viewer mouse events
// reach it before the viewer (handle drags) through a filter installed when the viewport overlay is created.
#include "CombineTool.h"
#include "FillTool.h"
#include "MiniToolbar.h"
#include "MoveTool.h"
#include "PlaneTool.h"
#include "PullTool.h"
#include "SelectTool.h"
#include "ShellTool.h"
#include "SketchEditor.h"
#include "SketchToolAdapter.h"
#include "ToolApi.h"
#include "ToolGuideBar.h"
#include "ViewportOverlay.h"
#include "pc/ShapeViewer.h"
#include "pc/SketchData.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>

namespace pcd {

namespace {
// key routing (ToolManager's frozen header has no eventFilter override: a helper object does it)
class KeyRouter : public QObject {
public:
  explicit KeyRouter(ToolManager* tm) : QObject(tm), tm_(tm) {}
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() != QEvent::ShortcutOverride && e->type() != QEvent::KeyPress) return QObject::eventFilter(o, e);
    auto* k = static_cast<QKeyEvent*>(e);
    if (e->type() == QEvent::ShortcutOverride) {
      IActiveTool* t = tm_->active();
      // SC23 (ANALYSIS §4.4): Ctrl+Z / Ctrl+Y during a 3D preview cancel the preview first; the event is NOT
      // accepted, so Geri Al / Yinele run right after (one undo step, never an implicit commit)
      if ((k->matches(QKeySequence::Undo) || k->matches(QKeySequence::Redo)) && t) {
        auto* tb = dynamic_cast<ToolBase*>(t);
        if (!(tb && tb->isSketchTool()))
          for (int i = 0; i < 8 && t->busy(); ++i)
            if (!t->cancel()) break;
        return false;
      }
      if (t && t->wantsKey(k)) { e->accept(); return true; }
      return false;
    }
    const int key = k->key();
    const bool routed = key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Space || key == Qt::Key_Return ||
                        key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Delete || key == Qt::Key_Up ||
                        key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right ||
                        (key == Qt::Key_G && (k->modifiers() & Qt::ControlModifier)) ||
                        (key == Qt::Key_A && (k->modifiers() & Qt::ControlModifier)) ||
                        (!k->text().isEmpty() && (k->text()[0].isDigit() || k->text()[0] == '.' || k->text()[0] == '-' ||
                                                  k->text()[0] == '(' || k->text()[0] == ',')) ||
                        (tm_->active() && tm_->active()->wantsKey(k));
    if (routed && tm_->routeKey(k)) return true;
    return false;
  }
  ToolManager* tm_;
};

class ViewerMouseRouter : public QObject {
public:
  ViewerMouseRouter(ToolManager* tm, std::function<ToolBase*()> active, std::function<MiniToolbar*()> mini)
      : QObject(tm), active_(std::move(active)), mini_(std::move(mini)) {}
  bool eventFilter(QObject* o, QEvent* e) override {
    ToolBase* t = active_();
    auto* w = qobject_cast<QWidget*>(o);
    switch (e->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick: {
        auto* m = static_cast<QMouseEvent*>(e);
        if (MiniToolbar* mt = mini_(); mt && !mt->sketchMode() && m->button() == Qt::LeftButton) mt->setAnchor(m->pos());
        if (t && t->viewerMousePress(m)) { if (w) w->setFocus(); return true; }
        break;
      }
      case QEvent::MouseMove: {
        auto* m = static_cast<QMouseEvent*>(e);
        if (MiniToolbar* mt = mini_()) mt->cursorMoved(m->pos());
        if (t && t->viewerMouseMove(m)) return true;
        break;
      }
      case QEvent::MouseButtonRelease:
        if (t && t->viewerMouseRelease(static_cast<QMouseEvent*>(e))) return true;
        break;
      default: break;
    }
    return QObject::eventFilter(o, e);
  }
  std::function<ToolBase*()> active_;
  std::function<MiniToolbar*()> mini_;
};
} // namespace

struct ToolManager::Impl {
  IDesignHost* host = nullptr;
  QActionGroup* group = nullptr;
  QHash<QString, QAction*> byId;
  QStringList toolIds;
  QList<QAction*> edit, sketch, cons, dims, other;
  QHash<QString, ToolBase*> tools;
  ToolBase* active = nullptr;
  QString activeId = "select";
  QPointer<ViewportOverlay> overlay;
  QPointer<MiniToolbar> mini;
  QPointer<ToolGuideBar> guides;
  QPointer<QWidget> mouseViewer;
  KeyRouter* keys = nullptr;
  ViewerMouseRouter* mouse = nullptr;
  bool activating = false;
  QList<QMetaObject::Connection> conns;
};

ToolManager::ToolManager(IDesignHost* host, QObject* parent) : QObject(parent), d(new Impl) {
  d->host = host;
  d->group = new QActionGroup(this);
  d->group->setExclusive(true);
  d->keys = new KeyRouter(this);
  auto mk = [this](const QString& id, const QString& text, const QString& tip, bool tool, bool checkable) {
    QObject* owner = QObject::parent() ? QObject::parent() : this;
    auto* a = new QAction(text, owner);
    a->setObjectName("tool." + id);
    a->setCheckable(checkable);
    if (!tip.isEmpty()) a->setToolTip(tip);
    d->byId.insert(id, a);
    if (tool) {
      d->toolIds << id;
      d->group->addAction(a);
      connect(a, &QAction::triggered, this, [this, id] { activate(id); });
    }
    return a;
  };
  // Düzenle (Edit) group
  d->edit << mk("select", "Seç", "Seç (Select) — S", true, true) << mk("pull", "Çek", "Çek (Pull) — P", true, true)
          << mk("move", "Taşı", "Taşı (Move) — M", true, true) << mk("fill", "Doldur", "Doldur (Fill) — F", true, true);
  // Sketch group
  d->sketch << mk("sketch.line", "Çizgi", "Çizgi (Line) — L", true, true) << mk("sketch.rect", "Dikdörtgen", "Dikdörtgen (Rectangle) — R", true, true)
            << mk("sketch.circle", "Çember", "Çember (Circle) — C", true, true) << mk("sketch.arc", "Yay", "Yay (Sweep Arc)", true, true)
            << mk("sketch.point", "Nokta", "Nokta (Point)", true, true)
            << mk("sketch.construction", "Yapı çizgisi", "Yapı çizgisi (Construction Line): seçili öğeleri / yeni çizimleri yapı geometrisi yapar", false, true)
            << mk("sketch.trim", "Kırp", "Kırp (Trim Away) — T", true, true);
  // "sketch.select" is a tool id without its own action: the single "Seç" action is shared (SPEC §1.3).
  d->toolIds << "sketch.select";
  using T = pc::SkConstraintType;
  struct C { const char* txt; T t; };
  const C cons[] = {{"Çakışık", T::Coincident}, {"Yatay", T::Horizontal}, {"Dikey", T::Vertical}, {"Paralel", T::Parallel},
                    {"Dik", T::Perpendicular}, {"Teğet", T::Tangent}, {"Eşit", T::Equal}, {"Sabit", T::Fixed},
                    {"Orta nokta", T::Midpoint}, {"Simetrik", T::Symmetric}, {"Eş merkezli", T::Concentric}};
  for (const auto& c : cons) {
    QAction* a = mk(QString("constraint.%1").arg(int(c.t)), c.txt, QString(), false, false);
    const T t = c.t;
    connect(a, &QAction::triggered, this, [this, t] { if (d->host && d->host->sketchEditor()) d->host->sketchEditor()->applyConstraint(t); });
    d->cons << a;
  }
  const C dims[] = {{"Mesafe", T::Distance}, {"Yatay mesafe", T::DistanceX}, {"Dikey mesafe", T::DistanceY},
                    {"Uzunluk", T::Length}, {"Yarıçap", T::Radius}, {"Çap", T::Diameter}, {"Açı", T::Angle}};
  for (const auto& c : dims) {
    QAction* a = mk(QString("dimension.%1").arg(int(c.t)), c.txt, QString(), false, false);
    const T t = c.t;
    connect(a, &QAction::triggered, this, [this, t] { if (d->host && d->host->sketchEditor()) d->host->sketchEditor()->applyDimension(t); });
    d->dims << a;
  }
  d->other << mk("combine", "Birleştir", "Birleştir (Combine) — I", true, true) << mk("plane", "Düzlem", "Düzlem (Plane)", true, true)
           << mk("shell", "Kabuk", "Kabuk (Shell)", true, true) << mk("sketch.delete", "Sil", "Sil (Delete) — Del", false, false);
  connect(d->byId.value("sketch.construction"), &QAction::toggled, this, [this](bool on) {
    if (!d->host || !d->host->sketchEditor()) return;
    d->host->sketchEditor()->setConstructionMode(on);
    d->host->sketchEditor()->toggleConstructionOfSelection();
    if (d->active && d->active->isSketchTool()) emit optionsChanged();
  });
  connect(d->byId.value("sketch.delete"), &QAction::triggered, this, [this] {
    if (d->host && d->host->sketchEditor()) d->host->sketchEditor()->deleteSelection();
  });
  // tools
  auto reg = [this](ToolBase* t) {
    d->tools.insert(t->id(), t);
    connect(t, &ToolBase::requestTool, this, [this](const QString& id) { QMetaObject::invokeMethod(this, [this, id] { activate(id); }, Qt::QueuedConnection); });
  };
  reg(new SelectTool(this));
  reg(new PullTool(this));
  reg(new MoveTool(this));
  reg(new FillTool(this));
  reg(new CombineTool(this));
  reg(new PlaneTool(this));
  reg(new ShellTool(this));
  using ST = SketchEditor::Tool;
  reg(new SketchToolAdapter("sketch.select", ST::Select, "Seç (Select)", this));
  reg(new SketchToolAdapter("sketch.line", ST::Line, "Çizgi (Line)", this));
  reg(new SketchToolAdapter("sketch.rect", ST::Rect, "Dikdörtgen (Rectangle)", this));
  reg(new SketchToolAdapter("sketch.circle", ST::Circle, "Çember (Circle)", this));
  reg(new SketchToolAdapter("sketch.arc", ST::Arc, "Yay (Sweep Arc)", this));
  reg(new SketchToolAdapter("sketch.point", ST::Point, "Nokta (Point)", this));
  reg(new SketchToolAdapter("sketch.trim", ST::Trim, "Kırp (Trim Away)", this));
  d->byId.value("select")->setChecked(true);
}

ToolManager::~ToolManager() {
  // no deactivate(): the host (usually the parent window) is already being destroyed
  for (auto& c : d->conns) disconnect(c);
  d->active = nullptr;
  delete d;
}

QAction* ToolManager::action(const QString& toolId) const { return d->byId.value(toolId, nullptr); }
QList<QAction*> ToolManager::editActions() const { return d->edit; }
QList<QAction*> ToolManager::sketchActions() const { return d->sketch; }
QList<QAction*> ToolManager::constraintActions() const { return d->cons; }
QList<QAction*> ToolManager::dimensionActions() const { return d->dims; }
QList<QAction*> ToolManager::otherActions() const { return d->other; }
QStringList ToolManager::toolIds() const { return d->toolIds; }
IActiveTool* ToolManager::active() const { return d->active; }
QString ToolManager::activeId() const { return d->activeId; }

bool ToolManager::activate(const QString& toolId) {
  if (!d->toolIds.contains(toolId) || !d->tools.contains(toolId == "select" ? QString("select") : toolId)) return false;
  QString tid = toolId;
  IDesignHost* h = d->host;
  const bool sketchMode = h && h->designMode() == DesignMode::Sketch;
  if (tid == "select" && sketchMode) tid = "sketch.select";
  if (tid == "sketch.select" && h && !sketchMode) tid = "select";
  if (tid.startsWith("sketch.") && h && !sketchMode) {
    // SPEC §2.1 [U]: a sketch tool chosen in 3D enters Sketch mode first
    d->activating = true;
    const bool ok = h->setDesignMode(DesignMode::Sketch);
    d->activating = false;
    if (!ok || h->designMode() != DesignMode::Sketch) {
      if (QAction* a = d->byId.value(d->activeId == "sketch.select" ? QString("select") : d->activeId)) a->setChecked(true);
      return false;
    }
  }
  ToolBase* next = d->tools.value(tid);
  if (!next) return false;
  if (d->active == next) {
    if (QAction* a = d->byId.value(tid == "sketch.select" ? QString("select") : tid); a && !a->isChecked()) a->setChecked(true);
    return true;
  }
  if (d->active) {
    // a busy tool is cancelled first (Esc semantics)
    for (int i = 0; i < 8 && d->active->busy(); ++i)
      if (!d->active->cancel()) break;
    for (auto& c : d->conns) disconnect(c);
    d->conns.clear();
    d->active->deactivate();
  }
  d->active = next;
  d->activeId = tid;
  if (h && h->viewer() && !d->overlay) {
    d->overlay = new ViewportOverlay(h->viewer());
    if (!d->mouse)
      d->mouse = new ViewerMouseRouter(this, [this] { return d->active; }, [this] { return d->mini.data(); });
    h->viewer()->installEventFilter(d->mouse);
    d->mouseViewer = h->viewer();
  }
  for (ToolBase* t : d->tools) t->setOverlay(d->overlay);
  if (d->overlay) { d->overlay->setTool(next); d->overlay->raise(); }
  d->conns << connect(next, &IActiveTool::promptChanged, this, [this](const QString& s) {
    emit promptChanged(s);
    if (d->host && !s.isEmpty()) d->host->showPrompt(s, 0);
  });
  d->conns << connect(next, &IActiveTool::optionsChanged, this, &ToolManager::optionsChanged);
  d->conns << connect(next, &IActiveTool::guidesChanged, this, &ToolManager::guidesChanged);
  d->conns << connect(next, &IActiveTool::operationChanged, this, &ToolManager::operationChanged);   // [SC23]
  d->conns << connect(next, &IActiveTool::finished, this, [this] {
    QMetaObject::invokeMethod(this, [this] { activate(d->host && d->host->designMode() == DesignMode::Sketch ? "sketch.select" : "select"); },
                              Qt::QueuedConnection);
  });
  if (h) next->activate(h);
  if (QAction* a = d->byId.value(tid == "sketch.select" ? QString("select") : tid); a && !a->isChecked()) a->setChecked(true);
  emit activeToolChanged(tid);
  emit optionsChanged();
  emit guidesChanged();
  emit promptChanged(next->prompt());
  if (h && !next->prompt().isEmpty()) h->showPrompt(next->prompt(), 0);
  emit operationChanged(next->operation());   // [SC23] initial state of the new tool
  return true;
}

bool ToolManager::cancel() {
  if (!d->active) return false;
  if (d->active->cancel()) return true;
  const bool sketch = d->host && d->host->designMode() == DesignMode::Sketch;
  const QString base = sketch ? "sketch.select" : "select";
  if (d->activeId != base) return activate(base);
  emit operationChanged(d->active->operation());   // SC23: Esc in idle Select = "next action" (clears a red HUD message)
  return false;
}

void ToolManager::installKeyRouting(QWidget* w) {
  if (w) w->installEventFilter(d->keys);
}

bool ToolManager::routeKey(QKeyEvent* e) {
  if (!d->active || e->type() != QEvent::KeyPress) return false;
  if (d->active->wantsKey(e) && d->active->keyPress(e)) return true;
  const int key = e->key();
  const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
  if (key == Qt::Key_Escape) {
    const bool did = cancel();
    return did || d->active->isSketchTool();
  }
  if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && !(m & (Qt::ControlModifier | Qt::AltModifier))) {
    if (d->active->guides().isEmpty()) return false;
    cycleGuide(key == Qt::Key_Backtab || (m & Qt::ShiftModifier) ? -1 : 1);
    return true;
  }
  if (key == Qt::Key_G && (m & Qt::ControlModifier)) return createGroup();
  if (key == Qt::Key_Delete && d->active->isSketchTool()) return deleteSelection();
  return false;
}

bool ToolManager::handlePick(const PickEvent& e) {
  if (!d->active) return false;
  if (d->mini && !d->mini->sketchMode() && e.viewerPos.x() >= 0) d->mini->setAnchor(e.viewerPos);
  return d->active->pick(e);
}

void ToolManager::handleHover(const SelItem& item) {
  if (d->active) d->active->hover(item);
}

bool ToolManager::createGroup() { return d->active && d->active->createGroup(); }
bool ToolManager::deleteSelection() { return d->active && d->active->deleteSelection(); }

void ToolManager::populateContextMenu(QMenu* menu, const QList<SelItem>& underCursor) {
  if (d->active && menu) d->active->populateContextMenu(menu, underCursor);
  // edges under the cursor also offer the Pull edge actions when Pull is not active
  if (menu && d->active && d->active->id() != "pull") {
    bool edge = false;
    for (const auto& it : underCursor) if (it.kind == SelItem::Kind::Edge && !it.name.startsWith("sketch:")) edge = true;
    if (edge) {
      menu->addSeparator();
      QAction* r = menu->addAction("Yuvarlat (Çek)");
      r->setObjectName("actPullRound");
      connect(r, &QAction::triggered, this, [this, underCursor] {
        if (!activate("pull")) return;
        d->active->setOption("edge", "round");
        if (d->host) d->host->selection()->set(underCursor);
      });
      QAction* c = menu->addAction("Pahla (Çek)");
      c->setObjectName("actPullChamfer");
      connect(c, &QAction::triggered, this, [this, underCursor] {
        if (!activate("pull")) return;
        d->active->setOption("edge", "chamfer");
        if (d->host) d->host->selection()->set(underCursor);
      });
    }
  }
}

void ToolManager::onDocumentChanged() {
  if (d->overlay) d->overlay->clearMarkers();   // SC23: End Sketch open-end markers belong to the old document
  if (d->active) d->active->documentChanged();
  if (d->overlay) d->overlay->update();
  if (d->active) emit operationChanged(d->active->operation());   // SC23: the new document ends an earlier refusal
}

void ToolManager::onDesignModeChanged(pcd::DesignMode m) {
  if (m == DesignMode::Sketch && d->overlay) d->overlay->clearMarkers();   // SC23
  if (!d->activating) {
    if (m == DesignMode::Sketch) {
      if (!d->active || !d->active->isSketchTool()) activate("sketch.select");
      else if (d->host && d->host->sketchEditor()) d->active->activate(d->host);   // re-bind to the new sketch session
    } else if (d->active && d->active->isSketchTool()) {
      activate("select");
    }
  }
  if (d->mini) d->mini->rebuild();
  if (d->guides) d->guides->rebuild();
}

QWidget* ToolManager::createMiniToolbar(QWidget* viewer) {
  if (!viewer) return nullptr;
  if (!d->mini) d->mini = new MiniToolbar(this, d->host, viewer);
  return d->mini;
}

QWidget* ToolManager::createToolGuideBar(QWidget* viewer) {
  if (!viewer) return nullptr;
  if (!d->guides) d->guides = new ToolGuideBar(this, viewer);
  return d->guides;
}

QWidget* ToolManager::createOptionsWidget(QWidget* parent) { return d->active ? d->active->createOptionsWidget(parent) : nullptr; }

void ToolManager::setOption(const QString& key, const QVariant& value) {
  if (d->active) d->active->setOption(key, value);
}

void ToolManager::cycleGuide(int step) {
  if (!d->active) return;
  const QList<ToolGuide> gs = d->active->guides();
  QList<int> en;
  for (int i = 0; i < gs.size(); ++i) if (gs[i].enabled) en << i;
  if (en.isEmpty()) return;
  int cur = 0;
  for (int k = 0; k < en.size(); ++k) if (gs[en[k]].key == d->active->activeGuide()) cur = k;
  const int n = en.size();
  const int nk = ((cur + step) % n + n) % n;
  d->active->setActiveGuide(gs[en[nk]].key);
  emit guidesChanged();
}

} // namespace pcd
