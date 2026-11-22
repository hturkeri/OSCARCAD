// WP-2: PDF page rendering with Qt6::Pdf (PDFium). Compiled only when PC_DRAWING_HAVE_QTPDF; fallback in nodeps/.
#include "pc/drawing/qt/PdfRender.h"
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <cmath>

namespace pc::drawing::qt {

namespace {
bool openDoc(QPdfDocument& doc, const QString& path, QString* err) {
  const auto e = doc.load(path);
  if (e != QPdfDocument::Error::None) {
    if (err) {
      switch (e) {
        case QPdfDocument::Error::FileNotFound: *err = QStringLiteral("PDF bulunamadı: ") + path; break;
        case QPdfDocument::Error::IncorrectPassword: *err = QStringLiteral("PDF parola korumalı: ") + path; break;
        case QPdfDocument::Error::UnsupportedSecurityScheme: *err = QStringLiteral("PDF güvenlik şeması desteklenmiyor"); break;
        default: *err = QStringLiteral("PDF açılamadı: ") + path; break;
      }
    }
    return false;
  }
  return true;
}

// Renders `page` at `px` and composites on white (QtPdf leaves the background transparent).
QImage renderOnWhite(QPdfDocument& doc, int page, QSize px) {
  QPdfDocumentRenderOptions opt;
  const QImage argb = doc.render(page, px, opt);
  if (argb.isNull()) return QImage();
  QImage out(px, QImage::Format_RGB32);
  out.fill(Qt::white);
  QPainter p(&out);
  p.drawImage(0, 0, argb);
  p.end();
  return out;
}
}  // namespace

bool hasPdfRendering() { return true; }

int pdfPageCount(const QString& path, QString* err) {
  QPdfDocument doc;
  if (!openDoc(doc, path, err)) return 0;
  return doc.pageCount();
}

bool renderPdfPage(const QString& path, int page, double dpi, RasterImage& out, QString* err, bool color) {
  QPdfDocument doc;
  if (!openDoc(doc, path, err)) return false;
  if (page < 0 || page >= doc.pageCount()) {
    if (err) *err = QStringLiteral("Geçersiz sayfa %1 (sayfa sayısı %2)").arg(page).arg(doc.pageCount());
    return false;
  }
  if (!(dpi > 0)) dpi = 300;
  const QSizeF pt = doc.pagePointSize(page);
  const QSize px(std::max(1, static_cast<int>(std::lround(pt.width() * dpi / 72.0))),
                 std::max(1, static_cast<int>(std::lround(pt.height() * dpi / 72.0))));
  if (static_cast<double>(px.width()) * px.height() > 400e6) {
    if (err) *err = QStringLiteral("Sayfa bu çözünürlükte çok büyük (%1x%2 px)").arg(px.width()).arg(px.height());
    return false;
  }
  const QImage img = renderOnWhite(doc, page, px);
  if (img.isNull()) {
    if (err) *err = QStringLiteral("PDF sayfası çizilemedi");
    return false;
  }
  out = fromQImage(color ? img.convertToFormat(QImage::Format_RGB888) : img.convertToFormat(QImage::Format_Grayscale8));
  out.dpi = dpi;
  return true;
}

QImage renderPdfThumbnail(const QString& path, int page, int maxPx, QString* err) {
  QPdfDocument doc;
  if (!openDoc(doc, path, err)) return QImage();
  if (page < 0 || page >= doc.pageCount()) {
    if (err) *err = QStringLiteral("Geçersiz sayfa %1").arg(page);
    return QImage();
  }
  const QSizeF pt = doc.pagePointSize(page);
  const double s = std::max(16, maxPx) / std::max(pt.width(), pt.height());
  return renderOnWhite(doc, page, QSize(std::max(1, int(pt.width() * s)), std::max(1, int(pt.height() * s))));
}

}  // namespace pc::drawing::qt
