// Symbol-font repair tables. OWNER WP-1.
#include "normalize/SymbolFonts.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace pc::drawing::norm {

const std::vector<FontTable>& symbolFontTables() {
  static const std::vector<FontTable> t = {
      // Observed (enjektör, PScript5 + Inventor): TrueType symbol subset without ToUnicode, /Differences [1 /asciitilde /grave /n]
      {"*cad-symbol-subset*", {{"asciitilde", "°"}, {"grave", "±"}, {"n", "Ø"}}},
      // AutoCAD / Inventor GD&T fonts: lower-case n is the diameter sign
      {"AIGDT", {{"n", "Ø"}, {"asciitilde", "°"}, {"grave", "±"}}},
      {"GDT", {{"n", "Ø"}, {"asciitilde", "°"}, {"grave", "±"}}},
      // ISOCP / ISOCPEUR (SHX-derived TrueType): %%c etc. already mapped by glyph names; keep hook
      {"isocp", {{"Oslash", "Ø"}, {"oslash", "Ø"}}},
      // Adobe Symbol: emptyset is commonly used as the diameter sign
      {"Symbol", {{"emptyset", "Ø"}}},
  };
  return t;
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
}  // namespace

const FontTable* chooseSymbolTable(const std::string& font, const std::string& diffNames, bool symbolic) {
  const std::string lf = lower(font);
  for (auto& t : symbolFontTables()) {
    const std::string fam = t.family;
    if (fam[0] == '*') continue;
    if (lf.find(lower(fam)) != std::string::npos) return &t;
  }
  // heuristic: symbolic subset font whose /Differences names are ALL symbol glyph names of the table
  if (symbolic && !diffNames.empty()) {
    const FontTable& t = symbolFontTables()[0];
    std::istringstream is(diffNames);
    std::string n; bool all = true; int cnt = 0;
    while (is >> n) { ++cnt; if (!t.glyphToUtf8.count(n)) { all = false; break; } }
    if (all && cnt > 0) return &t;
  }
  return nullptr;
}

std::string normalizeSymbols(const std::string& s) {
  static const std::pair<const char*, const char*> rep[] = {{"\xE2\x8C\x80", "Ø"}, {"\xE2\x88\x85", "Ø"}, {"\xC3\xB8", "Ø"},
                                                            {"\xE2\x88\x92", "-"}, {"\xC2\xA0", " "}, {"\xE2\x80\x93", "-"},
                                                            // typographic ligatures (Qt/Word PDFs map them to U+FB0x)
                                                            {"\xEF\xAC\x80", "ff"}, {"\xEF\xAC\x81", "fi"}, {"\xEF\xAC\x82", "fl"},
                                                            {"\xEF\xAC\x83", "ffi"}, {"\xEF\xAC\x84", "ffl"}};
  std::string o = s;
  for (auto& [a, b] : rep) {
    std::size_t p = 0;
    const std::string A = a, B = b;
    while ((p = o.find(A, p)) != std::string::npos) { o.replace(p, A.size(), B); p += B.size(); }
  }
  return o;
}

}  // namespace pc::drawing::norm
