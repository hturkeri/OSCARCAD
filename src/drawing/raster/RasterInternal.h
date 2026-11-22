#pragma once
// WP-2 PRIVATE: raster ingest internals (OpenCV). Pixel coordinates: x right, y DOWN, pixel centres at integers,
// in the DESKEWED (rectified) image. Arc angles are stored in the SHEET convention (CCW, y up) already.
#include "pc/drawing/Stages.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/version.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp>  // OpenCV 5: contour/shape geometry moved out of imgproc
#endif
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace pc::drawing::raster {

// ------------------------------------------------------------------ OpenCV 4.x / 5.x adapter (CvCompat.cpp)
namespace cvc {
bool hasLsd();                       // cv::createLineSegmentDetector usable (OpenCV >= 4.5.1, 5.x)
// Line segments for skew estimation: LSD when available, otherwise Canny + probabilistic Hough.
void detectSegments(const cv::Mat& gray8, std::vector<cv::Vec4f>& out);
int houghCircleMethod();             // HOUGH_GRADIENT_ALT when available, else HOUGH_GRADIENT
std::string versionString();         // "4.6.0"
}  // namespace cvc

struct Prep {
  cv::Mat gray;                      // deskewed, background-normalised gray (white paper)
  cv::Mat bin;                       // ink = 255 (deskewed, cleaned, outside border masked)
  int W = 0, H = 0;                  // rectified size
  int origW = 0, origH = 0;
  double dpi = 300;
  bool dpiEstimated = false;
  double skewDeg = 0;                // applied rotation, CCW positive (RasterInfo::deskewDeg)
  double noise = 0;                  // gray-level noise estimate
  bool noisy = false;
  cv::Rect border;                   // outer border (empty if not found)
  cv::Rect titleBlock;               // title block (empty if not found)
  std::array<double, 9> homography{1, 0, 0, 0, 1, 0, 0, 0, 1};
  double pxPerMm() const { return dpi / 25.4; }
  double mm(double px) const { return px * 25.4 / dpi; }
  double px(double mm) const { return mm * dpi / 25.4; }
};

struct Seg {
  cv::Point2d a, b;
  double width = 0;                  // px
  bool freeA = true, freeB = true;   // end not at a junction
  double support = 1;                // ink coverage 0..1
  int pieces = 1;                    // dashed: number of dash pieces
  std::vector<double> dashPx;        // dashed: on/off pattern (px)
  std::string lineType;              // "", "hidden", "center"
  bool arrowA = false, arrowB = false;
  bool dead = false;
  std::string ref;
  double length() const { return std::hypot(b.x - a.x, b.y - a.y); }
};

struct ArcF {
  cv::Point2d c;
  double r = 0;
  double a0 = 0, a1 = 360;           // SHEET convention degrees, CCW from a0 to a1 (a1 > a0)
  bool full = false;
  double width = 0;
  double rms = 0;
  double support = 1;
  bool dead = false;
  std::string ref;
  std::vector<cv::Point2d> pts;      // skeleton support points (refit on merge)
};

struct Arrow {
  cv::Point2d tip, base;
  double width = 0;                  // base width px
  bool filled = true;
  float conf = 0.7f;
};

struct TextBox {
  cv::Rect box;
  int rot = 0;                       // 0 horizontal, 90 vertical (reading direction decided by OCR: 90 or 270)
  std::vector<int> comps;            // connected-component labels
  bool inTitle = false;
  double charH = 0;                  // median character height (px, in reading frame)
};

struct Geometry {
  std::vector<Seg> lines;
  std::vector<ArcF> arcs;
  std::vector<Arrow> arrows;
  double thinW = 0, thickW = 0;      // stroke width clusters (px)
};

// Preprocess.cpp
bool preprocess(const RasterImage& img, const IngestOptions& opt, Prep& out, std::vector<Diagnostic>& diags,
                std::string* err);
double estimateDpiFromSize(int w, int h, bool* ok);
// TextRegions.cpp — text components separated from geometry; textMask = 255 on text pixels.
void detectText(const Prep& p, cv::Mat& labels, std::vector<TextBox>& boxes, cv::Mat& textMask);
// Vectorize.cpp
bool vectorize(const Prep& p, const cv::Mat& geomBin, Geometry& g, ProgressSink* progress);

// sheet-convention angle (deg, [0,360)) of point q around c in pixel coordinates (y down)
inline double sheetAngleDeg(const cv::Point2d& c, const cv::Point2d& q) {
  double a = std::atan2(-(q.y - c.y), q.x - c.x) * 180.0 / CV_PI;
  if (a < 0) a += 360;
  return a;
}

}  // namespace pc::drawing::raster
