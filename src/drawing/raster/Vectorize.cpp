// WP-2: geometry vectorisation of the (text-free) binary drawing.
//   1. Zhang–Suen thinning + staircase removal -> 1-px skeleton; distance transform -> local stroke width.
//   2. Skeleton graph tracing (endpoints, junction clusters, loops) -> pixel chains; spur pruning.
//   3. Recursive line-or-circle fitting of every chain (split at the point of max chord deviation).
//   4. Merging: collinear lines (junction splits), co-circular arcs (-> full circles), corner / T snapping.
//   5. Hough circles (OpenCV) as extra candidates, accepted only with >= 85 % ink support on the circumference.
//   6. Dashed / centre-line grouping (collinear short pieces with regular gaps -> one styled line).
//   7. Arrowheads: tapering stroke-width profile at line ends (filled triangles).
// Everything here is version independent (only cv::distanceTransform / HoughCircles / basic Mat ops are used).
#include "raster/RasterInternal.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <numeric>

namespace pc::drawing::raster {

namespace {

constexpr int kDX[8] = {0, 1, 1, 1, 0, -1, -1, -1};   // N NE E SE S SW W NW  (y down: N = -1)
constexpr int kDY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// ------------------------------------------------------------------------------------------------ thinning
void zhangSuen(cv::Mat& img) {   // img: 0/1 uint8, border pixels must be 0
  const int W = img.cols, H = img.rows;
  std::vector<int> pts;
  for (int y = 1; y < H - 1; ++y) {
    const uint8_t* r = img.ptr<uint8_t>(y);
    for (int x = 1; x < W - 1; ++x) if (r[x]) pts.push_back(y * W + x);
  }
  uint8_t* d = img.data;
  std::vector<int> del;
  bool changed = true;
  while (changed) {
    changed = false;
    for (int pass = 0; pass < 2; ++pass) {
      del.clear();
      for (int idx : pts) {
        if (!d[idx]) continue;
        const uint8_t p2 = d[idx - W], p3 = d[idx - W + 1], p4 = d[idx + 1], p5 = d[idx + W + 1], p6 = d[idx + W],
                      p7 = d[idx + W - 1], p8 = d[idx - 1], p9 = d[idx - W - 1];
        const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
        if (B < 2 || B > 6) continue;
        const int A = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) + (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
        if (A != 1) continue;
        if (pass == 0) { if (p2 * p4 * p6 || p4 * p6 * p8) continue; }
        else { if (p2 * p4 * p8 || p2 * p6 * p8) continue; }
        del.push_back(idx);
      }
      for (int idx : del) d[idx] = 0;
      if (!del.empty()) changed = true;
    }
    pts.erase(std::remove_if(pts.begin(), pts.end(), [&](int i) { return !d[i]; }), pts.end());
  }
  // staircase removal: delete pixels whose neighbours stay 8-connected without them (not endpoints)
  for (int idx : pts) {
    if (!d[idx]) continue;
    int nb[8], cnt = 0;
    for (int k = 0; k < 8; ++k) { nb[k] = d[idx + kDY[k] * W + kDX[k]]; cnt += nb[k]; }
    if (cnt < 2 || cnt > 3) continue;
    // remove only the corner of an L made by two 4-neighbours that touch diagonally (N&E, E&S, S&W, W&N),
    // with the diagonal between them empty and no other neighbour except possibly the opposite diagonal
    for (int k = 0; k < 8; k += 2) {
      const int a = k, b = (k + 2) % 8, diag = k + 1;
      if (nb[a] && nb[b] && !nb[diag] && cnt == 2) { d[idx] = 0; break; }
    }
  }
}

// ------------------------------------------------------------------------------------------------ fitting
struct LineFit { cv::Point2d c, dir; double maxDev = 0, rms = 0; };
LineFit fitLine(const std::vector<cv::Point2d>& p, std::size_t i0, std::size_t i1) {
  LineFit f;
  const double n = static_cast<double>(i1 - i0 + 1);
  double mx = 0, my = 0;
  for (std::size_t i = i0; i <= i1; ++i) { mx += p[i].x; my += p[i].y; }
  mx /= n; my /= n;
  double sxx = 0, sxy = 0, syy = 0;
  for (std::size_t i = i0; i <= i1; ++i) {
    const double dx = p[i].x - mx, dy = p[i].y - my;
    sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
  }
  const double th = 0.5 * std::atan2(2 * sxy, sxx - syy);
  f.c = {mx, my};
  f.dir = {std::cos(th), std::sin(th)};
  double s2 = 0;
  for (std::size_t i = i0; i <= i1; ++i) {
    const double dv = std::abs(-(p[i].x - mx) * f.dir.y + (p[i].y - my) * f.dir.x);
    f.maxDev = std::max(f.maxDev, dv);
    s2 += dv * dv;
  }
  f.rms = std::sqrt(s2 / n);
  return f;
}

struct CircFit { cv::Point2d c; double r = 0, maxDev = 1e9, rms = 1e9; bool ok = false; };
CircFit fitCircle(const std::vector<cv::Point2d>& p, std::size_t i0, std::size_t i1) {
  CircFit f;
  const std::size_t n = i1 - i0 + 1;
  if (n < 5) return f;
  // Kasa, centred for conditioning
  double mx = 0, my = 0;
  for (std::size_t i = i0; i <= i1; ++i) { mx += p[i].x; my += p[i].y; }
  mx /= n; my /= n;
  cv::Matx33d A = cv::Matx33d::zeros();
  cv::Vec3d b(0, 0, 0);
  for (std::size_t i = i0; i <= i1; ++i) {
    const double x = p[i].x - mx, y = p[i].y - my, z = x * x + y * y;
    const cv::Vec3d r(x, y, 1);
    for (int a = 0; a < 3; ++a) { for (int c = 0; c < 3; ++c) A(a, c) += r[a] * r[c]; b[a] += r[a] * z; }
  }
  cv::Vec3d s;
  if (!cv::solve(A, b, s, cv::DECOMP_SVD)) return f;
  double cx = s[0] / 2, cy = s[1] / 2;
  double r = std::sqrt(std::max(0.0, s[2] + cx * cx + cy * cy));
  // Gauss-Newton (geometric distance), a few iterations
  for (int it = 0; it < 8; ++it) {
    cv::Matx33d JtJ = cv::Matx33d::zeros();
    cv::Vec3d Jtr(0, 0, 0);
    for (std::size_t i = i0; i <= i1; ++i) {
      const double dx = p[i].x - mx - cx, dy = p[i].y - my - cy, d = std::hypot(dx, dy);
      if (d < 1e-9) continue;
      const cv::Vec3d J(-dx / d, -dy / d, -1);
      const double res = d - r;
      for (int a = 0; a < 3; ++a) { for (int c = 0; c < 3; ++c) JtJ(a, c) += J[a] * J[c]; Jtr[a] += J[a] * res; }
    }
    cv::Vec3d delta;
    if (!cv::solve(JtJ, -Jtr, delta, cv::DECOMP_SVD)) break;
    cx += delta[0]; cy += delta[1]; r += delta[2];
    if (cv::norm(delta) < 1e-4) break;
  }
  f.c = {cx + mx, cy + my};
  f.r = r;
  double s2 = 0;
  f.maxDev = 0;
  for (std::size_t i = i0; i <= i1; ++i) {
    const double dv = std::abs(std::hypot(p[i].x - f.c.x, p[i].y - f.c.y) - r);
    f.maxDev = std::max(f.maxDev, dv);
    s2 += dv * dv;
  }
  f.rms = std::sqrt(s2 / n);
  f.ok = std::isfinite(r) && r > 0;
  return f;
}

double angDiff(double a, double b) {   // smallest |a-b| for directions modulo 180
  double d = std::fmod(std::abs(a - b), 180.0);
  return std::min(d, 180 - d);
}
double dirDeg(const cv::Point2d& v) { double a = std::atan2(v.y, v.x) * 180 / CV_PI; if (a < 0) a += 180; if (a >= 180) a -= 180; return a; }
double norm2(const cv::Point2d& v) { return std::hypot(v.x, v.y); }

struct Chain {
  std::vector<cv::Point2d> pts;
  bool freeA = false, freeB = false, loop = false;
};

class Vectorizer {
 public:
  Vectorizer(const Prep& p, const cv::Mat& geom, Geometry& g) : P(p), G(geom), out(g) {}
  bool run(ProgressSink* prog);

