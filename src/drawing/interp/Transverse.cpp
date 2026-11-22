// R3 — transverse view analysis (see Transverse.h).
#include "interp/Transverse.h"
#include "interp/InterpUtil.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace pc::drawing::interp {

namespace {

double norm360(double a) { a = std::fmod(a, 360.0); return a < 0 ? a + 360 : a; }
double angDiff(double a, double b) { double d = std::fabs(norm360(a - b)); return std::min(d, 360 - d); }

struct CircleGroup { Vec2 c; std::vector<std::pair<double, double>> rs; /* radius, coverage deg */ std::vector<int> ids; };

std::vector<CircleGroup> circleGroups(const PrimitiveSheet& s) {
  std::vector<CircleGroup> g;
  for (auto& p : s.prims) {
    if ((p.kind != PrimKind::Circle && p.kind != PrimKind::Arc) || p.radius < 0.5) continue;
    double span = p.kind == PrimKind::Circle ? 360 : std::fabs(p.a1 - p.a0);
    CircleGroup* t = nullptr;
    for (auto& x : g) if (distance(x.c, p.center) < std::max(0.1, 0.01 * p.radius)) { t = &x; break; }
    if (!t) { g.push_back({p.center, {}, {}}); t = &g.back(); }
    t->ids.push_back(p.id);
    bool found = false;
    for (auto& [r, cov] : t->rs) if (std::fabs(r - p.radius) < 0.01 * r) { cov += span; found = true; break; }
    if (!found) t->rs.push_back({p.radius, span});
  }
  for (auto& x : g) std::sort(x.rs.begin(), x.rs.end());
  return g;
}

struct Wall { const Primitive* p; Vec2 a, b; double ang, d, ra, rb, ua, ub; };   // a inner, b outer end
struct Pair { int i, j; std::string type; double e, w, ang, overlap; };

// part-mm scale of a view from Ø texts around it
double calibrate(const PrimitiveSheet& s, const CircleGroup& g, double Rmax, std::string& ev, std::vector<int>& used) {
  std::vector<std::pair<double, int>> cand;   // factor, text id
  std::vector<std::pair<double, int>> texts;  // D, id
  for (auto& t : s.texts) {
    if (distance(textCentre(t), g.c) > 3.2 * Rmax) continue;
    std::string c = canonNote(t.utf8);
    if (c.find('@') == std::string::npos) continue;
    DimTextContext ctx;
    ctx.nearCircleOrCylinder = true;
    DimText d = parseDim(t.utf8.substr(0, t.utf8.find(' ') == std::string::npos ? t.utf8.size() : t.utf8.find(' ')), ctx);
    if (!d.valid || d.kind != DimKind::Diameter || d.nominal <= 0) continue;
    texts.push_back({d.nominal, t.id});
    for (auto& [r, cov] : g.rs) cand.push_back({d.nominal / (2 * r), t.id});
  }
  double best = 0;
  int bestVotes = 0;
  for (auto& [f, id] : cand) {
    int votes = 0;
    for (auto& [D, tid] : texts)
      for (auto& [r, cov] : g.rs) if (std::fabs(D / (2 * r) - f) < 0.015 * f) { ++votes; break; }
    if (votes > bestVotes || (votes == bestVotes && f < best)) { bestVotes = votes; best = f; }
  }
  if (bestVotes >= 1) {
    for (auto& [D, tid] : texts)
      for (auto& [r, cov] : g.rs) if (std::fabs(D / (2 * r) - best) < 0.015 * best) { used.push_back(tid); break; }
    ev = "Ø ölçüleri (" + std::to_string(bestVotes) + " ölçü)";
    return best;
  }
  return 0;
}

std::vector<Wall> walls(const PrimitiveSheet& s, const CircleGroup& g, double Rmax) {
  std::vector<Wall> out;
  for (auto& p : s.prims) {
    if (p.kind != PrimKind::Line || p.fromDimBlock) continue;
    double r0 = distance(p.p0, g.c), r1 = distance(p.p1, g.c);
    if (std::max(r0, r1) > 1.08 * Rmax) continue;
    double L = distance(p.p0, p.p1);
    if (L < 0.04 * Rmax || std::fabs(r0 - r1) < 0.1 * L) continue;   // too short / chord-like
    Wall w{&p, r0 < r1 ? p.p0 : p.p1, r0 < r1 ? p.p1 : p.p0, 0, 0, std::min(r0, r1), std::max(r0, r1), 0, 0};
    Vec2 t = unit(w.b - w.a);
    w.ang = norm360(rad2deg(std::atan2(t.y, t.x)));
    w.d = cross(t, w.a - g.c);
    w.ua = dot(w.a - g.c, t);
    w.ub = dot(w.b - g.c, t);
    if (std::fabs(w.d) < 0.01 * Rmax) continue;   // radial (centre line, end cap)
    out.push_back(w);
  }
  return out;
}

std::string tagFor(const std::string& kind, const std::string& role, std::set<std::string>& usedTags) {
  std::string t = role == "primary" ? "pri" : role == "secondary" ? "sec" : kind == "groove" ? "grv" : "slt";
  std::string u = t;
  for (int k = 2; usedTags.count(u); ++k) u = t + std::to_string(k);
  usedTags.insert(u);
  return u;
}

// snap a measured value to the text value when they agree
double snap(double meas, double text, double tolAbs, SlotFamily& f, const char* what) {
  if (!has(text)) return meas;
  if (std::fabs(meas - text) <= tolAbs + 0.04 * text) return text;
  f.warnings.push_back(std::string(what) + ": çizim " + fmt(meas, 3) + " / not " + fmt(text, 3) + " -> not kullanıldı");
  return text;
}

}  // namespace

