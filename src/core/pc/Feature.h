#pragma once
// PersistCAD core — parametric feature DATA model (history-based, evaluated top to bottom).
//
// Property schema per feature type (all numeric values are EXPRESSIONS over parameters):
//
//  Sketch   : `sketch` member holds the data. props: none.
//
//  Extrude  : sketch=<featureId of a Sketch>, distance=<expr>,
//             direction = normal | reverse | symmetric          (default normal)
//             op = new | join | cut | intersect                (default new if no body yet, else join)
//             draft=<expr deg, default 0>  (positive = tapers inward)
//             through=0|1  (cut only: through all, ignores distance)
//  Revolve  : sketch=<id>, axis = U | V | <entityId of a (construction) line in the sketch>,
//             angle=<expr deg, default 360>, op = ...
//  Hole     : sketch=<id> — every NON-construction Circle center and every free point
//             (points not referenced by any entity, except origin) is a hole location.
//             Hole axis = -sketch normal (drills into material below the sketch plane).
//             diameter=<expr>, depth=<expr> | through=1,
//             kind = simple | counterbore | countersink,
//             cbDiameter, cbDepth (counterbore), csDiameter, csAngle (countersink, deg, default 90),
//             tipAngle=<deg, default 118, blind holes only; 0 = flat bottom>
//  Fillet   : radius=<expr>; refs = edge selectors (see below)
//  Chamfer  : distance=<expr>; refs = edge selectors
//  Shell    : thickness=<expr>; refs = face names to remove (open faces); direction=inward|outward (default inward)
//  Pattern  : kind = linear | circular; source=<featureId> (Extrude-cut/Hole/Extrude-join tool body is repeated)
//             count = total number of instances incl. the source; count=1 is allowed and adds nothing.
//             linear: count=<expr>, spacing=<expr>, dirX,dirY,dirZ (expr; default 1,0,0)
//             circular: count=<expr>, angle=<expr deg, default 360>, axis = X | Y | Z
//
//  ---- v2: datum planes & multi-body (SpaceClaim-like) -------------------------------------------
//  Plane    : DATUM PLANE (no solid geometry). Sketches use it via SketchPlaneKind::OnPlane.
//             kind = edgeNormal | edgeAngle | twoEdges | faceOffset          (default edgeNormal)
//             edgeNormal: refs[0] = edge name. t=<expr 0..1, default 0> = normalised arc length along the
//                         edge, measured in the direction of the edge's underlying curve parameter
//                         (BRepAdaptor_Curve, ignoring TopAbs orientation). origin = point at t,
//                         normal = curve tangent at t. Works on lines, arcs, circles, splines.
//             edgeAngle : refs[0] = STRAIGHT edge; refs[1] = reference face (optional; default = the planar
//                         face adjacent to the edge whose name sorts first). angle=<expr deg, default 0>,
//                         t=<expr, default 0.5> (origin along the edge). The plane CONTAINS the edge:
//                         angle 0 => normal = reference face outward normal; the normal is then rotated by
//                         +angle about the edge curve direction (right-hand rule).  u = edge curve direction.
//             twoEdges  : refs[0], refs[1] = straight edges that are coplanar (intersecting or parallel).
//                         origin = midpoint of refs[0]; u = direction of refs[0];
//                         normal = normalize(d0 x (m1 - m0)) (fallback d0 x d1); error if collinear/skew.
//             faceOffset: refs[0] = planar face name; plane = face plane (OnFace frame rule).
//             axisAngle : no refs (works without a body). The plane CONTAINS a world axis: axis = X | Y | Z
//                         (default Z); angle=<expr deg, default 0> rotates the base plane about that axis
//                         (right-hand rule). origin = (0,0,0), v = the axis, u = base u rotated by angle,
//                         normal = u x v. Base u: Z -> +X (angle 0 == standard XZ plane), X -> +Y, Y -> +Z.
//             Common to all kinds: offset=<expr, default 0> translates along the final normal;
//             rotate=<expr deg, default 0> rotates u/v about the normal; flip=0|1 reverses normal (v := -v).
//             Unless stated, u follows the OnFace rule (world +X projected, else world +Y); v = normal x u.
//             show=0|1 (default 1): 0 marks a construction plane that the designer draws only while it is
//             selected or its sketch is current (keeps helper planes out of the 3D view); geometry unaffected.
//  Combine  : body-to-body boolean (SpaceClaim "Combine").
//             op = join | cut | intersect; target=<body name>; tools=<comma-separated body names>;
//             keepTools=0|1 (default 0: tool bodies are consumed).
//             largestOnly=0|1 (default 0): keep only the largest solid of the result (drops enclosed pockets,
//             e.g. sealed dead volumes when a fluid volume is extracted as envelope minus solids).
//
//  Transform: (SC-UX CR-3, SpaceClaim "Move") rigid motion of bodies. bodies=<comma list, empty = all bodies>;
//             rotation first: axis = X | Y | Z (through ox,oy,oz <expr, default 0>), angle=<expr deg, default 0>;
//             then translation tx,ty,tz=<expr, default 0>. copy=0|1 (default 0): 1 keeps the originals and adds
//             moved copies as new bodies named "<body>.<fid>" whose faces are "<fid>.<originalFaceName>".
//             Moved (copy=0) bodies keep their name and every face keeps its persistent name.
//
//  Any body-creating feature: mfg=0 marks its body as REFERENCE geometry (fluid volume, CFD domain): the body
//  is modelled and exported but skipped by all manufacturing checks (pc_mfg).
//
// BODIES (multi-body parts):
//   * Extrude/Revolve/Pattern(op=new) create a NEW BODY. Body name = prop `bodyName` if given, else the
//     feature id. The first solid feature of a part always creates a body (op=new implied).
//   * Extrude/Revolve/Hole/Pattern ops join|cut|intersect act on prop `targets` = comma-separated body
//     names. Empty `targets` = AUTO: join -> every body the tool touches is merged (if it touches none, the
//     tool becomes a new body named by the feature id); cut/intersect -> every body the tool touches
//     (intersect: bodies not touched are removed).
//   * keepTool=0|1 (cut/intersect only, default 0): additionally keep the tool solid as a new body named
//     `bodyName` or "<fid>.tool".
//   * Fillet/Chamfer/Shell act on the body(ies) owning the referenced edges/faces.
//   * When bodies are merged the result keeps the name of the OLDEST body. If a body splits into disjoint
//     solids, pieces are named "<body>:1", "<body>:2" ordered by centroid (x, then y, then z).
//   * Face names stay globally unique across all bodies (the naming contract below is unchanged).
//
// Edge selector strings in `refs` (Fillet/Chamfer):
//   "<edgeName>"           a persistent edge name (see naming)
//   "face:<faceName>"      all edges bounding that face
//
// PERSISTENT NAMING CONTRACT (implemented by src/model, consumed by GUI/mfg/apps):
//   Faces are named by the feature that CREATED them:
//     Extrude:  "<fid>.start", "<fid>.end", "<fid>.side.e<entityId>"   (entityId = sketch entity)
//               for a full circle: "<fid>.side.e<circleId>"
//     Revolve:  "<fid>.rev.e<entityId>", "<fid>.start", "<fid>.end" (start/end only if angle<360)
//     Hole:     "<fid>.h<k>.wall", "<fid>.h<k>.bottom", "<fid>.h<k>.cb_wall", "<fid>.h<k>.cb_floor",
//               "<fid>.h<k>.cs"   (k = 0-based location index, ordered by (u,v) of the sketch point)
//     Fillet:   "<fid>.f<k>"   Chamfer: "<fid>.c<k>"   (k = index of the source edge in sorted order)
//     Shell:    "<fid>.inner.<originalFaceName>"
//     Pattern:  "<fid>.i<k>.<originalFaceName>"  (k = instance index, 1..count-1)
//   Faces that survive a later operation keep their name. If an operation splits a face into
//   several pieces, the pieces are named "<name>:1", "<name>:2", ... ordered by centroid (x, then y, then z).
//   Edges are named by their two adjacent faces: "edge(<faceA>|<faceB>)" with faceA < faceB
//   lexicographically; if several edges share the same pair, suffix ":1", ":2" ordered by midpoint.
//   Edges with one adjacent face (seam/closed) are "edge(<face>)" (+ suffix).
#include "pc/SketchData.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pc {

enum class FeatureType { Sketch, Extrude, Revolve, Hole, Fillet, Chamfer, Shell, Pattern, Plane, Combine, Transform };

const char* toString(FeatureType t);
bool fromString(const std::string& s, FeatureType& out);

struct Feature {
  std::string id;                       // unique, e.g. "Extrude1" (usable as name prefix: no '.', '|', ':')
  FeatureType type = FeatureType::Sketch;
  bool suppressed = false;
  std::map<std::string, std::string> props;
  std::vector<std::string> refs;
  std::optional<Sketch> sketch;         // only for FeatureType::Sketch

  std::string prop(const std::string& key, const std::string& def = "") const {
    auto it = props.find(key);
    return it == props.end() ? def : it->second;
  }
};

} // namespace pc
