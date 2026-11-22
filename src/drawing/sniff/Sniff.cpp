// S0: file-type routing (SPEC §2.0). OWNER WP-1.
#include "pc/drawing/Stages.h"
#include "dxf/DxfReader.h"
#if PC_DRAWING_HAVE_QPDF
#include "pdf/PdfInterp.h"
#endif
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace pc::drawing {
namespace {

std::string head(const std::string& path, std::size_t n) {
  std::ifstream f(path, std::ios::binary);
  std::string s(n, '\0');
  f.read(&s[0], static_cast<std::streamsize>(n));
  s.resize(static_cast<std::size_t>(f.gcount()));
  return s;
}
std::string ext(const std::string& p) {
  const auto d = p.find_last_of('.');
  std::string e = d == std::string::npos ? "" : p.substr(d + 1);
  for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}
uint16_t be16(const unsigned char* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t be32(const unsigned char* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// PNG pHYs / JPEG JFIF density / TIFF (not parsed) -> dpi; size from IHDR / SOF
void rasterMeta(const std::string& h, SniffResult& r, int& w, int& hh) {
  const auto* u = reinterpret_cast<const unsigned char*>(h.data());
  w = hh = 0;
  if (h.size() > 24 && std::memcmp(h.data(), "\x89PNG", 4) == 0) {
    w = static_cast<int>(be32(u + 16)); hh = static_cast<int>(be32(u + 20));
    std::size_t p = 8;
    while (p + 12 <= h.size()) {
      const uint32_t len = be32(u + p);
      if (std::memcmp(h.data() + p + 4, "pHYs", 4) == 0 && p + 17 <= h.size() && u[p + 16] == 1) {
        r.dpi = be32(u + p + 8) * 0.0254; break;
      }
      if (std::memcmp(h.data() + p + 4, "IDAT", 4) == 0) break;
      p += 12 + len;
    }
  } else if (h.size() > 4 && u[0] == 0xFF && u[1] == 0xD8) {
    std::size_t p = 2;
    while (p + 9 < h.size() && u[p] == 0xFF) {
      const unsigned char m = u[p + 1];
      const uint16_t len = be16(u + p + 2);
      if (m == 0xE0 && std::memcmp(h.data() + p + 4, "JFIF", 4) == 0 && p + 16 < h.size()) {
        const int unit = u[p + 11]; const double xd = be16(u + p + 12);
        if (unit == 1) r.dpi = xd; else if (unit == 2) r.dpi = xd * 2.54;
      }
      if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB)) {
        hh = be16(u + p + 5); w = be16(u + p + 7); break;
      }
      p += 2 + len;
    }
  }
}
}  // namespace

SniffResult sniff(const std::string& path) {
  SniffResult r;
  std::ifstream test(path, std::ios::binary);
  if (!test) {
    r.result = StageResult::failure(Status::InvalidInput, "Dosya açılamadı: " + path);
    r.message = r.result.message;
    return r;
  }
  test.close();
  const std::string h = head(path, 4096);
  const std::string e = ext(path);
  // ---- PDF
  if (h.find("%PDF-") != std::string::npos && h.find("%PDF-") < 1024) {
#if PC_DRAWING_HAVE_QPDF
    std::vector<pdf::PageStats> pages;
    std::string err;
    if (!pdf::pdfPageStats(path, pages, &err)) {
      r.result = StageResult::failure(Status::InvalidInput, "PDF okunamadı: " + err);
      r.message = r.result.message;
      return r;
    }
    r.pageCount = static_cast<int>(pages.size());
    for (auto& st : pages) {
      PageInfo pi;
      pi.index = st.index; pi.box = st.box; pi.pathCount = st.pathCount; pi.textCount = st.textCount; pi.imageCount = st.imageCount;
      pi.imageCoverage = st.imageCoverage;
      const bool scanned = st.imageCoverage >= 0.8 && st.pathCount < 50;
      pi.kind = scanned ? SourceKind::ScannedPdf : SourceKind::VectorPdf;
      // stroked-path + text density vs page-covering image
      const double content = st.pathCount + 2.0 * st.textCount;
      pi.vectorScore = std::clamp(content / 200.0, 0.0, 1.0) * (1.0 - 0.9 * std::min(1.0, st.imageCoverage / 0.8 * (st.pathCount < 50)));
      if (scanned) pi.vectorScore = std::min(pi.vectorScore, 0.1);
      r.pages.push_back(pi);
    }
    if (!r.pages.empty()) { r.kind = r.pages[0].kind; r.vectorScore = r.pages[0].vectorScore; }
    std::ostringstream m;
    m << "PDF, " << r.pageCount << " sayfa; sayfa 1: " << (r.kind == SourceKind::ScannedPdf ? "taranmış" : "vektör") << " ("
      << (r.pages.empty() ? 0 : r.pages[0].pathCount) << " yol, " << (r.pages.empty() ? 0 : r.pages[0].textCount) << " metin, "
      << (r.pages.empty() ? 0 : r.pages[0].imageCount) << " görüntü)";
    r.message = m.str();
    r.result = StageResult::success(r.message);
#else
    r.kind = SourceKind::VectorPdf;
    r.result = StageResult::unavailable("sniff(PDF sayfa istatistiği)", "qpdf");
    r.result.status = Status::Ok;   // type recognised; page statistics unavailable
    r.result.message = "PDF (sayfa istatistiği için qpdf gerekli)";
    r.message = r.result.message;
#endif
    return r;
  }
  // ---- DXF
  if (h.rfind("AutoCAD Binary DXF", 0) == 0) {
    r.kind = SourceKind::Dxf;
    r.result = StageResult::failure(Status::InvalidInput, "İkili DXF desteklenmiyor (faz 2); ASCII DXF olarak kaydedin");
    r.message = r.result.message;
    return r;
  }
  {
    std::string up = h.substr(0, 2048);
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (up.find("SECTION") != std::string::npos && (up.find("HEADER") != std::string::npos || up.find("ENTITIES") != std::string::npos ||
                                                    up.find("TABLES") != std::string::npos || up.find("CLASSES") != std::string::npos)) {
      r.kind = SourceKind::Dxf;
      dxf::DxfFile f; std::string err;
      if (dxf::readDxf(path, f, &err)) {
        r.dxfLayouts.push_back("");
        for (auto& n : f.layoutNames) if (n != "Model") r.dxfLayouts.push_back(n);
        r.pageCount = static_cast<int>(r.dxfLayouts.size());
        for (int i = 0; i < r.pageCount; ++i) { PageInfo pi; pi.index = i; pi.kind = SourceKind::Dxf; pi.vectorScore = 1; r.pages.push_back(pi); }
        r.pages[0].pathCount = static_cast<int>(f.entities.size());
        r.vectorScore = 1;
        r.message = "DXF (" + (f.acadver.empty() ? std::string("sürüm ?") : f.acadver) + "), " + std::to_string(f.entities.size()) + " varlık";
        r.result = StageResult::success(r.message);
      } else {
        r.result = StageResult::failure(Status::InvalidInput, err);
        r.message = err;
      }
      return r;
    }
  }
  // ---- raster
  const bool png = h.size() > 8 && std::memcmp(h.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
  const bool jpg = h.size() > 3 && static_cast<unsigned char>(h[0]) == 0xFF && static_cast<unsigned char>(h[1]) == 0xD8;
  const bool tif = h.size() > 4 && (std::memcmp(h.data(), "II*\0", 4) == 0 || std::memcmp(h.data(), "MM\0*", 4) == 0);
  const bool bmp = h.size() > 2 && h[0] == 'B' && h[1] == 'M';
  if (png || jpg || tif || bmp) {
    int w = 0, hh = 0;
    rasterMeta(h, r, w, hh);
    r.kind = SourceKind::RasterImage;
    // photo heuristic without decoding: JPEG with EXIF camera data and no density -> suggest photo
    r.looksLikePhoto = jpg && h.find("Exif") != std::string::npos && (h.find("Make") != std::string::npos || h.find("Model") != std::string::npos);
    if (r.looksLikePhoto) r.kind = SourceKind::Photo;
    PageInfo pi; pi.index = 0; pi.kind = r.kind; pi.imageCount = 1; pi.imageCoverage = 1;
    if (r.dpi > 0 && w > 0) pi.box = {{0, 0}, {w / r.dpi * 25.4, hh / r.dpi * 25.4}};
    r.pages.push_back(pi);
    std::ostringstream m;
    m << (png ? "PNG" : jpg ? "JPEG" : tif ? "TIFF" : "BMP") << " görüntü";
    if (w > 0) m << " " << w << "x" << hh;
    if (r.dpi > 0) m << ", " << static_cast<int>(r.dpi + 0.5) << " dpi"; else m << ", DPI bilinmiyor";
    if (r.looksLikePhoto) m << " (fotoğraf olabilir)";
    r.message = m.str();
    r.result = StageResult::success(r.message);
    return r;
  }
  r.result = StageResult::failure(Status::InvalidInput, "Tanınmayan dosya türü" + (e.empty() ? std::string() : " (." + e + ")"));
  r.message = r.result.message;
  return r;
}

}  // namespace pc::drawing
