// PDF text decoding helpers. OWNER WP-1.
#include "pdf/PdfText.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace pc::drawing::pdf {

void appendUtf8(std::string& o, uint32_t cp) {
  if (cp < 0x80) o.push_back(static_cast<char>(cp));
  else if (cp < 0x800) { o.push_back(static_cast<char>(0xC0 | (cp >> 6))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
  else if (cp < 0x10000) {
    o.push_back(static_cast<char>(0xE0 | (cp >> 12))); o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x110000) {
    o.push_back(static_cast<char>(0xF0 | (cp >> 18))); o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

namespace {
struct NameCp { const char* n; uint32_t cp; };
// Adobe Glyph List subset: Latin-1, Latin Extended-A (Turkish), punctuation, CAD symbols, Greek.
const NameCp kAgl[] = {
  {"space", 0x20}, {"exclam", 0x21}, {"quotedbl", 0x22}, {"numbersign", 0x23}, {"dollar", 0x24}, {"percent", 0x25},
  {"ampersand", 0x26}, {"quotesingle", 0x27}, {"quoteright", 0x2019}, {"parenleft", 0x28}, {"parenright", 0x29},
  {"asterisk", 0x2A}, {"plus", 0x2B}, {"comma", 0x2C}, {"hyphen", 0x2D}, {"minus", 0x2212}, {"period", 0x2E},
  {"slash", 0x2F}, {"zero", 0x30}, {"one", 0x31}, {"two", 0x32}, {"three", 0x33}, {"four", 0x34}, {"five", 0x35},
  {"six", 0x36}, {"seven", 0x37}, {"eight", 0x38}, {"nine", 0x39}, {"colon", 0x3A}, {"semicolon", 0x3B}, {"less", 0x3C},
  {"equal", 0x3D}, {"greater", 0x3E}, {"question", 0x3F}, {"at", 0x40}, {"bracketleft", 0x5B}, {"backslash", 0x5C},
  {"bracketright", 0x5D}, {"asciicircum", 0x5E}, {"underscore", 0x5F}, {"grave", 0x60}, {"quoteleft", 0x2018},
  {"braceleft", 0x7B}, {"bar", 0x7C}, {"braceright", 0x7D}, {"asciitilde", 0x7E}, {"exclamdown", 0xA1}, {"cent", 0xA2},
  {"sterling", 0xA3}, {"currency", 0xA4}, {"yen", 0xA5}, {"brokenbar", 0xA6}, {"section", 0xA7}, {"dieresis", 0xA8},
  {"copyright", 0xA9}, {"ordfeminine", 0xAA}, {"guillemotleft", 0xAB}, {"logicalnot", 0xAC}, {"registered", 0xAE},
  {"macron", 0xAF}, {"degree", 0xB0}, {"plusminus", 0xB1}, {"twosuperior", 0xB2}, {"threesuperior", 0xB3},
  {"acute", 0xB4}, {"mu", 0xB5}, {"paragraph", 0xB6}, {"periodcentered", 0xB7}, {"cedilla", 0xB8}, {"onesuperior", 0xB9},
  {"ordmasculine", 0xBA}, {"guillemotright", 0xBB}, {"onequarter", 0xBC}, {"onehalf", 0xBD}, {"threequarters", 0xBE},
  {"questiondown", 0xBF}, {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acircumflex", 0xC2}, {"Atilde", 0xC3}, {"Adieresis", 0xC4},
  {"Aring", 0xC5}, {"AE", 0xC6}, {"Ccedilla", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9}, {"Ecircumflex", 0xCA},
  {"Edieresis", 0xCB}, {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icircumflex", 0xCE}, {"Idieresis", 0xCF}, {"Eth", 0xD0},
  {"Ntilde", 0xD1}, {"Ograve", 0xD2}, {"Oacute", 0xD3}, {"Ocircumflex", 0xD4}, {"Otilde", 0xD5}, {"Odieresis", 0xD6},
  {"multiply", 0xD7}, {"Oslash", 0xD8}, {"Ugrave", 0xD9}, {"Uacute", 0xDA}, {"Ucircumflex", 0xDB}, {"Udieresis", 0xDC},
  {"Yacute", 0xDD}, {"Thorn", 0xDE}, {"germandbls", 0xDF}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"acircumflex", 0xE2},
  {"atilde", 0xE3}, {"adieresis", 0xE4}, {"aring", 0xE5}, {"ae", 0xE6}, {"ccedilla", 0xE7}, {"egrave", 0xE8},
  {"eacute", 0xE9}, {"ecircumflex", 0xEA}, {"edieresis", 0xEB}, {"igrave", 0xEC}, {"iacute", 0xED}, {"icircumflex", 0xEE},
  {"idieresis", 0xEF}, {"eth", 0xF0}, {"ntilde", 0xF1}, {"ograve", 0xF2}, {"oacute", 0xF3}, {"ocircumflex", 0xF4},
  {"otilde", 0xF5}, {"odieresis", 0xF6}, {"divide", 0xF7}, {"oslash", 0xF8}, {"ugrave", 0xF9}, {"uacute", 0xFA},
  {"ucircumflex", 0xFB}, {"udieresis", 0xFC}, {"yacute", 0xFD}, {"thorn", 0xFE}, {"ydieresis", 0xFF},
  {"Gbreve", 0x11E}, {"gbreve", 0x11F}, {"Idotaccent", 0x130}, {"Idot", 0x130}, {"dotlessi", 0x131}, {"Scedilla", 0x15E},
  {"scedilla", 0x15F}, {"Scaron", 0x160}, {"scaron", 0x161}, {"Zcaron", 0x17D}, {"zcaron", 0x17E}, {"Ydieresis", 0x178},
  {"OE", 0x152}, {"oe", 0x153}, {"Lslash", 0x141}, {"lslash", 0x142}, {"florin", 0x192}, {"circumflex", 0x2C6},
  {"caron", 0x2C7}, {"breve", 0x2D8}, {"dotaccent", 0x2D9}, {"ring", 0x2DA}, {"ogonek", 0x2DB}, {"tilde", 0x2DC},
  {"hungarumlaut", 0x2DD}, {"endash", 0x2013}, {"emdash", 0x2014}, {"quotesinglbase", 0x201A}, {"quotedblleft", 0x201C},
  {"quotedblright", 0x201D}, {"quotedblbase", 0x201E}, {"dagger", 0x2020}, {"daggerdbl", 0x2021}, {"bullet", 0x2022},
  {"ellipsis", 0x2026}, {"perthousand", 0x2030}, {"guilsinglleft", 0x2039}, {"guilsinglright", 0x203A},
  {"fraction", 0x2044}, {"Euro", 0x20AC}, {"trademark", 0x2122}, {"fi", 0xFB01}, {"fl", 0xFB02}, {"nbspace", 0xA0},
  {"sfthyphen", 0xAD}, {"diameter", 0x2300}, {"emptyset", 0x2205}, {"infinity", 0x221E}, {"notequal", 0x2260},
  {"lessequal", 0x2264}, {"greaterequal", 0x2265}, {"approxequal", 0x2248}, {"radical", 0x221A}, {"partialdiff", 0x2202},
  {"summation", 0x2211}, {"product", 0x220F}, {"integral", 0x222B}, {"Delta", 0x2206}, {"Omega", 0x2126},
  {"lozenge", 0x25CA}, {"pi", 0x3C0}, {"mu1", 0xB5}, {"angle", 0x2220}, {"perpendicular", 0x22A5}, {"parallel", 0x2225},
  {"Alpha", 0x391}, {"Beta", 0x392}, {"Gamma", 0x393}, {"Epsilon", 0x395}, {"Zeta", 0x396}, {"Eta", 0x397}, {"Theta", 0x398},
  {"Iota", 0x399}, {"Kappa", 0x39A}, {"Lambda", 0x39B}, {"Mu", 0x39C}, {"Nu", 0x39D}, {"Xi", 0x39E}, {"Omicron", 0x39F},
  {"Pi", 0x3A0}, {"Rho", 0x3A1}, {"Sigma", 0x3A3}, {"Tau", 0x3A4}, {"Upsilon", 0x3A5}, {"Phi", 0x3A6}, {"Chi", 0x3A7},
  {"Psi", 0x3A8}, {"alpha", 0x3B1}, {"beta", 0x3B2}, {"gamma", 0x3B3}, {"delta", 0x3B4}, {"epsilon", 0x3B5}, {"zeta", 0x3B6},
  {"eta", 0x3B7}, {"theta", 0x3B8}, {"iota", 0x3B9}, {"kappa", 0x3BA}, {"lambda", 0x3BB}, {"nu", 0x3BD}, {"xi", 0x3BE},
  {"omicron", 0x3BF}, {"rho", 0x3C1}, {"sigma", 0x3C3}, {"tau", 0x3C4}, {"upsilon", 0x3C5}, {"phi", 0x3C6}, {"chi", 0x3C7},
  {"psi", 0x3C8}, {"omega", 0x3C9}, {"universal", 0x2200}, {"existential", 0x2203}, {"suchthat", 0x220B},
  {"asteriskmath", 0x2217}, {"congruent", 0x2245}, {"therefore", 0x2234}, {"similar", 0x223C}, {"arrowright", 0x2192},
  {"arrowleft", 0x2190}, {"arrowup", 0x2191}, {"arrowdown", 0x2193}, {"element", 0x2208}, {"gradient", 0x2207},
  {"minute", 0x2032}, {"second", 0x2033}, {"dotmath", 0x22C5}, {"periodcentered", 0xB7}};

const char* kStdHigh[] = {  // StandardEncoding 0xA1..0xFB (index code-0xA1)
  "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section", "currency", "quotesingle", "quotedblleft",
  "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "", "endash", "dagger", "daggerdbl", "periodcentered", "",
  "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright", "guillemotright", "ellipsis", "perthousand", "",
  "questiondown", "", "grave", "acute", "circumflex", "tilde", "macron", "breve", "dotaccent", "dieresis", "", "ring",
  "cedilla", "", "hungarumlaut", "ogonek", "caron", "emdash", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "", "AE", "", "ordfeminine", "", "", "", "", "Lslash", "Oslash", "OE", "ordmasculine", "", "", "", "", "", "ae", "", "",
  "", "dotlessi", "", "", "lslash", "oslash", "oe", "germandbls"};
const char* kAsciiNames[] = {  // 0x20..0x7E (WinAnsi names; Standard differs at 0x27, 0x60)
  "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand", "quotesingle", "parenleft", "parenright",
  "asterisk", "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four", "five", "six", "seven",
  "eight", "nine", "colon", "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G",
  "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft", "backslash",
  "bracketright", "asciicircum", "underscore", "grave", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n",
  "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde"};
const char* kWin80[] = {"Euro", "", "quotesinglbase", "florin", "quotedblbase", "ellipsis", "dagger", "daggerdbl", "circumflex",
                        "perthousand", "Scaron", "guilsinglleft", "OE", "", "Zcaron", "", "", "quoteleft", "quoteright",
                        "quotedblleft", "quotedblright", "bullet", "endash", "emdash", "tilde", "trademark", "scaron",
                        "guilsinglright", "oe", "", "zcaron", "Ydieresis"};
const char* kLatin1A0[] = {  // 0xA0..0xFF glyph names (WinAnsi = ISO Latin-1 names)
  "space", "exclamdown", "cent", "sterling", "currency", "yen", "brokenbar", "section", "dieresis", "copyright", "ordfeminine",
  "guillemotleft", "logicalnot", "hyphen", "registered", "macron", "degree", "plusminus", "twosuperior", "threesuperior",
  "acute", "mu", "paragraph", "periodcentered", "cedilla", "onesuperior", "ordmasculine", "guillemotright", "onequarter",
  "onehalf", "threequarters", "questiondown", "Agrave", "Aacute", "Acircumflex", "Atilde", "Adieresis", "Aring", "AE",
  "Ccedilla", "Egrave", "Eacute", "Ecircumflex", "Edieresis", "Igrave", "Iacute", "Icircumflex", "Idieresis", "Eth", "Ntilde",
  "Ograve", "Oacute", "Ocircumflex", "Otilde", "Odieresis", "multiply", "Oslash", "Ugrave", "Uacute", "Ucircumflex",
  "Udieresis", "Yacute", "Thorn", "germandbls", "agrave", "aacute", "acircumflex", "atilde", "adieresis", "aring", "ae",
  "ccedilla", "egrave", "eacute", "ecircumflex", "edieresis", "igrave", "iacute", "icircumflex", "idieresis", "eth", "ntilde",
  "ograve", "oacute", "ocircumflex", "otilde", "odieresis", "divide", "oslash", "ugrave", "uacute", "ucircumflex", "udieresis",
  "yacute", "thorn", "ydieresis"};
const char* kMac80[] = {
  "Adieresis", "Aring", "Ccedilla", "Eacute", "Ntilde", "Odieresis", "Udieresis", "aacute", "agrave", "acircumflex",
  "adieresis", "atilde", "aring", "ccedilla", "eacute", "egrave", "ecircumflex", "edieresis", "iacute", "igrave",
  "icircumflex", "idieresis", "ntilde", "oacute", "ograve", "ocircumflex", "odieresis", "otilde", "uacute", "ugrave",
  "ucircumflex", "udieresis", "dagger", "degree", "cent", "sterling", "section", "bullet", "paragraph", "germandbls",
  "registered", "copyright", "trademark", "acute", "dieresis", "notequal", "AE", "Oslash", "infinity", "plusminus",
  "lessequal", "greaterequal", "yen", "mu", "partialdiff", "summation", "product", "pi", "integral", "ordfeminine",
  "ordmasculine", "Omega", "ae", "oslash", "questiondown", "exclamdown", "logicalnot", "radical", "florin", "approxequal",
  "Delta", "guillemotleft", "guillemotright", "ellipsis", "space", "Agrave", "Atilde", "Otilde", "OE", "oe", "endash",
  "emdash", "quotedblleft", "quotedblright", "quoteleft", "quoteright", "divide", "lozenge", "ydieresis", "Ydieresis",
  "fraction", "currency", "guilsinglleft", "guilsinglright", "fi", "fl", "daggerdbl", "periodcentered", "quotesinglbase",
  "quotedblbase", "perthousand", "Acircumflex", "Ecircumflex", "Aacute", "Edieresis", "Egrave", "Iacute", "Icircumflex",
  "Idieresis", "Igrave", "Oacute", "Ocircumflex", "apple", "Ograve", "Uacute", "Ucircumflex", "Ugrave", "dotlessi",
  "circumflex", "tilde", "macron", "breve", "dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek", "caron"};
// Adobe Symbol built-in encoding (subset relevant for drawings)
std::string symbolName(unsigned c) {
  static const char* greekUpper = "Alpha Beta Chi Delta Epsilon Phi Gamma Eta Iota theta1 Kappa Lambda Mu Nu Omicron Pi Theta Rho Sigma Tau Upsilon sigma1 Omega Xi Psi Zeta";
  static const char* greekLower = "alpha beta chi delta epsilon phi gamma eta iota phi1 kappa lambda mu nu omicron pi theta rho sigma tau upsilon omega1 omega xi psi zeta";
  auto nth = [](const char* list, unsigned i) {
    std::string s(list); std::size_t a = 0;
    for (unsigned k = 0; k < i; ++k) { a = s.find(' ', a); if (a == std::string::npos) return std::string(); ++a; }
    const std::size_t b = s.find(' ', a);
    return s.substr(a, b == std::string::npos ? std::string::npos : b - a);
  };
  if (c >= 'A' && c <= 'Z') return nth(greekUpper, c - 'A');
  if (c >= 'a' && c <= 'z') return nth(greekLower, c - 'a');
  switch (c) {
    case 0x22: return "universal"; case 0x24: return "existential"; case 0x27: return "suchthat"; case 0x2A: return "asteriskmath";
    case 0x2D: return "minus"; case 0x40: return "congruent"; case 0x5C: return "therefore"; case 0x5E: return "perpendicular";
    case 0x60: return "radicalex"; case 0x7E: return "similar"; case 0xA2: return "minute"; case 0xA3: return "lessequal";
    case 0xA5: return "infinity"; case 0xB0: return "degree"; case 0xB1: return "plusminus"; case 0xB2: return "second";
    case 0xB3: return "greaterequal"; case 0xB4: return "multiply"; case 0xB6: return "partialdiff"; case 0xB7: return "bullet";
    case 0xB8: return "divide"; case 0xB9: return "notequal"; case 0xBB: return "approxequal"; case 0xBC: return "ellipsis";
    case 0xC6: return "emptyset"; case 0xD0: return "angle"; case 0xD1: return "gradient"; case 0xD6: return "radical";
    case 0xD7: return "dotmath"; case 0xE5: return "summation"; case 0xF2: return "integral";
    default: break;
  }
  if (c >= 0x20 && c <= 0x7E) return kAsciiNames[c - 0x20];
  return "";
}
}  // namespace

uint32_t glyphNameToUnicode(const std::string& name0) {
  if (name0.empty()) return 0;
  std::string name = name0;
  const std::size_t dot = name.find('.');
  if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
  static const std::map<std::string, uint32_t> agl = [] {
    std::map<std::string, uint32_t> m;
    for (auto& e : kAgl) m.emplace(e.n, e.cp);
    return m;
  }();
  if (auto it = agl.find(name); it != agl.end()) return it->second;
  if (name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0]))) return static_cast<unsigned char>(name[0]);
  auto hexOk = [](const std::string& s) { if (s.empty()) return false; for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false; return true; };
  if (name.size() == 7 && name.rfind("uni", 0) == 0 && hexOk(name.substr(3))) return static_cast<uint32_t>(std::strtoul(name.c_str() + 3, nullptr, 16));
  if (name.size() >= 5 && name.size() <= 7 && name[0] == 'u' && hexOk(name.substr(1))) return static_cast<uint32_t>(std::strtoul(name.c_str() + 1, nullptr, 16));
  return 0;
}

std::string encodingGlyphName(BaseEnc e, unsigned c) {
  if (c > 255) return "";
  switch (e) {
    case BaseEnc::Symbol: return symbolName(c);
    case BaseEnc::None: return "";
    case BaseEnc::Standard:
      if (c == 0x27) return "quoteright";
      if (c == 0x60) return "quoteleft";
      if (c >= 0x20 && c <= 0x7E) return kAsciiNames[c - 0x20];
      if (c >= 0xA1 && c <= 0xFB) return kStdHigh[c - 0xA1];
      return "";
    case BaseEnc::WinAnsi:
      if (c >= 0x20 && c <= 0x7E) return kAsciiNames[c - 0x20];
      if (c >= 0x80 && c <= 0x9F) return kWin80[c - 0x80];
      if (c >= 0xA0) return kLatin1A0[c - 0xA0];
      return "";
    case BaseEnc::MacRoman:
      if (c >= 0x20 && c <= 0x7E) return kAsciiNames[c - 0x20];
      if (c >= 0x80) return kMac80[c - 0x80];
      return "";
  }
  return "";
}

namespace {
// PostScript-ish tokenizer sufficient for CMaps
struct Tok { enum K { Hex, Num, Name, Word, ArrOpen, ArrClose, End } k; std::string s; };
std::vector<Tok> tokenize(const std::string& d) {
  std::vector<Tok> t;
  std::size_t i = 0;
  while (i < d.size()) {
    const char c = d[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
    if (c == '%') { while (i < d.size() && d[i] != '\n' && d[i] != '\r') ++i; continue; }
    if (c == '<' && i + 1 < d.size() && d[i + 1] == '<') { i += 2; continue; }
    if (c == '>' && i + 1 < d.size() && d[i + 1] == '>') { i += 2; continue; }
    if (c == '<') {
      std::size_t j = d.find('>', i);
      if (j == std::string::npos) break;
      std::string h; for (std::size_t k = i + 1; k < j; ++k) if (std::isxdigit(static_cast<unsigned char>(d[k]))) h.push_back(d[k]);
      t.push_back({Tok::Hex, h}); i = j + 1; continue;
    }
    if (c == '[') { t.push_back({Tok::ArrOpen, "["}); ++i; continue; }
    if (c == ']') { t.push_back({Tok::ArrClose, "]"}); ++i; continue; }
    if (c == '(') {   // literal string: skip
      int depth = 1; ++i;
      while (i < d.size() && depth) { if (d[i] == '\\') ++i; else if (d[i] == '(') ++depth; else if (d[i] == ')') --depth; ++i; }
      continue;
    }
    std::size_t j = i;
    while (j < d.size() && !std::isspace(static_cast<unsigned char>(d[j])) && !std::strchr("[]<>()/%", d[j])) ++j;
    if (c == '/') { j = i + 1; while (j < d.size() && !std::isspace(static_cast<unsigned char>(d[j])) && !std::strchr("[]<>()/%{}", d[j])) ++j; t.push_back({Tok::Name, d.substr(i + 1, j - i - 1)}); i = j; continue; }
    if (j == i) { ++i; continue; }
    const std::string w = d.substr(i, j - i);
    t.push_back({std::isdigit(static_cast<unsigned char>(w[0])) || w[0] == '-' ? Tok::Num : Tok::Word, w});
    i = j;
  }
  return t;
}
uint32_t hexVal(const std::string& h) { return static_cast<uint32_t>(std::strtoul(h.substr(0, 8).c_str(), nullptr, 16)); }
std::string utf16HexToUtf8(const std::string& h) {
  std::string o;
  std::vector<uint32_t> u;
  for (std::size_t i = 0; i + 4 <= h.size(); i += 4) u.push_back(static_cast<uint32_t>(std::strtoul(h.substr(i, 4).c_str(), nullptr, 16)));
  if (h.size() == 2) u.push_back(static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16)));
  for (std::size_t i = 0; i < u.size(); ++i) {
    uint32_t cp = u[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < u.size()) { cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00); ++i; }
    appendUtf8(o, cp);
  }
  return o;
}
}  // namespace

