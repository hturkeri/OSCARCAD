// PersistCAD sketch profile construction: loops -> nesting -> OCCT planar faces.
#include "pc/SketchSolver.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Circle.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace pc::sketch {

namespace {

constexpr double kPi = 3.14159265358979323846;
struct V2 { double x = 0, y = 0; };
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator*(V2 a, double k) { return {a.x * k, a.y * k}; }
inline double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
inline double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline double norm(V2 a) { return std::hypot(a.x, a.y); }

double normAngle(double a) {  // -> [0, 2pi)
  a = std::fmod(a, 2 * kPi);
  if (a < 0) a += 2 * kPi;
  return a;
}

// 2D curve of one entity (natural direction: line a->b, arc CCW start->end, circle CCW).
struct Curve {
  int eid = 0;
  SkEntityType type = SkEntityType::Line;
  int na = -1, nb = -1;  // node indices of start/end (lines, arcs)
  V2 a, b;               // start/end positions (node positions)
  V2 c;                  // center
  double r = 0, a0 = 0, sweep = 0;
  bool inLoop = false;
  bool bad = false;
};

bool onArc(const Curve& cv, V2 p, double angTol) {
  if (cv.type == SkEntityType::Circle) return true;
  double t = normAngle(std::atan2(p.y - cv.c.y, p.x - cv.c.x) - cv.a0);
  return t <= cv.sweep + angTol || t >= 2 * kPi - angTol;
}

// Intersection points of two curves (without filtering of shared nodes). `overlap` set for
// collinear / co-circular overlap.
void intersect(const Curve& A, const Curve& B, double tol, std::vector<V2>& pts, bool& overlap) {
  overlap = false;
  auto lineLine = [&](const Curve& P, const Curve& Q) {
    V2 d1 = P.b - P.a, d2 = Q.b - Q.a;
    double l1 = norm(d1), l2 = norm(d2);
    double den = cross(d1, d2);
    if (std::abs(den) <= 1e-12 * l1 * l2) {
      // parallel: collinear overlap?
      if (std::abs(cross(d1, Q.a - P.a)) / l1 > tol) return;
      double t0 = dot(Q.a - P.a, d1) / l1, t1 = dot(Q.b - P.a, d1) / l1;
      double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(l1, std::max(t0, t1));
      if (hi - lo > tol) overlap = true;
      else if (hi - lo > -tol) pts.push_back(P.a + d1 * (0.5 * (lo + hi) / l1));
      return;
    }
    double t = cross(Q.a - P.a, d2) / den, u = cross(Q.a - P.a, d1) / den;
    double et = tol / l1, eu = tol / l2;
    if (t >= -et && t <= 1 + et && u >= -eu && u <= 1 + eu) pts.push_back(P.a + d1 * t);
  };
  auto lineRound = [&](const Curve& P, const Curve& R) {
    V2 d = P.b - P.a;
    double l = norm(d);
    V2 u = d * (1.0 / l);
    double tc = dot(R.c - P.a, u);
    double h = cross(u, R.c - P.a);  // signed distance center-line
    if (std::abs(h) > R.r + tol) return;
    double w2 = R.r * R.r - h * h;
    double w = w2 > 0 ? std::sqrt(w2) : 0.0;
    double ts[2] = {tc - w, tc + w};
    int nt = w > 0 ? 2 : 1;
    for (int i = 0; i < nt; ++i) {
      if (ts[i] < -tol || ts[i] > l + tol) continue;
      V2 p = P.a + u * ts[i];
      if (onArc(R, p, tol / R.r)) pts.push_back(p);
    }
  };
  auto roundRound = [&](const Curve& P, const Curve& Q) {
    V2 dc = Q.c - P.c;
    double d = norm(dc);
    if (d <= tol && std::abs(P.r - Q.r) <= tol) {
      // co-circular: overlap if angular ranges overlap beyond endpoints
      if (P.type == SkEntityType::Circle || Q.type == SkEntityType::Circle) { overlap = true; return; }
      for (int k = 0; k < 16; ++k) {  // sample interior of P
        double ang = P.a0 + P.sweep * (k + 0.5) / 16.0;
        V2 p = P.c + V2{std::cos(ang), std::sin(ang)} * P.r;
        if (onArc(Q, p, -1e-9)) { overlap = true; return; }
      }
      return;
    }
    if (d > P.r + Q.r + tol || d < std::abs(P.r - Q.r) - tol || d <= tol) return;
    double aa = (P.r * P.r - Q.r * Q.r + d * d) / (2 * d);
    double h2 = P.r * P.r - aa * aa;
    double h = h2 > 0 ? std::sqrt(h2) : 0.0;
    V2 e = dc * (1.0 / d), m = P.c + e * aa, nrm{-e.y, e.x};
    V2 cand[2] = {m + nrm * h, m - nrm * h};
    int nc = h > 0 ? 2 : 1;
    for (int i = 0; i < nc; ++i)
      if (onArc(P, cand[i], tol / P.r) && onArc(Q, cand[i], tol / Q.r)) pts.push_back(cand[i]);
  };
  bool al = A.type == SkEntityType::Line, bl = B.type == SkEntityType::Line;
  if (al && bl) lineLine(A, B);
  else if (al) lineRound(A, B);
  else if (bl) lineRound(B, A);
  else roundRound(A, B);
}

struct LoopEdge { int curve; bool fwd; };
struct Loop {
  std::vector<LoopEdge> edges;
  double area = 0;
  V2 probe;  // a point on the loop
  bool bad = false;
  int depth = 0, parent = -1;
};

double windingAngle(const Curve& cv, bool fwd, V2 p) {
  double w;
  if (cv.type == SkEntityType::Circle) {
    w = norm(p - cv.c) < cv.r ? 2 * kPi : 0.0;
  } else {
    V2 s = cv.a - p, e = cv.b - p;
    w = std::atan2(cross(s, e), dot(s, e));
    if (cv.type == SkEntityType::Arc && norm(p - cv.c) < cv.r) {
      double am = cv.a0 + 0.5 * cv.sweep;
      V2 mid = cv.c + V2{std::cos(am), std::sin(am)} * cv.r;
      double sp = cross(cv.b - cv.a, p - cv.a), sm = cross(cv.b - cv.a, mid - cv.a);
      if (sp * sm > 0) w += 2 * kPi;
    }
  }
  return fwd ? w : -w;
}

double curveArea(const Curve& cv) {  // signed area contribution, natural direction
  if (cv.type == SkEntityType::Circle) return kPi * cv.r * cv.r;
  double a = 0.5 * cross(cv.a, cv.b);
  if (cv.type == SkEntityType::Arc) a += 0.5 * cv.r * cv.r * (cv.sweep - std::sin(cv.sweep));
  return a;
}

V2 curveMid(const Curve& cv) {
  if (cv.type == SkEntityType::Line) return (cv.a + cv.b) * 0.5;
  double am = cv.a0 + 0.5 * cv.sweep;
  return cv.c + V2{std::cos(am), std::sin(am)} * cv.r;
}

struct UF {
  std::vector<int> p;
  explicit UF(int n) : p(n) { for (int i = 0; i < n; ++i) p[i] = i; }
  int f(int x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
  void u(int a, int b) { a = f(a); b = f(b); if (a != b) p[std::max(a, b)] = std::min(a, b); }
};

} // namespace

Profile buildProfile(const Sketch& sk, const gp_Ax3& frame) {
  Profile P;
  std::vector<std::string> errors;
  auto err = [&](const std::string& s) {
    if (std::find(errors.begin(), errors.end(), s) == errors.end()) errors.push_back(s);
  };
  const gp_Pnt O = frame.Location();
  const gp_Dir X = frame.XDirection(), Y = frame.YDirection(), N = frame.Direction();
  auto to3d = [&](V2 p) {
    return gp_Pnt(O.X() + p.x * X.X() + p.y * Y.X(), O.Y() + p.x * X.Y() + p.y * Y.Y(), O.Z() + p.x * X.Z() + p.y * Y.Z());
  };
  // handedness: CCW in (u,v) is CCW about N only for a right-handed frame
  const double hand = (X.Crossed(Y)).Dot(N) >= 0 ? 1.0 : -1.0;

  // ---- free points ----
  std::set<int> used;
  for (const auto& e : sk.entities) used.insert(e.points.begin(), e.points.end());
  std::vector<const SkPoint*> fps;
  for (const auto& p : sk.points)
    if (p.id != 0 && !used.count(p.id)) fps.push_back(&p);
  std::stable_sort(fps.begin(), fps.end(), [](const SkPoint* a, const SkPoint* b) {
    long long ka = std::llround(a->x * 1e6), kb = std::llround(b->x * 1e6);
    if (ka != kb) return ka < kb;
    return a->y < b->y;
  });
  for (auto* p : fps) {
    P.freePoints.push_back(to3d({p->x, p->y}));
    P.freePoints2d.emplace_back(p->id, gp_Pnt2d(p->x, p->y));
  }

  // ---- scale ----
  double scale = 1.0;
  {
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    for (const auto& p : sk.points) { mnx = std::min(mnx, p.x); mxx = std::max(mxx, p.x); mny = std::min(mny, p.y); mxy = std::max(mxy, p.y); }
    if (mxx >= mnx) scale = std::max(scale, std::hypot(mxx - mnx, mxy - mny));
    for (const auto& e : sk.entities) if (e.type == SkEntityType::Circle) scale = std::max(scale, 2 * e.radius);
  }
  const double mergeTol = std::max(1e-6, 1e-9 * scale);
  const double geomTol = std::max(1e-7, 1e-8 * scale);

  // ---- curves + nodes ----
  std::vector<Curve> curves;
  std::vector<int> nodePt;                 // node candidates: point ids used as endpoints
  std::map<int, int> ptNode;
  auto nodeOf = [&](int pid) {
    auto it = ptNode.find(pid);
    if (it != ptNode.end()) return it->second;
    int k = (int)nodePt.size();
    nodePt.push_back(pid);
    ptNode[pid] = k;
    return k;
  };
  for (const auto& e : sk.entities) {
    if (e.construction) continue;
    size_t need = e.type == SkEntityType::Line ? 2 : e.type == SkEntityType::Circle ? 1 : 3;
    bool okRefs = e.points.size() == need;
    for (int pid : e.points) okRefs = okRefs && sk.point(pid);
    if (!okRefs) { err("Geçersiz varlık " + std::to_string(e.id) + ": nokta referansı"); continue; }
    Curve cv;
    cv.eid = e.id;
    cv.type = e.type;
    if (e.type == SkEntityType::Line) {
      const SkPoint *p1 = sk.point(e.points[0]), *p2 = sk.point(e.points[1]);
      cv.a = {p1->x, p1->y}; cv.b = {p2->x, p2->y};
      if (norm(cv.b - cv.a) <= mergeTol) { err("Sıfır uzunluklu doğru: varlık " + std::to_string(e.id)); continue; }
      cv.na = nodeOf(e.points[0]); cv.nb = nodeOf(e.points[1]);
    } else if (e.type == SkEntityType::Circle) {
      const SkPoint* c = sk.point(e.points[0]);
      cv.c = {c->x, c->y}; cv.r = e.radius; cv.a0 = 0; cv.sweep = 2 * kPi;
      if (!(cv.r > mergeTol)) { err("Geçersiz daire yarıçapı: varlık " + std::to_string(e.id)); continue; }
    } else {
      const SkPoint *c = sk.point(e.points[0]), *s = sk.point(e.points[1]), *t = sk.point(e.points[2]);
      cv.c = {c->x, c->y}; cv.a = {s->x, s->y}; cv.b = {t->x, t->y};
      cv.r = norm(cv.a - cv.c);
      double r2 = norm(cv.b - cv.c);
      if (!(cv.r > mergeTol) || norm(cv.b - cv.a) <= mergeTol) { err("Dejenere yay: varlık " + std::to_string(e.id)); continue; }
      if (std::abs(r2 - cv.r) > 1e-6 * std::max(1.0, cv.r)) {
        err("Yay " + std::to_string(e.id) + ": başlangıç/bitiş yarıçapları farklı (çizim çözülmemiş)");
        continue;
      }
      cv.a0 = std::atan2(cv.a.y - cv.c.y, cv.a.x - cv.c.x);
      cv.sweep = normAngle(std::atan2(cv.b.y - cv.c.y, cv.b.x - cv.c.x) - cv.a0);
      if (cv.sweep <= 0) cv.sweep = 2 * kPi;
      cv.na = nodeOf(e.points[1]); cv.nb = nodeOf(e.points[2]);
    }
    curves.push_back(cv);
  }

  // merge nodes by coordinates
  const int nn = (int)nodePt.size();
  UF uf(nn);
  for (int i = 0; i < nn; ++i)
    for (int j = i + 1; j < nn; ++j) {
      const SkPoint *a = sk.point(nodePt[i]), *b = sk.point(nodePt[j]);
      if (std::hypot(a->x - b->x, a->y - b->y) <= mergeTol) uf.u(i, j);
    }
  std::vector<V2> nodePos(nn);
  for (int i = 0; i < nn; ++i) { const SkPoint* p = sk.point(nodePt[uf.f(i)]); nodePos[i] = {p->x, p->y}; }
  for (auto& cv : curves) {
    if (cv.type == SkEntityType::Circle) continue;
    cv.na = uf.f(cv.na); cv.nb = uf.f(cv.nb);
    if (cv.na == cv.nb) { err("Kapalı tek kenar (başlangıç = bitiş): varlık " + std::to_string(cv.eid)); cv.bad = true; continue; }
    cv.a = nodePos[cv.na]; cv.b = nodePos[cv.nb];
    if (cv.type == SkEntityType::Arc) {
      cv.r = norm(cv.a - cv.c);
      cv.a0 = std::atan2(cv.a.y - cv.c.y, cv.a.x - cv.c.x);
      cv.sweep = normAngle(std::atan2(cv.b.y - cv.c.y, cv.b.x - cv.c.x) - cv.a0);
    }
  }

  // ---- topology: degrees, pruning of open chains ----
  std::vector<std::vector<int>> inc(nn);
  for (size_t i = 0; i < curves.size(); ++i)
    if (curves[i].type != SkEntityType::Circle && !curves[i].bad) {
      inc[curves[i].na].push_back((int)i);
      inc[curves[i].nb].push_back((int)i);
    }
  std::vector<int> deg(nn);
  for (int i = 0; i < nn; ++i) deg[i] = (int)inc[i].size();
  std::vector<char> removed(curves.size(), 0);
  {
    std::vector<int> openPts;
    for (int i = 0; i < nn; ++i)
      if (uf.f(i) == i && deg[i] == 1) openPts.push_back(nodePt[i]);
    std::sort(openPts.begin(), openPts.end());
    std::vector<int> stack;
    for (int i = 0; i < nn; ++i) if (deg[i] == 1) stack.push_back(i);
    while (!stack.empty()) {
      int nd = stack.back(); stack.pop_back();
      if (deg[nd] != 1) continue;
      for (int ci : inc[nd]) {
        if (removed[ci]) continue;
        removed[ci] = 1;
        int other = curves[ci].na == nd ? curves[ci].nb : curves[ci].na;
        --deg[nd]; --deg[other];
        if (deg[other] == 1) stack.push_back(other);
      }
    }
    if (!openPts.empty()) {
      std::ostringstream os;
      os << "Açık kontur: nokta ";
      for (size_t k = 0; k < openPts.size(); ++k) os << (k ? ", " : "") << openPts[k];
      os << " (bağlantısız kenarlar:";
      for (size_t ci = 0; ci < curves.size(); ++ci)
        if (removed[ci]) os << " " << curves[ci].eid;
      os << ")";
      err(os.str());
    }
  }
  std::vector<char> branchNode(nn, 0);
  for (int i = 0; i < nn; ++i)
    if (deg[i] > 2) {
      branchNode[i] = 1;
      err("Dallanan kontur (" + std::to_string(deg[i]) + " kenar): nokta " + std::to_string(nodePt[i]));
    }

  // ---- loops ----
  std::vector<Loop> loops;
  std::vector<int> curveLoop(curves.size(), -1);
  for (size_t ci = 0; ci < curves.size(); ++ci) {
    const Curve& cv = curves[ci];
    if (cv.bad || removed[ci] || curveLoop[ci] >= 0) continue;
    Loop L;
    if (cv.type == SkEntityType::Circle) {
      L.edges.push_back({(int)ci, true});
      curveLoop[ci] = (int)loops.size();
      loops.push_back(L);
      continue;
    }
    // walk the cycle
    bool broken = false;
    int cur = (int)ci;
    bool fwd = true;
    int startNode = cv.na;
    std::vector<int> members;
    for (size_t guard = 0; guard <= curves.size(); ++guard) {
      L.edges.push_back({cur, fwd});
      members.push_back(cur);
      curveLoop[cur] = (int)loops.size();
      int endNode = fwd ? curves[cur].nb : curves[cur].na;
      if (branchNode[endNode]) broken = true;
      if (endNode == startNode) break;
      int next = -1;
      for (int cj : inc[endNode])
        if (cj != cur && !removed[cj] && curveLoop[cj] < 0) { next = cj; break; }
      if (next < 0) { broken = true; break; }
      fwd = curves[next].na == endNode;
      cur = next;
    }
    if (branchNode[startNode]) broken = true;
    L.bad = broken;
    loops.push_back(L);
  }

  // ---- self / mutual intersections ----
  {
    std::vector<int> act;
    for (size_t ci = 0; ci < curves.size(); ++ci)
      if (curveLoop[ci] >= 0 && !loops[curveLoop[ci]].bad) act.push_back((int)ci);
    const double excl = std::max(1e-6, 1e-5 * scale);
    for (size_t i = 0; i < act.size(); ++i)
      for (size_t j = i + 1; j < act.size(); ++j) {
        const Curve &A = curves[act[i]], &B = curves[act[j]];
        std::vector<V2> pts;
        bool overlap = false;
        intersect(A, B, geomTol, pts, overlap);
        bool hit = overlap;
        if (!hit)
          for (V2 p : pts) {
            bool nearShared = false;
            if (A.type != SkEntityType::Circle && B.type != SkEntityType::Circle) {
              for (int na : {A.na, A.nb})
                for (int nb : {B.na, B.nb})
                  if (na == nb && norm(p - nodePos[na]) <= excl) nearShared = true;
            }
            if (!nearShared) { hit = true; break; }
          }
        if (hit) {
          err("Kesişen eğriler: varlık " + std::to_string(A.eid) + " ve " + std::to_string(B.eid));
          loops[curveLoop[act[i]]].bad = true;
          loops[curveLoop[act[j]]].bad = true;
        }
      }
  }

  // ---- area, probe, nesting ----
  std::vector<int> good;
  for (size_t li = 0; li < loops.size(); ++li) {
    Loop& L = loops[li];
    if (L.bad) continue;
    for (auto& le : L.edges) L.area += (le.fwd ? 1 : -1) * curveArea(curves[le.curve]);
    L.probe = curveMid(curves[L.edges[0].curve]);
    good.push_back((int)li);
  }
  auto contains = [&](const Loop& outer, V2 p) {
    double w = 0;
    for (auto& le : outer.edges) w += windingAngle(curves[le.curve], le.fwd, p);
    return std::abs(w) > kPi;
  };
  for (int i : good) {
    for (int j : good) {
      if (i == j) continue;
      if (contains(loops[j], loops[i].probe)) loops[i].depth++;
    }
  }
  for (int i : good) {
    for (int j : good) {
      if (i == j || loops[j].depth != loops[i].depth - 1) continue;
      if (contains(loops[j], loops[i].probe)) { loops[i].parent = j; break; }
    }
  }

  // ---- OCCT construction ----
  BRep_Builder BB;
  std::vector<double> nodeTol(nn, Precision::Confusion());
  for (const auto& cv : curves)
    if (cv.type == SkEntityType::Arc && !cv.bad && cv.na >= 0) {
      double mis = std::abs(norm(cv.b - cv.c) - cv.r);
      nodeTol[cv.nb] = std::max(nodeTol[cv.nb], 2.0 * mis);
    }
  std::vector<TopoDS_Vertex> verts(nn);
  auto vertex = [&](int nd) -> const TopoDS_Vertex& {
    if (verts[nd].IsNull()) BB.MakeVertex(verts[nd], to3d(nodePos[nd]), nodeTol[nd]);
    return verts[nd];
  };
  std::vector<TopoDS_Edge> occEdges(curves.size());
  auto makeEdge = [&](int ci) -> bool {
    if (!occEdges[ci].IsNull()) return true;
    const Curve& cv = curves[ci];
    gp_Ax2 ax(to3d(cv.c), hand > 0 ? N : N.Reversed(), X);
    if (cv.type == SkEntityType::Line) {
      BRepBuilderAPI_MakeEdge me(vertex(cv.na), vertex(cv.nb));
      if (!me.IsDone()) return false;
      occEdges[ci] = me.Edge();
    } else if (cv.type == SkEntityType::Circle) {
      BRepBuilderAPI_MakeEdge me(gp_Circ(ax, cv.r));
      if (!me.IsDone()) return false;
      occEdges[ci] = me.Edge();
    } else {
      // parameter of the circle in ax: angle measured from X in the (X, N x X) system
      Handle(Geom_Circle) gc = new Geom_Circle(ax, cv.r);
      double p1 = normAngle(cv.a0), p2 = p1 + cv.sweep;
      BRepBuilderAPI_MakeEdge me(gc, vertex(cv.na), vertex(cv.nb), p1, p2);
      if (!me.IsDone()) return false;
      occEdges[ci] = me.Edge();
    }
    return true;
  };

  gp_Pln pln(frame);
  for (int fi : good) {
    if (loops[fi].depth % 2 != 0) continue;
    std::vector<int> wl{fi};
    for (int h : good)
      if (loops[h].parent == fi && loops[h].depth % 2 == 1) wl.push_back(h);
    bool okFace = true;
    std::vector<TopoDS_Wire> wires;
    for (size_t k = 0; k < wl.size() && okFace; ++k) {
      const Loop& L = loops[wl[k]];
      bool wantCCW = k == 0;  // outer CCW (about N), holes CW
      bool reverse = (L.area * hand > 0) != wantCCW;
      TopoDS_Wire w;
      BB.MakeWire(w);
      const int ne = (int)L.edges.size();
      for (int q = 0; q < ne; ++q) {
        const LoopEdge& le = reverse ? L.edges[ne - 1 - q] : L.edges[q];
        if (!makeEdge(le.curve)) { okFace = false; break; }
        bool f = reverse ? !le.fwd : le.fwd;
        BB.Add(w, f ? occEdges[le.curve] : TopoDS::Edge(occEdges[le.curve].Reversed()));
      }
      w.Closed(Standard_True);
      wires.push_back(w);
    }
    if (!okFace) { err("Kenar oluşturulamadı (kontur " + std::to_string(fi) + ")"); continue; }
    BRepBuilderAPI_MakeFace mf(pln, wires[0], Standard_True);
    for (size_t k = 1; k < wires.size() && mf.IsDone(); ++k) mf.Add(wires[k]);
    if (!mf.IsDone()) { err("Yüz oluşturulamadı"); continue; }
    TopoDS_Face face = mf.Face();
    P.faces.push_back(face);
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
      const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
      for (size_t ci = 0; ci < occEdges.size(); ++ci)
        if (!occEdges[ci].IsNull() && occEdges[ci].IsSame(e)) { P.edges.push_back({e, curves[ci].eid}); break; }
    }
  }

  P.ok = errors.empty();
  std::ostringstream os;
  for (size_t i = 0; i < errors.size(); ++i) os << (i ? "; " : "") << errors[i];
  P.message = os.str();
  return P;
}

gp_Ax3 standardFrame(SketchPlaneKind kind, double offset) {
  // Right-handed frames (u x v = normal), offset along the normal.
  switch (kind) {
    case SketchPlaneKind::XZ: return gp_Ax3(gp_Pnt(0, -offset, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    case SketchPlaneKind::YZ: return gp_Ax3(gp_Pnt(offset, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    default: return gp_Ax3(gp_Pnt(0, 0, offset), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  }
}

} // namespace pc::sketch
