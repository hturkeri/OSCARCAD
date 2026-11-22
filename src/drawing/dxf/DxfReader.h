#pragma once
// WP-1 private: ASCII DXF group-code reader (R12 .. R2018). No external library (libdxfrw/dxflib are GPL).
#include <map>
#include <string>
#include <vector>

namespace pc::drawing::dxf {

struct Group { int code = 0; std::string value; };

// One entity (or table record): its type and every group code after the 0-group, in file order.
struct Ent {
  std::string type;
  std::vector<Group> g;
  std::vector<Ent> children;    // POLYLINE -> VERTEX..., INSERT -> ATTRIB...
  const std::string* first(int code) const { for (auto& x : g) if (x.code == code) return &x.value; return nullptr; }
  std::string str(int code, const std::string& def = {}) const { auto* s = first(code); return s ? *s : def; }
  double num(int code, double def = 0) const;
  int integer(int code, int def = 0) const;
  std::vector<double> all(int code) const;
};

struct Block { std::string name; double bx = 0, by = 0; int flags = 0; std::vector<Ent> ents; };
struct Layer { std::string name, linetype = "CONTINUOUS"; int color = 7; int lineweight = -3; bool off = false, frozen = false; };
struct LType { std::string name; std::vector<double> pattern; };   // DXF 49 values (dash +, gap -, dot 0)

struct DxfFile {
  std::map<std::string, std::vector<Group>> header;   // "$INSUNITS" -> groups
  std::map<std::string, Layer> layers;                // upper-case key
  std::map<std::string, LType> ltypes;                // upper-case key
  std::map<std::string, double> dimlfac;              // DIMSTYLE name (upper) -> DIMLFAC (144)
  std::map<std::string, Block> blocks;                // exact name
  std::vector<Ent> entities;
  std::string acadver;                                 // "AC1009" (R12) ...
  bool utf8 = true;                                    // strings stored UTF-8 (>= AC1021) else ANSI code page
  std::string codepage;                                // "ANSI_1254"
  std::vector<std::string> layoutNames;                // OBJECTS/LAYOUT names (paper-space layouts)
};

// Parses the file. Returns false + err on unreadable / binary / malformed input.
bool readDxf(const std::string& path, DxfFile& out, std::string* err);
// Decode a DXF string value to UTF-8 (code page, \U+XXXX, \M+nXXXX) — no %% or MTEXT handling.
std::string decodeString(const std::string& s, const DxfFile& f);
// %%c -> Ø, %%d -> °, %%p -> ±, %%u/%%o removed, %%nnn -> char.
std::string convertPercentCodes(const std::string& s);
// MTEXT formatting removal; returns one string per paragraph (\P). Stacked "\S+0^-0.5;" -> " +0/-0.5".
std::vector<std::string> stripMText(const std::string& s);
std::string upper(std::string s);

}  // namespace pc::drawing::dxf
