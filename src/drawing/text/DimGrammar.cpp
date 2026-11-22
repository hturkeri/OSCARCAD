// Dimension text grammar (SPEC §2.10) — parseDimText / rankDimTextHypotheses / formatDimText. OWNER WP-2.
// Pure C++, no optional dependency. Works on UTF-32 internally so that Ø ± ° □ ↧ are single symbols.
//
// Two layers:
//   1. normalisation (always): ⌀ ∅ ø %%c -> Ø ; %%p +/- +- -> ± ; %%d º ˚ -> ° ; × * -> x ; − – — -> - ;
//      Turkish decimal comma "12,5" -> 12.5 ; whitespace collapsed.
//   2. OCR confusion-set corrections (ctx.fromOcr only), each logged in DimText::corrections and penalising conf by
//      kConfCorrectionPenalty: O/o/0/@/Q -> Ø before a number, l/I/| -> 1, S -> 5, B -> 8, O -> 0 inside numbers,
//      lone "+t" -> ± (symmetric tolerance), trailing o/0 -> ° in angular context, missing ° in chamfers.
#include "pc/drawing/Confidence.h"
#include "pc/drawing/DimGrammar.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pc::drawing {
namespace {

constexpr char32_t kDia = U'Ø';    // Ø
constexpr char32_t kPm = U'±';     // ±
constexpr char32_t kDeg = U'°';    // °
constexpr char32_t kSq = U'□';     // □
constexpr char32_t kDepth = U'↧';  // ↧

// ------------------------------------------------------------------------------------------------ UTF-8 <-> UTF-32
std::u32string decodeUtf8(const std::string& s) {
  std::u32string out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = 0;
    int n = 0;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else { out.push_back(U'�'); ++i; continue; }
    if (i + n > s.size()) { out.push_back(U'�'); break; }
    bool ok = true;
    for (int k = 1; k < n; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) { out.push_back(U'�'); ++i; continue; }
    out.push_back(cp);
    i += n;
  }
  return out;
}

std::string encodeUtf8(const std::u32string& s) {
  std::string out;
  for (char32_t cp : s) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
bool isAlpha(char32_t c) {
  return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'İ' || c == U'ı' || c == U'Ç' ||
         c == U'ç' || c == U'Ğ' || c == U'ğ' || c == U'Ö' || c == U'ö' || c == U'Ş' ||
         c == U'ş' || c == U'Ü' || c == U'ü';
}
bool isSpace(char32_t c) { return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U' '; }
char32_t upper(char32_t c) {
  if (c >= U'a' && c <= U'z') return c - 32;
  switch (c) {
    case U'ı': return U'I';   // ı
    case U'İ': return U'I';   // İ
    case U'ç': return U'Ç';
    case U'ğ': return U'Ğ';
    case U'ö': return U'Ö';
    case U'ş': return U'Ş';
    case U'ü': return U'Ü';
    default: return c;
  }
}

// ------------------------------------------------------------------------------------------------ normalisation
std::u32string normalize(const std::u32string& in) {
  std::u32string s;
  for (std::size_t i = 0; i < in.size(); ++i) {
    char32_t c = in[i];
    // DXF %% codes
    if (c == U'%' && i + 2 < in.size() && in[i + 1] == U'%') {
      const char32_t k = upper(in[i + 2]);
      if (k == U'C') { s.push_back(kDia); i += 2; continue; }
      if (k == U'P') { s.push_back(kPm); i += 2; continue; }
      if (k == U'D') { s.push_back(kDeg); i += 2; continue; }
    }
    switch (c) {
      case U'⌀': case U'∅': case U'ø': case U'⍉': c = kDia; break;
      case U'º': case U'˚': case U'⁰': case U'∘': c = kDeg; break;
      case U'×': case U'✕': case U'✖': c = U'x'; break;
      case U'−': case U'–': case U'—': case U'‐': case U'‑': c = U'-'; break;
      case U'″': case U'”': case U'“': c = U'"'; break;
      case U'′': case U'’': case U'‘': c = U'\''; break;
      case U'◻': case U'▢': c = kSq; break;
      case U'↓': c = kDepth; break;
      default: break;
    }
    if (isSpace(c)) c = U' ';
    s.push_back(c);
  }
  // "+/-" and "+-" -> ±
  std::u32string t;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == U'+' && i + 2 < s.size() && s[i + 1] == U'/' && s[i + 2] == U'-') { t.push_back(kPm); i += 2; continue; }
    if (s[i] == U'+' && i + 1 < s.size() && s[i + 1] == U'-' && (i + 2 >= s.size() || isDigit(s[i + 2]) || s[i + 2] == U'.' || s[i + 2] == U' ')) {
      t.push_back(kPm); i += 1; continue;
    }
    t.push_back(s[i]);
  }
  // decimal comma between digits -> '.'
  for (std::size_t i = 1; i + 1 < t.size(); ++i)
    if (t[i] == U',' && isDigit(t[i - 1]) && isDigit(t[i + 1])) t[i] = U'.';
  // collapse whitespace, trim
  std::u32string u;
  for (char32_t c : t) {
    if (c == U' ' && (u.empty() || u.back() == U' ')) continue;
    u.push_back(c);
  }
  while (!u.empty() && u.back() == U' ') u.pop_back();
  // no space between sign/± and the number it applies to: "± 0.1" -> "±0.1", "+ 0.05" -> "+0.05"
  std::u32string v;
  for (std::size_t i = 0; i < u.size(); ++i) {
    v.push_back(u[i]);
    if ((u[i] == kPm || u[i] == U'+') && i + 2 < u.size() && u[i + 1] == U' ' && (isDigit(u[i + 2]) || u[i + 2] == U'.')) ++i;
  }
  return v;
}

