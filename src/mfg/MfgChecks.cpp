// PersistCAD mfg — public API: presets, drill table, parameter checks, validation pipeline, merging.
#include "MfgInternal.h"

#include "pc/Expression.h"
#include "pc/SimdState.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace pc::mfg {

const char* toString(Severity s) { return s == Severity::Error ? "Hata" : s == Severity::Warning ? "Uyarı" : "Bilgi"; }
const char* toString(Domain d) {
  switch (d) {
    case Domain::Parameter: return "Parametre";
    case Domain::Model: return "Model";
    case Domain::Additive: return "Katmanlı İmalat";
    default: return "Konvansiyonel İmalat";
  }
}
bool Report::hasErrors() const {
  for (auto& i : issues)
    if (i.severity == Severity::Error) return true;
  return false;
}
bool Report::hasWarnings() const {
  for (auto& i : issues)
    if (i.severity == Severity::Warning) return true;
  return false;
}
int Report::count(Severity s) const {
  int n = 0;
  for (auto& i : issues)
    if (i.severity == s) ++n;
  return n;
}

// ============================================================================ presets
// Values are conservative industry design-guide defaults (machine/material specific limits should be
// tuned per supplier). Rationale per process in the comments.
AmSettings amPreset(AmProcess p) {
  AmSettings s;
  s.process = p;
  switch (p) {
    case AmProcess::None:
      s.enabled = false;
      break;
    case AmProcess::LPBF_Metal:
      // Laser powder bed fusion (Ti-6Al-4V / 316L / IN718, 30-60 um layers, ~80-100 um spot):
      // walls < ~0.4 mm are not reproducible (melt pool width ~0.1-0.2 mm, distortion), pins ~0.3 mm,
      // holes < 0.5 mm fuse shut; horizontal holes up to ~6-8 mm self-support (arch); down-skin
      // < 45 deg needs supports; powder must escape through holes >= ~2 mm; EOS M290-class envelope.
      s.material = "Ti-6Al-4V";
      s.minWallThickness = 0.4; s.minFeatureSize = 0.3; s.minHoleDiameter = 0.5;
      s.maxSelfSupportingHoleDia = 8; s.minOverhangAngle = 45; s.minGap = 0.3; s.maxAspectRatio = 8;
      s.minEscapeHoleDia = 2.0; s.buildX = 250; s.buildY = 250; s.buildZ = 300; s.layerThickness = 0.03;
      break;
    case AmProcess::EBM_Metal:
      // Electron beam melting (Arcam, Ti-6Al-4V, 50-90 um layers, hot sintered cake): coarser beam
      // and surface -> walls >= 0.6 mm, features >= 0.6 mm; sintered cake partly supports overhangs
      // but heat-sink supports still recommended below 45 deg; the sintered powder is hard to blast out
      // -> larger gaps (>= 0.8 mm) and escape holes (>= 3 mm). Arcam A2X envelope 200x200x380.
      s.material = "Ti-6Al-4V";
      s.minWallThickness = 0.6; s.minFeatureSize = 0.6; s.minHoleDiameter = 1.0;
      s.maxSelfSupportingHoleDia = 10; s.minOverhangAngle = 45; s.minGap = 0.8; s.maxAspectRatio = 8;
      s.minEscapeHoleDia = 3.0; s.buildX = 200; s.buildY = 200; s.buildZ = 380; s.layerThickness = 0.07;
      break;
    case AmProcess::DED_Metal:
      // Directed energy deposition (blown powder / wire, IN718/Ti64): bead width 1-3 mm, layer
      // ~0.5 mm -> walls >= 2 mm, holes are machined afterwards (>= 4 mm as-built), no support
      // structures possible -> overhang limit ~60 deg from the plate (30 deg from vertical),
      // horizontal holes effectively not self-supporting above ~4 mm. No powder bed -> no trapped powder.
      s.material = "Inconel 718";
      s.minWallThickness = 2.0; s.minFeatureSize = 1.5; s.minHoleDiameter = 4.0;
      s.maxSelfSupportingHoleDia = 4; s.minOverhangAngle = 60; s.minGap = 2.0; s.maxAspectRatio = 15;
      s.minEscapeHoleDia = 0; s.buildX = 600; s.buildY = 400; s.buildZ = 500; s.layerThickness = 0.5;
      break;
    case AmProcess::FDM_Polymer:
      // Fused deposition (0.4 mm nozzle, PLA/ABS/PETG): >= 2 perimeters -> 0.8 mm walls (1.2 for
      // strength), pins >= 1 mm, holes >= 2 mm (shrink ~0.2 mm), horizontal holes sag above ~6 mm,
      // overhang 45 deg, bridges up to ~10 mm, 0.5 mm clearance between moving parts, closed cavities
      // are fine (no powder). Prusa MK4-class envelope.
      s.material = "PLA";
      s.minWallThickness = 0.8; s.minFeatureSize = 1.0; s.minHoleDiameter = 2.0;
      s.maxSelfSupportingHoleDia = 6; s.minOverhangAngle = 45; s.minGap = 0.5; s.maxAspectRatio = 10;
      s.minEscapeHoleDia = 0; s.buildX = 250; s.buildY = 210; s.buildZ = 220; s.layerThickness = 0.2;
      break;
    case AmProcess::SLA_Resin:
      // Vat photopolymerisation (Formlabs-class, 50 um): unsupported walls >= 0.5 mm, details 0.3 mm,
      // holes >= 0.5 mm, overhangs need supports below ~30 deg, uncured resin must drain through
      // holes >= 3.5 mm (cupping / suction on hollow parts). Form 3 envelope 145x145x185.
      s.material = "Standard Resin";
      s.minWallThickness = 0.5; s.minFeatureSize = 0.3; s.minHoleDiameter = 0.5;
      s.maxSelfSupportingHoleDia = 10; s.minOverhangAngle = 30; s.minGap = 0.5; s.maxAspectRatio = 8;
      s.minEscapeHoleDia = 3.5; s.buildX = 145; s.buildY = 145; s.buildZ = 185; s.layerThickness = 0.05;
      break;
    case AmProcess::SLS_Polymer:
      // Selective laser sintering (PA12): unsintered powder supports everything -> no overhang or
      // horizontal-hole limits; walls >= 0.7 mm, holes >= 1.5 mm, 0.5 mm clearance, escape holes
      // >= 3.5 mm for hollow parts. EOS P396 envelope 340x340x600.
      s.material = "PA12";
      s.minWallThickness = 0.7; s.minFeatureSize = 0.8; s.minHoleDiameter = 1.5;
      s.maxSelfSupportingHoleDia = 1e6; s.minOverhangAngle = 0; s.minGap = 0.5; s.maxAspectRatio = 10;
      s.minEscapeHoleDia = 3.5; s.buildX = 340; s.buildY = 340; s.buildZ = 600; s.layerThickness = 0.1;
      break;
  }
  return s;
}

