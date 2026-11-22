// ASCII DXF group-code reader. OWNER WP-1.
#include "dxf/DxfReader.h"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pc::drawing::dxf {

double Ent::num(int code, double def) const {
  auto* s = first(code);
  if (!s) return def;
  char* end = nullptr;
  const double v = std::strtod(s->c_str(), &end);
  return end == s->c_str() ? def : v;
}
int Ent::integer(int code, int def) const { auto* s = first(code); return s ? std::atoi(s->c_str()) : def; }
std::vector<double> Ent::all(int code) const {
  std::vector<double> v;
  for (auto& x : g) if (x.code == code) v.push_back(std::strtod(x.value.c_str(), nullptr));
  return v;
}
std::string upper(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }

namespace {
std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}
void appendUtf8(std::string& o, unsigned cp) {
  if (cp < 0x80) o.push_back(static_cast<char>(cp));
  else if (cp < 0x800) { o.push_back(static_cast<char>(0xC0 | (cp >> 6))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
  else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12))); o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18))); o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}
bool validUtf8(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || i + n > s.size()) return false;
    for (int k = 1; k < n; ++k) if ((static_cast<unsigned char>(s[i + k]) >> 6) != 2) return false;
    i += n;
  }
  return true;
}
// Windows-1252 0x80..0x9F
const unsigned short k1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039,
                                   0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                   0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
unsigned ansiToUnicode(unsigned char c, bool turkish) {
  if (c < 0x80) return c;
  if (c < 0xA0) return k1252[c - 0x80];
  if (turkish) {
    switch (c) { case 0xD0: return 0x011E; case 0xDD: return 0x0130; case 0xDE: return 0x015E;
                 case 0xF0: return 0x011F; case 0xFD: return 0x0131; case 0xFE: return 0x015F; default: break; }
  }
  return c;   // Latin-1
}
}  // namespace

std::string decodeString(const std::string& s, const DxfFile& f) {
  std::string base;
  if (f.utf8 && validUtf8(s)) base = s;
  else {
    const bool tr = f.codepage.find("1254") != std::string::npos;
    for (unsigned char c : s) appendUtf8(base, ansiToUnicode(c, tr));
  }
  // \U+XXXX and \M+nXXXX escapes
  std::string o;
  for (std::size_t i = 0; i < base.size(); ++i) {
    if (base[i] == '\\' && i + 6 < base.size() + 0 && (base[i + 1] == 'U' || base[i + 1] == 'u') && base[i + 2] == '+') {
      const std::string hex = base.substr(i + 3, 4);
      bool ok = hex.size() == 4;
      for (char h : hex) ok = ok && std::isxdigit(static_cast<unsigned char>(h));
      if (ok) { appendUtf8(o, static_cast<unsigned>(std::strtoul(hex.c_str(), nullptr, 16))); i += 6; continue; }
    }
    if (base[i] == '\\' && i + 7 < base.size() + 0 && (base[i + 1] == 'M' || base[i + 1] == 'm') && base[i + 2] == '+') {
      // \M+nXXXX: multibyte (DBCS) — rare in our domain; keep the code as a replacement char
      o += "\xEF\xBF\xBD"; i += 7; continue;
    }
    o.push_back(base[i]);
  }
  return o;
}

std::string convertPercentCodes(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && s[i + 1] == '%') {
      const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
      if (c == 'c') { o += "Ø"; i += 2; continue; }
      if (c == 'd') { o += "°"; i += 2; continue; }
      if (c == 'p') { o += "±"; i += 2; continue; }
      if (c == 'u' || c == 'o' || c == 'k') { i += 2; continue; }
      if (c == '%') { o += "%"; i += 2; continue; }
      if (std::isdigit(static_cast<unsigned char>(s[i + 2]))) {
        std::size_t j = i + 2; unsigned v = 0; int n = 0;
        while (j < s.size() && n < 3 && std::isdigit(static_cast<unsigned char>(s[j]))) { v = v * 10 + (s[j] - '0'); ++j; ++n; }
        appendUtf8(o, v); i = j - 1; continue;
      }
    }
    o.push_back(s[i]);
  }
  // Unicode diameter sign U+2300 -> Ø (contract: symbols normalised)
  std::string r; const std::string dia = "\xE2\x8C\x80";
  for (std::size_t i = 0; i < o.size(); ++i) {
    if (o.compare(i, dia.size(), dia) == 0) { r += "Ø"; i += dia.size() - 1; continue; }
    r.push_back(o[i]);
  }
  return r;
}

