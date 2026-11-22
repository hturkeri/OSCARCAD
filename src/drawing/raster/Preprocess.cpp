// WP-2: raster pre-processing — gray conversion, (photo rectification), DPI, deskew, noise estimate, background
// normalisation, adaptive binarisation, speckle removal, border + title-block detection and outside-border masking.
#include "raster/RasterInternal.h"
#include <algorithm>
#include <numeric>

namespace pc::drawing::raster {

namespace {

cv::Mat toGray(const RasterImage& img) {
  cv::Mat src(img.height, img.width, CV_8UC(img.channels), const_cast<uint8_t*>(img.data.data()), img.rowBytes());
  cv::Mat g;
  if (img.channels == 1) g = src.clone();
  else if (img.channels == 3) cv::cvtColor(src, g, cv::COLOR_BGR2GRAY);
  else {
    // composite BGRA on white
    g.create(img.height, img.width, CV_8U);
    for (int y = 0; y < img.height; ++y) {
      const uint8_t* s = src.ptr<uint8_t>(y);
      uint8_t* d = g.ptr<uint8_t>(y);
      for (int x = 0; x < img.width; ++x) {
        const double a = s[4 * x + 3] / 255.0;
        const double l = 0.114 * s[4 * x] + 0.587 * s[4 * x + 1] + 0.299 * s[4 * x + 2];
        d[x] = cv::saturate_cast<uint8_t>(a * l + (1 - a) * 255);
      }
    }
  }
  return g;
}

double weightedMedian(std::vector<std::pair<double, double>> v) {   // (value, weight)
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  double tot = 0;
  for (auto& p : v) tot += p.second;
  double acc = 0;
  for (auto& p : v) {
    acc += p.second;
    if (acc >= tot / 2) return p.first;
  }
  return v.back().first;
}

// Skew from long near-horizontal / near-vertical segments (length weighted median), degrees in image coords.
double estimateSkew(const cv::Mat& gray) {
  cv::Mat small = gray;
  double f = 1;
  const int longSide = std::max(gray.cols, gray.rows);
  if (longSide > 2000) {
    f = 2000.0 / longSide;
    cv::resize(gray, small, cv::Size(), f, f, cv::INTER_AREA);
  }
  std::vector<cv::Vec4f> segs;
  cvc::detectSegments(small, segs);
  std::vector<std::pair<double, double>> v;
  const double minLen = std::min(small.cols, small.rows) * 0.04;
  for (const auto& l : segs) {
    const double dx = l[2] - l[0], dy = l[3] - l[1];
    const double len = std::hypot(dx, dy);
    if (len < minLen) continue;
    double a = std::atan2(dy, dx) * 180 / CV_PI;
    double r = std::fmod(a + 360.0, 90.0);
    if (r > 45) r -= 90;
    if (std::abs(r) > 10) continue;
    v.emplace_back(r, len * len);
  }
  if (v.size() < 3) return 0;
  // refine: mean of the inliers around the weighted median
  const double med = weightedMedian(v);
  double s = 0, w = 0;
  for (auto& p : v)
    if (std::abs(p.first - med) < 0.5) { s += p.first * p.second; w += p.second; }
  return w > 0 ? s / w : med;
}

double estimateNoise(const cv::Mat& gray) {
  cv::Mat med;
  cv::medianBlur(gray, med, 3);
  double sum = 0;
  long n = 0;
  for (int y = 0; y < gray.rows; y += 2) {
    const uint8_t* g = gray.ptr<uint8_t>(y);
    const uint8_t* m = med.ptr<uint8_t>(y);
    for (int x = 0; x < gray.cols; x += 2) {
      if (m[x] < 170) continue;
      sum += std::abs(int(g[x]) - int(m[x]));
      ++n;
    }
  }
  return n ? sum / n : 0;
}

void removeSpeckles(cv::Mat& bin, int minArea) {
  cv::Mat labels, stats, cent;
  const int n = cv::connectedComponentsWithStats(bin, labels, stats, cent, 8, CV_32S);
  std::vector<uint8_t> kill(n, 0);
  for (int i = 1; i < n; ++i)
    if (stats.at<int>(i, cv::CC_STAT_AREA) < minArea) kill[i] = 1;
  for (int y = 0; y < bin.rows; ++y) {
    const int* l = labels.ptr<int>(y);
    uint8_t* b = bin.ptr<uint8_t>(y);
    for (int x = 0; x < bin.cols; ++x)
      if (l[x] && kill[l[x]]) b[x] = 0;
  }
}

// rows (or cols) whose long-line ink fraction is high
std::vector<int> longLineRows(const cv::Mat& lines, bool horizontal) {
  std::vector<int> out;
  const int n = horizontal ? lines.rows : lines.cols;
  for (int i = 0; i < n; ++i) {
    const int cnt = cv::countNonZero(horizontal ? lines.row(i) : lines.col(i));
    if (cnt > 0) out.push_back(i);
  }
  return out;
}

void detectBorderAndTitle(Prep& p) {
  const int W = p.W, H = p.H;
  cv::Mat hl, vl;
  cv::morphologyEx(p.bin, hl, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(10, int(W * 0.45)), 1)));
  cv::morphologyEx(p.bin, vl, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, std::max(10, int(H * 0.45)))));
  const auto rows = longLineRows(hl, true), cols = longLineRows(vl, false);
  int top = -1, bot = -1, left = -1, right = -1;
  for (int r : rows) if (r < H * 0.25) { top = r; break; }
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) if (*it > H * 0.75) { bot = *it; break; }
  for (int c : cols) if (c < W * 0.25) { left = c; break; }
  for (auto it = cols.rbegin(); it != cols.rend(); ++it) if (*it > W * 0.75) { right = *it; break; }
  if (top < 0 || bot < 0 || left < 0 || right < 0) return;
  p.border = cv::Rect(left, top, right - left + 1, bot - top + 1);
  // mask everything outside the border (scanner margins, punch holes, edge shadows)
  const int m = 2;
  for (int y = 0; y < H; ++y) {
    uint8_t* b = p.bin.ptr<uint8_t>(y);
    for (int x = 0; x < W; ++x)
      if (x < left - m || x > right + m || y < top - m || y > bot + m) b[x] = 0;
  }
  // title block: a horizontal line in the lower part that reaches the right border, whose left end continues down
  // to the bottom border as a vertical line.
  cv::Mat hl2;
  cv::morphologyEx(p.bin, hl2, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(10, int(W * 0.12)), 1)));
  const int innerRight = right - static_cast<int>(p.px(1.5));
  const int innerBot = bot - static_cast<int>(p.px(3));
  for (int y = static_cast<int>(top + (bot - top) * 0.5); y < innerBot; ++y) {
    if (hl2.at<uint8_t>(y, std::max(0, innerRight)) == 0) continue;
    int x = innerRight;
    while (x > left && (hl2.at<uint8_t>(y, x) || (y > 0 && hl2.at<uint8_t>(y - 1, x)) || (y + 1 < H && hl2.at<uint8_t>(y + 1, x)))) --x;
    const int xl = x + 1;
    if (innerRight - xl < W * 0.12) continue;
    // vertical line at xl (± 6 px) down to bottom border
    int best = 0;
    for (int dx = -6; dx <= 6; ++dx) {
      const int xx = xl + dx;
      if (xx < 0 || xx >= W) continue;
      int cnt = 0;
      for (int yy = y; yy <= bot; ++yy) if (p.bin.at<uint8_t>(yy, xx)) ++cnt;
      best = std::max(best, cnt);
    }
    if (best >= 0.9 * (bot - y)) {
      p.titleBlock = cv::Rect(xl, y, right - xl + 1, bot - y + 1);
      break;
    }
  }
}

