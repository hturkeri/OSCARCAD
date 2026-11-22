#pragma once
// PersistCAD appui — "Save as App" dialog (Qt). OWNER: Agent E.
// Lets the user: name/version/author/description the app, tick which parameters are exposed
// (label, min, max, step, group per parameter), choose export permissions, block-on-error,
// manufacturing check toggles, then choose output path & form (standalone executable and/or
// .pcapp package). Runs pc::app::lintApp and shows problems before writing.
// The dialog writes exposure edits back into the Document's ParamTable when accepted.
#include "pc/AppPackage.h"
#include <QDialog>
#include <memory>

namespace pc::appui {

class SaveAsAppDialog : public QDialog {
  Q_OBJECT
public:
  // runnerExePath: path of persistcad_runner (designer passes applicationDirPath()+"/persistcad_runner").
  SaveAsAppDialog(Document& doc, const QString& runnerExePath, QWidget* parent = nullptr);
  ~SaveAsAppDialog() override;

  // Path(s) written after exec() == Accepted.
  QString writtenExecutable() const;
  QString writtenPackage() const;

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

// Convenience: run the dialog modally; returns true if an app was written.
bool runSaveAsApp(Document& doc, const QString& runnerExePath, QWidget* parent);

} // namespace pc::appui
