// SC-UX shell (WP-A): construction of the SpaceClaim-like window (actions, menus, ribbon, docks, status bar),
// selection wiring, panels, context menus, body visibility commands, layout persistence, SC23 viewport HUD wiring.
// rebuildStructure() lives in MainWindowStructure.cpp (SC23, WP-O).
#include "ExprEdit.h"
#include "Icons.h"
#include "MainWindow.h"
#include "ParamSelectUi.h"
#include "OptionsPanel.h"
#include "PropertiesPanel.h"
#include "Ribbon.h"
#include "NamedSelectionPanel.h"
#include "SelectionPanel.h"
#include "Shortcuts.h"
#include "StatusBarWidgets.h"
#include "StructurePanel.h"
#include "ViewportHud.h"
#include "ShellUtil.h"
#include "DocumentTabBar.h"
#include "HelpViewer.h"
#include "ViewsPanel.h"
#include "tools/MiniToolbar.h"
#include <QCheckBox>
#include <QCursor>
#include <QStyle>
#include <QStyleOption>
#include <QPainter>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QScrollArea>
#include <QTabBar>
#include <QTreeWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <cmath>
#ifdef PC_HAVE_DRAWING_IMPORT
#include "pc/drawing/ui/ImportAction.h"
#endif

using namespace pc;

namespace pcd {

namespace {
QString kindTextShell(ParamKind k) {
  switch (k) {
    case ParamKind::Length: return "mm";
    case ParamKind::Angle: return "°";
    case ParamKind::Count: return "adet";
    default: return "-";
  }
}
class KindDelegateShell : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
    auto* c = new QComboBox(parent);
    c->addItems({"mm", "°", "adet", "-"});
    return c;
  }
  void setEditorData(QWidget* e, const QModelIndex& i) const override { static_cast<QComboBox*>(e)->setCurrentText(i.data(Qt::EditRole).toString()); }
  void setModelData(QWidget* e, QAbstractItemModel* m, const QModelIndex& i) const override { m->setData(i, static_cast<QComboBox*>(e)->currentText()); }
};
QAction* mk(QObject* parent, const QString& name, const QString& text, const QString& icon, const QString& tip, int iconSize = 32) {
  auto* a = new QAction(icon.isEmpty() ? QIcon() : makeIcon(icon, iconSize), text, parent);
  a->setObjectName(name);
  if (!tip.isEmpty()) a->setToolTip(tip);
  return a;
}
// SpaceClaim-style tooltip "Türkçe (English)" on actions created by WP-B without one
void decorate(QAction* a, const QString& icon, const QString& tip, const QString& iconText = QString()) {
  if (!a) return;
  if (a->icon().isNull() && !icon.isEmpty()) a->setIcon(makeIcon(icon, 32));
  if ((a->toolTip().isEmpty() || a->toolTip() == a->text()) && !tip.isEmpty()) a->setToolTip(tip);
  if (!iconText.isEmpty()) a->setIconText(iconText);
}
// SC23 WP-S (S-P1-9): F6 / Shift+F6 cycles the keyboard focus through the window's zones:
// graphics area (sketch overlay while sketching) -> Structure panel page -> Options / Properties page -> ribbon tabs.
QWidget* focusTarget(QWidget* zone) {
  if (!zone || !zone->isVisible()) return nullptr;
  if (zone->focusPolicy() != Qt::NoFocus) return zone;
  for (QWidget* c : zone->findChildren<QWidget*>())
    if (c->isVisible() && c->isEnabled() && (c->focusPolicy() & Qt::TabFocus)) return c;
  return zone;
}
void cycleFocus(MainWindow* w, int dir) {
  struct Z { QWidget* w; const char* name; };
  auto* st = w->findChild<QTabWidget*>("structureTabs");
  auto* ot = w->findChild<QTabWidget*>("optionsTabs");
  const QList<Z> zones = {{w->inSketchMode() ? static_cast<QWidget*>(w->sketchEditor()) : w->viewerWidget(), "Grafik alanı"},
                          {st ? st->currentWidget() : nullptr, "Yapı paneli"},
                          {ot ? ot->currentWidget() : nullptr, "Seçenekler / Özellikler"},
                          {w->ribbon() ? w->ribbon()->tabBar() : nullptr, "Şerit sekmeleri"}};
  QWidget* f = QApplication::focusWidget();
  int cur = -1;
  for (int i = 0; i < zones.size() && cur < 0; ++i)
    if (zones[i].w && f && (f == zones[i].w || zones[i].w->isAncestorOf(f))) cur = i;
  if (cur < 0 && f && w->viewerWidget()->isAncestorOf(f)) cur = 0;
  for (int k = 1; k <= zones.size(); ++k) {
    const int i = ((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir * k + 4 * zones.size()) % zones.size();
    if (QWidget* t = focusTarget(zones[i].w)) {
      t->setFocus(dir > 0 ? Qt::TabFocusReason : Qt::BacktabFocusReason);
      w->statusBar()->showMessage(QString("Odak: %1 (F6 / Shift+F6)").arg(QString::fromUtf8(zones[i].name)), 2500);
      return;
    }
  }
}

// SC23 WP-S (S-P1-6): SpaceClaim-style tooltips for ribbon / quick-access / guide buttons: name, shortcut,
// description, "F1: yardım" ([G] §2; the F3 video line is not offered). Built from the action (tooltip text
// "Ad (English): açıklama — K"), so the action tooltips themselves stay unchanged.
class RichToolTipFilter : public QObject {
public:
  RichToolTipFilter(QWidget* window) : QObject(window), window_(window) {}
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() != QEvent::ToolTip) return false;
    auto* b = qobject_cast<QToolButton*>(o);
    if (!b || !b->defaultAction() || b->window() != window_) return false;
    QString html = richToolTip(b->defaultAction());
    if (html.isEmpty()) return false;
    if (!b->isEnabled() && b->defaultAction()->isEnabled())   // disabled through its ribbon group: say why
      for (QWidget* p = b->parentWidget(); p; p = p->parentWidget())
        if (const QString why = p->property("disabledReason").toString(); !why.isEmpty() && !p->isEnabled()) {
          html.replace("</qt>", "<br><span style='color:#8a5a00'>" + why.toHtmlEscaped() + "</span></qt>");
          break;
        }
    QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), html, b);
    return true;
  }
private:
  QWidget* window_;
};

} // namespace

// SC23 WP-S (S-P1-9): dock title bar with a collapse button (SpaceClaim panel headers carry pin icons [G]; the
// exact pin semantics are [D], PersistCAD collapses the panel to its title so the neighbour panel takes the room).
// objectNames: "dockTitle.<dock>", buttons "dockCollapse" / "dockFloat" / "dockClose". The dock keeps the property
// "collapsed" (bool).
void setDockCollapsed(QDockWidget* d, bool on) {
  if (!d || !d->widget() || d->property("collapsed").toBool() == on) return;
  QWidget* tb = d->titleBarWidget();
  if (on) {
    d->setProperty("expandedHeight", d->height());
    d->widget()->hide();
    const int h = (tb ? tb->sizeHint().height() : 22) + 2;
    d->setMinimumHeight(h);
    d->setMaximumHeight(h);
  } else {
    d->setMinimumHeight(0);
    d->setMaximumHeight(QWIDGETSIZE_MAX);
    d->widget()->show();
    if (auto* mw = qobject_cast<QMainWindow*>(d->parentWidget()))
      mw->resizeDocks({d}, {std::max(160, d->property("expandedHeight").toInt())}, Qt::Vertical);
  }
  d->setProperty("collapsed", on);
  if (tb)
    if (auto* b = tb->findChild<QToolButton*>("dockCollapse")) {
      b->setIcon(d->style()->standardIcon(on ? QStyle::SP_TitleBarUnshadeButton : QStyle::SP_TitleBarShadeButton));
      b->setToolTip(on ? "Paneli aç" : "Paneli daralt (yalnız başlık kalır)");
    }
}
bool dockCollapsed(const QDockWidget* d) { return d && d->property("collapsed").toBool(); }

namespace {
void installDockTitleBar(QDockWidget* d) {
  auto* bar = new QWidget(d);
  bar->setObjectName("dockTitle." + d->objectName());
  bar->setAttribute(Qt::WA_StyledBackground, true);
  bar->setStyleSheet("QWidget#dockTitle\\." + d->objectName() + " { background: #e3e9f1; border-bottom: 1px solid #c3cedb; }"
                     "QLabel { color: #24364d; font-weight: 600; }"
                     "QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 0px; }"
                     "QToolButton:hover { background: #d3e0f0; border-color: #9fbbe0; }");
  auto* h = new QHBoxLayout(bar);
  h->setContentsMargins(6, 2, 2, 2);
  h->setSpacing(1);
  auto* lbl = new QLabel(d->windowTitle(), bar);
  lbl->setObjectName("dockTitleText");
  h->addWidget(lbl, 1);
  QObject::connect(d, &QDockWidget::windowTitleChanged, lbl, &QLabel::setText);
  auto mkBtn = [&](const char* name, QStyle::StandardPixmap px, const QString& tip) {
    auto* b = new QToolButton(bar);
    b->setObjectName(name);
    b->setIcon(d->style()->standardIcon(px));
    b->setIconSize(QSize(12, 12));
    b->setFixedSize(18, 18);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    h->addWidget(b);
    return b;
  };
  auto* col = mkBtn("dockCollapse", QStyle::SP_TitleBarShadeButton, "Paneli daralt (yalnız başlık kalır)");
  QObject::connect(col, &QToolButton::clicked, d, [d] { setDockCollapsed(d, !d->property("collapsed").toBool()); });
  auto* fl = mkBtn("dockFloat", QStyle::SP_TitleBarNormalButton, "Paneli ayır / yerine koy");
  QObject::connect(fl, &QToolButton::clicked, d, [d] { d->setFloating(!d->isFloating()); });
  auto* cl = mkBtn("dockClose", QStyle::SP_TitleBarCloseButton, "Paneli kapat (Görüntü > Pencere ile geri açılır)");
  QObject::connect(cl, &QToolButton::clicked, d, [d] { d->close(); });
  d->setTitleBarWidget(bar);
  d->setProperty("collapsed", false);
}

// SC23 WP-S (S-P1-3): QStatusBar paints its temporary message from a fixed left offset. With the error icon shown
// (property "errorIcon") the message is painted right of the icon instead, so icon and text never overlap.
class PromptStatusBar : public QStatusBar {
public:
  using QStatusBar::QStatusBar;
protected:
  void paintEvent(QPaintEvent* e) override {
    const QString msg = currentMessage();
    if (!property("errorIcon").toBool() || msg.isEmpty()) { QStatusBar::paintEvent(e); return; }
    QPainter p(this);
    QStyleOption opt;
    opt.initFrom(this);
    style()->drawPrimitive(QStyle::PE_PanelStatusBar, &opt, &p, this);
    int right = width() - 8;
    for (QWidget* c : findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
      if (c->isVisible() && c->objectName() != "statusSeverity" && c->x() > 30) right = std::min(right, c->x() - 6);
    const QRect r(28, 0, std::max(0, right - 28), height());
    p.setPen(palette().color(foregroundRole()));
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(msg, Qt::ElideRight, r.width()));
  }
};

const char* kLayoutKey = "layout/scux-v1";
// SC23 WP-S (S-P1-1): Sketch-tab groups are DISABLED (not hidden) outside sketch mode; the Tasarım groups stay
// visible and are disabled while sketching where they would act on the 3D model. Single owner of the sketch-mode
// ribbon state (setSketchMode no longer touches the ribbon).
void syncSketchRibbon(Ribbon* rb, bool sketching) {
  if (!rb) return;
  for (const char* k : {"sketchCreate", "sketchModify", "constraints", "dimensions", "sketchSession"})
    if (RibbonGroup* g = rb->group(k)) {
      g->setVisible(true);
      g->setEnabled(sketching);
      g->setProperty("disabledReason", "Sketch modunda etkin: K ile sketch açın ya da düzlemsel bir yüz seçip K'ye basın.");
    }
  for (const char* k : {"intersect", "create", "body", "drawingImport"})
    if (RibbonGroup* g = rb->group(k)) {
      g->setVisible(true);
      g->setEnabled(!sketching);
      g->setProperty("disabledReason", "Sketch açıkken pasif: önce Sketch'i Bitir (D).");
    }
}
bool layoutPersistence() { return QCoreApplication::applicationName().startsWith("persistcad"); }
} // namespace