bool rectifyPhoto(cv::Mat& gray, std::array<double, 9>& Hm, std::vector<Diagnostic>& diags) {
  cv::Mat blur, th;
  cv::GaussianBlur(gray, blur, cv::Size(5, 5), 0);
  cv::threshold(blur, th, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);   // paper = bright
  std::vector<std::vector<cv::Point>> cs;
  cv::findContours(th, cs, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  double bestA = 0;
  std::vector<cv::Point> quad;
  for (auto& c : cs) {
    const double a = cv::contourArea(c);
    if (a < 0.2 * gray.total()) continue;
    std::vector<cv::Point> ap;
    cv::approxPolyDP(c, ap, 0.02 * cv::arcLength(c, true), true);
    if (ap.size() == 4 && cv::isContourConvex(ap) && a > bestA) { bestA = a; quad = ap; }
  }
  if (quad.empty()) {
    diags.push_back({Severity::Warning, "raster.photo.noQuad", "Fotoğrafta kağıt dörtgeni bulunamadı; düzeltme yapılmadı",
                     static_cast<int>(Stage::Ingest), {}});
    return false;
  }
  // order TL, TR, BR, BL
  std::sort(quad.begin(), quad.end(), [](auto& a, auto& b) { return a.y < b.y; });
  if (quad[0].x > quad[1].x) std::swap(quad[0], quad[1]);
  if (quad[2].x < quad[3].x) std::swap(quad[2], quad[3]);
  const double wTop = cv::norm(quad[1] - quad[0]), wBot = cv::norm(quad[2] - quad[3]);
  const double hL = cv::norm(quad[3] - quad[0]), hR = cv::norm(quad[2] - quad[1]);
  double w = std::max(wTop, wBot), h = std::max(hL, hR);
  // snap the aspect to ISO 216 (sqrt 2) when close
  const double asp = w / h, iso = w > h ? std::sqrt(2.0) : 1 / std::sqrt(2.0);
  if (std::abs(asp / iso - 1) < 0.12) h = w / iso;
  std::vector<cv::Point2f> src{quad[0], quad[1], quad[2], quad[3]};
  std::vector<cv::Point2f> dst{{0, 0}, {float(w - 1), 0}, {float(w - 1), float(h - 1)}, {0, float(h - 1)}};
  cv::Mat M = cv::getPerspectiveTransform(src, dst);
  cv::Mat out;
  cv::warpPerspective(gray, out, M, cv::Size(int(w), int(h)), cv::INTER_CUBIC, cv::BORDER_CONSTANT, 255);
  gray = out;
  for (int i = 0; i < 9; ++i) Hm[i] = M.at<double>(i / 3, i % 3);
  diags.push_back({Severity::Info, "raster.photo.rectified", "Fotoğraf perspektifi düzeltildi (en iyi çaba)",
                   static_cast<int>(Stage::Ingest), {}});
  return true;
}

}  // namespace

