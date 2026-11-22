// PersistCAD model — Up To references (SC-PULLUPTO). See pc/UpTo.h.
#include "pc/UpTo.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <cmath>
#include <regex>

namespace pc::model {

namespace {

double sOf(const gp_Ax3& fr, const gp_Pnt& p) { return gp_Vec(fr.Location(), p).Dot(gp_Vec(fr.Direction())); }

bool planeDistance(const gp_Ax3& fr, const gp_Pnt& org, const gp_Dir& n, double* s, std::string* err, const std::string& what) {
  if (std::fabs(std::fabs(n.Dot(fr.Direction())) - 1.0) > 1e-6) {
    if (err) *err = what + " sketch düzlemine paralel değil";
    return false;
  }
  *s = sOf(fr, org);
  return true;
}

// farthest (max |s|) of the given points
double extreme(const gp_Ax3& fr, const std::vector<gp_Pnt>& pts) {
  double best = 0;
  bool any = false;
  for (const auto& p : pts) {
    const double v = sOf(fr, p);
    if (!any || std::fabs(v) > std::fabs(best) + 1e-12) best = v;
    any = true;
  }
  return best;
}

std::vector<gp_Pnt> curveSamples(const TopoDS_Edge& e, int n = 16) {
  std::vector<gp_Pnt> out;
  BRepAdaptor_Curve c(e);
  const double a = c.FirstParameter(), b = c.LastParameter();
  for (int i = 0; i <= n; ++i) out.push_back(c.Value(a + (b - a) * i / n));
  return out;
}

} // namespace

std::string upToSketchOf(const std::string& ref) {
  static const std::regex re("^sketch:([^:]+):.*$");
  std::smatch m;
  if (std::regex_match(ref, m, re)) return m[1];
  return {};
}

std::string upToLabel(const std::string& ref) {
  static const std::regex reP("^sketch:([^:]+):p(\\d+)$"), reE("^sketch:([^:]+):e(\\d+)$"), reS("^sketch:([^:]+):(plane|r\\d+)$"),
      reV("^vtx:(.+):([01])$");
  std::smatch m;
  if (std::regex_match(ref, m, reP)) return std::string(m[1]) + " noktası p" + std::string(m[2]);
  if (std::regex_match(ref, m, reE)) return std::string(m[1]) + " eğrisi e" + std::string(m[2]);
  if (std::regex_match(ref, m, reS)) return std::string(m[1]) + " düzlemi";
  if (std::regex_match(ref, m, reV)) return std::string(m[1]) + " köşesi " + std::string(m[2]);
  if (ref.rfind("datum:", 0) == 0) return ref.substr(6) + " düzlemi";
  if (ref.rfind("edge(", 0) == 0) return ref + " kenarı";
  return ref + " yüzü";
}

bool upToSignedDistance(const std::string& ref, const gp_Ax3& fr, const UpToSources& src, double* s, std::string* err,
                        std::string* warn) {
  auto fail = [&](const std::string& m) {
    if (err) *err = m;
    return false;
  };
  if (ref.empty()) return fail("Kadar hedefi boş");
  try {
    static const std::regex reSk("^sketch:([^:]+):(p(\\d+)|e(\\d+)|plane|r\\d+)$"), reV("^vtx:(.+):([01])$");
    std::smatch m;
    if (std::regex_match(ref, m, reSk)) {
      const std::string sid = m[1];
      const Sketch* sk = nullptr;
      gp_Ax3 sf;
      if (!src.sketch || !src.sketch(sid, &sk, &sf) || !sk)
        return fail("Kadar hedefi sketch '" + sid + "' bulunamadı (bastırılmış ya da bu özellikten sonra)");
      auto P3 = [&](double u, double v) { return gp_Pnt(sf.Location().XYZ() + sf.XDirection().XYZ() * u + sf.YDirection().XYZ() * v); };
      if (m[3].matched) {
        const SkPoint* p = sk->point(std::stoi(std::string(m[3])));
        if (!p) return fail("Kadar hedefi nokta " + ref + " bulunamadı");
        *s = sOf(fr, P3(p->x, p->y));
        return true;
      }
      if (m[4].matched) {
        const SkEntity* e = sk->entity(std::stoi(std::string(m[4])));
        if (!e) return fail("Kadar hedefi eğri " + ref + " bulunamadı");
        std::vector<gp_Pnt> pts;
        auto pt = [&](int pid) { const SkPoint* q = sk->point(pid); return q ? P3(q->x, q->y) : P3(0, 0); };
        if (e->type == SkEntityType::Line && e->points.size() == 2) {
          pts = {pt(e->points[0]), pt(e->points[1])};
        } else if (!e->points.empty()) {
          const SkPoint* c = sk->point(e->points[0]);
          if (!c) return fail("Kadar hedefi eğri " + ref + " bulunamadı");
          double r = e->radius, a0 = 0, sw = 2 * M_PI;
          if (e->type == SkEntityType::Arc && e->points.size() == 3) {
            const SkPoint *a = sk->point(e->points[1]), *b = sk->point(e->points[2]);
            if (!a || !b) return fail("Kadar hedefi eğri " + ref + " bulunamadı");
            r = std::hypot(a->x - c->x, a->y - c->y);
            a0 = std::atan2(a->y - c->y, a->x - c->x);
            sw = std::atan2(b->y - c->y, b->x - c->x) - a0;
            while (sw <= 0) sw += 2 * M_PI;
          }
          for (int i = 0; i <= 64; ++i) {
            const double t = a0 + sw * i / 64.0;
            pts.push_back(P3(c->x + r * std::cos(t), c->y + r * std::sin(t)));
          }
        }
        if (pts.empty()) return fail("Kadar hedefi eğri " + ref + " boş");
        *s = extreme(fr, pts);
        return true;
      }
      return planeDistance(fr, sf.Location(), sf.Direction(), s, err, sid + " düzlemi");
    }
    if (ref.rfind("datum:", 0) == 0) {
      gp_Ax3 d;
      const std::string id = ref.substr(6);
      if (!src.datum || !src.datum(id, &d)) return fail("Kadar hedefi düzlem '" + id + "' bulunamadı");
      return planeDistance(fr, d.Location(), d.Direction(), s, err, id + " düzlemi");
    }
    if (std::regex_match(ref, m, reV)) {
      TopoDS_Edge e;
      if (!src.edge || !src.edge(m[1], &e)) return fail("Kadar hedefi kenar '" + std::string(m[1]) + "' bulunamadı");
      BRepAdaptor_Curve c(e);
      *s = sOf(fr, c.Value(std::string(m[2]) == "0" ? c.FirstParameter() : c.LastParameter()));
      return true;
    }
    if (ref.rfind("edge(", 0) == 0) {
      TopoDS_Edge e;
      if (!src.edge || !src.edge(ref, &e)) return fail("Kadar hedefi kenar '" + ref + "' bulunamadı");
      *s = extreme(fr, curveSamples(e));
      return true;
    }
    TopoDS_Face F;
    if (!src.face || !src.face(ref, &F)) return fail("Kadar hedefi '" + ref + "' bulunamadı");
    BRepAdaptor_Surface as(F, Standard_False);
    if (as.GetType() == GeomAbs_Plane) {
      const gp_Pln pl = as.Plane();
      if (std::fabs(std::fabs(pl.Axis().Direction().Dot(fr.Direction())) - 1.0) <= 1e-6) {
        *s = sOf(fr, pl.Location());
        return true;
      }
    }
    std::vector<gp_Pnt> pts;
    for (TopExp_Explorer ex(F, TopAbs_EDGE); ex.More(); ex.Next()) {
      const auto v = curveSamples(TopoDS::Edge(ex.Current()), 8);
      pts.insert(pts.end(), v.begin(), v.end());
    }
    if (pts.empty()) return fail("Kadar hedefi yüz '" + ref + "' boş");
    *s = extreme(fr, pts);
    if (warn) *warn = "Kadar: '" + ref + "' sketch düzlemine paralel bir düzlem değil; en uzak noktasına kadar uzatıldı";
    return true;
  } catch (const Standard_Failure& e) {
    return fail(std::string("Kadar hedefi hesaplanamadı (") + (e.GetMessageString() ? e.GetMessageString() : "OCCT") + ")");
  } catch (const std::exception& e) {
    return fail(std::string("Kadar hedefi hesaplanamadı (") + e.what() + ")");
  }
}

bool upToDistance(const ModelResult& model, const gp_Ax3& frame, const std::string& ref, double* s, std::string* err,
                  std::string* warn) {
  UpToSources src;
  src.sketch = [&](const std::string& sid, const Sketch** sk, gp_Ax3* fr) {
    auto it = model.solvedSketches.find(sid);
    if (it == model.solvedSketches.end()) return false;
    for (const auto& fi : model.info)
      if (fi.featureId == sid && fi.type == FeatureType::Sketch) {
        *sk = &it->second;
        *fr = fi.sketchFrame;
        return true;
      }
    return false;
  };
  src.datum = [&](const std::string& id, gp_Ax3* fr) {
    auto it = model.datumPlanes.find(id);
    if (it == model.datumPlanes.end()) return false;
    *fr = it->second.frame;
    return true;
  };
  src.face = [&](const std::string& n, TopoDS_Face* f) {
    auto it = model.faces.find(n);
    if (it == model.faces.end()) return false;
    *f = it->second;
    return true;
  };
  src.edge = [&](const std::string& n, TopoDS_Edge* e) {
    auto it = model.edges.find(n);
    if (it == model.edges.end()) return false;
    *e = it->second;
    return true;
  };
  return upToSignedDistance(ref, frame, src, s, err, warn);
}

} // namespace pc::model