ConvSettings convPreset(ConvProcess p) {
  ConvSettings s;
  s.process = p;
  switch (p) {
    case ConvProcess::None:
      s.enabled = false;
      break;
    case ConvProcess::CNC_Milling3Axis:
      // 3-axis VMC, Al 7075: smallest economical end mill ~3 mm (internal vertical corner R >= 1.5),
      // standard end mill reach ~4xD, twist drills ~10xD (deeper = gun drilling), metal walls >= 0.8 mm
      // and height/thickness <= ~15 (chatter / deflection), holes >= 1 mm.
      s.material = "Al 7075";
      s.minToolDiameter = 3.0; s.maxToolLengthToDia = 4.0; s.maxHoleDepthToDia = 10.0;
      s.minWallThickness = 0.8; s.maxWallHeightToThickness = 15.0; s.minHoleDiameter = 1.0;
      s.stockX = 300; s.stockY = 300; s.stockZ = 150;
      break;
    case ConvProcess::CNC_Milling5Axis:
      // 5-axis: tilting allows shorter tools (reach ~6xD) and access to inclined faces; same tool
      // and wall limits; trunnion table ~500 mm.
      s.material = "Ti-6Al-4V";
      s.minToolDiameter = 3.0; s.maxToolLengthToDia = 6.0; s.maxHoleDepthToDia = 10.0;
      s.minWallThickness = 0.8; s.maxWallHeightToThickness = 20.0; s.minHoleDiameter = 1.0;
      s.stockX = 500; s.stockY = 500; s.stockZ = 400;
      break;
    case ConvProcess::CNC_Turning:
      // Lathe (spindle = tool axis): smallest boring bar ~4 mm with ~4xD overhang, axial drilling
      // ~10xD, thin-walled tubes >= 0.8 mm; bar stock Ø100 x 300 (stockX = bar Ø, stockZ = length).
      s.material = "Steel 1.4404";
      s.minToolDiameter = 4.0; s.maxToolLengthToDia = 4.0; s.maxHoleDepthToDia = 10.0;
      s.minWallThickness = 0.8; s.maxWallHeightToThickness = 15.0; s.minHoleDiameter = 1.0;
      s.stockX = 100; s.stockY = 100; s.stockZ = 300;
      break;
  }
  return s;
}