ToUnicodeCMap parseToUnicode(const std::string& data) {
  ToUnicodeCMap cm;
  const auto t = tokenize(data);
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i].k != Tok::Word) continue;
    if (t[i].s == "begincodespacerange") {
      for (++i; i + 1 < t.size() && t[i].k == Tok::Hex; i += 2)
        cm.codespace.push_back({hexVal(t[i].s), hexVal(t[i + 1].s), static_cast<int>((t[i].s.size() + 1) / 2)});
      --i;
    } else if (t[i].s == "beginbfchar") {
      for (++i; i + 1 < t.size() && t[i].k == Tok::Hex; i += 2) cm.map[hexVal(t[i].s)] = utf16HexToUtf8(t[i + 1].s);
      --i;
    } else if (t[i].s == "beginbfrange") {
      ++i;
      while (i + 2 < t.size() && t[i].k == Tok::Hex && t[i + 1].k == Tok::Hex) {
        const uint32_t lo = hexVal(t[i].s), hi = hexVal(t[i + 1].s);
        if (t[i + 2].k == Tok::Hex) {
          // increment last UTF-16 unit
          const std::string dst = t[i + 2].s;
          if (hi >= lo && hi - lo < 65536) {
            const std::string prefix = dst.size() > 4 ? dst.substr(0, dst.size() - 4) : std::string();
            const uint32_t base = static_cast<uint32_t>(std::strtoul(dst.substr(dst.size() >= 4 ? dst.size() - 4 : 0).c_str(), nullptr, 16));
            for (uint32_t c = lo; c <= hi; ++c) {
              char buf[8]; std::snprintf(buf, sizeof buf, "%04X", (base + (c - lo)) & 0xFFFF);
              cm.map[c] = utf16HexToUtf8(prefix + buf);
            }
          }
          i += 3;
        } else if (t[i + 2].k == Tok::ArrOpen) {
          std::size_t j = i + 3; uint32_t c = lo;
          while (j < t.size() && t[j].k == Tok::Hex) { if (c <= hi) cm.map[c] = utf16HexToUtf8(t[j].s); ++c; ++j; }
          i = (j < t.size() && t[j].k == Tok::ArrClose) ? j + 1 : j;
        } else {
          i += 3;
        }
      }
      --i;
    }
  }
  return cm;
}

