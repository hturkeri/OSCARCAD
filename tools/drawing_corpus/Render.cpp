// Qt rendering of a GroundTruth sheet: vector PDF variants, PNG, degraded scan / photo JPG. OWNER WP-1.
#include "Corpus.h"
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <cmath>

namespace corpus {
namespace {
const double kPi = 3.14159265358979323846;
const double kSheetW = 420, kSheetH = 297;

QFont baseFont() {
  QFont f(QString::fromStdString(fontFamily()));
  f.setPixelSize(100);
  f.setHintingPreference(QFont::PreferNoHinting);
  f.setKerning(false);
  return f;
}
double capRatio() {   // cap height / pixel size
  static const double r = [] { QFontMetricsF fm(baseFont()); return fm.capHeight() > 0 ? fm.capHeight() / 100.0 : 0.72; }();
  return r;
}

void arcPath(QPainterPath& path, const Primitive& p, bool moveFirst) {
  const double a0 = p.a0 * kPi / 180, a1 = p.a1 * kPi / 180;
  const int n = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (kPi / 2) - 1e-9)));
  const double da = (a1 - a0) / n, k = 4.0 / 3.0 * std::tan(da / 4);
  auto pt = [&](double a) { return QPointF(p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)); };
  if (moveFirst) path.moveTo(pt(a0));
  for (int i = 0; i < n; ++i) {
    const double s = a0 + i * da, e = s + da;
    const QPointF ps = pt(s), pe = pt(e);
    path.cubicTo(ps + QPointF(-std::sin(s), std::cos(s)) * (k * p.radius), pe - QPointF(-std::sin(e), std::cos(e)) * (k * p.radius), pe);
  }
}

// flatten arc into points (chord error <= 0.01 mm)
std::vector<Vec2> flatten(const Primitive& p) {
  const double span = (p.a1 - p.a0) * kPi / 180;
  const double step = 2 * std::acos(std::max(0.0, 1 - 0.01 / std::max(p.radius, 0.02)));
  const int n = std::max(4, static_cast<int>(std::ceil(span / std::max(step, 1e-3))));
  std::vector<Vec2> v;
  for (int i = 0; i <= n; ++i) {
    const double a = p.a0 * kPi / 180 + span * i / n;
    v.push_back({p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)});
  }
  return v;
}

// split a polyline into dash pieces following `dash`
std::vector<std::pair<Vec2, Vec2>> dashPieces(const std::vector<Vec2>& pts, const std::vector<double>& dash) {
  std::vector<std::pair<Vec2, Vec2>> out;
  std::size_t di = 0; double left = dash[0]; bool on = true;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    Vec2 a = pts[i]; const Vec2 b = pts[i + 1];
    double segLeft = distance(a, b);
    while (segLeft > 1e-9) {
      const double t = std::min(left, segLeft);
      const Vec2 nb = a + (b - a) * (t / distance(a, b));
      if (on) {
        if (!out.empty() && distance(out.back().second, a) < 1e-9 && i > 0) out.back().second = nb;
        else out.push_back({a, nb});
      }
      a = nb; segLeft -= t; left -= t;
      if (left <= 1e-9) { di = (di + 1) % dash.size(); left = dash[di]; on = !on; }
    }
  }
  // CAD exporters end a dashed edge on a dash: close a trailing gap with a short end dash
  if (!out.empty() && pts.size() >= 2 && distance(out.back().second, pts.back()) > 1e-6) {
    const Vec2 e = pts.back(), d = e - pts[pts.size() - 2];
    if (distance(out.back().second, e) <= 0.8) out.back().second = e;   // short trailing gap: stretch the last dash
    else out.push_back({e - d * (std::min(0.5, length(d)) / length(d)), e});
  }
  return out;
}

