// WP-2 test helpers (see wp2_fixtures.h).
#include "wp2_fixtures.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#if PC_DRAWING_HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/version.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp>
#endif
#endif

using namespace pc::drawing;

namespace wp2 {

std::string fixtureDir() { return std::string(PC_DRAWING_TEST_DIR) + "/fixtures/wp2"; }
std::vector<std::string> fixtureNames() { return {"wp2_flange", "wp2_shaft", "wp2_plate"}; }

bool loadTruth(const std::string& name, Truth& t, std::string* err) {
  std::ifstream f(fixtureDir() + "/" + name + ".truth.json");
  if (!f) { if (err) *err = "truth missing: " + name; return false; }
  nlohmann::json j;
  try { f >> j; } catch (const std::exception& e) { if (err) *err = e.what(); return false; }
  t = Truth{};
  t.name = name;
  t.W = j["size"][0];
  t.H = j["size"][1];
  t.dpi = j.value("dpi", 300.0);
  for (auto& l : j["lines"]) t.lines.push_back({l["p0"][0], l["p0"][1], l["p1"][0], l["p1"][1], l["w"], l["type"]});
  for (auto& c : j["circles"]) t.circles.push_back({c["c"][0], c["c"][1], c["r"], c["w"]});
  for (auto& a : j["arcs"]) t.arcs.push_back({a["c"][0], a["c"][1], a["r"], a["a0"], a["a1"], a["w"]});
  for (auto& a : j["arrows"]) t.arrows.push_back({a["tip"][0], a["tip"][1], a["base"][0], a["base"][1]});
  for (auto& x : j["texts"])
    t.texts.push_back({x["box"][0], x["box"][1], x["box"][2], x["box"][3], x["text"], x["rot"], x["dim"], x["circle"], x["angular"]});
  return true;
}

bool makeVariant(const std::string& name, const std::string& variant, RasterImage& img, std::string* err) {
#if PC_DRAWING_HAVE_OPENCV
  const std::string path = fixtureDir() + "/" + name + ".png";
  if (variant == "clean") return loadRasterFile(path, img, err);
  cv::Mat g = cv::imread(path, cv::IMREAD_GRAYSCALE);
  if (g.empty()) { if (err) *err = "cannot read " + path; return false; }
  cv::RNG rng(variant == "noisy" ? 1234 : 4321);
  cv::Mat f;
  g.convertTo(f, CV_32F);
  int q = 85;
  if (variant == "noisy") {
    cv::GaussianBlur(f, f, cv::Size(0, 0), 1.0);
    cv::Mat n(f.size(), CV_32F);
    rng.fill(n, cv::RNG::NORMAL, 0, 15);
    f += n;
    q = 60;
  } else if (variant == "skewed") {
    const cv::Point2f c((g.cols - 1) / 2.f, (g.rows - 1) / 2.f);
    cv::Mat R = cv::getRotationMatrix2D(c, 2.0, 1.0);   // 2 deg CCW
    cv::warpAffine(f, f, R, f.size(), cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(255));
    cv::GaussianBlur(f, f, cv::Size(0, 0), 0.7);
    cv::Mat n(f.size(), CV_32F);
    rng.fill(n, cv::RNG::NORMAL, 0, 6);
    f += n;
  } else {
    if (err) *err = "unknown variant " + variant;
    return false;
  }
  cv::Mat u8;
  f.convertTo(u8, CV_8U);
  std::vector<uchar> buf;
  cv::imencode(".jpg", u8, buf, {cv::IMWRITE_JPEG_QUALITY, q});
  cv::Mat dec = cv::imdecode(buf, cv::IMREAD_GRAYSCALE);
  img = RasterImage{};
  img.width = dec.cols;
  img.height = dec.rows;
  img.channels = 1;
  img.dpi = 300;
  img.data.assign(dec.datastart, dec.dataend);
  return true;
#else
  (void)name; (void)variant; (void)img;
  if (err) *err = "built without OpenCV";
  return false;
#endif
}

std::string normalizeWs(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n') { if (!o.empty() && o.back() != ' ') o.push_back(' '); }
    else o.push_back(c);
  }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  return o;
}