 private:
  const Prep& P;
  const cv::Mat& G;
  Geometry& out;
  cv::Mat dist, skel;
  std::vector<Chain> chains;
  double tolBase = 1.3;

  double localWidth(const std::vector<cv::Point2d>& pts, std::size_t i0, std::size_t i1) const;
  void trace();
  void fitChain(const Chain& ch);
  void fitRange(const std::vector<cv::Point2d>& pts, std::size_t i0, std::size_t i1, bool freeA, bool freeB, int depth);
  void refineTangents(const std::vector<std::pair<bool, int>>& seq);
  bool arrowNear(const cv::Point2d& j, const cv::Point2d& u, double w) const;
  std::vector<std::pair<bool, int>>* seq_ = nullptr;   // chain-order record of fitted primitives (arc?, index)
  void mergeLines();
  void mergeArcs();
  void absorbIntoCircles();
  void houghCircles();
  void groupDashes();
  void snapCorners();
  void detectArrows();
  void finish();
  double support(const cv::Point2d& a, const cv::Point2d& b, double w) const;
  bool ink(int x, int y) const { return x >= 0 && y >= 0 && x < G.cols && y < G.rows && G.at<uint8_t>(y, x); }
};

double Vectorizer::localWidth(const std::vector<cv::Point2d>& pts, std::size_t i0, std::size_t i1) const {
  std::vector<float> v;
  const std::size_t n = i1 - i0 + 1;
  const std::size_t skip = n > 12 ? 3 : 0;
  for (std::size_t i = i0 + skip; i + skip <= i1; ++i) {
    const int x = static_cast<int>(std::lround(pts[i].x)), y = static_cast<int>(std::lround(pts[i].y));
    if (x >= 0 && y >= 0 && x < dist.cols && y < dist.rows) v.push_back(dist.at<float>(y, x));
  }
  if (v.empty()) return 1;
  std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
  return std::max(1.0, 2.0 * v[v.size() / 2] - 1.0);
}

void Vectorizer::trace() {
  const int W = skel.cols, H = skel.rows;
  const uint8_t* s = skel.data;
  auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && s[y * W + x]; };
  cv::Mat deg = cv::Mat::zeros(skel.size(), CV_8U);
  std::vector<cv::Point> nodes;
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      if (!s[y * W + x]) continue;
      int c = 0;
      for (int k = 0; k < 8; ++k) c += on(x + kDX[k], y + kDY[k]);
      deg.at<uint8_t>(y, x) = static_cast<uint8_t>(c);
      if (c != 2) nodes.emplace_back(x, y);
    }
  cv::Mat visited = cv::Mat::zeros(skel.size(), CV_8U);
  auto isNode = [&](int x, int y) { const int d = deg.at<uint8_t>(y, x); return on(x, y) && d != 2; };
  auto walk = [&](cv::Point start, cv::Point first, Chain& ch) {
    ch.pts.clear();
    ch.pts.emplace_back(start.x, start.y);
    cv::Point prev = start, cur = first;
    for (;;) {
      ch.pts.emplace_back(cur.x, cur.y);
      if (isNode(cur.x, cur.y)) { ch.freeB = deg.at<uint8_t>(cur.y, cur.x) == 1; return; }
      visited.at<uint8_t>(cur.y, cur.x) = 1;
      cv::Point next(-1, -1);
      // prefer a node neighbour (ends the chain), then 4-neighbours, then diagonals
      for (int pass = 0; pass < 3 && next.x < 0; ++pass)
        for (int k = 0; k < 8; ++k) {
          if (pass == 1 && (k & 1)) continue;
          if (pass == 2 && !(k & 1)) continue;
          const int nx = cur.x + kDX[k], ny = cur.y + kDY[k];
          if (!on(nx, ny) || (nx == prev.x && ny == prev.y)) continue;
          if (pass == 0) {
            if (isNode(nx, ny) && !(nx == start.x && ny == start.y && ch.pts.size() <= 2)) { next = {nx, ny}; break; }
            continue;
          }
          if (isNode(nx, ny) || visited.at<uint8_t>(ny, nx)) continue;
          next = {nx, ny};
          break;
        }
      if (next.x < 0) { ch.freeB = true; return; }
      prev = cur;
      cur = next;
    }
  };
  for (const auto& n : nodes) {
    const int d = deg.at<uint8_t>(n.y, n.x);
    if (d == 0) continue;
    for (int k = 0; k < 8; ++k) {
      const int nx = n.x + kDX[k], ny = n.y + kDY[k];
      if (!on(nx, ny) || isNode(nx, ny) || visited.at<uint8_t>(ny, nx)) continue;
      Chain ch;
      ch.freeA = d == 1;
      walk(n, {nx, ny}, ch);
      chains.push_back(std::move(ch));
    }
  }
  // closed loops without nodes
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      if (!on(x, y) || visited.at<uint8_t>(y, x) || isNode(x, y)) continue;
      Chain ch;
      ch.loop = true;
      cv::Point prev(x, y), cur(x, y);
      visited.at<uint8_t>(y, x) = 1;
      ch.pts.emplace_back(x, y);
      for (;;) {
        cv::Point next(-1, -1);
        for (int pass = 0; pass < 2 && next.x < 0; ++pass)
          for (int k = 0; k < 8; ++k) {
            if ((k & 1) != pass) continue;
            const int nx = cur.x + kDX[k], ny = cur.y + kDY[k];
            if (!on(nx, ny) || visited.at<uint8_t>(ny, nx)) continue;
            next = {nx, ny};
            break;
          }
        if (next.x < 0) break;
        visited.at<uint8_t>(next.y, next.x) = 1;
        ch.pts.emplace_back(next.x, next.y);
        prev = cur;
        cur = next;
      }
      if (ch.pts.size() >= 8) {
        ch.pts.push_back(ch.pts.front());
        chains.push_back(std::move(ch));
      }
    }
  // spur pruning: short junction-to-free-end chains (arrowhead base corners, thinning artefacts at thick corners)
  std::vector<Chain> kept;
  for (auto& ch : chains) {
    double len = 0;
    for (std::size_t i = 1; i < ch.pts.size(); ++i) len += norm2(ch.pts[i] - ch.pts[i - 1]);
    const bool spur = !ch.loop && (ch.freeA != ch.freeB);
    if (spur) {
      const cv::Point2d j = ch.freeA ? ch.pts.back() : ch.pts.front();
      const int jx = static_cast<int>(j.x), jy = static_cast<int>(j.y);
      const double wj = 2.0 * dist.at<float>(jy, jx);
      if (len < std::max(4.0, 1.6 * wj)) continue;
    }
    if (!ch.loop && ch.freeA && ch.freeB && len < 2) continue;
    kept.push_back(std::move(ch));
  }
  chains.swap(kept);
  // re-join chains through junction clusters that have exactly two incident chain ends left (spur removed)
  cv::Mat jmask = cv::Mat::zeros(skel.size(), CV_8U);
  for (const auto& n : nodes)
    if (deg.at<uint8_t>(n.y, n.x) >= 3) jmask.at<uint8_t>(n.y, n.x) = 255;
  cv::Mat jl;
  cv::connectedComponents(jmask, jl, 8, CV_32S);
  auto clusterOf = [&](const cv::Point2d& q) {
    const int x = static_cast<int>(q.x), y = static_cast<int>(q.y);
    return jmask.at<uint8_t>(y, x) ? jl.at<int>(y, x) : 0;
  };
  for (int pass = 0; pass < 50; ++pass) {
    std::map<int, std::vector<std::pair<int, int>>> inc;   // cluster -> (chain, end)
    for (int i = 0; i < static_cast<int>(chains.size()); ++i) {
      const Chain& ch = chains[i];
      if (ch.loop || ch.pts.empty()) continue;
      if (!ch.freeA) if (int c = clusterOf(ch.pts.front())) inc[c].push_back({i, 0});
      if (!ch.freeB) if (int c = clusterOf(ch.pts.back())) inc[c].push_back({i, 1});
    }
    bool any = false;
    std::vector<uint8_t> touched(chains.size(), 0);
    for (auto& [c, v] : inc) {
      if (v.size() != 2) continue;
      auto [i, ei] = v[0];
      auto [j, ej] = v[1];
      if (touched[i] || touched[j]) continue;
      if (i == j) {   // both ends of one chain meet at the cluster -> closed loop
        chains[i].loop = true;
        chains[i].pts.push_back(chains[i].pts.front());
        touched[i] = 1;
        any = true;
        continue;
      }
      Chain& A = chains[i];
      Chain& B = chains[j];
      if (ei == 0) { std::reverse(A.pts.begin(), A.pts.end()); std::swap(A.freeA, A.freeB); }
      if (ej == 1) { std::reverse(B.pts.begin(), B.pts.end()); std::swap(B.freeA, B.freeB); }
      A.pts.insert(A.pts.end(), B.pts.begin() + 1, B.pts.end());
      A.freeB = B.freeB;
      B.pts.clear();
      touched[i] = touched[j] = 1;
      any = true;
    }
    chains.erase(std::remove_if(chains.begin(), chains.end(), [](const Chain& c) { return c.pts.empty(); }), chains.end());
    if (!any) break;
  }
}