void drawSheet(QPainter& g, const GroundTruth& gt, int variant) {
  g.setRenderHint(QPainter::Antialiasing, true);
  for (auto& p : gt.sheet.prims) {
    if (p.kind == PrimKind::Arrowhead) {
      const Vec2 d = (p.p1 - p.p0) * (1 / distance(p.p0, p.p1)), n{-d.y, d.x};
      const Vec2 b1 = p.p1 + n * (kArrowW / 2), b2 = p.p1 - n * (kArrowW / 2);
      QPainterPath path; path.moveTo(p.p0.x, p.p0.y); path.lineTo(b1.x, b1.y); path.lineTo(b2.x, b2.y); path.closeSubpath();
      g.fillPath(path, Qt::black);
      continue;
    }
    QPen pen(Qt::black, p.style.width > 0 ? p.style.width : 0.25, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    const bool dashed = !p.style.dash.empty();
    std::vector<Vec2> poly;
    if (p.kind == PrimKind::Line) poly = {p.p0, p.p1};
    else if (p.kind == PrimKind::Polyline) poly = p.pts;
    else if (variant == 2) poly = flatten(p);
    if (variant == 2 && dashed) {
      g.setPen(pen);
      for (auto& [a, b] : dashPieces(poly, p.style.dash)) g.drawLine(QPointF(a.x, a.y), QPointF(b.x, b.y));
      continue;
    }
    if (dashed) {
      QVector<qreal> pat;
      for (double x : p.style.dash) pat << x / pen.widthF();
      pen.setDashPattern(pat);
    }
    g.setPen(pen); g.setBrush(Qt::NoBrush);
    QPainterPath path;
    if (!poly.empty()) { path.moveTo(poly[0].x, poly[0].y); for (std::size_t i = 1; i < poly.size(); ++i) path.lineTo(poly[i].x, poly[i].y); }
    else arcPath(path, p, true);
    g.drawPath(path);
  }
  const QFont f = baseFont();
  for (auto& t : gt.sheet.texts) {
    const double k = t.height / capRatio() / 100.0;
    g.save();
    g.translate(t.origin.x, t.origin.y);
    g.rotate(t.rotDeg);
    g.scale(k, -k);
    const QString s = QString::fromStdString(t.utf8);
    if (variant == 1) {
      QPainterPath path; path.addText(0, 0, f, s);
      g.fillPath(path, Qt::black);
    } else {
      g.setFont(f); g.setPen(Qt::black); g.drawText(QPointF(0, 0), s);
    }
    g.restore();
  }
}

QTransform sheetToDevice(double pxPerMm) { return QTransform(pxPerMm, 0, 0, -pxPerMm, 0, kSheetH * pxPerMm); }

QImage gray(const QImage& im) { return im.convertToFormat(QImage::Format_Grayscale8); }
void boxBlur(QImage& im, int r) {
  if (r <= 0) return;
  const int w = im.width(), h = im.height();
  std::vector<int> tmp(static_cast<std::size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    const uchar* s = im.constScanLine(y);
    int acc = 0; for (int x = -r; x <= r; ++x) acc += s[std::clamp(x, 0, w - 1)];
    for (int x = 0; x < w; ++x) { tmp[static_cast<std::size_t>(y) * w + x] = acc; acc += s[std::min(x + r + 1, w - 1)] - s[std::max(x - r, 0)]; }
  }
  const int n = (2 * r + 1) * (2 * r + 1);
  for (int x = 0; x < w; ++x) {
    int acc = 0; for (int y = -r; y <= r; ++y) acc += tmp[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w + x];
    for (int y = 0; y < h; ++y) {
      im.scanLine(y)[x] = static_cast<uchar>(acc / n);
      acc += tmp[static_cast<std::size_t>(std::min(y + r + 1, h - 1)) * w + x] - tmp[static_cast<std::size_t>(std::max(y - r, 0)) * w + x];
    }
  }
}
}  // namespace

std::string fontFamily() { return "DejaVu Sans"; }
double textAdvanceMm(const std::string& s, double h) {
  QFontMetricsF fm(baseFont());
  return fm.horizontalAdvance(QString::fromStdString(s)) * h / capRatio() / 100.0;
}

bool writePdf(const GroundTruth& gt, const std::string& path, int variant) {
  QPdfWriter w(QString::fromStdString(path));
  w.setPageSize(QPageSize(QSizeF(kSheetW, kSheetH), QPageSize::Millimeter));
  w.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
  w.setResolution(1200);
  w.setCreator("PersistCAD pc_drawing_corpus");
  w.setTitle(QString::fromStdString(gt.caseName));
  QPainter g;
  if (!g.begin(&w)) return false;
  // MediaBox is rounded to whole points: flip about the real page height so sheet mm == PDF user space * kPtToMm
  const double pageHmm = w.pageLayout().fullRectPoints().height() * 25.4 / 72.0, k = 1200 / 25.4;
  g.setTransform(QTransform(k, 0, 0, -k, 0, pageHmm * k));
  drawSheet(g, gt, variant);
  return g.end();
}

