// WP-2: pc_drawing_qt root — RasterImage <-> QImage conversion and the PdfRasterizer adapter (Qt6::Gui only).
#include "pc/drawing/qt/PdfRender.h"
#include <cstring>

namespace pc::drawing::qt {

QImage toQImage(const RasterImage& img) {
  if (img.empty() || (img.channels != 1 && img.channels != 3 && img.channels != 4)) return QImage();
  const std::size_t rb = img.rowBytes();
  if (img.data.size() < rb * static_cast<std::size_t>(img.height)) return QImage();
  const QImage::Format f = img.channels == 1 ? QImage::Format_Grayscale8
                           : img.channels == 3 ? QImage::Format_RGB888 : QImage::Format_ARGB32;
  QImage out(img.width, img.height, f);
  if (out.isNull()) return out;
  out.setDotsPerMeterX(img.dpi > 0 ? qRound(img.dpi / 0.0254) : out.dotsPerMeterX());
  out.setDotsPerMeterY(img.dpi > 0 ? qRound(img.dpi / 0.0254) : out.dotsPerMeterY());
  for (int y = 0; y < img.height; ++y) {
    const uint8_t* s = img.data.data() + rb * y;
    uchar* d = out.scanLine(y);
    if (img.channels == 1) {
      std::memcpy(d, s, static_cast<std::size_t>(img.width));
    } else if (img.channels == 3) {          // BGR -> RGB
      for (int x = 0; x < img.width; ++x) { d[3 * x] = s[3 * x + 2]; d[3 * x + 1] = s[3 * x + 1]; d[3 * x + 2] = s[3 * x]; }
    } else {                                 // BGRA == ARGB32 in little-endian memory order
      auto* q = reinterpret_cast<QRgb*>(d);
      for (int x = 0; x < img.width; ++x) q[x] = qRgba(s[4 * x + 2], s[4 * x + 1], s[4 * x], s[4 * x + 3]);
    }
  }
  return out;
}

RasterImage fromQImage(const QImage& in) {
  RasterImage r;
  if (in.isNull()) return r;
  const bool gray = in.isGrayscale() || in.format() == QImage::Format_Grayscale8 || in.format() == QImage::Format_Grayscale16;
  QImage img;
  if (gray) {
    img = in.convertToFormat(QImage::Format_Grayscale8);
  } else {
    // composite transparency on white, then 8-bit RGB
    if (in.hasAlphaChannel()) {
      QImage w(in.size(), QImage::Format_RGB32);
      w.fill(Qt::white);
      for (int y = 0; y < in.height(); ++y) {
        auto* d = reinterpret_cast<QRgb*>(w.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
          const QRgb c = in.pixel(x, y);
          const int a = qAlpha(c);
          d[x] = qRgb((qRed(c) * a + 255 * (255 - a)) / 255, (qGreen(c) * a + 255 * (255 - a)) / 255,
                      (qBlue(c) * a + 255 * (255 - a)) / 255);
        }
      }
      img = w.convertToFormat(QImage::Format_RGB888);
    } else {
      img = in.convertToFormat(QImage::Format_RGB888);
    }
  }
  r.width = img.width();
  r.height = img.height();
  r.channels = gray ? 1 : 3;
  r.stride = 0;
  r.data.resize(static_cast<std::size_t>(r.width) * r.channels * r.height);
  for (int y = 0; y < r.height; ++y) {
    const uchar* s = img.constScanLine(y);
    uint8_t* d = r.data.data() + static_cast<std::size_t>(r.width) * r.channels * y;
    if (gray) std::memcpy(d, s, static_cast<std::size_t>(r.width));
    else
      for (int x = 0; x < r.width; ++x) { d[3 * x] = s[3 * x + 2]; d[3 * x + 1] = s[3 * x + 1]; d[3 * x + 2] = s[3 * x]; }
  }
  if (in.dotsPerMeterX() > 0) r.dpi = in.dotsPerMeterX() * 0.0254;
  return r;
}

PdfRasterizer makePdfRasterizer() {
  return [](const std::string& path, int page, double dpi, RasterImage& out, std::string* err) {
    QString e;
    const bool ok = renderPdfPage(QString::fromStdString(path), page, dpi, out, &e);
    if (!ok && err) *err = e.toStdString();
    return ok;
  };
}

}  // namespace pc::drawing::qt
