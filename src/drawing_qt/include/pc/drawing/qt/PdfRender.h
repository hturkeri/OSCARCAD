#pragma once
// pc_drawing_qt — Qt helpers for drawing import (Qt6::Gui; PDF rendering needs Qt6::Pdf). OWNER WP-2.
// FROZEN (DrawingIR 1.0). Without QtPdf the PDF functions return false / 0 with err "QtPdf olmadan derlendi".
#include "pc/drawing/Raster.h"
#include <QImage>
#include <QString>

namespace pc::drawing::qt {

bool hasPdfRendering();             // true if built with Qt6::Pdf
int pdfPageCount(const QString& path, QString* err = nullptr);   // 0 on error
// Renders page `page` (0-based) at `dpi`; transparent background composited on white; out = 8-bit gray (channels 1)
// or BGR (channels 3) when `color`; out.dpi = dpi. Works headless (QPA offscreen).
bool renderPdfPage(const QString& path, int page, double dpi, RasterImage& out, QString* err = nullptr, bool color = false);
// Thumbnail for the wizard's source page (long side `maxPx`).
QImage renderPdfThumbnail(const QString& path, int page, int maxPx, QString* err = nullptr);
// pc::drawing::PdfRasterizer bound to renderPdfPage (for IngestOptions::pdfRasterizer).
PdfRasterizer makePdfRasterizer();

QImage toQImage(const RasterImage& img);        // deep copy (Format_Grayscale8 / RGB888 / ARGB32)
RasterImage fromQImage(const QImage& img);      // deep copy -> gray (if grayscale) or BGR

}  // namespace pc::drawing::qt