void Vectorizer::fitRange(const std::vector<cv::Point2d>& pts, std::size_t i0, std::size_t i1, bool freeA, bool freeB, int depth) {
  if (i1 <= i0) return;
  const double w = localWidth(pts, i0, i1);
  const double tol = tolBase + 0.15 * w;
  const LineFit lf = fitLine(pts, i0, i1);
  const double chord = norm2(pts[i1] - pts[i0]);
  if (lf.maxDev <= tol && chord > 0.5) {
    Seg s;
    auto proj = [&](const cv::Point2d& q) { const double t = (q - lf.c).dot(lf.dir); return lf.c + lf.dir * t; };
    s.a = proj(pts[i0]);
    s.b = proj(pts[i1]);
    s.width = w;
    s.freeA = freeA;
    s.freeB = freeB;
    out.lines.push_back(s);
    if (seq_) seq_->push_back({false, static_cast<int>(out.lines.size()) - 1});
    return;
  }
  const std::size_t n = i1 - i0 + 1;
  if (n >= 8) {
    const CircFit cf = fitCircle(pts, i0, i1);
    if (cf.ok && cf.maxDev <= tol * 1.3 && cf.rms <= tol * 0.6 && cf.r >= std::max(4.0, 1.5 * w) && cf.r < 3.0 * std::max(P.W, P.H)) {
      // sweep (unwrapped along the chain)
      double sweep = 0, prev = std::atan2(pts[i0].y - cf.c.y, pts[i0].x - cf.c.x);
      for (std::size_t i = i0 + 1; i <= i1; ++i) {
        const double a = std::atan2(pts[i].y - cf.c.y, pts[i].x - cf.c.x);
        double d = a - prev;
        while (d > CV_PI) d -= 2 * CV_PI;
        while (d < -CV_PI) d += 2 * CV_PI;
        sweep += d;
        prev = a;
      }
      const double sweepDeg = std::abs(sweep) * 180 / CV_PI;
      if (sweepDeg >= 20) {
        ArcF a;
        a.c = cf.c;
        a.r = cf.r;
        a.width = w;
        a.rms = cf.rms;
        if (sweepDeg >= 345) {
          a.full = true;
          a.a0 = 0;
          a.a1 = 360;
        } else {
          // sheet angles: image sweep > 0 means clockwise on the sheet
          double s0 = sheetAngleDeg(cf.c, pts[i0]), s1 = sheetAngleDeg(cf.c, pts[i1]);
          if (sweep > 0) std::swap(s0, s1);   // CCW on sheet from s0 to s1
          // extend free ends by half the stroke width
          const double ext = (w / 2) / cf.r * 180 / CV_PI;
          const bool freeStart = sweep > 0 ? freeB : freeA, freeEnd = sweep > 0 ? freeA : freeB;
          if (freeStart) s0 -= ext;
          if (freeEnd) s1 += ext;
          while (s1 <= s0) s1 += 360;
          a.a0 = std::fmod(s0 + 720, 360.0);
          a.a1 = a.a0 + (s1 - s0);
        }
        a.pts.assign(pts.begin() + static_cast<long>(i0), pts.begin() + static_cast<long>(i1) + 1);
        out.arcs.push_back(a);
        if (seq_) seq_->push_back({true, static_cast<int>(out.arcs.size()) - 1});
        return;
      }
    }
  }
  if (depth > 40 || n < 3) return;
  // split at the max deviation from the chord (closed chains: farthest point from the start)
  std::size_t split = i0 + n / 2;
  double best = -1;
  const cv::Point2d A = pts[i0], B = pts[i1];
  const cv::Point2d d = B - A;
  const double L = norm2(d);
  for (std::size_t i = i0 + 1; i < i1; ++i) {
    const double dv = L > 1 ? std::abs((pts[i] - A).cross(d)) / L : norm2(pts[i] - A);
    if (dv > best) { best = dv; split = i; }
  }
  fitRange(pts, i0, split, freeA, false, depth + 1);
  fitRange(pts, split, i1, false, freeB, depth + 1);
}

