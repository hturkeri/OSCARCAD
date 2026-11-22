#include "Tessellation.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_Triangulation.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TopAbs.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <cmath>

namespace pc::viewer {

namespace {
// Same purpose as pc/SimdState.h (core); duplicated because pc_viewer depends on Qt + OCCT only.
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx"), noinline)) void vzeroupperImpl() { __builtin_ia32_vzeroupper(); }
void clearDirtyAvxState() {
  static const bool hasAvx = (__builtin_cpu_init(), __builtin_cpu_supports("avx") != 0);
  if (hasAvx) vzeroupperImpl();
}
#else
void clearDirtyAvxState() {}
#endif

void growBox(MeshData& m, double x, double y, double z) {
  if (!m.hasBox) {
    m.bmin[0] = m.bmax[0] = x; m.bmin[1] = m.bmax[1] = y; m.bmin[2] = m.bmax[2] = z;
    m.hasBox = true;
    return;
  }
  m.bmin[0] = std::min(m.bmin[0], x); m.bmax[0] = std::max(m.bmax[0], x);
  m.bmin[1] = std::min(m.bmin[1], y); m.bmax[1] = std::max(m.bmax[1], y);
  m.bmin[2] = std::min(m.bmin[2], z); m.bmax[2] = std::max(m.bmax[2], z);
}
void pushLine(MeshData& m, const gp_Pnt& a, const gp_Pnt& b) {
  const float v[6] = {float(a.X()), float(a.Y()), float(a.Z()), float(b.X()), float(b.Y()), float(b.Z())};
  m.lines.insert(m.lines.end(), v, v + 6);
  // free wires (e.g. the designer's sketch overlay) must count for fitAll / clipping as well
  growBox(m, a.X(), a.Y(), a.Z());
  growBox(m, b.X(), b.Y(), b.Z());
}
} // namespace

MeshData tessellate(const TopoDS_Shape& shape, const std::map<std::string, TopoDS_Face>& faceNames,
                    const std::map<std::string, TopoDS_Edge>& edgeNames) {
  clearDirtyAvxState();   // GUI paint code may leave dirty AVX state; OCCT meshing then runs 2-3x slower
  MeshData m;
  if (shape.IsNull()) return m;

  Bnd_Box box;
  BRepBndLib::Add(shape, box);
  if (box.IsVoid()) return m;
  double x0, y0, z0, x1, y1, z1;
  box.Get(x0, y0, z0, x1, y1, z1);
  const double diag = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
  // Display quality: 0.05 % of the diagonal and 7 deg per facet (~50 facets per full circle). The previous
  // 0.15 % / 20 deg showed small cylinders and cones of multi-body assemblies as 18-sided polygons.
  const double defl = std::max(diag * 0.0005, 1e-4);
  try {
    BRepMesh_IncrementalMesh mesher(shape, defl, /*relative*/ false, /*angular*/ 0.12, /*parallel*/ true);
  } catch (...) {
  }

  TopTools_IndexedMapOfShape faceMap, edgeMap;
  TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
  TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

  // face -> owning solid (used by the section view to cap each body separately)
  TopTools_IndexedMapOfShape solidMap;
  TopExp::MapShapes(shape, TopAbs_SOLID, solidMap);
  m.solidCount = solidMap.Extent();
  std::vector<int> faceSolid(size_t(faceMap.Extent()) + 1, -1);
  for (int si = 1; si <= solidMap.Extent(); ++si) {
    TopTools_IndexedMapOfShape sf;
    TopExp::MapShapes(solidMap(si), TopAbs_FACE, sf);
    for (int k = 1; k <= sf.Extent(); ++k) {
      const int idx = faceMap.FindIndex(sf(k));
      if (idx > 0 && faceSolid[size_t(idx)] < 0) faceSolid[size_t(idx)] = si - 1;
    }
  }

  std::map<int, QString> faceLabel, edgeLabel;
  for (const auto& [name, f] : faceNames) {
    if (f.IsNull()) continue;
    int idx = faceMap.FindIndex(f);
    if (idx > 0) faceLabel[idx] = QString::fromStdString(name);
  }
  for (const auto& [name, e] : edgeNames) {
    if (e.IsNull()) continue;
    int idx = edgeMap.FindIndex(e);
    if (idx > 0) edgeLabel[idx] = QString::fromStdString(name);
  }

  // ---- faces
  for (int fi = 1; fi <= faceMap.Extent(); ++fi) {
    const TopoDS_Face face = TopoDS::Face(faceMap(fi));
    TopLoc_Location loc;
    Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
    if (tri.IsNull()) continue;
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    const gp_Trsf trsf = loc.Transformation();
    BRepAdaptor_Surface surf(face, false);
    const int nn = tri->NbNodes();
    std::vector<gp_Pnt> P(nn + 1);
    std::vector<gp_Vec> N(nn + 1, gp_Vec(0, 0, 0));
    std::vector<bool> haveN(nn + 1, false);
    for (int i = 1; i <= nn; ++i) {
      P[i] = tri->Node(i).Transformed(trsf);
      if (tri->HasUVNodes()) {
        try {
          gp_Pnt2d uv = tri->UVNode(i);
          gp_Pnt pp; gp_Vec du, dv;
          surf.D1(uv.X(), uv.Y(), pp, du, dv);
          gp_Vec n = du.Crossed(dv);
          if (n.Magnitude() > 1e-12) {
            n.Normalize();
            if (reversed) n.Reverse();
            N[i] = n;
            haveN[i] = true;
          }
        } catch (...) {
        }
      }
    }
    FaceRec rec;
    rec.name = faceLabel.count(fi) ? faceLabel[fi] : QString();
    rec.first = m.vertexCount();
    rec.solid = faceSolid[size_t(fi)];
    const int nt = tri->NbTriangles();
    for (int t = 1; t <= nt; ++t) {
      int a, b, c;
      tri->Triangle(t).Get(a, b, c);
      if (reversed) std::swap(b, c);
      gp_Vec gn = gp_Vec(P[a], P[b]).Crossed(gp_Vec(P[a], P[c]));
      if (gn.Magnitude() < 1e-14) continue;  // degenerate sliver
      gn.Normalize();
      const int idx[3] = {a, b, c};
      for (int k = 0; k < 3; ++k) {
        const int i = idx[k];
        gp_Vec n = haveN[i] ? N[i] : gn;
        // keep consistent with winding: flip if the analytic normal disagrees strongly with the triangle
        if (n.Dot(gn) < 0) n = gn;
        m.tris.push_back(float(P[i].X())); m.tris.push_back(float(P[i].Y())); m.tris.push_back(float(P[i].Z()));
        m.tris.push_back(float(n.X())); m.tris.push_back(float(n.Y())); m.tris.push_back(float(n.Z()));
        growBox(m, P[i].X(), P[i].Y(), P[i].Z());
      }
    }
    rec.count = m.vertexCount() - rec.first;
    if (rec.count > 0) m.faces.push_back(rec);
  }

  // ---- edges
  TopTools_IndexedDataMapOfShapeListOfShape e2f;
  TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, e2f);
  for (int ei = 1; ei <= edgeMap.Extent(); ++ei) {
    const TopoDS_Edge edge = TopoDS::Edge(edgeMap(ei));
    if (BRep_Tool::Degenerated(edge)) continue;
    EdgeRec rec;
    rec.name = edgeLabel.count(ei) ? edgeLabel[ei] : QString();
    rec.first = int(m.lines.size() / 3);
    bool done = false;
    int ie = e2f.FindIndex(edge);
    if (ie > 0) {
      for (TopTools_ListIteratorOfListOfShape it(e2f(ie)); it.More() && !done; it.Next()) {
        const TopoDS_Face face = TopoDS::Face(it.Value());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
        if (poly.IsNull()) continue;
        const TColStd_Array1OfInteger& nodes = poly->Nodes();
        const gp_Trsf trsf = loc.Transformation();
        for (int i = nodes.Lower(); i < nodes.Upper(); ++i) {
          pushLine(m, tri->Node(nodes(i)).Transformed(trsf), tri->Node(nodes(i + 1)).Transformed(trsf));
        }
        done = nodes.Length() >= 2;
      }
    }
    if (!done) {
      TopLoc_Location loc;
      Handle(Poly_Polygon3D) p3 = BRep_Tool::Polygon3D(edge, loc);
      if (!p3.IsNull() && p3->NbNodes() >= 2) {
        const gp_Trsf trsf = loc.Transformation();
        const TColgp_Array1OfPnt& nd = p3->Nodes();
        for (int i = nd.Lower(); i < nd.Upper(); ++i) pushLine(m, nd(i).Transformed(trsf), nd(i + 1).Transformed(trsf));
        done = true;
      }
    }
    if (!done) {
      try {
        BRepAdaptor_Curve c(edge);
        GCPnts_QuasiUniformDeflection q(c, defl * 2);
        if (q.IsDone() && q.NbPoints() >= 2) {
          for (int i = 1; i < q.NbPoints(); ++i) pushLine(m, q.Value(i), q.Value(i + 1));
          done = true;
        }
      } catch (...) {
      }
    }
    rec.count = int(m.lines.size() / 3) - rec.first;
    if (rec.count > 0) m.edges.push_back(rec);
  }
  return m;
}

} // namespace pc::viewer