// ============================================================================ drills
const std::vector<double>& standardDrillSizes() {
  static const std::vector<double> v = [] {
    std::vector<double> d;
    for (int i = 3; i <= 30; ++i) d.push_back(i / 10.0);          // 0.3 .. 3.0 in 0.1 steps (ISO 235 / DIN 338)
    for (int i = 7; i <= 40; ++i) d.push_back(i * 0.5);           // 3.5 .. 20 in 0.5 steps
    for (int i = 21; i <= 50; ++i) d.push_back(i);                // 21 .. 50 in 1 mm steps
    // tapping drills (ISO 2306): M4 3.3, M5 4.2, M8 6.8, M10 8.5, M12 10.2, M20 17.5
    // clearance holes (ISO 273 medium): M3 3.4, M4 4.5, M5 5.5, M6 6.6, M8 9, M10 11, M12 13.5,
    //                                   M14 15.5, M16 17.5, M20 22, M24 26, M30 33
    for (double x : {3.3, 3.4, 4.2, 4.5, 5.5, 6.6, 6.8, 8.5, 9.0, 10.2, 11.0, 13.5, 15.5, 17.5, 22.0, 26.0, 33.0})
      d.push_back(x);
    std::sort(d.begin(), d.end());
    std::vector<double> u;
    for (double x : d)
      if (u.empty() || std::fabs(u.back() - x) > 1e-9) u.push_back(x);
    return u;
  }();
  return v;
}

double nearestStandardDrill(double d) {
  const auto& v = standardDrillSizes();
  if (!(d > 0)) return v.front();
  if (d > v.back() + 0.5) return d;  // bored / interpolated, no standard drill
  double best = v.front();
  for (double x : v)
    if (std::fabs(x - d) < std::fabs(best - d) - 1e-12) best = x;
  return best;
}

// ============================================================================ checkParameters
static const char* kNumericProps[] = {"distance", "draft", "angle", "diameter", "depth", "cbDiameter", "cbDepth",
                                      "csDiameter", "csAngle", "tipAngle", "radius", "thickness", "count", "spacing",
                                      "dirX", "dirY", "dirZ"};