struct Corrector {
  std::vector<std::string>* log;
  int count = 0;
  void add(const std::string& what) { log->push_back(what); ++count; }
};

bool numCharAt(const std::u32string& s, std::size_t i) { return i < s.size() && (isDigit(s[i]) || s[i] == U'.'); }

// OCR confusion corrections that can be decided locally (before parsing).
std::u32string ocrCorrect(std::u32string s, const DimTextContext& ctx, Corrector& cor) {
  // 1) l I | ! -> 1, S -> 5, B -> 8, O/o -> 0, Z -> 2 when inside / adjacent to a number (not a fit class like "H7").
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char32_t c = s[i];
    const bool prevNum = i > 0 && numCharAt(s, i - 1);
    const bool nextNum = numCharAt(s, i + 1);
    const bool prevAlpha = i > 0 && isAlpha(s[i - 1]);
    const bool nextAlpha = i + 1 < s.size() && isAlpha(s[i + 1]);
    char32_t to = 0;
    if ((c == U'l' || c == U'I' || c == U'|' || c == U'!') && ((prevNum && !nextAlpha) || (nextNum && !prevAlpha))) to = U'1';
    else if (c == U'S' && prevNum && nextNum) to = U'5';
    else if (c == U'S' && nextNum && !prevAlpha && !(i + 1 < s.size() && (s[i + 1] == U'0'))) to = U'5';
    else if (c == U'B' && ((prevNum && !nextAlpha) || (nextNum && !prevAlpha && i > 0 && s[i - 1] == U'M'))) to = U'8';
    else if (c == U'B' && nextNum && !prevAlpha) to = U'8';
    else if (c == U'S' && prevNum && (i + 1 >= s.size() || s[i + 1] == U' ' || s[i + 1] == U'-')) to = U'5';
    else if (c == U'B' && i > 0 && s[i - 1] == U'M' && (i < 2 || !isAlpha(s[i - 2])) && i + 1 < s.size() &&
             (s[i + 1] == U'x' || s[i + 1] == U'X' || s[i + 1] == U' ' || s[i + 1] == U'-')) to = U'8';
    else if ((c == U'O' || c == U'o') && prevNum && nextNum) to = U'0';
    else if (c == U'Z' && prevNum && nextNum) to = U'2';
    if (to) {
      std::string m = encodeUtf8(std::u32string(1, c)) + "->" + static_cast<char>(to);
      s[i] = to;
      cor.add(m);
    }
  }
  // 2) Ø confusions at the start of a dimension token: start of text, after "Nx ", after "S" (SØ), after "(".
  auto tokenStart = [&](std::size_t i) {
    if (i == 0) return true;
    std::size_t j = i;
    if (s[j - 1] == U' ') --j;
    if (j == 0) return true;
    const char32_t p = s[j - 1];
    if (p == U'(') return true;
    if ((p == U'x' || p == U'X') && j >= 2 && isDigit(s[j - 2])) return true;
    if (p == U'S' && j == 1) return true;
    return false;
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char32_t c = s[i];
    if (!(c == U'O' || c == U'o' || c == U'0' || c == U'@' || c == U'Q' || c == U'Ð' || c == U'Θ' ||
          c == U'Φ' || c == U'Ф' || c == U'ð'))
      continue;
    if (!tokenStart(i)) continue;
    std::size_t j = i + 1;
    bool spaced = false;
    if (j < s.size() && s[j] == U' ' && c != U'0') { ++j; spaced = true; }
    if (j >= s.size() || !isDigit(s[j])) continue;
    // "0.5" is a number, "05"/"020" is not -> Ø ; "O 20" -> Ø20
    if (c == U'0' && spaced) continue;
    s[i] = kDia;
    if (spaced) s.erase(i + 1, 1);
    std::string m = encodeUtf8(std::u32string(1, c)) + "->Ø";
    if (ctx.nearCircleOrCylinder) m += " (circle context)";
    cor.add(m);
  }
  // 3) 't' / 'T' between number and number -> ± ("12t0.1")
  for (std::size_t i = 1; i + 1 < s.size(); ++i)
    if ((s[i] == U't' || s[i] == U'‡' || s[i] == U'f') && numCharAt(s, i - 1) && numCharAt(s, i + 1)) {
      s[i] = kPm;
      cor.add("t->±");
    }
  // 4) degree confusions: number followed by o/O/* at end or before space/± -> °
  for (std::size_t i = 1; i < s.size(); ++i) {
    const char32_t c = s[i];
    if (!(c == U'o' || c == U'O' || c == U'*' || c == U'ª')) continue;
    if (!isDigit(s[i - 1])) continue;
    const bool end = i + 1 >= s.size() || s[i + 1] == U' ' || s[i + 1] == kPm || s[i + 1] == U'+' || s[i + 1] == U'-';
    if (!end) continue;
    s[i] = kDeg;
    cor.add(encodeUtf8(std::u32string(1, c)) + "->°");
  }
  // 5) fit class digit confusions: "<number> ho" / "H?" -> "h6": a 1-2 letter token after a number whose last char
  //    is o/b/G (LSTM reads a small 6 as o/b) at a token end.
  for (std::size_t i = 1; i < s.size(); ++i) {
    const char32_t c = s[i];
    if (!(c == U'o' || c == U'b' || c == U'G')) continue;
    const bool end = i + 1 >= s.size() || s[i + 1] == U' ' || s[i + 1] == U'/' || s[i + 1] == U'(';
    if (!end || !isAlpha(s[i - 1])) continue;
    std::size_t k = i - 1;
    if (k > 0 && isAlpha(s[k - 1])) --k;
    if (k == 0 || (k > 0 && isAlpha(s[k - 1]))) continue;
    std::size_t j = k;
    if (s[j - 1] == U' ' && j >= 2) --j;
    if (!isDigit(s[j - 1]) && s[j - 1] != U'/') continue;
    const std::u32string tok = s.substr(k, i - k);
    if (tok == U"D" || tok == U"d" || tok == U"t" || tok == U"T") continue;   // "DO"/"to" are words, not fits
    s[i] = U'6';
    cor.add(encodeUtf8(std::u32string(1, c)) + "->6 (fit)");
  }
  // 6) hole callout without Ø: "4x 10.2" next to a circle -> "4x Ø10.2"
  if (ctx.nearCircleOrCylinder) {
    std::size_t i = 0;
    while (i < s.size() && isDigit(s[i])) ++i;
    if (i > 0 && i < s.size() && (s[i] == U'x' || s[i] == U'X')) {
      std::size_t j = i + 1;
      if (j < s.size() && s[j] == U' ') ++j;
      if (j < s.size() && isDigit(s[j])) {
        s.insert(j, 1, kDia);
        cor.add("+Ø (hole count, circle context)");
      }
    }
  }
  return s;
}

