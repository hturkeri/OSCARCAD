#pragma once
// SC-UX (WP-B) — tool guides (SPEC §1.2 #7): exclusive icon buttons showing the active tool's guides (Pull: Seç,
// Yön, Döndür, Kadar, Cetvel). Tab cycles them (ToolManager).
// SC23 WP-S (S-P1-5): placed at the viewer's TOP-LEFT, in the viewport HUD's guideArea() (below "<tool> · <seçim>"
// and its message line), "Seç" alone on top, the other guides in TWO columns (SpaceClaim 2023 R1 [G]). Without a HUD
// (test hosts) the same slot is computed from fixed margins.
#include <QFrame>
#include <QPointer>

class QButtonGroup;
class QGridLayout;

namespace pcd {
class ToolManager;

class ToolGuideBar : public QFrame {
  Q_OBJECT
public:
  ToolGuideBar(ToolManager* tm, QWidget* viewer);
  QString checkedKey() const;
  void rebuild();
  static QIcon guideIcon(const QString& key, int size = 24);

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void place();
  QPointer<ToolManager> tm_;
  QPointer<QWidget> viewer_;
  QGridLayout* lay_ = nullptr;
  QPointer<QWidget> hud_;   // SC23: the viewer's ViewportHud (found lazily)
  QButtonGroup* group_ = nullptr;
};

} // namespace pcd
