#pragma once
// WP-3 private: planar arrangement of line / arc segments (split at intersections, dedupe, drop dangling edges,
// half-edge faces). Used by S6 to find closed regions (material faces) of a view.
#include "pc/drawing/Geometry.h"
#include <vector>

namespace pc::drawing::interp {

struct ASeg {
  bool arc = false;
  Vec2 a, b;                 // start / end
  Vec2 c; double r = 0;      // arc centre / radius
  bool ccw = true;           // arc direction a -> b
  int src = -1;              // source primitive id (-1 = synthetic, e.g. axis)
  int tag = 0;               // caller tag (0 visible, 1 hidden, 2 axis ...)
};

double segLength(const ASeg& s);
Vec2 segPoint(const ASeg& s, double t);          // t in [0,1]
double segSweep(const ASeg& s);                  // arc: signed sweep (rad), + = ccw
std::vector<Vec2> segPolyline(const ASeg& s, double maxStep);
double distToSeg(const ASeg& s, Vec2 q);

struct Arrangement {
  struct Vertex { Vec2 p; std::vector<int> out; };          // outgoing half-edges sorted CCW
  struct Edge { ASeg seg; int v0 = -1, v1 = -1; std::vector<int> srcs; std::vector<int> tags; };
  struct Half { int edge = -1; bool fwd = true; int from = -1, to = -1; int next = -1; int cycle = -1; double ang = 0; };
  struct Cycle { std::vector<int> halves; double area = 0; std::vector<Vec2> poly; int component = -1; int container = -1; };
  std::vector<Vertex> verts;
  std::vector<Edge> edges;
  std::vector<Half> halves;
  std::vector<Cycle> cycles;          // area > 0: bounded face (CCW); area < 0: outer boundary of a component

  // tol: vertex snapping tolerance (same units as segments)
  void build(const std::vector<ASeg>& segs, double tol, bool removeDangling = true);
  ASeg halfSeg(int h) const;          // geometry of half-edge in its direction
  int twin(int h) const { return h ^ 1; }
  Vec2 interiorPoint(int cycle) const;
};

bool pointInPolygon(Vec2 q, const std::vector<Vec2>& poly);
double polygonArea(const std::vector<Vec2>& poly);

}  // namespace pc::drawing::interp
