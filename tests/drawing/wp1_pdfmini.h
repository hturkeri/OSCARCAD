#pragma once
// WP-1 test helper: writes a minimal single-page PDF (one content stream, Helvetica WinAnsi) for ingest tests.
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace wp1 {
inline void writeMiniPdf(const std::string& path, const std::string& content, double wPt = 842, double hPt = 595) {
  std::vector<std::string> objs = {
      "<< /Type /Catalog /Pages 2 0 R >>",
      "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + std::to_string(wPt) + " " + std::to_string(hPt) +
          "] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
      "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream",
      "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"};
  std::ostringstream o;
  o << "%PDF-1.4\n";
  std::vector<std::size_t> off;
  for (std::size_t i = 0; i < objs.size(); ++i) {
    off.push_back(static_cast<std::size_t>(o.tellp()));
    o << (i + 1) << " 0 obj\n" << objs[i] << "\nendobj\n";
  }
  const auto xref = static_cast<std::size_t>(o.tellp());
  o << "xref\n0 " << objs.size() + 1 << "\n0000000000 65535 f \n";
  for (auto x : off) { char b[32]; std::snprintf(b, sizeof b, "%010zu 00000 n \n", x); o << b; }
  o << "trailer\n<< /Size " << objs.size() + 1 << " /Root 1 0 R >>\nstartxref\n" << xref << "\n%%EOF\n";
  std::ofstream(path, std::ios::binary) << o.str();
}
}  // namespace wp1
