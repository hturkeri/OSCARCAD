// S5 — view graph: alignment relations, projection method, primary view, symmetry axes (rotational parts),
// view roles and sheet -> view (part mm) transforms.
#include "interp/InterpUtil.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace pc::drawing {
using namespace interp;

namespace {

struct AxisEval { int prim = -1; bool horizontal = true; double pos = 0; double score = 0; };

double mirrorScore(const std::vector<const Primitive*>& vis, bool horizontal, double pos, double tol) {
  double total = 0, ok = 0;
  for (auto* p : vis) {
    for (auto& q : samplePrim(*p, 0.5)) {
      Vec2 m = horizontal ? Vec2{q.x, 2 * pos - q.y} : Vec2{2 * pos - q.x, q.y};
      total += 1;
      for (auto* o : vis) if (distToPrim(*o, m) < tol) { ok += 1; break; }
    }
  }
  return total > 0 ? ok / total : 0;
}

// last edit of an op for a view (payload parsed), nullptr-json if none
nlohmann::json lastEdit(const DrawingIR& ir, const char* op, int viewId) {
  nlohmann::json r;
  for (auto& e : ir.edits) {
    if (e.stage != Stage::Views || e.op != op) continue;
    try {
      auto j = nlohmann::json::parse(e.payload);
      if (viewId < 0 || j.value("viewId", -2) == viewId) r = j;
    } catch (...) {}
  }
  return r;
}

}  // namespace

