// PersistCAD mfg — v3 "analytic phase 2".
//
// The analytic (geometry-free, parameter-aware) model cannot place sketches on datum planes (Plane features are
// defined on B-Rep edges/faces), nor OnFace sketches on faces it does not model. After validateChange has
// regenerated the candidate, those frames are taken from the model with exactly the rule of
// pc::model::sketchFrame (datum frame of `planeRef` translated by the sketch offset along its normal), and the
// whole analytic rule set runs again with them. Suggestion probes re-build the analytic model with other
// parameter values: the provider then re-uses the regenerated frame when no parameter that defines the plane
// changed, and otherwise regenerates the model only up to the plane feature (cached per value set).
#include "MfgInternal.h"

#include "pc/Expression.h"

#include <gp_Ax1.hxx>

#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace pc::mfg::detail {

namespace {
thread_local const FrameProvider* gProvider = nullptr;
}

const FrameProvider* currentFrameProvider() { return gProvider; }
FrameScope::FrameScope(const FrameProvider* p) : prev_(gProvider) { gProvider = p; }
FrameScope::~FrameScope() { gProvider = prev_; }

namespace {

// parameters referenced by features[0..last] (props, sketch dimensions, sketch offsets)
std::vector<std::string> paramsUpTo(const Document& doc, int last) {
  std::set<std::string> seen;
  std::vector<std::string> out;
  auto add = [&](const std::string& expr) {
    if (expr.empty()) return;
    std::vector<std::string> names;
    try {
      names = referencedNames(expr);
    } catch (const std::exception&) {
      return;
    }
    for (auto& n : names)
      if (doc.params.find(n) && seen.insert(n).second) out.push_back(n);
  };
  for (int i = 0; i <= last && i < int(doc.features.size()); ++i) {
    const Feature& f = doc.features[i];
    for (auto& [k, v] : f.props) add(v);
    if (f.sketch) {
      for (auto& c : f.sketch->constraints) add(c.valueExpr);
      add(f.sketch->plane.offsetExpr);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------- analytic datum planes
// A datum plane is defined on B-Rep edges/faces. For the suggestion probes it is re-evaluated analytically from
// the analytic model's planar faces (edge = intersection line of its two named faces; a cap/side or side/side
// pair of one prism also gives the edge's end points), with the rules of pc/Feature.h. Unknowns of the B-Rep
// (edge parameter direction, trimmed end points, which side a normal points to) are calibrated once against the
// exact datum frame of the regenerated model at the validated values: the analytic plane is used only if the
// calibrated evaluation reproduces that frame.
struct AEdge {
  bool seg = false;     // end points known (a -> b)
  gp_Pnt a, b, ref;     // ref: foot of the world origin on the line
  gp_Dir d;             // seg: a -> b, else n_A x n_B
  std::string fa, fb;   // the two faces (fa < fb)
};

std::string stripSuffix(const std::string& n) {
  auto pos = n.rfind(':');
  if (pos == std::string::npos || pos + 1 >= n.size()) return n;
  for (size_t i = pos + 1; i < n.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(n[i]))) return n;
  return n.substr(0, pos);
}

// "<fid>.end" | "<fid>.start" | "<fid>.side.e<k>" of an original (non-pattern) prism
bool prismFace(const AModel& m, const std::string& face, const Prism*& P, std::string& rest) {
  std::string n = stripSuffix(face);
  auto dot = n.find('.');
  if (dot == std::string::npos) return false;
  std::string fid = n.substr(0, dot);
  rest = n.substr(dot + 1);
  if (rest.rfind("tool.", 0) == 0) {
    fid += ".tool";
    rest = rest.substr(5);
  }
  P = m.prismByFid(fid);
  return P != nullptr;
}

bool sideSeg(const Prism& P, const std::string& rest, Seg& out) {
  if (rest.rfind("side.e", 0) != 0) return false;
  int eid = std::atoi(rest.c_str() + 6);
  for (auto& sg : P.segs)
    if (sg.entityId == eid && sg.kind == SegKind::Line) {
      out = sg;
      return true;
    }
  return false;
}

bool analyticEdge(const AModel& m, const std::string& refIn, AEdge& e) {
  std::string r = refIn;
  auto close = r.rfind(')');
  if (r.rfind("edge(", 0) != 0 || close == std::string::npos) return false;
  r = r.substr(5, close - 5);
  auto bar = r.find('|');
  if (bar == std::string::npos) return false;
  e.fa = r.substr(0, bar);
  e.fb = r.substr(bar + 1);
  gp_Pnt pa, pb;
  gp_Dir na, nb;
  if (!m.facePlane(e.fa, pa, na) || !m.facePlane(e.fb, pb, nb)) return false;
  gp_Vec dv = gp_Vec(na) ^ gp_Vec(nb);
  if (dv.Magnitude() < 1e-9) return false;
  e.d = gp_Dir(dv);
  double c = na.Dot(nb), det = 1 - c * c;
  double h1 = gp_Vec(pa.XYZ()).Dot(gp_Vec(na)), h2 = gp_Vec(pb.XYZ()).Dot(gp_Vec(nb));
  e.ref = gp_Pnt(((gp_Vec(na) * (h1 - h2 * c) + gp_Vec(nb) * (h2 - h1 * c)) / det).XYZ());
  // end points when both faces belong to the same prism
  const Prism *PA = nullptr, *PB = nullptr;
  std::string ra, rb;
  if (prismFace(m, e.fa, PA, ra) && prismFace(m, e.fb, PB, rb) && PA == PB) {
    const Prism& P = *PA;
    Seg s, s2;
    bool capA = ra == "end" || ra == "start", capB = rb == "end" || rb == "start";
    if (capA != capB) {
      const std::string& cap = capA ? e.fa : e.fb;
      const std::string& side = capA ? rb : ra;
      gp_Pnt pc;
      gp_Dir nc;
      if (sideSeg(P, side, s) && m.facePlane(cap, pc, nc)) {
        double w = gp_Vec(P.frame.Location(), pc).Dot(gp_Vec(P.frame.Direction()));
        e.a = P.toWorld(s.a, w);
        e.b = P.toWorld(s.b, w);
        e.seg = true;
      }
    } else if (!capA && sideSeg(P, ra, s) && sideSeg(P, rb, s2)) {
      for (V2 p : {s.a, s.b})
        for (V2 q : {s2.a, s2.b})
          if (norm(p - q) < 1e-7 && !e.seg) {
            e.a = P.toWorld(p, P.w0);
            e.b = P.toWorld(p, P.w1);
            e.seg = true;
          }
    }
    if (e.seg) {
      gp_Vec ab(e.a, e.b);
      if (ab.Magnitude() < 1e-9) e.seg = false;
      else e.d = gp_Dir(ab);
    }
  }
  return true;
}

// edge parameter of a point: fraction along a -> b (seg) or signed distance from ref along d
double edgeParam(const AEdge& e, const gp_Pnt& p) {
  if (e.seg) {
    gp_Vec ab(e.a, e.b);
    return gp_Vec(e.a, p).Dot(ab) / ab.SquareMagnitude();
  }
  return gp_Vec(e.ref, p).Dot(gp_Vec(e.d));
}
gp_Pnt edgePoint(const AEdge& e, double t) {
  if (e.seg) return e.a.Translated(gp_Vec(e.a, e.b) * t);
  return e.ref.Translated(gp_Vec(e.d) * t);
}

gp_Dir onFaceU(const gp_Dir& n) {
  gp_Vec N(n);
  gp_Vec u = gp_Vec(1, 0, 0) - N * n.X();
  if (u.Magnitude() < 1e-6) u = gp_Vec(0, 1, 0) - N * n.Y();
  return gp_Dir(u);
}

struct PlaneCalib {
  bool ok = false;
  int sigma = 1;            // edge curve direction = sigma * analytic direction
  int nsign = 1;            // twoEdges: normal side
  double tpar0 = 0, tpar1 = 0;   // edge parameters of the origin(s) at the calibration values
  double tBase = 0;         // value of prop `t` at the calibration values
  bool seg0 = false, seg1 = false;
};

// Evaluate a Plane feature analytically (pc/Feature.h rules). For calibration (`cal.ok == false`, `exact` given)
// the edge parameters are taken from the exact frame's origin.
bool evalPlane(const AModel& m, const Feature& pf, const ParamValues& v, const PlaneCalib& cal, int sigma, int nsign,
               const gp_Ax3* exact, double tpar0Override, gp_Ax3& out, double* tparOut = nullptr) {
  std::string kind = pf.prop("kind", "edgeNormal");
  bool ok = true;
  double offset = evalProp(pf, "offset", v, 0, &ok), rotate = evalProp(pf, "rotate", v, 0, &ok);
  bool flip = pf.prop("flip", "0") == "1";
  if (!ok) return false;
  gp_Pnt O;
  gp_Dir N, U;
  if (kind == "faceOffset") {
    if (pf.refs.empty()) return false;
    gp_Pnt p;
    gp_Dir n;
    if (!m.facePlane(pf.refs[0], p, n)) return false;
    gp_Vec NV(n);
    O = gp_Pnt(NV.XYZ() * gp_Vec(p.XYZ()).Dot(NV));
    N = n;
    U = onFaceU(n);
  } else {
    if (pf.refs.empty()) return false;
    AEdge e0;
    if (!analyticEdge(m, pf.refs[0], e0)) return false;
    double t = evalProp(pf, "t", v, kind == "edgeNormal" ? 0.0 : 0.5, &ok);
    if (!ok) return false;
    double tp;
    if (exact) tp = edgeParam(e0, exact->Location());  // calibration
    else {
      if (cal.seg0 != e0.seg) return false;
      tp = cal.tpar0;
      if (std::fabs(t - cal.tBase) > 1e-12) {
        if (!e0.seg) return false;
        tp += t - cal.tBase;
      }
    }
    if (tpar0Override == tpar0Override) tp = tpar0Override;
    if (tparOut) *tparOut = tp;
    O = edgePoint(e0, tp);
    gp_Dir D = sigma > 0 ? e0.d : e0.d.Reversed();
    if (kind == "edgeNormal") {
      N = D;
      U = onFaceU(N);
    } else if (kind == "edgeAngle") {
      std::string rf = pf.refs.size() > 1 && !pf.refs[1].empty() ? pf.refs[1] : std::min(e0.fa, e0.fb);
      gp_Pnt p;
      gp_Dir n0;
      if (!m.facePlane(rf, p, n0)) return false;
      gp_Vec np = gp_Vec(n0) - gp_Vec(D) * n0.Dot(D);
      if (np.Magnitude() < 1e-9) return false;
      double ang = evalProp(pf, "angle", v, 0, &ok);
      if (!ok) return false;
      N = gp_Dir(np).Rotated(gp_Ax1(gp_Pnt(0, 0, 0), D), ang * kPi / 180.0);
      U = D;
    } else if (kind == "twoEdges") {
      if (pf.refs.size() < 2) return false;
      AEdge e1;
      if (!analyticEdge(m, pf.refs[1], e1)) return false;
      gp_Pnt m1 = e1.seg ? gp_Pnt((e1.a.XYZ() + e1.b.XYZ()) * 0.5)
                         : e1.ref.Translated(gp_Vec(e1.d) * gp_Vec(e1.ref, O).Dot(gp_Vec(e1.d)));
      gp_Vec n = gp_Vec(D) ^ gp_Vec(O, m1);
      if (n.Magnitude() < 1e-9) n = gp_Vec(D) ^ gp_Vec(e1.d);
      if (n.Magnitude() < 1e-9) return false;
      N = gp_Dir(n * double(nsign));
      U = D;
    } else {
      return false;
    }
  }
  // U must be perpendicular to N (edgeAngle/twoEdges: by construction)
  gp_Vec uu = gp_Vec(U) - gp_Vec(N) * U.Dot(N);
  if (uu.Magnitude() < 1e-9) return false;
  U = gp_Dir(uu);
  if (std::fabs(rotate) > 0) U = U.Rotated(gp_Ax1(gp_Pnt(0, 0, 0), N), rotate * kPi / 180.0);
  if (flip) N = N.Reversed();
  O.Translate(gp_Vec(N) * offset);
  out = gp_Ax3(O, N, U);
  return true;
}

bool sameFrame(const gp_Ax3& a, const gp_Ax3& b, double scale) {
  double tol = 1e-6 * std::max(1.0, scale);
  return a.Location().Distance(b.Location()) < tol && a.Direction().IsEqual(b.Direction(), 1e-6) &&
         a.XDirection().IsEqual(b.XDirection(), 1e-6);
}

class ModelFrameProvider final : public FrameProvider {
 public:
  ModelFrameProvider(const Document& doc, const ParamValues& base, const model::ModelResult& model)
      : doc_(doc), base_(base), model_(model) {}

  bool frame(const Feature& f, const ParamValues& values, const AModel& partial, gp_Ax3& out) const override {
    if (!f.sketch) return false;
    const auto kind = f.sketch->plane.kind;
    int anchor;
    if (kind == SketchPlaneKind::OnPlane) anchor = doc_.indexOf(f.sketch->plane.planeRef);
    else if (kind == SketchPlaneKind::OnFace) anchor = doc_.indexOf(f.id) - 1;
    else return false;
    if (anchor < 0) return false;
    auto& names = relevant(anchor);
    std::vector<double> key;
    key.reserve(names.size());
    bool same = true;
    for (auto& n : names) {
      auto it = values.find(n);
      auto ib = base_.find(n);
      if (it == values.end()) return false;
      key.push_back(it->second);
      if (ib == base_.end() || std::fabs(it->second - ib->second) > 1e-12 * std::max(1.0, std::fabs(ib->second)))
        same = false;
    }
    if (kind == SketchPlaneKind::OnPlane) {
      const std::string& pid = f.sketch->plane.planeRef;
      // datum planes are re-evaluated on every regeneration and later features never change them, so the final
      // model's plane is the plane at that point of the history
      if (same) {
        if (!model::sketchFrame(doc_, f.id, values, model_, out)) return false;
        calibrate(pid, values, partial);
        return true;
      }
      // probe values: analytic re-evaluation of the datum plane when calibrated, else partial regeneration
      auto ic = calib_.find(pid);
      if (ic != calib_.end() && ic->second.ok) {
        gp_Ax3 pl;
        const Feature* pf = doc_.feature(pid);
        const PlaneCalib& c = ic->second;
        if (pf && evalPlane(partial, *pf, values, c, c.sigma, c.nsign, nullptr, std::nan(""), pl)) {
          double off = 0;
          try {
            off = f.sketch->plane.offsetExpr.empty() ? 0 : evalWith(f.sketch->plane.offsetExpr, values);
          } catch (const std::exception&) {
            return false;
          }
          out = pl;
          out.Translate(gp_Vec(out.Direction()) * off);  // as pc::model::sketchFrame
          return true;
        }
      }
    }
    // faces may be modified by later features -> model regenerated up to the sketch / plane
    const model::ModelResult* p = partialModel(anchor, key, values);
    return p && model::sketchFrame(doc_, f.id, values, *p, out);
  }

 private:
  // once per plane, at the validated values: find the B-Rep unknowns that reproduce the exact datum frame
  void calibrate(const std::string& pid, const ParamValues& values, const AModel& partial) const {
    if (calib_.count(pid)) return;
    PlaneCalib& c = calib_[pid];
    const Feature* pf = doc_.feature(pid);
    auto dp = model_.datumPlanes.find(pid);
    if (!pf || dp == model_.datumPlanes.end()) return;
    const gp_Ax3& ex = dp->second.frame;
    bool ok = true;
    c.tBase = evalProp(*pf, "t", values, pf->prop("kind", "edgeNormal") == "edgeNormal" ? 0.0 : 0.5, &ok);
    if (!ok) return;
    // the exact origin before the offset (offset moves along the final normal)
    double offset = evalProp(*pf, "offset", values, 0, &ok);
    gp_Ax3 ex0 = ex;
    ex0.Translate(gp_Vec(ex.Direction()) * -offset);
    for (int sigma : {1, -1})
      for (int nsign : {1, -1}) {
        gp_Ax3 a;
        double tp = 0;
        if (!evalPlane(partial, *pf, values, c, sigma, nsign, &ex0, std::nan(""), a, &tp)) continue;
        if (!sameFrame(a, ex, partial.scale)) continue;
        c.ok = true;
        c.sigma = sigma;
        c.nsign = nsign;
        c.tpar0 = tp;
        AEdge e0;
        if (!pf->refs.empty() && analyticEdge(partial, pf->refs[0], e0)) c.seg0 = e0.seg;
        return;
      }
  }

  const std::vector<std::string>& relevant(int anchor) const {
    auto it = relevant_.find(anchor);
    if (it == relevant_.end()) it = relevant_.emplace(anchor, paramsUpTo(doc_, anchor)).first;
    return it->second;
  }

  const model::ModelResult* partialModel(int anchor, const std::vector<double>& key, const ParamValues& values) const {
    auto ck = std::make_pair(anchor, key);
    auto it = partials_.find(ck);
    if (it != partials_.end()) return it->second.get();
    std::shared_ptr<model::ModelResult> r;
    try {
      r = std::make_shared<model::ModelResult>(model::regenerate(doc_, values, anchor));
    } catch (const std::exception&) {
      r.reset();
    }
    partials_[ck] = r;
    return r.get();
  }

  const Document& doc_;
  const ParamValues& base_;
  const model::ModelResult& model_;
  mutable std::map<int, std::vector<std::string>> relevant_;
  mutable std::map<std::string, PlaneCalib> calib_;
  mutable std::map<std::pair<int, std::vector<double>>, std::shared_ptr<model::ModelResult>> partials_;
};

}  // namespace

bool needsPhase2(const Document& doc) {
  for (auto& f : doc.features) {
    if (f.suppressed || f.type != FeatureType::Sketch || !f.sketch) continue;
    if (f.sketch->plane.kind == SketchPlaneKind::OnPlane) return true;
    if (f.sketch->plane.kind == SketchPlaneKind::OnFace) {
      // faces the analytic model does not know: Combine-contributed faces, faces of features on datum planes
      const std::string& ref = f.sketch->plane.faceRef;
      std::string fid = ref.substr(0, ref.find('.'));
      const Feature* src = doc.feature(fid);
      if (src && src->type == FeatureType::Combine) return true;
    }
  }
  return false;
}

Report checkAnalyticWithModel(const Document& doc, const ParamValues& values, const MfgSettings& s,
                              const model::ModelResult& model) {
  ModelFrameProvider prov(doc, values, model);
  FrameScope scope(&prov);
  Report r = checkAnalyticImpl(doc, values, s);
  // body attribution from the B-Rep where the analytic body simulation could not decide (AUTO ops on several
  // bodies): first highlighted face that belongs to a body
  const bool multi = model.bodies.size() > 1;
  for (auto& i : r.issues) {
    if (!i.body.empty()) continue;
    for (auto& fc : i.faces) {
      std::string b = model.bodyOfFace(fc);
      if (b.empty()) continue;
      i.body = b;
      if (multi && i.message.rfind("Gövde ", 0) != 0) i.message = "Gövde " + b + ": " + i.message;
      break;
    }
  }
  return r;
}

}  // namespace pc::mfg::detail