void Vectorizer::fitChain(const Chain& ch) {
  if (ch.pts.size() < 2) return;
  const std::size_t before = out.lines.size();
  std::vector<std::pair<bool, int>> seq;
  seq_ = &seq;
  fitRange(ch.pts, 0, ch.pts.size() - 1, ch.freeA, ch.freeB, 0);
  seq_ = nullptr;
  refineTangents(seq);
  // extend free line ends by half the stroke width (thinning eats w/2 at line ends)
  for (std::size_t i = before; i < out.lines.size(); ++i) {
    Seg& s = out.lines[i];
    const double L = s.length();
    if (L < 1e-9) continue;
    const cv::Point2d u = (s.b - s.a) * (1.0 / L);
    if (s.freeA) s.a -= u * (s.width / 2);
    if (s.freeB) s.b += u * (s.width / 2);
  }
}

// Line - (short lines on the circle) - arc sequences of one chain: move the break to the tangent point (fillets).
void Vectorizer::refineTangents(const std::vector<std::pair<bool, int>>& seq) {
  for (std::size_t k = 0; k < seq.size(); ++k) {
    if (!seq[k].first) continue;
    ArcF& A = out.arcs[seq[k].second];
    if (A.full) continue;
    const double tol = std::max(2.0, 0.5 * A.width + 1);
    auto onCircle = [&](const cv::Point2d& q) { return std::abs(norm2(q - A.c) - A.r) <= tol; };
    for (int dir = -1; dir <= 1; dir += 2) {
      std::vector<int> onC;
      int lineIdx = -1;
      for (long m = static_cast<long>(k) + dir; m >= 0 && m < static_cast<long>(seq.size()); m += dir) {
        if (seq[m].first) break;
        Seg& L = out.lines[seq[m].second];
        if (L.length() < 0.6 * A.r && onCircle(L.a) && onCircle(L.b)) { onC.push_back(seq[m].second); continue; }
        lineIdx = seq[m].second;
        break;
      }
      if (lineIdx < 0) continue;
      Seg& L = out.lines[lineIdx];
      const double len = L.length();
      if (len < 1e-6) continue;
      const cv::Point2d u = (L.b - L.a) * (1.0 / len);
      const double t = (A.c - L.a).dot(u);
      const cv::Point2d T = L.a + u * t;
      const double d = norm2(A.c - T);
      if (std::abs(d - A.r) > std::max(2.5, 0.05 * A.r)) continue;   // not tangent
      if (t < -tol || t > len + tol) continue;
      // which ends are shared
      const cv::Point2d e0(A.c.x + A.r * std::cos(A.a0 * CV_PI / 180), A.c.y - A.r * std::sin(A.a0 * CV_PI / 180));
      const cv::Point2d e1(A.c.x + A.r * std::cos(A.a1 * CV_PI / 180), A.c.y - A.r * std::sin(A.a1 * CV_PI / 180));
      const bool lineEndB = std::min(norm2(L.b - e0), norm2(L.b - e1)) < std::min(norm2(L.a - e0), norm2(L.a - e1));
      cv::Point2d& le = lineEndB ? L.b : L.a;
      const bool arcEnd1 = norm2(le - e1) < norm2(le - e0);
      if (norm2(le - (arcEnd1 ? e1 : e0)) > 0.8 * A.r + 10) continue;
      le = T;
      double ang = sheetAngleDeg(A.c, T);
      if (arcEnd1) {
        while (ang < A.a1 - 180) ang += 360;
        while (ang > A.a1 + 180) ang -= 360;
        if (ang > A.a0 && ang - A.a0 < 360) A.a1 = ang;
      } else {
        while (ang < A.a0 - 180) ang += 360;
        while (ang > A.a0 + 180) ang -= 360;
        if (ang < A.a1 && A.a1 - ang < 360) {
          const double span = A.a1 - ang;
          A.a0 = std::fmod(ang + 720, 360.0);
          A.a1 = A.a0 + span;
        }
      }
      for (int i : onC) out.lines[i].dead = true;
    }
  }
}

// Arrowhead-like widening of the ink around junction j along direction u (both sides)?
bool Vectorizer::arrowNear(const cv::Point2d& j, const cv::Point2d& u, double w) const {
  const cv::Point2d nv(-u.y, u.x);
  const double cap = P.px(1.6);
  for (int side = -1; side <= 1; side += 2)
    for (double t = std::max(3.0, w + 1); t <= P.px(2.5); t += 1.0) {
      const cv::Point2d c = j + u * (t * side);
      int run = 1;
      bool capped = false;
      for (int sg = -1; sg <= 1; sg += 2)
        for (double k = 1; k <= cap; k += 1) {
          const cv::Point2d q = c + nv * (k * sg);
          if (!ink(int(std::lround(q.x)), int(std::lround(q.y)))) break;
          ++run;
          if (k >= cap - 1) capped = true;
        }
      if (!capped && run >= std::max(2.2 * w + 1, P.px(0.45))) return true;
    }
  return false;
}

double Vectorizer::support(const cv::Point2d& a, const cv::Point2d& b, double w) const {
  const double L = norm2(b - a);
  if (L < 1) return ink(int(std::lround(a.x)), int(std::lround(a.y))) ? 1 : 0;
  const int n = std::max(2, static_cast<int>(L / 2));
  const int r = std::max(1, static_cast<int>(std::ceil(w / 2)));
  int hit = 0;
  for (int i = 0; i <= n; ++i) {
    const cv::Point2d q = a + (b - a) * (static_cast<double>(i) / n);
    const int x = static_cast<int>(std::lround(q.x)), y = static_cast<int>(std::lround(q.y));
    bool h = false;
    for (int dy = -r; dy <= r && !h; ++dy)
      for (int dx = -r; dx <= r && !h; ++dx) h = ink(x + dx, y + dy);
    hit += h;
  }
  return static_cast<double>(hit) / (n + 1);
}

