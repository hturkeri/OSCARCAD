// SC-UX (WP-B) — sketch regions / curves for the 3D view + OCCT helpers. See SketchRegions.h.
#include "SketchRegions.h"
#include "pc/SketchSolver.h"
#include "pc/Expression.h"
#include "pc/Regions.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <QRegularExpression>
#include <cmath>

using namespace pc;

namespace pcd {

bool parseSketchItemName(const QString& name, std::string* sid, char* type, int* index) {
  static const QRegularExpression re("^sketch:([^:]+):([rep])(\\d+)$");
  const auto m = re.match(name);
  if (!m.hasMatch()) return false;
  if (sid) *sid = m.captured(1).toStdString();
  if (type) *type = m.captured(2).at(0).toLatin1();
  if (index) *index = m.captured(3).toInt();
  return true;
}
QString regionName(const std::string& sid, int k) { return QString("sketch:%1:r%2").arg(QString::fromStdString(sid)).arg(k); }
QString sketchCurveName(const std::string& sid, int id) { return QString("sketch:%1:e%2").arg(QString::fromStdString(sid)).arg(id); }

bool sketchFrameOf(const model::ModelResult& model, const std::string& sid, gp_Ax3& frame) {
  for (const auto& fi : model.info)
    if (fi.featureId == sid && fi.type == FeatureType::Sketch) { frame = fi.sketchFrame; return true; }
  return false;
}

bool sketchRegionInfo(const Document& doc, const model::ModelResult& model, const std::string& sid,
                      sketch::RegionResult& out, gp_Ax3& frame) {
  out = sketch::RegionResult();
  if (!sketchFrameOf(model, sid, frame)) return false;
  const Sketch* sk = nullptr;
  auto it = model.solvedSketches.find(sid);
  if (it != model.solvedSketches.end()) sk = &it->second;
  else if (const Feature* f = doc.feature(sid); f && f->sketch) sk = &*f->sketch;
  if (!sk) return false;
  double g = -1;
  if (const Feature* f = doc.feature(sid); f && !f->prop("gapTol").empty()) {
    try { g = evalWith(f->prop("gapTol"), model.params); } catch (...) { g = -1; }
  }
  try {
    out = sketch::sketchRegions(*sk, frame, g);   // SC-PULLUPTO: tolerant regions (pc/Regions.h)
  } catch (...) {
    return false;
  }
  return true;
}

bool sketchRegionFaces(const Document& doc, const model::ModelResult& model, const std::string& sid,
                       std::vector<TopoDS_Face>& faces, gp_Ax3& frame) {
  faces.clear();
  sketch::RegionResult r;
  if (!sketchRegionInfo(doc, model, sid, r, frame)) return false;
  faces = r.profile.faces;
  return !faces.empty();
}

std::set<int> consumedRegions(const Document& doc, const std::string& sid, bool* all) {
  std::set<int> out;
  if (all) *all = false;
  for (const auto& f : doc.features) {
    if (f.suppressed || f.prop("sketch") != sid) continue;
    if (f.type != FeatureType::Extrude && f.type != FeatureType::Revolve && f.type != FeatureType::Hole) continue;
    const QString regs = QString::fromStdString(f.prop("regions")).trimmed();
    if (regs.isEmpty() || f.type == FeatureType::Hole) {
      if (all) *all = true;
      continue;
    }
    for (const QString& k : regs.split(',', Qt::SkipEmptyParts)) out.insert(k.trimmed().toInt());
  }
  return out;
}

bool sketchConsumed(const Document& doc, const std::string& sid) {
  // SC-PULLUPTO: an Extrude / Revolve of only some regions (`regions=`) leaves the sketch and its other regions shown
  bool all = false;
  consumedRegions(doc, sid, &all);
  return all;
}

std::set<std::string> unconsumedSketches(const Document& doc, int rollback) {
  std::set<std::string> out;
  const int n = rollback >= 0 ? std::min(int(doc.features.size()), rollback + 1) : int(doc.features.size());
  Document upto = doc;
  upto.features.resize(size_t(n));
  for (const auto& f : upto.features)
    if (f.type == FeatureType::Sketch && !f.suppressed && !sketchConsumed(upto, f.id)) out.insert(f.id);
  return out;
}

int appendSketchRegionFaces(const Document& doc, const model::ModelResult& model, const std::set<std::string>& sketchIds,
                            TopoDS_Compound& comp, std::map<std::string, TopoDS_Face>& faceNames) {
  BRep_Builder bb;
  if (comp.IsNull()) bb.MakeCompound(comp);
  int n = 0;
  for (const auto& sid : sketchIds) {
    std::vector<TopoDS_Face> faces;
    gp_Ax3 frame;
    if (!sketchRegionFaces(doc, model, sid, faces, frame)) continue;
    // display copy lifted a little along the normal: a sketch on a body face must not z-fight with it
    gp_Trsf lift;
    lift.SetTranslation(gp_Vec(frame.Direction()) * 0.02);
    const std::set<int> used = consumedRegions(doc, sid);   // SC-PULLUPTO: regions already pulled are not shown
    for (size_t k = 0; k < faces.size(); ++k) {
      if (used.count(int(k))) continue;
      try {
        const TopoDS_Face shown = TopoDS::Face(BRepBuilderAPI_Transform(faces[k], lift, true).Shape());
        bb.Add(comp, shown);
        faceNames[regionName(sid, int(k)).toStdString()] = shown;
        ++n;
      } catch (...) {}
    }
  }
  return n;
}

int appendSketchCurveEdges(const Document& doc, const model::ModelResult& model, const std::set<std::string>& sketchIds,
                           TopoDS_Compound& comp, std::map<std::string, TopoDS_Edge>& names) {
  BRep_Builder bb;
  if (comp.IsNull()) bb.MakeCompound(comp);
  int n = 0;
  for (const auto& sid : sketchIds) {
    gp_Ax3 fr;
    if (!sketchFrameOf(model, sid, fr)) continue;
    const Sketch* sk = nullptr;
    auto it = model.solvedSketches.find(sid);
    if (it != model.solvedSketches.end()) sk = &it->second;
    else if (const Feature* f = doc.feature(sid); f && f->sketch) sk = &*f->sketch;
    if (!sk) continue;
    const gp_Pnt O = fr.Location();
    const gp_Dir X = fr.XDirection(), Y = fr.YDirection(), N = fr.Direction();
    auto P3 = [&](int pid) {
      const SkPoint* p = sk->point(pid);
      return gp_Pnt(O.XYZ() + X.XYZ() * (p ? p->x : 0) + Y.XYZ() * (p ? p->y : 0));
    };
    for (const auto& e : sk->entities) {
      try {
        TopoDS_Edge edge;
        if (e.type == SkEntityType::Line && e.points.size() == 2) {
          const gp_Pnt a = P3(e.points[0]), b = P3(e.points[1]);
          if (a.Distance(b) < 1e-9) continue;
          edge = BRepBuilderAPI_MakeEdge(a, b);
        } else if (e.type == SkEntityType::Circle && !e.points.empty() && e.radius > 1e-9) {
          edge = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(P3(e.points[0]), N, X), e.radius));
        } else if (e.type == SkEntityType::Arc && e.points.size() == 3) {
          const gp_Pnt c = P3(e.points[0]), s0 = P3(e.points[1]), s1 = P3(e.points[2]);
          const double r = c.Distance(s0);
          if (r < 1e-9) continue;
          const gp_Ax2 ax(c, N, X);
          auto ang = [&](const gp_Pnt& q) {
            const gp_Vec d(c, q);
            return std::atan2(d.Dot(gp_Vec(ax.YDirection())), d.Dot(gp_Vec(ax.XDirection())));
          };
          double a0 = ang(s0), a1 = ang(s1);
          while (a1 <= a0) a1 += 2 * M_PI;
          edge = BRepBuilderAPI_MakeEdge(gp_Circ(ax, r), a0, a1);
        } else {
          continue;
        }
        bb.Add(comp, edge);
        names[sketchCurveName(sid, e.id).toStdString()] = edge;
        ++n;
      } catch (...) {}
    }
  }
  return n;
}

