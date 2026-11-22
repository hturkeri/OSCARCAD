#pragma once
// WP-1 private: symbol-font repair tables (SPEC §2.4). Extensible: add a FontTable entry.
#include <map>
#include <string>
#include <vector>

namespace pc::drawing::norm {

struct FontTable {
  const char* family;               // case-insensitive substring of the font name; "*cad-symbol-subset*" = heuristic entry
  std::map<std::string, std::string> glyphToUtf8;   // glyph name -> replacement (UTF-8)
};
const std::vector<FontTable>& symbolFontTables();
// Chooses a table for a run: font name + glyph-name set of its /Differences (space separated, sorted) + symbolic flag.
// Returns nullptr if no repair applies.
const FontTable* chooseSymbolTable(const std::string& font, const std::string& diffNames, bool symbolic);
// Canonical symbol spelling: "⌀"/"∅"/"ø" -> "Ø", "−" -> "-", NBSP -> space.
std::string normalizeSymbols(const std::string& s);

}  // namespace pc::drawing::norm
