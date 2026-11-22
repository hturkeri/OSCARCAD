#pragma once
// PersistCAD model module — internal persistent-naming toolkit (NOT a frozen contract; used by
// src/model and its unit tests). Implements the naming rules of pc/Feature.h on raw OCCT shapes.
#include <BRepBuilderAPI_MakeShape.hxx>
#include <NCollection_DataMap.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <map>
#include <string>
#include <vector>

namespace pc::model::naming {

// face (IsSame, orientation ignored) -> persistent name
using NameMap = NCollection_DataMap<TopoDS_Shape, std::string, TopTools_ShapeMapHasher>;

struct NamedShape {
  TopoDS_Shape shape;
  NameMap faces;        // every face of `shape` -> unique name
  std::map<std::string, TopoDS_Face> byName() const;
  std::string nameOf(const TopoDS_Shape& face) const;   // "" if unknown
};

// A naming proposal for a face of the result. Lower `prio` wins when one face gets several names;
// ties are broken by the lexicographically smallest name (deterministic).
struct Candidate {
  TopoDS_Shape face;
  std::string name;
  int prio = 0;
};

// Resolve candidates into a complete, unique NameMap over all faces of `result`:
//  * each face takes its best candidate,
//  * a name carried by several faces (split) becomes "<name>:1", ":2", ... ordered by centroid (x,y,z),
//  * faces without any candidate get "<fid>.u<k>" (k ordered by centroid), reported in `log`.
NameMap finalize(const TopoDS_Shape& result, const std::vector<Candidate>& cands, const std::string& fid,
                 std::vector<std::string>* log = nullptr);

// Map the face names of `inputs` (each with its priority) through an OCCT operation history onto
// `result` (Modified / IsDeleted / unchanged). `extra` = names of faces created by the operation itself.
NameMap propagate(BRepBuilderAPI_MakeShape& op, const std::vector<std::pair<const NamedShape*, int>>& inputs,
                  const TopoDS_Shape& result, const std::vector<Candidate>& extra, const std::string& fid,
                  std::vector<std::string>* log = nullptr);

// Candidates of one input through `op` (helper of propagate).
void collectCandidates(BRepBuilderAPI_MakeShape& op, const NamedShape& in, int prio,
                       const TopTools_IndexedMapOfShape& resultFaces, std::vector<Candidate>& out);

// Edge names per contract: "edge(<a>|<b>)" (a<b), "edge(<a>)" for single-face edges, duplicates
// suffixed ":1", ":2" ordered by edge midpoint (x,y,z). Degenerated edges are skipped.
std::map<std::string, TopoDS_Edge> nameEdges(const TopoDS_Shape& shape, const NameMap& faces);

// Centroid-based strict-weak ordering helper (x, then y, then z with 1e-7 tolerance).
gp_Pnt faceCentroid(const TopoDS_Shape& face);
bool pointLess(const gp_Pnt& a, const gp_Pnt& b);

} // namespace pc::model::naming