// ------------------------------------------------------------------------------------------------ parser
struct Parser {
  const std::u32string& s;
  std::size_t p = 0;
  explicit Parser(const std::u32string& str) : s(str) {}
  bool eof() const { return p >= s.size(); }
  char32_t peek(std::size_t k = 0) const { return p + k < s.size() ? s[p + k] : 0; }
  void ws() { while (!eof() && s[p] == U' ') ++p; }
  bool eat(char32_t c) { if (peek() == c) { ++p; return true; } return false; }
  bool eatCi(char32_t c) { if (!eof() && upper(s[p]) == upper(c)) { ++p; return true; } return false; }
  // keyword, case-insensitive, must end at a non-letter
  bool keyword(const char32_t* kw) {
    std::size_t q = p;
    for (const char32_t* k = kw; *k; ++k, ++q)
      if (q >= s.size() || upper(s[q]) != upper(*k)) return false;
    if (q < s.size() && isAlpha(s[q])) return false;
    p = q;
    return true;
  }
  bool number(double& v, std::string* text = nullptr) {
    std::size_t q = p;
    std::string t;
    while (q < s.size() && isDigit(s[q])) t.push_back(static_cast<char>(s[q++]));
    if (q < s.size() && s[q] == U'.' && q + 1 < s.size() && isDigit(s[q + 1])) {
      t.push_back('.');
      ++q;
      while (q < s.size() && isDigit(s[q])) t.push_back(static_cast<char>(s[q++]));
    } else if (!t.empty() && q < s.size() && s[q] == U'.' && !(q + 1 < s.size() && isAlpha(s[q + 1]))) {
      ++q;   // "12." trailing dot
    }
    if (t.empty()) return false;
    p = q;
    v = std::strtod(t.c_str(), nullptr);
    if (text) *text = t;
    return true;
  }
  bool integer(int& v) {
    std::size_t q = p;
    std::string t;
    while (q < s.size() && isDigit(s[q])) t.push_back(static_cast<char>(s[q++]));
    if (t.empty() || t.size() > 4) return false;
    if (q < s.size() && s[q] == U'.' && q + 1 < s.size() && isDigit(s[q + 1])) return false;
    p = q;
    v = std::atoi(t.c_str());
    return true;
  }
  std::u32string rest() const { return p < s.size() ? s.substr(p) : std::u32string(); }
};