namespace {
struct P2 { double x, y; };
double d2(P2 a, P2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

template <class T, class D, class F>
PR greedyMatch(const std::vector<T>& truth, const std::vector<D>& det, F cost) {
  PR pr;
  pr.nTruth = static_cast<int>(truth.size());
  pr.nDet = static_cast<int>(det.size());
  std::vector<std::tuple<double, int, int>> c;
  for (std::size_t i = 0; i < truth.size(); ++i)
    for (std::size_t j = 0; j < det.size(); ++j) {
      const double v = cost(truth[i], det[j]);
      if (v >= 0) c.emplace_back(v, static_cast<int>(i), static_cast<int>(j));
    }
  std::sort(c.begin(), c.end());
  std::vector<uint8_t> ut(truth.size()), ud(det.size());
  for (auto& [v, i, j] : c) {
    if (ut[i] || ud[j]) continue;
    ut[i] = ud[j] = 1;
    ++pr.tp;
  }
  return pr;
}
}  // namespace

Metrics evaluate(const PrimitiveSheet& sheet, const Truth& t, double tolMm) {
  Metrics m;
  if (!sheet.raster) return m;
  m.deskewDeg = sheet.raster->deskewDeg;
  Affine2 inv{};
  invert(sheet.raster->pxToSheet, inv);
  auto px = [&](Vec2 v) { const Vec2 q = transformPoint(inv, v); return P2{q.x, q.y}; };
  const double pxPerMm = t.dpi / 25.4;
  const double tol = tolMm * pxPerMm;
  struct DL { P2 a, b; };
  struct DC { P2 c; double r; double a0, a1; };
  std::vector<DL> lines, arrows;
  std::vector<DC> circles, arcs;
  for (const auto& p : sheet.prims) {
    if (p.kind == PrimKind::Line) lines.push_back({px(p.p0), px(p.p1)});
    else if (p.kind == PrimKind::Arrowhead) arrows.push_back({px(p.p0), px(p.p1)});
    else if (p.kind == PrimKind::Circle) circles.push_back({px(p.center), p.radius * pxPerMm, 0, 360});
    else if (p.kind == PrimKind::Arc) arcs.push_back({px(p.center), p.radius * pxPerMm, p.a0, p.a1});
  }
  m.lines = greedyMatch(t.lines, lines, [&](const TLine& a, const DL& b) {
    const P2 a0{a.x0, a.y0}, a1{a.x1, a.y1};
    const double e = std::min(std::max(d2(a0, b.a), d2(a1, b.b)), std::max(d2(a0, b.b), d2(a1, b.a)));
    return e <= std::max(tol, 0.6 * a.w + 2) ? e : -1.0;
  });
  if (std::getenv("PC_WP2_VERBOSE")) {
    for (const auto& a : t.lines) {
      double best = 1e9; const DL* bd = nullptr;
      for (const auto& b : lines) {
        const P2 a0{a.x0, a.y0}, a1{a.x1, a.y1};
        const double e = std::min(std::max(d2(a0, b.a), d2(a1, b.b)), std::max(d2(a0, b.b), d2(a1, b.a)));
        if (e < best) { best = e; bd = &b; }
      }
      if (best > std::max(tol, 0.6 * a.w + 2))
        std::printf("  miss line %s (%.0f,%.0f)-(%.0f,%.0f) best %.1f%s\n", a.type.c_str(), a.x0, a.y0, a.x1, a.y1, best,
                    bd ? (" (" + std::to_string(int(bd->a.x)) + "," + std::to_string(int(bd->a.y)) + ")-(" + std::to_string(int(bd->b.x)) + "," + std::to_string(int(bd->b.y)) + ")").c_str() : "");
    }
    for (const auto& b : lines) {
      bool any = false;
      for (const auto& a : t.lines) {
        const P2 a0{a.x0, a.y0}, a1{a.x1, a.y1};
        const double e = std::min(std::max(d2(a0, b.a), d2(a1, b.b)), std::max(d2(a0, b.b), d2(a1, b.a)));
        if (e <= std::max(tol, 0.6 * a.w + 2)) any = true;
      }
      if (!any) std::printf("  extra line (%.0f,%.0f)-(%.0f,%.0f) len %.0f\n", b.a.x, b.a.y, b.b.x, b.b.y, d2(b.a, b.b));
    }
    for (const auto& a : t.arrows) {
      double best = 1e9;
      for (const auto& b : arrows) best = std::min(best, std::max(d2({a.tx, a.ty}, b.a), d2({a.bx, a.by}, b.b)));
      if (best > std::max(tol * 1.5, 8.0)) std::printf("  miss arrow tip (%.0f,%.0f) base (%.0f,%.0f) best %.1f\n", a.tx, a.ty, a.bx, a.by, best);
    }
    for (const auto& c : circles) std::printf("  det circle (%.1f,%.1f) r %.1f\n", c.c.x, c.c.y, c.r);
    for (const auto& c : arcs) std::printf("  det arc (%.1f,%.1f) r %.1f %.0f..%.0f\n", c.c.x, c.c.y, c.r, c.a0, c.a1);
  }
  m.circles = greedyMatch(t.circles, circles, [&](const TCircle& a, const DC& b) {
    const double e = std::max(d2({a.cx, a.cy}, b.c), std::abs(a.r - b.r));
    return e <= std::max(tol * 0.7, 0.02 * a.r) ? e : -1.0;
  });
  m.arcs = greedyMatch(t.arcs, arcs, [&](const TArc& a, const DC& b) {
    const double e = std::max(d2({a.cx, a.cy}, b.c), std::abs(a.r - b.r));
    auto ad = [](double x, double y) { double d = std::fmod(std::abs(x - y), 360.0); return std::min(d, 360 - d); };
    if (ad(a.a0, b.a0) > 8 || ad(a.a1, b.a1) > 8) return -1.0;
    return e <= std::max(tol, 0.03 * a.r) ? e : -1.0;
  });
  m.arrows = greedyMatch(t.arrows, arrows, [&](const TArrow& a, const DL& b) {
    const double e = std::max(d2({a.tx, a.ty}, b.a), d2({a.bx, a.by}, b.b));
    return e <= std::max(tol * 1.5, 8.0) ? e : -1.0;
  });
  // texts: match by box overlap
  std::vector<std::array<double, 4>> boxes;
  for (const auto& r : sheet.texts) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& q : r.quad) { const P2 p = px(q); x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    boxes.push_back({x0, y0, x1, y1});
  }
  std::vector<int> det(sheet.texts.size());
  for (std::size_t i = 0; i < det.size(); ++i) det[i] = static_cast<int>(i);
  std::vector<int> assigned(t.texts.size(), -1);
  m.textBoxes = greedyMatch(t.texts, det, [&](const TText& a, int j) {
    const auto& b = boxes[j];
    const double ix = std::min(a.x1, b[2]) - std::max(a.x0, b[0]), iy = std::min(a.y1, b[3]) - std::max(a.y0, b[1]);
    if (ix <= 0 || iy <= 0) return -1.0;
    const double inter = ix * iy, uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b[2] - b[0]) * (b[3] - b[1]) - inter;
    const double iou = inter / uni;
    return iou >= 0.3 ? 1 - iou : -1.0;
  });
  // re-run the matching to know the pairs (small n)
  {
    std::vector<std::tuple<double, int, int>> c;
    for (std::size_t i = 0; i < t.texts.size(); ++i)
      for (std::size_t j = 0; j < boxes.size(); ++j) {
        const auto& a = t.texts[i];
        const auto& b = boxes[j];
        const double ix = std::min(a.x1, b[2]) - std::max(a.x0, b[0]), iy = std::min(a.y1, b[3]) - std::max(a.y0, b[1]);
        if (ix <= 0 || iy <= 0) continue;
        const double inter = ix * iy, uni = (a.x1 - a.x0) * (a.y1 - a.y0) + (b[2] - b[0]) * (b[3] - b[1]) - inter;
        if (inter / uni >= 0.3) c.emplace_back(1 - inter / uni, static_cast<int>(i), static_cast<int>(j));
      }
    std::sort(c.begin(), c.end());
    std::vector<uint8_t> ud(boxes.size());
    for (auto& [v, i, j] : c) if (assigned[i] < 0 && !ud[j]) { assigned[i] = j; ud[j] = 1; }
  }
  for (std::size_t i = 0; i < t.texts.size(); ++i) {
    const auto& tt = t.texts[i];
    const TextRun* r = assigned[i] >= 0 ? &sheet.texts[assigned[i]] : nullptr;
    const std::string got = r ? r->utf8 : std::string();
    const std::string raw = r ? (r->attrs.count("ocr.raw") ? r->attrs.at("ocr.raw") : r->utf8) : std::string();
    if (tt.dim) {
      ++m.dimTotal;
      const std::string want = formatDimText(parseDimText(tt.text));
      const bool rawOk = normalizeWs(raw) == normalizeWs(tt.text);
      const bool gOk = normalizeWs(got) == normalizeWs(want);
      m.dimRawExact += rawOk;
      m.dimGrammarExact += gOk;
      if (!gOk) m.notes.push_back(t.name + " dim '" + tt.text + "' raw='" + raw + "' -> '" + got + "'");
    } else {
      ++m.noteTotal;
      const bool ok = normalizeWs(got) == normalizeWs(tt.text);
      m.noteExact += ok;
      if (!ok) m.notes.push_back(t.name + " note '" + tt.text + "' -> '" + got + "'");
    }
  }
  return m;
}

void accumulate(Metrics& acc, const Metrics& m) {
  auto add = [](PR& a, const PR& b) { a.tp += b.tp; a.nTruth += b.nTruth; a.nDet += b.nDet; };
  add(acc.lines, m.lines); add(acc.circles, m.circles); add(acc.arcs, m.arcs); add(acc.arrows, m.arrows);
  add(acc.textBoxes, m.textBoxes);
  acc.dimTotal += m.dimTotal; acc.dimRawExact += m.dimRawExact; acc.dimGrammarExact += m.dimGrammarExact;
  acc.noteTotal += m.noteTotal; acc.noteExact += m.noteExact;
  acc.notes.insert(acc.notes.end(), m.notes.begin(), m.notes.end());
}

}  // namespace wp2
