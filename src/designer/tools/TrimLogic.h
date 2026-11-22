#pragma once
// SC-UX (WP-B) — "Kırp (Trim Away)" for sketches: pure logic, no GUI (SPEC §3 #5, ACCEPTANCE #48).
// The piece of `entityId` that contains the picked point (u, v) and is bounded by the nearest intersections with
// the other sketch curves (or by the entity's own ends) is removed:
//   * Line: split into up to two lines; a line without intersections is deleted.
//   * Circle: needs at least two intersections; becomes the remaining Arc (same id, so radius / diameter
//     dimensions keep working).
//   * Arc: shortened or split into two arcs; an arc without intersections is deleted.
// New end points get a PointOnEntity constraint on the curve that cut them. Dimensional constraints of a split
// entity are dropped (its length changed); Horizontal / Vertical are kept on every remaining line piece.
#include "pc/SketchData.h"
#include <QString>
#include <vector>

namespace pcd {

bool trimSketchEntity(const pc::Sketch& in, int entityId, double u, double v, pc::Sketch& out, QString* message = nullptr);

// Intersection parameters (for tests / previews): t in [0,1] along a line, or angle (rad, [0, 2pi)) on a circle /
// arc, of every crossing of `entityId` with the other curves of the sketch.
std::vector<double> trimCutParameters(const pc::Sketch& sk, int entityId);

} // namespace pcd