// ---- PARAM.RANGE on derived parameters (design relations such as "wall = D/2 - h/cos(a)" with min 0) ----------
// A derived parameter cannot be set directly, so "set it to the limit" is not a usable suggestion. Find its input
// parameters (plain numbers, transitively; exposed ones first) and solve for the value of ONE input that brings
// the derived quantity back to its limit (scan + bisection inside the input's own range).
static bool isDerivedParam(const Parameter& p) {
  try { return !referencedNames(p.expression).empty(); } catch (...) { return false; }
}
static void collectInputs(const Document& doc, const Parameter& p, std::set<std::string>& seen, std::vector<std::string>& out) {
  std::vector<std::string> refs;
  try { refs = referencedNames(p.expression); } catch (...) { return; }
  for (auto& n : refs) {
    if (!seen.insert(n).second) continue;
    const Parameter* q = doc.params.find(n);
    if (!q) continue;
    if (isDerivedParam(*q)) collectInputs(doc, *q, seen, out);
    else out.push_back(n);
  }
}
static bool suggestForDerived(const Document& doc, const ParamValues& values, const Parameter& p, double lim, bool low,
                              std::string& outName, double& outValue, std::vector<std::string>& outParams) {
  std::set<std::string> seen;
  std::vector<std::string> inputs;
  collectInputs(doc, p, seen, inputs);
  std::stable_sort(inputs.begin(), inputs.end(), [&](const std::string& a, const std::string& b) {
    return doc.params.find(a)->exposed > doc.params.find(b)->exposed;
  });
  std::map<std::string, std::string> base;   // current values of all plain inputs (keeps other overrides)
  for (auto& q : doc.params.all())
    if (!isDerivedParam(q) && values.count(q.name)) base[q.name] = formatNumber(values.at(q.name));
  auto evalAt = [&](const std::string& name, double x, double& y) {
    auto ov = base;
    ov[name] = formatNumber(x);
    auto r = doc.params.evaluate(nullptr, ov);
    if (!r || !r->count(p.name) || !std::isfinite(r->at(p.name))) return false;
    y = r->at(p.name);
    return true;
  };
  auto okAt = [&](double y) { return low ? y >= lim - 1e-12 : y <= lim + 1e-12; };
  bool found = false;
  for (auto& name : inputs) {
    const Parameter* q = doc.params.find(name);
    if (!q || !values.count(name) || !std::isfinite(q->minValue) || !std::isfinite(q->maxValue)) continue;
    const double cur = values.at(name), a = q->minValue, b = q->maxValue;
    if (!(b > a)) continue;
    // scan for the feasible point closest to the current value
    const int N = 200;
    double best = 0, bestDist = 1e300;
    bool have = false;
    for (int i = 0; i <= N; ++i) {
      double x = a + (b - a) * i / N, y;
      if (q->kind == ParamKind::Count) x = std::round(x);
      if (!evalAt(name, x, y) || !okAt(y)) continue;
      if (std::fabs(x - cur) < bestDist) { bestDist = std::fabs(x - cur); best = x; have = true; }
    }
    if (!have) continue;
    if (q->kind != ParamKind::Count) {
      // refine towards the current value (infeasible side) by bisection, then round to the UI step on the feasible side
      double lo = best, hi = std::clamp(cur, a, b), y;
      for (int k = 0; k < 40; ++k) {
        double mid = 0.5 * (lo + hi);
        if (evalAt(name, mid, y) && okAt(y)) lo = mid; else hi = mid;
      }
      best = lo;
      const double step = q->step > 0 ? q->step : 0;
      if (step > 0) {
        const double dir = best >= cur ? 1.0 : -1.0;   // feasible side
        double xr = dir > 0 ? std::ceil(best / step - 1e-9) * step : std::floor(best / step + 1e-9) * step;
        xr = std::clamp(xr, a, b);
        if (evalAt(name, xr, y) && okAt(y)) best = xr;
      }
    }
    if (!found) { outName = name; outValue = best; found = true; outParams.clear(); }
    outParams.push_back(name);
  }
  if (found) {   // the solved parameter first
    auto it = std::find(outParams.begin(), outParams.end(), outName);
    if (it != outParams.end()) std::rotate(outParams.begin(), it, it + 1);
  }
  return found;
}

