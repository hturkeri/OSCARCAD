#include "pc/Expression.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace pc {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

enum class Tok { Num, Ident, Op, LParen, RParen, Comma, End };

struct Token {
  Tok type;
  std::string text;
  double value = 0;
  size_t pos = 0;
};

std::vector<Token> tokenize(const std::string& s) {
  std::vector<Token> out;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
      size_t start = i;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
      if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
        if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
          i = j;
          while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
      }
      std::string num = s.substr(start, i - start);
      char* end = nullptr;
      double v = std::strtod(num.c_str(), &end);
      if (!end || *end != '\0') throw ExprError("Geçersiz sayı: '" + num + "'");
      out.push_back({Tok::Num, num, v, start});
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t start = i;
      while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
      out.push_back({Tok::Ident, s.substr(start, i - start), 0, start});
      continue;
    }
    if (c == '(') { out.push_back({Tok::LParen, "(", 0, i++}); continue; }
    if (c == ')') { out.push_back({Tok::RParen, ")", 0, i++}); continue; }
    if (c == ',') { out.push_back({Tok::Comma, ",", 0, i++}); continue; }
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%') {
      out.push_back({Tok::Op, std::string(1, c), 0, i++});
      continue;
    }
    throw ExprError(std::string("Beklenmeyen karakter '") + c + "' (konum " + std::to_string(i) + ")");
  }
  out.push_back({Tok::End, "", 0, s.size()});
  return out;
}

bool isFunction(const std::string& n) {
  static const std::set<std::string> f = {"sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sqrt", "abs",
                                          "min", "max", "pow", "floor", "ceil", "round", "exp", "log"};
  return f.count(n) > 0;
}
bool isConstant(const std::string& n) { return n == "pi" || n == "e"; }

// Recursive-descent parser. If lookup is null, only collects names (values = 1).
class Parser {
public:
  Parser(const std::vector<Token>& t, const VarLookup* lookup, std::vector<std::string>* names)
      : t_(t), lookup_(lookup), names_(names) {}

  double parse() {
    double v = expr();
    if (cur().type != Tok::End) throw ExprError("Fazla ifade: '" + cur().text + "'");
    return v;
  }

private:
  const Token& cur() const { return t_[i_]; }
  void next() { if (i_ + 1 < t_.size()) ++i_; }

  double expr() {
    double v = term();
    while (cur().type == Tok::Op && (cur().text == "+" || cur().text == "-")) {
      char op = cur().text[0];
      next();
      double r = term();
      v = op == '+' ? v + r : v - r;
    }
    return v;
  }
  double term() {
    double v = unary();
    while (cur().type == Tok::Op && (cur().text == "*" || cur().text == "/" || cur().text == "%")) {
      char op = cur().text[0];
      next();
      double r = unary();
      if (op == '*') v *= r;
      else if (op == '/') {
        if (lookup_ && r == 0.0) throw ExprError("Sıfıra bölme");
        v = lookup_ ? v / r : v;
      } else {
        v = lookup_ ? std::fmod(v, r) : v;
      }
    }
    return v;
  }
  double unary() {
    if (cur().type == Tok::Op && (cur().text == "-" || cur().text == "+")) {
      bool neg = cur().text == "-";
      next();
      double v = unary();
      return neg ? -v : v;
    }
    return power();
  }
  double power() {
    double b = primary();
    if (cur().type == Tok::Op && cur().text == "^") {
      next();
      double e = unary();  // right assoc
      return lookup_ ? std::pow(b, e) : b;
    }
    return b;
  }
  double primary() {
    const Token& tk = cur();
    if (tk.type == Tok::Num) { next(); return tk.value; }
    if (tk.type == Tok::LParen) {
      next();
      double v = expr();
      if (cur().type != Tok::RParen) throw ExprError("')' bekleniyor");
      next();
      return v;
    }
    if (tk.type == Tok::Ident) {
      std::string name = tk.text;
      next();
      if (cur().type == Tok::LParen) {
        if (!isFunction(name)) throw ExprError("Bilinmeyen fonksiyon: " + name);
        next();
        std::vector<double> args;
        if (cur().type != Tok::RParen) {
          args.push_back(expr());
          while (cur().type == Tok::Comma) { next(); args.push_back(expr()); }
        }
        if (cur().type != Tok::RParen) throw ExprError("')' bekleniyor (" + name + ")");
        next();
        return call(name, args);
      }
      if (isConstant(name)) return name == "pi" ? kPi : std::exp(1.0);
      if (names_) {
        bool seen = false;
        for (auto& n : *names_) if (n == name) { seen = true; break; }
        if (!seen) names_->push_back(name);
      }
      if (!lookup_) return 1.0;
      auto v = (*lookup_)(name);
      if (!v) throw ExprError("Bilinmeyen parametre: " + name);
      return *v;
    }
    if (tk.type == Tok::End) throw ExprError("İfade eksik");
    throw ExprError("Beklenmeyen simge: '" + tk.text + "'");
  }

