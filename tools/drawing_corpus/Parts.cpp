// Part families with parameter variation and dimensioning plan. OWNER WP-1.
#include "Corpus.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace corpus {
namespace {
const double kPi = 3.14159265358979323846;

gp_Dir axisDir(int a) { return a == 0 ? gp_Dir(1, 0, 0) : a == 1 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1); }
TopoDS_Shape box(double x0, double y0, double z0, double x1, double y1, double z1) {
  return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1)).Shape();
}
TopoDS_Shape cyl(int axis, double cx, double cy, double cz, double r, double t0, double t1) {
  gp_Dir d = axisDir(axis);
  gp_Pnt p(cx + d.X() * t0, cy + d.Y() * t0, cz + d.Z() * t0);
  return BRepPrimAPI_MakeCylinder(gp_Ax2(p, d), r, t1 - t0).Shape();
}
TopoDS_Shape coneX(double x0, double r0, double r1, double len) {
  return BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(x0, 0, 0), gp_Dir(1, 0, 0)), r0, r1, len).Shape();
}
TopoDS_Shape fuse(const TopoDS_Shape& a, const TopoDS_Shape& b) { return BRepAlgoAPI_Fuse(a, b).Shape(); }
TopoDS_Shape cut(const TopoDS_Shape& a, const TopoDS_Shape& b) { return BRepAlgoAPI_Cut(a, b).Shape(); }
// hexagon prism along X, flats at +-s/2 in Z
TopoDS_Shape hexX(double x0, double len, double s) {
  const double e = s / std::sqrt(3.0) * 2;
  BRepBuilderAPI_MakePolygon poly;
  for (int k = 0; k < 6; ++k) poly.Add(gp_Pnt(x0, e / 2 * std::cos(k * kPi / 3), e / 2 * std::sin(k * kPi / 3)));
  poly.Close();
  TopoDS_Face f = BRepBuilderAPI_MakeFace(poly.Wire()).Face();
  return BRepPrimAPI_MakePrism(f, gp_Vec(len, 0, 0)).Shape();
}

std::string fmt(double v) {
  std::ostringstream o; o.setf(std::ios::fixed); o.precision(2); o << v;
  std::string s = o.str();
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}
// value rounded to `step`, uniform in [lo, hi]; nominal for index 0
double pick(std::mt19937& rng, int index, double nominal, double lo, double hi, double step = 1) {
  if (index == 0) return nominal;
  std::uniform_real_distribution<double> u(lo, hi);
  return std::round(u(rng) / step) * step;
}

DimSpec lin(int view, Vec2 a, Vec2 b, bool horiz, double offset, const std::string& prefix, double value,
            const std::string& param, DimKind kind = DimKind::Linear) {
  DimSpec d; d.view = view; d.type = DimType::Linear; d.a = a; d.b = b; d.horiz = horiz; d.offset = offset;
  d.kind = kind; d.nominal = value; d.text = prefix + fmt(value); d.param = param;
  return d;
}
DimSpec inside(int view, Vec2 a, Vec2 b, const std::string& prefix, double value, const std::string& param,
               DimKind kind = DimKind::Diameter) {
  DimSpec d = lin(view, a, b, false, 0, prefix, value, param, kind); d.inside = true; return d;
}
DimSpec radial(int view, Vec2 c, double r, double ang, const std::string& text, double value, const std::string& param,
               DimKind kind = DimKind::Diameter) {
  DimSpec d; d.view = view; d.type = DimType::Radial; d.c = c; d.r = r; d.angDeg = ang; d.kind = kind;
  d.text = text; d.nominal = value; d.param = param; return d;
}
void tol(DimSpec& d, double t) { d.tolPlus = t; d.tolMinus = -t; d.text += "±" + fmt(t); }
void addParams(Part& p) {
  for (auto& d : p.dims)
    if (!d.param.empty()) {
      bool dup = false; for (auto& q : p.params) dup |= q.name == d.param;
      if (!dup) p.params.push_back({d.param, d.nominal, d.driving ? ParamTag::FromDimension : ParamTag::Derived});
    }
}
}  // namespace

const std::vector<std::string>& families() {
  static const std::vector<std::string> f = {"plate", "lbracket", "flange", "shaft", "bushing", "injector"};
  return f;
}

