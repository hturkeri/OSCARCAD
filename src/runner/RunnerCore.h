#pragma once
// Runner core helpers — GUI-free (used by --validate-only and by the window).
#include "pc/AppPackage.h"
#include "pc/MfgChecks.h"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace pc::runner {

struct ExposedParam {
  std::string name, label, group;
  ParamKind kind = ParamKind::Length;
  double minV = 0, maxV = 0, step = 0, def = 0;
};

// Exposed parameters (with defaults evaluated from the document). Throws on evaluation failure.
std::vector<ExposedParam> exposedParams(const pc::app::AppDefinition& a);

// Defaults for all params, overridden by values; returns {name -> expression string}.
std::map<std::string, std::string> toOverrides(const std::map<std::string, double>& values);

// MfgSettings with AM / CNC disabled according to the app options.
MfgSettings effectiveMfg(const pc::app::AppDefinition& a);

// Parse "NAME=VALUE" (VALUE numeric or constant expression).
bool parseSetArg(const std::string& s, std::string& name, double& value, std::string* err);

// Material density in g/cm^3 (default 7.85).
double densityForMaterial(const std::string& material);
std::string activeMaterial(const MfgSettings& m);

// Issue visibility according to app options (domain toggles).
bool issueVisible(const pc::app::AppDefinition& a, const pc::mfg::Issue& i);

// v2: per-body summary (volume mm^3, mass g from material density).
struct BodySummary { std::string name; double volume = 0, massG = 0; };
std::vector<BodySummary> bodySummaries(const pc::model::ModelResult& m, double densityGcm3);

nlohmann::json issueToJson(const pc::mfg::Issue& i);
nlohmann::json reportToJson(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r,
                            const std::map<std::string, double>& setValues);
std::string reportToText(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r,
                         const std::map<std::string, double>& setValues);

// Safe wrapper around mfg::validateChange (never throws).
pc::mfg::ValidationResult validateSafe(const pc::app::AppDefinition& a,
                                       const std::map<std::string, std::string>& overrides);

// Named selections (docs/named_selections/MODEL.md) at the given parameter values: regenerate + resolve.
// JSON: {"ok": bool, "selections": [{"name","id","status","statusLabel","faces":[..],"bodies":[..],
//        "diagnostic"}]}. Never throws.
nlohmann::json namedSelectionsReport(const pc::app::AppDefinition& a, const std::map<std::string, std::string>& overrides);
// Regenerate with `overrides` and write STEP whose faces carry the named-selection names (CFD/FEA boundary
// zones). `bodies`: export only these bodies (empty = all). Never throws.
bool exportStepWithNames(const pc::app::AppDefinition& a, const std::map<std::string, std::string>& overrides,
                         const std::string& path, std::string* err = nullptr,
                         const std::vector<std::string>& bodies = {});

// Acceptance considering the app's visible domains.
bool acceptable(const pc::app::AppDefinition& a, const pc::mfg::ValidationResult& r);

} // namespace pc::runner