double estimateDpiFromSize(int w, int h, bool* ok) {
  static const double sizes[][2] = {{841, 1189}, {594, 841}, {420, 594}, {297, 420}, {210, 297}, {148, 210},
                                    {215.9, 279.4}, {279.4, 431.8}};
  const double lo = std::min(w, h), hi = std::max(w, h);
  for (double dpi : {300.0, 200.0, 150.0, 400.0, 600.0, 100.0, 72.0, 96.0}) {
    const double a = lo * 25.4 / dpi, b = hi * 25.4 / dpi;
    for (auto& s : sizes)
      if (std::abs(a / s[0] - 1) < 0.03 && std::abs(b / s[1] - 1) < 0.03) {
        if (ok) *ok = true;
        return dpi;
      }
  }
  if (ok) *ok = false;
  return 300;
}

bool preprocess(const RasterImage& img, const IngestOptions& opt, Prep& p, std::vector<Diagnostic>& diags, std::string* err) {
  const int st = static_cast<int>(Stage::Ingest);
  if (img.empty() || (img.channels != 1 && img.channels != 3 && img.channels != 4) ||
      img.data.size() < img.rowBytes() * static_cast<std::size_t>(img.height - 1) + static_cast<std::size_t>(img.width) * img.channels) {
    if (err) *err = "ingestRaster: geçersiz görüntü";
    return false;
  }
  p.origW = img.width;
  p.origH = img.height;
  cv::Mat gray = toGray(img);
  if (opt.rasterIsPhoto) rectifyPhoto(gray, p.homography, diags);
  // DPI
  if (opt.rasterDpiOverride > 0) {
    p.dpi = opt.rasterDpiOverride;
  } else if (img.dpi > 0) {
    p.dpi = img.dpi;
  } else {
    bool ok = false;
    p.dpi = estimateDpiFromSize(gray.cols, gray.rows, &ok);
    p.dpiEstimated = true;
    diags.push_back({Severity::Warning, "raster.dpiEstimated",
                     ok ? "DPI dosyada yok; kağıt boyutuna göre " + std::to_string(int(p.dpi)) + " dpi varsayıldı"
                        : "DPI dosyada yok ve kağıt boyutu tanınmadı; 300 dpi varsayıldı",
                     st, {}});
  }
  if (p.dpi < 100) diags.push_back({Severity::Warning, "raster.lowDpi", "Çözünürlük düşük (<100 dpi); tanıma güvenilmez", st, {}});
  // deskew
  const double skew = estimateSkew(gray);
  p.skewDeg = 0;
  if (std::abs(skew) > 0.03 && std::abs(skew) <= 10) {
    const cv::Point2f c(static_cast<float>((gray.cols - 1) / 2.0), static_cast<float>((gray.rows - 1) / 2.0));
    cv::Mat R = cv::getRotationMatrix2D(c, skew, 1.0);
    cv::Mat rot;
    cv::warpAffine(gray, rot, R, gray.size(), cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(255));
    gray = rot;
    p.skewDeg = skew;
    diags.push_back({Severity::Info, "raster.deskew", "Eğiklik düzeltildi: " + std::to_string(skew) + "°", st, {}});
  }
  p.W = gray.cols;
  p.H = gray.rows;
  // noise + denoise
  p.noise = estimateNoise(gray);
  p.noisy = p.noise > 3.0;
  cv::Mat work = gray;
  if (p.noisy) {
    cv::medianBlur(gray, work, 3);
    if (p.noise > 6.0) cv::GaussianBlur(work, work, cv::Size(3, 3), 0.8);
  }
  // background normalisation: divide by a closing that removes the strokes (illumination / paper tone)
  int k = static_cast<int>(p.px(2.5)) | 1;
  k = std::clamp(k, 15, 101);
  cv::Mat bg;
  cv::Mat small;
  const double f = 0.25;
  cv::resize(work, small, cv::Size(), f, f, cv::INTER_AREA);
  cv::morphologyEx(small, small, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(3, int(k * f) | 1), std::max(3, int(k * f) | 1))));
  cv::resize(small, bg, work.size(), 0, 0, cv::INTER_LINEAR);
  cv::Mat norm(work.size(), CV_8U);
  for (int y = 0; y < work.rows; ++y) {
    const uint8_t* s = work.ptr<uint8_t>(y);
    const uint8_t* b = bg.ptr<uint8_t>(y);
    uint8_t* d = norm.ptr<uint8_t>(y);
    for (int x = 0; x < work.cols; ++x) d[x] = cv::saturate_cast<uint8_t>(255.0 * s[x] / std::max<int>(b[x], 40));
  }
  p.gray = norm;
  // binarise: Otsu on the normalised image, clamped (a nearly empty page must not get a silly threshold)
  cv::Mat tmp;
  double t = cv::threshold(norm, tmp, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
  t = std::clamp(t, 110.0, 215.0);
  cv::threshold(norm, p.bin, t, 255, cv::THRESH_BINARY_INV);
  const int minArea = std::max(4, static_cast<int>(std::lround(std::pow(p.px(p.noisy ? 0.3 : 0.2), 2))));
  removeSpeckles(p.bin, minArea);
  if (p.noisy) {
    // fill pinholes in strokes (noise inside ink)
    cv::Mat inv = 255 - p.bin;
    removeSpeckles(inv, 4);
    p.bin = 255 - inv;
  }
  detectBorderAndTitle(p);
  return true;
}

}  // namespace pc::drawing::raster