bool faceCentroidNormal(const TopoDS_Face& f, gp_Pnt& c, gp_Dir& n) {
  if (f.IsNull()) return false;
  try {
    GProp_GProps gp;
    BRepGProp::SurfaceProperties(f, gp);
    c = gp.CentreOfMass();
    Handle(Geom_Surface) s = BRep_Tool::Surface(f);
    if (s.IsNull()) return false;
    GeomAPI_ProjectPointOnSurf proj(c, s);
    double u = 0, v = 0;
    if (proj.NbPoints() > 0) proj.LowerDistanceParameters(u, v);
    BRepAdaptor_Surface as(f);
    BRepLProp_SLProps props(as, u, v, 1, 1e-7);
    if (!props.IsNormalDefined()) return false;
    gp_Dir d = props.Normal();
    if (f.Orientation() == TopAbs_REVERSED) d.Reverse();
    n = d;
    // a centroid off the face (ring, L-shape): use the surface point closest to it for the handle
    if (proj.NbPoints() > 0 && proj.LowerDistance() > 1e-6) c = proj.NearestPoint();
    return true;
  } catch (...) {
    return false;
  }
}

bool edgeMidInward(const model::ModelResult& model, const QString& edgeName, gp_Pnt& mid, gp_Dir& inward) {
  auto it = model.edges.find(edgeName.toStdString());
  if (it == model.edges.end()) return false;
  try {
    BRepAdaptor_Curve cv(it->second);
    const double t = 0.5 * (cv.FirstParameter() + cv.LastParameter());
    mid = cv.Value(t);
    gp_Vec sum(0, 0, 0);
    if (edgeName.startsWith("edge(")) {
      const QString inner = edgeName.mid(5, edgeName.lastIndexOf(')') - 5);
      for (const QString& fn : inner.split('|')) {
        auto fit = model.faces.find(fn.toStdString());
        if (fit == model.faces.end()) continue;
        Handle(Geom_Surface) s = BRep_Tool::Surface(fit->second);
        if (s.IsNull()) continue;
        GeomAPI_ProjectPointOnSurf proj(mid, s);
        if (proj.NbPoints() == 0) continue;
        double u, v;
        proj.LowerDistanceParameters(u, v);
        BRepAdaptor_Surface as(fit->second);
        BRepLProp_SLProps props(as, u, v, 1, 1e-7);
        if (!props.IsNormalDefined()) continue;
        gp_Dir d = props.Normal();
        if (fit->second.Orientation() == TopAbs_REVERSED) d.Reverse();
        sum += gp_Vec(d);
      }
    }
    if (sum.Magnitude() < 1e-9) return false;
    inward = gp_Dir(-sum);
    return true;
  } catch (...) {
    return false;
  }
}

bool faceByName(const Document& doc, const model::ModelResult& model, const QString& name, TopoDS_Face& out) {
  std::string sid;
  char type = 0;
  int k = -1;
  if (parseSketchItemName(name, &sid, &type, &k)) {
    if (type != 'r') return false;
    std::vector<TopoDS_Face> faces;
    gp_Ax3 fr;
    if (!sketchRegionFaces(doc, model, sid, faces, fr) || k < 0 || k >= int(faces.size())) return false;
    out = faces[size_t(k)];
    return true;
  }
  auto it = model.faces.find(name.toStdString());
  if (it == model.faces.end()) return false;
  out = it->second;
  return true;
}

} // namespace pcd