// ============================================================================ actions & menus
void MainWindow::buildActions() {
  // NOTE: creation order matters for harnesses that search actions by text prefix (findChildren order):
  // "Yeni" and "Aç..." are the first actions starting with those words.
  actNew_ = mk(this, "actNew", "Yeni", "new", "Yeni belge (New)", 16);
  connect(actNew_, &QAction::triggered, this, [this] { if (maybeSave()) newDocument(); });
  actOpen_ = mk(this, "actOpen", "Aç...", "open", "Aç (Open)", 16);
  connect(actOpen_, &QAction::triggered, this, [this] { fileOpen(); });
  actSave_ = mk(this, "actSave", "Kaydet", "save", "Kaydet (Save)", 16);
  connect(actSave_, &QAction::triggered, this, [this] { fileSave(); });
  actSaveAs_ = mk(this, "actSaveAs", "Farklı Kaydet...", "save", "Farklı kaydet (Save As)", 16);
  connect(actSaveAs_, &QAction::triggered, this, [this] { fileSaveAs(); });
  actSaveAsApp_ = mk(this, "actSaveAsApp", "Uygulama Olarak Kaydet... (Save as App)", "saveapp",
                     "Uygulama olarak kaydet (Save as App): parametreler ve model bağımsız bir konfigüratör uygulamasına dönüşür");
  actSaveAsApp_->setIconText("Uygulama olarak\nkaydet");
  connect(actSaveAsApp_, &QAction::triggered, this, [this] { saveAsApp(); });
  actUndo_ = mk(this, "actUndo", "Geri Al", "undo", "Geri al (Undo)", 16);
  connect(actUndo_, &QAction::triggered, this, [this] { if (sketching_) sketchEd_->undo(); else undo_.undo(); });
  actRedo_ = mk(this, "actRedo", "Yinele", "redo", "Yinele (Redo)", 16);
  connect(actRedo_, &QAction::triggered, this, [this] { if (sketching_) sketchEd_->redo(); else undo_.redo(); });
  actQuit_ = mk(this, "actQuit", "Çıkış", "", "Çıkış (Exit)");
  connect(actQuit_, &QAction::triggered, this, [this] { close(); });
  actMfgSettings_ = mk(this, "actMfgSettings", "Ayarlar...", "settings", "İmalat ayarları (süreç, kurallar, stok)");
  connect(actMfgSettings_, &QAction::triggered, this, [this] { openMfgSettings(); });
  actCheckNow_ = mk(this, "actCheckNow", "Şimdi Denetle", "check", "İmalat denetimini şimdi çalıştır");
  actCheckNow_->setIconText("Şimdi\nDenetle");
  connect(actCheckNow_, &QAction::triggered, this, [this] { runCheck(); });

  // Orient group
  actHome_ = mk(this, "actHomeView", "Ana görünüm", "home", "Ana görünüm (Home): izometrik + sığdır", 16);
  connect(actHome_, &QAction::triggered, this, [this] { homeView(); });
  actPlanView_ = mk(this, "actPlanView", "Plan görünümü", "planview", "Plan görünümü (Plan View): sketch düzlemine / seçili düzleme dik bak", 16);
  connect(actPlanView_, &QAction::triggered, this, [this] { planView(); });
  actFit_ = mk(this, "actZoomExtents", "Sığdır", "fit", "Sığdır (Zoom Extents)", 16);
  connect(actFit_, &QAction::triggered, this, [this] { zoomExtents(); });
  actZoomIn_ = mk(this, "actZoomIn", "Yakınlaştır", "zoomin", "Yakınlaştır (Zoom In)", 16);
  connect(actZoomIn_, &QAction::triggered, this, [this] { zoomStep(+1); });
  actZoomOut_ = mk(this, "actZoomOut", "Uzaklaştır", "zoomout", "Uzaklaştır (Zoom Out)", 16);
  connect(actZoomOut_, &QAction::triggered, this, [this] { zoomStep(-1); });
  actPrevView_ = mk(this, "actPrevView", "Önceki görünüm", "prevview", "Önceki görünüm (Previous View)", 16);
  connect(actPrevView_, &QAction::triggered, this, [this] { previousView(); });
  actNextView_ = mk(this, "actNextView", "Sonraki görünüm", "nextview", "Sonraki görünüm (Next View)", 16);
  connect(actNextView_, &QAction::triggered, this, [this] { nextView(); });
  actSpin_ = mk(this, "actSpin", "Çevir", "spin", "Çevir (Spin): sol sürükleme modeli döndürür (orta düğme her zaman çevirir)", 16);
  actPan_ = mk(this, "actPan", "Kaydır", "pan", "Kaydır (Pan): sol sürükleme görünümü kaydırır (Shift+orta düğme)", 16);
  actZoomDrag_ = mk(this, "actZoomDrag", "Yakınlaştır", "zoom", "Yakınlaştır (Zoom): sol sürükleme yukarı/aşağı yakınlaştırır (Ctrl+orta düğme)", 16);
  int k = 1;
  for (QAction* a : {actSpin_, actPan_, actZoomDrag_}) {
    a->setCheckable(true);
    const int mode = k++;
    connect(a, &QAction::toggled, this, [this, mode](bool on) { setLeftDragMode(on ? mode : 0); });
  }

  // Mode group (SPEC §2.1). The section button is SectionControls::toggleAction() itself ("Kesit").
  actModeSketch_ = mk(this, "actModeSketch", "Sketch modu", "modesketch", "Sketch modu (Sketch Mode): seçili yüzde / düzlemde sketch");
  actModeSketch_->setIconText("Sketch\nmodu");
  actModeSketch_->setCheckable(true);
  connect(actModeSketch_, &QAction::triggered, this, [this] { setDesignMode(DesignMode::Sketch); syncModeActions(); });
  actModeSection_ = mk(this, "actModeSection", "Kesit modu", "section", "Kesit modu (Section Mode)");
  actModeSection_->setCheckable(true);
  connect(actModeSection_, &QAction::triggered, this, [this] { setDesignMode(DesignMode::Section); syncModeActions(); });
  actMode3D_ = mk(this, "actMode3D", "3B modu", "mode3d", "3B modu (3D Mode): açık sketch'i bitirir, kesiti kapatır");
  actMode3D_->setIconText("3B\nmodu");
  actMode3D_->setCheckable(true);
  connect(actMode3D_, &QAction::triggered, this, [this] { setDesignMode(DesignMode::ThreeD); syncModeActions(); });
  QAction* sec = section_->toggleAction();
  sec->setIcon(makeIcon("section", 32));
  sec->setIconText("Kesit\nmodu");
  sec->setToolTip("Kesit modu (Section Mode) — X");
  connect(sec, &QAction::triggered, this, [this](bool on) {
    // clicking the ribbon button while sketching: the sketch is committed first (modes are exclusive)
    if (on && sketching_ && !finishSketch(true)) section_->setSectionOn(false);
    if (on) cancelPlanePick();
    notifyDesignMode();
  });

  // Sketch session (contextual)
  actFinishSketch_ = mk(this, "actFinishSketch", "Sketch'i Bitir", "ok", "Sketch'i bitir ve 3B'ye dön (Return to 3D Mode) — D");
  actFinishSketch_->setIconText("Sketch'i\nBitir");
  connect(actFinishSketch_, &QAction::triggered, this, [this] { finishSketch(true); });
  actCancelSketch_ = mk(this, "actCancelSketch", "Vazgeç", "error", "Sketch değişikliklerini at (Cancel)");
  connect(actCancelSketch_, &QAction::triggered, this, [this] { finishSketch(false); });

  // Groups, bodies, misc
  actCreateGroup_ = mk(this, "actCreateGroup", "Grup oluştur", "group", "Grup oluştur (Create Group): seçili yüz / kenar / gövde -> adlandırılmış seçim; ölçü / değer -> parametre — Ctrl+G", 16);
  connect(actCreateGroup_, &QAction::triggered, this, [this] {
    // context-sensitive, like SpaceClaim: a tool's value box / the dimension just pulled -> parameter (tool);
    // faces / edges / bodies selected in 3D -> named selection; a measurable sketch selection -> parameter popup
    if (tools_ && tools_->createGroup()) return;
    if (canCreateNamedSelection()) { createNamedSelectionFromSelection(true); return; }
    if (paramSel_ && paramSel_->triggerPrimary()) return;   // Seçimden parametre: the selected dimension
    createGroupInteractive();
  });
  actHideBody_ = mk(this, "actHideBody", "Gizle", "hide", "Gizle (Hide): seçili yüzler -> yüzler, seçili gövdeler -> gövdeler (görünüm durumu, geometri değişmez)", 16);
  connect(actHideBody_, &QAction::triggered, this, [this] { hideSelectedBodies(); });
  actHideOthers_ = mk(this, "actHideOthers", "Diğerlerini gizle", "hideothers", "Diğerlerini gizle (Hide Others): yalnız seçili yüzler / gövdeler görünür kalır", 16);
  connect(actHideOthers_, &QAction::triggered, this, [this] { hideOtherBodies(); });
  actShowAll_ = mk(this, "actShowAll", "Tümünü göster", "showall", "Tümünü göster (Show All): gizli yüz ve gövdeleri göster", 16);
  connect(actShowAll_, &QAction::triggered, this, [this] { showAllBodies(); });
  actTransparent_ = mk(this, "actTransparent", "Saydam yap", "transparent", "Saydam yap: seçili yüzleri / gövdeleri yarı saydam göster (tekrar: kaldır)", 16);
  connect(actTransparent_, &QAction::triggered, this, [this] { toggleSelectionTransparent(); });
  actDelete_ = mk(this, "actDelete", "Seçili özelliği sil", "delete", "Sil (Delete): seçili özellik(ler) / sketch öğeleri", 16);
  connect(actDelete_, &QAction::triggered, this, [this] { deleteSelectionCommand(); });
  actResetLayout_ = mk(this, "actResetLayout", "Düzeni sıfırla", "layout", "Panelleri varsayılan düzene döndür");
  actResetLayout_->setIconText("Düzeni\nsıfırla");
  connect(actResetLayout_, &QAction::triggered, this, [this] { resetLayout(); });
  actMass_ = mk(this, "actMassProperties", "Kütle özellikleri", "mass", "Kütle özellikleri (Mass): seçili gövdelerin hacim, alan, ağırlık merkezi, sınır kutusu");
  actMass_->setIconText("Kütle\nözellikleri");
  connect(actMass_, &QAction::triggered, this, [this] { showMassProperties(); });
  actMeasure_ = mk(this, "actMeasure", "Ölç", "measure", "Ölç (Measure) — E (2. faz)");
  actMeasure_->setEnabled(false);
  actFileMenu_ = mk(this, "actFileMenu", "Dosya menüsü", "", "Dosya menüsü (File)");
  connect(actFileMenu_, &QAction::triggered, this, [this] { if (ribbon_) ribbon_->fileButton()->showMenu(); });
  // SC23 WP-S: standard views with SpaceClaim 2023 R1 shortcuts (Views panel [G]: Isometric Ctrl+0 ... Front Ctrl+6);
  // Arka (Back) has no shortcut ([D] what SpaceClaim lists below Front). Shortcuts: Shortcuts.cpp table.
  {
    struct SV { const char* name; const char* text; const char* icon; viewer::ShapeViewer::View v; const char* en; };
    const SV svs[] = {{"actViewIso", "İzometrik", "iso", viewer::ShapeViewer::View::Iso, "Isometric"},
                      {"actViewTrimetric", "Trimetrik", "trimetric", viewer::ShapeViewer::View::Trimetric, "Trimetric"},
                      {"actViewTop", "Üst", "top", viewer::ShapeViewer::View::Top, "Top"},
                      {"actViewBottom", "Alt", "top", viewer::ShapeViewer::View::Bottom, "Bottom"},
                      {"actViewLeft", "Sol", "right", viewer::ShapeViewer::View::Left, "Left"},
                      {"actViewRight", "Sağ", "right", viewer::ShapeViewer::View::Right, "Right"},
                      {"actViewFront", "Ön", "front", viewer::ShapeViewer::View::Front, "Front"},
                      {"actViewBack", "Arka", "front", viewer::ShapeViewer::View::Back, "Back"}};
    for (const SV& sv : svs) {
      QAction* a = mk(this, sv.name, QString::fromUtf8(sv.text), sv.icon,
                      QString("%1 (%2): standart görünüm; yalnız kamera değişir, sketch düzlemi değişmez").arg(QString::fromUtf8(sv.text), sv.en), 16);
      const auto vv = sv.v;
      // camera only (T10): in sketch mode the overlay follows the viewer camera, the sketch plane is untouched
      connect(a, &QAction::triggered, this, [this, vv] { if (sketching_ && !sketchEd_->attachedViewer()) sketchEd_->fitView(); else viewer_->setView(vv); });
      if (vv == viewer::ShapeViewer::View::Trimetric) actViewTrimetric_ = a;
    }
  }
  // SC23 WP-S: F1 help (S-P1-6), F6 / Shift+F6 panel focus cycle (S-P1-9)
  auto* help = mk(this, "actHelp", "Yardım", "info", "Yardım (Help): kullanım kılavuzu; bir düğmenin üzerindeyken o komutun bölümü", 16);
  connect(help, &QAction::triggered, this, [this] {
    QString topic;
    if (auto* b = qobject_cast<QToolButton*>(QApplication::widgetAt(QCursor::pos())); b && b->defaultAction())
      topic = b->defaultAction()->text().section(" (", 0, 0).section("...", 0, 0).replace('\n', ' ').trimmed();
    HelpViewer::showHelp(this, topic);
  });
  auto* fNext = mk(this, "actFocusNext", "Sonraki panel", "", "Odağı sonraki bölgeye taşı: görünüm → Yapı → Seçenekler → şerit (F6)", 16);
  connect(fNext, &QAction::triggered, this, [this] { cycleFocus(this, +1); });
  auto* fPrev = mk(this, "actFocusPrev", "Önceki panel", "", "Odağı önceki bölgeye taşı (Shift+F6)", 16);
  connect(fPrev, &QAction::triggered, this, [this] { cycleFocus(this, -1); });
  for (auto [name, text, icon] : {std::tuple{"actToggleStructure", "Yapı paneli", "bodies"}, std::tuple{"actToggleOptions", "Seçenekler paneli", "options"},
                                  std::tuple{"actToggleIssues", "İmalat Denetimi", "panel"}}) {
    QAction* a = mk(this, name, QString::fromUtf8(text), icon, QString::fromUtf8(text) + ": paneli göster / gizle", 32);
    a->setCheckable(true);
    a->setChecked(true);
  }
  connect(&undo_, &QUndoStack::canUndoChanged, this, [this](bool c) { if (!sketching_) actUndo_->setEnabled(c); });
  connect(&undo_, &QUndoStack::canRedoChanged, this, [this](bool c) { if (!sketching_) actRedo_->setEnabled(c); });
}

