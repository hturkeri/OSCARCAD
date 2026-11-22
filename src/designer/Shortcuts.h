#pragma once
// SC-UX shell (WP-A): the single keyboard-shortcut table (SPEC §4). Every shortcut of the designer window,
// including the tool actions created by WP-B's ToolManager (which create their actions WITHOUT shortcuts), is
// assigned here by action objectName. Single-letter shortcuts are Qt::WindowShortcut; "View" entries are
// Qt::WidgetWithChildrenShortcut on the viewer. Tab / Space / digits / Enter / Esc / Shift+arrows are NOT
// shortcuts: ToolManager::installKeyRouting hands them to the active tool first (SPEC §7.3).
#include <QList>
#include <QString>
#include <functional>

class QAction;
class QObject;
class QWidget;

namespace pcd {

enum class ShortcutContext { Global, View, Sketch, Tool, Box };

struct ShortcutDef {
  const char* actionObjectName;
  const char* keys;          // QKeySequence text; several alternatives separated by ';' ("Z;Alt+Z")
  ShortcutContext ctx;
};

// The table (SPEC §4 order).
const QList<ShortcutDef>& shortcutTable();

// Assigns the table to the actions found under `window` (findChildren<QAction*> by objectName). View-context
// actions are also added to `viewWidget` with Qt::WidgetWithChildrenShortcut; global ones are added to the window
// (QWidget::addAction) so they work while no ribbon tab shows them. Returns the number of actions configured.
int applyShortcuts(QWidget* window, QWidget* viewWidget);

// SPEC §3.9: unsupported SpaceClaim keys (B, Ctrl+C, Ctrl+D, ...) show a one-line explanation in the status bar
// ("Blend (B) PersistCAD'de yok."). Creates one hidden QAction per key (objectName "unsupported.<keys>").
void installUnsupportedKeys(QWidget* window, std::function<void(const QString&)> showMessage);

// SC23 WP-S (S-P1-6): HTML tooltip "<b>Ad (English)</b> K / açıklama / F1: yardım" for a command, built from its
// tooltip ("Ad (English): açıklama — K"), its shortcut and a short description table for commands whose tooltip has
// none. Empty for actions without text.
QString richToolTip(const QAction* a);

// True if `w` currently takes typed text (line edits incl. table cell editors and ExprEdit, spin boxes, text edits,
// editable combo boxes, an item view whose current cell would start editing on a key press).
bool isTextInputWidget(const QWidget* w);

// Application-wide guard (SPEC §4, §10.4 #2): while a text input has the focus, unmodified / Shift-only printable
// keys never trigger a shortcut (ShortcutOverride accepted -> the key goes to the widget).
void installTextInputShortcutGuard(QObject* appOrWindow);

} // namespace pcd
