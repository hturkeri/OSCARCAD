#include "RunnerCore.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace pc::runner {

std::vector<ExposedParam> exposedParams(const pc::app::AppDefinition& a) {
  ParamValues vals = a.document.evaluateParams();
  std::vector<ExposedParam> out;
  for (const auto& p : a.document.params.all()) {
    if (!p.exposed) continue;
    ExposedParam e;
    e.name = p.name;
    e.label = p.label.empty() ? p.name : p.label;
    e.group = p.group.empty() ? "Parametreler" : p.group;
    e.kind = p.kind;
    e.minV = p.minValue;
    e.maxV = p.maxValue;
    e.step = p.step;
    auto it = vals.find(p.name);
    e.def = it == vals.end() ? 0.0 : it->second;
    out.push_back(e);
  }
  return out;
}

std::map<std::string, std::string> toOverrides(const std::map<std::string, double>& values) {
  std::map<std::string, std::string> o;
  for (auto& [k, v] : values) o[k] = pc::formatNumber(v);
  return o;
}

MfgSettings effectiveMfg(const pc::app::AppDefinition& a) {
  MfgSettings s = a.document.mfg;
  if (!a.showAdditiveChecks) s.am.enabled = false;
  if (!a.showConventionalChecks) s.conv.enabled = false;
  return s;
}

bool parseSetArg(const std::string& s, std::string& name, double& value, std::string* err) {
  auto eq = s.find('=');
  if (eq == std::string::npos || eq == 0) { if (err) *err = "Geçersiz --set argümanı (NAME=VALUE bekleniyor): " + s; return false; }
  name = s.substr(0, eq);
  std::string v = s.substr(eq + 1);
  for (auto& c : v) if (c == ',') c = '.';
  try {
    value = pc::evaluateExpression(v, [](const std::string&) { return std::optional<double>(); });
  } catch (const std::exception& e) {
    if (err) *err = "Geçersiz değer '" + v + "': " + e.what();
    return false;
  }
  return true;
}

std::string activeMaterial(const MfgSettings& m) {
  if (m.am.enabled && m.am.process != AmProcess::None && !m.am.material.empty()) return m.am.material;
  if (!m.conv.material.empty()) return m.conv.material;
  return m.am.material;
}

double densityForMaterial(const std::string& material) {
  std::string n;
  for (char c : material) if (!std::isspace(static_cast<unsigned char>(c)) && c != '_') n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
  if (has("ti-6al-4v") || has("ti6al4v") || has("ti64")) return 4.43;
  if (has("316l")) return 7.99;
  if (has("in718") || has("inconel718") || has("inconel")) return 8.19;
  if (has("7075")) return 2.81;
  if (has("6061")) return 2.70;
  if (has("pa12")) return 1.01;
  if (has("pla")) return 1.24;
  if (has("resin") || has("recine") || has("reçine")) return 1.15;
  return 7.85;
}

bool issueVisible(const pc::app::AppDefinition& a, const pc::mfg::Issue& i) {
  if (i.domain == pc::mfg::Domain::Additive) return a.showAdditiveChecks;
  if (i.domain == pc::mfg::Domain::Conventional) return a.showConventionalChecks;
  return true;
}

nlohmann::json issueToJson(const pc::mfg::Issue& i) {
  nlohmann::json j;
  j["severity"] = pc::mfg::toString(i.severity);
  j["domain"] = pc::mfg::toString(i.domain);
  j["ruleId"] = i.ruleId;
  j["message"] = i.message;
  j["featureId"] = i.featureId;
  j["params"] = i.params;
  if (i.suggestedValue) j["suggestedValue"] = *i.suggestedValue; else j["suggestedValue"] = nullptr;
  j["actual"] = i.actual;
  j["limit"] = i.limit;
  j["faces"] = i.faces;
  j["edges"] = i.edges;
  j["body"] = i.body;
  return j;
}