Part makePart(const std::string& fam, int idx, std::mt19937& rng) {
  Part p; p.family = fam;
  std::bernoulli_distribution coin(0.5);
  if (fam == "plate") {
    const double W = pick(rng, idx, 100, 60, 140, 5), H = pick(rng, idx, 60, 40, 90, 5), T = pick(rng, idx, 8, 5, 15);
    const double d = pick(rng, idx, 9, 6, 12, 0.5), e = pick(rng, idx, 10, 8, 14);
    TopoDS_Shape s = box(0, 0, 0, W, T, H);
    for (double x : {e, W - e}) for (double z : {e, H - e}) { s = cut(s, cyl(1, x, 0, z, d / 2, -1, T + 1)); p.cyls.push_back({{x, 0, z}, 1, d / 2, 0, T}); }
    p.shape = s; p.views = {ViewRole::Front, ViewRole::Top}; p.material = "S235JR"; p.strategy = Strategy::ExtrudeTwoView;
    p.dims.push_back(lin(0, {0, 0}, {W, 0}, true, -10, "", W, "W"));
    if (idx % 2 == 1) tol(p.dims.back(), 0.1);
    p.dims.push_back(lin(0, {0, 0}, {0, H}, false, -10, "", H, "H"));
    p.dims.push_back(lin(0, {e, H - e}, {W - e, H - e}, true, 10, "", W - 2 * e, "Px"));
    p.dims.push_back(lin(0, {W - e, e}, {W - e, H - e}, false, 10, "", H - 2 * e, "Py"));
    p.dims.push_back(radial(0, {W - e, H - e}, d / 2, 40, "4x Ø" + fmt(d), d, "Dh"));
    p.dims.push_back(lin(1, {W, 0}, {W, T}, false, 10, "", T, "T"));
  } else if (fam == "lbracket") {
    const double A = pick(rng, idx, 80, 50, 120, 5), B = pick(rng, idx, 60, 40, 100, 5), t = pick(rng, idx, 8, 5, 12);
    const double Wd = pick(rng, idx, 40, 25, 60, 5), d = pick(rng, idx, 10, 6, 12), hx = std::round((t + A) / 2 + 2);
    TopoDS_Shape s = fuse(box(0, 0, 0, A, Wd, t), box(0, 0, 0, t, Wd, B));
    s = cut(s, cyl(2, hx, Wd / 2, 0, d / 2, -1, t + 1));
    p.cyls.push_back({{hx, Wd / 2, 0}, 2, d / 2, 0, t});
    p.shape = s; p.views = {ViewRole::Front, ViewRole::Top, ViewRole::Left}; p.material = "S235JR"; p.strategy = Strategy::MultiFeature;
    p.dims.push_back(lin(0, {0, 0}, {A, 0}, true, -10, "", A, "A"));
    p.dims.push_back(lin(0, {0, 0}, {0, B}, false, -10, "", B, "B"));
    p.dims.push_back(lin(0, {A, 0}, {A, t}, false, 10, "", t, "T"));
    p.dims.push_back(lin(0, {0, B}, {t, B}, true, 10, "", t, "T2"));
    p.dims.push_back(radial(1, {hx, Wd / 2}, d / 2, 45, "Ø" + fmt(d), d, "Dh"));
    p.dims.push_back(lin(1, {0, Wd}, {hx, Wd / 2}, true, 10, "", hx, "Hx"));
    p.dims.push_back(lin(1, {0, 0}, {0, Wd}, false, -10, "", Wd, "Wd"));
    if (idx % 2 == 0) tol(p.dims.back(), 0.2);
  } else if (fam == "flange") {
    const double Df = pick(rng, idx, 120, 80, 160, 5), Tf = pick(rng, idx, 12, 8, 18), Dh = std::round(Df * pick(rng, idx, 0.5, 0.4, 0.55, 0.05));
    const double Lh = pick(rng, idx, 20, 10, 30), Db = std::round(Dh * 0.6), P = std::round((Df + Dh) / 2 / 2) * 2;
    const double dh = pick(rng, idx, 10, 8, 14);
    const int n = idx % 3 == 2 ? 6 : 4;
    TopoDS_Shape s = fuse(cyl(1, 0, 0, 0, Df / 2, 0, Tf), cyl(1, 0, 0, 0, Dh / 2, Tf, Tf + Lh));
    s = cut(s, cyl(1, 0, 0, 0, Db / 2, -1, Tf + Lh + 1));
    p.cyls.push_back({{0, 0, 0}, 1, Df / 2, 0, Tf + Lh});
    for (int k = 0; k < n; ++k) {
      const double a = 2 * kPi * k / n, x = P / 2 * std::cos(a), z = P / 2 * std::sin(a);
      s = cut(s, cyl(1, x, 0, z, dh / 2, -1, Tf + 1));
      p.cyls.push_back({{x, 0, z}, 1, dh / 2, 0, Tf});
    }
    p.shape = s; p.views = {ViewRole::Front, ViewRole::Top}; p.material = "C45"; p.strategy = Strategy::MultiFeature;
    p.dims.push_back(radial(0, {0, 0}, Df / 2, 135, "Ø" + fmt(Df), Df, "Df"));
    p.dims.push_back(radial(0, {P / 2, 0}, dh / 2, 30, std::to_string(n) + "x Ø" + fmt(dh), dh, "Dh"));
    p.dims.push_back(radial(0, {0, 0}, Db / 2, 225, "Ø" + fmt(Db), Db, "Db"));
    if (idx % 2 == 0) tol(p.dims.back(), 0.05);
    p.dims.push_back(lin(0, {-P / 2, 0}, {P / 2, 0}, true, 10, "Ø", P, "P", DimKind::Diameter));
    p.dims.push_back(lin(1, {Df / 2, 0}, {Df / 2, Tf}, false, 10, "", Tf, "Tf"));
    p.dims.push_back(lin(1, {Df / 2, 0}, {Dh / 2, Tf + Lh}, false, 20, "", Tf + Lh, "L"));
    p.dims.push_back(lin(1, {-Dh / 2, Tf + Lh}, {Dh / 2, Tf + Lh}, true, 10, "Ø", Dh, "Dhub", DimKind::Diameter));
  } else if (fam == "shaft") {
    const double D1 = pick(rng, idx, 30, 20, 40, 2), D2 = D1 + pick(rng, idx, 10, 4, 16, 2), D3 = D1 - pick(rng, idx, 8, 4, 10, 2);
    const double L1 = pick(rng, idx, 40, 25, 60, 5), L2 = pick(rng, idx, 30, 15, 40, 5), L3 = pick(rng, idx, 50, 30, 70, 5);
    const double c = idx % 2 == 0 ? 1.5 : 1, L = L1 + L2 + L3;
    TopoDS_Shape s = fuse(cyl(0, 0, 0, 0, D1 / 2, 0, L1), cyl(0, 0, 0, 0, D2 / 2, L1, L1 + L2));
    s = fuse(s, cyl(0, 0, 0, 0, D3 / 2, L1 + L2, L - c));
    s = fuse(s, coneX(L - c, D3 / 2, D3 / 2 - c, c));
    p.cyls.push_back({{0, 0, 0}, 0, D2 / 2, 0, L});
    p.shape = s; p.views = {ViewRole::Front, ViewRole::Left}; p.material = "42CrMo4"; p.strategy = Strategy::Revolve;
    p.dims.push_back(inside(0, {L1 / 2, -D1 / 2}, {L1 / 2, D1 / 2}, "Ø", D1, "D1"));
    if (idx % 2 == 0) tol(p.dims.back(), 0.02);
    p.dims.push_back(inside(0, {L1 + L2 / 2, -D2 / 2}, {L1 + L2 / 2, D2 / 2}, "Ø", D2, "D2"));
    p.dims.push_back(inside(0, {L1 + L2 + L3 / 2, -D3 / 2}, {L1 + L2 + L3 / 2, D3 / 2}, "Ø", D3, "D3"));
    p.dims.push_back(lin(0, {0, -D1 / 2}, {L1, -D2 / 2}, true, -10, "", L1, "L1"));
    p.dims.push_back(lin(0, {L1, -D2 / 2}, {L1 + L2, -D2 / 2}, true, -10, "", L2, "L2"));
    p.dims.push_back(lin(0, {L1 + L2, -D2 / 2}, {L, -D3 / 2 + c}, true, -10, "", L3, "L3"));
    p.dims.push_back(lin(0, {0, D1 / 2}, {L, D3 / 2 - c}, true, 10, "", L, "L"));
    p.dims.back().driving = false;
    DimSpec ch; ch.view = 0; ch.type = DimType::Leader; ch.c = {L - c / 2, D3 / 2 - c / 2}; ch.angDeg = 50; ch.kind = DimKind::Chamfer;
    ch.text = fmt(c) + "x45°"; ch.nominal = c; ch.param = "C"; p.dims.push_back(ch);
  } else if (fam == "bushing") {
    const double D = pick(rng, idx, 30, 20, 40, 2), Dc = D + pick(rng, idx, 12, 6, 16, 2), d = D - pick(rng, idx, 10, 6, 12, 2);
    const double L = pick(rng, idx, 40, 25, 60, 5), Lc = pick(rng, idx, 6, 4, 10);
    TopoDS_Shape s = fuse(cyl(0, 0, 0, 0, Dc / 2, 0, Lc), cyl(0, 0, 0, 0, D / 2, Lc, L));
    s = cut(s, cyl(0, 0, 0, 0, d / 2, -1, L + 1));
    p.cyls.push_back({{0, 0, 0}, 0, Dc / 2, 0, L});
    p.shape = s; p.views = {ViewRole::Front, ViewRole::Left}; p.material = "CuSn12"; p.strategy = Strategy::Revolve;
    p.dims.push_back(inside(0, {Lc / 2, -Dc / 2}, {Lc / 2, Dc / 2}, "Ø", Dc, "Dc"));
    p.dims.push_back(inside(0, {(Lc + L) / 2, -D / 2}, {(Lc + L) / 2, D / 2}, "Ø", D, "D"));
    tol(p.dims.back(), 0.05);
    p.dims.push_back(lin(0, {0, -Dc / 2}, {L, -D / 2}, true, -10, "", L, "L"));
    p.dims.push_back(lin(0, {0, Dc / 2}, {Lc, Dc / 2}, true, 10, "", Lc, "Lc"));
    p.dims.push_back(radial(1, {0, 0}, d / 2, 45, "Ø" + fmt(d), d, "d"));
  } else if (fam == "injector") {
    const double Dt = pick(rng, idx, 14, 10, 16, 2), s = Dt + pick(rng, idx, 4, 4, 8, 2), Dd = pick(rng, idx, 12, 10, 13, 1);
    const double Lt = pick(rng, idx, 10, 8, 14), Lx = pick(rng, idx, 7, 5, 9, 0.5), Ld = pick(rng, idx, 9, 6, 12), Lc = 3;
    const double db = std::round(Dt * 0.3 * 2) / 2, L = Lt + Lx + Ld + Lc;
    TopoDS_Shape sh = fuse(cyl(0, 0, 0, 0, Dt / 2, 0, Lt), hexX(Lt, Lx, s));
    sh = fuse(sh, cyl(0, 0, 0, 0, Dd / 2, Lt + Lx, Lt + Lx + Ld));
    sh = fuse(sh, coneX(Lt + Lx + Ld, Dd / 2, Dd / 4, Lc));
    sh = cut(sh, cyl(0, 0, 0, 0, db / 2, -1, L - 1.5));
    p.cyls.push_back({{0, 0, 0}, 0, s / std::sqrt(3.0), 0, L});
    p.shape = sh; p.views = {ViewRole::Front, ViewRole::Left}; p.material = "1.4005"; p.strategy = Strategy::MultiFeature;
    p.dims.push_back(inside(0, {Lt / 2, -Dt / 2}, {Lt / 2, Dt / 2}, "M", Dt, "Dt", DimKind::Thread));
    p.dims.back().text += "x1";
    p.dims.push_back(inside(0, {Lt + Lx + Ld / 2, -Dd / 2}, {Lt + Lx + Ld / 2, Dd / 2}, "Ø", Dd, "Dd"));
    p.dims.push_back(lin(0, {0, -Dt / 2}, {Lt, -s / 2}, true, -10, "", Lt, "Lt"));
    p.dims.push_back(lin(0, {Lt, -s / 2}, {Lt + Lx, -s / 2}, true, -10, "", Lx, "Lx"));
    tol(p.dims.back(), 0.5);
    p.dims.push_back(lin(0, {Lt + Lx, -s / 2}, {Lt + Lx + Ld, -Dd / 2}, true, -10, "", Ld, "Ld"));
    p.dims.push_back(lin(0, {0, Dt / 2}, {L, 0}, true, 10, "", L, "L"));
    tol(p.dims.back(), 0.2);
    const double e = s / std::sqrt(3.0) * 2;
    p.dims.push_back(lin(1, {e / 4, -s / 2}, {e / 4, s / 2}, false, 10, "", s, "SW"));
  } else {
    throw std::runtime_error("unknown family " + fam);
  }
  char nm[64]; std::snprintf(nm, sizeof nm, "%s_%02d", fam.c_str(), idx);
  p.name = nm;
  addParams(p);
  return p;
}
}  // namespace corpus
