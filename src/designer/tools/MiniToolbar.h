#pragma once
// SC-UX (WP-B) — mini-toolbar (SPEC §1.2 #6, §2.2): a frameless translucent strip over the viewer.
//  * Sketch mode: Seç · 3B'ye Dön · Yeni sketch düzlemi · Izgarayı taşı · Plan görünümü (top-left of the view).
//  * 3D tools: the active tool's options flagged onMiniToolbar (Pull: Ekle/Kes/Ayrı, Yuvarlat/Pah, İki yöne,
//    Grup), shown near the last click; it fades when the cursor moves away.
#include <QFrame>
#include <QPointer>

class QGraphicsOpacityEffect;
class QHBoxLayout;
class QToolButton;

namespace pcd {
class ToolManager;
class IDesignHost;

class MiniToolbar : public QFrame {
  Q_OBJECT
public:
  MiniToolbar(ToolManager* tm, IDesignHost* host, QWidget* viewer);
  void rebuild();
  void setAnchor(const QPoint& viewerPos);      // last click
  // SC23 WP-S (S-P1-7): right click -> the strip appears right above the context menu at viewerPos ([G] §4)
  void showForContextMenu(const QPoint& viewerPos);
  void cursorMoved(const QPoint& viewerPos);    // fade with the distance
  bool sketchMode() const { return sketch_; }

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void place();
  QToolButton* addButton(const QString& name, const QString& text, const QString& tip, const QIcon& icon);
  QPointer<ToolManager> tm_;
  IDesignHost* host_ = nullptr;
  QPointer<QWidget> viewer_;
  QHBoxLayout* lay_ = nullptr;
  QGraphicsOpacityEffect* fx_ = nullptr;
  QPoint anchor_{-1, -1};
  bool ctxAnchor_ = false;
  bool hudConnected_ = false;
  bool sketch_ = false;
};

} // namespace pcd
