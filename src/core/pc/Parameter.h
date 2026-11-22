#pragma once
// PersistCAD core — named, expression-driven model parameters.
// Units: lengths in millimetres, angles in degrees.
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pc {

using ParamValues = std::map<std::string, double>;

enum class ParamKind { Length, Angle, Count, Scalar };

struct Parameter {
  std::string name;          // identifier: [A-Za-z_][A-Za-z0-9_]*
  std::string expression;    // "40", "W/2 + 3", ...
  ParamKind kind = ParamKind::Length;
  double minValue = -std::numeric_limits<double>::infinity();  // hard range (user-defined)
  double maxValue =  std::numeric_limits<double>::infinity();
  std::string description;

  // --- "App" exposure (used by Save as App / runner) ---
  bool exposed = false;      // shown as an input in the generated app
  std::string label;         // UI label in the app (falls back to name)
  double step = 0.0;         // UI spin step (0 = auto)
  std::string group;         // UI group box name in the app
};

struct ParamError {
  std::string param;
  std::string message;
};

class ParamTable {
public:
  const std::vector<Parameter>& all() const { return params_; }
  std::vector<Parameter>& all() { return params_; }

  const Parameter* find(const std::string& name) const;
  Parameter* find(const std::string& name);
  bool add(const Parameter& p);                 // false if name exists/invalid
  bool remove(const std::string& name);
  bool rename(const std::string& oldName, const std::string& newName); // also rewrites expressions of other params
  bool setExpression(const std::string& name, const std::string& expr);

  // Evaluate all parameters (dependency order). `overrides` replace the expression of the
  // named parameters (values given as expression strings, e.g. {"T","2.5"}).
  // On failure returns std::nullopt and fills errors (unknown name, cycle, syntax, NaN).
  std::optional<ParamValues> evaluate(std::vector<ParamError>* errors = nullptr,
                                      const std::map<std::string, std::string>& overrides = {}) const;

  // Range check of already-evaluated values against min/max. Returns violations.
  std::vector<ParamError> checkRanges(const ParamValues& values) const;

  static bool isValidName(const std::string& n);

private:
  std::vector<Parameter> params_;
};

// Evaluate an arbitrary expression (e.g. a feature property) against parameter values.
// Throws pc::ExprError.
double evalWith(const std::string& expr, const ParamValues& values);

} // namespace pc