void MainWindow::buildMenus() {
  // The menu bar stays hidden (SPEC §1.2): every menu lives on in the ribbon (Dosya button, ▾ buttons).
  menuFile_ = new QMenu("&Dosya", this);
  menuFile_->setObjectName("menuFile");
  menuFile_->addAction(actNew_);
  menuFile_->addAction(actOpen_);
  menuFile_->addAction(actSave_);
  menuFile_->addAction(actSaveAs_);
  menuFile_->addSeparator();
  menuFile_->addAction(actSaveAsApp_);
  menuFile_->addSeparator();
  auto* mExp = menuFile_->addMenu(makeIcon("export", 16), "Dışa Aktar");
  mExp->setObjectName("menuExport");
  connect(mExp->addAction("STEP (.step)..."), &QAction::triggered, this, [this] { exportShape(true); });
  connect(mExp->addAction("STL (.stl)..."), &QAction::triggered, this, [this] { exportShape(false); });
#ifdef PC_HAVE_DRAWING_IMPORT
  {  // DRW: designer hook — Dosya > "Teknik resimden içe aktar…" (wizard result = ONE undo step)
    pc::drawing::ui::DrawingImportHost h;
    h.currentDocument = [this] { return doc_; };
    h.commit = [this](const pc::Document& merged, const QString& undoText) { commitDocument(merged, undoText); };
    QAction* aDrw = pc::drawing::ui::installDrawingImportAction(this, nullptr, h);
    aDrw->setIcon(makeIcon("open", 16));
    menuFile_->insertAction(mExp->menuAction(), aDrw);
  }
#endif
  recentMenu_ = menuFile_->addMenu("Son dosyalar");
  recentMenu_->setObjectName("menuRecent");
  auto* mSet = menuFile_->addMenu(makeIcon("settings", 16), "Ayarlar");
  mSet->setObjectName("menuSettings");
  auto* aMfg = mSet->addAction(makeIcon("settings", 16), "İmalat ayarları...");
  connect(aMfg, &QAction::triggered, this, [this] { openMfgSettings(); });
  menuFile_->addSeparator();
  connect(menuFile_->addAction("Hakkında"), &QAction::triggered, this, [this] {
    QMessageBox::about(this, "PersistCAD Hakkında",
                       "<b>PersistCAD</b> 0.1<br>Parametrik, kalıcı yüz isimlendirmeli (SpaceClaim benzeri) CAD.<br>"
                       "Her parametre değişikliği katmanlı ve konvansiyonel imalat kurallarına göre denetlenir; "
                       "model ve parametreler 'Uygulama Olarak Kaydet' ile bağımsız bir konfigüratör uygulamasına dönüştürülebilir.<br><br>"
                       "C++17 · Qt 6 · OpenCASCADE 7.6");
  });
  menuFile_->addAction(actQuit_);

  // Klasik ▾ (former "Ekle" menu): the feature dialogs with their unchanged texts
  insertMenu_ = new QMenu("Klasik", this);
  insertMenu_->setObjectName("menuClassic");
  struct FT { FeatureType t; const char* txt; const char* icon; };
  const FT fts[] = {{FeatureType::Sketch, "Sketch...", "sketch"}, {FeatureType::Plane, "Düzlem...", "plane"}, {FeatureType::Extrude, "Extrude...", "extrude"},
                    {FeatureType::Revolve, "Revolve...", "revolve"}, {FeatureType::Hole, "Delik...", "hole"}, {FeatureType::Fillet, "Fillet...", "fillet"},
                    {FeatureType::Chamfer, "Chamfer...", "chamfer"}, {FeatureType::Shell, "Shell...", "shell"}, {FeatureType::Pattern, "Pattern...", "pattern"}};
  for (const auto& f : fts) {
    QAction* a = insertMenu_->addAction(makeIcon(f.icon, 16), f.txt);
    const FeatureType t = f.t;
    connect(a, &QAction::triggered, this, [this, t] { newFeature(t); });
    featureActions_ << a;
  }
  actClassicCombine_ = insertMenu_->addAction(makeIcon("combine", 32), "Gövdeleri Birleştir (Combine)...");
  actClassicCombine_->setData("menuOnly");
  actClassicCombine_->setIconText("Gövdeleri\nbirleştir");
  actClassicCombine_->setToolTip("Gövdeleri birleştir (Combine): Ekle / Çıkar / Kesişim (çoklu gövde)");
  connect(actClassicCombine_, &QAction::triggered, this, [this] { newFeature(FeatureType::Combine); });
  featureActions_ << actClassicCombine_;

  // Desen ▾
  menuPattern_ = new QMenu("Desen", this);
  menuPattern_->setObjectName("menuPattern");
  for (auto [txt, kind] : {std::pair{"Doğrusal desen (Linear Pattern)...", "linear"}, std::pair{"Dairesel desen (Circular Pattern)...", "circular"}}) {
    QAction* a = menuPattern_->addAction(makeIcon("pattern", 16), QString::fromUtf8(txt));
    const std::string kk = kind;
    connect(a, &QAction::triggered, this, [this, kk] {
      if (sketching_) return;
      Feature f;
      f.type = FeatureType::Pattern;
      f.id = doc_.uniqueFeatureId(FeatureType::Pattern);
      f.props["kind"] = kk;
      // source = the feature of the selected face / feature (SPEC §3 #24)
      for (const SelItem& s : selection_->items()) {
        const std::string fid = s.kind == SelItem::Kind::Face ? s.name.section('.', 0, 0).toStdString()
                                : s.kind == SelItem::Kind::Feature ? s.name.toStdString() : std::string();
        if (!fid.empty() && doc_.feature(fid)) { f.props["source"] = fid; break; }
      }
      if (!f.props.count("source") && !selectedFace_.isEmpty()) f.props["source"] = selectedFace_.section('.', 0, 0).toStdString();
      openFeatureDialog(f, true);
    });
  }

  // Görünümler ▾ (Design > Orient) and Görünüm ▾ (Display): menu title "Görünüm" (gui_harness_v4 looks for it).
  // SC23 WP-S: the entries are the named standard-view actions (shortcuts Ctrl+0..6, also in the Görünümler panel).
  menuViews_ = new QMenu("Görünüm", this);
  menuViews_->setObjectName("menuViews");
  menuView_ = new QMenu("Görünüm", this);
  menuView_->setObjectName("menuView");
  for (const char* n : {"actViewIso", "actViewTrimetric", "actViewTop", "actViewBottom", "actViewFront", "actViewBack", "actViewLeft", "actViewRight"})
    if (QAction* a = findChild<QAction*>(n)) { menuViews_->addAction(a); menuView_->addAction(a); }
  menuViews_->addSeparator();
  menuViews_->addAction(actHome_);
  menuViews_->addAction(actFit_);
  menuView_->addSeparator();
  menuView_->addAction(actFit_);
  menuView_->addSeparator();
  menuView_->addAction(section_->toggleAction());
  auto* mSec = menuView_->addMenu(makeIcon("section", 16), "Kesit düzlemi");
  mSec->setObjectName("menuSectionPlane");
  auto* secGroup = new QActionGroup(this);
  struct SP { const char* txt; const char* name; viewer::ShapeViewer::SectionPreset p; };
  const SP secPresets[] = {{"XZ düzlemi", "actSectionXZ", viewer::ShapeViewer::SectionPreset::XZ},
                           {"YZ düzlemi", "actSectionYZ", viewer::ShapeViewer::SectionPreset::YZ},
                           {"XY düzlemi", "actSectionXY", viewer::ShapeViewer::SectionPreset::XY},
                           {"Çeyrek kesit", "actSectionQuarter", viewer::ShapeViewer::SectionPreset::Quarter}};
  for (const auto& sp : secPresets) {
    QAction* a = mSec->addAction(sp.txt);
    a->setObjectName(sp.name);
    a->setCheckable(true);
    a->setData(int(sp.p));
    secGroup->addAction(a);
    const auto pp = sp.p;
    connect(a, &QAction::triggered, this, [this, pp] {
      if (sketching_ && !finishSketch(true)) return;
      section_->setPreset(pp);
      section_->setSectionOn(true);
    });
  }
  mSec->addSeparator();
  auto* aSecFlip = mSec->addAction("Ters çevir");
  aSecFlip->setObjectName("actSectionFlip");
  aSecFlip->setCheckable(true);
  connect(aSecFlip, &QAction::toggled, this, [this](bool on) { section_->setFlipped(on); });
  auto syncSec = [this, secGroup, aSecFlip] {
    for (QAction* a : secGroup->actions()) a->setChecked(a->data().toInt() == int(section_->preset()));
    QSignalBlocker b(aSecFlip);
    aSecFlip->setChecked(section_->flipped());
    statusBar()->showMessage(section_->sectionOn() ? "Kesit görünümü açık (yalnızca görünüm; model değişmez)" : "Kesit görünümü kapalı", 4000);
    syncModeActions();
  };
  connect(section_, &viewer::SectionControls::changed, this, syncSec);
  secGroup->actions().first()->setChecked(true);

  // Display toggles
  auto toggle = [this](const QString& name, const QString& text, const QString& icon, bool on, auto fn) {
    QAction* a = mk(this, name, text, icon, QString(), 16);
    a->setCheckable(true);
    a->setChecked(on);
    connect(a, &QAction::toggled, this, fn);
    return a;
  };
  QAction* aEdges = toggle("actShowEdges", "Kenarlar", "edges", true, [this](bool on) { viewer_->setShowEdges(on); });
  QAction* aPlanes = toggle("actShowPlanes", "Datum düzlemleri", "plane", true, [this](bool on) { showPlanes_ = on; applyDatumPlanes(); });
  QAction* aDims = toggle("actSketchDims3D", "Sketch ölçüleri (3B)", "dim.length", true, [this](bool on) { showSketchDims_ = on; updateViewer(true); });
  aDims->setToolTip("Seçili / son düzenlenen sketch'in ölçü değerlerini 3B görünümde etiket olarak göster");
  QAction* aAxes = toggle("actShowAxes", "Eksenler", "axes", true, [this](bool on) { viewer_->setShowAxes(on); });
  QAction* aColors = toggle("actBodyColors", "Gövde renkleri", "bodies", true, [this](bool on) { showBodyColors_ = on; applyBodyAppearance(); });
  actNsLabels_ = toggle("actNsLabels", "Adlandırılmış seçim etiketleri", "namedsel.labels", false, [this](bool on) {
    showNsLabels_ = on;
    nsApplyDecorations();
  });
  actNsLabels_->setToolTip("Adlandırılmış seçimlerin adlarını 3B görünümde etiket olarak göster");
  for (QAction* a : {aEdges, aPlanes, aDims, aAxes, aColors, actNsLabels_}) menuView_->addAction(a);

  // hidden menu bar keeps a classic copy of the main menus (not shown; harness-visible QMenu objects)
  menuBar()->addMenu(menuFile_);
  menuBar()->setVisible(false);
}

