#pragma once
// WP-1 private: PDF text decoding helpers (encodings, Adobe Glyph List subset, ToUnicode CMap). No qpdf types.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pc::drawing::pdf {

// Glyph name -> Unicode code point (AGL subset + uniXXXX / uXXXX[XX] + "name.suffix"); 0 if unknown.
uint32_t glyphNameToUnicode(const std::string& name);
// Built-in encodings: code -> glyph name ("" = undefined).
enum class BaseEnc { Standard, WinAnsi, MacRoman, Symbol, None };
std::string encodingGlyphName(BaseEnc e, unsigned code);
void appendUtf8(std::string& o, uint32_t cp);

struct CMapRange { uint32_t lo, hi; int bytes; };
struct ToUnicodeCMap {
  std::vector<CMapRange> codespace;           // empty -> assume `defaultBytes`
  std::map<uint32_t, std::string> map;         // code -> UTF-8
  bool empty() const { return map.empty(); }
};
// Parses bfchar / bfrange / codespacerange of a (decoded) CMap stream.
ToUnicodeCMap parseToUnicode(const std::string& data);
// Split a show-text byte string into codes using the codespace (or fixed `defaultBytes`).
std::vector<std::pair<uint32_t, int>> splitCodes(const std::string& bytes, const std::vector<CMapRange>& codespace, int defaultBytes);
// Approximate Helvetica advance widths (1/1000 em) for standard-14 fonts without /Widths.
double standardWidth(const std::string& baseFont, uint32_t unicode);

}  // namespace pc::drawing::pdf