std::string fmtNum(double v) {
  if (std::abs(v) < 5e-7) return "0";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.4f", v);
  std::string t = buf;
  if (t.find('.') != std::string::npos) {
    while (!t.empty() && t.back() == '0') t.pop_back();
    if (!t.empty() && t.back() == '.') t.pop_back();
  }
  if (t == "-0") t = "0";
  return t;
}

// ISO coarse pitches (M1..M68)
double coarsePitch(double d) {
  static const double tab[][2] = {{1, .25},  {1.2, .25}, {1.4, .3},  {1.6, .35}, {2, .4},  {2.5, .45}, {3, .5},
                                  {3.5, .6}, {4, .7},    {5, .8},    {6, 1},     {7, 1},   {8, 1.25},  {10, 1.5},
                                  {12, 1.75}, {14, 2},   {16, 2},    {18, 2.5},  {20, 2.5}, {22, 2.5}, {24, 3},
                                  {27, 3},   {30, 3.5},  {33, 3.5},  {36, 4},    {39, 4},  {42, 4.5},  {45, 4.5},
                                  {48, 5},   {52, 5},    {56, 5.5},  {60, 5.5},  {64, 6},  {68, 6}};
  for (auto& r : tab)
    if (std::abs(r[0] - d) < 1e-6) return r[1];
  return 0;
}
// BSP (G) pipe thread major diameters, inch fraction -> mm ; TPI
bool pipeThread(const std::string& frac, double& major, double& tpi) {
  static const struct { const char* f; double d, tpi; } tab[] = {
      {"1/16", 7.723, 28}, {"1/8", 9.728, 28},  {"1/4", 13.157, 19}, {"3/8", 16.662, 19}, {"1/2", 20.955, 14},
      {"5/8", 22.911, 14}, {"3/4", 26.441, 14}, {"7/8", 30.201, 14}, {"1", 33.249, 11},    {"1 1/4", 41.910, 11},
      {"1 1/2", 47.803, 11}, {"2", 59.614, 11}};
  for (auto& r : tab)
    if (frac == r.f) { major = r.d; tpi = r.tpi; return true; }
  return false;
}

// inch fraction "3/8", "1 1/4", "1" ; returns value in inch and canonical text
bool inchFraction(Parser& P, double& inch, std::string& text) {
  const std::size_t save = P.p;
  int a = 0;
  if (!P.integer(a)) return false;
  if (P.eat(U'/')) {
    int b = 0;
    if (!P.integer(b) || b == 0) { P.p = save; return false; }
    inch = static_cast<double>(a) / b;
    text = std::to_string(a) + "/" + std::to_string(b);
    return true;
  }
  // "1 1/4"
  const std::size_t save2 = P.p;
  if (P.eat(U' ')) {
    int n = 0, m = 0;
    if (P.integer(n) && P.eat(U'/') && P.integer(m) && m != 0) {
      inch = a + static_cast<double>(n) / m;
      text = std::to_string(a) + " " + std::to_string(n) + "/" + std::to_string(m);
      return true;
    }
    P.p = save2;
  }
  inch = a;
  text = std::to_string(a);
  return true;
}

// ---- tolerance after nominal (and fit). Returns true if something was consumed.
bool parseTolerance(Parser& P, DimText& d, bool fromOcr, Corrector& cor, bool angular) {
  const std::size_t save = P.p;
  P.ws();
  bool paren = P.eat(U'(');
  if (paren) P.ws();
  double v = 0;
  if (P.eat(kPm)) {
    P.ws();
    if (!P.number(v)) { P.p = save; return false; }
    if (angular) P.eat(kDeg);
    d.tolPlus = v;
    d.tolMinus = -v;
    if (paren) { P.ws(); P.eat(U')'); }
    return true;
  }
  auto signedDev = [&](double& out, bool allowUnsignedZero) -> bool {
    const std::size_t q = P.p;
    int sign = 0;
    if (P.eat(U'+')) sign = 1;
    else if (P.eat(U'-')) sign = -1;
    P.ws();
    double x = 0;
    if (!P.number(x)) { P.p = q; return false; }
    if (sign == 0) {
      if (!(allowUnsignedZero && x == 0)) { P.p = q; return false; }
    }
    if (angular) P.eat(kDeg);
    out = sign < 0 ? -x : x;
    return true;
  };
  double a = 0;
  if (!signedDev(a, true)) { P.p = save; return false; }
  // a lone "0" is only a tolerance if a second deviation follows
  const std::size_t afterA = P.p;
  P.ws();
  bool slash = P.eat(U'/');
  P.ws();
  double b = 0;
  bool haveB = signedDev(b, true);
  if (!haveB) {
    P.p = afterA;
    if (a == 0) { P.p = save; return false; }
    // single deviation
    if (fromOcr && a > 0) {
      d.tolPlus = a;
      d.tolMinus = -a;
      cor.add("+->± (symmetric tol)");
    } else if (a > 0) {
      d.tolPlus = a;
    } else {
      d.tolMinus = a;
    }
  } else {
    (void)slash;
    d.tolPlus = std::max(a, b);
    d.tolMinus = std::min(a, b);
    if (*d.tolPlus == 0) d.tolPlus = 0.0;     // normalise -0
    if (*d.tolMinus == 0) d.tolMinus = 0.0;
  }
  if (paren) { P.ws(); P.eat(U')'); }
  return true;
}

