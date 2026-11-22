#include "pc/Parameter.h"
#include "pc/Expression.h"

#include <cctype>
#include <cmath>
#include <functional>
#include <set>

namespace pc {

bool ParamTable::isValidName(const std::string& n) {
  if (n.empty()) return false;
  if (!(std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')) return false;
  for (char c : n)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  static const std::set<std::string> reserved = {"pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
                                                 "sqrt", "abs", "min", "max", "pow", "floor", "ceil", "round",
                                                 "exp", "log"};
  return reserved.count(n) == 0;
}

const Parameter* ParamTable::find(const std::string& name) const {
  for (auto& p : params_) if (p.name == name) return &p;
  return nullptr;
}
Parameter* ParamTable::find(const std::string& name) {
  for (auto& p : params_) if (p.name == name) return &p;
  return nullptr;
}

bool ParamTable::add(const Parameter& p) {
  if (!isValidName(p.name) || find(p.name)) return false;
  params_.push_back(p);
  return true;
}

bool ParamTable::remove(const std::string& name) {
  for (auto it = params_.begin(); it != params_.end(); ++it)
    if (it->name == name) { params_.erase(it); return true; }
  return false;
}

bool ParamTable::rename(const std::string& oldName, const std::string& newName) {
  Parameter* p = find(oldName);
  if (!p || !isValidName(newName) || find(newName)) return false;
  p->name = newName;
  for (auto& q : params_) q.expression = renameIdentifier(q.expression, oldName, newName);
  return true;
}

bool ParamTable::setExpression(const std::string& name, const std::string& expr) {
  Parameter* p = find(name);
  if (!p) return false;
  p->expression = expr;
  return true;
}

std::optional<ParamValues> ParamTable::evaluate(std::vector<ParamError>* errors,
                                                const std::map<std::string, std::string>& overrides) const {
  std::map<std::string, std::string> exprs;
  for (auto& p : params_) exprs[p.name] = p.expression;
  for (auto& [k, v] : overrides) {
    if (!exprs.count(k)) {
      if (errors) errors->push_back({k, "Tanımsız parametre"});
      return std::nullopt;
    }
    exprs[k] = v;
  }

  ParamValues values;
  std::map<std::string, int> state;  // 0 = unvisited, 1 = visiting, 2 = done
  bool ok = true;

  std::function<bool(const std::string&)> visit = [&](const std::string& n) -> bool {
    int& st = state[n];
    if (st == 2) return true;
    if (st == 1) {
      if (errors) errors->push_back({n, "Döngüsel bağımlılık"});
      return false;
    }
    st = 1;
    try {
      for (auto& dep : referencedNames(exprs[n])) {
        if (!exprs.count(dep)) {
          if (errors) errors->push_back({n, "Bilinmeyen parametre: " + dep});
          return false;
        }
        if (!visit(dep)) return false;
      }
      double v = evaluateExpression(exprs[n], [&](const std::string& id) -> std::optional<double> {
        auto it = values.find(id);
        if (it == values.end()) return std::nullopt;
        return it->second;
      });
      values[n] = v;
    } catch (const std::exception& ex) {
      if (errors) errors->push_back({n, ex.what()});
      return false;
    }
    state[n] = 2;
    return true;
  };

  for (auto& p : params_)
    if (!visit(p.name)) ok = false;
  if (!ok) return std::nullopt;
  return values;
}

std::vector<ParamError> ParamTable::checkRanges(const ParamValues& values) const {
  std::vector<ParamError> out;
  for (auto& p : params_) {
    auto it = values.find(p.name);
    if (it == values.end()) continue;
    double v = it->second;
    if (v < p.minValue - 1e-12)
      out.push_back({p.name, p.name + " = " + formatNumber(v) + " < min " + formatNumber(p.minValue)});
    if (v > p.maxValue + 1e-12)
      out.push_back({p.name, p.name + " = " + formatNumber(v) + " > max " + formatNumber(p.maxValue)});
    if (p.kind == ParamKind::Count && std::fabs(v - std::round(v)) > 1e-9)
      out.push_back({p.name, p.name + " tam sayı olmalı"});
  }
  return out;
}

double evalWith(const std::string& expr, const ParamValues& values) {
  return evaluateExpression(expr, [&](const std::string& id) -> std::optional<double> {
    auto it = values.find(id);
    if (it == values.end()) return std::nullopt;
    return it->second;
  });
}

} // namespace pc
