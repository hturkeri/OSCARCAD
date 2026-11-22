// WP-2: S1c raster ingest — RasterImage -> PrimitiveSheet (same IR as vector ingest, with confidences).
// Pipeline: preprocess (deskew, denoise, binarise, border/title) -> text/graphics separation -> vectorisation
// (lines, arcs, circles, dashed lines, arrowheads) -> per-line OCR (Tesseract, multi-hypothesis) re-ranked by the
// dimension grammar with geometric context -> IR in sheet mm (RasterInfo::pxToSheet).
#include "ocr/Ocr.h"
#include "pc/drawing/Confidence.h"
#include "pc/drawing/DimGrammar.h"
#include "raster/RasterInternal.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

namespace pc::drawing {

namespace {

using raster::Prep;
using raster::TextBox;

std::string fmt(double v, int prec = 3) {
  char b[64];
  std::snprintf(b, sizeof b, "%.*f", prec, v);
  return b;
}

struct OcrOut {
  bool done = false;
  std::string utf8, raw;
  float conf = 0;
  int rotDeg = 0;
  bool isDim = false;
  DimText parsed;
  std::vector<TextHypothesis> alts;
  DimTextContext ctx;
};

const char* kDimWhitelist =
    "0123456789.,+-±°Øx×/\"'()ACDEFGHJKMNPRSTUXYZacdefghjkmnprstuxyz";

cv::Mat cropText(const Prep& p, const cv::Mat& labels, const TextBox& tb, double scale) {
  const int pad = std::max(6, static_cast<int>(tb.charH * 0.5));
  cv::Mat c(tb.box.height + 2 * pad, tb.box.width + 2 * pad, CV_8U, cv::Scalar(255));
  std::set<int> ls(tb.comps.begin(), tb.comps.end());
  for (int y = 0; y < tb.box.height; ++y) {
    const int* l = labels.ptr<int>(tb.box.y + y);
    uint8_t* d = c.ptr<uint8_t>(y + pad);
    for (int x = 0; x < tb.box.width; ++x)
      if (ls.count(l[tb.box.x + x])) d[x + pad] = 0;
  }
  (void)p;
  if (std::abs(scale - 1) > 0.05) cv::resize(c, c, cv::Size(), scale, scale, scale < 1 ? cv::INTER_AREA : cv::INTER_LINEAR);
  return c;
}

ocr::GrayView view(const cv::Mat& m) { return {m.cols, m.rows, m.step, m.data}; }

int countLetters(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++n;
  return n;
}

void runOcr(ocr::Engine& eng, ocr::Engine* engTur, const Prep& p, const cv::Mat& labels, const TextBox& tb, OcrOut& o) {
  const double scale = std::clamp(42.0 / std::max(1.0, tb.charH), 0.5, 4.0);
  cv::Mat c = cropText(p, labels, tb, scale);
  std::vector<std::pair<int, cv::Mat>> orients;
  if (tb.rot == 0) orients.emplace_back(0, c);
  else {
    cv::Mat a, b;
    cv::rotate(c, a, cv::ROTATE_90_CLOCKWISE);          // text reading bottom -> top (ISO vertical dims)
    cv::rotate(c, b, cv::ROTATE_90_COUNTERCLOCKWISE);   // reading top -> bottom
    orients.emplace_back(90, a);
    orients.emplace_back(270, b);
  }
  const double dpi = 300;
  ocr::Result best;
  int bestRot = 0;
  cv::Mat bestImg;
  double bestScore = -1;
  for (auto& [rot, img] : orients) {
    ocr::Result r = eng.recognize(view(img), 7, "", dpi);
    const DimText d = parseDimText(r.text, o.ctx);
    const double sc = r.conf + (d.valid ? 0.25 * d.conf : 0);
    if (sc > bestScore) { bestScore = sc; best = r; bestRot = rot; bestImg = img; }
  }
  o.rotDeg = bestRot;
  o.raw = best.text;
  std::vector<TextHypothesis> hyps;
  if (!best.text.empty()) hyps.push_back({best.text, best.conf});
  for (auto& a : best.alternatives) hyps.push_back(a);
  ocr::Result dimR, turR;
  if (!tb.inTitle) {
    dimR = eng.recognize(view(bestImg), 7, kDimWhitelist, dpi);
    if (!dimR.text.empty()) hyps.push_back({dimR.text, dimR.conf * 0.98f});
    for (auto& a : dimR.alternatives) hyps.push_back(a);
  }
  if (engTur && (tb.inTitle || countLetters(best.text) >= 3)) {
    turR = engTur->recognize(view(bestImg), 7, "", dpi);
  }
  DimText parsed;
  const int bi = rankDimTextHypotheses(hyps, o.ctx, &parsed);
  const bool dimOk = bi >= 0 && parsed.valid && !tb.inTitle && parsed.kind != DimKind::Note;
  if (dimOk) {
    o.isDim = true;
    o.parsed = parsed;
    o.utf8 = formatDimText(parsed);
    o.conf = static_cast<float>(std::min(0.85, 0.5 * hyps[bi].conf + 0.5 * parsed.conf));
  } else {
    // note / title text: prefer the Turkish model (Turkish letters, also reads Latin), then plain eng
    if (!turR.text.empty() && turR.conf >= best.conf - 0.1f) { o.utf8 = turR.text; o.conf = turR.conf; }
    else { o.utf8 = best.text; o.conf = best.conf; }
    o.conf = std::min(o.conf * 0.9f, 0.85f);
    o.parsed = parseDimText(o.utf8, o.ctx);
  }
  std::set<std::string> seen{o.utf8};
  std::vector<TextHypothesis> all = hyps;
  if (!turR.text.empty()) all.push_back({turR.text, turR.conf});
  std::stable_sort(all.begin(), all.end(), [](const TextHypothesis& a, const TextHypothesis& b) { return a.conf > b.conf; });
  for (auto& h : all)
    if (!h.utf8.empty() && seen.insert(h.utf8).second && o.alts.size() < 6) o.alts.push_back(h);
  o.done = true;
}

}  // namespace

StageResult ingestRaster(const RasterImage& img, const IngestOptions& opt, PrimitiveSheet& out, int& nextId, ProgressSink* prog) {
  const int st = static_cast<int>(Stage::Ingest);
  StageResult res;
  Prep p;
  std::string err;
  report(prog, 0.0, "Görüntü hazırlanıyor");
  try {
    if (!raster::preprocess(img, opt, p, res.diagnostics, &err)) {
      res.status = Status::InvalidInput;
      res.message = err;
      res.diagnostics.push_back({Severity::Error, "raster.invalidImage", err, st, {}});
      return res;
    }
    if (isCancelled(prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    report(prog, 0.15, "Metin bölgeleri ayrılıyor");
    cv::Mat labels, textMask;
    std::vector<TextBox> boxes;
    raster::detectText(p, labels, boxes, textMask);
    cv::Mat geom = p.bin.clone();
    geom.setTo(0, textMask);
    if (isCancelled(prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    report(prog, 0.25, "Geometri vektörleştiriliyor");
    raster::Geometry g;
    if (!raster::vectorize(p, geom, g, prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");

    // ---------------------------------------------------------------- coordinate mapping
    const double s = 25.4 / p.dpi;
    const Affine2 pxToSheet{s, 0, 0, -s, 0, p.H * s};
    auto P = [&](const cv::Point2d& q) { return transformPoint(pxToSheet, {q.x, q.y}); };
    const SourceKind src = opt.rasterIsPhoto ? SourceKind::Photo : SourceKind::RasterImage;

    // ---------------------------------------------------------------- OCR context from geometry
    std::vector<OcrOut> ocrOut(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
      const auto& b = boxes[i].box;
      const cv::Point2d c(b.x + b.width / 2.0, b.y + b.height / 2.0);
      DimTextContext& ctx = ocrOut[i].ctx;
      ctx.fromOcr = true;
      for (const auto& a : g.arcs) {
        const double d = std::hypot(c.x - a.c.x, c.y - a.c.y);
        if (a.full && d <= a.r + p.px(20) && a.r >= p.px(1)) ctx.nearCircleOrCylinder = true;
        if (!a.full) {
          if (std::abs(d - a.r) <= p.px(15)) {
            double t = raster::sheetAngleDeg(a.c, c);
            while (t < a.a0 - 25) t += 360;
            if (t <= a.a1 + 25) {
              if (a.width <= 1.3 * g.thinW + 0.5 && d > a.r * 0.5) ctx.angular = true;
              else ctx.nearArc = true;
            }
          }
        }
      }
    }

    // ---------------------------------------------------------------- OCR (parallel, one engine per thread)
    std::string ocrMsg;
    bool ocrOk = false;
    std::string tessDir;
    if (!boxes.empty()) {
      report(prog, 0.6, "Metin okunuyor (OCR)");
      const bool wantTur = std::find(opt.ocrLanguages.begin(), opt.ocrLanguages.end(), "tur") != opt.ocrLanguages.end();
      tessDir = ocr::resolveTessdataDir(opt.tessdataDir, "eng");
      const std::string turDir = wantTur ? ocr::resolveTessdataDir(opt.tessdataDir, "eng+tur") : std::string();
      if (!ocr::available()) {
        ocrMsg = "OCR: tesseract olmadan derlendi";
      } else if (tessDir.empty()) {
        ocrMsg = "OCR: tessdata (eng.traineddata) bulunamadı";
      } else {
        unsigned hw = std::thread::hardware_concurrency();
        int nThreads = opt.threads > 0 ? opt.threads : static_cast<int>(hw ? hw : 2);
        nThreads = std::clamp(nThreads, 1, std::min<int>(8, static_cast<int>(boxes.size())));
        std::atomic<std::size_t> next{0};
        std::atomic<bool> cancelled{false};
        std::mutex mu;
        std::string engErr;
        auto worker = [&] {
          ocr::Engine eng(tessDir, "eng");
          std::unique_ptr<ocr::Engine> tur;
          if (!turDir.empty()) tur = std::make_unique<ocr::Engine>(turDir, "tur");   // tur primary: eng+tur drops Ş/Ö/Ç/İ
          if (!eng.ok()) { std::lock_guard<std::mutex> lk(mu); engErr = eng.error(); return; }
          for (;;) {
            const std::size_t i = next++;
            if (i >= boxes.size() || cancelled) break;
            runOcr(eng, tur && tur->ok() ? tur.get() : nullptr, p, labels, boxes[i], ocrOut[i]);
          }
        };
        std::vector<std::thread> th;
        for (int t = 1; t < nThreads; ++t) th.emplace_back(worker);
        worker();
        for (auto& t : th) t.join();
        if (!engErr.empty()) ocrMsg = engErr;
        ocrOk = engErr.empty();
        if (turDir.empty() && wantTur)
          res.diagnostics.push_back({Severity::Info, "ocr.noTur", "tur.traineddata bulunamadı; yalnız eng kullanıldı", st, {}});
      }
      if (!ocrOk)
        res.diagnostics.push_back({Severity::Warning, "ocr.unavailable", ocrMsg + " — metin bölgeleri okunmadan eklendi", st, {}});
    }
    if (isCancelled(prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    report(prog, 0.9, "Ara gösterim oluşturuluyor");

    // ---------------------------------------------------------------- IR
    out = PrimitiveSheet{};
    out.source = src;
    out.page = opt.page;
    out.sheet = {{0, 0}, {p.W * s, p.H * s}};
    out.unitToMm = s;
    RasterInfo ri;
    ri.widthPx = p.origW;
    ri.heightPx = p.origH;
    ri.dpi = p.dpi;
    ri.dpiEstimated = p.dpiEstimated;
    ri.deskewDeg = p.skewDeg;
    ri.homography = p.homography;
    ri.pxToSheet = pxToSheet;
    const float noisePenalty = p.noisy ? 0.05f : 0.f;
    // lines
    for (const auto& L : g.lines) {
      Primitive pr;
      pr.id = nextId++;
      pr.kind = PrimKind::Line;
      pr.p0 = P(L.a);
      pr.p1 = P(L.b);
      pr.style.width = L.width * s;
      for (double d : L.dashPx) pr.style.dash.push_back(d * s);
      const double lenMm = L.length() * s;
      float c = static_cast<float>(0.72 + 0.14 * L.support) - noisePenalty;
      if (lenMm < 2) c -= 0.15f;
      if (L.pieces > 1) c = 0.75f - noisePenalty;
      pr.conf = std::clamp(c, 0.3f, 0.88f);
      pr.prov = {src, opt.page, L.ref};
      pr.attrs["raster.widthPx"] = fmt(L.width, 2);
      if (!L.lineType.empty()) pr.attrs["raster.lineType"] = L.lineType;
      if (L.pieces > 1) pr.attrs["raster.dashPieces"] = std::to_string(L.pieces);
      if (L.arrowA || L.arrowB) pr.attrs["raster.arrowEnds"] = std::string(L.arrowA ? "0" : "") + (L.arrowB ? "1" : "");
      if (p.border.area() > 0) {
        const bool onBorder = (std::abs(L.a.x - p.border.x) < 4 && std::abs(L.b.x - p.border.x) < 4) ||
                              (std::abs(L.a.y - p.border.y) < 4 && std::abs(L.b.y - p.border.y) < 4) ||
                              (std::abs(L.a.x - p.border.br().x + 1) < 4 && std::abs(L.b.x - p.border.br().x + 1) < 4) ||
                              (std::abs(L.a.y - p.border.br().y + 1) < 4 && std::abs(L.b.y - p.border.br().y + 1) < 4);
        if (onBorder) pr.attrs["raster.border"] = "1";
      }
      if (p.titleBlock.area() > 0) {
        cv::Rect tb = p.titleBlock;
        tb.x -= 3; tb.y -= 3; tb.width += 6; tb.height += 6;
        if (tb.contains(cv::Point(L.a)) && tb.contains(cv::Point(L.b))) pr.attrs["raster.titleBlock"] = "1";
      }
      out.prims.push_back(std::move(pr));
    }
    // arcs and circles
    for (const auto& A : g.arcs) {
      Primitive pr;
      pr.id = nextId++;
      pr.kind = A.full ? PrimKind::Circle : PrimKind::Arc;
      pr.center = P(A.c);
      pr.radius = A.r * s;
      pr.a0 = A.full ? 0 : A.a0;
      pr.a1 = A.full ? 360 : A.a1;
      pr.style.width = A.width * s;
      float c = A.ref.rfind("hough", 0) == 0 ? 0.65f : static_cast<float>(0.88 - 0.08 * std::min(2.0, A.rms));
      c = c * static_cast<float>(std::min(1.0, A.support + 0.05)) - noisePenalty;
      pr.conf = std::clamp(c, 0.3f, 0.88f);
      pr.prov = {src, opt.page, A.ref};
      pr.attrs["raster.fitRmsPx"] = fmt(A.rms, 2);
      pr.attrs["raster.support"] = fmt(A.support, 2);
      out.prims.push_back(std::move(pr));
    }
    // arrowheads
    int ak = 0;
    for (const auto& A : g.arrows) {
      Primitive pr;
      pr.id = nextId++;
      pr.kind = PrimKind::Arrowhead;
      pr.p0 = P(A.tip);
      pr.p1 = P(A.base);
      pr.style.filled = A.filled;
      pr.style.width = A.width * s;   // base width
      pr.conf = std::clamp(A.conf - noisePenalty, 0.3f, 0.85f);
      pr.prov = {src, opt.page, "arrow#" + std::to_string(ak++)};
      out.prims.push_back(std::move(pr));
    }
    // texts
    for (std::size_t i = 0; i < boxes.size(); ++i) {
      const TextBox& tb = boxes[i];
      const OcrOut& o = ocrOut[i];
      TextRun t;
      t.id = nextId++;
      const double x0 = tb.box.x, y0 = tb.box.y, x1 = tb.box.x + tb.box.width, y1 = tb.box.y + tb.box.height;
      int rot = tb.rot == 0 ? 0 : (o.done ? o.rotDeg : 90);
      if (rot == 0) {
        t.quad = {P({x0, y1}), P({x1, y1}), P({x1, y0}), P({x0, y0})};
        t.height = (y1 - y0) * s;
      } else if (rot == 90) {
        t.quad = {P({x1, y1}), P({x1, y0}), P({x0, y0}), P({x0, y1})};
        t.height = (x1 - x0) * s;
      } else {
        t.quad = {P({x0, y0}), P({x0, y1}), P({x1, y1}), P({x1, y0})};
        t.height = (x1 - x0) * s;
      }
      t.origin = t.quad[0];
      t.rotDeg = rot;
      t.font = "ocr";
      t.utf8 = o.utf8;
      t.alternatives = o.alts;
      t.conf = o.done ? std::clamp(o.conf - noisePenalty, 0.f, 0.85f) : 0.f;
      t.prov = {src, opt.page, "tess line " + std::to_string(i)};
      t.attrs["raster.boxPx"] = std::to_string(tb.box.x) + "," + std::to_string(tb.box.y) + "," + std::to_string(tb.box.width) + "," + std::to_string(tb.box.height);
      t.attrs["ocr.raw"] = o.raw;
      if (!o.done) t.attrs["ocr.unavailable"] = "1";
      if (o.isDim) {
        t.attrs["ocr.dim"] = "1";
        std::string cs;
        for (const auto& c : o.parsed.corrections) cs += (cs.empty() ? "" : "; ") + c;
        if (!cs.empty()) t.attrs["ocr.corrections"] = cs;
      }
      std::string ctx;
      if (o.ctx.nearCircleOrCylinder) ctx += "circle,";
      if (o.ctx.nearArc) ctx += "arc,";
      if (o.ctx.angular) ctx += "angular,";
      if (!ctx.empty()) { ctx.pop_back(); t.attrs["raster.ctx"] = ctx; }
      if (tb.inTitle) t.attrs["raster.titleBlock"] = "1";
      out.texts.push_back(std::move(t));
    }
    // sheet attributes
    out.attrs["raster.opencv"] = raster::cvc::versionString();
    out.attrs["raster.lsd"] = raster::cvc::hasLsd() ? "1" : "0";
    out.attrs["raster.noise"] = fmt(p.noise, 2);
    out.attrs["raster.strokeThinMm"] = fmt(g.thinW * s, 3);
    out.attrs["raster.strokeThickMm"] = fmt(g.thickW * s, 3);
    if (ocrOk) {
      out.attrs["ocr.engine"] = ocr::engineVersion();
      out.attrs["ocr.tessdata"] = tessDir;
    }
    auto boxStr = [&](const cv::Rect& r) {
      const Vec2 a = P({double(r.x), double(r.y + r.height - 1)}), b = P({double(r.x + r.width - 1), double(r.y)});
      return fmt(a.x) + "," + fmt(a.y) + "," + fmt(b.x) + "," + fmt(b.y);
    };
    if (p.border.area() > 0) out.attrs["raster.borderBox"] = boxStr(p.border);
    if (p.titleBlock.area() > 0) out.attrs["raster.titleBlockBox"] = boxStr(p.titleBlock);
    if (!opt.debugDir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(opt.debugDir, ec);
      const std::string path = (std::filesystem::path(opt.debugDir) / ("raster_p" + std::to_string(opt.page) + "_bin.png")).string();
      try {
        cv::Mat dbg;
        cv::cvtColor(255 - p.bin, dbg, cv::COLOR_GRAY2BGR);
        dbg.setTo(cv::Scalar(200, 200, 255), textMask);
        for (const auto& L : g.lines) cv::line(dbg, L.a, L.b, L.pieces > 1 ? cv::Scalar(255, 0, 0) : cv::Scalar(0, 160, 0), 1);
        for (const auto& A : g.arcs) {
          if (A.full) cv::circle(dbg, A.c, int(A.r), cv::Scalar(0, 0, 255), 1);
          else cv::ellipse(dbg, A.c, cv::Size(int(A.r), int(A.r)), 0, -A.a1, -A.a0, cv::Scalar(0, 0, 255), 1);
        }
        for (const auto& A : g.arrows) cv::line(dbg, A.tip, A.base, cv::Scalar(255, 0, 255), 3);
        for (const auto& b : boxes) cv::rectangle(dbg, b.box, cv::Scalar(0, 128, 255), 1);
        if (cv::imwrite(path, dbg)) ri.preprocessedImagePath = path;
      } catch (const cv::Exception&) {
      }
    }
    out.raster = ri;
    out.diagnostics = res.diagnostics;
    res.status = Status::Ok;
    res.message = std::to_string(g.lines.size()) + " çizgi, " + std::to_string(g.arcs.size()) + " yay/daire, " +
                  std::to_string(g.arrows.size()) + " ok ucu, " + std::to_string(boxes.size()) + " metin";
    report(prog, 1.0, res.message);
    return res;
  } catch (const cv::Exception& e) {
    res.status = Status::Failed;
    res.message = std::string("ingestRaster: OpenCV hatası: ") + e.what();
    res.diagnostics.push_back({Severity::Error, "raster.opencv", res.message, st, {}});
    return res;
  } catch (const std::exception& e) {
    res.status = Status::Failed;
    res.message = std::string("ingestRaster: ") + e.what();
    res.diagnostics.push_back({Severity::Error, "raster.exception", res.message, st, {}});
    return res;
  }
}

}  // namespace pc::drawing