bool parseFit(Parser& P, DimText& d) {
  const std::size_t save = P.p;
  P.ws();
  auto one = [&](std::string& out) -> bool {
    const std::size_t q = P.p;
    std::string t;
    int letters = 0;
    while (!P.eof() && letters < 2 && ((P.peek() >= U'a' && P.peek() <= U'z') || (P.peek() >= U'A' && P.peek() <= U'Z'))) {
      t.push_back(static_cast<char>(P.peek()));
      ++P.p;
      ++letters;
    }
    if (letters == 0) { P.p = q; return false; }
    int digits = 0;
    while (!P.eof() && digits < 2 && isDigit(P.peek())) { t.push_back(static_cast<char>(P.peek())); ++P.p; ++digits; }
    if (digits == 0 || (!P.eof() && (isDigit(P.peek()) || isAlpha(P.peek()) || P.peek() == U'.'))) { P.p = q; return false; }
    // reject keywords that look like fits
    std::string up = t;
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (up.rfind("DP", 0) == 0) { P.p = q; return false; }
    out = t;
    return true;
  };
  std::string f1;
  if (!one(f1)) { P.p = save; return false; }
  const std::size_t q = P.p;
  if (P.eat(U'/')) {
    std::string f2;
    if (one(f2)) f1 += "/" + f2;
    else P.p = q;
  }
  d.fitClass = f1;
  return true;
}

// suffix keywords / depth / through / reference. Returns true if anything consumed.
bool parseSuffix(Parser& P, DimText& d, std::u32string& notes) {
  const std::size_t save = P.p;
  P.ws();
  if (P.eof()) { P.p = save; return false; }
  auto dot = [&] { P.eat(U'.'); };
  if (P.keyword(U"max") || P.keyword(U"maks") || P.keyword(U"maximum")) { dot(); d.isMax = true; return true; }
  if (P.keyword(U"min") || P.keyword(U"minimum")) { dot(); d.isMin = true; return true; }
  if (P.keyword(U"REF") || P.keyword(U"(REF)")) { d.reference = true; return true; }
  if (P.peek() == U'(' ) {
    const std::size_t q = P.p;
    ++P.p;
    if (P.keyword(U"REF") && P.eat(U')')) { d.reference = true; return true; }
    P.p = q;
  }
  if (P.keyword(U"THRU") || P.keyword(U"THROUGH") || P.keyword(U"boydan")) {
    P.ws();
    P.keyword(U"boya");
    d.through = true;
    return true;
  }
  if (P.eat(kDepth) || P.keyword(U"derinlik") || P.keyword(U"DP") || P.keyword(U"DEPTH") || P.keyword(U"derin")) {
    P.ws();
    P.eat(U':');
    P.ws();
    double v = 0;
    if (P.number(v)) { d.depth = v; return true; }
    P.p = save;
    return false;
  }
  for (const char32_t* kw : {U"TYP", U"TYP.", U"tipik", U"EQS", U"EQ", U"SP", U"ESIT", U"ÇEVRE", U"CEVRE",
                             U"PCD", U"ALL", U"AROUND", U"BOTH", U"SIDES"}) {
    if (P.keyword(kw)) {
      if (!notes.empty()) notes.push_back(U' ');
      notes += kw;
      P.eat(U'.');
      return true;
    }
  }
  P.p = save;
  return false;
}

bool isKnownChamferAngle(double a) { return a == 15 || a == 30 || a == 45 || a == 60 || a == 75; }

