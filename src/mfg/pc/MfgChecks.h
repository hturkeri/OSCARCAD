#pragma once
// PersistCAD mfg module — Design-for-Additive-Manufacturing and Design-for-Manufacturing checks.
// OWNER: Agent C (mfg). Interface frozen; implementation in src/mfg/*.cpp.
//
// Pipeline used by designer AND generated apps whenever parameters change:
//   1. range checks (Parameter min/max)                     — instant
//   2. checkAnalytic: feature-aware rules on evaluated values — no geometry, < 1 ms
//      (e.g. Shell thickness < AM min wall, Hole L/D > drill limit, fillet radius < tool radius)
//   3. regenerate candidate model (pc::model)
//   4. checkGeometric: rules on the B-Rep (wall thickness sampling, overhang faces vs build dir,
//      sharp internal vertical edges, build envelope, enclosed cavities, ...)
// Each issue names the PARAMETER(S) that drive the violation and, when possible, a suggested
// corrected value so the UI can offer "Önerilen değeri uygula".
#include "pc/Document.h"
#include "pc/Model.h"
#include <optional>
#include <string>
#include <vector>

namespace pc::mfg {

enum class Severity { Info, Warning, Error };
enum class Domain { Parameter, Model, Additive, Conventional };

const char* toString(Severity s);
const char* toString(Domain d);

struct Issue {
  Severity severity = Severity::Warning;
  Domain domain = Domain::Additive;
  std::string ruleId;                 // stable id, e.g. "AM.MIN_WALL", "CNC.HOLE_LD"
  std::string message;                // Turkish, specific: includes actual vs limit values
  std::string featureId;              // feature that causes it ("" if global)
  std::vector<std::string> params;    // driving parameter names (most relevant first)
  std::optional<double> suggestedValue; // proposed value for params[0]
  double actual = 0, limit = 0;       // measured value vs rule limit
  std::vector<std::string> faces;     // persistent face names to highlight
  std::vector<std::string> edges;     // persistent edge names to highlight
  std::string body;                   // v2: body name the issue belongs to ("" = whole part / n.a.)
};

struct Report {
  std::vector<Issue> issues;
  bool hasErrors() const;
  bool hasWarnings() const;
  int count(Severity s) const;
};

// Process presets (sensible industry defaults) — fills limits for the chosen process/material.
AmSettings amPreset(AmProcess p);
ConvSettings convPreset(ConvProcess p);

// Step 1+2: no geometry.
Report checkParameters(const Document& doc, const ParamValues& values);              // ranges, eval errors
Report checkAnalytic(const Document& doc, const ParamValues& values, const MfgSettings& s);

// Step 4: geometry based.
Report checkGeometric(const Document& doc, const model::ModelResult& model, const MfgSettings& s);

// Full pipeline for a candidate parameter change. `overrides` = {paramName -> expression}.
// Returns all issues; outModel receives the regenerated candidate (if it got that far).
// If checkParameters reports errors, no regeneration is performed.
struct ValidationResult {
  Report report;
  ParamValues values;
  model::ModelResult model;
  bool regenerated = false;
  bool acceptable() const { return regenerated && model.ok && !report.hasErrors(); }
};
ValidationResult validateChange(const Document& doc, const std::map<std::string, std::string>& overrides,
                                const MfgSettings& s);

// Find a value for `param` that makes `expr` (evaluated with values, param varied) equal `target`.
// Used to turn a limit on a derived quantity into a parameter suggestion. Secant/bisection.
std::optional<double> solveParamFor(const std::string& expr, const std::string& param,
                                    const ParamValues& values, double target);

// Standard metric drill sizes (mm) and nearest helper.
const std::vector<double>& standardDrillSizes();
double nearestStandardDrill(double d);

} // namespace pc::mfg