void Vectorizer::mergeLines() {
  auto& L = out.lines;
  for (int pass = 0; pass < 6; ++pass) {
    bool any = false;
    std::vector<int> idx;
    for (int i = 0; i < static_cast<int>(L.size()); ++i) if (!L[i].dead && L[i].pieces == 1) idx.push_back(i);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return dirDeg(L[a].b - L[a].a) < dirDeg(L[b].b - L[b].a); });
    for (std::size_t ii = 0; ii < idx.size(); ++ii) {
      Seg& A = L[idx[ii]];
      if (A.dead) continue;
      for (std::size_t jj = 0; jj < idx.size(); ++jj) {
        if (jj == ii) continue;
        Seg& B = L[idx[jj]];
        if (B.dead) continue;
        const double la = A.length(), lb = B.length();
        if (la < 1e-6 || lb < 1e-6) continue;
        const double tolAng = std::max(1.5, std::atan2(2.0, std::min(la, lb)) * 180 / CV_PI);
        if (angDiff(dirDeg(A.b - A.a), dirDeg(B.b - B.a)) > tolAng) continue;
        const double wmax = std::max(A.width, B.width), wmin = std::min(A.width, B.width);
        if (wmax > 2.2 * wmin + 1) continue;
        const Seg& Lg = la >= lb ? A : B;
        const cv::Point2d u = (Lg.b - Lg.a) * (1.0 / Lg.length());
        const cv::Point2d nrm(-u.y, u.x);
        const double perpTol = std::max(1.5, 0.45 * wmax);
        const Seg& Sm = la >= lb ? B : A;
        if (std::abs((Sm.a - Lg.a).dot(nrm)) > perpTol || std::abs((Sm.b - Lg.a).dot(nrm)) > perpTol) continue;
        double ta0 = (A.a - Lg.a).dot(u), ta1 = (A.b - Lg.a).dot(u), tb0 = (B.a - Lg.a).dot(u), tb1 = (B.b - Lg.a).dot(u);
        const double aLo = std::min(ta0, ta1), aHi = std::max(ta0, ta1), bLo = std::min(tb0, tb1), bHi = std::max(tb0, tb1);
        const double gap = std::max(bLo - aHi, aLo - bHi);
        if (gap > std::max(2.5, 1.2 * wmax)) continue;
        if (wmax <= 1.5 * out.thinW + 0.5 && gap > -3) {
          // chained dimensions: two dimension lines meet at an extension line with arrowheads -> keep separate
          const double jt = aHi < bLo ? (aHi + bLo) / 2 : (bHi + aLo) / 2;
          if (arrowNear(Lg.a + u * jt, u, wmax)) continue;
        }
        // merge B into A
        struct E { double t; cv::Point2d p; bool fr; };
        E es[4] = {{ta0, A.a, A.freeA}, {ta1, A.b, A.freeB}, {tb0, B.a, B.freeA}, {tb1, B.b, B.freeB}};
        const E* lo = &es[0];
        const E* hi = &es[0];
        for (const E& e : es) { if (e.t < lo->t) lo = &e; if (e.t > hi->t) hi = &e; }
        const cv::Point2d base = Lg.a;
        const double w = (A.width * la + B.width * lb) / (la + lb);
        const cv::Point2d na = base + u * lo->t + nrm * ((lo->p - base).dot(nrm) * 0.5);
        const cv::Point2d nb = base + u * hi->t + nrm * ((hi->p - base).dot(nrm) * 0.5);
        const bool fa = lo->fr, fb = hi->fr;
        A.a = na; A.b = nb; A.freeA = fa; A.freeB = fb; A.width = w;
        B.dead = true;
        any = true;
      }
    }
    if (!any) break;
  }
  L.erase(std::remove_if(L.begin(), L.end(), [](const Seg& s) { return s.dead; }), L.end());
}

void Vectorizer::mergeArcs() {
  auto& A = out.arcs;
  for (int pass = 0; pass < 6; ++pass) {
    bool any = false;
    for (std::size_t i = 0; i < A.size(); ++i) {
      if (A[i].dead) continue;
      for (std::size_t j = 0; j < A.size(); ++j) {
        if (i == j || A[j].dead) continue;
        ArcF& a = A[i];
        ArcF& b = A[j];
        const double r = std::max(a.r, b.r);
        const double tc = std::max(4.0, 0.08 * r), tr = std::max(3.5, 0.07 * r);
        if (norm2(a.c - b.c) > tc || std::abs(a.r - b.r) > tr) continue;
        if (std::max(a.width, b.width) > 2.2 * std::min(a.width, b.width) + 1) continue;
        const double wa = a.full ? 360 : a.a1 - a.a0, wb = b.full ? 360 : b.a1 - b.a0;
        const double gapTol = std::max(12.0, (3 * std::max(a.width, b.width) / r) * 180 / CV_PI);
        bool merged = false;
        double n0 = a.a0, n1 = a.a1;
        if (a.full || b.full) { merged = true; n0 = 0; n1 = 360; }
        else {
          // try b placed after a (b.a0 + k*360 near a.a1) or before a
          for (int k = -1; k <= 1 && !merged; ++k) {
            const double b0 = b.a0 + 360 * k, b1 = b.a1 + 360 * k;
            if (b0 <= a.a1 + gapTol && b1 >= a.a0 - gapTol) {
              n0 = std::min(a.a0, b0);
              n1 = std::max(a.a1, b1);
              merged = true;
            }
          }
        }
        if (!merged) continue;
        const double ws = wa + wb;
        a.c = (a.c * wa + b.c * wb) * (1.0 / ws);
        a.r = (a.r * wa + b.r * wb) / ws;
        if (!a.pts.empty() && !b.pts.empty()) {
          a.pts.insert(a.pts.end(), b.pts.begin(), b.pts.end());
          const CircFit cf = fitCircle(a.pts, 0, a.pts.size() - 1);
          if (cf.ok && cf.rms < 3.0) { a.c = cf.c; a.r = cf.r; }
        } else if (a.pts.empty()) {
          a.pts = b.pts;
        }
        a.width = (a.width * wa + b.width * wb) / ws;
        a.rms = std::max(a.rms, b.rms);
        if (n1 - n0 >= 330) { a.full = true; a.a0 = 0; a.a1 = 360; }
        else { a.a0 = std::fmod(n0 + 720, 360.0); a.a1 = a.a0 + (n1 - n0); }
        b.dead = true;
        any = true;
      }
    }
    if (!any) break;
  }
  A.erase(std::remove_if(A.begin(), A.end(), [](const ArcF& a) { return a.dead; }), A.end());
}

// Short lines lying on a found circle/arc (fragments between junctions) are absorbed.
void Vectorizer::absorbIntoCircles() {
  for (auto& s : out.lines) {
    for (const auto& a : out.arcs) {
      const double tol = std::max(2.5, 0.6 * a.width + 1);
      auto on = [&](const cv::Point2d& q) { return std::abs(norm2(q - a.c) - a.r) <= tol; };
      const cv::Point2d m = (s.a + s.b) * 0.5;
      if (s.length() < 0.5 * a.r + 6 && on(s.a) && on(s.b) && on(m)) {
        if (!a.full) {
          auto inArc = [&](const cv::Point2d& q) {
            double t = sheetAngleDeg(a.c, q);
            while (t < a.a0 - 5) t += 360;
            return t <= a.a1 + 5;
          };
          if (!inArc(s.a) || !inArc(s.b)) continue;
        }
        s.dead = true;
        break;
      }
    }
  }
  out.lines.erase(std::remove_if(out.lines.begin(), out.lines.end(), [](const Seg& s) { return s.dead; }), out.lines.end());
}

void Vectorizer::houghCircles() {
  cv::Mat small;
  const double f = 0.5;
  cv::resize(G, small, cv::Size(), f, f, cv::INTER_AREA);
  cv::Mat inv = 255 - small;
  cv::GaussianBlur(inv, inv, cv::Size(5, 5), 1.2);
  std::vector<cv::Vec3f> cs;
  const int minR = static_cast<int>(std::max(4.0, P.px(1.5) * f));
  const int maxR = static_cast<int>(std::min(G.cols, G.rows) * 0.45 * f);
  try {
    if (cvc::houghCircleMethod() == cv::HOUGH_GRADIENT) cv::HoughCircles(inv, cs, cv::HOUGH_GRADIENT, 1, minR * 2.0, 120, 40, minR, maxR);
    else cv::HoughCircles(inv, cs, cvc::houghCircleMethod(), 1.5, minR * 2.0, 300, 0.8, minR, maxR);
  } catch (const cv::Exception&) {
    return;
  }
  for (const auto& c : cs) {
    const cv::Point2d cc(c[0] / f, c[1] / f);
    const double r = c[2] / f;
    bool dup = false;
    for (const auto& a : out.arcs)
      if (norm2(a.c - cc) < std::max(4.0, 0.06 * r) && std::abs(a.r - r) < std::max(4.0, 0.06 * r)) { dup = true; break; }
    if (dup) continue;
    // verification: ink on the circumference, no ink just inside / outside (not a filled blob / text cluster)
    const int N = std::max(90, static_cast<int>(2 * CV_PI * r / 2));
    int on = 0, offIn = 0, offOut = 0;
    const double band = 2.0;
    const double off = std::max(5.0, out.thickW + 3);
    for (int i = 0; i < N; ++i) {
      const double t = 2 * CV_PI * i / N, ct = std::cos(t), st = std::sin(t);
      bool h = false;
      for (double dr = -band; dr <= band && !h; dr += 1) h = ink(int(std::lround(cc.x + (r + dr) * ct)), int(std::lround(cc.y + (r + dr) * st)));
      on += h;
      offIn += ink(int(std::lround(cc.x + (r - off) * ct)), int(std::lround(cc.y + (r - off) * st)));
      offOut += ink(int(std::lround(cc.x + (r + off) * ct)), int(std::lround(cc.y + (r + off) * st)));
    }
    if (on < 0.85 * N || offIn > 0.35 * N || offOut > 0.35 * N) continue;
    ArcF a;
    a.c = cc;
    a.r = r;
    a.full = true;
    a.width = out.thinW;
    a.support = static_cast<double>(on) / N;
    a.ref = "hough";
    out.arcs.push_back(a);
  }
}