DimText parseCore(const std::u32string& s, const std::string& raw, const DimTextContext& ctx, Corrector& cor) {
  DimText d;
  d.raw = raw;
  d.corrections = *cor.log;
  if (s.empty()) { d.kind = DimKind::Unknown; return d; }
  std::u32string body = s;
  // whole-text reference "(12)"
  if (body.size() >= 3 && body.front() == U'(' && body.back() == U')') {
    d.reference = true;
    body = body.substr(1, body.size() - 2);
    while (!body.empty() && body.front() == U' ') body.erase(0, 1);
    while (!body.empty() && body.back() == U' ') body.pop_back();
  }
  // basic dimension written "[12]"
  if (body.size() >= 3 && body.front() == U'[' && body.back() == U']') {
    d.basic = true;
    body = body.substr(1, body.size() - 2);
  }
  Parser P(body);
  std::u32string notes;
  bool core = false;

  // ---- chamfer "2x45°", "2 x 45°", "C2"
  {
    const std::size_t q = P.p;
    double a = 0, b = 0;
    if (P.number(a)) {
      P.ws();
      if (P.eatCi(U'x')) {
        P.ws();
        const std::size_t bpos = P.p;
        if (P.number(b)) {
          bool deg = P.eat(kDeg);
          if (!deg && ctx.fromOcr && b > 90 && P.eof()) {
            // "2x450" (° read as 0) / "2x459"
            const double b10 = std::floor(b / 10);
            if (isKnownChamferAngle(b10)) { b = b10; deg = true; cor.add("0->° (chamfer)"); }
          }
          P.ws();
          if (deg || (P.eof() && isKnownChamferAngle(b) && a < b)) {
            if (!deg) cor.add("missing ° (chamfer)");
            d.kind = DimKind::Chamfer;
            d.nominal = a;
            d.chamferAngle = b;
            core = true;
          } else {
            P.p = bpos;
          }
        }
      }
    }
    if (!core) P.p = q;
    if (!core && (P.peek() == U'C' || P.peek() == U'c') && (isDigit(P.peek(1)) || (P.peek(1) == U' ' && isDigit(P.peek(2))))) {
      ++P.p;
      P.ws();
      if (P.number(a)) {
        d.kind = DimKind::Chamfer;
        d.nominal = a;
        d.chamferAngle = 45;
        core = true;
      } else {
        P.p = q;
      }
    }
  }

  // ---- count "4x", "4 X", "4 adet", "4 DELİK"
  if (!core) {
    const std::size_t q = P.p;
    int n = 0;
    if (P.integer(n) && n > 0) {
      P.ws();
      bool ok = false;
      if (P.eatCi(U'x') && !(P.peek() >= U'a' && P.peek() <= U'z')) ok = true;
      else if (P.keyword(U"adet") || P.keyword(U"DELİK") || P.keyword(U"DELIK") || P.keyword(U"HOLES") ||
               P.keyword(U"PLACES") || P.keyword(U"PL") || P.keyword(U"yer")) ok = true;
      if (ok) {
        d.count = n;
        P.ws();
        if (P.eof()) {
          d.kind = DimKind::Count;
          d.valid = true;
          d.conf = 0.7f;
          return d;
        }
      } else {
        P.p = q;
      }
    } else {
      P.p = q;
    }
  }

  // ---- prefixes and threads
  bool angular = false;
  if (!core) {
    const std::size_t q = P.p;
    // "max." before the value ("Ø max.11.8" / "max 20.5")
    auto preMaxMin = [&] {
      const std::size_t r = P.p;
      if (P.keyword(U"max") || P.keyword(U"maks")) { P.eat(U'.'); P.ws(); d.isMax = true; return; }
      if (P.keyword(U"min")) { P.eat(U'.'); P.ws(); d.isMin = true; return; }
      P.p = r;
    };
    preMaxMin();
    if (P.peek() == U'S' && (P.peek(1) == kDia || P.peek(1) == U'R')) { d.spherical = true; ++P.p; }
    if (P.eat(kDia)) { d.kind = DimKind::Diameter; P.ws(); }
    else if (P.peek() == U'R' && (isDigit(P.peek(1)) || P.peek(1) == U'.' || (P.peek(1) == U' ' && isDigit(P.peek(2))))) {
      ++P.p; d.kind = DimKind::Radius; P.ws();
    }
    else if (P.eat(kSq)) { d.squareSym = true; d.kind = DimKind::Linear; P.ws(); }
    else if (P.peek() == U'M' && (isDigit(P.peek(1)) || (P.peek(1) == U' ' && isDigit(P.peek(2))))) {
      // metric thread
      ++P.p;
      P.ws();
      double major = 0, pitch = 0;
      std::string mt;
      if (P.number(major, &mt)) {
        std::string spec = "M" + fmtNum(major);
        const std::size_t r = P.p;
        P.ws();
        if (P.eatCi(U'x')) {
          P.ws();
          if (P.number(pitch)) spec += "x" + fmtNum(pitch);
          else P.p = r;
        } else {
          P.p = r;
        }
        // tolerance class "-6H", "-6g", "-6H7"? ; "LH"
        const std::size_t r2 = P.p;
        P.ws();
        if (P.eat(U'-')) {
          P.ws();
          std::string cls;
          while (!P.eof() && (isDigit(P.peek()) || (P.peek() >= U'a' && P.peek() <= U'z') || (P.peek() >= U'A' && P.peek() <= U'Z')) && cls.size() < 6)
            cls.push_back(static_cast<char>(P.peek())), ++P.p;
          if (!cls.empty() && isDigit(static_cast<char32_t>(cls[0]))) spec += "-" + cls;
          else P.p = r2;
        } else {
          P.p = r2;
        }
        const std::size_t r3 = P.p;
        P.ws();
        if (P.keyword(U"LH")) spec += " LH";
        else P.p = r3;
        d.kind = DimKind::Thread;
        d.threadSpec = spec;
        d.threadMajor = major;
        d.threadPitch = pitch > 0 ? pitch : coarsePitch(major);
        d.nominal = major;
        core = true;
      } else {
        P.p = q;
      }
    } else if (P.peek() == U'G' && (isDigit(P.peek(1)) || (P.peek(1) == U' ' && isDigit(P.peek(2))))) {
      ++P.p;
      P.ws();
      double inch = 0;
      std::string ft;
      double major = 0, tpi = 0;
      if (inchFraction(P, inch, ft) && pipeThread(ft, major, tpi)) {
        std::string spec = "G" + ft;
        if (P.peek() == U'A' || P.peek() == U'B') { spec.push_back(static_cast<char>(P.peek())); ++P.p; }
        d.kind = DimKind::Thread;
        d.threadSpec = spec;
        d.threadMajor = major;
        d.threadPitch = 25.4 / tpi;
        d.nominal = major;
        core = true;
      } else {
        P.p = q;
      }
    }
    // inch unified thread: 3/8"-40UNS, 9/16"-24UNEF-2A, 1/4-20 UNC
    if (!core && d.kind == DimKind::Unknown) {
      const std::size_t r = P.p;
      double inch = 0;
      std::string ft;
      if (inchFraction(P, inch, ft)) {
        P.eat(U'"');
        P.ws();
        int tpi = 0;
        bool ok = false;
        std::string series;
        if (P.eat(U'-')) {
          P.ws();
          if (P.integer(tpi) && tpi > 0) {
            P.ws();
            for (const char32_t* sr : {U"UNEF", U"UNC", U"UNF", U"UNS", U"UNJF", U"UNJC", U"UN", U"NPT", U"NPTF"}) {
              if (P.keyword(sr)) { series = encodeUtf8(sr); ok = true; break; }
              // allow directly followed by "-2A"
              const std::size_t t = P.p;
              bool m = true;
              for (const char32_t* k = sr; *k; ++k) if (!P.eatCi(*k)) { m = false; break; }
              if (m && (P.eof() || P.peek() == U'-' || P.peek() == U' ')) { series = encodeUtf8(sr); ok = true; break; }
              P.p = t;
            }
          }
        }
        if (ok) {
          std::string spec = ft + "\"-" + std::to_string(tpi) + series;
          const std::size_t t = P.p;
          P.ws();
          if (P.eat(U'-')) {
            int cls = 0;
            if (P.integer(cls) && (P.peek() == U'A' || P.peek() == U'B')) {
              spec += "-" + std::to_string(cls) + static_cast<char>(P.peek());
              ++P.p;
            } else {
              P.p = t;
            }
          } else {
            P.p = t;
          }
          d.kind = DimKind::Thread;
          d.threadSpec = spec;
          d.threadMajor = inch * 25.4;
          d.threadPitch = 25.4 / tpi;
          d.nominal = d.threadMajor;
          core = true;
        } else {
          P.p = r;
        }
      }
    }
    if (!core) {
      preMaxMin();
      double v = 0;
      if (P.number(v)) {
        d.nominal = v;
        if (d.kind == DimKind::Unknown) d.kind = DimKind::Linear;
        core = true;
        // angle
        if (P.eat(kDeg)) {
          d.kind = DimKind::Angle;
          angular = true;
          const std::size_t r = P.p;
          double mins = 0;
          P.ws();
          if (P.number(mins) && P.eat(U'\'')) {
            d.nominal += mins / 60.0;
            const std::size_t r2 = P.p;
            double secs = 0;
            P.ws();
            if (P.number(secs) && P.eat(U'"')) d.nominal += secs / 3600.0;
            else P.p = r2;
          } else {
            P.p = r;
          }
        } else if (ctx.fromOcr && ctx.angular && d.kind == DimKind::Linear) {
          // ° read as 0 / 9 ("450" for 45°) or dropped
          if (v > 360 && std::floor(v) == v) {
            const double v10 = std::floor(v / 10);
            if (v10 <= 360) { d.nominal = v10; cor.add("0->° (angular context)"); }
          } else {
            cor.add("missing ° (angular context)");
          }
          d.kind = DimKind::Angle;
          angular = true;
        }
      } else {
        P.p = q;
        d.kind = DimKind::Unknown;
        d.isMax = d.isMin = false;
      }
    }
  }

  if (!core) {
    // not a dimension: a note (or nothing recognisable)
    d.valid = false;
    bool letters = false;
    for (char32_t c : s) if (isAlpha(c)) letters = true;
    d.kind = letters ? DimKind::Note : DimKind::Unknown;
    d.remainder = encodeUtf8(s);
    d.conf = 0.1f;
    d.corrections = *cor.log;
    return d;
  }

  // ---- fit, tolerance, suffixes (any order after the value; at most once each)
  bool gotFit = false, gotTol = false;
  for (int iter = 0; iter < 8 && !P.eof(); ++iter) {
    if (!gotFit && d.kind != DimKind::Angle && d.kind != DimKind::Chamfer && d.kind != DimKind::Thread && parseFit(P, d)) { gotFit = true; continue; }
    if (!gotTol && parseTolerance(P, d, ctx.fromOcr, cor, angular)) { gotTol = true; continue; }
    if (parseSuffix(P, d, notes)) continue;
    break;
  }
  P.ws();
  std::u32string rem = P.rest();
  bool remOk = rem.empty();
  if (!rem.empty()) {
    // trailing free text is acceptable as a note only if it is separated by a space and starts with a letter
    const bool sep = P.p > 0 && body[P.p - 1] == U' ';
    remOk = sep && isAlpha(rem.front());
  }
  if (!notes.empty()) {
    if (!rem.empty()) notes += U" " + rem;
    rem = notes;
  }
  d.remainder = encodeUtf8(rem);
  d.valid = remOk;
  float conf = 1.0f;
  if (!remOk) {
    conf = 0.25f;
    if (d.kind != DimKind::Thread) d.kind = DimKind::Note;
  } else if (!rem.empty()) {
    conf *= 0.9f;
  }
  d.corrections = *cor.log;
  for (int i = 0; i < cor.count; ++i) conf *= kConfCorrectionPenalty;
  // implausible values
  if (d.kind == DimKind::Angle && d.nominal > 360) conf *= 0.5f;
  if (d.kind == DimKind::Chamfer && (d.chamferAngle <= 0 || d.chamferAngle >= 90)) conf *= 0.5f;
  d.conf = conf;
  return d;
}

}  // namespace