// ============================================================================ tools
void MainWindow::createToolManager() {
  tools_ = new ToolManager(this, this);
  tools_->activate("select");
  toolsReal_ = tools_->active() != nullptr;   // false only for the P0 stub (no IActiveTool objects)
  connect(tools_, &ToolManager::activeToolChanged, this, [this](const QString& id) { onToolChanged(id); });
  connect(tools_, &ToolManager::optionsChanged, this, [this] { QTimer::singleShot(0, this, [this] { refreshOptionsPanel(); }); });
  connect(tools_, &ToolManager::promptChanged, this, [this](const QString& t) { statusBar()->showMessage(t); });
  connect(sketchEd_, &SketchEditor::toolChanged, this, [this](int t) {
    if (toolsReal_ || bridgingTool_) return;
    static const char* ids[] = {"sketch.select", "sketch.point", "sketch.line", "sketch.rect", "sketch.circle", "sketch.arc"};
    if (t < 0 || t >= 6) return;
    bridgingTool_ = true;
    tools_->activate(t == 0 ? QString("select") : QString(ids[t]));
    bridgingTool_ = false;
  });
  // stub bridge for the non-tool sketch actions (constraints / dimensions / construction / delete)
  for (QAction* a : tools_->constraintActions() + tools_->dimensionActions()) {
    const bool dim = a->objectName().startsWith("tool.dimension.");
    const auto t = SkConstraintType(a->objectName().section('.', -1).toInt());
    connect(a, &QAction::triggered, this, [this, dim, t] {
      if (toolsReal_ || !sketching_) return;
      if (dim) sketchEd_->applyDimension(t);
      else sketchEd_->applyConstraint(t);
    });
  }
  if (QAction* c = tools_->action("sketch.construction"))
    connect(c, &QAction::triggered, this, [this](bool on) {
      if (toolsReal_ || !sketching_) return;
      sketchEd_->setConstructionMode(on);
      sketchEd_->toggleConstructionOfSelection();
    });
  if (QAction* d = tools_->action("sketch.delete"))
    connect(d, &QAction::triggered, this, [this] { if (!toolsReal_ && sketching_) sketchEd_->deleteSelection(); });

  // icons / tooltips in SpaceClaim terms (WP-B's actions may already carry their own)
  struct D { const char* id; const char* icon; const char* tip; const char* iconText; };
  const D deco[] = {{"select", "select", "Seç (Select)", ""}, {"pull", "pull", "Çek (Pull): yüz, kenar ya da sketch bölgesini çek; değer yaz", ""},
                    {"move", "move", "Taşı (Move)", ""}, {"fill", "fill", "Doldur (Fill): yuvarlatma / pah / kesiği geri al", ""},
                    {"combine", "combine", "Birleştir (Combine): hedef ve kesici gövde seç", ""},
                    {"plane", "plane", "Düzlem (Plane): seçili yüzden / kenardan datum düzlemi", ""},
                    {"shell", "shell", "Kabuk (Shell)", ""},
                    {"sketch.line", "line", "Çizgi (Line)", ""}, {"sketch.rect", "rect", "Dikdörtgen (Rectangle)", ""},
                    {"sketch.circle", "circle", "Çember (Circle)", ""}, {"sketch.arc", "arc", "Yay (Sweep Arc)", ""},
                    {"sketch.point", "point", "Nokta (Point)", ""}, {"sketch.construction", "construction", "Yapı çizgisi (Construction Line)", ""},
                    {"sketch.trim", "trim", "Kırp (Trim Away)", ""}, {"sketch.delete", "delete", "Sil (Delete): seçili sketch öğeleri", ""}};
  for (const auto& d : deco) decorate(tools_->action(d.id), d.icon, QString::fromUtf8(d.tip), QString::fromUtf8(d.iconText));
  static const QHash<int, QPair<const char*, const char*>> conIcons = {
      {int(SkConstraintType::Coincident), {"con.coincident", "Çakışık (Coincident)"}}, {int(SkConstraintType::Horizontal), {"con.horizontal", "Yatay (Horizontal)"}},
      {int(SkConstraintType::Vertical), {"con.vertical", "Dikey (Vertical)"}}, {int(SkConstraintType::Parallel), {"con.parallel", "Paralel (Parallel)"}},
      {int(SkConstraintType::Perpendicular), {"con.perpendicular", "Dik (Perpendicular)"}}, {int(SkConstraintType::Tangent), {"con.tangent", "Teğet (Tangent)"}},
      {int(SkConstraintType::Equal), {"con.equal", "Eşit (Equal)"}}, {int(SkConstraintType::Fixed), {"con.fixed", "Sabit (Fixed)"}},
      {int(SkConstraintType::Midpoint), {"con.midpoint", "Orta nokta (Midpoint)"}}, {int(SkConstraintType::Symmetric), {"con.symmetric", "Simetrik (Symmetric)"}},
      {int(SkConstraintType::Concentric), {"con.concentric", "Eş merkezli (Concentric)"}},
      {int(SkConstraintType::Distance), {"dim.distance", "Mesafe (Distance): önce seç, sonra tıkla"}},
      {int(SkConstraintType::DistanceX), {"dim.hdistance", "Yatay mesafe (Horizontal Distance)"}},
      {int(SkConstraintType::DistanceY), {"dim.vdistance", "Dikey mesafe (Vertical Distance)"}},
      {int(SkConstraintType::Length), {"dim.length", "Uzunluk (Length)"}}, {int(SkConstraintType::Radius), {"dim.radius", "Yarıçap (Radius)"}},
      {int(SkConstraintType::Diameter), {"dim.diameter", "Çap (Diameter)"}}, {int(SkConstraintType::Angle), {"dim.angle", "Açı (Angle)"}}};
  for (QAction* a : tools_->constraintActions() + tools_->dimensionActions()) {
    const int t = a->objectName().section('.', -1).toInt();
    if (conIcons.contains(t)) decorate(a, conIcons[t].first, QString::fromUtf8(conIcons[t].second));
  }
  tools_->installKeyRouting(viewer_);
  tools_->installKeyRouting(sketchEd_);
  // WP-B overlays (children of the viewer, raised above the sketch overlay by themselves)
  if (toolsReal_) {
    miniToolbar_ = tools_->createMiniToolbar(viewer_);
    guideBar_ = tools_->createToolGuideBar(viewer_);
  }
}