std::string summary(const SlotFamily& f) {
  std::ostringstream o;
  o << f.count << "x " << f.kind << " W" << fmt(f.width, 3);
  if (f.kind == "slot") o << " OFS" << fmt(f.offset, 3) << (f.senseView > 0 ? " CCW" : " CW");
  if (has(f.height)) o << " H" << fmt(f.height, 3);
  o << " pitch" << fmt(f.pitchDeg, 2) << " phase" << fmt(f.phaseDeg, 2);
  if (has(f.x0)) o << " x=" << fmt(f.x0, 3) << ".." << fmt(f.x1, 3);
  if (f.faceRelative) o << (f.frontFace ? " front-face" : " rear-face");
  if (f.alongBody) o << " full-length";
  return o.str();
}

TransverseResult analyzeTransverse(const PrimitiveSheet& s) {
  TransverseResult R;
  R.notes = parseNotes(s);
  auto groups = circleGroups(s);
  std::set<std::string> usedTags;
  for (auto& g : groups) {
    double Rmax = 0, cov = 0;
    for (auto& [r, c] : g.rs) if (r > Rmax) { Rmax = r; cov = c; }
    if (cov < 180) continue;
    auto W = walls(s, g, Rmax);
    // ---- candidate wall pairs
    std::vector<Pair> pairs;
    const double tol = 0.02 * Rmax, tolC = 0.008 * Rmax;   // pair plausibility / family clustering
    for (size_t i = 0; i < W.size(); ++i)
      for (size_t j = i + 1; j < W.size(); ++j) {
        if (angDiff(W[i].ang, W[j].ang) > 1.0) continue;
        double lo = std::max(W[i].ua, W[j].ua), hi = std::min(W[i].ub, W[j].ub);
        double ov = hi - lo, shorter = std::min(W[i].ub - W[i].ua, W[j].ub - W[j].ua);
        if (ov < 0.3 * shorter) continue;
        double di = W[i].d, dj = W[j].d, w = std::fabs(di - dj);
        if (w < tol || w > 0.6 * Rmax) continue;
        Pair P{(int)i, (int)j, "", 0, w, W[i].ang, ov};
        if (di * dj > 0) { P.type = "slot"; P.e = (di + dj) / 2; }
        else if (std::fabs(di + dj) < 0.1 * w) { P.type = "groove"; P.e = 0; }
        else continue;
        pairs.push_back(P);
      }
    // ---- cluster by (type, W, e) and test angular uniformity
    struct Cl { std::string type; double w, e; std::vector<Pair> ps; };
    std::vector<Cl> cls;
    for (auto& P : pairs) {
      Cl* t = nullptr;
      for (auto& c : cls) if (c.type == P.type && std::fabs(c.w - P.w) < tolC && std::fabs(c.e - P.e) < tolC) { t = &c; break; }
      if (!t) { cls.push_back({P.type, P.w, P.e, {}}); t = &cls.back(); }
      t->ps.push_back(P);
    }
    struct Fam { Cl* c; std::vector<Pair> reps; };
    std::vector<Fam> fams;
    for (auto& c : cls) {
      std::vector<Pair> reps;   // one per distinct angle (longest overlap)
      for (auto& P : c.ps) {
        bool merged = false;
        for (auto& r : reps)
          if (angDiff(r.ang, P.ang) < 1.5) { if (P.overlap > r.overlap) r = P; merged = true; break; }
        if (!merged) reps.push_back(P);
      }
      int N = (int)reps.size();
      if (N < 2) continue;
      // family size from the median over angles; per angle keep the pair closest to it
      std::vector<double> ws, es;
      for (auto& r : reps) { ws.push_back(r.w); es.push_back(r.e); }
      std::nth_element(ws.begin(), ws.begin() + N / 2, ws.end());
      std::nth_element(es.begin(), es.begin() + N / 2, es.end());
      c.w = ws[N / 2];
      c.e = es[N / 2];
      for (auto& r : reps)
        for (auto& P : c.ps)
          if (angDiff(r.ang, P.ang) < 1.5 && std::fabs(P.w - c.w) + std::fabs(P.e - c.e) < std::fabs(r.w - c.w) + std::fabs(r.e - c.e)) r = P;
      std::sort(reps.begin(), reps.end(), [](auto& a, auto& b) { return a.ang < b.ang; });
      // N >= 3 (two parallel opposite pairs are indistinguishable from section hatching), equal angular pitch,
      // and every instance spans the same radii (hatch segments of a rib do not)
      bool uniform = N >= 3;
      double ra0 = 1e300, ra1 = -1e300, rb0 = 1e300, rb1 = -1e300;
      for (int k = 0; k < N; ++k) {
        if (angDiff(reps[(k + 1) % N].ang - reps[k].ang, 360.0 / N) > 2.0) uniform = false;
        double ra = std::min(W[reps[k].i].ra, W[reps[k].j].ra), rb = std::max(W[reps[k].i].rb, W[reps[k].j].rb);
        ra0 = std::min(ra0, ra); ra1 = std::max(ra1, ra); rb0 = std::min(rb0, rb); rb1 = std::max(rb1, rb);
      }
      if (ra1 - ra0 > 0.05 * Rmax || rb1 - rb0 > 0.05 * Rmax) uniform = false;
      if (uniform) fams.push_back({&c, reps});
    }
    std::sort(fams.begin(), fams.end(), [](auto& a, auto& b) { return a.reps.size() > b.reps.size(); });
    std::set<int> usedPrims;
    std::vector<Fam> chosen;
    for (auto& F : fams) {
      bool clash = false;
      for (auto& P : F.reps) if (usedPrims.count(W[P.i].p->id) || usedPrims.count(W[P.j].p->id)) clash = true;
      if (clash) continue;
      for (auto& P : F.reps) { usedPrims.insert(W[P.i].p->id); usedPrims.insert(W[P.j].p->id); }
      chosen.push_back(F);
    }
    if (chosen.empty()) continue;
    // ---- view
    TransverseView V;
    V.centre = g.c;
    std::vector<int> calTexts;
    V.factor = calibrate(s, g, Rmax, V.scaleEvidence, calTexts);
    if (V.factor <= 0) {
      V.factor = R.notes.scaleNum > 0 ? R.notes.scaleDen / R.notes.scaleNum : 1.0;
      V.scaleEvidence = R.notes.scaleNum > 0 ? "not ölçeği" : "ölçek yok (1:1 varsayıldı)";
      R.warnings.push_back("Kesit görünüşü ölçeği Ø ölçülerinden bulunamadı: " + V.scaleEvidence);
    }
    const double f = V.factor;
    for (auto& [r, c] : g.rs) V.radii.push_back(r * f);
    // caption: closest caption below the circles
    const ViewCaption* cap = nullptr;
    double bestD = 1e300;
    for (auto& c : R.notes.captions) {
      double dy = g.c.y - c.pos.y;
      if (dy < Rmax || dy > 3.5 * Rmax || std::fabs(c.pos.x - g.c.x) > 2 * Rmax) continue;
      if (dy < bestD) { bestD = dy; cap = &c; }
    }
    if (cap) {
      V.letter = cap->letter;
      V.role = cap->role;
      V.section = cap->section;
      V.partNo = cap->partNo;
      if (const TextRun* t = findText(s, cap->textId)) V.caption = t->utf8;
    }
    V.conf = cap ? 0.9f : 0.6f;
    for (auto& F : chosen) {
      SlotFamily S;
      S.kind = F.c->type;
      S.role = S.kind == "slot" ? V.role : "";
      S.countGeom = (int)F.reps.size();
      S.count = S.countGeom;
      S.pitchDeg = 360.0 / S.count;
      S.phaseDeg = F.reps.front().ang;
      S.widthGeom = F.c->w * f;
      S.offsetGeom = std::fabs(F.c->e) * f;
      S.senseView = F.c->e > 0 ? 1 : -1;
      S.textIds = calTexts;
      double rIn = 1e300, rOut = 0, uInSum = 0;
      for (auto& P : F.reps) {
        const Wall &a = W[P.i], &b = W[P.j];
        rIn = std::min({rIn, a.ra, b.ra});
        rOut = std::max({rOut, a.rb, b.rb});
        uInSum += 0.5 * (a.ua + b.ua);
        S.primIds.push_back(a.p->id);
        S.primIds.push_back(b.p->id);
        if (S.kind == "slot" && std::fabs(a.ra - b.ra) < tol) S.throughWall = true;
      }
      S.uIn = uInSum / F.reps.size() * f;
      if (S.kind == "groove") {   // bottom = along-t inner wall end; OD = largest circle
        S.rIn = S.uIn;
        S.rOut = Rmax * f;
        S.height = S.rOut - S.rIn;
      } else {
        S.rIn = rIn * f;
        S.rOut = std::max(rOut, Rmax) * f;
        if (S.throughWall) {   // inner end on a circle of the group -> snap to that radius
          for (auto& [r, c] : g.rs) if (std::fabs(r - rIn) < tol) S.rIn = r * f;
        }
      }
      // ---- notes
      const FeatureHint* h = nullptr;
      for (auto& x : R.notes.features)
        if (x.kind == S.kind && !V.letter.empty() && x.viewLetter == V.letter) h = &x;
      if (!h) h = findHint(R.notes, S.kind, S.role);
      S.width = S.widthGeom;
      S.offset = S.offsetGeom;
      S.conf = 0.7f;
      if (h) {
        if (h->count && h->count != S.countGeom)
          S.warnings.push_back("adet: çizim " + std::to_string(S.countGeom) + " / not " + std::to_string(h->count));
        if (h->count) S.count = h->count;
        S.pitchDeg = 360.0 / S.count;
        if (has(h->pitchDeg) && std::fabs(h->pitchDeg - S.pitchDeg) > 0.5)
          S.warnings.push_back("eş aralık notu " + fmt(h->pitchDeg, 2) + "° ≠ 360/" + std::to_string(S.count));
        S.width = snap(S.widthGeom, h->width, 0.02 * Rmax * f, S, "genişlik");
        if (has(h->height)) S.height = S.kind == "groove" ? snap(S.height, h->height, 0.02 * Rmax * f, S, "derinlik") : h->height;
        if (has(h->offset)) S.offset = snap(S.offsetGeom, h->offset, 0.02 * Rmax * f, S, "eksen kaçıklığı");
        S.senseText = h->sense;
        S.x0 = h->x0;
        S.x1 = h->x1;
        S.textIds.insert(S.textIds.end(), h->textIds.begin(), h->textIds.end());
        S.conf = std::min(0.95f, 0.75f + 0.2f * h->conf);
        if (S.senseText && S.senseText != S.senseView)
          S.warnings.push_back("dönüş yönü: çizim " + std::string(S.senseView > 0 ? "CCW" : "CW") + " / not " +
                               (S.senseText > 0 ? "CCW" : "CW") + " -> not kullanıldı");
        if (S.senseText) S.senseView = S.senseText;
      }
      // tangency note: outer wall tangent to a named circle -> offset = D/2 - W/2
      if (S.kind == "slot" && !R.notes.tangentOuterWallTo.empty()) {
        for (auto& [name, D] : R.notes.namedDiameters)
          if (name.find(R.notes.tangentOuterWallTo) != std::string::npos || R.notes.tangentOuterWallTo.find(name) != std::string::npos) {
            S.tangentDia = D;
            S.tangentName = name;
            double o = D / 2 - S.width / 2;
            if (std::fabs(o - S.offsetGeom) > 0.04 * Rmax * f)
              S.warnings.push_back("teğetlik notu kaçıklık " + fmt(o, 3) + " ≠ çizim " + fmt(S.offsetGeom, 3));
            S.offset = o;
          }
      }
      S.tag = tagFor(S.kind, S.role, usedTags);
      V.families.push_back(S);
    }
    R.views.push_back(std::move(V));
  }
  if (R.views.empty()) return R;
  // ---- longitudinal view: lines of the visible-outline width (= slot wall width) in the same row, left of the
  //      leftmost transverse view
  double visW = 0;
  for (auto& V : R.views)
    for (auto& S : V.families)
      for (int id : S.primIds) if (const Primitive* p = findPrim(s, id)) visW = std::max(visW, p->style.width);
  double xMinT = 1e300, yC = R.views.front().centre.y, rowH = 0;
  for (auto& V : R.views) {
    double Rs = V.radii.back() / V.factor;
    xMinT = std::min(xMinT, V.centre.x - 1.2 * Rs);
    rowH = std::max(rowH, 1.6 * Rs);
  }
  Box2 lb = emptyBox();
  for (auto& p : s.prims) {
    if (p.kind != PrimKind::Line || p.style.width < 0.95 * visW || p.style.width > 1.5 * visW + 1e-9) continue;
    if (distance(p.p0, p.p1) > 0.6 * width(s.sheet)) continue;
    Vec2 m = (p.p0 + p.p1) * 0.5;
    if (m.x > xMinT || std::fabs(m.y - yC) > rowH) continue;
    expand(lb, p.p0);
    expand(lb, p.p1);
  }
  if (!isEmpty(lb)) R.faceXSheet = lb.lo.x;
  // ---- flow direction
  R.flowSign = 1;
  R.flowEvidence = "varsayım (+x)";
  if (R.notes.flowSign) { R.flowSign = R.notes.flowSign; R.flowEvidence = "not"; }
  else if (!isEmpty(lb))
    for (auto& t : s.texts)
      if (canonNote(t.utf8).find("ORIFICE") != std::string::npos && textCentre(t).y > lb.lo.y - 5 && textCentre(t).y < lb.hi.y + 5) {
        R.flowSign = textCentre(t).x < 0.5 * (lb.lo.x + lb.hi.x) ? -1 : 1;
        R.flowEvidence = "meme deliği konumu (ORIFICE)";
      }
  // ---- axial link
  for (auto& V : R.views) {
    if (!V.letter.empty() && has(R.faceXSheet)) {
      std::vector<double> xs;
      for (auto& t : s.texts) {
        if (trim(canonNote(t.utf8)) != V.letter) continue;
        Vec2 c = textCentre(t);
        bool inView = false;
        for (auto& U : R.views) if (distance(c, U.centre) < 1.3 * U.radii.back() / U.factor) inView = true;
        if (!inView && c.x > R.faceXSheet - 2 && c.x < xMinT) xs.push_back(c.x);
      }
      if (!xs.empty()) {
        double xm = 0;
        for (double x : xs) xm += x;
        V.xMarker = (xm / xs.size() - R.faceXSheet) * V.factor;
      }
    }
    std::string cap = canonNote(V.caption);
    bool face = cap.find("FACE") != std::string::npos;
    bool front = cap.find("REAR") == std::string::npos && cap.find("BACK") == std::string::npos;
    for (auto& S : V.families) {
      if (S.kind == "groove") { S.alongBody = true; S.axialEvidence = "oluk: gövde boyunca (OD besleme oluğu)"; continue; }
      if (face) {
        S.faceRelative = true;
        S.frontFace = front;
        if (!has(S.height)) S.height = S.width;
        S.axialEvidence = std::string(front ? "ön" : "arka") + " yüzden derinlik " + fmt(S.height, 3) + " (" + V.caption + ")";
        continue;
      }
      double tolX = 0.5 * (has(S.height) ? S.height : S.width);
      if (has(S.x0) && has(V.xMarker)) {
        bool in = V.xMarker > S.x0 - tolX && V.xMarker < S.x1 + tolX;
        S.axialEvidence = "not x=" + fmt(S.x0, 3) + ".." + fmt(S.x1, 3) + (in ? ", kesit işareti " : ", kesit işareti DIŞINDA ") +
                          V.letter + " x=" + fmt(V.xMarker, 2);
        if (!in) { S.warnings.push_back(S.axialEvidence); S.conf *= 0.8f; }
      } else if (has(S.x0)) {
        S.axialEvidence = "not x=" + fmt(S.x0, 3) + ".." + fmt(S.x1, 3);
      } else if (has(V.xMarker)) {
        double H = has(S.height) ? S.height : S.width;
        S.x0 = V.xMarker - H / 2;
        S.x1 = V.xMarker + H / 2;
        S.axialEvidence = "kesit işareti " + V.letter + " x=" + fmt(V.xMarker, 2) + " ± H/2 (varsayım)";
        S.conf *= 0.7f;
      } else {
        S.warnings.push_back("eksenel konum bulunamadı");
        S.conf *= 0.4f;
      }
      if (has(S.x0) && !has(S.height)) S.height = S.x1 - S.x0;
      if (has(S.x0) && has(S.height) && std::fabs((S.x1 - S.x0) - S.height) > 1e-6)
        S.warnings.push_back("x aralığı " + fmt(S.x1 - S.x0, 3) + " ≠ yükseklik notu " + fmt(S.height, 3));
    }
  }
  R.viewedFromDownstream = true;   // drawing convention of this family (notes: "viewed from downstream", "FRONT FACE")
  return R;
}

void attachTransverse(DrawingIR& ir, StageResult& r) {
  TransverseResult T = analyzeTransverse(ir.norm);
  for (auto& V : T.views)
    for (auto& S : V.families) {
      std::string s = (V.letter.empty() ? std::string("?") : V.letter) + ": " + summary(S);
      ir.views.attrs["r3.transverse." + S.tag] = s;
      r.diagnostics.push_back({Severity::Info, "r3.transverse", "Kesit görünüşü " + s, (int)Stage::Views, S.primIds});
      for (auto& w : S.warnings)
        r.diagnostics.push_back({Severity::Warning, "r3.transverse.mismatch", V.letter + "/" + S.tag + ": " + w, (int)Stage::Views, {}});
    }
  if (!T.views.empty()) ir.views.attrs["r3.transverse.flow"] = std::to_string(T.flowSign) + " (" + T.flowEvidence + ")";
}

}  // namespace pc::drawing::interp
