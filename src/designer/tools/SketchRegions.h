#pragma once
// SC-UX (WP-B) — sketch regions and sketch curves in the 3D view (SPEC §2.2, §6.3) + small OCCT helpers shared
// by the tools. appendSketchRegionFaces() itself is declared in ToolApi.h (contract).
#include "ToolApi.h"
#include "pc/Regions.h"
#include <QString>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <set>
#include <string>
#include <vector>

namespace pcd {

// "sketch:<sid>:r<k>" / "sketch:<sid>:e<id>" / "sketch:<sid>:p<id>". type = 'r', 'e' or 'p'.
bool parseSketchItemName(const QString& name, std::string* sid, char* type, int* index);
QString regionName(const std::string& sid, int k);
QString sketchCurveName(const std::string& sid, int entityId);

// Region faces of a sketch (true geometry, not the display copy), in profile order, + its 3D frame.
bool sketchRegionFaces(const pc::Document& doc, const pc::model::ModelResult& model, const std::string& sid,
                       std::vector<TopoDS_Face>& faces, gp_Ax3& frame);
// SC-PULLUPTO: the full region analysis (gap tolerance from the sketch's `gapTol` prop, open gaps, holes, ...).
bool sketchRegionInfo(const pc::Document& doc, const pc::model::ModelResult& model, const std::string& sid,
                      pc::sketch::RegionResult& out, gp_Ax3& frame);
// Region indices of `sid` consumed by Extrude / Revolve `regions=` lists; *all = a consumer takes the whole sketch.
std::set<int> consumedRegions(const pc::Document& doc, const std::string& sid, bool* all = nullptr);
bool sketchFrameOf(const pc::model::ModelResult& model, const std::string& sid, gp_Ax3& frame);

// Sketch feature ids that no non-suppressed solid feature (Extrude / Revolve / Hole) consumes, up to the rollback
// position (-1 = all). These are the sketches whose regions the host shows by default.
std::set<std::string> unconsumedSketches(const pc::Document& doc, int rollbackIndex = -1);
bool sketchConsumed(const pc::Document& doc, const std::string& sid);

// Every curve (construction lines included, so that they can be Alt+clicked as a revolve axis) of the given
// sketches as named edges "sketch:<sid>:e<id>". Returns the number of edges added.
int appendSketchCurveEdges(const pc::Document& doc, const pc::model::ModelResult& model,
                           const std::set<std::string>& sketchIds, TopoDS_Compound& comp,
                           std::map<std::string, TopoDS_Edge>& edgeNames);

// ---- OCCT helpers for handles
bool faceCentroidNormal(const TopoDS_Face& f, gp_Pnt& centroid, gp_Dir& outwardNormal);
// Mid point of a model edge and the direction pointing into the material (- sum of the adjacent outward normals).
bool edgeMidInward(const pc::model::ModelResult& model, const QString& edgeName, gp_Pnt& mid, gp_Dir& inward);
// Face of a model / region name (regions resolved through sketchRegionFaces). false if unknown.
bool faceByName(const pc::Document& doc, const pc::model::ModelResult& model, const QString& name, TopoDS_Face& out);

} // namespace pcd