void Vectorizer::groupDashes() {
  auto& L = out.lines;
  const double maxPiece = P.px(15), maxGap = P.px(4.5);
  std::vector<int> cand;
  for (int i = 0; i < static_cast<int>(L.size()); ++i)
    if (!L[i].dead && L[i].length() <= maxPiece) cand.push_back(i);
  const int n = static_cast<int>(cand.size());
  std::vector<int> succ(n, -1), pred(n, -1);
  std::vector<double> succGap(n, 1e18);
  for (int ii = 0; ii < n; ++ii) {
    const Seg& A = L[cand[ii]];
    const double la = A.length();
    if (la < 1e-6) continue;
    for (int jj = 0; jj < n; ++jj) {
      if (ii == jj) continue;
      const Seg& B = L[cand[jj]];
      const double lb = B.length();
      if (lb < 1e-6) continue;
      // direction: use the longer piece
      const Seg& R = la >= lb ? A : B;
      const cv::Point2d u0 = (R.b - R.a) * (1.0 / R.length());
      if (angDiff(dirDeg(A.b - A.a), dirDeg(B.b - B.a)) > std::max(3.0, std::atan2(2.0, std::min(la, lb)) * 180 / CV_PI)) continue;
      const cv::Point2d nrm(-u0.y, u0.x);
      const double perpTol = std::max(2.0, 0.7 * std::max(A.width, B.width));
      const cv::Point2d mb = (B.a + B.b) * 0.5, ma = (A.a + A.b) * 0.5;
      if (std::abs((mb - A.a).dot(nrm)) > perpTol || std::abs((ma - B.a).dot(nrm)) > perpTol) continue;
      // forward along u0 (canonical orientation: +x, or +y for vertical)
      cv::Point2d u = u0;
      if (u.x < -1e-9 || (std::abs(u.x) < 1e-9 && u.y < 0)) u = -u;
      const double a0 = std::min((A.a - ma).dot(u), (A.b - ma).dot(u)), a1 = std::max((A.a - ma).dot(u), (A.b - ma).dot(u));
      const double b0 = std::min((B.a - ma).dot(u), (B.b - ma).dot(u));
      (void)a0;
      const double gap = b0 - a1;
      if (gap < -1.5 || gap > maxGap) continue;
      if ((B.a - ma).dot(u) + (B.b - ma).dot(u) <= 0) continue;   // B must lie ahead of A
      if (gap < succGap[ii]) { succGap[ii] = gap; succ[ii] = jj; }
    }
  }
  // resolve: each node keeps one predecessor (the one with smallest gap)
  std::vector<double> predGap(n, 1e18);
  for (int i = 0; i < n; ++i)
    if (succ[i] >= 0 && succGap[i] < predGap[succ[i]]) { predGap[succ[i]] = succGap[i]; pred[succ[i]] = i; }
  for (int i = 0; i < n; ++i)
    if (succ[i] >= 0 && pred[succ[i]] != i) succ[i] = -1;
  std::vector<uint8_t> seen(n, 0);
  for (int s = 0; s < n; ++s) {
    if (pred[s] >= 0 || seen[s]) continue;
    std::vector<int> ch;
    for (int k = s; k >= 0 && !seen[k]; k = succ[k]) { seen[k] = 1; ch.push_back(k); }
    if (ch.size() < 2) continue;
    if (ch.size() == 2) {   // short centre marks: only thin lines with a real gap
      const Seg& s0 = L[cand[ch[0]]];
      const Seg& s1 = L[cand[ch[1]]];
      if (std::max(s0.width, s1.width) > 1.5 * out.thinW + 0.5) continue;
      if (succGap[ch[0]] < P.px(0.4)) continue;
    }
    // pattern
    std::vector<double> lens, gaps;
    cv::Point2d u = L[cand[ch.front()]].b - L[cand[ch.front()]].a;
    {
      cv::Point2d tot(0, 0);
      for (int k : ch) {
        cv::Point2d d = L[cand[k]].b - L[cand[k]].a;
        if (d.dot(u) < 0) d = -d;
        tot += d;
      }
      u = tot * (1.0 / norm2(tot));
    }
    if (u.x < -1e-9 || (std::abs(u.x) < 1e-9 && u.y < 0)) u = -u;
    const cv::Point2d o = L[cand[ch.front()]].a;
    double tMin = 1e18, tMax = -1e18;
    double wsum = 0, lsum = 0, perpSum = 0;
    std::vector<std::pair<double, double>> iv;
    for (int k : ch) {
      const Seg& sg = L[cand[k]];
      const double t0 = (sg.a - o).dot(u), t1 = (sg.b - o).dot(u);
      iv.emplace_back(std::min(t0, t1), std::max(t0, t1));
      tMin = std::min(tMin, std::min(t0, t1));
      tMax = std::max(tMax, std::max(t0, t1));
      const double l = sg.length();
      lens.push_back(l);
      wsum += sg.width * l;
      lsum += l;
      perpSum += ((sg.a + sg.b) * 0.5 - o).dot(cv::Point2d(-u.y, u.x)) * l;
    }
    std::sort(iv.begin(), iv.end());
    for (std::size_t k = 1; k < iv.size(); ++k) gaps.push_back(std::max(0.0, iv[k].first - iv[k - 1].second));
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
    const double mg = median(gaps);
    if (mg < 1.0) continue;   // touching pieces: not a dashed line
    std::vector<double> sorted = lens;
    std::sort(sorted.begin(), sorted.end());
    const double lMax = sorted.back();
    const double lMin = sorted.front();
    Seg d;
    const cv::Point2d nrm(-u.y, u.x);
    const double off = perpSum / lsum;
    d.a = o + u * tMin + nrm * off;
    d.b = o + u * tMax + nrm * off;
    d.width = wsum / lsum;
    d.pieces = static_cast<int>(ch.size());
    d.freeA = d.freeB = true;
    // centre line: long and short dashes alternate (short < 0.45 long)
    std::vector<double> longs, shorts;
    const double split = (lMax + lMin) / 2;
    for (double l : lens) (l < split ? shorts : longs).push_back(l);
    if (lMax > 2.5 * lMin && shorts.size() >= 1 && longs.size() >= 2 && median(shorts) < 0.45 * median(longs)) {
      d.lineType = "center";
      d.dashPx = {median(longs), mg, median(shorts), mg};
    } else {
      d.lineType = "hidden";
      d.dashPx = {median(lens), mg};
    }
    d.ref = "dash x" + std::to_string(ch.size());
    for (int k : ch) L[cand[k]].dead = true;
    L.push_back(d);
  }
  L.erase(std::remove_if(L.begin(), L.end(), [](const Seg& s) { return s.dead; }), L.end());
}

