// PersistCAD model — persistent naming implementation (see pc/Feature.h contract).
#include "pc/ModelNaming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <set>

namespace pc::model::naming {

std::map<std::string, TopoDS_Face> NamedShape::byName() const {
  // Explore the shape so returned faces carry their in-shape orientation (outward normal semantics).
  std::map<std::string, TopoDS_Face> m;
  TopTools_IndexedMapOfShape seen;
  for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
    if (seen.Contains(ex.Current())) continue;
    seen.Add(ex.Current());
    const std::string* n = faces.Seek(ex.Current());
    if (n) m[*n] = TopoDS::Face(ex.Current());
  }
  return m;
}

std::string NamedShape::nameOf(const TopoDS_Shape& face) const {
  const std::string* s = faces.Seek(face);
  return s ? *s : std::string();
}

gp_Pnt faceCentroid(const TopoDS_Shape& face) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(face, g);
  if (g.Mass() > 1e-12) return g.CentreOfMass();
  // degenerate: fall back to vertex average
  gp_XYZ s(0, 0, 0);
  int n = 0;
  for (TopExp_Explorer ex(face, TopAbs_VERTEX); ex.More(); ex.Next()) {
    s += BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).XYZ();
    ++n;
  }
  return n ? gp_Pnt(s / n) : gp_Pnt();
}

bool pointLess(const gp_Pnt& a, const gp_Pnt& b) {
  const double tol = 1e-7;
  if (std::abs(a.X() - b.X()) > tol) return a.X() < b.X();
  if (std::abs(a.Y() - b.Y()) > tol) return a.Y() < b.Y();
  if (std::abs(a.Z() - b.Z()) > tol) return a.Z() < b.Z();
  return false;
}

static void sortByCentroid(std::vector<TopoDS_Shape>& faces) {
  std::vector<std::pair<gp_Pnt, TopoDS_Shape>> v;
  v.reserve(faces.size());
  for (auto& f : faces) v.emplace_back(faceCentroid(f), f);
  std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return pointLess(a.first, b.first); });
  for (size_t i = 0; i < v.size(); ++i) faces[i] = v[i].second;
}

NameMap finalize(const TopoDS_Shape& result, const std::vector<Candidate>& cands, const std::string& fid,
                 std::vector<std::string>* log) {
  TopTools_IndexedMapOfShape rf;
  TopExp::MapShapes(result, TopAbs_FACE, rf);

  // best candidate per result face
  std::vector<const Candidate*> best(rf.Extent() + 1, nullptr);
  for (const auto& c : cands) {
    int idx = rf.FindIndex(c.face);
    if (idx == 0) continue;
    const Candidate*& b = best[idx];
    if (!b || c.prio < b->prio || (c.prio == b->prio && c.name < b->name)) b = &c;
  }
  // group by name
  std::map<std::string, std::vector<TopoDS_Shape>> groups;
  std::vector<TopoDS_Shape> unnamed;
  for (int i = 1; i <= rf.Extent(); ++i) {
    if (best[i]) groups[best[i]->name].push_back(rf(i));
    else unnamed.push_back(rf(i));
  }
  NameMap out;
  std::set<std::string> used;
  for (auto& [name, fs] : groups) {
    if (fs.size() == 1) {
      out.Bind(fs[0], name);
      used.insert(name);
    } else {
      sortByCentroid(fs);
      for (size_t k = 0; k < fs.size(); ++k) {
        std::string n = name + ":" + std::to_string(k + 1);
        out.Bind(fs[k], n);
        used.insert(n);
      }
    }
  }
  if (!unnamed.empty()) {
    sortByCentroid(unnamed);
    int k = 0;
    for (auto& f : unnamed) {
      std::string n;
      do { n = fid + ".u" + std::to_string(k++); } while (used.count(n));
      used.insert(n);
      out.Bind(f, n);
      if (log) log->push_back("unnamed face after " + fid + " -> fallback name " + n);
    }
  }
  // Defensive uniqueness pass (a split suffix could collide with an existing name).
  std::map<std::string, int> cnt;
  for (NameMap::Iterator it(out); it.More(); it.Next()) cnt[it.Value()]++;
  bool dup = false;
  for (auto& [n, c] : cnt) dup |= c > 1;
  if (dup) {
    std::map<std::string, std::vector<TopoDS_Shape>> g2;
    for (NameMap::Iterator it(out); it.More(); it.Next()) g2[it.Value()].push_back(it.Key());
    for (auto& [n, fs] : g2) {
      if (fs.size() < 2) continue;
      sortByCentroid(fs);
      for (size_t k = 0; k < fs.size(); ++k) out.ChangeFind(fs[k]) = n + ":d" + std::to_string(k + 1);
      if (log) log->push_back("duplicate name resolved: " + n);
    }
  }
  return out;
}