Report checkParameters(const Document& doc, const ParamValues& values) {
  Report r;
  using detail::fmt;
  // evaluation errors (missing / non-finite values)
  bool missing = false;
  for (auto& p : doc.params.all())
    if (!values.count(p.name)) missing = true;
  if (missing) {
    std::map<std::string, std::string> ov;
    for (auto& [k, v] : values)
      if (doc.params.find(k)) ov[k] = formatNumber(v);
    std::vector<ParamError> errs;
    doc.params.evaluate(&errs, ov);
    std::set<std::string> reported;
    for (auto& e : errs) {
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.EVAL";
      is.params = {e.param};
      is.message = "Parametre " + e.param + " değerlendirilemedi: " + e.message;
      reported.insert(e.param);
      r.issues.push_back(is);
    }
    for (auto& p : doc.params.all())
      if (!values.count(p.name) && !reported.count(p.name)) {
        Issue is;
        is.severity = Severity::Error;
        is.domain = Domain::Parameter;
        is.ruleId = "PARAM.EVAL";
        is.params = {p.name};
        is.message = "Parametre " + p.name + " için değer yok (ifade: \"" + p.expression + "\").";
        r.issues.push_back(is);
      }
  }
  for (auto& p : doc.params.all()) {
    auto it = values.find(p.name);
    if (it == values.end()) continue;
    double v = it->second;
    if (!std::isfinite(v)) {
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.EVAL";
      is.params = {p.name};
      is.message = "Parametre " + p.name + " geçersiz değer (NaN/sonsuz).";
      r.issues.push_back(is);
      continue;
    }
    const char* unit = p.kind == ParamKind::Length ? " mm" : p.kind == ParamKind::Angle ? "°" : "";
    if (v < p.minValue - 1e-12 || v > p.maxValue + 1e-12) {
      bool low = v < p.minValue;
      double lim = low ? p.minValue : p.maxValue;
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.RANGE";
      is.params = {p.name};
      is.actual = v;
      is.limit = lim;
      std::string lbl = p.label.empty() ? p.name : p.label;
      is.message = lbl + " = " + fmt(v) + unit + (low ? " < izin verilen en küçük değer " : " > izin verilen en büyük değer ") +
                   fmt(lim) + unit + ".";
      std::string drvName;
      double drvValue = 0;
      if (!isDerivedParam(p)) {
        is.suggestedValue = lim;
        is.message += " " + p.name + " = " + fmt(lim) + unit + " önerilir.";
      } else if (suggestForDerived(doc, values, p, lim, low, drvName, drvValue, is.params)) {
        // a derived quantity (design relation) is out of range: propose a value for one of its INPUT parameters
        const Parameter* q = doc.params.find(drvName);
        const char* qu = q && q->kind == ParamKind::Length ? " mm" : q && q->kind == ParamKind::Angle ? "°" : "";
        is.suggestedValue = drvValue;
        is.message += " Türetilen büyüklük (" + p.name + " = " + p.expression + "); " + drvName + " = " + fmt(drvValue) + qu + " önerilir.";
      } else {
        is.message += " Türetilen büyüklük (" + p.name + " = " + p.expression + "); bağlı olduğu girdi parametrelerini değiştirin.";
      }
      r.issues.push_back(is);
    }
    if (p.kind == ParamKind::Count && std::fabs(v - std::round(v)) > 1e-9) {
      double s = std::clamp(std::round(v), std::ceil(p.minValue - 1e-9), std::floor(p.maxValue + 1e-9));
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.COUNT";
      is.params = {p.name};
      is.actual = v;
      is.limit = s;
      is.suggestedValue = s;
      is.message = p.name + " = " + fmt(v, 3) + " adet sayısı tam sayı olmalı. " + p.name + " = " + fmt(s, 0) + " önerilir.";
      r.issues.push_back(is);
    }
  }
  if (missing) return r;
  // feature property / sketch dimension expressions
  for (auto& f : doc.features) {
    if (f.suppressed) continue;
    for (const char* key : kNumericProps) {
      auto it = f.props.find(key);
      if (it == f.props.end() || it->second.empty()) continue;
      try {
        double x = evalWith(it->second, values);
        if (!std::isfinite(x)) throw ExprError("NaN");
      } catch (const std::exception& ex) {
        Issue is;
        is.severity = Severity::Error;
        is.domain = Domain::Parameter;
        is.ruleId = "PARAM.FEATURE_EXPR";
        is.featureId = f.id;
        try {
          for (auto& n : referencedNames(it->second))
            if (!values.count(n)) is.params.push_back(n);
        } catch (const std::exception&) {}
        is.message = f.id + "." + key + " ifadesi \"" + it->second + "\" değerlendirilemedi: " + ex.what();
        r.issues.push_back(is);
      }
    }
    if (f.sketch) {
      for (auto& c : f.sketch->constraints) {
        if (c.valueExpr.empty()) continue;
        try {
          double x = evalWith(c.valueExpr, values);
          if (!std::isfinite(x)) throw ExprError("NaN");
        } catch (const std::exception& ex) {
          Issue is;
          is.severity = Severity::Error;
          is.domain = Domain::Parameter;
          is.ruleId = "PARAM.SKETCH_EXPR";
          is.featureId = f.id;
          try {
            for (auto& n : referencedNames(c.valueExpr))
              if (!values.count(n)) is.params.push_back(n);
          } catch (const std::exception&) {}
          is.message = f.id + " ölçüsü #" + std::to_string(c.id) + " \"" + c.valueExpr + "\" değerlendirilemedi: " + ex.what();
          r.issues.push_back(is);
        }
      }
    }
  }
  return r;
}

Report checkAnalytic(const Document& doc, const ParamValues& values, const MfgSettings& s) {
  return detail::checkAnalyticImpl(doc, values, s);
}

Report checkGeometric(const Document& doc, const model::ModelResult& model, const MfgSettings& s) {
  resetSimdUpperState();   // pc/SimdState.h
  return detail::checkGeometricImpl(doc, model, s);
}

static void sortBySeverity(std::vector<Issue>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Issue& a, const Issue& b) { return int(a.severity) > int(b.severity); });
}