// ============================================================================ ribbon
void MainWindow::buildRibbon() {
  ribbon_ = new Ribbon(this);
  ribbon_->setFileMenu(menuFile_);
  for (QAction* a : {actNew_, actOpen_, actSave_, actUndo_, actRedo_}) ribbon_->quickAccessBar()->addAction(a);
  // SC23 WP-S (S-P1-1): Sketch tab first, like SpaceClaim 2023 R1 [G] (File, Sketch, Design, Display, ...)
  ribbon_->addTab("sketch", "Sketch");
  ribbon_->addTab("design", "Tasarım");
  ribbon_->addTab("display", "Görüntü");
  ribbon_->addTab("measure", "Ölçüm");
  ribbon_->addTab("mfg", "İmalat");
  ribbon_->setTabToolTip("sketch", "Sketch: çizim, kısıt ve ölçü araçları; sketch modunda kendiliğinden seçilir, 3B'de pasiftir");
  ribbon_->setTabToolTip("design", "Tasarım (Design): yönlendirme, modlar, Seç / Çek / Taşı / Doldur, birleştirme, oluşturma");

  // ---- Sketch (S-P1-1). Groups: Yönlendir / Oluştur / Değiştir / Kısıtlar / Ölçüler / Düzenle / Sketch'i Bitir.
  // Collapse order (Ribbon.h): Kısıtlar, Ölçüler, Yönlendir, Değiştir, Düzenle; Oluştur and Sketch'i Bitir never.
  RibbonGroup* g = ribbon_->addGroup("sketch", "sketchOrient", "Yönlendir");
  g->setCollapsePriority(3);
  g->addSmall(actHome_);
  g->addSmall(actPlanView_);
  g->addSmall(actFit_);
  g = ribbon_->addGroup("sketch", "sketchCreate", "Oluştur");
  g->setCollapsePriority(-1);
  g->setContextual(true);
  for (QAction* a : tools_->sketchActions())
    if (a->objectName() != "tool.sketch.trim") g->addSmall(a);
  g = ribbon_->addGroup("sketch", "sketchModify", "Değiştir");
  g->setCollapsePriority(4);
  g->setContextual(true);
  if (QAction* t = tools_->action("sketch.trim")) g->addSmall(t);
  if (QAction* del = tools_->action("sketch.delete")) { del->setIcon(makeIcon("delete", 16)); g->addSmall(del); }
  g = ribbon_->addGroup("sketch", "constraints", "Kısıtlar");
  g->setCollapsePriority(1);
  g->setContextual(true);
  g->setCollapsedIcon(makeIcon("con.coincident", 32));
  for (QAction* a : tools_->constraintActions()) g->addSmall(a, true);
  g = ribbon_->addGroup("sketch", "dimensions", "Ölçüler");
  g->setCollapsePriority(2);
  g->setContextual(true);
  g->setCollapsedIcon(makeIcon("dim.length", 32));
  for (QAction* a : tools_->dimensionActions()) g->addSmall(a, true);
  g = ribbon_->addGroup("sketch", "sketchEdit", "Düzenle");
  g->setCollapsePriority(5);
  g->setMaxLevel(RibbonGroup::Level::Compact);   // Seç / Çek / Taşı / Doldur stay direct buttons (T13)
  for (QAction* a : tools_->editActions()) g->addLarge(a);
  g = ribbon_->addGroup("sketch", "sketchSession", "Sketch'i Bitir");
  g->setCollapsePriority(-1);
  g->setContextual(true);
  g->addLarge(actFinishSketch_);
  g->addLarge(actCancelSketch_);
  actSketchFit_ = nullptr;
  {
    auto* box = new QWidget;
    auto* bl = new QVBoxLayout(box);
    bl->setContentsMargins(4, 0, 2, 0);
    bl->setSpacing(1);
    auto* lbl = new QLabel("Düzlem ofseti:", box);
    lbl->setStyleSheet("color:#24364d;");
    bl->addWidget(lbl);
    sketchOffset_ = new ExprEdit(&work_, box);
    sketchOffset_->setObjectName("sketchOffset");
    sketchOffset_->setParamHint("SketchOfs", ParamKind::Length);
    // SC23: was 265 px; value field + preview only (picker and "Yeni…" on a second row below)
    sketchOffset_->setToolTip("Sketch düzlemini normali boyunca öteler (mm, ifade veya parametre)");
    bl->addWidget(sketchOffset_);
    // SC23: two rows instead of one 400 px row (value + preview / parameter picker + "Yeni…"), so the group stays
    // ≈ 200 px wide; the controls are the ExprEdit's own (signals unchanged), only their place in the box changes.
    if (QComboBox* pick = sketchOffset_->picker()) {
      auto* row2 = new QHBoxLayout;
      row2->setContentsMargins(0, 0, 0, 0);
      row2->setSpacing(2);
      if (sketchOffset_->layout()) sketchOffset_->layout()->removeWidget(pick);
      pick->setParent(box);
      pick->setMinimumContentsLength(9);
      row2->addWidget(pick, 1);
      if (auto* nb = sketchOffset_->findChild<QToolButton*>(QString(), Qt::FindDirectChildrenOnly)) {
        sketchOffset_->layout()->removeWidget(nb);
        nb->setParent(box);
        row2->addWidget(nb);
      }
      bl->addLayout(row2);
    }
    bl->addStretch();
    g->addWidget(box);
    connect(sketchOffset_, &ExprEdit::textChanged, this, [this] {
      if (updatingOffset_ || !sketching_ || !sketchOffset_->isValid()) return;
      sketchEd_->setPlaneOffset(sketchOffset_->text());
      syncSketchFrame();
    });
  }

  // ---- Tasarım (Design). The contextual sketch groups moved to the Sketch tab (S-P1-1).
  // Collapse order: İçe aktar, Gövde / Görünürlük, Oluştur, Kesişim, Mod, Yönlendir; Düzenle never.
  g = ribbon_->addGroup("design", "orient", "Yönlendir");
  g->setCollapsePriority(6);
  g->addSmall(actHome_);
  g->addSmall(actPlanView_);
  g->addSmall(actFit_);
  g->addSmall(actSpin_, true);
  g->addSmall(actPan_, true);
  g->addSmall(actZoomDrag_, true);
  auto* views = mk(this, "actViewsMenu", "Görünümler", "views", "Görünümler (Views): standart görünümler");
  views->setMenu(menuViews_);
  g->addLarge(views);

  g = ribbon_->addGroup("design", "mode", "Mod");
  g->setCollapsePriority(5);
  g->addLarge(actModeSketch_);
  g->addLarge(section_->toggleAction());
  g->addLarge(actMode3D_);

  g = ribbon_->addGroup("design", "edit", "Düzenle");
  g->setCollapsePriority(-1);
  for (QAction* a : tools_->editActions()) g->addLarge(a);

  g = ribbon_->addGroup("design", "intersect", "Kesişim");
  g->setCollapsePriority(4);
  if (QAction* c = tools_->action("combine")) g->addLarge(c);

  g = ribbon_->addGroup("design", "create", "Oluştur");
  g->setCollapsePriority(3);
  if (QAction* p = tools_->action("plane")) g->addLarge(p);
  auto* pat = mk(this, "actPatternMenu", "Desen", "pattern", "Desen (Pattern): seçili özelliği doğrusal / dairesel çoğalt", 16);
  pat->setMenu(menuPattern_);
  g->addSmall(pat);
  if (QAction* s = tools_->action("shell")) { s->setIcon(makeIcon("shell", 16)); g->addSmall(s); }
  auto* hole = mk(this, "actHole", "Delik", "hole", "Delik (Hole) [PC]: sketch noktalarında delik", 16);
  connect(hole, &QAction::triggered, this, [this] { newFeature(FeatureType::Hole); });
  g->addSmall(hole);
  auto* classic = mk(this, "actClassicMenu", "Klasik", "extrude", "Klasik komutlar: özellik diyalogları (Sketch, Extrude, Revolve, Fillet, ...)");
  classic->setMenu(insertMenu_);
  g->addLarge(classic);

  g = ribbon_->addGroup("design", "body", "Gövde / Görünürlük");
  g->setCollapsePriority(2);
  g->addLarge(actClassicCombine_);
  g->addSmall(actHideBody_);
  g->addSmall(actHideOthers_);
  g->addSmall(actShowAll_);
  g->addSmall(actTransparent_);

#ifdef PC_HAVE_DRAWING_IMPORT
  if (auto* aDrw = findChild<QAction*>("actionDrawingImport")) {   // Tasarım > İçe aktar > "Teknik resim"
    g = ribbon_->addGroup("design", "drawingImport", "İçe aktar");
    g->setCollapsePriority(1);
    auto* rb = new QAction(makeIcon("open", 32), "Teknik resim", this);
    rb->setObjectName("actRibbonDrawingImport");
    rb->setToolTip(aDrw->toolTip());
    connect(rb, &QAction::triggered, aDrw, &QAction::trigger);
    g->addLarge(rb);
  }
#endif
  syncSketchRibbon(ribbon_, false);
  // S-P1-1: the Sketch tab is selected while sketching; leaving the sketch brings the Tasarım tab back ([G] G06).
  // Runs at the end of MainWindowOps' setSketchMode (designModeChanged via notifyDesignMode is its last step).
  connect(this, &MainWindow::designModeChanged, this, [this](DesignMode m) {
    syncSketchRibbon(ribbon_, sketching_);
    if (m == DesignMode::Sketch || sketching_) ribbon_->setCurrentTab("sketch");
    else if (ribbon_->currentTabKey() == "sketch") ribbon_->setCurrentTab("design");
  });

  // ---- Görüntü (Display)
  g = ribbon_->addGroup("display", "visibility", "Görünürlük");
  for (const char* n : {"actShowEdges", "actShowPlanes", "actSketchDims3D", "actShowAxes", "actBodyColors"})
    if (QAction* a = findChild<QAction*>(n)) g->addSmall(a);
  g = ribbon_->addGroup("display", "hideShow", "Gizle / Göster");
  for (QAction* a : {actHideBody_, actHideOthers_, actShowAll_, actTransparent_}) g->addSmall(a);
  createColorRibbonGroup();   // SC-COLOR: Görüntü > Stil (Renk, Rengi sıfırla)
  g = ribbon_->addGroup("display", "section", "Kesit");
  // (the "Kesit" toggle itself lives in Tasarım > Mod and in the "Görünüm" menu below; one visible button only,
  //  gui_harness_v4 looks it up through QToolBar::widgetForAction)
  auto* secMenuAct = mk(this, "actSectionMenu", "Kesit düzlemi", "planview", "Kesit düzlemi (XZ / YZ / XY / çeyrek, ters çevir)");
  secMenuAct->setIconText("Kesit\ndüzlemi");
  secMenuAct->setMenu(findChild<QMenu*>("menuSectionPlane"));
  g->addLarge(secMenuAct);
  auto* viewMenuAct = mk(this, "actViewMenu", "Görünüm", "views", "Görünüm: standart görünümler, sığdır, kesit, görüntü seçenekleri");
  viewMenuAct->setMenu(menuView_);
  g->addLarge(viewMenuAct);
  g = ribbon_->addGroup("display", "window", "Pencere");
  g->addLarge(actResetLayout_);
  for (const char* n : {"actToggleStructure", "actToggleOptions", "actToggleIssues"}) g->addSmall(findChild<QAction*>(n));

  // ---- Ölçüm (Measure)
  g = ribbon_->addGroup("measure", "measureTools", "Ölç");
  g->addLarge(actMeasure_);
  g->addLarge(actMass_);

  // ---- İmalat (Manufacturing) [PC]
  g = ribbon_->addGroup("mfg", "mfgCheck", "Denetim");
  g->addLarge(actCheckNow_);
  g->addLarge(actMfgSettings_);
  g->addLarge(findChild<QAction*>("actToggleIssues"));
  g = ribbon_->addGroup("mfg", "app", "Uygulama");
  g->addLarge(actSaveAsApp_);
  g = ribbon_->addGroup("mfg", "export", "Dışa aktar");
  for (QAction* a : menuFile_->findChild<QMenu*>("menuExport")->actions()) {
    a->setIcon(makeIcon("export", 16));
    g->addSmall(a);
  }

  ribbon_->setCurrentTab("design");
  // Full-width strip above the dock areas, like SpaceClaim's ribbon. (Not setMenuWidget(): QMainWindow::menuBar()
  // would replace - and delete - a non-QMenuBar menu widget; the hidden menu bar must stay a real QMenuBar.)
  auto* host = new QToolBar("Ribbon", this);
  host->setObjectName("ribbonHost");
  host->setMovable(false);
  host->setFloatable(false);
  host->setContextMenuPolicy(Qt::PreventContextMenu);
  host->toggleViewAction()->setVisible(false);
  host->layout()->setContentsMargins(0, 0, 0, 0);
  host->layout()->setSpacing(0);
  host->setStyleSheet("QToolBar#ribbonHost { border: none; padding: 0px; spacing: 0px; background: #e9eef5; }");
  host->addWidget(ribbon_);
  addToolBar(Qt::TopToolBarArea, host);
}