QImage renderImage(const GroundTruth& gt, double dpi) {
  const double k = dpi / 25.4;
  QImage im(static_cast<int>(std::lround(kSheetW * k)), static_cast<int>(std::lround(kSheetH * k)), QImage::Format_RGB32);
  im.fill(Qt::white);
  im.setDotsPerMeterX(static_cast<int>(std::lround(dpi / 0.0254))); im.setDotsPerMeterY(im.dotsPerMeterX());
  QPainter g(&im);
  g.setTransform(sheetToDevice(k));
  drawSheet(g, gt, 0);
  g.end();
  QImage out = gray(im);
  out.setDotsPerMeterX(im.dotsPerMeterX()); out.setDotsPerMeterY(im.dotsPerMeterY());
  return out;
}

QImage makeScan(const QImage& clean, double skewDeg, double blurSigma, int noise, std::mt19937& rng) {
  QImage im(clean.size(), QImage::Format_RGB32);
  im.fill(QColor(246, 244, 238));
  {
    QPainter g(&im);
    g.setRenderHint(QPainter::SmoothPixmapTransform);
    g.translate(im.width() / 2.0, im.height() / 2.0); g.rotate(-skewDeg); g.translate(-im.width() / 2.0, -im.height() / 2.0);
    g.setCompositionMode(QPainter::CompositionMode_Multiply);
    g.drawImage(0, 0, clean.convertToFormat(QImage::Format_RGB32));
    g.resetTransform();
    std::uniform_real_distribution<double> ux(0, im.width()), uy(0, im.height()), ur(20, 120);
    for (int i = 0; i < 3; ++i) {   // stains
      g.setPen(Qt::NoPen); g.setBrush(QColor(150, 140, 110, 40));
      g.drawEllipse(QPointF(ux(rng), uy(rng)), ur(rng), ur(rng) * 0.7);
    }
    g.setPen(QPen(QColor(120, 120, 120, 70), 3));   // fold mark
    const double fx = im.width() * 0.5 + ur(rng);
    g.drawLine(QPointF(fx, 0), QPointF(fx + 15, im.height()));
  }
  QImage out = gray(im);
  boxBlur(out, static_cast<int>(std::lround(blurSigma)));
  std::uniform_int_distribution<int> un(-noise, noise);
  for (int y = 0; y < out.height(); ++y) {
    uchar* s = out.scanLine(y);
    for (int x = 0; x < out.width(); ++x) s[x] = static_cast<uchar>(std::clamp(s[x] + un(rng), 0, 255));
  }
  out.setDotsPerMeterX(clean.dotsPerMeterX()); out.setDotsPerMeterY(clean.dotsPerMeterY());
  return out;
}

QImage makePhoto(const QImage& clean, double tiltDeg, std::mt19937& rng) {
  const int W = clean.width(), H = clean.height();
  QImage im(W, H, QImage::Format_RGB32);
  im.fill(QColor(90, 85, 80));
  std::uniform_real_distribution<double> j(0.02, 0.08);
  const double t = std::tan(tiltDeg * kPi / 180) * 0.5;
  QPolygonF dst;
  dst << QPointF(W * (j(rng) + t * 0.3), H * j(rng)) << QPointF(W * (1 - j(rng) - t * 0.3), H * j(rng))
      << QPointF(W * (1 - j(rng) + t * 0.1), H * (1 - j(rng))) << QPointF(W * (j(rng) - t * 0.1), H * (1 - j(rng)));
  QPolygonF src; src << QPointF(0, 0) << QPointF(W, 0) << QPointF(W, H) << QPointF(0, H);
  QTransform q;
  QTransform::quadToQuad(src, dst, q);
  QPainter g(&im);
  g.setRenderHint(QPainter::SmoothPixmapTransform);
  g.setTransform(q);
  g.drawImage(0, 0, clean.convertToFormat(QImage::Format_RGB32));
  g.resetTransform();
  QLinearGradient lg(0, 0, W, H);
  lg.setColorAt(0, QColor(255, 255, 255, 0)); lg.setColorAt(1, QColor(40, 30, 20, 110));
  g.fillRect(im.rect(), lg);
  g.end();
  return im;
}
}  // namespace corpus