void Vectorizer::snapCorners() {
  auto& L = out.lines;
  for (std::size_t i = 0; i < L.size(); ++i)
    for (std::size_t j = i + 1; j < L.size(); ++j) {
      Seg& A = L[i];
      Seg& B = L[j];
      if (A.pieces > 1 || B.pieces > 1) continue;
      if (angDiff(dirDeg(A.b - A.a), dirDeg(B.b - B.a)) < 20) continue;
      const double snap = std::max(3.0, 1.2 * std::max(A.width, B.width));
      cv::Point2d* ea[2] = {&A.a, &A.b};
      cv::Point2d* eb[2] = {&B.a, &B.b};
      for (auto* pa : ea)
        for (auto* pb : eb) {
          if (norm2(*pa - *pb) > snap) continue;
          // intersection of the infinite lines
          const cv::Point2d r = A.b - A.a, s = B.b - B.a;
          const double den = r.cross(s);
          if (std::abs(den) < 1e-9) continue;
          const double t = (B.a - A.a).cross(s) / den;
          const cv::Point2d X = A.a + r * t;
          if (norm2(X - *pa) <= 2 * snap && norm2(X - *pb) <= 2 * snap) { *pa = X; *pb = X; }
        }
    }
}

void Vectorizer::detectArrows() {
  const double capRun = P.px(1.6);
  for (auto& s : out.lines) {
    if (s.pieces > 1) continue;
    const double L = s.length();
    if (L < P.px(2.0)) continue;
    for (int end = 0; end < 2; ++end) {
      const cv::Point2d E = end == 0 ? s.a : s.b;
      const cv::Point2d u = ((end == 0 ? s.b : s.a) - E) * (1.0 / L);   // inward
      const cv::Point2d nv(-u.y, u.x);
      const double lw = std::max(1.0, s.width);
      const double tMax = std::min(P.px(7.0), 0.7 * L);
      std::vector<double> ts, ws;
      for (double t = std::max(2.0, lw); t <= tMax; t += 1.0) {
        const cv::Point2d c = E + u * t;
        if (!ink(int(std::lround(c.x)), int(std::lround(c.y)))) { ts.push_back(t); ws.push_back(0); continue; }
        int up = 0, dn = 0;
        bool capped = false;
        for (double k = 1; k <= capRun; k += 1) { const cv::Point2d q = c + nv * k; if (!ink(int(std::lround(q.x)), int(std::lround(q.y)))) break; ++up; if (k >= capRun - 1) capped = true; }
        for (double k = 1; k <= capRun; k += 1) { const cv::Point2d q = c - nv * k; if (!ink(int(std::lround(q.x)), int(std::lround(q.y)))) break; ++dn; if (k >= capRun - 1) capped = true; }
        ts.push_back(t);
        ws.push_back(capped ? -1 : 1 + up + dn);
      }
      if (ws.size() < 6) continue;
      if (std::getenv("PC_WP2_ARROWDBG") && std::abs(E.x - std::atof(std::getenv("PC_WP2_ARROWDBG"))) < 3) {
        std::fprintf(stderr, "arrowdbg E(%.1f,%.1f) lw %.2f:", E.x, E.y, lw);
        for (std::size_t i = 0; i < ws.size(); ++i) std::fprintf(stderr, " %.0f", ws[i]);
        std::fprintf(stderr, "\n");
      }
      // find the base: the maximum width before the profile falls back to the line width
      int iMax = -1;
      double wMax = 0;
      for (std::size_t i = 0; i < ws.size(); ++i) {
        if (ws[i] < 0) continue;
        if (ws[i] > wMax) { wMax = ws[i]; iMax = static_cast<int>(i); }
        if (iMax >= 0 && ws[i] >= 0 && ws[i] <= 1.6 * lw + 1 && wMax >= 2.2 * lw) break;
      }
      if (iMax < 0 || wMax < std::max(2.2 * lw, P.px(0.45))) continue;
      // plateau at the base (anti-aliased base edge), then the profile must drop back to the line width
      std::size_t iEnd = static_cast<std::size_t>(iMax);
      while (iEnd + 1 < ws.size() && ws[iEnd + 1] >= 0.8 * wMax && ts[iEnd + 1] - ts[iMax] < 0.25 * ts[iMax] + 2) ++iEnd;
      bool drop = false;
      for (std::size_t i = iEnd + 1; i < ws.size() && i <= iEnd + 4; ++i)
        if (ws[i] >= 0 && ws[i] <= 1.6 * lw + 1) drop = true;
      if (!drop) continue;
      const double tb = ts[iEnd];
      // taper: half-way width between 0.2 and 0.8 of max, and the near-tip width small
      double wHalf = -1, wTip = -1;
      for (std::size_t i = 0; i < ws.size(); ++i) {
        if (ws[i] < 0) continue;
        if (wTip < 0 && ts[i] <= tb * 0.35) wTip = ws[i];
        if (std::abs(ts[i] - tb / 2) <= 1.0) wHalf = ws[i];
      }
      if (wHalf < 0 || wHalf < 0.2 * wMax || wHalf > 0.85 * wMax) continue;
      if (wTip >= 0 && wTip > 0.75 * wMax) continue;
      const double len = tb;
      if (len < P.px(1.0) || len > P.px(8.0)) continue;
      const double ratio = wMax / len;
      if (ratio < 0.12 || ratio > 0.9) continue;
      // filled? sample the triangle interior
      int in = 0, tot = 0;
      for (double t = tb * 0.3; t <= tb * 0.9; t += 1.0) {
        const double half = 0.5 * wMax * (t / tb) * 0.7;
        for (double k = -half; k <= half; k += 1.0) {
          const cv::Point2d q = E + u * t + nv * k;
          in += ink(int(std::lround(q.x)), int(std::lround(q.y)));
          ++tot;
        }
      }
      Arrow a;
      a.tip = E;
      a.base = E + u * tb;
      a.width = wMax;
      a.filled = tot == 0 || in >= 0.8 * tot;
      const double shape = 1.0 - std::abs(wHalf / wMax - 0.5);
      a.conf = static_cast<float>(std::clamp(0.55 + 0.3 * shape, 0.5, 0.85));
      out.arrows.push_back(a);
      (end == 0 ? s.arrowA : s.arrowB) = true;
    }
  }
}