// ============================================================================ docks
void MainWindow::buildDocks() {
  setDockNestingEnabled(true);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  // ---- Structure panel (top-left): Yapı / Geçmiş / Seçim / Gruplar
  dockStructure_ = new QDockWidget("Yapı", this);
  dockStructure_->setObjectName("dockStructure");
  dockStructure_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
  structure_ = new StructurePanel(dockStructure_);
  dockStructure_->setWidget(structure_);

  // History = the parametric feature tree (unchanged behaviour: double click = edit, context menu = rollback ...)
  tree_ = new QTreeWidget;
  tree_->setObjectName("featureTree");
  tree_->setHeaderHidden(true);
  tree_->setIconSize(QSize(18, 18));
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::treeContextMenu);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) { if (!sketching_) openFeatureEditor(it->data(0, Qt::UserRole).toString().toStdString()); });
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
    const std::string id = currentTreeFeature();
    if (const Feature* lf = id.empty() ? nullptr : doc_.feature(id); lf && lf->type == FeatureType::Sketch && id != labelSketchId_) {
      labelSketchId_ = id;   // v6: label this sketch's dimensions in 3D
      if (showSketchDims_ && sketchShownIn3D(id)) updateViewer(true);
    }
    {
      const Feature* sf = id.empty() ? nullptr : doc_.feature(id);
      selectedDatum_ = (sf && sf->type == FeatureType::Plane) ? QString::fromStdString(id) : QString();
      applyDatumPlanes();
    }
    if (id.empty()) { if (!featDlg_) viewer_->clearHighlights(); return; }
    QStringList faces;
    for (const auto& fi : model_.info)
      if (fi.featureId == id) for (const auto& n : fi.createdFaces) faces << QString::fromStdString(n);
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces(faces, QColor(90, 160, 235));
    auto it = model_.sketchResults.find(id);
    if (it != model_.sketchResults.end()) lblDof_->setText(QString("Sketch DOF: %1").arg(it->second.dof));
    selFromLegacy_ = true;
    selection_->set({SelItem{SelItem::Kind::Feature, QString::fromStdString(id)}});
    selFromLegacy_ = false;
  });
  structure_->addPage(tree_, "history", "Geçmiş", makeIcon("extrude", 14), "Geçmiş (History) [PersistCAD]: parametrik özellik ağacı, geri sarma, bastırma");

  selPanel_ = new SelectionPanel(selection_);
  structure_->addPage(selPanel_, "selection", "Seçim", makeIcon("select", 14), "Seçim (Selection)");

  // Groups = parameter table (columns unchanged; 'App'te göster' = Uygulamada göster)
  paramsPage_ = new QWidget;
  paramsPage_->setObjectName("groupsPage");
  auto* pl = new QVBoxLayout(paramsPage_);
  pl->setContentsMargins(3, 3, 3, 3);
  pl->setSpacing(3);
  auto* btnRow = new QHBoxLayout;
  auto* bAdd = new QPushButton(makeIcon("group", 16), "Parametre ekle");
  bAdd->setToolTip("Grup oluştur (Create Group): yeni parametre (P1 = 10) ekler; değiştirmek için hücreyi düzenleyin. Ctrl+G: diyalogla.");
  auto* bDel = new QPushButton(makeIcon("delete", 16), "Seçiliyi sil");
  auto* bUse = new QPushButton("Kullanıldığı yerler");
  bUse->setObjectName("paramUsages");
  bUse->setToolTip("Seçili parametreyi kullanan özelliklerin yüzlerini görünümde vurgular");
  for (QPushButton* b : {bAdd, bDel, bUse}) btnRow->addWidget(b);
  btnRow->addStretch();
  connect(bAdd, &QPushButton::clicked, this, &MainWindow::addParameter);
  connect(bDel, &QPushButton::clicked, this, &MainWindow::removeParameter);
  // Gruplar = two sections (SpaceClaim Groups panel folders): Parametreler (driving dimensions) on top,
  // Adlandırılmış Seçimler (named selections) below, in a vertical splitter.
  auto* groupsSplit = new QSplitter(Qt::Vertical, paramsPage_);
  groupsSplit->setObjectName("groupsSplitter");
  groupsSplit->setChildrenCollapsible(false);
  auto* paramBox = new QWidget;
  paramBox->setObjectName("groupsParamSection");
  auto* pbl = new QVBoxLayout(paramBox);
  pbl->setContentsMargins(0, 0, 0, 0);
  pbl->setSpacing(3);
  {
    auto* hdr = new QHBoxLayout;
    auto* ic = new QLabel;
    ic->setPixmap(makeIcon("group", 16).pixmap(16, 16));
    auto* t = new QLabel("<b>Parametreler</b>");
    t->setObjectName("paramTitle");
    t->setToolTip("Parametreler (sürücü ölçüler / Driving Dimensions)");
    hdr->addWidget(ic);
    hdr->addWidget(t);
    hdr->addStretch();
    pbl->addLayout(hdr);
  }
  pbl->addLayout(btnRow);
  groupsSplit->addWidget(paramBox);
  pl->addWidget(groupsSplit, 1);
  paramView_ = new QTableView(paramsPage_);
  paramView_->setObjectName("parameterTable");
  paramModel_ = new QStandardItemModel(this);
  paramView_->setModel(paramModel_);
  paramView_->setItemDelegateForColumn(3, new KindDelegateShell(paramView_));
  paramView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  paramView_->setAlternatingRowColors(true);
  paramView_->verticalHeader()->hide();
  paramView_->verticalHeader()->setDefaultSectionSize(22);
  pbl->addWidget(paramView_, 1);
  createNamedSelectionPanel(groupsSplit);
  connect(paramModel_, &QStandardItemModel::itemChanged, this, &MainWindow::onParamItemChanged);
  connect(paramView_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& cur) {
    if (updatingParams_ || !cur.isValid() || !paramModel_->item(cur.row(), 0)) return;
    const QString n = paramModel_->item(cur.row(), 0)->data(Qt::UserRole).toString();
    selFromLegacy_ = true;
    selection_->set({SelItem{SelItem::Kind::Parameter, n}});
    selFromLegacy_ = false;
  });
  connect(bUse, &QPushButton::clicked, this, [this] {
    const auto idx = paramView_->currentIndex();
    if (!idx.isValid()) { statusBar()->showMessage("Önce bir parametre seçin.", 4000); return; }
    const QString n = paramModel_->item(idx.row(), 0)->data(Qt::UserRole).toString();
    QStringList faces, users;
    for (const auto& f : doc_.features) {
      const QString summary = featureParameterSummary(QString::fromStdString(f.id));
      bool uses = false;
      for (const QString& l : summary.split('\n', Qt::SkipEmptyParts)) if (l.section(' ', 0, 0) == n) uses = true;
      if (!uses) continue;
      users << QString::fromStdString(f.id);
      for (const auto& fi : model_.info)
        if (fi.featureId == f.id) for (const auto& fn : fi.createdFaces) faces << QString::fromStdString(fn);
    }
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces(faces, QColor(150, 90, 230));
    statusBar()->showMessage(users.isEmpty() ? QString("%1 hiçbir özellikte kullanılmıyor.").arg(n)
                                             : QString("%1 kullanan özellikler: %2").arg(n, users.join(", ")), 8000);
  });
  structure_->addPage(paramsPage_, "groups", "Gruplar", makeIcon("group", 14), "Gruplar (Groups): Parametreler ('Uygulamada göster' = Save-as-App girdileri) ve Adlandırılmış Seçimler (Ctrl+G)");

  // Hidden compatibility widget: the v2 "Gövdeler" list (automation API bodiesTree(); the visible body list is
  // the Yapı tree's "Gövdeler" node). Both drive the same functions.
  bodiesTree_ = new QTreeWidget(structure_);
  bodiesTree_->setObjectName("bodiesTree");
  bodiesTree_->setColumnCount(3);
  bodiesTree_->setHeaderLabels({"Gövde", "Hacim (mm³)", "Oluşturan"});
  bodiesTree_->setRootIsDecorated(false);
  bodiesTree_->setContextMenuPolicy(Qt::CustomContextMenu);
  bodiesTree_->hide();
  connect(bodiesTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::bodiesContextMenu);
  connect(bodiesTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
    if (updatingBodies_ || col != 0) return;
    setBodyVisible(it->data(0, Qt::UserRole).toString(), it->checkState(0) == Qt::Checked);
  });
  connect(bodiesTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
    if (updatingBodies_) return;
    const auto sel = bodiesTree_->selectedItems();
    if (!sel.isEmpty()) selectBody(sel.first()->data(0, Qt::UserRole).toString());
  });

  connect(structure_, &StructurePanel::itemPicked, this, [this](const SelItem& s, bool add) {
    if (add) { selection_->toggle(s); return; }
    if (s.kind == SelItem::Kind::Body) { selectBody(s.name); return; }
    if (s.kind == SelItem::Kind::Datum) { selectedDatum_ = s.name; applyDatumPlanes(); }
    if (s.kind == SelItem::Kind::Sketch && s.name.toStdString() != labelSketchId_) {
      labelSketchId_ = s.name.toStdString();
      if (showSketchDims_ && sketchShownIn3D(labelSketchId_)) updateViewer(true);
    }
    selection_->set({s});
  });
  connect(structure_, &StructurePanel::itemActivated, this, [this](const SelItem& s) {
    if (s.kind == SelItem::Kind::Sketch) editSketch(s.name.toStdString());
    else if (s.kind == SelItem::Kind::Datum) openFeatureEditor(s.name.toStdString());
  });
  connect(structure_, &StructurePanel::visibilityToggled, this, [this](const SelItem& s, bool on) {
    if (s.kind == SelItem::Kind::Body) QTimer::singleShot(0, this, [this, s, on] { setBodyVisible(s.name, on); });
    else if (s.kind == SelItem::Kind::Face) QTimer::singleShot(0, this, [this, s, on] { setFaceVisible(s.name, on); });   // "Gizli yüzler" rows
    else if (s.kind == SelItem::Kind::Sketch) QTimer::singleShot(0, this, [this, s, on] { setSketchShownIn3D(s.name.toStdString(), on); });
  });
  connect(structure_, &StructurePanel::renameRequested, this, [this](const SelItem& s, const QString& to) {
    QTimer::singleShot(0, this, [this, s, to] { if (!renameBody(s.name, to)) rebuildStructure(); });
  });
  connect(structure_, &StructurePanel::contextMenuRequested, this, &MainWindow::structureContextMenu);
  addDockWidget(Qt::LeftDockWidgetArea, dockStructure_);

  // ---- Options / Properties (bottom-left, tabs at the bottom)
  dockOptions_ = new QDockWidget("Seçenekler", this);
  dockOptions_->setObjectName("dockOptions");
  dockOptions_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable);
  optionsTabs_ = new QTabWidget(dockOptions_);
  optionsTabs_->setObjectName("optionsTabs");
  optionsTabs_->setTabPosition(QTabWidget::South);
  optionsTabs_->setDocumentMode(true);
  options_ = new OptionsPanel;
  options_->setExpressionContext([this] { return expressionContext(); });
  connect(options_, &OptionsPanel::optionChanged, this, [this](const QString& key, const QVariant& v) {
    if (toolsReal_) { tools_->setOption(key, v); return; }
    if (key == "filter") {   // stub: the shell's own selection-filter option
      static const QStringList keys = {"smart", "face", "edge", "body", "datum", "sketch"};
      const int i = keys.indexOf(v.toString());
      if (i >= 0) selection_->setFilter(SelFilter(i));
    }
  });
  auto syncDockTitle = [this] {
    dockOptions_->setWindowTitle(optionsTabs_->currentIndex() == 1 ? QString("Özellikler") : options_->title());
  };
  connect(options_, &OptionsPanel::titleChanged, this, syncDockTitle);
  props_ = new PropertiesPanel(this);
  PropertiesPanel::Hooks h;
  h.renameBody = [this](const QString& a, const QString& b) { return renameBody(a, b); };
  h.setBodyVisible = [this](const QString& b, bool on) { setBodyVisible(b, on); };
  h.bodyVisible = [this](const QString& b) { return bodyVisible(b); };
  h.showInHistory = [this](const std::string& id) { selectFeatureInHistory(id); };
  h.parameterSummary = [this](const QString& id) { return featureParameterSummary(id); };
  h.sketchShown = [this](const std::string& id) { return sketchShownIn3D(id); };
  h.setSketchShown = [this](const std::string& id, bool on) { QTimer::singleShot(0, this, [this, id, on] { setSketchShownIn3D(id, on); }); };
  h.openMfgSettings = [this] { openMfgSettings(); };
  h.filePath = [this] { return path_; };
  h.measureRows = [this] { return paramSel_ ? paramSel_->propertyRows() : QList<PropertiesPanel::Hooks::MeasureRow>(); };
  h.namedSelectionsOf = [this](const QList<SelItem>& items) { return namedSelectionsOf(items); };
  h.nameItems = [this](const QList<SelItem>& items, const QString& n) { return nameItems(items, n); };
  h.colorOf = [this](const SelItem& it, bool* inh) { return itemColor(it, inh); };   // SC-COLOR
  h.colorMenu = [this](QWidget* parent, const QList<SelItem>& items) { return makeColorMenu(parent, [items] { return items; }); };
  props_->setHooks(h);
  optionsTabs_->addTab(options_, makeIcon("options", 14), "Seçenekler");
  optionsTabs_->addTab(props_, makeIcon("props", 14), "Özellikler");
  optionsTabs_->setTabToolTip(0, "Seçenekler (Options): etkin aracın seçenekleri");
  optionsTabs_->setTabToolTip(1, "Özellikler (Properties): seçili öğenin özellikleri");
  connect(optionsTabs_, &QTabWidget::currentChanged, this, syncDockTitle);
  dockOptions_->setWidget(optionsTabs_);
  addDockWidget(Qt::LeftDockWidgetArea, dockOptions_);
  splitDockWidget(dockStructure_, dockOptions_, Qt::Vertical);

  // ---- Manufacturing check (bottom)
  dockIssues_ = new QDockWidget("İmalat Denetimi", this);
  dockIssues_->setObjectName("dockIssues");
  issueTable_ = new QTableWidget(0, 8, dockIssues_);
  issueTable_->setObjectName("issueTable");
  issueTable_->setHorizontalHeaderLabels({"", "Alan", "Kural", "Mesaj", "Parametre", "Önerilen", "Gövde", ""});
  issueTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  issueTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  issueTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  issueTable_->verticalHeader()->hide();
  issueTable_->verticalHeader()->setDefaultSectionSize(22);
  issueTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  connect(issueTable_, &QTableWidget::itemSelectionChanged, this, [this] {
    const auto rows = issueTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) { viewer_->clearHighlights(); return; }
    const int r = rows.first().row();
    if (r < 0 || r >= int(report_.issues.size())) return;
    const auto& is = report_.issues[size_t(r)];
    QStringList f, e;
    for (const auto& n : is.faces) f << QString::fromStdString(n);
    for (const auto& n : is.edges) e << QString::fromStdString(n);
    const QColor col = is.severity == mfg::Severity::Error ? QColor(230, 60, 40) : QColor(240, 170, 30);
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces(f, col);
    viewer_->setHighlightedEdges(e, QColor(255, 90, 0));
  });
  dockIssues_->setWidget(issueTable_);
  addDockWidget(Qt::BottomDockWidgetArea, dockIssues_);

  // SC23 WP-S (S-P1-9): the Structure panel's page tabs (Yapı ... Görünümler) must not dictate the column width:
  // compact tabs, scroll arrows when the column is narrow (SpaceClaim's five tabs fit ≈ 240 px [G]).
  if (auto* st = structure_->findChild<QTabWidget*>("structureTabs")) {
    st->setUsesScrollButtons(true);
    st->tabBar()->setElideMode(Qt::ElideNone);
    st->setIconSize(QSize(12, 12));
    st->tabBar()->setStyleSheet("QTabBar::tab { padding: 3px 5px 3px 5px; }");
  }
  // Options without a scroll bar (S-P1-9): when the active tool's options need more height than the Options panel
  // has, the panel grows (the Structure panel keeps 100..170 px). Only grows; the user's split is kept otherwise.
  // The Structure page's own minimum (tab bar + tree, ≈150-180 px depending on fonts) must not block the fit:
  // an explicit minimum lets the dock shrink to the floor below (the tree scrolls).
  if (structure_) structure_->setMinimumHeight(80);
  auto fitOptions = [this] {
    QTimer::singleShot(0, this, [this] {
      auto* sa = options_ ? options_->findChild<QScrollArea*>() : nullptr;
      if (!sa || !sa->widget() || !dockOptions_->isVisible() || dockOptions_->isFloating() || dockCollapsed(dockOptions_) ||
          dockCollapsed(dockStructure_) || optionsTabs_->currentIndex() != 0)
        return;
      const int need = sa->widget()->sizeHint().height() - sa->viewport()->height();
      if (need <= 0) return;
      const int total = dockStructure_->height() + dockOptions_->height();
      // Structure floor: 170 px on tall windows, down to 100 px on short ones (1024x768 with Windows fonts needs it)
      const int floorH = std::clamp(int(total * 0.15), 100, 170);
      const int opt = std::min(dockOptions_->height() + need + 4, total - floorH);
      if (opt > dockOptions_->height()) resizeDocks({dockStructure_, dockOptions_}, {total - opt, opt}, Qt::Vertical);
      if (qEnvironmentVariableIsSet("PC_SC23_DEBUG_FIT"))
        qWarning("fitOptions: need=%d total=%d floor=%d opt=%d -> struct=%d (min %d/%d) options=%d (min %d/%d) vp=%d",
                 need, total, floorH, opt, dockStructure_->height(), dockStructure_->minimumHeight(),
                 dockStructure_->minimumSizeHint().height(), dockOptions_->height(), dockOptions_->minimumHeight(),
                 dockOptions_->minimumSizeHint().height(), sa->viewport()->height());
    });
  };
  connect(options_, &OptionsPanel::titleChanged, this, fitOptions);
  connect(tools_, &ToolManager::optionsChanged, this, fitOptions);
  connect(tools_, &ToolManager::activeToolChanged, this, fitOptions);
  connect(optionsTabs_, &QTabWidget::currentChanged, this, fitOptions);

  // SC23 WP-S (S-P1-9): collapsible panel headers, minimum width 240 px (left column ≈ 20 % at 1240 px [G])
  for (QDockWidget* d : {dockStructure_, dockOptions_}) {
    installDockTitleBar(d);
    d->setMinimumWidth(240);
  }
  installEventFilter(this);   // window resize -> keep the left column proportional (eventFilter)

  // panel toggles (Görüntü > Pencere, İmalat > Denetim; created in buildActions). Own actions: a dock's
  // toggleViewAction() text follows the dock title ("Seçenekler - Çek (Pull)"), not a stable button label.
  for (auto [d, name] : {std::pair{dockStructure_, "actToggleStructure"}, std::pair{dockOptions_, "actToggleOptions"},
                         std::pair{dockIssues_, "actToggleIssues"}}) {
    QAction* a = findChild<QAction*>(name);
    if (!a) continue;
    QDockWidget* dk = d;
    connect(a, &QAction::toggled, dk, [dk](bool on) { dk->setVisible(on); });
    connect(dk, &QDockWidget::visibilityChanged, a, [a, dk](bool) { const QSignalBlocker bl(a); a->setChecked(!dk->isHidden()); });
  }
}

