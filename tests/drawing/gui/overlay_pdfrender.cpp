// WP-2 GUI test (QPA offscreen): pc_drawing_qt — toQImage/fromQImage round trips, QtPdf page rendering (page count,
// dpi, gray/colour, errors, thumbnail), PdfRasterizer adapter feeding ingestRaster.
#include "pc/drawing/Drawing.h"
#include "pc/drawing/qt/PdfRender.h"
#include <QApplication>
#include <QDir>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <cmath>
#include <cstdio>

using namespace pc::drawing;

namespace {
int g_fail = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
  } while (0)

// A4 landscape, 2 pages; page 0: 0.7 mm rectangle 100x60 mm at (50,50) mm from top-left, red disc at (250,150) mm.
QString writePdf() {
  const QString path = QDir(QStringLiteral(PC_DRAWING_TMP_DIR)).filePath(QStringLiteral("wp2_overlay_pdfrender.pdf"));
  QPdfWriter w(path);
  w.setPageSize(QPageSize(QPageSize::A4));
  w.setPageOrientation(QPageLayout::Landscape);
  w.setPageMargins(QMarginsF(0, 0, 0, 0));
  w.setResolution(1200);
  QPainter p(&w);
  const double u = 1200 / 25.4;   // device units per mm
  p.setPen(QPen(Qt::black, 0.7 * u));
  p.drawRect(QRectF(50 * u, 50 * u, 100 * u, 60 * u));
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::red);
  p.drawEllipse(QPointF(250 * u, 150 * u), 10 * u, 10 * u);
  w.newPage();
  p.setPen(QPen(Qt::black, 0.5 * u));
  p.drawLine(QPointF(10 * u, 10 * u), QPointF(100 * u, 10 * u));
  p.end();
  return path;
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  // ---- conversions
  {
    RasterImage g;
    g.width = 5; g.height = 3; g.channels = 1; g.stride = 8; g.dpi = 300;
    g.data.assign(24, 0);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 5; ++x) g.data[y * 8 + x] = static_cast<uint8_t>(10 * y + x);
    const QImage q = qt::toQImage(g);
    CHECK(q.format() == QImage::Format_Grayscale8 && q.width() == 5 && q.height() == 3);
    CHECK(qGray(q.pixel(4, 2)) == 24);
    const RasterImage g2 = qt::fromQImage(q);
    CHECK(g2.channels == 1 && g2.width == 5 && g2.data.size() == 15 && g2.data[2 * 5 + 4] == 24);
    CHECK(std::abs(g2.dpi - 300) < 0.5);
    RasterImage c;
    c.width = 2; c.height = 1; c.channels = 3;
    c.data = {10, 20, 30, 40, 50, 60};   // BGR
    const QImage qc = qt::toQImage(c);
    CHECK(qRed(qc.pixel(0, 0)) == 30 && qBlue(qc.pixel(0, 0)) == 10);
    const RasterImage c2 = qt::fromQImage(qc);
    CHECK(c2.channels == 3 && c2.data == c.data);
    RasterImage a;
    a.width = 1; a.height = 1; a.channels = 4;
    a.data = {0, 0, 255, 0};   // fully transparent red -> white after compositing
    const RasterImage a2 = qt::fromQImage(qt::toQImage(a));
    CHECK(a2.channels == 3 && a2.data[0] == 255 && a2.data[1] == 255 && a2.data[2] == 255);
    CHECK(qt::toQImage(RasterImage()).isNull());
    CHECK(qt::fromQImage(QImage()).empty());
  }
  // ---- PDF rendering
  if (!qt::hasPdfRendering()) {
    QString e;
    CHECK(qt::pdfPageCount(QStringLiteral("x.pdf"), &e) == 0 && !e.isEmpty());
    std::printf("overlay_pdfrender: QtPdf yok, %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
  }
  const QString pdf = writePdf();
  QString err;
  CHECK(qt::pdfPageCount(pdf, &err) == 2);
  RasterImage img;
  CHECK(qt::renderPdfPage(pdf, 0, 100, img, &err));
  CHECK(img.channels == 1 && img.dpi == 100);
  CHECK(std::abs(img.width - 1169) <= 2 && std::abs(img.height - 827) <= 2);   // 297 x 210 mm @ 100 dpi
  auto at = [&](const RasterImage& r, double xmm, double ymm, int ch = 0) {
    const int x = static_cast<int>(xmm / 25.4 * r.dpi), y = static_cast<int>(ymm / 25.4 * r.dpi);
    return r.data[r.rowBytes() * y + static_cast<std::size_t>(x) * r.channels + ch];
  };
  CHECK(at(img, 100, 50) < 100);     // top edge of the rectangle
  CHECK(at(img, 100, 80) > 240);     // inside: white (transparent background composited)
  RasterImage col;
  CHECK(qt::renderPdfPage(pdf, 0, 72, col, &err, true));
  CHECK(col.channels == 3 && at(col, 250, 150, 2) > 200 && at(col, 250, 150, 0) < 60);   // red disc in BGR
  CHECK(!qt::renderPdfPage(pdf, 5, 100, img, &err) && !err.isEmpty());
  err.clear();
  CHECK(!qt::renderPdfPage(QStringLiteral("/nonexistent/x.pdf"), 0, 100, img, &err) && !err.isEmpty());
  const QImage th = qt::renderPdfThumbnail(pdf, 1, 200, &err);
  CHECK(!th.isNull() && std::max(th.width(), th.height()) == 200);
  // ---- PdfRasterizer -> ingestRaster
  {
    PdfRasterizer r = qt::makePdfRasterizer();
    RasterImage page;
    std::string e;
    CHECK(r(pdf.toStdString(), 0, 200, page, &e));
    CHECK(!r("/nonexistent.pdf", 0, 200, page, &e) && !e.empty());
    CHECK(r(pdf.toStdString(), 0, 200, page, &e));
    if (buildFeatures().opencv) {
      PrimitiveSheet s;
      int id = 1;
      IngestOptions o;
      o.ocrLanguages = {"eng"};
      const StageResult res = ingestRaster(page, o, s, id);
      CHECK(res.ok());
      int longLines = 0;
      for (const auto& p : s.prims)
        if (p.kind == PrimKind::Line && distance(p.p0, p.p1) > 50) ++longLines;
      CHECK(longLines >= 4);
    }
  }
  std::printf("overlay_pdfrender: %d failure(s)\n", g_fail);
  return g_fail ? 1 : 0;
}
