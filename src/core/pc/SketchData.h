#pragma once
// PersistCAD core — 2D sketch DATA model (pure data; solving lives in src/sketch).
//
// CONTRACT (all modules rely on this):
//  * Every sketch implicitly owns point id 0 = ORIGIN at (0,0), always fixed. It is stored in
//    `points` (Sketch::create() adds it). Ids are unique across points, entities, constraints
//    of one sketch (single counter `nextId`).
//  * Coordinates are sketch-plane local (u = plane xDir, v = plane yDir), millimetres.
//  * Point/entity coordinates stored in the document are the LAST SOLVED values; they also act
//    as the initial guess for the next solve.
//  * Arcs are CCW from start point to end point around center. Arc radius is implied by
//    |start-center| (the `radius` field is kept equal by the solver for arcs).
//  * Circle: points = {center}, radius in `radius`.
//  * Dimensional constraints carry `valueExpr`: an expression over document parameters
//    ("W", "H/2", "25"). Angles in degrees.
//
// Constraint `refs` semantics (ids):
//   Coincident        {p1, p2}
//   Horizontal        {line}                     Vertical {line}
//   HorizontalPts     {p1, p2}  (same v)         VerticalPts {p1, p2} (same u)
//   Parallel          {line1, line2}             Perpendicular {line1, line2}
//   Tangent           {e1, e2}  (line|arc|circle with arc|circle; endpoint-tangency when they share a point)
//   Equal             {e1, e2}  (line-line: equal length; arc/circle pair: equal radius)
//   Fixed             {p}       (point locked at its stored coordinates)
//   PointOnEntity     {p, e}    (e = line (infinite carrier) | circle | arc)
//   Midpoint          {p, line}
//   Symmetric         {p1, p2, line}  (p1,p2 mirror images about the line)
//   Concentric        {e1, e2}  (arc/circle centers coincide)
//  Dimensional (valueExpr required):
//   Distance          {p1, p2}        |p2-p1| = value  (>0)
//   DistanceX         {p1, p2}        p2.u - p1.u = value (signed)
//   DistanceY         {p1, p2}        p2.v - p1.v = value (signed)
//   PointLineDistance {p, line}       unsigned distance = value
//   Length            {line}          length = value
//   Radius            {arc|circle}    radius = value
//   Diameter          {arc|circle}    2*radius = value
//   Angle             {line1, line2}  signed CCW angle from dir(line1) to dir(line2) = value (deg);
//                                     dir(line) = points[1]-points[0]
#include <string>
#include <vector>

namespace pc {

struct SkPoint {
  int id = 0;
  double x = 0, y = 0;
};

enum class SkEntityType { Line, Circle, Arc };

struct SkEntity {
  int id = 0;
  SkEntityType type = SkEntityType::Line;
  std::vector<int> points;   // Line {p1,p2}; Circle {center}; Arc {center,start,end}
  double radius = 0;         // Circle radius (Arc: kept = |start-center|)
  bool construction = false; // construction geometry is ignored for profiles
};

enum class SkConstraintType {
  Coincident, Horizontal, Vertical, HorizontalPts, VerticalPts, Parallel, Perpendicular,
  Tangent, Equal, Fixed, PointOnEntity, Midpoint, Symmetric, Concentric,
  Distance, DistanceX, DistanceY, PointLineDistance, Length, Radius, Diameter, Angle
};

bool isDimensional(SkConstraintType t);
const char* toString(SkConstraintType t);
bool fromString(const std::string& s, SkConstraintType& out);

struct SkConstraint {
  int id = 0;
  SkConstraintType type = SkConstraintType::Coincident;
  std::vector<int> refs;
  std::string valueExpr;     // dimensional only
};

enum class SketchPlaneKind { XY, XZ, YZ, OnFace, OnPlane };

// Placement of the sketch plane.
//  XY: origin (0,0,0), u=+X, v=+Y, normal=+Z
//  XZ: origin (0,0,0), u=+X, v=+Z, normal=-Y   (right-handed: u x v = normal)
//  YZ: origin (0,0,0), u=+Y, v=+Z, normal=+X
//  OnFace: planar face referenced by persistent name `faceRef`; the model module derives the frame:
//          normal = OUTWARD face normal; origin = projection of world origin (0,0,0) onto the face
//          plane; u = projection of world +X onto the plane (if |proj| < 1e-6 use world +Y), normalised;
//          v = normal x u.  (So a sketch on the top face of a part at z=T has u=+X, v=+Y, origin (0,0,T).)
//  OnPlane: datum plane feature (FeatureType::Plane) referenced by its feature id in `planeRef`;
//          the sketch frame IS the datum frame (origin/u/v/normal), then offsetExpr is applied.
//  `offsetExpr` translates the plane along its normal (expression, mm).
struct SketchPlane {
  SketchPlaneKind kind = SketchPlaneKind::XY;
  std::string faceRef;
  std::string planeRef;      // OnPlane: id of a Plane feature
  std::string offsetExpr = "0";
};

struct Sketch {
  SketchPlane plane;
  std::vector<SkPoint> points;
  std::vector<SkEntity> entities;
  std::vector<SkConstraint> constraints;
  int nextId = 1;

  static Sketch create();  // with origin point id 0

  const SkPoint* point(int id) const;
  SkPoint* point(int id);
  const SkEntity* entity(int id) const;
  SkEntity* entity(int id);

  // Convenience builders (return new id). They do NOT add constraints except where stated.
  int addPoint(double x, double y);
  int addLine(int p1, int p2, bool construction = false);
  int addLine(double x1, double y1, double x2, double y2, bool construction = false); // creates 2 points
  int addCircle(int center, double r, bool construction = false);
  int addCircle(double cx, double cy, double r, bool construction = false);           // creates center
  int addArc(int center, int start, int end, bool construction = false);
  int addConstraint(SkConstraintType t, std::vector<int> refs, std::string valueExpr = "");
  // Axis-aligned rectangle from 4 lines with coincident corners + H/V constraints. Returns line ids
  // in order bottom, right, top, left (each line oriented CCW).
  std::vector<int> addRectangle(double x0, double y0, double x1, double y1);
};

} // namespace pc
