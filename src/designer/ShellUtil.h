#pragma once
// SC23 WP-S: small shell helpers shared by MainWindowShell.cpp / MainWindowV2.cpp (not part of the frozen
// MainWindow.h). Implemented in MainWindowShell.cpp.
class QDockWidget;

namespace pcd {
// S-P1-9: collapse a left-column panel to its title bar (the neighbour panel takes the room) or expand it again.
void setDockCollapsed(QDockWidget* d, bool on);
bool dockCollapsed(const QDockWidget* d);
} // namespace pcd
