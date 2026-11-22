// Vector PDF ingest + scanned-page image extraction (SPEC §2.1, Raster.h). OWNER WP-1. Needs libqpdf.
#include "pdf/PdfInterp.h"
#include "glyphtext/GlyphText.h"
#include "pc/drawing/Stages.h"
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <cmath>
#include <sstream>

namespace pc::drawing {

namespace pdf {
bool pdfPageStats(const std::string& path, std::vector<PageStats>& pages, std::string* err) {
  const int n = pdfPageCount(path, err);
  if (n < 0) return false;
  pages.clear();
  for (int i = 0; i < n; ++i) {
    PrimitiveSheet tmp; int id = 1; PageStats st;
    InterpOptions o; o.collect = false;
    if (!interpretPage(path, i, o, tmp, id, st, err)) return false;
    pages.push_back(st);
  }
  return true;
}
}  // namespace pdf

StageResult ingestPdfVector(const std::string& path, const IngestOptions& opt, PrimitiveSheet& out, int& nextId, ProgressSink* progress) {
  report(progress, 0.0, "PDF açılıyor");
  std::string err;
  const int n = pdf::pdfPageCount(path, &err);
  if (n < 0) return StageResult::failure(Status::InvalidInput, err.empty() ? "PDF okunamadı" : "PDF okunamadı: " + err);
  if (opt.page < 0 || opt.page >= n)
    return StageResult::failure(Status::InvalidInput, "Sayfa " + std::to_string(opt.page + 1) + " yok (" + std::to_string(n) + " sayfa)");
  if (isCancelled(progress)) return StageResult::failure(Status::Cancelled, "İptal edildi");
  PrimitiveSheet s;
  s.source = SourceKind::VectorPdf; s.file = path; s.page = opt.page; s.unitToMm = kPtToMm;
  pdf::PageStats st;
  int id = nextId;
  if (!pdf::interpretPage(path, opt.page, pdf::InterpOptions{}, s, id, st, &err)) return StageResult::failure(Status::Failed, err);
  // R1 hook: text drawn as filled glyph outlines (Matplotlib/Acrobat "text as paths") -> TextRuns; the consumed glyph
  // primitives are removed. No-op for pages without such text. See glyphtext/GlyphText.h, docs R1_GLYPHTEXT.md.
  {
    glyphtext::Options go; go.tessdataDir = opt.tessdataDir;
    const glyphtext::Stats gs = glyphtext::extractGlyphText(s, id, go);
    if (gs.runs > 0) {
      Diagnostic d; d.severity = Severity::Info; d.stage = static_cast<int>(Stage::Ingest); d.code = "pdf.glyphText";
      d.message = "Glif çizgisi olarak çizilmiş metin tanındı: " + std::to_string(gs.runs) + " metin, " + std::to_string(gs.glyphs) +
                  " glif (" + std::to_string(gs.glyphClasses) + " farklı), " + std::to_string(gs.removedPrims) + " ilkel çıkarıldı";
      s.diagnostics.push_back(d);
    } else if (gs.note == "ocr unavailable") {
      Diagnostic d; d.severity = Severity::Warning; d.stage = static_cast<int>(Stage::Ingest); d.code = "pdf.glyphText.noOcr";
      d.message = "Sayfada glif çizgisi olarak çizilmiş metin var ama OCR yok; metin okunamadı";
      s.diagnostics.push_back(d);
    }
  }
  nextId = id;
  StageResult r = StageResult::success();
  r.diagnostics = s.diagnostics;
  if (st.imageCoverage >= 0.8 && st.pathCount < 50)
    r.warn("pdf.page.looksScanned", "Sayfa taranmış görünüyor (görüntü kapsamı %" + std::to_string(static_cast<int>(st.imageCoverage * 100)) +
                                         "); raster girişi önerilir", {}, static_cast<int>(Stage::Ingest));
  s.attrs["pdf.pageCount"] = std::to_string(n);
  s.attrs["pdf.imageCount"] = std::to_string(st.imageCount);
  s.diagnostics = r.diagnostics;
  out = std::move(s);
  std::ostringstream m;
  m << "PDF sayfa " << opt.page + 1 << ": " << out.prims.size() << " ilkel, " << out.texts.size() << " metin, " << st.imageCount << " görüntü";
  r.message = m.str();
  report(progress, 1.0, r.message);
  return r;
}

bool extractPdfPageImage(const std::string& path, int page, RasterImage& out, std::string* err) {
  try {
    QPDF q; q.setSuppressWarnings(true); q.processFile(path.c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    if (page < 0 || page >= static_cast<int>(pages.size())) { if (err) *err = "Sayfa yok"; return false; }
    QPDFPageObjectHelper ph = pages[static_cast<std::size_t>(page)];
    // largest image by pixel count (scans: one page-sized image)
    QPDFObjectHandle best; long long bestPx = 0;
    for (auto& [name, img] : ph.getImages()) {
      const long long px = static_cast<long long>(img.getKey("/Width").getIntValue()) * img.getKey("/Height").getIntValue();
      if (px > bestPx) { bestPx = px; best = img; }
    }
    if (!best.isStream()) { if (err) *err = "Sayfada görüntü yok"; return false; }
    QPDFObjectHandle d = best.getDict();
    const int w = static_cast<int>(d.getKey("/Width").getIntValue()), h = static_cast<int>(d.getKey("/Height").getIntValue());
    const int bpc = d.hasKey("/BitsPerComponent") ? static_cast<int>(d.getKey("/BitsPerComponent").getIntValue()) : 1;
    QPDFObjectHandle filt = d.getKey("/Filter");
    std::string lastFilter = filt.isName() ? filt.getName() : (filt.isArray() && filt.getArrayNItems() ? filt.getArrayItem(filt.getArrayNItems() - 1).getName() : "");
    if (lastFilter == "/CCITTFaxDecode" || lastFilter == "/JBIG2Decode" || lastFilter == "/JPXDecode") {
      if (err) *err = "Görüntü kodlaması desteklenmiyor (" + lastFilter + "); PdfRasterizer kullanın";
      return false;
    }
    std::shared_ptr<Buffer> data;
    try { data = best.getStreamData(qpdf_dl_all); } catch (const std::exception& e) {
      if (err) *err = std::string("Görüntü çözülemedi: ") + e.what();
      return false;
    }
    // colour space
    int comps = 1; bool indexed = false; std::vector<uint8_t> palette; int palComps = 3;
    QPDFObjectHandle cs = d.getKey("/ColorSpace");
    auto compsOf = [](QPDFObjectHandle c) {
      if (c.isName()) { const std::string n = c.getName(); return n == "/DeviceRGB" || n == "/CalRGB" ? 3 : n == "/DeviceCMYK" ? 4 : 1; }
      if (c.isArray() && c.getArrayNItems() > 1 && c.getArrayItem(0).getName() == "/ICCBased")
        return static_cast<int>(c.getArrayItem(1).getDict().getKey("/N").getIntValue());
      return 3;
    };
    if (d.getKey("/ImageMask").isBool() && d.getKey("/ImageMask").getBoolValue()) comps = 1;
    else if (cs.isArray() && cs.getArrayNItems() >= 4 && cs.getArrayItem(0).getName() == "/Indexed") {
      indexed = true; palComps = compsOf(cs.getArrayItem(1));
      QPDFObjectHandle lut = cs.getArrayItem(3);
      std::string l = lut.isString() ? lut.getStringValue() : std::string();
      if (lut.isStream()) { auto b = lut.getStreamData(qpdf_dl_all); l.assign(reinterpret_cast<const char*>(b->getBuffer()), b->getSize()); }
      palette.assign(l.begin(), l.end());
    } else if (!cs.isNull()) comps = compsOf(cs);
    const std::size_t rowIn = (static_cast<std::size_t>(w) * comps * bpc + 7) / 8;
    if (data->getSize() < rowIn * static_cast<std::size_t>(h)) { if (err) *err = "Görüntü verisi kısa"; return false; }
    const unsigned char* src = data->getBuffer();
    RasterImage img;
    img.width = w; img.height = h;
    const int outC = (indexed ? palComps : comps) >= 3 ? 3 : 1;
    img.channels = outC; img.stride = 0;
    img.data.resize(static_cast<std::size_t>(w) * h * outC);
    auto sample = [&](const unsigned char* row, int idx) -> int {
      if (bpc == 8) return row[idx];
      if (bpc == 16) return row[idx * 2];
      const int bit = idx * bpc;
      const int v = (row[bit / 8] >> (8 - bpc - bit % 8)) & ((1 << bpc) - 1);
      return indexed ? v : v * 255 / ((1 << bpc) - 1);
    };
    const bool decodeInvert = d.getKey("/Decode").isArray() && d.getKey("/Decode").getArrayNItems() >= 2 &&
                              d.getKey("/Decode").getArrayItem(0).getNumericValue() > d.getKey("/Decode").getArrayItem(1).getNumericValue();
    for (int y = 0; y < h; ++y) {
      const unsigned char* row = src + rowIn * y;
      uint8_t* o = img.data.data() + static_cast<std::size_t>(y) * w * outC;
      for (int x = 0; x < w; ++x) {
        int c[4] = {0, 0, 0, 0};
        if (indexed) {
          const int i = sample(row, x);
          for (int k = 0; k < palComps && k < 4; ++k) c[k] = static_cast<std::size_t>(i * palComps + k) < palette.size() ? palette[i * palComps + k] : 0;
        } else for (int k = 0; k < comps && k < 4; ++k) c[k] = sample(row, x * comps + k);
        const int nc = indexed ? palComps : comps;
        int r, gch, b;
        if (nc == 4) { r = 255 - std::min(255, c[0] + c[3]); gch = 255 - std::min(255, c[1] + c[3]); b = 255 - std::min(255, c[2] + c[3]); }
        else if (nc >= 3) { r = c[0]; gch = c[1]; b = c[2]; }
        else { r = gch = b = c[0]; }
        if (decodeInvert) { r = 255 - r; gch = 255 - gch; b = 255 - b; }
        if (outC == 3) { o[x * 3] = static_cast<uint8_t>(b); o[x * 3 + 1] = static_cast<uint8_t>(gch); o[x * 3 + 2] = static_cast<uint8_t>(r); }
        else o[x] = static_cast<uint8_t>(r);
      }
    }
    // dpi from the page box (scan assumed to cover the page)
    QPDFObjectHandle box = ph.getAttribute("/CropBox", false);
    if (!box.isArray()) box = ph.getAttribute("/MediaBox", false);
    double pw = 612, phh = 792;
    if (box.isArray() && box.getArrayNItems() == 4) {
      pw = std::abs(box.getArrayItem(2).getNumericValue() - box.getArrayItem(0).getNumericValue());
      phh = std::abs(box.getArrayItem(3).getNumericValue() - box.getArrayItem(1).getNumericValue());
    }
    const int rot = static_cast<int>(ph.getAttribute("/Rotate", false).isInteger() ? ph.getAttribute("/Rotate", false).getIntValue() : 0);
    if (rot % 180 != 0) std::swap(pw, phh);
    img.dpi = pw > 0 ? w / (pw / 72.0) : 0;
    out = std::move(img);
    return true;
  } catch (const std::exception& e) {
    if (err) *err = std::string("PDF okunamadı: ") + e.what();
    return false;
  }
}

}  // namespace pc::drawing