std::vector<BodySummary> bodySummaries(const pc::model::ModelResult& m, double rho) {
  std::vector<BodySummary> out;
  for (auto& b : m.bodies) {
    BodySummary s;
    s.name = b.name;
    try { s.volume = pc::model::massProperties(b.shape).volume; } catch (...) {}
    s.massG = s.volume / 1000.0 * rho;
    out.push_back(s);
  }
  return out;
}

bool acceptable(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r) {
  if (!r.regenerated || !r.model.ok || r.model.shape.IsNull()) return false;   // an empty model is not a part
  for (auto& i : r.report.issues)
    if (i.severity == pc::mfg::Severity::Error && issueVisible(a, i)) return false;
  return true;
}

nlohmann::json reportToJson(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r,
                            const std::map<std::string, double>& setValues) {
  nlohmann::json j;
  j["app"] = a.appName;
  j["version"] = a.version;
  j["overrides"] = setValues;
  // the app's inputs (exposed parameters) with the values used for this run: --set overrides, else the evaluated
  // model values, else the default expression's value
  j["parameters"] = nlohmann::json::object();
  for (const auto& p : a.document.params.all()) {
    if (!p.exposed) continue;
    double v = std::nan("");
    if (auto it = setValues.find(p.name); it != setValues.end()) v = it->second;
    else if (auto mt = r.model.params.find(p.name); mt != r.model.params.end()) v = mt->second;
    else { try { v = pc::evalWith(p.expression, a.document.evaluateParams()); } catch (...) {} }
    j["parameters"][p.name] = std::isfinite(v) ? nlohmann::json(v) : nlohmann::json(nullptr);
  }
  j["regenerated"] = r.regenerated;
  j["modelOk"] = r.model.ok;
  j["acceptable"] = acceptable(a, r);
  int e = 0, w = 0, inf = 0;
  j["issues"] = nlohmann::json::array();
  for (auto& i : r.report.issues) {
    if (!issueVisible(a, i)) continue;
    if (i.severity == pc::mfg::Severity::Error) ++e; else if (i.severity == pc::mfg::Severity::Warning) ++w; else ++inf;
    j["issues"].push_back(issueToJson(i));
  }
  j["bodies"] = nlohmann::json::array();
  if (r.regenerated) {
    double rho = densityForMaterial(activeMaterial(effectiveMfg(a)));
    for (auto& b : bodySummaries(r.model, rho))
      j["bodies"].push_back({{"name", b.name}, {"volumeMm3", b.volume}, {"massG", b.massG}});
  }
  j["counts"] = {{"errors", e}, {"warnings", w}, {"info", inf}};
  // Bounding box of the regenerated model (mm): lets scripts verify that a --set value drives the geometry.
  if (r.regenerated && !r.model.shape.IsNull()) {
    try {
      const auto mp = pc::model::massProperties(r.model.shape);
      j["bbox"] = {{"xmin", mp.xmin}, {"ymin", mp.ymin}, {"zmin", mp.zmin}, {"xmax", mp.xmax}, {"ymax", mp.ymax}, {"zmax", mp.zmax}};
      j["volumeMm3"] = mp.volume;
    } catch (...) {}
  }
  return j;
}