void MainWindow::buildStatusBar() {
  // SC23 WP-S (S-P1-3): dark status bar like SpaceClaim 2023 R1 [G]: prompt on the left (QStatusBar message +
  // severity icon), selection / measure info in the middle, filter, revert selection, PersistCAD counters and the
  // ←/→ view buttons on the right. Errors carry an icon + text (never colour alone).
  setStatusBar(new PromptStatusBar(this));   // before any widget is added (S-P1-3, see PromptStatusBar)
  statusBar()->setObjectName("mainStatusBar");
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setStyleSheet(
      "QStatusBar#mainStatusBar { background: #24496f; color: #f3f7fb; border-top: 1px solid #16324f; }"
      "QStatusBar#mainStatusBar::item { border: none; }"
      "QStatusBar#mainStatusBar QLabel { color: #f3f7fb; border: none; border-left: 1px solid #3f6690; padding: 0px 7px; }"
      "QStatusBar#mainStatusBar QLabel#statusSeverity { border: none; padding: 0px 2px 0px 4px; }"
      "QStatusBar#mainStatusBar QLabel#hoverInfo { border: none; }"
      "QStatusBar#mainStatusBar QComboBox { background: #2f5b88; color: #f3f7fb; border: 1px solid #5c82ab; border-radius: 2px;"
      "  padding: 0px 18px 0px 6px; min-height: 18px; }"
      "QStatusBar#mainStatusBar QComboBox QAbstractItemView { background: white; color: #1b2b40; }"
      "QStatusBar#mainStatusBar QToolButton { background: #dbe6f2; border: 1px solid #9fb6d0; border-radius: 2px; margin: 1px 2px; }"
      "QStatusBar#mainStatusBar QToolButton:hover { background: #f5f9fd; }"
      "QStatusBar#mainStatusBar QToolButton:disabled { background: #46698f; border-color: #46698f; }");
  auto* sev = new QLabel;
  sev->setObjectName("statusSeverity");
  sev->setToolTip("Son işlem hatası (ayrıntı görünümün sol üstünde)");
  sev->setPixmap(makeIcon("hud.error", 16).pixmap(16, 16));
  sev->hide();
  // not in the status-bar layout (QStatusBar hides normal widgets under a temporary message): a free child at the
  // left edge; PromptStatusBar paints the message right of it
  sev->setParent(statusBar());
  sev->setFixedSize(20, 16);
  lblHover_ = new QLabel("Yüz: -");
  lblHover_->setObjectName("hoverInfo");
  lblHover_->setMinimumWidth(160);
  statusBar()->addWidget(lblHover_, 1);
  sbw_ = new StatusBarWidgets(statusBar(), selection_, this);
  lblIssues_ = new QLabel("Hata: 0   Uyarı: 0");
  lblRegen_ = new QLabel("Yenileme: -");
  lblVol_ = new QLabel("Hacim: -");
  lblDof_ = new QLabel("Sketch DOF: -");
  for (QLabel* l : {lblIssues_, lblRegen_, lblVol_, lblDof_}) {
    l->setFrameStyle(QFrame::NoFrame);
    sbw_->addPermanent(l);
  }
  lblIssues_->setObjectName("issueCounts");
  lblIssues_->setToolTip("İmalat denetimi: hata / uyarı sayısı");
  lblRegen_->setObjectName("regenTime");
  lblVol_->setObjectName("volumeInfo");
  lblDof_->setObjectName("sketchDof");
  sbw_->setViewActions(actPrevView_, actNextView_);
}

// ============================================================================ selection
void MainWindow::wireSelection() {
  connect(selection_, &SelectionModel::changed, this, &MainWindow::onSelectionChanged);
  connect(selection_, &SelectionModel::filterChanged, this, [this](SelFilter f) {
    statusBar()->showMessage("Seçim filtresi: " + selFilterText(f), 3000);
    if (!toolsReal_) refreshOptionsPanel();
  });
}

QStringList MainWindow::facesOfSelection() const {
  QStringList faces;
  for (const SelItem& s : selection_->items()) {
    switch (s.kind) {
      case SelItem::Kind::Face:
      case SelItem::Kind::SketchRegion: faces << s.name; break;
      case SelItem::Kind::Body:
        if (const auto* b = model_.body(s.name.toStdString())) for (const auto& f : b->faces) faces << QString::fromStdString(f);
        break;
      case SelItem::Kind::Feature:
        for (const auto& fi : model_.info)
          if (fi.featureId == s.name.toStdString()) for (const auto& n : fi.createdFaces) faces << QString::fromStdString(n);
        break;
      default: break;
    }
  }
  return faces;
}

void MainWindow::highlightSelection() {
  if (featDlg_) return;   // the open feature dialog owns the highlight of its refs
  viewer_->clearHighlights();
  QStringList faces = facesOfSelection();
  QStringList edges = selection_->namesOfKind(SelItem::Kind::Edge);
  edges << selection_->namesOfKind(SelItem::Kind::SketchCurve);   // SC-2D-IMPORT: selected 3D sketch curves
  // a named selection clicked in the Gruplar panel highlights in its own colour (until the selection changes)
  QColor faceCol(70, 140, 235), edgeCol(255, 140, 0);
  if (!nsActive_.isEmpty() && selection_->items() == nsActiveItems_)
    if (const NamedSelection* ns = nsFind(nsActive_)) faceCol = edgeCol = QColor(QString::fromStdString(ns->color));
  lastHlFaces_.clear();
  if (!faces.isEmpty()) { viewer_->setHighlightedFaces(faces, faceCol); lastHlFaces_ = faces; lastHlColor_ = faceCol; }
  else if (!toolEmphFaces_.isEmpty()) viewer_->setHighlightedFaces(toolEmphFaces_, QColor(120, 205, 235));
  if (!toolEmphEdges_.isEmpty()) edges << toolEmphEdges_;
  if (!edges.isEmpty()) viewer_->setHighlightedEdges(edges, edgeCol);
}

void MainWindow::onSelectionChanged() {
  if (!nsActive_.isEmpty() && selection_->items() != nsActiveItems_) {   // the user selected something else
    nsActive_.clear();
    nsActiveItems_.clear();
    if (nsPanel_) nsPanel_->setCurrentId(QString());   // the panel row no longer describes the selection
  }
  if (!selFromLegacy_) highlightSelection();
  // datum selection drives the highlighted plane drawing
  const QStringList datums = selection_->namesOfKind(SelItem::Kind::Datum);
  if (!datums.isEmpty() && datums.last() != selectedDatum_) { selectedDatum_ = datums.last(); applyDatumPlanes(); }
  const QStringList edges = selection_->namesOfKind(SelItem::Kind::Edge);
  if (!edges.isEmpty()) selectedEdge_ = edges.last();
  structure_->syncSelection(selection_->items());
  schedulePanelsRefresh();
}

void MainWindow::schedulePanelsRefresh() {
  if (panelsQueued_) return;
  panelsQueued_ = true;
  QTimer::singleShot(0, this, [this] { panelsQueued_ = false; refreshPanels(); });
}

void MainWindow::refreshPanels() {
  if (props_) props_->rebuild();
}

QString MainWindow::selectedEdge() const {
  const QStringList e = selection_ ? selection_->namesOfKind(SelItem::Kind::Edge) : QStringList();
  return e.isEmpty() ? selectedEdge_ : e.last();
}

// ============================================================================ SC23 viewport HUD (P0-SC23 wiring)
// Short tool + selection text over the viewer (ANALYSIS §5.3 #2) and the operation feedback hook (§5.3 #1).
void MainWindow::buildHud() {
  hud_ = new ViewportHud(viewer_);
  connect(tools_, &ToolManager::activeToolChanged, this, [this] { updateHudToolText(); });
  connect(tools_, &ToolManager::operationChanged, this, [this](const OpStatus& st) { showOperationFeedback(st); });
  connect(selection_, &SelectionModel::changed, this, [this] { updateHudSelectionText(); });
  // S-P1-3: an error in the HUD also puts the error icon in front of the status-bar prompt (icon + text)
  connect(hud_, &ViewportHud::layoutChanged, this, [this] {
    if (auto* sev = statusBar()->findChild<QLabel*>("statusSeverity")) {
      const bool on = !hud_->messageText().isEmpty() && hud_->messageSeverity() == ViewportHud::Severity::Error;
      statusBar()->setProperty("errorIcon", on);
      sev->move(6, std::max(0, (statusBar()->height() - sev->height()) / 2));
      sev->setVisible(on);
      if (on) sev->raise();
      statusBar()->update();
    }
  });
  // the HUD, its message strip and the tool guides stay above the sketch overlay (raised when sketching starts)
  connect(this, &MainWindow::designModeChanged, this, [this] {
    QTimer::singleShot(0, this, [this] {
      if (!hud_) return;
      hud_->raise();
      if (hud_->messageLabel()) hud_->messageLabel()->raise();
      if (guideBar_) guideBar_->raise();
      if (miniToolbar_) miniToolbar_->raise();
      updateHudToolText();
    });
  });
  qApp->installEventFilter(new RichToolTipFilter(this));   // S-P1-6
  updateHudToolText();
  updateHudSelectionText();
}

void MainWindow::updateHudToolText() {
  if (!hud_ || !tools_) return;
  static const QHash<QString, QString> fallback = {
      {"select", "Seç"}, {"sketch.select", "Seç"}, {"pull", "Çek"}, {"move", "Taşı"}, {"fill", "Doldur"},
      {"combine", "Birleştir"}, {"plane", "Düzlem"}, {"shell", "Kabuk"}, {"sketch.line", "Çizgi"},
      {"sketch.rect", "Dikdörtgen"}, {"sketch.circle", "Çember"}, {"sketch.arc", "Yay"}, {"sketch.point", "Nokta"},
      {"sketch.trim", "Kırp"}};
  QString t = tools_->active() ? tools_->active()->title() : fallback.value(tools_->activeId(), tools_->activeId());
  hud_->setToolText(t.section(" (", 0, 0).trimmed());   // "Çek (Pull)" -> "Çek"
}

void MainWindow::updateHudSelectionText() {
  if (!hud_ || !selection_) return;
  hud_->setSelectionText(selectionSummary(selection_->items()).section(':', 0, 0));   // "1 yüz: X" -> "1 yüz"
}

void MainWindow::selectFeatureInHistory(const std::string& id) {
  structure_->setCurrentPage("history");
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    QTreeWidgetItem* it = tree_->topLevelItem(i);
    if (it->data(0, Qt::UserRole).toString().toStdString() == id) {
      tree_->clearSelection();
      it->setSelected(true);
      tree_->scrollToItem(it);
      tree_->setFocus();
      return;
    }
  }
}