std::vector<std::pair<uint32_t, int>> splitCodes(const std::string& bytes, const std::vector<CMapRange>& cs, int defaultBytes) {
  std::vector<std::pair<uint32_t, int>> out;
  std::size_t i = 0;
  while (i < bytes.size()) {
    int n = 0;
    if (!cs.empty()) {
      for (int len = 1; len <= 4 && !n; ++len) {
        if (i + len > bytes.size()) break;
        uint32_t v = 0; for (int k = 0; k < len; ++k) v = (v << 8) | static_cast<unsigned char>(bytes[i + k]);
        for (auto& r : cs) if (r.bytes == len && v >= r.lo && v <= r.hi) { n = len; break; }
      }
    }
    if (!n) n = std::min<int>(defaultBytes, static_cast<int>(bytes.size() - i));
    uint32_t v = 0; for (int k = 0; k < n; ++k) v = (v << 8) | static_cast<unsigned char>(bytes[i + k]);
    out.push_back({v, n});
    i += n;
  }
  return out;
}

double standardWidth(const std::string& baseFont, uint32_t u) {
  if (baseFont.find("Courier") != std::string::npos) return 600;
  // Helvetica AFM widths 0x20..0x7E
  static const short hv[95] = {278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556,
                               556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278,
                               500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469,
                               556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500,
                               278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
  if (u >= 0x20 && u <= 0x7E) return hv[u - 0x20];
  return 556;
}

}  // namespace pc::drawing::pdf