void collectCandidates(BRepBuilderAPI_MakeShape& op, const NamedShape& in, int prio,
                       const TopTools_IndexedMapOfShape& resultFaces, std::vector<Candidate>& out) {
  for (NameMap::Iterator it(in.faces); it.More(); it.Next()) {
    const TopoDS_Shape& f = it.Key();
    bool deleted = false;
    try { deleted = op.IsDeleted(f); } catch (...) { deleted = false; }
    bool any = false;
    try {
      const TopTools_ListOfShape& mods = op.Modified(f);
      for (TopTools_ListIteratorOfListOfShape li(mods); li.More(); li.Next()) {
        if (li.Value().ShapeType() != TopAbs_FACE) continue;
        if (resultFaces.Contains(li.Value())) {
          out.push_back({li.Value(), it.Value(), prio});
          any = true;
        }
      }
    } catch (...) {
    }
    if (!any && resultFaces.Contains(f)) {
      out.push_back({f, it.Value(), prio});
      any = true;
    }
    (void)deleted;
  }
}

NameMap propagate(BRepBuilderAPI_MakeShape& op, const std::vector<std::pair<const NamedShape*, int>>& inputs,
                  const TopoDS_Shape& result, const std::vector<Candidate>& extra, const std::string& fid,
                  std::vector<std::string>* log) {
  TopTools_IndexedMapOfShape rf;
  TopExp::MapShapes(result, TopAbs_FACE, rf);
  std::vector<Candidate> cands;
  for (auto& [ns, prio] : inputs) collectCandidates(op, *ns, prio, rf, cands);
  for (auto& c : extra) cands.push_back(c);
  return finalize(result, cands, fid, log);
}

static gp_Pnt edgeMid(const TopoDS_Edge& e) {
  BRepAdaptor_Curve c(e);
  return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
}

std::map<std::string, TopoDS_Edge> nameEdges(const TopoDS_Shape& shape, const NameMap& faces) {
  TopTools_IndexedDataMapOfShapeListOfShape anc;
  TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, anc);
  std::map<std::string, std::vector<TopoDS_Edge>> groups;
  for (int i = 1; i <= anc.Extent(); ++i) {
    const TopoDS_Edge& e = TopoDS::Edge(anc.FindKey(i));
    if (BRep_Tool::Degenerated(e)) continue;
    std::set<std::string> names;
    for (TopTools_ListIteratorOfListOfShape li(anc(i)); li.More(); li.Next()) {
      const std::string* n = faces.Seek(li.Value());
      names.insert(n ? *n : std::string("?"));
    }
    if (names.empty()) continue;
    std::string key = "edge(";
    bool first = true;
    for (auto& n : names) {
      if (!first) key += "|";
      key += n;
      first = false;
    }
    key += ")";
    groups[key].push_back(e);
  }
  std::map<std::string, TopoDS_Edge> out;
  for (auto& [key, es] : groups) {
    if (es.size() == 1) {
      out[key] = es[0];
      continue;
    }
    std::vector<std::pair<gp_Pnt, TopoDS_Edge>> v;
    for (auto& e : es) v.emplace_back(edgeMid(e), e);
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return pointLess(a.first, b.first); });
    for (size_t k = 0; k < v.size(); ++k) out[key + ":" + std::to_string(k + 1)] = v[k].second;
  }
  return out;
}

} // namespace pc::model::naming