std::vector<std::string> stripMText(const std::string& s) {
  std::vector<std::string> paras(1);
  auto skipTo = [&](std::size_t& i, char c) { while (i < s.size() && s[i] != c) ++i; };
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '{' || c == '}') continue;
    if (c != '\\' || i + 1 >= s.size()) { paras.back().push_back(c); continue; }
    const char k = s[++i];
    switch (k) {
      case 'P': paras.emplace_back(); break;
      case 'p': skipTo(i, ';'); break;  // paragraph formatting
      case '~': paras.back().push_back(' '); break;
      case '\\': case '{': case '}': paras.back().push_back(k); break;
      case 'L': case 'l': case 'O': case 'o': case 'K': case 'k': case 'N': break;
      case 'f': case 'F': case 'H': case 'h': case 'C': case 'c': case 'A': case 'a': case 'Q': case 'q': case 'W': case 'w':
      case 'T': case 't':
        skipTo(i, ';'); break;
      case 'S': {   // stacked text up to ';'  a^b, a/b, a#b
        std::size_t j = i + 1; std::string body;
        while (j < s.size() && s[j] != ';') body.push_back(s[j++]);
        i = j;
        std::size_t sep = body.find_first_of("^/#");
        std::string top = sep == std::string::npos ? body : body.substr(0, sep);
        std::string bot = sep == std::string::npos ? std::string() : body.substr(sep + 1);
        auto tr = [](std::string x) { while (!x.empty() && x.front() == ' ') x.erase(x.begin()); while (!x.empty() && x.back() == ' ') x.pop_back(); return x; };
        top = tr(top); bot = tr(bot);
        if (sep != std::string::npos && body[sep] == '^') {   // tolerance stack
          auto& p = paras.back();
          if (!p.empty() && p.back() != ' ') p.push_back(' ');
          p += top + "/" + bot;
        } else {
          paras.back() += top + (bot.empty() ? "" : "/" + bot);
        }
        break;
      }
      default: paras.back().push_back(k); break;   // unknown escape: keep char
    }
  }
  return paras;
}

