// P0 bootstrap GUI smoke test (OWNER P0/WP-3): pc_drawing_ui / pc_drawing_qt link and construct headless.
#include "pc/drawing/qt/PdfRender.h"
#include "pc/drawing/ui/ImportAction.h"
#include "pc/drawing/ui/OverlayView.h"
#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <cstdio>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  pc::drawing::DrawingIR ir;
  pc::drawing::ui::OverlayView view;
  view.setIR(&ir);
  view.refresh();
  if (view.renderToImage(64).isNull()) { std::fprintf(stderr, "renderToImage null\n"); return 1; }
  QMainWindow mw;
  QMenu menu;
  pc::drawing::ui::DrawingImportHost host;
  host.currentDocument = [] { return pc::Document(); };
  host.commit = [](const pc::Document&, const QString&) {};
  QAction* a = pc::drawing::ui::installDrawingImportAction(&mw, &menu, host);
  if (!a || menu.actions().isEmpty()) { std::fprintf(stderr, "action not installed\n"); return 1; }
  std::printf("qtpdf rendering: %d\n", pc::drawing::qt::hasPdfRendering() ? 1 : 0);
  return 0;
}
