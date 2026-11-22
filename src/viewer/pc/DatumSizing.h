#pragma once
// Local-context sizing of datum plane squares (v3).
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <map>
#include <string>
#include <vector>

namespace pc::viewer {

// Half-size of the square drawn for a datum plane:
//   clamp(max(0.6 * refSize, 0.15 * diag), .., 0.35 * diag)
// refSize = length of the reference edge / diagonal of the reference face (max over refs found);
// diag = bbox diagonal of `model`. Returns `fallback` when the model has no bounding box.
double datumPlaneHalfSize(const TopoDS_Shape& model, const std::vector<std::string>& refs,
                          const std::map<std::string, TopoDS_Edge>& edges,
                          const std::map<std::string, TopoDS_Face>& faces, double fallback = 20.0);

} // namespace pc::viewer