void Vectorizer::finish() {
  for (auto& s : out.lines) s.support = support(s.a, s.b, s.width);
  for (auto& a : out.arcs) {
    if (a.ref == "hough") continue;
    const int N = 72;
    int on = 0, tot = 0;
    const double span = a.full ? 360 : a.a1 - a.a0;
    for (int i = 0; i <= N; ++i) {
      const double t = (a.a0 + span * i / N) * CV_PI / 180;
      const cv::Point2d q(a.c.x + a.r * std::cos(t), a.c.y - a.r * std::sin(t));
      bool h = false;
      for (int dy = -2; dy <= 2 && !h; ++dy)
        for (int dx = -2; dx <= 2 && !h; ++dx) h = ink(int(std::lround(q.x)) + dx, int(std::lround(q.y)) + dy);
      on += h;
      ++tot;
    }
    a.support = static_cast<double>(on) / tot;
    if (!a.full && span >= 240) {
      int onF = 0;
      for (int i = 0; i < 180; ++i) {
        const double t = 2 * CV_PI * i / 180;
        const cv::Point2d q(a.c.x + a.r * std::cos(t), a.c.y - a.r * std::sin(t));
        bool h = false;
        for (int dy = -2; dy <= 2 && !h; ++dy)
          for (int dx = -2; dx <= 2 && !h; ++dx) h = ink(int(std::lround(q.x)) + dx, int(std::lround(q.y)) + dy);
        onF += h;
      }
      if (onF >= 0.92 * 180) { a.full = true; a.a0 = 0; a.a1 = 360; }
    }
  }
  // drop degenerate leftovers (tiny fragments that are not dashes / dots)
  out.lines.erase(std::remove_if(out.lines.begin(), out.lines.end(),
                                 [&](const Seg& s) { return s.length() < std::max(3.0, 0.8 * s.width) || s.support < 0.5; }),
                  out.lines.end());
  out.arcs.erase(std::remove_if(out.arcs.begin(), out.arcs.end(), [](const ArcF& a) { return a.support < 0.6; }), out.arcs.end());
  // co-circular partial arcs covering >= 150 deg whose full circumference is inked -> one full circle
  // (a circle cut by dimension lines / arrowheads into several arcs)
  auto fullInk = [&](const ArcF& a) {
    int onF = 0;
    for (int i = 0; i < 180; ++i) {
      const double t = 2 * CV_PI * i / 180;
      const cv::Point2d q(a.c.x + a.r * std::cos(t), a.c.y - a.r * std::sin(t));
      bool h = false;
      for (int dy = -2; dy <= 2 && !h; ++dy)
        for (int dx = -2; dx <= 2 && !h; ++dx) h = ink(int(std::lround(q.x)) + dx, int(std::lround(q.y)) + dy);
      onF += h;
    }
    return onF / 180.0;
  };
  for (std::size_t i = 0; i < out.arcs.size(); ++i) {
    ArcF& a = out.arcs[i];
    if (a.full || a.dead) continue;
    std::vector<std::size_t> grp{i};
    double span = a.a1 - a.a0, wsum = span, cx = a.c.x * span, cy = a.c.y * span, rs = a.r * span;
    for (std::size_t j = 0; j < out.arcs.size(); ++j) {
      const ArcF& b = out.arcs[j];
      if (j == i || b.full || b.dead) continue;
      if (std::hypot(a.c.x - b.c.x, a.c.y - b.c.y) > 0.05 * a.r + 2 || std::abs(a.r - b.r) > 0.05 * a.r + 2) continue;
      const double sb = b.a1 - b.a0;
      grp.push_back(j);
      span += sb; wsum += sb; cx += b.c.x * sb; cy += b.c.y * sb; rs += b.r * sb;
    }
    if (grp.size() < 2 || span < 150) continue;
    ArcF c = a;
    c.c = {cx / wsum, cy / wsum};
    c.r = rs / wsum;
    if (fullInk(c) < 0.92) continue;
    c.full = true; c.a0 = 0; c.a1 = 360; c.support = 1;
    for (std::size_t j : grp) out.arcs[j].dead = true;
    a = c;
    a.dead = false;
  }
  out.arcs.erase(std::remove_if(out.arcs.begin(), out.arcs.end(), [](const ArcF& a) { return a.dead; }), out.arcs.end());
  // short arcs hugging a full circle (fits of circle ink + an arrowhead / leader touching it) are not geometry
  std::vector<ArcF> circles;
  for (const auto& a : out.arcs)
    if (a.full) circles.push_back(a);
  out.arcs.erase(std::remove_if(out.arcs.begin(), out.arcs.end(), [&](const ArcF& a) {
                   if (a.full || a.a1 - a.a0 > 60) return false;
                   for (const auto& c : circles) {
                     const double dc = std::hypot(a.c.x - c.c.x, a.c.y - c.c.y);
                     if (dc < 0.15 * c.r + 3 && std::abs(a.r - c.r) < 0.2 * c.r + 2 && std::abs(a.r - c.r) > 0.5 * c.width + 1)
                       return true;
                   }
                   return false;
                 }),
                 out.arcs.end());
}

bool Vectorizer::run(ProgressSink* prog) {
  // stroke width map and skeleton
  cv::Mat padded;
  cv::copyMakeBorder(G, padded, 1, 1, 1, 1, cv::BORDER_CONSTANT, 0);
  cv::distanceTransform(padded, dist, cv::DIST_L2, 3);
  dist = dist(cv::Rect(1, 1, G.cols, G.rows)).clone();
  skel = padded / 255;
  zhangSuen(skel);
  skel = skel(cv::Rect(1, 1, G.cols, G.rows)).clone();
  if (isCancelled(prog)) return false;
  report(prog, 0.35, "İskelet çıkarıldı");
  // stroke width clusters from skeleton samples
  {
    std::vector<float> w;
    for (int y = 0; y < skel.rows; y += 1)
      for (int x = 0; x < skel.cols; x += 1)
        if (skel.at<uint8_t>(y, x)) w.push_back(2 * dist.at<float>(y, x) - 1);
    if (!w.empty()) {
      std::sort(w.begin(), w.end());
      out.thinW = std::max(1.0f, w[w.size() / 5]);
      out.thickW = std::max(out.thinW, static_cast<double>(w[w.size() * 9 / 10]));
    }
  }
  tolBase = P.noisy ? 1.8 : 1.3;
  trace();
  if (isCancelled(prog)) return false;
  for (std::size_t i = 0; i < chains.size(); ++i) {
    fitChain(chains[i]);
    if ((i & 1023) == 0 && isCancelled(prog)) return false;
  }
  out.lines.erase(std::remove_if(out.lines.begin(), out.lines.end(), [](const Seg& s) { return s.dead; }), out.lines.end());
  report(prog, 0.5, "Çizgiler ve yaylar uyduruldu");
  mergeLines();
  mergeArcs();
  absorbIntoCircles();
  houghCircles();
  mergeArcs();
  if (isCancelled(prog)) return false;
  groupDashes();
  mergeLines();
  snapCorners();
  detectArrows();
  finish();
  int k = 0;
  for (auto& s : out.lines) if (s.ref.empty()) s.ref = "skel#" + std::to_string(k++);
  k = 0;
  for (auto& a : out.arcs) if (a.ref.empty()) a.ref = "skel-arc#" + std::to_string(k++); else a.ref += "#" + std::to_string(k++);
  return true;
}

}  // namespace

bool vectorize(const Prep& p, const cv::Mat& geomBin, Geometry& g, ProgressSink* progress) {
  Vectorizer v(p, geomBin, g);
  return v.run(progress);
}

}  // namespace pc::drawing::raster
