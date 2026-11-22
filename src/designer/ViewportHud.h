#pragma once
// PersistCAD designer — SC23 viewport HUD (ANALYSIS §5.1 S-P0-2, §5.3). API FROZEN after P0-SC23
// (docs/sc2023/P0_CONTRACT.md); ViewportHud.cpp is owned by WP-S afterwards.
//
// A transparent overlay child of the viewer, top-left:
//   line 1  "<tool> · <selection>"         e.g. "Çek · 1 bölge"
//   line 2  message strip (Info / Warning / Error), optional timeout
//   below   guideArea(): where the tool guides (ToolGuideBar::place) go
// Mouse events pass through to the viewer everywhere except over the message strip. Qt skips the whole subtree of a
// WA_TransparentForMouseEvents widget, so the message strip is a SIBLING of the HUD (also a child of the viewer,
// created, positioned and deleted by the HUD); reach it with messageLabel() or viewer->findChild("viewportHud.message").
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>
#include <QLabel>
class QTimer;

namespace pcd {

class ViewportHud : public QWidget {
  Q_OBJECT
public:
  enum class Severity { Info, Warning, Error };

  explicit ViewportHud(QWidget* viewer);   // parent = the viewer; objectName "viewportHud"
  ~ViewportHud() override;

  void setToolText(const QString& text);        // short tool name, "" = none
  void setSelectionText(const QString& text);   // short selection summary, "" = none
  // Message strip (objectName "viewportHud.message"). ms > 0: cleared after ms milliseconds; 0 = until
  // clearMessage() / the next showMessage().
  void showMessage(const QString& text, Severity severity = Severity::Info, int ms = 0);
  void clearMessage();
  // Area (in viewer coordinates) right below the HUD text, reserved for the tool guides.
  QRect guideArea() const;

  QLabel* messageLabel() const { return message_; }

  // test accessors
  QString toolText() const { return tool_; }
  QString selectionText() const { return sel_; }
  QString messageText() const { return msg_; }
  Severity messageSeverity() const { return sev_; }

signals:
  void layoutChanged();   // geometry / guideArea() changed

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void relayout();
  QPointer<QWidget> viewer_;
  QLabel* title_ = nullptr;
  QPointer<QLabel> message_;
  QTimer* timer_ = nullptr;
  QString tool_, sel_, msg_;
  Severity sev_ = Severity::Info;
};

} // namespace pcd