ValidationResult validateChange(const Document& doc, const std::map<std::string, std::string>& overrides,
                                const MfgSettings& s) {
  ValidationResult vr;
  std::vector<ParamError> errs;
  auto v = doc.params.evaluate(&errs, overrides);
  if (!v) {
    for (auto& e : errs) {
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.EVAL";
      is.params = {e.param};
      is.message = "Parametre " + e.param + " değerlendirilemedi: " + e.message;
      vr.report.issues.push_back(is);
    }
    if (vr.report.issues.empty()) {
      Issue is;
      is.severity = Severity::Error;
      is.domain = Domain::Parameter;
      is.ruleId = "PARAM.EVAL";
      is.message = "Parametreler değerlendirilemedi.";
      vr.report.issues.push_back(is);
    }
    return vr;
  }
  vr.values = *v;
  Report pr = checkParameters(doc, vr.values);
  vr.report.issues = pr.issues;
  if (pr.hasErrors()) {
    sortBySeverity(vr.report.issues);
    return vr;
  }
  // Analytic phase 1 runs before regeneration. Documents with sketches the geometry-free model cannot place
  // (OnPlane datum-plane sketches, OnFace on Combine faces) run the analytic rules AFTER regeneration instead
  // ("phase 2"), with those frames taken from the regenerated model, so every feature gets the
  // parameter-driven issues + suggestions. Phase 1 is the fallback when regeneration throws.
  const bool phase2 = detail::needsPhase2(doc);
  Report ar;
  if (!phase2) ar = checkAnalytic(doc, vr.values, s);
  Issue regenErr;
  try {
    vr.model = model::regenerate(doc, vr.values);
    vr.regenerated = true;
  } catch (const std::exception& ex) {
    regenErr.severity = Severity::Error;
    regenErr.domain = Domain::Model;
    regenErr.ruleId = "MODEL.REGENERATE";
    regenErr.message = std::string("Model yeniden oluşturulamadı: ") + ex.what();
  }
  if (phase2) ar = vr.regenerated ? detail::checkAnalyticWithModel(doc, vr.values, s, vr.model) : checkAnalytic(doc, vr.values, s);
  if (!vr.regenerated) ar.issues.push_back(regenErr);
  if (vr.regenerated) {
    Report gr = checkGeometric(doc, vr.model, s);
    // Enclosed-void verdicts of the analytic (geometry-free) model are only estimates: it does not
    // model revolves/combines, so a cut into a revolved body can look "enclosed". When the B-Rep
    // exists, voids are decided exactly by the shell count (geometric voidRule): drop analytic
    // void issues the geometry does not confirm.
    if (vr.model.ok && !vr.model.shape.IsNull()) {
      auto isVoid = [](const Issue& i) { return i.ruleId == "CNC.ENCLOSED_VOID" || i.ruleId == "AM.TRAPPED_POWDER"; };
      bool geoVoid = false;
      for (auto& i : gr.issues) geoVoid = geoVoid || isVoid(i);
      if (!geoVoid) ar.issues.erase(std::remove_if(ar.issues.begin(), ar.issues.end(), isVoid), ar.issues.end());
    }
    if (phase2) detail::mergeByFaces(doc, ar.issues, gr.issues);
    else detail::mergeInto(ar.issues, gr.issues);
  }
  for (auto& i : ar.issues) vr.report.issues.push_back(i);
  sortBySeverity(vr.report.issues);
  return vr;
}

