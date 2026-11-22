// P0 (frozen): build feature report. Versions come from CMake (src/drawing/CMakeLists.txt).
#include "pc/drawing/Features.h"
#include "pc/drawing/Version.h"

#ifndef PC_DRAWING_QPDF_VERSION
#define PC_DRAWING_QPDF_VERSION ""
#endif
#ifndef PC_DRAWING_OPENCV_VERSION
#define PC_DRAWING_OPENCV_VERSION ""
#endif
#ifndef PC_DRAWING_QT_VERSION
#define PC_DRAWING_QT_VERSION ""
#endif
#ifndef PC_DRAWING_TESSERACT_VERSION
#define PC_DRAWING_TESSERACT_VERSION ""
#endif

namespace pc::drawing {

const BuildFeatures& buildFeatures() {
  static const BuildFeatures f = [] {
    BuildFeatures b;
    b.qpdf = PC_DRAWING_HAVE_QPDF != 0;
    b.opencv = PC_DRAWING_HAVE_OPENCV != 0;
    b.tesseract = PC_DRAWING_HAVE_TESSERACT != 0;
    b.qtpdf = PC_DRAWING_HAVE_QTPDF != 0;
    if (b.qpdf) b.qpdfVersion = PC_DRAWING_QPDF_VERSION;
    if (b.opencv) b.opencvVersion = PC_DRAWING_OPENCV_VERSION;
    if (b.tesseract) b.tesseractVersion = PC_DRAWING_TESSERACT_VERSION;
    if (b.qtpdf) b.qtVersion = PC_DRAWING_QT_VERSION;
    return b;
  }();
  return f;
}

std::string BuildFeatures::summary() const {
  auto item = [](const char* name, bool on, const std::string& v) {
    return std::string(name) + (on ? (v.empty() ? std::string(" var") : " " + v) : std::string(" yok"));
  };
  return item("qpdf", qpdf, qpdfVersion) + ", " + item("OpenCV", opencv, opencvVersion) + ", " +
         item("Tesseract", tesseract, tesseractVersion) + ", " + item("QtPdf", qtpdf, qtVersion);
}

std::string libraryVersion() { return std::string("pc_drawing 0.1 (") + kDrawingIRVersion + ")"; }

}  // namespace pc::drawing