bool readDxf(const std::string& path, DxfFile& out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "DXF açılamadı: " + path; return false; }
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (content.rfind("AutoCAD Binary DXF", 0) == 0) { if (err) *err = "İkili DXF desteklenmiyor (faz 2)"; return false; }
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF) content.erase(0, 3);   // BOM
  std::vector<Group> gs;
  {
    std::size_t pos = 0;
    auto nextLine = [&](std::string& line) {
      if (pos >= content.size()) return false;
      std::size_t e = content.find('\n', pos);
      if (e == std::string::npos) e = content.size();
      line.assign(content, pos, e - pos);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      pos = e + 1;
      return true;
    };
    std::string a, b;
    while (nextLine(a)) {
      if (!nextLine(b)) break;
      const std::string t = trim(a);
      if (t.empty()) continue;
      char* end = nullptr;
      const long code = std::strtol(t.c_str(), &end, 10);
      if (end == t.c_str() || *end) { if (err) *err = "DXF grup kodu bozuk: '" + t + "'"; return false; }
      Group g; g.code = static_cast<int>(code);
      // keep text values verbatim (minus CR); trim others
      g.value = (g.code == 1 || g.code == 3 || (g.code >= 300 && g.code < 310)) ? b : trim(b);
      gs.push_back(std::move(g));
    }
  }
  if (gs.empty()) { if (err) *err = "DXF boş"; return false; }

  // group into records by 0-code
  std::vector<Ent> recs;
  for (auto& g : gs) {
    if (g.code == 0) { recs.push_back(Ent{trim(g.value), {}, {}}); continue; }
    if (!recs.empty()) recs.back().g.push_back(g);
  }
  enum class Sec { None, Header, Tables, Blocks, Entities, Objects, Other } sec = Sec::None;
  Block* curBlock = nullptr;
  std::vector<Ent>* target = nullptr;
  Ent* openPoly = nullptr;   // POLYLINE / INSERT with ATTRIBs collecting children
  bool sawSection = false;
  for (std::size_t i = 0; i < recs.size(); ++i) {
    Ent& r = recs[i];
    if (r.type == "SECTION") {
      sawSection = true;
      const std::string n = r.str(2);
      sec = n == "HEADER" ? Sec::Header : n == "TABLES" ? Sec::Tables : n == "BLOCKS" ? Sec::Blocks
          : n == "ENTITIES" ? Sec::Entities : n == "OBJECTS" ? Sec::Objects : Sec::Other;
      if (sec == Sec::Header) {
        std::string var;
        for (auto& g : r.g) {
          if (g.code == 2 && g.value == "HEADER" && var.empty()) continue;
          if (g.code == 9) { var = g.value; out.header[var]; continue; }
          if (!var.empty()) out.header[var].push_back(g);
        }
      }
      target = sec == Sec::Entities ? &out.entities : nullptr;
      openPoly = nullptr;
      continue;
    }
    if (r.type == "ENDSEC") { sec = Sec::None; target = nullptr; openPoly = nullptr; continue; }
    if (r.type == "EOF") break;
    switch (sec) {
      case Sec::Tables:
        if (r.type == "LAYER") {
          Layer l; l.name = r.str(2); l.linetype = r.str(6, "CONTINUOUS"); l.color = r.integer(62, 7);
          l.lineweight = r.integer(370, -3); l.off = l.color < 0; l.frozen = (r.integer(70) & 1) != 0;
          if (!l.name.empty()) out.layers[upper(l.name)] = l;
        } else if (r.type == "LTYPE") {
          LType t; t.name = r.str(2); t.pattern = r.all(49);
          if (!t.name.empty()) out.ltypes[upper(t.name)] = t;
        } else if (r.type == "DIMSTYLE") {
          const std::string n = r.str(2);
          if (!n.empty()) out.dimlfac[upper(n)] = r.num(144, 1.0);
        }
        break;
      case Sec::Blocks:
        if (r.type == "BLOCK") {
          Block b; b.name = r.str(2); b.bx = r.num(10); b.by = r.num(20); b.flags = r.integer(70);
          curBlock = &(out.blocks[b.name] = b);
          target = &curBlock->ents; openPoly = nullptr;
        } else if (r.type == "ENDBLK") { curBlock = nullptr; target = nullptr; openPoly = nullptr; }
        else if (target) goto entity;
        break;
      case Sec::Entities: goto entity;
      case Sec::Objects:
        if (r.type == "LAYOUT") { for (auto& g : r.g) if (g.code == 1) { out.layoutNames.push_back(g.value); break; } }
        break;
      default: break;
    }
    continue;
  entity:
    if (!target) continue;
    if (r.type == "VERTEX" || r.type == "ATTRIB") { if (openPoly) openPoly->children.push_back(r); continue; }
    if (r.type == "SEQEND") { openPoly = nullptr; continue; }
    target->push_back(r);
    openPoly = nullptr;
    if (r.type == "POLYLINE" || (r.type == "INSERT" && r.integer(66) == 1)) openPoly = &target->back();
  }
  if (!sawSection) { if (err) *err = "DXF değil (SECTION yok)"; return false; }
  if (auto it = out.header.find("$ACADVER"); it != out.header.end() && !it->second.empty()) out.acadver = it->second[0].value;
  if (auto it = out.header.find("$DWGCODEPAGE"); it != out.header.end() && !it->second.empty()) out.codepage = it->second[0].value;
  out.utf8 = out.acadver.empty() || out.acadver >= "AC1021";
  return true;
}

}  // namespace pc::drawing::dxf