// ============================================================================ merge
namespace detail {

static int sevRank(Severity s) { return int(s); }

// "worse" = larger relative violation; for min-type rules actual < limit, for max-type actual > limit
static double badness(const Issue& i) {
  if (i.limit == 0 && i.actual == 0) return 0;
  double l = std::fabs(i.limit) > 1e-12 ? std::fabs(i.limit) : 1.0;
  return std::fabs(i.actual - i.limit) / l;
}

static void absorb(Issue& keep, const Issue& other) {
  for (auto& f : other.faces)
    if (std::find(keep.faces.begin(), keep.faces.end(), f) == keep.faces.end()) keep.faces.push_back(f);
  for (auto& e : other.edges)
    if (std::find(keep.edges.begin(), keep.edges.end(), e) == keep.edges.end()) keep.edges.push_back(e);
  for (auto& p : other.params)
    if (std::find(keep.params.begin(), keep.params.end(), p) == keep.params.end()) keep.params.push_back(p);
  if (sevRank(other.severity) > sevRank(keep.severity)) keep.severity = other.severity;
}

void dedupe(std::vector<Issue>& issues) {
  std::vector<Issue> out;
  for (auto& is : issues) {
    std::string key = is.ruleId + "|" + is.featureId + "|" + (is.params.empty() ? "" : is.params[0]) + "|" + is.body;
    bool merged = false;
    for (auto& o : out) {
      std::string k2 = o.ruleId + "|" + o.featureId + "|" + (o.params.empty() ? "" : o.params[0]) + "|" + o.body;
      if (k2 != key) continue;
      if (badness(is) > badness(o) + 1e-12) {
        Issue n = is;
        absorb(n, o);
        o = n;
      } else {
        absorb(o, is);
      }
      merged = true;
      break;
    }
    if (!merged) out.push_back(is);
  }
  issues.swap(out);
}

// v3 (phase 2): mergeInto, then a geometric issue that is still separate is absorbed into an analytic issue of
// the same rule and body when it highlights one of the analytic issue's faces (the B-Rep pass may attribute a face
// of a Combine/kept-tool body to another feature id) or comes from a Fillet/Chamfer on an edge of such a face (thin
// wall measured on the fillet of a thin rib); the analytic issue keeps its parameters and suggestion.
void mergeByFaces(const Document& doc, std::vector<Issue>& dst, const std::vector<Issue>& src) {
  const size_t nAnalytic = dst.size();
  mergeInto(dst, src);
  std::vector<Issue> out(dst.begin(), dst.begin() + nAnalytic);
  for (size_t gi = nAnalytic; gi < dst.size(); ++gi) {
    const Issue& g = dst[gi];
    Issue* match = nullptr;
    for (auto& a : out) {
      if (a.ruleId != g.ruleId || a.body != g.body || a.params.empty()) continue;
      for (auto& f : g.faces)
        if (std::find(a.faces.begin(), a.faces.end(), f) != a.faces.end()) { match = &a; break; }
      if (!match && !g.featureId.empty() && g.featureId != a.featureId) {
        const Feature* ft = doc.feature(g.featureId);
        if (ft && (ft->type == FeatureType::Fillet || ft->type == FeatureType::Chamfer))
          for (auto& r : ft->refs)
            for (auto& f : a.faces) {
              auto pos = r.find(f);
              if (pos == std::string::npos) continue;
              char after = pos + f.size() < r.size() ? r[pos + f.size()] : ')';
              char before = pos > 0 ? r[pos - 1] : '(';
              if ((after == '|' || after == ')' || after == ':') && (before == '|' || before == '(' || before == ':'))
                match = &a;
            }
      }
      if (match) break;
    }
    if (!match) {
      out.push_back(g);
      continue;
    }
    for (auto& f : g.faces)
      if (std::find(match->faces.begin(), match->faces.end(), f) == match->faces.end()) match->faces.push_back(f);
    for (auto& e : g.edges)
      if (std::find(match->edges.begin(), match->edges.end(), e) == match->edges.end()) match->edges.push_back(e);
    if (sevRank(g.severity) > sevRank(match->severity)) match->severity = g.severity;
  }
  dst.swap(out);
}

void mergeInto(std::vector<Issue>& dst, const std::vector<Issue>& src) {
  for (auto& g : src) {
    Issue* match = nullptr;
    for (auto& a : dst)
      if (a.ruleId == g.ruleId && a.featureId == g.featureId && (a.body == g.body || a.body.empty() || g.body.empty())) { match = &a; break; }
    if (!match) {
      dst.push_back(g);
      continue;
    }
    // analytic issue keeps its parameter + suggestion; geometric adds faces/edges and may be worse
    Issue keep = *match;
    if (keep.body.empty()) keep.body = g.body;
    for (auto& f : g.faces)
      if (std::find(keep.faces.begin(), keep.faces.end(), f) == keep.faces.end()) keep.faces.push_back(f);
    for (auto& e : g.edges)
      if (std::find(keep.edges.begin(), keep.edges.end(), e) == keep.edges.end()) keep.edges.push_back(e);
    if (keep.params.empty()) {
      keep.params = g.params;
      keep.suggestedValue = g.suggestedValue;
    }
    if (sevRank(g.severity) > sevRank(keep.severity)) keep.severity = g.severity;
    *match = keep;
  }
}

}  // namespace detail
}  // namespace pc::mfg