DimText parseDimText(const std::string& text, const DimTextContext& ctx) {
  std::vector<std::string> log;
  Corrector cor{&log};
  try {
    std::u32string s = normalize(decodeUtf8(text));
    if (ctx.fromOcr) s = ocrCorrect(s, ctx, cor);
    return parseCore(s, text, ctx, cor);
  } catch (...) {
    DimText d;
    d.raw = text;
    d.kind = DimKind::Unknown;
    return d;
  }
}

int rankDimTextHypotheses(const std::vector<TextHypothesis>& hyps, const DimTextContext& ctx, DimText* best) {
  if (hyps.empty()) return -1;
  int bi = -1;
  double bs = -1e9;
  DimText bd;
  for (std::size_t i = 0; i < hyps.size(); ++i) {
    const DimText d = parseDimText(hyps[i].utf8, ctx);
    double g = d.valid ? d.conf : 0.15 * d.conf;
    if (d.valid) {
      if (ctx.nearCircleOrCylinder && d.kind == DimKind::Diameter) g += 0.1;
      if ((ctx.angular) && (d.kind == DimKind::Angle || d.kind == DimKind::Chamfer)) g += 0.1;
      if (ctx.nearArc && d.kind == DimKind::Radius) g += 0.1;
      if (!d.remainder.empty()) g -= 0.05;
    }
    const double ocr = std::clamp(static_cast<double>(hyps[i].conf), 0.0, 1.0);
    const double score = 0.4 * ocr + 0.6 * g - 1e-6 * static_cast<double>(i);
    if (score > bs) { bs = score; bi = static_cast<int>(i); bd = d; }
  }
  if (best) *best = bd;
  return bi;
}

