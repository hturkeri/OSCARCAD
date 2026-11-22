// applyUserEdits — replay of wizard / overlay corrections (UserEdit.h catalogue) after their stage.
#include "interp/InterpUtil.h"
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace pc::drawing {
using namespace interp;
using nlohmann::json;

namespace {

Vec2 jv(const json& j) { return j.is_array() && j.size() >= 2 ? Vec2{j[0].get<double>(), j[1].get<double>()} : Vec2{}; }
Box2 jb(const json& j) { return {jv(j.value("lo", json::array())), jv(j.value("hi", json::array()))}; }
std::vector<int> jids(const json& j, const char* k) {
  std::vector<int> v;
  if (j.contains(k) && j[k].is_array()) for (auto& x : j[k]) v.push_back(x.get<int>());
  return v;
}

SketchCandidate* findSketch(DrawingIR& ir, int id) {
  for (auto& c : ir.sketches) if (c.id == id) return &c;
  return nullptr;
}

void resolveCandidate(SketchCandidate& c) {
  ParamValues vals = proposalValues(c.params);
  Sketch s = c.sketch;
  auto r = sketch::solve(s, vals);
  if (r.converged) c.sketch = s;
  c.dof = r.dof;
  c.conflicting = r.conflicting;
  c.attrs["interp.solveStatus"] = r.message;
}

void closeMeasured(SketchCandidate& c, double rounding, std::set<std::string>& names) {
  ParamValues vals = proposalValues(c.params);
  auto dof = [&] { return sketch::degreesOfFreedom(c.sketch, vals); };
  auto fresh = [&](const std::string& p) {
    for (int k = 1;; ++k) { std::string n = p + std::to_string(k); if (!names.count(n)) { names.insert(n); return n; } }
  };
  std::vector<SkPoint> pts = c.sketch.points;
  std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
  for (auto& p : pts) {
    if (p.id == 0) continue;
    for (int ax = 0; ax < 2; ++ax) {
      int d0 = dof();
      if (d0 <= 0) return;
      double v = ax == 0 ? p.x : p.y;
      if (rounding > 0) v = std::round(v / rounding) * rounding;
      if (std::abs(v) < 1e-9) {
        int id = c.sketch.addConstraint(ax == 0 ? SkConstraintType::VerticalPts : SkConstraintType::HorizontalPts, {0, p.id});
        if (dof() >= d0) c.sketch.constraints.pop_back(); else c.measuredConstraints.push_back(id);
        continue;
      }
      std::string n = fresh("L_olc");
      vals[n] = std::abs(v);
      int id = c.sketch.addConstraint(ax == 0 ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {0, p.id}, v >= 0 ? n : "-" + n);
      if (dof() >= d0) { c.sketch.constraints.pop_back(); vals.erase(n); names.erase(n); continue; }
      c.measuredConstraints.push_back(id);
      ParamProposal pp;
      pp.param.name = n;
      pp.param.expression = fmt(std::abs(v), 6);
      pp.param.description = "[Ö] geometriden ölçüldü (kullanıcı: kalan serbestlikleri kapat)";
      pp.tag = ParamTag::Measured;
      pp.nominalValue = pp.tolMidValue = std::abs(v);
      c.params.push_back(pp);
    }
  }
}

void renameEverywhere(DrawingIR& ir, const std::string& from, const std::string& to) {
  for (auto& c : ir.sketches) {
    for (auto& p : c.params) {
      if (p.param.name == from) p.param.name = to;
      p.param.expression = renameIdentifier(p.param.expression, from, to);
    }
    for (auto& k : c.sketch.constraints) if (!k.valueExpr.empty()) k.valueExpr = renameIdentifier(k.valueExpr, from, to);
    c.sketch.plane.offsetExpr = renameIdentifier(c.sketch.plane.offsetExpr, from, to);
    for (auto& [k, v] : c.attrs) if (v == from) v = to;
  }
}

double measuredOf(const DrawingIR& ir, const Dimension& d) {
  const ViewRegion* v = findView(ir.layout, d.viewId);
  double f = v ? viewScaleFactor(*v) : 1;
  return d.kind == DimKind::Angle ? d.measuredSheet : d.measuredSheet * f;
}

void refreshResidual(const DrawingIR& ir, Dimension& d) {
  d.measuredPart = measuredOf(ir, d);
  if (!d.text.valid) return;
  double rn = d.measuredPart - d.text.nominal, rm = d.measuredPart - d.text.tolMid();
  d.residualVsTolMid = std::abs(rm) < std::abs(rn) - 1e-12;
  d.residual = d.residualVsTolMid ? rm : rn;
  double band = std::max({std::abs(d.text.tolPlus.value_or(0) - d.text.tolMinus.value_or(0)), 0.005 * std::abs(d.text.nominal), 0.05});
  d.outOfBand = !d.text.isMax && !d.text.isMin && std::abs(d.residual) > band;
}

void geometryToDim(const DrawingIR& ir, Dimension& d) {
  std::vector<const Primitive*> g;
  for (int id : d.geomPrimIds) if (auto* p = findPrim(ir.norm, id)) g.push_back(p);
  if (g.size() == 1 && (g[0]->kind == PrimKind::Circle || g[0]->kind == PrimKind::Arc)) {
    bool rad = d.text.kind == DimKind::Radius;
    d.kind = rad ? DimKind::Radius : DimKind::Diameter;
    d.anchors = {g[0]->center, {g[0]->center.x + g[0]->radius, g[0]->center.y}};
    d.measuredSheet = rad ? g[0]->radius : 2 * g[0]->radius;
    d.direction = {1, 0};
  } else if (g.size() == 1 && g[0]->kind == PrimKind::Line) {
    d.anchors = {g[0]->p0, g[0]->p1};
    d.direction = unit(g[0]->p1 - g[0]->p0);
    d.measuredSheet = distance(g[0]->p0, g[0]->p1);
    d.kind = DimKind::Aligned;
  } else if (g.size() >= 2 && g[0]->kind == PrimKind::Line && g[1]->kind == PrimKind::Line) {
    Vec2 dir = unit(g[0]->p1 - g[0]->p0);
    Vec2 n = perp(dir);
    Vec2 a = g[0]->p0;
    double t = dot(g[1]->p0 - a, n);
    d.anchors = {a, a + n * t};
    d.direction = std::abs(n.x) >= std::abs(n.y) ? Vec2{n.x >= 0 ? 1.0 : -1.0, 0} * (1.0) : Vec2{0, 1};
    if (std::abs(n.x) > 0.9999 || std::abs(n.y) > 0.9999) d.direction = {std::abs(n.x), std::abs(n.y)};
    else d.direction = n;
    d.measuredSheet = std::abs(t);
    d.kind = d.text.kind == DimKind::Diameter ? DimKind::Diameter : (std::abs(n.x) > 0.9999 || std::abs(n.y) > 0.9999 ? DimKind::Linear : DimKind::Aligned);
  } else if (g.size() >= 2) {
    Vec2 a = g[0]->kind == PrimKind::Line ? g[0]->p0 : g[0]->center, b = g[1]->kind == PrimKind::Line ? g[1]->p0 : g[1]->center;
    d.anchors = {a, b};
    d.direction = unit(b - a);
    d.measuredSheet = distance(a, b);
    d.kind = DimKind::Aligned;
  }
}

}  // namespace

StageResult applyUserEdits(DrawingIR& ir, Stage stage) {
  StageResult res = StageResult::success();
  int applied = 0;
  auto stale = [&](const UserEdit& e, const std::string& what) {
    res.warn("edit.staleRef", "Düzeltme uygulanamadı (" + e.op + "): " + what, {}, static_cast<int>(stage));
  };
  std::set<std::string> names;
  for (auto& c : ir.sketches) for (auto& p : c.params) names.insert(p.param.name);
  for (const UserEdit& e : ir.edits) {
    if (e.stage != stage) continue;
    json j;
    try { j = json::parse(e.payload.empty() ? "{}" : e.payload); } catch (...) { stale(e, "geçersiz JSON"); continue; }
    const std::string& op = e.op;
    try {
      // ------------------------------------------------------------------ Ingest (consumed as options before S1)
      if (op == edit_op::setSourceKind || op == edit_op::setPage || op == edit_op::setDpi) { ++applied; continue; }
      // ------------------------------------------------------------------ Normalize
      if (op == edit_op::deletePrims) {
        auto ids = jids(j, "ids");
        std::set<int> s(ids.begin(), ids.end());
        auto& v = ir.norm.prims;
        size_t n0 = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& p) { return s.count(p.id); }), v.end());
        if (v.size() == n0) stale(e, "ilkel yok"); else ++applied;
      } else if (op == edit_op::addPrim) {
        Primitive p;
        p.id = ir.newId();
        if (!fromString(j.value("kind", std::string("Line")), p.kind)) p.kind = PrimKind::Line;
        p.p0 = jv(j.value("p0", json::array())); p.p1 = jv(j.value("p1", json::array()));
        p.center = jv(j.value("center", json::array()));
        p.radius = j.value("radius", 0.0); p.a0 = j.value("a0", 0.0); p.a1 = j.value("a1", 360.0);
        double wmax = 0, wmin = 1e9;
        for (auto& q : ir.norm.prims) if (q.style.width > 0) { wmax = std::max(wmax, q.style.width); wmin = std::min(wmin, q.style.width); }
        std::string role = j.value("role", std::string());
        p.style.width = role.empty() || role == "Visible" ? wmax : (wmin < 1e9 ? wmin : 0);
        if (role == "Hidden") p.style.dash = {3, 1.5};
        if (role == "Center") p.style.dash = {8, 1.5, 1, 1.5};
        if (!role.empty()) p.attrs["edit.role"] = role;
        p.conf = kConfUser;
        p.prov.source = ir.norm.source;
        p.prov.ref = "kullanıcı";
        ir.norm.prims.push_back(p);
        ++applied;
      } else if (op == edit_op::mergePrims) {
        auto ids = jids(j, "ids");
        std::vector<Primitive*> ps;
        for (int id : ids) if (auto* p = findPrim(ir.norm, id); p && p->kind == PrimKind::Line) ps.push_back(p);
        if (ps.size() < 2) { stale(e, "birleştirilecek çizgi yok"); continue; }
        Vec2 d = unit(ps[0]->p1 - ps[0]->p0), o = ps[0]->p0;
        double tmin = 1e300, tmax = -1e300;
        for (auto* p : ps) for (Vec2 q : {p->p0, p->p1}) { double t = dot(q - o, d); tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
        Primitive m = *ps[0];
        m.id = ir.newId();
        m.p0 = o + d * tmin; m.p1 = o + d * tmax;
        m.mergedFrom = ids;
        m.conf = kConfUser;
        std::set<int> s(ids.begin(), ids.end());
        ir.norm.prims.erase(std::remove_if(ir.norm.prims.begin(), ir.norm.prims.end(), [&](auto& p) { return s.count(p.id); }), ir.norm.prims.end());
        ir.norm.prims.push_back(m);
        ++applied;
      } else if (op == edit_op::snapPoint) {
        Primitive* p = findPrim(ir.norm, j.value("id", -1));
        if (!p) { stale(e, "ilkel yok"); continue; }
        Vec2 to = jv(j.value("to", json::array()));
        int end = j.value("end", 0);
        if (p->kind == PrimKind::Line) (end == 0 ? p->p0 : p->p1) = to;
        else if (p->kind == PrimKind::Arc) {
          double a = rad2deg(std::atan2(to.y - p->center.y, to.x - p->center.x));
          if (end == 0) { p->a0 = a; while (p->a1 <= p->a0) p->a1 += 360; }
          else { p->a1 = a; while (p->a1 <= p->a0) p->a1 += 360; }
        }
        p->conf = kConfUser;
        ++applied;
      } else if (op == edit_op::setText) {
        TextRun* t = findText(ir.norm, j.value("textId", -1));
        if (!t) { stale(e, "metin yok"); continue; }
        t->utf8 = j.value("utf8", t->utf8);
        t->conf = kConfUser;
        t->alternatives.clear();
        ++applied;
      } else if (op == edit_op::deleteTexts) {
        auto ids = jids(j, "ids");
        std::set<int> s(ids.begin(), ids.end());
        ir.norm.texts.erase(std::remove_if(ir.norm.texts.begin(), ir.norm.texts.end(), [&](auto& t) { return s.count(t.id); }), ir.norm.texts.end());
        ++applied;
      }
      // ------------------------------------------------------------------ Layout
      else if (op == edit_op::setBorder) { ir.layout.border = jb(j["box"]); ir.layout.borderConf = kConfUser; ++applied; }
      else if (op == edit_op::setTitleBlockBox) { ir.layout.title.box = jb(j["box"]); ir.layout.title.conf = kConfUser; ++applied; }
      else if (op == edit_op::setTitleField) {
        std::string k = j.value("key", std::string()), v = j.value("value", std::string());
        ir.layout.title.fields[k] = v;
        double n, d;
        if (k == "scale" && parseScaleText(v, n, d)) { ir.layout.title.scale.num = n; ir.layout.title.scale.den = d; ir.layout.title.scale.conf = kConfUser; }
        ++applied;
      } else if (op == edit_op::setScale) {
        Scale s;
        if (j.value("nts", false)) s.notToScale = true;
        else { s.num = j.value("num", 1.0); s.den = j.value("den", 1.0); }
        if (s.num <= 0 || s.den <= 0) { stale(e, "geçersiz ölçek"); continue; }
        s.conf = kConfUser;
        s.evidence = "kullanıcı";
        ir.layout.title.scale = s;
        for (auto& v : ir.layout.views) if (v.role != ViewRole::Detail || v.scale.evidence != "görünüş etiketi") v.scale = s;
        ir.layout.attrs["interp.userScale"] = "1";
        ++applied;
      } else if (op == edit_op::setProjection) {
        ProjectionMethod m;
        if (!fromString(j.value("method", std::string()), m)) { stale(e, "yöntem"); continue; }
        ir.layout.title.projection = m;
        ++applied;
      } else if (op == edit_op::addView || op == edit_op::setViewBox) {
        ViewRegion nv;
        ViewRegion* v = &nv;
        if (op == edit_op::setViewBox) {
          v = findView(ir.layout, j.value("viewId", -1));
          if (!v) { stale(e, "görünüş yok"); continue; }
        } else {
          nv.id = ir.newId();
          nv.scale = ir.layout.title.scale;
          fromString(j.value("role", std::string("Unknown")), nv.role);
          nv.label = j.value("label", std::string());
        }
        v->box = jb(j["box"]);
        v->primIds.clear(); v->textIds.clear();
        for (auto& p : ir.norm.prims) {
          Box2 b = primBox(p);
          if (b.lo.x >= v->box.lo.x && b.hi.x <= v->box.hi.x && b.lo.y >= v->box.lo.y && b.hi.y <= v->box.hi.y) v->primIds.push_back(p.id);
        }
        for (auto& t : ir.norm.texts) if (contains(v->box, textCentre(t))) v->textIds.push_back(t.id);
        v->userEdited = true;
        v->roleConf = kConfUser;
        if (op == edit_op::addView) {
          // a user view takes its primitives away from the automatically found ones
          std::set<int> s(nv.primIds.begin(), nv.primIds.end());
          for (auto& o : ir.layout.views) o.primIds.erase(std::remove_if(o.primIds.begin(), o.primIds.end(), [&](int id) { return s.count(id); }), o.primIds.end());
          ir.layout.views.erase(std::remove_if(ir.layout.views.begin(), ir.layout.views.end(), [](auto& o) { return o.primIds.empty() && !o.userEdited; }), ir.layout.views.end());
          ir.layout.views.push_back(nv);
        }
        ++applied;
      } else if (op == edit_op::setViewRole) {
        ViewRegion* v = findView(ir.layout, j.value("viewId", -1));
        ViewRole r;
        if (!v || !fromString(j.value("role", std::string()), r)) { stale(e, "görünüş/rol"); continue; }
        v->role = r; v->roleConf = kConfUser; v->userEdited = true;
        ++applied;
      } else if (op == edit_op::deleteView) {
        int id = j.value("viewId", -1);
        auto& vs = ir.layout.views;
        size_t n0 = vs.size();
        vs.erase(std::remove_if(vs.begin(), vs.end(), [&](auto& v) { return v.id == id; }), vs.end());
        if (vs.size() == n0) stale(e, "görünüş yok"); else ++applied;
      }
      // ------------------------------------------------------------------ Annotate
      else if (op == edit_op::setRole) {
        LineRole r;
        if (!fromString(j.value("role", std::string()), r)) { stale(e, "rol"); continue; }
        for (int id : jids(j, "ids")) if (findPrim(ir.norm, id)) { ir.annot.roles[id] = r; ir.annot.roleConf[id] = kConfUser; }
        auto& cl = ir.annot.centerlineIds;
        cl.clear();
        for (auto& [id, rr] : ir.annot.roles) if (rr == LineRole::Center) if (auto* p = findPrim(ir.norm, id); p && p->kind == PrimKind::Line) cl.push_back(id);
        ++applied;
      } else if (op == edit_op::setDimText) {
        Dimension* d = findDimension(ir.annot, j.value("dimId", -1));
        if (!d) { stale(e, "ölçü yok"); continue; }
        DimKind before = d->kind;
        d->text = parseDim(j.value("utf8", d->text.raw));
        d->tolTextIds.clear();
        if (d->text.kind == DimKind::Radius && before == DimKind::Diameter) { d->kind = DimKind::Radius; d->measuredSheet /= 2; }
        else if (d->text.kind == DimKind::Diameter && before == DimKind::Radius) { d->kind = DimKind::Diameter; d->measuredSheet *= 2; }
        d->driving = d->text.valid && !(d->text.reference || d->text.isMax || d->text.isMin);
        d->userEdited = true;
        d->conf = kConfUser;
        refreshResidual(ir, *d);
        ++applied;
      } else if (op == edit_op::bindDimension) {
        Dimension* d = findDimension(ir.annot, j.value("dimId", -1));
        if (!d) { stale(e, "ölçü yok"); continue; }
        d->geomPrimIds = jids(j, "geomPrimIds");
        geometryToDim(ir, *d);
        if (j.contains("anchors") && j["anchors"].size() == 2) {
          d->anchors = {jv(j["anchors"][0]), jv(j["anchors"][1])};
          Vec2 dv = d->anchors[1] - d->anchors[0];
          d->direction = std::abs(dv.x) > std::abs(dv.y) ? Vec2{1, 0} : Vec2{0, 1};
          if (d->kind == DimKind::Linear || d->kind == DimKind::Aligned) d->measuredSheet = std::abs(dot(dv, d->direction));
        }
        d->userEdited = true;
        d->conf = kConfUser;
        refreshResidual(ir, *d);
        ++applied;
      } else if (op == edit_op::setDriving) {
        Dimension* d = findDimension(ir.annot, j.value("dimId", -1));
        if (!d) { stale(e, "ölçü yok"); continue; }
        d->driving = j.value("driving", true);
        d->userEdited = true;
        ++applied;
      } else if (op == edit_op::addDimension) {
        const TextRun* t = findText(ir.norm, j.value("textId", -1));
        if (!t) { stale(e, "metin yok"); continue; }
        Dimension d;
        d.id = ir.newId();
        d.textId = t->id;
        d.text = parseDim(t->utf8);
        d.geomPrimIds = jids(j, "geomPrimIds");
        geometryToDim(ir, d);
        DimKind k;
        if (j.contains("kind") && fromString(j.value("kind", std::string()), k)) d.kind = k;
        int best = -1;
        double bd = 1e300;
        for (auto& v : ir.layout.views) {
          double dd = distance((v.box.lo + v.box.hi) * 0.5, d.anchors[0]);
          if (dd < bd) { bd = dd; best = v.id; }
        }
        d.viewId = best;
        d.driving = d.text.valid && !(d.text.reference || d.text.isMax || d.text.isMin);
        d.userEdited = true;
        d.conf = kConfUser;
        refreshResidual(ir, d);
        ir.annot.dims.push_back(d);
        auto& u = ir.annot.unusedTextIds;
        u.erase(std::remove(u.begin(), u.end(), t->id), u.end());
        ++applied;
      } else if (op == edit_op::deleteDimension) {
        int id = j.value("dimId", -1);
        auto& v = ir.annot.dims;
        size_t n0 = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& d) { return d.id == id; }), v.end());
        if (v.size() == n0) stale(e, "ölçü yok"); else ++applied;
      }
      // ------------------------------------------------------------------ Views (honoured inside buildViews)
      else if (op == edit_op::setPrimaryView || op == edit_op::setAxis) {
        if (!findView(ir.layout, j.value("viewId", -1))) stale(e, "görünüş yok"); else ++applied;
      }
      // ------------------------------------------------------------------ Sketch
      else if (op == edit_op::setParamName) {
        std::string from = j.value("from", std::string()), to = j.value("to", std::string());
        if (!ParamTable::isValidName(to) || names.count(to)) { stale(e, "geçersiz ya da kullanılan ad '" + to + "'"); continue; }
        if (!names.count(from)) { stale(e, "parametre yok '" + from + "'"); continue; }
        renameEverywhere(ir, from, to);
        names.erase(from); names.insert(to);
        for (auto& c : ir.sketches) for (auto& p : c.params) if (p.param.name == to) p.userEdited = true;
        ++applied;
      } else if (op == edit_op::setParamValueMode) {
        SketchCandidate* c = findSketch(ir, j.value("sketchId", -1));
        std::string n = j.value("name", std::string());
        bool mid = j.value("tolMid", false);
        bool found = false;
        for (auto& sc : ir.sketches)
          for (auto& p : sc.params)
            if (p.param.name == n) { p.param.expression = fmt(mid ? p.tolMidValue : p.nominalValue, 6); p.userEdited = true; found = true; resolveCandidate(sc); }
        if (!found || !c) { stale(e, "parametre yok"); continue; }
        ++applied;
      } else if (op == edit_op::closeDofMeasured) {
        SketchCandidate* c = findSketch(ir, j.value("sketchId", -1));
        if (!c) { stale(e, "sketch yok"); continue; }
        closeMeasured(*c, 0.01, names);
        resolveCandidate(*c);
        ++applied;
      } else if (op == edit_op::deleteConstraint) {
        SketchCandidate* c = findSketch(ir, j.value("sketchId", -1));
        int cid = j.value("constraintId", -1);
        if (!c) { stale(e, "sketch yok"); continue; }
        auto& v = c->sketch.constraints;
        size_t n0 = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](auto& k) { return k.id == cid; }), v.end());
        if (v.size() == n0) { stale(e, "kısıt yok"); continue; }
        c->constraintToDimension.erase(cid);
        resolveCandidate(*c);
        ++applied;
      } else if (op == edit_op::replaceSketch) {
        SketchCandidate* c = findSketch(ir, j.value("sketchId", -1));
        if (!c || !j.contains("sketch")) { stale(e, "sketch yok"); continue; }
        c->sketch = sketchFromJson(j["sketch"]);
        for (auto it = c->constraintToDimension.begin(); it != c->constraintToDimension.end();)
          it = std::any_of(c->sketch.constraints.begin(), c->sketch.constraints.end(), [&](auto& k) { return k.id == it->first; })
                   ? std::next(it) : c->constraintToDimension.erase(it);
        c->attrs["interp.userSketch"] = "1";
        resolveCandidate(*c);
        ++applied;
      }
      // ------------------------------------------------------------------ Synthesize
      else if (op == edit_op::chooseStrategy) {
        int id = j.value("candidateId", -1);
        if (!std::any_of(ir.synthesis.begin(), ir.synthesis.end(), [&](auto& s) { return s.id == id; })) { stale(e, "aday yok"); continue; }
        ir.chosenSynthesis = id;
        ++applied;
      } else {
        stale(e, "bilinmeyen işlem");
      }
    } catch (const std::exception& ex) {
      stale(e, ex.what());
    }
  }
  res.message = std::to_string(applied) + " düzeltme uygulandı";
  return res;
}

}  // namespace pc::drawing
