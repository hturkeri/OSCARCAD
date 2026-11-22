// WP-2: thin OpenCV 4.x / 5.x adapter. Only core + imgproc + imgcodecs are used anywhere in raster/.
//  * LSD (cv::createLineSegmentDetector) was removed in 3.4.6..4.5.0 for licence reasons and is back since 4.5.1;
//    5.x keeps it in imgproc. Without it we fall back to Canny + HoughLinesP (only used for skew estimation; the
//    geometry itself comes from our own skeleton tracer, which is version independent).
//  * HOUGH_GRADIENT_ALT exists since 4.3.
#include "raster/RasterInternal.h"
#include <opencv2/core/version.hpp>

#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && (CV_VERSION_MINOR > 5 || (CV_VERSION_MINOR == 5 && CV_VERSION_REVISION >= 1)))
#define PC_RASTER_HAVE_LSD 1
#else
#define PC_RASTER_HAVE_LSD 0
#endif
#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 3)
#define PC_RASTER_HAVE_HOUGH_ALT 1
#else
#define PC_RASTER_HAVE_HOUGH_ALT 0
#endif

namespace pc::drawing::raster::cvc {

bool hasLsd() { return PC_RASTER_HAVE_LSD != 0; }

void detectSegments(const cv::Mat& gray8, std::vector<cv::Vec4f>& out) {
  out.clear();
#if PC_RASTER_HAVE_LSD
  try {
    auto lsd = cv::createLineSegmentDetector(cv::LSD_REFINE_STD);
    lsd->detect(gray8, out);
    return;
  } catch (const cv::Exception&) {
    out.clear();   // some builds stub LSD out and throw "not implemented" -> fall through
  }
#endif
  cv::Mat edges;
  cv::Canny(gray8, edges, 50, 150);
  std::vector<cv::Vec4i> l;
  const int minLen = std::max(20, std::min(gray8.cols, gray8.rows) / 20);
  cv::HoughLinesP(edges, l, 1, CV_PI / 1800, 80, minLen, 3);
  for (const auto& s : l) out.emplace_back(static_cast<float>(s[0]), static_cast<float>(s[1]), static_cast<float>(s[2]), static_cast<float>(s[3]));
}

int houghCircleMethod() {
#if PC_RASTER_HAVE_HOUGH_ALT
  return cv::HOUGH_GRADIENT_ALT;
#else
  return cv::HOUGH_GRADIENT;
#endif
}

std::string versionString() { return CV_VERSION; }

}  // namespace pc::drawing::raster::cvc