StageResult buildViews(DrawingIR& ir, const InterpretOptions& opt, ProgressSink* prog) {
  StageResult res = StageResult::success();
  const Stage st = Stage::Views;
  ViewGraph G;
  SheetLayout& L = ir.layout;
  if (L.views.empty()) return StageResult::failure(Status::InvalidInput, "Görünüş yok (sayfa düzeni adımını kontrol edin)");
  report(prog, 0.1, "Görünüş ilişkileri");
  std::map<int, Box2> vbox;
  std::map<int, std::vector<const Primitive*>> vis;
  for (auto& v : L.views) {
    vbox[v.id] = visibleBox(ir, v);
    vis[v.id] = viewPrims(ir, v, {LineRole::Visible});
  }
  // ---------------------------------------------------------------- relations
  for (size_t i = 0; i < L.views.size(); ++i)
    for (size_t j = 0; j < L.views.size(); ++j) {
      if (i == j) continue;
      const auto& a = vbox[L.views[i].id];
      const auto& b = vbox[L.views[j].id];
      double eh = 0.5 * (std::abs(a.lo.y - b.lo.y) + std::abs(a.hi.y - b.hi.y));
      double ev = 0.5 * (std::abs(a.lo.x - b.lo.x) + std::abs(a.hi.x - b.hi.x));
      double th = std::max(1.0, 0.02 * std::max(height(a), height(b)));
      double tv = std::max(1.0, 0.02 * std::max(width(a), width(b)));
      // centre alignment as fallback (end views of turned parts: same centre line)
      double ch = std::abs((a.lo.y + a.hi.y) - (b.lo.y + b.hi.y)) / 2, cv = std::abs((a.lo.x + a.hi.x) - (b.lo.x + b.hi.x)) / 2;
      ViewRelation r;
      r.a = L.views[i].id; r.b = L.views[j].id;
      if ((eh < th || ch < th) && (b.lo.x > a.hi.x || b.hi.x < a.lo.x)) {
        r.adj = b.lo.x > a.hi.x ? Adjacency::RightOf : Adjacency::LeftOf;
        r.alignErr = std::min(eh, ch);
        r.conf = float(std::clamp(1.0 - r.alignErr / th, 0.3, 1.0));
      } else if ((ev < tv || cv < tv) && (b.lo.y > a.hi.y || b.hi.y < a.lo.y)) {
        r.adj = b.lo.y > a.hi.y ? Adjacency::Above : Adjacency::Below;
        r.alignErr = std::min(ev, cv);
        r.conf = float(std::clamp(1.0 - r.alignErr / tv, 0.3, 1.0));
      } else continue;
      G.relations.push_back(r);
    }
  // ---------------------------------------------------------------- axes, symmetry, rotational
  report(prog, 0.4, "Simetri eksenleri");
  std::map<int, AxisEval> axis;
  for (auto& v : L.views) {
    const Box2& b = vbox[v.id];
    double tol = std::max(0.1, 0.002 * std::max(width(b), height(b)));
    AxisEval best;
    auto forced = lastEdit(ir, edit_op::setAxis, v.id);
    for (int id : v.primIds) {
      if (roleOf(ir, id) != LineRole::Center) continue;
      const Primitive* p = findPrim(ir.norm, id);
      if (!p || p->kind != PrimKind::Line) continue;
      if (!forced.is_null() && forced.value("primId", -1) != id) continue;
      Vec2 d = unit(p->p1 - p->p0);
      bool h = std::abs(d.y) < 0.01, ve = std::abs(d.x) < 0.01;
      if (!h && !ve) continue;
      double len = distance(p->p0, p->p1);
      double ext = h ? width(b) : height(b);
      if (forced.is_null() && len < 0.6 * ext) continue;
      double pos = h ? 0.5 * (p->p0.y + p->p1.y) : 0.5 * (p->p0.x + p->p1.x);
      double sc = mirrorScore(vis[v.id], h, pos, tol);
      if (sc > best.score || best.prim < 0) best = {id, h, pos, sc};
    }
    if (best.prim >= 0) {
      axis[v.id] = best;
      G.axisOfView[v.id] = best.prim;
      G.symmetryScore[v.id] = best.score;
    }
  }
  // dims per view
  std::map<int, int> dimCount, diaCount;
  for (auto& d : ir.annot.dims) { dimCount[d.viewId]++; if (d.kind == DimKind::Diameter) diaCount[d.viewId]++; }
  for (auto& v : L.views) {
    G.rotational[v.id] = false;
    auto it = axis.find(v.id);
    if (it == axis.end()) continue;
    const AxisEval& ax = it->second;
    double lineLen = 0, all = 0;
    for (auto* p : vis[v.id]) { double l = primLength(*p); all += l; if (p->kind == PrimKind::Line) lineLen += l; }
    bool profileLike = all > 0 && lineLen / all >= 0.5;
    bool hatch = false;
    for (int id : v.primIds) { auto* p = findPrim(ir.norm, id); if (p && p->kind == PrimKind::HatchRegion) hatch = true; }
    // end view: horizontally (axis horizontal) / vertically adjacent view with >= 2 concentric circles centred on the axis
    int endView = -1;
    for (auto& r : G.relations) {
      if (r.a != v.id) continue;
      bool ok = ax.horizontal ? (r.adj == Adjacency::LeftOf || r.adj == Adjacency::RightOf) : (r.adj == Adjacency::Above || r.adj == Adjacency::Below);
      if (!ok) continue;
      std::vector<const Primitive*> circ;
      for (auto* p : vis[r.b]) if (p->kind == PrimKind::Circle) circ.push_back(p);
      int conc = 0;
      for (auto* c1 : circ) {
        double off = ax.horizontal ? std::abs(c1->center.y - ax.pos) : std::abs(c1->center.x - ax.pos);
        if (off > 0.5) continue;
        int n = 0;
        for (auto* c2 : circ) if (c2 != c1 && distance(c1->center, c2->center) < 0.2) ++n;
        conc = std::max(conc, n + 1);
      }
      if (conc >= 2) { endView = r.b; break; }
    }
    bool rot = ax.score >= 0.9 && profileLike && (hatch || diaCount[v.id] > 0 || endView >= 0);
    G.rotational[v.id] = rot;
    if (rot && endView >= 0) G.attrs["interp.endView." + std::to_string(v.id)] = std::to_string(endView);
  }
  // ---------------------------------------------------------------- primary view
  report(prog, 0.7, "Birincil görünüş");
  double bestScore = -1;
  for (auto& v : L.views) {
    if (v.role == ViewRole::Ignore) continue;
    double sc = double(vis[v.id].size()) + 2.0 * dimCount[v.id] + (G.rotational[v.id] ? 1000 : 0);
    if (sc > bestScore) { bestScore = sc; G.primaryView = v.id; }
  }
  auto fp = lastEdit(ir, edit_op::setPrimaryView, -1);
  if (!fp.is_null() && findView(L, fp.value("viewId", -1))) G.primaryView = fp.value("viewId", -1);
  // ---------------------------------------------------------------- projection
  if (L.title.projection != ProjectionMethod::Unknown) { G.projection = L.title.projection; G.projectionConf = 0.9f; }
  else {
    G.projection = opt.defaultProjection;
    G.projectionConf = 0.5f;
    res.warn("views.projectionDefault", std::string("Projeksiyon yöntemi antette yok; varsayılan ") +
             (opt.defaultProjection == ProjectionMethod::ThirdAngle ? "ISO A (3. açı)" : "ISO E (1. açı)") + " kullanıldı", {}, (int)st);
  }
  // ---------------------------------------------------------------- roles
  for (auto& v : L.views) {
    if (v.userEdited || v.role == ViewRole::Section || v.role == ViewRole::Detail || v.role == ViewRole::Ignore) continue;
    if (v.id == G.primaryView) { v.role = ViewRole::Front; v.roleConf = 0.8f; continue; }
    v.role = ViewRole::Unknown;
    for (auto& r : G.relations) {
      if (r.a != G.primaryView || r.b != v.id) continue;
      bool first = G.projection != ProjectionMethod::ThirdAngle;
      switch (r.adj) {
        case Adjacency::Below: v.role = first ? ViewRole::Top : ViewRole::Bottom; break;
        case Adjacency::Above: v.role = first ? ViewRole::Bottom : ViewRole::Top; break;
        case Adjacency::RightOf: v.role = first ? ViewRole::Left : ViewRole::Right; break;
        case Adjacency::LeftOf: v.role = first ? ViewRole::Right : ViewRole::Left; break;
        default: break;
      }
      v.roleConf = 0.7f * r.conf;
    }
  }
  // ---------------------------------------------------------------- sheet -> view transforms
  for (auto& v : L.views) {
    const Box2& b = vbox[v.id];
    double f = viewScaleFactor(v);
    Vec2 o = b.lo;
    auto it = axis.find(v.id);
    if (G.rotational[v.id] && it != axis.end()) {
      o = it->second.horizontal ? Vec2{b.lo.x, it->second.pos} : Vec2{it->second.pos, b.lo.y};
      v.attrs["interp.axisDir"] = it->second.horizontal ? "h" : "v";
    } else {
      v.attrs.erase("interp.axisDir");
      for (auto& [k, val] : G.attrs)   // end view of a rotational profile: origin = circle centre
        if (k.rfind("interp.endView.", 0) == 0 && std::stoi(val) == v.id) {
          int pv = std::stoi(k.substr(15));
          const AxisEval& ax = axis[pv];
          for (auto* p : vis[v.id])
            if (p->kind == PrimKind::Circle && (ax.horizontal ? std::abs(p->center.y - ax.pos) : std::abs(p->center.x - ax.pos)) < 0.5) {
              o = p->center; v.attrs["interp.endViewOf"] = std::to_string(pv); break;
            }
        }
    }
    v.sheetToView = {f, 0, 0, f, -f * o.x, -f * o.y};
    v.attrs["interp.vbox"] = fmt(b.lo.x) + "," + fmt(b.lo.y) + "," + fmt(b.hi.x) + "," + fmt(b.hi.y);
  }
  ir.views = std::move(G);
  int nrot = 0;
  for (auto& [k, r] : ir.views.rotational) nrot += r;
  const ViewRegion* pv = findView(L, ir.views.primaryView);
  res.message = std::to_string(L.views.size()) + " görünüş, birincil: " + (pv ? std::string(labelTr(pv->role)) : "-") +
                (nrot ? ", döner simetrik" : "");
  report(prog, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
