#pragma once
// SC-UX shell (WP-A): SpaceClaim-like ribbon built from plain Qt6 Widgets (SPEC §1.2 #1-#4).
//
//   [Dosya ▾][quick access: Yeni Aç Kaydet Geri Yinele] | Sketch | Tasarım | Görüntü | Ölçüm | İmalat |   <title>
//   +--------------------------------------------------------------------------------------------------+
//   | [large][large] [s][s] | [s][s][s] | ...                                                    [»]   |
//   | (group content: embedded, non-movable QToolBars -> widgetForAction() / defaultAction() work)    |
//   |________Yönlendir______|____Sketch____|...                                                        |
//
// Large buttons: 32 px icon, text under the icon (QAction::iconText() may carry a "\n" for two lines; the
// action's text() stays the harness-visible text). Small buttons: 16 px icon + text (or icon only), stacked
// three per column. Groups are QFrames named "ribbonGroup.<key>", pages "ribbonTab.<key>".
//
// SC23 WP-S (S-P0-1) — width management. A group never gets squeezed (its width is fixed to its size hint, so no
// label is ever elided). When the tab's groups are wider than the ribbon, groups shrink in their collapse order
// (setCollapsePriority, lower = earlier; -1 = never):
//   level 0  Expanded   as built
//   level 1  Compact    large buttons become small "icon + text" buttons, three per column (groups that have large
//                       buttons and no embedded widget only)
//   level 2  Collapsed  one large drop-down button "<group title> ▾" listing the group's commands (QMenu)
// First every shrinkable group goes to Compact (in collapse order), then to Collapsed. If the tab still does not fit,
// the overflow button "»" (objectName "ribbonOverflow") at the right edge lists the commands of the groups that
// stick out. Re-evaluated on every resize, tab change and group show/hide.
#include <QFrame>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabBar;
class QToolBar;
class QToolButton;

namespace pcd {

class RibbonGroup : public QFrame {
  Q_OBJECT
public:
  enum class Level { Expanded = 0, Compact = 1, Collapsed = 2 };

  RibbonGroup(const QString& key, const QString& title, QWidget* parent = nullptr);
  QString key() const { return key_; }
  QString title() const;
  // Large button (32 px icon, text under). Consecutive large buttons share one toolbar.
  QToolButton* addLarge(QAction* a);
  // Small button (16 px). Three per column; iconOnly columns are narrow (tooltip carries the text).
  QToolButton* addSmall(QAction* a, bool iconOnly = false);
  void newColumn();                      // close the current small column
  void addWidget(QWidget* w);            // arbitrary widget in its own column (e.g. an expression field)
  void addSeparator();                   // thin vertical line between columns
  // The button of `a` that is shown at the current level (the expanded one if the group is collapsed).
  QToolButton* buttonFor(const QAction* a) const;
  QList<QToolButton*> buttons() const;
  QList<QAction*> actions() const { return order_; }
  void setContextual(bool on);           // contextual groups (sketch) get a tinted title strip
  bool isContextual() const { return contextual_; }

  // SC23 WP-S: width management (see the file comment)
  void setCollapsePriority(int p) { priority_ = p; }   // lower shrinks first, -1 = never
  int collapsePriority() const { return priority_; }
  void setCollapsedIcon(const QIcon& icon);            // icon of the drop-down button (default: first action's)
  void setMaxLevel(Level l) { maxLevel_ = l; }         // e.g. Compact: shrinks but never becomes a drop-down
  Level maxLevel() const { return maxLevel_; }
  void setLevel(Level l);
  Level level() const { return level_; }
  bool isCollapsed() const { return level_ == Level::Collapsed; }
  bool canCompact() const { return hasLarge_ && !hasWidget_; }
  int widthAt(Level l) const;                          // width the group needs at a level
  QToolButton* collapseButton() const { return collapseBtn_; }   // objectName "ribbonCollapse.<key>"
  QMenu* collapsedMenu() const { return menu_; }

private:
  QToolBar* newBar(bool large, bool iconOnly, QHBoxLayout* row);
  void buildCompact();
  void ensureCollapseButton();
  void fillMenu();
  QString key_;
  QWidget* content_ = nullptr;           // level 0
  QHBoxLayout* row_ = nullptr;
  QWidget* compact_ = nullptr;           // level 1 (built on demand)
  QToolButton* collapseBtn_ = nullptr;   // level 2
  QMenu* menu_ = nullptr;
  QLabel* title_ = nullptr;
  QToolBar* largeBar_ = nullptr;
  QToolBar* smallBar_ = nullptr;
  bool smallIconOnly_ = false;
  int smallCount_ = 0;
  bool contextual_ = false;
  bool hasLarge_ = false, hasWidget_ = false;
  int priority_ = 0;
  Level level_ = Level::Expanded;
  Level maxLevel_ = Level::Collapsed;
  QIcon collapsedIcon_;
  struct Item { QAction* a; bool large; bool iconOnly; bool column; };   // column: a newColumn()/separator mark
  QList<Item> items_;
  QList<QAction*> order_;
  QHash<const QAction*, QToolButton*> buttons_;          // level 0 buttons
  QHash<const QAction*, QToolButton*> compactButtons_;   // level 1 buttons
};

class Ribbon : public QWidget {
  Q_OBJECT
public:
  explicit Ribbon(QWidget* parent = nullptr);

  QToolButton* fileButton() const { return fileBtn_; }      // objectName "ribbonFileButton"
  QToolBar* quickAccessBar() const { return qat_; }         // objectName "quickAccessBar"
  QTabBar* tabBar() const { return tabs_; }                 // objectName "ribbonTabBar"
  void setFileMenu(QMenu* m);

  int addTab(const QString& key, const QString& title);     // page objectName "ribbonTab.<key>"
  RibbonGroup* addGroup(const QString& tabKey, const QString& key, const QString& title);
  void addStretch(const QString& tabKey);                   // right-aligns the following groups
  RibbonGroup* group(const QString& key) const { return groups_.value(key, nullptr); }
  QList<RibbonGroup*> groupsOf(const QString& tabKey) const;
  QStringList tabTexts() const;
  QStringList tabKeys() const { return keys_; }
  QString currentTabKey() const;
  void setCurrentTab(const QString& key);
  QWidget* page(const QString& key) const;
  void setTitleText(const QString& t);                      // right-hand document title
  void setTabToolTip(const QString& key, const QString& tip);

  // SC23 WP-S: width management (see the file comment). Applied to the current tab; also called by resize /
  // tab change / group show-hide (queued). Returns the groups that still stick out (listed under "»").
  QList<RibbonGroup*> fitGroups();
  QToolButton* overflowButton() const { return overflow_; }   // objectName "ribbonOverflow"
  QSize minimumSizeHint() const override;

  static constexpr int kPageHeight = 94;                    // SPEC §1.2 #3: fixed ribbon body height

signals:
  void currentTabChanged(const QString& key);
  void groupsFitted();                                      // after fitGroups() changed something

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void scheduleFit();
  QToolButton* fileBtn_ = nullptr;
  QToolBar* qat_ = nullptr;
  QTabBar* tabs_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QLabel* title_ = nullptr;
  QToolButton* overflow_ = nullptr;
  QMenu* overflowMenu_ = nullptr;
  QList<QPointer<RibbonGroup>> overflowGroups_;
  bool fitQueued_ = false, fitting_ = false;
  QStringList keys_;
  QHash<QString, QWidget*> pageByKey_;
  QHash<QString, RibbonGroup*> groups_;
  QHash<QString, QList<RibbonGroup*>> groupsByTab_;
};

} // namespace pcd