void MainWindow::setHistoryEnabled(bool on) {
  tree_->setEnabled(on);
  structure_->tree()->setEnabled(on);
}

// ============================================================================ commands
void MainWindow::createGroupInteractive() {
  if (sketching_) { statusBar()->showMessage("Grup oluştur: sketch'te bir ölçü alanındayken Ctrl+G kullanın.", 4000); return; }
  structure_->setCurrentPage("groups");
  std::string name;
  for (int i = 1;; ++i) { name = "P" + std::to_string(i); if (!doc_.params.find(name)) break; }
  createParameterInteractive(QString::fromStdString(name), ParamKind::Length, "10", this);
}

void MainWindow::deleteSelectionCommand() {
  if (sketching_) {
    if (toolsReal_) tools_->deleteSelection();
    else sketchEd_->deleteSelection();
    return;
  }
  if (nsPanel_ && nsPanel_->tree()->hasFocus() && !nsPanel_->currentId().isEmpty()) {   // Del in Adlandırılmış Seçimler
    removeNamedSelection(nsPanel_->currentId());
    return;
  }
  if (tools_ && tools_->deleteSelection()) return;
  std::vector<std::string> ids;
  for (const SelItem& s : selection_->items())
    if (s.kind == SelItem::Kind::Feature || s.kind == SelItem::Kind::Datum || s.kind == SelItem::Kind::Sketch)
      if (doc_.feature(s.name.toStdString()) && std::find(ids.begin(), ids.end(), s.name.toStdString()) == ids.end()) ids.push_back(s.name.toStdString());
  if (ids.empty()) { statusBar()->showMessage("Sil: Geçmiş ya da Yapı ağacında bir özellik seçin.", 4000); return; }
  QStringList names;
  for (const auto& i : ids) names << QString::fromStdString(i);
  if (!autoYes_ && QMessageBox::question(this, "Sil", QString("Seçili özellik(ler) silinsin mi?\n%1").arg(names.join(", "))) != QMessageBox::Yes) return;
  selection_->clear();
  for (const auto& i : ids) deleteFeature(i);
}

void MainWindow::showMassProperties() {
  QStringList bodies;
  for (const SelItem& s : selection_->items()) {
    if (s.kind == SelItem::Kind::Body) bodies << s.name;
    else if (s.kind == SelItem::Kind::Face) bodies << QString::fromStdString(model_.bodyOfFace(s.name.toStdString()));
  }
  bodies.removeAll(QString());
  bodies.removeDuplicates();
  if (bodies.isEmpty()) for (const auto& b : model_.bodies) bodies << QString::fromStdString(b.name);
  if (bodies.isEmpty()) { statusBar()->showMessage("Kütle özellikleri: modelde gövde yok.", 4000); return; }
  QString html = "<table cellspacing=6><tr><th align=left>Gövde</th><th>Hacim (mm³)</th><th>Alan (mm²)</th><th>Ağırlık merkezi</th><th>Sınır kutusu (mm)</th></tr>";
  double total = 0;
  for (const QString& n : bodies) {
    const auto* b = model_.body(n.toStdString());
    if (!b) continue;
    try {
      const auto mp = model::massProperties(b->shape);
      total += mp.volume;
      html += QString("<tr><td>%1</td><td align=right>%2</td><td align=right>%3</td><td>(%4, %5, %6)</td><td>%7 × %8 × %9</td></tr>")
                  .arg(n).arg(mp.volume, 0, 'f', 1).arg(mp.area, 0, 'f', 1).arg(mp.cx, 0, 'f', 2).arg(mp.cy, 0, 'f', 2).arg(mp.cz, 0, 'f', 2)
                  .arg(mp.xmax - mp.xmin, 0, 'f', 2).arg(mp.ymax - mp.ymin, 0, 'f', 2).arg(mp.zmax - mp.zmin, 0, 'f', 2);
    } catch (...) {}
  }
  html += QString("</table><p><b>Toplam hacim: %1 mm³</b></p>").arg(total, 0, 'f', 1);
  auto* mb = new QMessageBox(QMessageBox::Information, "Kütle özellikleri (Mass)", html, QMessageBox::Close, this);
  mb->setObjectName("massPropertiesDialog");
  mb->setAttribute(Qt::WA_DeleteOnClose);
  mb->setModal(false);
  mb->show();
  showQuickMeasure(QString("Hacim %1 mm³").arg(total, 0, 'f', 1));
}

// ============================================================================ context menus (SPEC §3.10)
void MainWindow::structureContextMenu(const SelItem& s, const QPoint& g) {
  if (sketching_) return;
  QMenu m(this);
  m.setObjectName("structureContextMenu");
  const std::string id = s.name.toStdString();
  if (s.kind == SelItem::Kind::Face) {   // a row under "Gizli yüzler (n)"
    const QString f = s.name;
    m.addAction(makeIcon("show", 16), "Göster", this, [this, f] { setFaceVisible(f, true); })->setObjectName("structShowFace");
    m.addAction(makeIcon("showall", 16), "Tümünü göster", this, [this] { showAllItems(); });
    m.exec(g);
    return;
  }
  if (s.kind == SelItem::Kind::Body) {
    const QString n = s.name;
    m.addAction("Yeniden adlandır...", this, [this, n] {
      for (int i = 0; i < structure_->bodiesNode()->childCount(); ++i)
        if (structure_->bodiesNode()->child(i)->data(0, Qt::UserRole).toString() == n) structure_->tree()->editItem(structure_->bodiesNode()->child(i), 0);
    });
    const bool vis = bodyVisible(n);
    m.addAction(makeIcon(vis ? "hide" : "show", 16), vis ? "Gizle" : "Göster", this, [this, n, vis] { setBodyVisible(n, !vis); })
        ->setObjectName(vis ? "structHide" : "structShow");
    m.addAction(makeIcon("hideothers", 16), "Yalnız bunu göster", this, [this, n] { isolateItems({SelItem{SelItem::Kind::Body, n}}); })
        ->setObjectName("structIsolate");
    const QStringList hf = hiddenFacesOfBody(n);
    if (!hf.isEmpty()) {
      QList<SelItem> its;
      for (const QString& f : hf) its << SelItem{SelItem::Kind::Face, f};
      m.addAction(makeIcon("show", 16), QString("Gizli yüzlerini göster (%1)").arg(hf.size()), this, [this, its] { showItems(its); })
          ->setObjectName("structShowFaces");
    }
    const bool tr = transparentBodies_.count(id) > 0;
    m.addAction(makeIcon("transparent", 16), tr ? "Saydamlığı kaldır" : "Saydam yap", this,
                [this, n, tr] { setItemsTransparent({SelItem{SelItem::Kind::Body, n}}, !tr); })->setObjectName("structTransparent");
    m.addAction(makeIcon("showall", 16), "Tümünü göster", this, [this] { showAllItems(); });
    if (const auto* b = model_.body(id)) {
      const std::string c = b->createdBy;
      m.addSeparator();
      m.addAction("Geçmişte göster", this, [this, c] { selectFeatureInHistory(c); });
    }
  } else {
    if (s.kind == SelItem::Kind::Datum) {
      m.addAction(makeIcon("sketch", 16), "Bu düzlemde sketch başlat", this, [this, id] { beginSketchOnPlane(id); });
      m.addAction("Düzlemi düzenle...", this, [this, id] { openFeatureEditor(id); });
    } else if (s.kind == SelItem::Kind::Sketch) {
      m.addAction(makeIcon("sketch", 16), "Sketch'i düzenle (K)", this, [this, id] { editSketch(id); });
      QAction* a = m.addAction("3B görünümde göster");
      a->setCheckable(true);
      a->setChecked(sketchShownIn3D(id));
      connect(a, &QAction::toggled, this, [this, id](bool on) { setSketchShownIn3D(id, on); });
    }
    m.addSeparator();
    m.addAction("Geçmişte göster", this, [this, id] { selectFeatureInHistory(id); });
    m.addAction(makeIcon("delete", 16), "Sil", this, [this, id] { deleteFeature(id); });
  }
  m.exec(g);
}

// ============================================================================ layout
void MainWindow::resetLayout() {
  if (!defaultLayout_.isEmpty()) restoreState(defaultLayout_, 2);
  setDockCollapsed(dockStructure_, false);
  setDockCollapsed(dockOptions_, false);
  dockStructure_->show();
  dockOptions_->show();
  dockIssues_->show();
  const int lw = std::clamp(int(width() * 0.22), 240, 330);   // SC23 WP-S: ≈ 20 % of the window, 240..330 px
  resizeDocks({dockStructure_, dockOptions_}, {lw, lw}, Qt::Horizontal);
  resizeDocks({dockStructure_, dockOptions_}, {500, 330}, Qt::Vertical);
  resizeDocks({dockIssues_}, {140}, Qt::Vertical);
  statusBar()->showMessage("Panel düzeni varsayılana döndü.", 3000);
}

void MainWindow::restoreLayout() {
  defaultLayout_ = saveState(2);
  if (!layoutPersistence()) return;
  QSettings s("PersistCAD", "Designer");
  const QByteArray st = s.value(kLayoutKey).toByteArray();
  if (!st.isEmpty()) restoreState(st, 2);
}

void MainWindow::saveLayout() {
  if (!layoutPersistence()) return;
  QSettings s("PersistCAD", "Designer");
  s.setValue(kLayoutKey, saveState(2));
}

// ============================================================================ event filter (viewer)
bool MainWindow::eventFilter(QObject* o, QEvent* e) {
  if (o == this && e->type() == QEvent::Resize && dockStructure_) {
    // SC23 WP-S (S-P1-9): the left panel column never takes more than ~26 % of a narrow window (min 240 px):
    // at 1024 px the graphics area keeps ≥ 750 px. Wider columns set by the user are kept on wide windows.
    QTimer::singleShot(0, this, [this] {
      if (!dockStructure_ || dockStructure_->isFloating()) return;
      const int W = width(), cur = dockStructure_->width();
      if (cur > std::max(240, int(W * 0.26))) resizeDocks({dockStructure_}, {std::max(240, int(W * 0.22))}, Qt::Horizontal);
      if (options_) emit options_->titleChanged(options_->title());   // re-run the Options height fit (buildDocks)
    });
  }
  if (o == viewer_) {
    if (e->type() == QEvent::Resize && sketchEd_) sketchEd_->setGeometry(viewer_->rect());
    if (e->type() == QEvent::KeyPress) {
      auto* ke = static_cast<QKeyEvent*>(e);
      if (ke->key() == Qt::Key_Escape) {
        if (planePick_) { cancelPlanePick("Sketch düzlemi seçimi iptal edildi."); notifyDesignMode(); return true; }
        if (!toolsReal_) {
          if (!pendingTwoEdge_.isEmpty()) { pendingTwoEdge_.clear(); statusBar()->showMessage("İki kenardan düzlem iptal edildi.", 4000); return true; }
          if (tools_->activeId() != "select") { tools_->activate("select"); return true; }
          selection_->clear();
          return true;
        }
      }
    }
    if (navigationFilter(e)) return true;
  }
  return QMainWindow::eventFilter(o, e);
}

// ------------------------------------------------------------------------------ Seçimden parametre
void MainWindow::createParamSelect() {
  ParamSelectController::Hooks h;
  h.commitNoValidate = [this](const Document& cand, const QString& text) { return commitNoValidate(cand, text); };
  h.selectParam = [this](const QString& name) {
    if (structure_) structure_->setCurrentPage("groups");
    rebuildParams();
    for (int r = 0; r < paramModel_->rowCount(); ++r)
      if (paramModel_->item(r, 0) && paramModel_->item(r, 0)->data(Qt::UserRole).toString() == name) {
        paramView_->selectRow(r);
        paramView_->scrollTo(paramModel_->index(r, 0));
      }
  };
  h.undoStack = [this] { return &undo_; };
  h.activeToolId = [this] { return tools_ ? tools_->activeId() : QString("select"); };
  paramSel_ = new ParamSelectController(this, h, this);
  connect(paramSel_, &ParamSelectController::measuresChanged, this, [this] { schedulePanelsRefresh(); });
  if (tools_) connect(tools_, &ToolManager::activeToolChanged, paramSel_, [this](const QString&) { paramSel_->refresh(); });
  connect(this, &MainWindow::designModeChanged, paramSel_, [this](DesignMode) { paramSel_->refresh(); });
}

} // namespace pcd

