// WP-2: text / graphics separation by connected-component statistics. Character candidates (size between ~1.2 and
// ~6.5 mm, not a thin dash) are linked into horizontal text lines (vertical overlap + small gap) and, for what is
// left, into vertical lines (rotated dimension text). Groups of >= 2 characters become text boxes; their pixels form
// the text mask that is removed from the geometry image.
#include "raster/RasterInternal.h"
#include <algorithm>
#include <map>
#include <numeric>

namespace pc::drawing::raster {

namespace {

struct Comp {
  int label = 0;
  cv::Rect r;
  int area = 0;
  bool cand = false;   // character candidate
  bool dot = false;    // tiny (period, decimal point, centre-line dot)
  bool thin = false;   // elongated stroke: only accepted inside a group with ordinary characters
};

struct UF {
  std::vector<int> p;
  explicit UF(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int f(int x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
  void u(int a, int b) { a = f(a); b = f(b); if (a != b) p[b] = a; }
};

// Link components along the reading direction. horizontal=true: x is the reading axis.
void link(const std::vector<Comp>& cs, const std::vector<int>& idx, bool horizontal, UF& uf) {
  auto ext = [&](const cv::Rect& r, bool along) { return along == horizontal ? std::make_pair(r.x, r.x + r.width) : std::make_pair(r.y, r.y + r.height); };
  // along-axis = reading axis
  std::vector<int> order = idx;
  std::sort(order.begin(), order.end(), [&](int a, int b) { return ext(cs[a].r, true).first < ext(cs[b].r, true).first; });
  for (std::size_t i = 0; i < order.size(); ++i) {
    const Comp& A = cs[order[i]];
    const auto aAl = ext(A.r, true), aAc = ext(A.r, false);
    const int aH = aAc.second - aAc.first;
    for (std::size_t j = i + 1; j < order.size(); ++j) {
      const Comp& B = cs[order[j]];
      const auto bAl = ext(B.r, true), bAc = ext(B.r, false);
      const int bH = bAc.second - bAc.first;
      const int hMax = std::max(aH, bH), hMin = std::min(aH, bH);
      const int gap = bAl.first - aAl.second;
      if (gap > 0.9 * hMax + 2) {
        if (bAl.first - aAl.second > 3 * hMax) break;
        continue;
      }
      // across-axis relation
      const int ov = std::min(aAc.second, bAc.second) - std::max(aAc.first, bAc.first);
      bool ok = false;
      if (A.dot || B.dot || hMin < 0.5 * hMax) {   // '.', '-', '°', ',' next to full-height characters
        const Comp& big = aH >= bH ? A : B;
        const auto bigAc = ext(big.r, false);
        const auto smAc = aH >= bH ? bAc : aAc;
        const int bh = bigAc.second - bigAc.first;
        ok = smAc.first >= bigAc.first - 0.3 * bh && smAc.second <= bigAc.second + 0.3 * bh && gap < 0.6 * hMax + 2;
      } else {
        ok = ov >= 0.45 * hMin && hMax <= 2.6 * hMin;
      }
      if (ok) uf.u(order[i], order[j]);
    }
  }
}

}  // namespace

void detectText(const Prep& p, cv::Mat& labels, std::vector<TextBox>& boxes, cv::Mat& textMask) {
  boxes.clear();
  cv::Mat stats, cents;
  const int n = cv::connectedComponentsWithStats(p.bin, labels, stats, cents, 8, CV_32S);
  const double cMin = p.px(1.0), cMax = p.px(6.5), dotMax = p.px(0.9), dashLen = p.px(2.0);
  std::vector<Comp> cs(n);
  std::vector<int> cand;
  for (int i = 1; i < n; ++i) {
    Comp& c = cs[i];
    c.label = i;
    c.r = cv::Rect(stats.at<int>(i, cv::CC_STAT_LEFT), stats.at<int>(i, cv::CC_STAT_TOP), stats.at<int>(i, cv::CC_STAT_WIDTH),
                   stats.at<int>(i, cv::CC_STAT_HEIGHT));
    c.area = stats.at<int>(i, cv::CC_STAT_AREA);
    const int mx = std::max(c.r.width, c.r.height), mn = std::min(c.r.width, c.r.height);
    if (mx > cMax) continue;
    if (mx >= dashLen && mx >= 5 * mn) {          // dash of a hidden / centre line, or 'l', 'I', '1', '|'
      if (mx > p.px(4.5)) continue;
      c.thin = true;
    }
    if (mx < cMin) {
      if (mx <= dotMax) { c.dot = true; c.cand = true; cand.push_back(i); }
      continue;
    }
    // thin hollow ring larger than a character (small hole circle)
    if (mx > p.px(4.5) && c.area < 0.18 * c.r.area()) continue;
    c.cand = true;
    cand.push_back(i);
  }
  auto groupsOf = [&](UF& uf, const std::vector<int>& idx) {
    std::map<int, std::vector<int>> g;
    for (int i : idx) g[uf.f(i)].push_back(i);
    return g;
  };
  std::vector<uint8_t> used(n, 0);
  auto accept = [&](const std::vector<int>& members, bool horizontal) -> bool {
    int real = 0, normal = 0;
    std::vector<int> hs;
    cv::Rect bb;
    for (int m : members) {
      bb = bb.area() ? (bb | cs[m].r) : cs[m].r;
      if (!cs[m].dot) { ++real; hs.push_back(horizontal ? cs[m].r.height : cs[m].r.width); }
      if (!cs[m].dot && !cs[m].thin) ++normal;
    }
    if (real < 2 || normal < 1 || 2 * normal < real - 1) return false;
    std::sort(hs.begin(), hs.end());
    const double med = hs[hs.size() / 2];
    if (med < p.px(1.4)) return false;
    const double len = horizontal ? bb.width : bb.height, th = horizontal ? bb.height : bb.width;
    if (len < 1.2 * th) return false;
    if (th > 2.2 * med) return false;
    TextBox tb;
    tb.box = bb;
    tb.rot = horizontal ? 0 : 90;
    tb.comps = members;
    tb.charH = med;
    tb.inTitle = p.titleBlock.area() > 0 && p.titleBlock.contains(cv::Point(bb.x + bb.width / 2, bb.y + bb.height / 2));
    boxes.push_back(tb);
    for (int m : members) used[m] = 1;
    return true;
  };
  {
    UF uf(n);
    link(cs, cand, true, uf);
    for (auto& [root, mem] : groupsOf(uf, cand)) accept(mem, true);
  }
  {
    std::vector<int> rest;
    for (int i : cand) if (!used[i]) rest.push_back(i);
    UF uf(n);
    link(cs, rest, false, uf);
    for (auto& [root, mem] : groupsOf(uf, rest)) accept(mem, false);
  }
  // text mask (component pixels only, slightly dilated)
  textMask = cv::Mat::zeros(p.bin.size(), CV_8U);
  for (int y = 0; y < labels.rows; ++y) {
    const int* l = labels.ptr<int>(y);
    uint8_t* t = textMask.ptr<uint8_t>(y);
    for (int x = 0; x < labels.cols; ++x)
      if (l[x] && used[l[x]]) t[x] = 255;
  }
  cv::dilate(textMask, textMask, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
  cv::bitwise_and(textMask, p.bin, textMask);
  std::sort(boxes.begin(), boxes.end(), [](const TextBox& a, const TextBox& b) {
    return a.box.y != b.box.y ? a.box.y < b.box.y : a.box.x < b.box.x;
  });
}

}  // namespace pc::drawing::raster
