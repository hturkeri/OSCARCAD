// WP-2: loadRasterFile — PNG / JPG / TIFF / BMP via OpenCV imgcodecs + resolution from file metadata
// (PNG pHYs, JPEG JFIF APP0 / EXIF, TIFF XResolution/ResolutionUnit, BMP biXPelsPerMeter). 16-bit -> 8-bit.
#include "raster/RasterInternal.h"
#include <opencv2/imgcodecs.hpp>
#include <cstring>
#include <fstream>
#include <iterator>

namespace pc::drawing {

namespace {

std::vector<uint8_t> readHead(const std::string& path, std::size_t maxBytes) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> b;
  if (!f) return b;
  b.resize(maxBytes);
  f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(maxBytes));
  b.resize(static_cast<std::size_t>(f.gcount()));
  return b;
}

uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t le32(const uint8_t* p) { return (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0]; }
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>((p[1] << 8) | p[0]); }

double pngDpi(const std::vector<uint8_t>& b) {
  if (b.size() < 8 || std::memcmp(b.data(), "\x89PNG\r\n\x1a\n", 8) != 0) return 0;
  std::size_t p = 8;
  while (p + 12 <= b.size()) {
    const uint32_t len = be32(&b[p]);
    const char* type = reinterpret_cast<const char*>(&b[p + 4]);
    if (std::memcmp(type, "pHYs", 4) == 0 && len >= 9 && p + 8 + 9 <= b.size()) {
      const uint32_t ppux = be32(&b[p + 8]);
      const uint8_t unit = b[p + 16];
      if (unit == 1 && ppux > 0) return ppux * 0.0254;   // pixels per metre
      return 0;
    }
    if (std::memcmp(type, "IDAT", 4) == 0) break;
    p += 12 + static_cast<std::size_t>(len);
  }
  return 0;
}

// TIFF IFD resolution (also used for EXIF inside JPEG). base = start of the TIFF header.
double tiffDpi(const uint8_t* base, std::size_t n) {
  if (n < 8) return 0;
  const bool le = base[0] == 'I' && base[1] == 'I';
  const bool be = base[0] == 'M' && base[1] == 'M';
  if (!le && !be) return 0;
  auto r16 = [&](std::size_t o) -> uint32_t { return o + 2 <= n ? (le ? le16(base + o) : be16(base + o)) : 0; };
  auto r32 = [&](std::size_t o) -> uint32_t { return o + 4 <= n ? (le ? le32(base + o) : be32(base + o)) : 0; };
  if (r16(2) != 42) return 0;
  const uint32_t ifd = r32(4);
  if (ifd + 2 > n) return 0;
  const uint32_t cnt = r16(ifd);
  double xres = 0;
  int unit = 2;
  for (uint32_t i = 0; i < cnt; ++i) {
    const std::size_t e = ifd + 2 + 12 * i;
    if (e + 12 > n) break;
    const uint32_t tag = r16(e), type = r16(e + 2);
    if (tag == 282 && type == 5) {
      const uint32_t off = r32(e + 8);
      const uint32_t num = r32(off), den = r32(off + 4);
      if (den) xres = static_cast<double>(num) / den;
    } else if (tag == 296) {
      unit = static_cast<int>(r16(e + 8));
    }
  }
  if (xres <= 0) return 0;
  if (unit == 3) return xres * 2.54;
  if (unit == 2) return xres;
  return 0;
}

double jpegDpi(const std::vector<uint8_t>& b) {
  if (b.size() < 4 || b[0] != 0xFF || b[1] != 0xD8) return 0;
  std::size_t p = 2;
  double exif = 0;
  while (p + 4 <= b.size() && b[p] == 0xFF) {
    const uint8_t m = b[p + 1];
    if (m == 0xDA || m == 0xD9) break;
    const uint16_t len = be16(&b[p + 2]);
    const std::size_t seg = p + 4;
    if (m == 0xE0 && len >= 14 && seg + 12 <= b.size() && std::memcmp(&b[seg], "JFIF\0", 5) == 0) {
      const uint8_t unit = b[seg + 7];
      const uint16_t xd = be16(&b[seg + 8]);
      if (unit == 1 && xd > 1) return xd;
      if (unit == 2 && xd > 1) return xd * 2.54;
    }
    if (m == 0xE1 && len >= 14 && seg + 6 <= b.size() && std::memcmp(&b[seg], "Exif\0\0", 6) == 0) {
      const std::size_t n = std::min<std::size_t>(len - 8, b.size() - (seg + 6));
      exif = tiffDpi(&b[seg + 6], n);
    }
    p += 2 + len;
  }
  return exif;
}

double bmpDpi(const std::vector<uint8_t>& b) {
  if (b.size() < 46 || b[0] != 'B' || b[1] != 'M') return 0;
  const int32_t ppm = static_cast<int32_t>(le32(&b[38]));
  return ppm > 0 ? ppm * 0.0254 : 0;
}

}  // namespace

bool loadRasterFile(const std::string& path, RasterImage& out, std::string* err) {
  out = RasterImage{};
  cv::Mat m;
  try {
    m = cv::imread(path, cv::IMREAD_UNCHANGED);
  } catch (const cv::Exception& e) {
    if (err) *err = std::string("loadRasterFile: ") + e.what();
    return false;
  }
  if (m.empty()) {
    if (err) *err = "loadRasterFile: dosya okunamadı: " + path;
    return false;
  }
  if (m.depth() == CV_16U) m.convertTo(m, CV_8U, 1.0 / 257.0);
  else if (m.depth() == CV_32F || m.depth() == CV_64F) m.convertTo(m, CV_8U, 255.0);
  else if (m.depth() != CV_8U) m.convertTo(m, CV_8U);
  if (m.channels() == 2) cv::cvtColor(m, m, cv::COLOR_GRAY2BGR);   // gray+alpha is rare; drop alpha
  if (m.channels() != 1 && m.channels() != 3 && m.channels() != 4) {
    if (err) *err = "loadRasterFile: desteklenmeyen kanal sayısı";
    return false;
  }
  out.width = m.cols;
  out.height = m.rows;
  out.channels = m.channels();
  out.stride = static_cast<std::size_t>(m.cols) * m.channels();
  out.data.resize(out.stride * m.rows);
  for (int y = 0; y < m.rows; ++y) std::memcpy(out.data.data() + y * out.stride, m.ptr(y), out.stride);
  const auto head = readHead(path, 1 << 20);
  double dpi = pngDpi(head);
  if (dpi <= 0) dpi = jpegDpi(head);
  if (dpi <= 0 && !head.empty()) dpi = tiffDpi(head.data(), head.size());
  if (dpi <= 0) dpi = bmpDpi(head);
  // 72/96 dpi in metadata is almost always a writer default, not a scan resolution
  if (dpi > 0 && (std::abs(dpi - 72) < 0.5 || std::abs(dpi - 96) < 0.5)) {
    bool ok = false;
    const double est = raster::estimateDpiFromSize(m.cols, m.rows, &ok);
    if (ok && std::abs(est - dpi) > 1) dpi = 0;
  }
  out.dpi = dpi > 0 ? dpi : 0;
  return true;
}

}  // namespace pc::drawing