std::string formatDimText(const DimText& d) {
  if (!d.valid) return d.raw;
  std::string out;
  if (d.count > 1) out += std::to_string(d.count) + "x ";
  if (d.kind == DimKind::Count) {
    out = std::to_string(d.count) + "x";
    return out;
  }
  auto tolStr = [&](bool angular) {
    std::string t;
    if (d.tolPlus && d.tolMinus && std::abs(*d.tolPlus + *d.tolMinus) < 1e-9 && *d.tolPlus > 0) {
      t = "±" + fmtNum(*d.tolPlus) + (angular ? "°" : "");
    } else if (d.tolPlus || d.tolMinus) {
      auto sgn = [](double v, bool upperDev) {
        if (std::abs(v) < 5e-7) return std::string(upperDev ? "+0" : "-0");
        return std::string(v > 0 ? "+" : "-") + fmtNum(std::abs(v));
      };
      t = " ";
      if (d.tolPlus) t += sgn(*d.tolPlus, true);
      if (d.tolPlus && d.tolMinus) t += "/";
      if (d.tolMinus) t += sgn(*d.tolMinus, false);
    }
    return t;
  };
  std::string core;
  switch (d.kind) {
    case DimKind::Thread: core = d.threadSpec; break;
    case DimKind::Chamfer: core = fmtNum(d.nominal) + "x" + fmtNum(d.chamferAngle) + "°"; break;
    case DimKind::Angle: {
      core = fmtNum(d.nominal) + "°" + tolStr(true);
      break;
    }
    default: {
      if (d.spherical) core += "S";
      if (d.kind == DimKind::Diameter) core += "Ø";
      else if (d.kind == DimKind::Radius) core += "R";
      if (d.squareSym) core += "□";
      core += fmtNum(d.nominal);
      if (!d.fitClass.empty()) core += " " + d.fitClass;
      core += tolStr(false);
      break;
    }
  }
  if (d.isMax) core += " max";
  if (d.isMin) core += " min";
  if (d.depth) core += " ↧" + fmtNum(*d.depth);
  if (d.through) core += " THRU";
  if (d.basic) core = "[" + core + "]";
  if (d.reference) core = "(" + core + ")";
  out += core;
  if (!d.remainder.empty()) out += " " + d.remainder;
  return out;
}

}  // namespace pc::drawing
