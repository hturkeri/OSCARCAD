#pragma once
// PersistCAD drawing import — raster buffers shared by WP-1 (scanned-PDF image extraction), WP-2 (raster ingest)
// and pc_drawing_qt (QtPdf rendering). FROZEN (DrawingIR 1.0).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pc::drawing {

// 8-bit image. channels: 1 = gray, 3 = BGR, 4 = BGRA (OpenCV order). Rows top-to-bottom, `stride` bytes per row
// (>= width*channels; 0 means tightly packed). dpi 0 = unknown.
struct RasterImage {
  int width = 0, height = 0, channels = 1;
  std::size_t stride = 0;
  std::vector<uint8_t> data;
  double dpi = 0;
  std::size_t rowBytes() const { return stride ? stride : static_cast<std::size_t>(width) * channels; }
  bool empty() const { return width <= 0 || height <= 0 || data.empty(); }
};

// Renders / extracts page `page` of a PDF at `dpi` (Qt layer injects pc::drawing::qt::renderPdfPage here so that
// pc_drawing stays Qt-free). Returns false + *err on failure.
using PdfRasterizer = std::function<bool(const std::string& path, int page, double dpi, RasterImage& out, std::string* err)>;

// PNG / JPG / TIFF / BMP via OpenCV imgcodecs; reads DPI from metadata when available.        [WP-2, needs OpenCV]
bool loadRasterFile(const std::string& path, RasterImage& out, std::string* err = nullptr);
// Embedded page image of a scanned PDF (DCT / Flate; CCITT/JBIG2 -> false, caller falls back to a PdfRasterizer).
// dpi is derived from the image size vs the page box.                                         [WP-1, needs qpdf]
bool extractPdfPageImage(const std::string& path, int page, RasterImage& out, std::string* err = nullptr);

}  // namespace pc::drawing