  double call(const std::string& f, const std::vector<double>& a) {
    auto need = [&](size_t n) {
      if (a.size() != n) throw ExprError(f + "() " + std::to_string(n) + " argüman bekler");
    };
    if (!lookup_) return 1.0;
    if (f == "min" || f == "max") {
      if (a.empty()) throw ExprError(f + "() en az 1 argüman bekler");
      double v = a[0];
      for (double x : a) v = (f == "min") ? std::min(v, x) : std::max(v, x);
      return v;
    }
    if (f == "atan2") { need(2); return std::atan2(a[0], a[1]) / kDeg; }
    if (f == "pow") { need(2); return std::pow(a[0], a[1]); }
    need(1);
    double x = a[0];
    if (f == "sin") return std::sin(x * kDeg);
    if (f == "cos") return std::cos(x * kDeg);
    if (f == "tan") return std::tan(x * kDeg);
    if (f == "asin") return std::asin(x) / kDeg;
    if (f == "acos") return std::acos(x) / kDeg;
    if (f == "atan") return std::atan(x) / kDeg;
    if (f == "sqrt") { if (x < 0) throw ExprError("sqrt(negatif)"); return std::sqrt(x); }
    if (f == "abs") return std::fabs(x);
    if (f == "floor") return std::floor(x);
    if (f == "ceil") return std::ceil(x);
    if (f == "round") return std::round(x);
    if (f == "exp") return std::exp(x);
    if (f == "log") { if (x <= 0) throw ExprError("log(<=0)"); return std::log(x); }
    throw ExprError("Bilinmeyen fonksiyon: " + f);
  }

  const std::vector<Token>& t_;
  size_t i_ = 0;
  const VarLookup* lookup_;
  std::vector<std::string>* names_;
};

} // namespace

double evaluateExpression(const std::string& expr, const VarLookup& lookup) {
  auto toks = tokenize(expr);
  Parser p(toks, &lookup, nullptr);
  double v = p.parse();
  if (!std::isfinite(v)) throw ExprError("Sonuç sonlu değil: " + expr);
  return v;
}

std::vector<std::string> referencedNames(const std::string& expr) {
  auto toks = tokenize(expr);
  std::vector<std::string> names;
  Parser p(toks, nullptr, &names);
  p.parse();
  return names;
}

std::optional<std::string> singleIdentifier(const std::string& expr) {
  try {
    auto toks = tokenize(expr);
    if (toks.size() == 2 && toks[0].type == Tok::Ident && !isConstant(toks[0].text)) return toks[0].text;
  } catch (...) {
  }
  return std::nullopt;
}

std::string renameIdentifier(const std::string& expr, const std::string& from, const std::string& to) {
  std::string out;
  size_t i = 0;
  auto isId0 = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
  auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  while (i < expr.size()) {
    char c = expr[i];
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      // number (incl. exponent) copied verbatim
      size_t s = i;
      while (i < expr.size() && (std::isdigit(static_cast<unsigned char>(expr[i])) || expr[i] == '.')) ++i;
      if (i < expr.size() && (expr[i] == 'e' || expr[i] == 'E')) {
        size_t j = i + 1;
        if (j < expr.size() && (expr[j] == '+' || expr[j] == '-')) ++j;
        if (j < expr.size() && std::isdigit(static_cast<unsigned char>(expr[j]))) {
          i = j;
          while (i < expr.size() && std::isdigit(static_cast<unsigned char>(expr[i]))) ++i;
        }
      }
      out += expr.substr(s, i - s);
      continue;
    }
    if (isId0(c)) {
      size_t s = i;
      while (i < expr.size() && isId(expr[i])) ++i;
      std::string id = expr.substr(s, i - s);
      size_t k = i;
      while (k < expr.size() && std::isspace(static_cast<unsigned char>(expr[k]))) ++k;
      bool isCall = k < expr.size() && expr[k] == '(';
      out += (id == from && !isCall) ? to : id;
      continue;
    }
    out += c;
    ++i;
  }
  return out;
}

std::string formatNumber(double v) {
  if (std::fabs(v - std::round(v)) < 1e-12 && std::fabs(v) < 1e15) {
    std::ostringstream os;
    os << static_cast<long long>(std::llround(v));
    return os.str();
  }
  char buf[64];
  for (int prec = 6; prec <= 17; ++prec) {
    std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
    if (std::strtod(buf, nullptr) == v || prec == 17) break;
    if (std::fabs(std::strtod(buf, nullptr) - v) <= 1e-12 * std::max(1.0, std::fabs(v))) break;
  }
  return buf;
}

} // namespace pc
