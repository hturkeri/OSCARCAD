#include "pc/SketchData.h"

#include <cmath>
#include <cstring>

namespace pc {

namespace {
struct CName { SkConstraintType t; const char* n; };
const CName kNames[] = {
    {SkConstraintType::Coincident, "Coincident"}, {SkConstraintType::Horizontal, "Horizontal"},
    {SkConstraintType::Vertical, "Vertical"}, {SkConstraintType::HorizontalPts, "HorizontalPts"},
    {SkConstraintType::VerticalPts, "VerticalPts"}, {SkConstraintType::Parallel, "Parallel"},
    {SkConstraintType::Perpendicular, "Perpendicular"}, {SkConstraintType::Tangent, "Tangent"},
    {SkConstraintType::Equal, "Equal"}, {SkConstraintType::Fixed, "Fixed"},
    {SkConstraintType::PointOnEntity, "PointOnEntity"}, {SkConstraintType::Midpoint, "Midpoint"},
    {SkConstraintType::Symmetric, "Symmetric"}, {SkConstraintType::Concentric, "Concentric"},
    {SkConstraintType::Distance, "Distance"}, {SkConstraintType::DistanceX, "DistanceX"},
    {SkConstraintType::DistanceY, "DistanceY"}, {SkConstraintType::PointLineDistance, "PointLineDistance"},
    {SkConstraintType::Length, "Length"}, {SkConstraintType::Radius, "Radius"},
    {SkConstraintType::Diameter, "Diameter"}, {SkConstraintType::Angle, "Angle"},
};
} // namespace

bool isDimensional(SkConstraintType t) {
  switch (t) {
    case SkConstraintType::Distance: case SkConstraintType::DistanceX: case SkConstraintType::DistanceY:
    case SkConstraintType::PointLineDistance: case SkConstraintType::Length: case SkConstraintType::Radius:
    case SkConstraintType::Diameter: case SkConstraintType::Angle:
      return true;
    default:
      return false;
  }
}

const char* toString(SkConstraintType t) {
  for (auto& c : kNames) if (c.t == t) return c.n;
  return "?";
}

bool fromString(const std::string& s, SkConstraintType& out) {
  for (auto& c : kNames)
    if (s == c.n) { out = c.t; return true; }
  return false;
}

Sketch Sketch::create() {
  Sketch s;
  s.points.push_back({0, 0.0, 0.0});
  s.nextId = 1;
  return s;
}

const SkPoint* Sketch::point(int id) const {
  for (auto& p : points) if (p.id == id) return &p;
  return nullptr;
}
SkPoint* Sketch::point(int id) {
  for (auto& p : points) if (p.id == id) return &p;
  return nullptr;
}
const SkEntity* Sketch::entity(int id) const {
  for (auto& e : entities) if (e.id == id) return &e;
  return nullptr;
}
SkEntity* Sketch::entity(int id) {
  for (auto& e : entities) if (e.id == id) return &e;
  return nullptr;
}

int Sketch::addPoint(double x, double y) {
  if (points.empty() || points.front().id != 0) points.insert(points.begin(), SkPoint{0, 0, 0});
  int id = nextId++;
  points.push_back({id, x, y});
  return id;
}

int Sketch::addLine(int p1, int p2, bool construction) {
  int id = nextId++;
  entities.push_back({id, SkEntityType::Line, {p1, p2}, 0.0, construction});
  return id;
}

int Sketch::addLine(double x1, double y1, double x2, double y2, bool construction) {
  int a = addPoint(x1, y1);
  int b = addPoint(x2, y2);
  return addLine(a, b, construction);
}

int Sketch::addCircle(int center, double r, bool construction) {
  int id = nextId++;
  entities.push_back({id, SkEntityType::Circle, {center}, r, construction});
  return id;
}

int Sketch::addCircle(double cx, double cy, double r, bool construction) {
  int c = addPoint(cx, cy);
  return addCircle(c, r, construction);
}

int Sketch::addArc(int center, int start, int end, bool construction) {
  int id = nextId++;
  double r = 0;
  const SkPoint* c = point(center);
  const SkPoint* s = point(start);
  if (c && s) r = std::hypot(s->x - c->x, s->y - c->y);
  entities.push_back({id, SkEntityType::Arc, {center, start, end}, r, construction});
  return id;
}

int Sketch::addConstraint(SkConstraintType t, std::vector<int> refs, std::string valueExpr) {
  int id = nextId++;
  constraints.push_back({id, t, std::move(refs), std::move(valueExpr)});
  return id;
}

std::vector<int> Sketch::addRectangle(double x0, double y0, double x1, double y1) {
  int a = addPoint(x0, y0), b = addPoint(x1, y0), c = addPoint(x1, y1), d = addPoint(x0, y1);
  int bottom = addLine(a, b), right = addLine(b, c), top = addLine(c, d), left = addLine(d, a);
  addConstraint(SkConstraintType::Horizontal, {bottom});
  addConstraint(SkConstraintType::Horizontal, {top});
  addConstraint(SkConstraintType::Vertical, {right});
  addConstraint(SkConstraintType::Vertical, {left});
  return {bottom, right, top, left};
}

} // namespace pc
