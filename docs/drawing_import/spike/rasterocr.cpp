// Feasibility spike: raster pipeline on a rendered drawing — deskew estimate, binarize, LSD segments, Hough circles,
// and Tesseract OCR (sparse text, digits+drawing symbols whitelist) with 0/90/270 deg passes for rotated dims.
// Usage: rasterocr page.png [tessdata_dir]
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <cstdio>
#include <string>

static void ocrPass(tesseract::TessBaseAPI& api, const cv::Mat& bin, int rotDeg) {
  cv::Mat img = bin;
  if (rotDeg == 90) cv::rotate(bin, img, cv::ROTATE_90_CLOCKWISE);
  if (rotDeg == 270) cv::rotate(bin, img, cv::ROTATE_90_COUNTERCLOCKWISE);
  api.SetImage(img.data, img.cols, img.rows, 1, (int)img.step);
  api.SetSourceResolution(300);
  api.Recognize(nullptr);
  tesseract::ResultIterator* it = api.GetIterator();
  if (!it) return;
  int n = 0;
  do {
    const char* w = it->GetUTF8Text(tesseract::RIL_WORD);
    if (!w) continue;
    float conf = it->Confidence(tesseract::RIL_WORD);
    int x1, y1, x2, y2; it->BoundingBox(tesseract::RIL_WORD, &x1, &y1, &x2, &y2);
    std::string s(w); delete[] w;
    bool hasDigit = false; for (char c : s) if (c >= '0' && c <= '9') hasDigit = true;
    if (hasDigit && conf > 50 && n < 400) { printf("  rot%3d conf%3.0f [%4d,%4d] '%s'\n", rotDeg, conf, x1, y1, s.c_str()); n++; }
  } while (it->Next(tesseract::RIL_WORD));
  delete it;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: rasterocr page.png [tessdata]\n"); return 2; }
  cv::Mat gray = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
  if (gray.empty()) { fprintf(stderr, "cannot read\n"); return 1; }
  cv::Mat bin; cv::adaptiveThreshold(gray, bin, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 31, 15);
  // LSD
  auto lsd = cv::createLineSegmentDetector(cv::LSD_REFINE_STD);
  std::vector<cv::Vec4f> lines; lsd->detect(gray, lines);
  int hv = 0; double angSum = 0; int angN = 0;
  for (auto& l : lines) { double a = std::atan2(l[3] - l[1], l[2] - l[0]) * 180 / CV_PI; double len = std::hypot(l[2]-l[0], l[3]-l[1]);
    double r = std::fmod(a + 360, 90.0); if (r > 45) r -= 90;
    if (std::fabs(r) < 2 && len > 60) { hv++; angSum += r * len; angN += (int)len; } }
  printf("image %dx%d  LSD segments=%zu  near-H/V long=%d  skew estimate=%.3f deg\n", gray.cols, gray.rows, lines.size(), hv, angN ? angSum / angN : 0.0);
  std::vector<cv::Vec3f> circles; cv::Mat blur; cv::medianBlur(gray, blur, 5);
  cv::HoughCircles(blur, circles, cv::HOUGH_GRADIENT_ALT, 1.5, 20, 300, 0.85, 8, 600);
  printf("HoughCircles(ALT) found %zu circles\n", circles.size());
  tesseract::TessBaseAPI api;
  if (api.Init(argc > 2 ? argv[2] : nullptr, "eng", tesseract::OEM_LSTM_ONLY)) { fprintf(stderr, "tesseract init failed\n"); return 1; }
  api.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);
  api.SetVariable("tessedit_char_whitelist", "0123456789.,+-±°ØRMx×/\"()ABCDEFHIKLNOSTUWX:");
  for (int rot : {0, 90}) ocrPass(api, bin, rot);
  api.End();
  return 0;
}
