#pragma once
// PersistCAD drawing import — which optional dependencies this build has. FROZEN (DrawingIR 1.0).
// Compile time: PC_DRAWING_HAVE_QPDF / _OPENCV / _TESSERACT / _QTPDF are always defined to 0 or 1 for every target
// that links pc_drawing (PUBLIC compile definitions). Run time: buildFeatures() (src/drawing/core/Features.cpp, P0).
#include <string>

namespace pc::drawing {

struct BuildFeatures {
  bool qpdf = false;                // vector PDF ingest, PDF image extraction, PDF sniff
  bool opencv = false;              // raster ingest
  bool tesseract = false;           // OCR (Tesseract + Leptonica)
  bool qtpdf = false;               // pc_drawing_qt PDF rendering (Qt6::Pdf)
  std::string qpdfVersion, opencvVersion, tesseractVersion, qtVersion;   // "" if absent
  std::string summary() const;      // "qpdf 11.9.0, OpenCV 4.6.0, Tesseract 5.3.4, QtPdf 6.4.2"
};
const BuildFeatures& buildFeatures();
std::string libraryVersion();       // "pc_drawing 0.1 (DrawingIR 1.0)"

}  // namespace pc::drawing