std::string reportToText(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r,
                         const std::map<std::string, double>& setValues) {
  std::ostringstream os;
  os << a.appName << " v" << a.version << "\n";
  for (auto& [k, v] : setValues) os << "  " << k << " = " << pc::formatNumber(v) << "\n";
  {
    std::string list;
    for (const auto& p : a.document.params.all()) {
      if (!p.exposed) continue;
      auto it = setValues.find(p.name);
      auto mt = r.model.params.find(p.name);
      const std::string val = it != setValues.end() ? pc::formatNumber(it->second) : mt != r.model.params.end() ? pc::formatNumber(mt->second) : p.expression;
      list += (list.empty() ? "" : ", ") + p.name + " = " + val;
    }
    if (!list.empty()) os << "Parametreler: " << list << "\n";
  }
  os << "Model: " << (r.regenerated && r.model.ok ? (r.model.shape.IsNull() ? "BOŞ (katı yok)" : "oluşturuldu") : "OLUŞTURULAMADI") << "\n";
  if (r.regenerated && r.model.bodies.size() > 1) {
    os << "Gövdeler (" << r.model.bodies.size() << "):\n";
    for (auto& b : bodySummaries(r.model, densityForMaterial(activeMaterial(effectiveMfg(a)))))
      os << "  - " << b.name << ": hacim " << pc::formatNumber(std::round(b.volume / 10.0) / 100.0) << " cm3, kütle "
         << pc::formatNumber(std::round(b.massG * 10.0) / 10.0) << " g\n";
  }
  for (auto& i : r.report.issues) {
    if (!issueVisible(a, i)) continue;
    os << "[" << pc::mfg::toString(i.severity) << "] (" << pc::mfg::toString(i.domain) << ") " << i.ruleId << ": " << i.message;
    if (!i.body.empty()) os << "  [Gövde: " << i.body << "]";
    if (i.suggestedValue && !i.params.empty()) os << "  -> Öneri: " << i.params[0] << " = " << pc::formatNumber(*i.suggestedValue);
    os << "\n";
  }
  os << "Sonuç: " << (acceptable(a, r) ? "UYGUN" : "UYGUN DEĞİL") << "\n";
  return os.str();
}

pc::mfg::ValidationResult validateSafe(const pc::app::AppDefinition& a,
                                       const std::map<std::string, std::string>& overrides) {
  // Runs on a QtConcurrent worker: nothing may escape (an exception stored in the future is rethrown
  // by QFutureWatcher::result() on the GUI thread and terminates the app). OCCT's Standard_Failure
  // does not derive from std::exception, hence the catch-all branches.
  auto fail = [](const std::string& what) {
    pc::mfg::ValidationResult r;
    pc::mfg::Issue i;
    i.severity = pc::mfg::Severity::Error;
    i.domain = pc::mfg::Domain::Model;
    i.ruleId = "APP.EXCEPTION";
    i.message = "Doğrulama başarısız: " + what;
    r.report.issues.push_back(i);
    return r;
  };
  try {
    return pc::mfg::validateChange(a.document, overrides, effectiveMfg(a));
  } catch (const std::exception& e) {
    return fail(e.what());
  } catch (const Standard_Failure& e) {
    return fail(std::string("OCCT: ") + (e.GetMessageString() ? e.GetMessageString() : ""));
  } catch (...) {
    return fail("bilinmeyen hata");
  }
}

nlohmann::json namedSelectionsReport(const pc::app::AppDefinition& a, const std::map<std::string, std::string>& overrides) {
  nlohmann::json j = {{"ok", false}, {"selections", nlohmann::json::array()}};
  try {
    pc::model::ModelResult m = pc::model::regenerate(a.document, overrides);
    j["ok"] = m.ok;
    for (const auto& r : pc::model::resolveAll(a.document, m)) {
      nlohmann::json s = {{"name", r.name}, {"id", r.id}, {"status", pc::model::toString(r.status)},
                          {"statusLabel", pc::model::statusLabel(r.status)}, {"faces", r.faces},
                          {"bodies", r.bodies}, {"diagnostic", r.diagnostic}};
      if (!r.edges.empty()) s["edges"] = r.edges;
      j["selections"].push_back(s);
    }
  } catch (const std::exception& e) {
    j["error"] = e.what();
  } catch (...) {
    j["error"] = "bilinmeyen hata";
  }
  return j;
}

bool exportStepWithNames(const pc::app::AppDefinition& a, const std::map<std::string, std::string>& overrides,
                         const std::string& path, std::string* err, const std::vector<std::string>& bodies) {
  try {
    pc::model::ModelResult m = pc::model::regenerate(a.document, overrides);
    if (m.shape.IsNull()) {
      if (err) *err = "Model üretilemedi";
      return false;
    }
    pc::model::StepExportOptions o;
    o.bodies = bodies;
    return pc::model::exportSTEPNamed(a.document, m, path, o, err);
  } catch (const std::exception& e) {
    if (err) *err = e.what();
  } catch (...) {
    if (err) *err = "STEP dışa aktarımı başarısız";
  }
  return false;
}

} // namespace pc::runner
