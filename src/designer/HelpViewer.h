#pragma once
// SC23 WP-S (S-P1-6): F1 help. A simple non-modal in-app viewer of docs/KULLANIM_KILAVUZU.md (Markdown in a
// QTextBrowser) with a find field. SpaceClaim's F1 opens the help for the hovered command [G]; here F1 opens the
// user guide and, when a ribbon button is under the mouse, jumps to the first mention of its name. F3 (video) is
// not offered.
#include <QDialog>

class QLabel;
class QLineEdit;
class QTextBrowser;

namespace pcd {

class HelpViewer : public QDialog {
  Q_OBJECT
public:
  explicit HelpViewer(QWidget* parent = nullptr);   // objectName "helpViewer"
  // Path of docs/KULLANIM_KILAVUZU.md: $PC_DOCS_DIR, the source tree of this build, then docs/ next to / above the
  // executable and the working directory. Empty if not found.
  static QString guidePath();
  bool loaded() const { return loaded_; }
  QString sourcePath() const { return path_; }
  bool findText(const QString& text);               // from the top; false if not found
  QTextBrowser* browser() const { return browser_; }
  // the window's single viewer (created on demand), shown and raised; topic = text to look for ("" = top)
  static HelpViewer* showHelp(QWidget* parent, const QString& topic = QString());

private:
  QTextBrowser* browser_ = nullptr;
  QLineEdit* find_ = nullptr;
  QLabel* status_ = nullptr;
  QString path_;
  bool loaded_ = false;
};

} // namespace pcd
