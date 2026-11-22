// Fallback compiled when Qt6::Pdf is NOT available (PC_DRAWING_HAVE_QTPDF=0). OWNER WP-2 (keep in sync; no QtPdf).
#include "pc/drawing/qt/PdfRender.h"

namespace pc::drawing::qt {

bool hasPdfRendering() { return false; }
int pdfPageCount(const QString&, QString* err) {
  if (err) *err = QStringLiteral("QtPdf olmadan derlendi");
  return 0;
}
bool renderPdfPage(const QString&, int, double, RasterImage&, QString* err, bool) {
  if (err) *err = QStringLiteral("QtPdf olmadan derlendi");
  return false;
}
QImage renderPdfThumbnail(const QString&, int, int, QString* err) {
  if (err) *err = QStringLiteral("QtPdf olmadan derlendi");
  return QImage();
}

}  // namespace pc::drawing::qt
