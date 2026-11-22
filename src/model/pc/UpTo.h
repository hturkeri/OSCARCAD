#pragma once
// PersistCAD model — "Kadar (Up To)" references of Extrude (SC-PULLUPTO, docs/spaceclaim_ux/PULL_UPTO.md).
//
// Extrude props (optional, additive to the schema in pc/Feature.h):
//   upTo=<ref>          the extrusion length is the signed distance of the target from the sketch plane, re-evaluated at
//                       every regeneration (associative: editing the target sketch / face / plane moves the end face).
//                       direction normal|reverse is derived from the sign (symmetric: total length = 2*|s|).
//   upToOffset=<expr>   added to |s| (Cetvel / Ruler: negative = stop before the target). Default 0.
//   regions=<k,k,...>   only these regions of the sketch (pc/Regions.h sketchRegions order) are extruded.
//   `distance` keeps the value computed when the Up To was made (informative; ignored while upTo is set).
// Sketch feature prop: gapTol=<mm> gap tolerance of its regions (default pc::sketch::autoGapTol).
//
// <ref> grammar:
//   sketch:<sid>:p<id>     a sketch point (curve end point, free point) of another sketch
//   sketch:<sid>:e<id>     a sketch curve: its point farthest from the sketch plane (max |s|)
//   sketch:<sid>:plane     the plane of a sketch (must be parallel)   ("sketch:<sid>:r<k>" means the same)
//   datum:<planeId>        a datum plane (must be parallel)
//   vtx:<edgeName>:<0|1>   first / last vertex of a model edge (curve parameter order)
//   edge(...)              a model edge: its point farthest from the sketch plane
//   <faceName>             a model face: parallel planar face -> its plane; otherwise its point farthest from the plane
#include "pc/Model.h"
#include <functional>
#include <string>

namespace pc::model {

struct UpToSources {
  std::function<bool(const std::string& sid, const Sketch** sk, gp_Ax3* frame)> sketch;
  std::function<bool(const std::string& id, gp_Ax3* frame)> datum;
  std::function<bool(const std::string& name, TopoDS_Face* f)> face;
  std::function<bool(const std::string& name, TopoDS_Edge* e)> edge;
};

// Signed distance s of the target from the plane of `frame` along frame.Direction(). false + *err if unresolved.
// *warn: non-fatal note (e.g. a non-parallel face, approximated by its farthest point).
bool upToSignedDistance(const std::string& ref, const gp_Ax3& frame, const UpToSources& src, double* s, std::string* err,
                        std::string* warn = nullptr);

// Same against a regenerated model (designer preview / tests).
bool upToDistance(const ModelResult& model, const gp_Ax3& frame, const std::string& ref, double* s, std::string* err,
                  std::string* warn = nullptr);

// The sketch feature a reference depends on ("" if none) — a target sketch must come before the Extrude.
std::string upToSketchOf(const std::string& ref);

// Short Turkish label: "Sketch2 noktası p7", "Extrude1.end yüzü", "Plane1 düzlemi", ...
std::string upToLabel(const std::string& ref);

} // namespace pc::model
