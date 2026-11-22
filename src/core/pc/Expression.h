#pragma once
// PersistCAD core — math expression evaluator (no OCCT, no Qt).
// Grammar: numbers, identifiers, + - * / ^, unary +/-, parentheses,
// functions: sin cos tan asin acos atan atan2 sqrt abs min max pow floor ceil round exp log
// constants: pi, e.   Trigonometric functions take/return DEGREES (CAD convention).
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pc {

class ExprError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

using VarLookup = std::function<std::optional<double>(const std::string&)>;

// Throws ExprError on syntax error or unknown identifier.
double evaluateExpression(const std::string& expr, const VarLookup& lookup);

// Identifiers (variables) referenced by an expression, without functions/constants. Throws on syntax error.
std::vector<std::string> referencedNames(const std::string& expr);

// True if expr is exactly one identifier (e.g. "T"), optionally surrounded by spaces. Returns the name.
std::optional<std::string> singleIdentifier(const std::string& expr);

// Replace whole-word identifier `from` with `to` in expr (function names untouched).
std::string renameIdentifier(const std::string& expr, const std::string& from, const std::string& to);

// Format a double for storing into an expression string (shortest round-trip-ish, e.g. "12.5").
std::string formatNumber(double v);

} // namespace pc
