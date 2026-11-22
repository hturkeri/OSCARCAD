#include "pc/DatumSizing.h"
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <cmath>

namespace pc::viewer {

static double bboxDiag(const TopoDS_Shape& s) {
  if (s.IsNull()) return 0;
  Bnd_Box b;
  BRepBndLib::Add(s, b);
  if (b.IsVoid()) return 0;
  double x0, y0, z0, x1, y1, z1;
  b.Get(x0, y0, z0, x1, y1, z1);
  return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
}

double datumPlaneHalfSize(const TopoDS_Shape& model, const std::vector<std::string>& refs,
                          const std::map<std::string, TopoDS_Edge>& edges,
                          const std::map<std::string, TopoDS_Face>& faces, double fallback) {
  const double diag = bboxDiag(model);
  if (diag <= 1e-9) return fallback;
  double ref = 0;
  for (const auto& r : refs) {
    if (r.empty()) continue;
    try {
      auto ie = edges.find(r);
      if (ie != edges.end() && !ie->second.IsNull()) {
        GProp_GProps p;
        BRepGProp::LinearProperties(ie->second, p);
        ref = std::max(ref, p.Mass());
        continue;
      }
      auto jf = faces.find(r);
      if (jf != faces.end() && !jf->second.IsNull()) ref = std::max(ref, bboxDiag(jf->second));
    } catch (...) {}
  }
  const double h = std::max(0.6 * ref, 0.15 * diag);
  return std::min(h, 0.35 * diag);
}

} // namespace pc::viewer
