#pragma once
// Internal: CPU tessellation of an OCCT shape into flat triangle / line arrays (no GL, no Qt GUI).
#include <QString>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <map>
#include <string>
#include <vector>

namespace pc::viewer {

struct FaceRec {
  QString name;
  int first = 0;   // first vertex (not float) in `tris`
  int count = 0;   // vertex count (multiple of 3)
  int solid = -1;  // index of the owning TopoDS_Solid (body) in the shape, -1 = not part of a solid
};
struct EdgeRec {
  QString name;
  int first = 0;   // first vertex in `lines` (GL_LINES pairs)
  int count = 0;
};

struct MeshData {
  std::vector<float> tris;    // pos(3)+normal(3) per vertex, 3 vertices per triangle
  std::vector<FaceRec> faces;
  std::vector<float> lines;   // pos(3) per vertex, 2 vertices per segment
  std::vector<EdgeRec> edges;
  double bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  bool hasBox = false;
  int solidCount = 0;         // number of solids found in the shape (section caps are drawn per solid)
  int vertexCount() const { return int(tris.size() / 6); }
};

MeshData tessellate(const TopoDS_Shape& shape, const std::map<std::string, TopoDS_Face>& faceNames,
                    const std::map<std::string, TopoDS_Edge>& edgeNames);

} // namespace pc::viewer
