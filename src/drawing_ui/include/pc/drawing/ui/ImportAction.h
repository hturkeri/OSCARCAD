#pragma once
// pc_drawing_ui — "Dosya > Teknik resimden içe aktar…" entry point. OWNER WP-3. FROZEN API (DrawingIR 1.0).
//
// Integration (applied by the designer owner after the current designer work lands; see OWNERSHIP.md):
//   src/designer/CMakeLists.txt : target_link_libraries(pc_designer PUBLIC pc_drawing_ui)   (if TARGET pc_drawing_ui)
//   MainWindowShell.cpp         : QAction* a = pc::drawing::ui::installDrawingImportAction(this, menuFile_, host);
//                                 ribbon_->...addAction(a)   // Ekle sekmesi > "Teknik resim"
#include "pc/Document.h"
#include <QString>
#include <functional>

class QAction;
class QMainWindow;
class QMenu;
class QWidget;

namespace pc::drawing::ui {

struct DrawingImportHost {
  std::function<pc::Document()> currentDocument;                                       // required
  // Apply the merged document as ONE undo step with text undoText ("Teknik resimden içe aktar").
  std::function<void(const pc::Document& merged, const QString& undoText)> commit;   // required
  std::function<void(const pc::Document& fresh)> openAsNewDocument;                   // optional ("Yeni belge")
  // Optional: open the designer's SketchEditor on a sketch (wizard step 5); returns false if cancelled.
  std::function<bool(QWidget* parent, pc::Sketch& sketch)> editSketch;
  QString tessdataDir;              // "" = <app dir>/../share/persistcad/tessdata
};

// Creates the action, inserts it into `fileMenu` (may be nullptr) and returns it (parent = mw) so the caller can
// also put it on the ribbon. Triggering it runs the wizard.
QAction* installDrawingImportAction(QMainWindow* mw, QMenu* fileMenu, const DrawingImportHost& host);
// Runs the modal wizard directly (tests / harness). Returns QDialog::Accepted / Rejected.
int runDrawingImportWizard(QWidget* parent, const DrawingImportHost& host, const QString& initialFile = QString());

}  // namespace pc::drawing::ui
