// SC-UX (WP-B) — PullMapping (pure logic). See PullMapping.h.
#include "PullMapping.h"
#include "SketchRegions.h"
#include "pc/Expression.h"
#include "pc/UpTo.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <QRegularExpression>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Trsf.hxx>
#include <algorithm>
#include <cmath>

using namespace pc;

namespace pcd::pull {

QString unsupportedFaceMessage() {
  return "Bu yüz için Çek henüz desteklenmiyor: yüzde sketch açmak için K, mevcut özelliği düzenlemek için çift tıklayın.";
}

bool isPlainNumber(const std::string& expr, double* v) {
  const QString s = QString::fromStdString(expr).trimmed();
  bool ok = false;
  const double d = s.toDouble(&ok);
  if (ok && v) *v = d;
  return ok;
}

int binding(const Document& doc, const std::string& expr, const QString& label, QString* param, QString* message) {
  std::optional<std::string> id;
  try { id = singleIdentifier(expr); } catch (...) {}
  if (!id) return 0;
  const Parameter* p = doc.params.find(*id);
  if (!p) return 0;
  if (isPlainNumber(p->expression)) {
    if (param) *param = QString::fromStdString(p->name);
    return 1;
  }
  if (message)
    *message = QString("%1 türetilmiş bir ifadeye bağlı (%2 = %3); Gruplar panelinden düzenleyin.")
                   .arg(label, QString::fromStdString(p->name), QString::fromStdString(p->expression));
  return 2;
}

namespace {
QString stripSplit(const QString& n) {
  static const QRegularExpression re(":(\\d+)$");
  QString s = n;
  return s.remove(re);
}
double evalOr(const std::string& e, const ParamValues& vals, double def) {
  try { return evalWith(e.empty() ? std::string("0") : e, vals); } catch (...) { return def; }
}
void setField(Target& t, const Document& doc, const model::ModelResult& model, const std::string& expr) {
  t.currentExpr = QString::fromStdString(expr);
  t.current = evalOr(expr, model.params, 0);
  QString p, m;
  const int b = binding(doc, expr, t.fieldLabel, &p, &m);
  if (b == 1) t.boundParam = p;
  if (b == 2) t.refusal = m;
}
QString fmt(double v) { return QString::fromStdString(formatNumber(v)); }

bool modelFace(const model::ModelResult& model, const QString& name, TopoDS_Face& out) {
  auto it = model.faces.find(name.toStdString());
  if (it == model.faces.end()) return false;
  out = it->second;
  return !out.IsNull();
}

// outward normal of a face at the middle of its UV box (orientation-aware; also fine for full cylinders, whose
// centroid lies on the axis)
bool outwardAt(const TopoDS_Face& f, gp_Pnt& p, gp_Dir& n) {
  try {
    double ua, ub, va, vb;
    BRepTools::UVBounds(f, ua, ub, va, vb);
    BRepAdaptor_Surface as(f);
    BRepLProp_SLProps pr(as, 0.5 * (ua + ub), 0.5 * (va + vb), 1, 1e-7);
    if (!pr.IsNormalDefined()) return false;
    p = pr.Value();
    n = pr.Normal();
    if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
    return true;
  } catch (...) {
    return false;
  }
}

bool isPlanarFace(const model::ModelResult& model, const QString& name) {
  TopoDS_Face f;
  if (!modelFace(model, name, f)) return false;
  try { return BRepAdaptor_Surface(f, Standard_False).GetType() == GeomAbs_Plane; } catch (...) { return false; }
}

// SpaceClaim rule: a planar face without a direct parametric mapping is pulled by extruding the face itself.
// Returns false when the face is not planar (the caller decides); otherwise `t` is a FaceExtrude target, possibly
// carrying a refusal (edge types the face sketch cannot hold).
bool faceExtrudeTarget(Target& t, const Document& doc, const model::ModelResult& model, const QString& face) {
  if (!isPlanarFace(model, face)) return false;
  t.kind = Kind::FaceExtrude;
  t.featureId.clear();
  t.prop = "distance";
  t.fieldLabel = "Mesafe";
  t.unit = "mm";
  t.delta = false;
  t.current = 0;
  t.currentExpr.clear();
  t.boundParam.clear();
  t.refusal.clear();
  t.face = face;
  t.describe = face;
  t.body = QString::fromStdString(model.bodyOfFace(face.toStdString()));
  QString err;
  if (!faceSketch(doc, model, face, nullptr, &err)) t.refusal = "Çek: " + err;
  return true;
}

// Cylindrical face of a revolve (profile line parallel to a U/V axis): its radial position is driven by
// DistanceX (axis V) / DistanceY (axis U) dimensions from the sketch origin to both line end points.
// Always returns true: `t` is either a RevolveRadius target or Unsupported with a clear message (never the angle).
bool revolveRadiusTarget(Target& t, const Document& doc, const model::ModelResult& model, const Feature& rev, const QString& rest) {
  auto refuse = [&](const QString& m) {
    t.kind = Kind::Unsupported;
    t.refusal = m;
    return true;
  };
  const QString keep = "döndürme açısı değiştirilmez; profil sketch'ini düzenleyin (çift tık).";
  TopoDS_Face F;
  if (!modelFace(model, t.face, F)) return refuse(unsupportedFaceMessage());
  gp_Cylinder cyl;
  try {
    BRepAdaptor_Surface s(F, Standard_False);
    if (s.GetType() != GeomAbs_Cylinder) return refuse("Çek: bu döndürme yüzü (koni / tor / eğri) için ofset henüz desteklenmiyor — " + keep);
    cyl = s.Cylinder();
  } catch (...) {
    return refuse(unsupportedFaceMessage());
  }
  static const QRegularExpression reRev("^rev\\.e(\\d+)$");
  const auto m = reRev.match(rest);
  const std::string sid = rev.prop("sketch");
  const Feature* sf = doc.feature(sid);
  auto solved = model.solvedSketches.find(sid);
  if (!m.hasMatch() || !sf || !sf->sketch || solved == model.solvedSketches.end()) return refuse("Çek: silindirik yüzün profil çizgisi bulunamadı — " + keep);
  const std::string axis = rev.prop("axis", "V");
  if (axis != "U" && axis != "u" && axis != "V" && axis != "v")
    return refuse("Çek: eksen bir sketch çizgisi olan döndürmede yarıçap ofseti desteklenmiyor — " + keep);
  const bool radialX = axis == "V" || axis == "v";
  const pc::SkEntity* e = solved->second.entity(m.captured(1).toInt());
  if (!e || e->type != pc::SkEntityType::Line || e->points.size() != 2) return refuse("Çek: silindirik yüzün profil çizgisi bulunamadı — " + keep);
  const SkConstraintType ct = radialX ? SkConstraintType::DistanceX : SkConstraintType::DistanceY;
  QStringList dims;
  for (int p : e->points) {
    int found = -1;
    for (const auto& c : sf->sketch->constraints)
      if (c.type == ct && c.refs.size() == 2 && ((c.refs[0] == 0 && c.refs[1] == p) || (c.refs[0] == p && c.refs[1] == 0))) { found = c.id; break; }
    if (found < 0)
      return refuse(QString("Çek: silindirik yüzün yarıçapı sketch orijininden bir %1 ölçüsüyle tanımlı değil (%2 çizgi e%3) — %4")
                        .arg(radialX ? "yatay (X)" : "dikey (Y)", QString::fromStdString(sid)).arg(e->id).arg(keep));
    if (!dims.contains(QString::number(found))) dims << QString::number(found);
  }
  gp_Pnt c;
  gp_Dir n;
  if (!outwardAt(F, c, n)) return refuse(unsupportedFaceMessage());
  const gp_Ax1 ax = cyl.Axis();
  gp_Vec radial(ax.Location(), c);
  radial -= gp_Vec(ax.Direction()) * radial.Dot(gp_Vec(ax.Direction()));
  t.kind = Kind::RevolveRadius;
  t.featureId = QString::fromStdString(sid);
  t.sketchId = t.featureId;
  t.prop = "radius";
  t.fieldLabel = "Mesafe";
  t.unit = "mm";
  t.delta = true;
  t.current = cyl.Radius();
  t.currentExpr = fmt(cyl.Radius());
  t.dims = dims;
  t.sign = radial.Magnitude() > 1e-12 && radial.Dot(gp_Vec(n)) < 0 ? -1 : 1;
  t.message = QString("Yarıçap ofseti: %1 ölçüsü (%2) değişir.").arg(QString::fromStdString(sid), dims.join(", "));
  return true;
}

// "D/2" + 3 -> "D/2 + 3"; "D/2 + 3" + 2 -> "D/2 + 5" (offsets fold instead of piling up)
std::string addOffset(const std::string& expr, double d) {
  static const QRegularExpression tail("^(.*\\S)\\s+([+-])\\s+([0-9]+(?:\\.[0-9]+)?)$");
  QString base = QString::fromStdString(expr).trimmed();
  double off = d;
  const auto m = tail.match(base);
  if (m.hasMatch()) {
    const QString head = m.captured(1);
    // fold only a trailing additive constant of the whole expression (no parentheses / products after it)
    bool ok = false;
    const double k = m.captured(3).toDouble(&ok);
    if (ok && !QString("+-*/^(,").contains(head.back())) {
      base = head;
      off += m.captured(2) == "-" ? -k : k;
    }
  }
  if (std::fabs(off) < 1e-12) return base.toStdString();
  return (base + (off < 0 ? " - " : " + ") + fmt(std::fabs(off))).toStdString();
}
} // namespace

bool faceSketch(const Document& doc, const model::ModelResult& model, const QString& face, Feature* out, QString* err,
                bool* normalInward, gp_Ax3* frameOut, const std::vector<TopoDS_Edge>* onlyEdges) {
  auto fail = [&](const QString& m) {
    if (err) *err = m;
    return false;
  };
  TopoDS_Face F;
  if (!modelFace(model, face, F)) return fail(QString("yüz bulunamadı (%1)").arg(face));
  if (!isPlanarFace(model, face)) return fail(QString("yüz düzlemsel değil (%1)").arg(face));
  Feature sf;
  sf.id = doc.uniqueFeatureId(FeatureType::Sketch);
  sf.type = FeatureType::Sketch;
  Sketch sk = Sketch::create();
  sk.plane.kind = SketchPlaneKind::OnFace;
  sk.plane.faceRef = face.toStdString();
  sk.plane.offsetExpr = "0";
  sf.sketch = sk;
  gp_Ax3 fr;
  {
    Document tmp = doc;
    tmp.features.push_back(sf);
    std::string e;
    if (!model::sketchFrame(tmp, sf.id, model.params, model, fr, &e)) return fail(QString::fromStdString(e));
  }
  if (frameOut) *frameOut = fr;
  // the OnFace frame takes the plane's axis as "outward"; for an indirect plane (e.g. planar faces of a Revolve) that
  // axis points INTO the material. Report it, so the Extrude direction can be chosen against the true outward normal.
  if (normalInward) {
    gp_Pnt p;
    gp_Dir n;
    *normalInward = outwardAt(F, p, n) && n.Dot(fr.Direction()) < 0;
  }
  gp_Trsf toLocal;
  toLocal.SetTransformation(fr);
  Sketch& s = *sf.sketch;
  auto local = [&](const gp_Pnt& p) { return p.Transformed(toLocal); };
  auto pid = [&](const gp_Pnt& p3) {
    const gp_Pnt p = local(p3);
    for (const auto& q : s.points)
      if (q.id != 0 && std::hypot(q.x - p.X(), q.y - p.Y()) < 1e-6) return q.id;
    return s.addPoint(p.X(), p.Y());
  };
  int n = 0;
  try {
    for (TopExp_Explorer wx(F, TopAbs_WIRE); wx.More(); wx.Next()) {
      for (TopExp_Explorer ex(wx.Current(), TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        if (BRep_Tool::Degenerated(e)) continue;
        if (onlyEdges && std::none_of(onlyEdges->begin(), onlyEdges->end(), [&](const TopoDS_Edge& o) { return o.IsSame(e); })) continue;
        BRepAdaptor_Curve cv(e);
        const double a = cv.FirstParameter(), b = cv.LastParameter();
        const gp_Pnt pa = cv.Value(a), pb = cv.Value(b);
        switch (cv.GetType()) {
          case GeomAbs_Line:
            if (pa.Distance(pb) < 1e-9) continue;
            s.addLine(pid(pa), pid(pb));
            break;
          case GeomAbs_Circle: {
            const gp_Circ ci = cv.Circle();
            const gp_Pnt cl = local(ci.Location());
            if (std::fabs(cl.Z()) > 1e-6 || std::fabs(std::fabs(ci.Axis().Direction().Dot(fr.Direction())) - 1) > 1e-6)
              return fail("yüz kenarındaki yay yüz düzleminde değil");
            if (pa.Distance(pb) < 1e-7) {
              s.addCircle(s.addPoint(cl.X(), cl.Y()), ci.Radius());
            } else {
              const bool ccw = ci.Axis().Direction().Dot(fr.Direction()) > 0;
              const int c = s.addPoint(cl.X(), cl.Y());
              s.addArc(c, ccw ? pid(pa) : pid(pb), ccw ? pid(pb) : pid(pa));
            }
            break;
          }
          default:
            return fail("yüz kenarlarında çizgi / yay / daire dışında eğri var (spline, elips); yüzden uzatma yapılamaz");
        }
        ++n;
      }
    }
  } catch (...) {
    return fail("yüz kenarları okunamadı");
  }
  if (n == 0) return fail("yüzün kenarı yok");
  if (out) *out = sf;
  return true;
}

pc::Document withNewFeatures(const Document& doc, const Result& r, int rollbackIndex) {
  Document cand = doc;
  int idx = rollbackIndex >= 0 ? rollbackIndex + 1 : int(cand.features.size());
  idx = std::min<int>(idx, int(cand.features.size()));
  if (r.hasPreFeature) cand.features.insert(cand.features.begin() + idx++, r.preFeature);
  if (r.isNewFeature) cand.features.insert(cand.features.begin() + idx++, r.feature);
  for (const auto& m : r.moreFeatures) cand.features.insert(cand.features.begin() + idx++, m);   // SC-PULLUPTO
  return cand;
}

Target analyze(const Document& doc, const model::ModelResult& model, const QList<SelItem>& sel, const Options& opt) {
  Target t;
  if (sel.isEmpty()) return t;
  // ---- edges -> new Fillet / Chamfer
  QStringList edges;
  for (const auto& it : sel)
    if (it.kind == SelItem::Kind::Edge && !it.name.startsWith("sketch:")) edges << it.name;
  if (!edges.isEmpty() && edges.size() == sel.size()) {
    const bool ch = opt.edge == "chamfer";
    t.kind = ch ? Kind::NewChamfer : Kind::NewFillet;
    t.refs = edges;
    t.fieldLabel = ch ? "Mesafe" : "Yarıçap";
    t.current = 0;
    t.describe = edges.size() == 1 ? edges.first() : QString("%1 kenar").arg(edges.size());
    return t;
  }
  const SelItem it = sel.first();
  std::string sid;
  char type = 0;
  int k = -1;
  // ---- sketch region / whole sketch -> new Extrude / Revolve
  if ((it.kind == SelItem::Kind::SketchRegion || it.kind == SelItem::Kind::Face) && parseSketchItemName(it.name, &sid, &type, &k) && type == 'r') {
  } else if (it.kind == SelItem::Kind::Sketch) {
    sid = it.name.toStdString();
    k = 0;
    type = 'r';
  }
  if (type == 'r' && !sid.empty()) {
    const Feature* sf = doc.feature(sid);
    if (!sf || sf->type != FeatureType::Sketch) { t.kind = Kind::Unsupported; t.refusal = "Sketch bulunamadı."; return t; }
    std::vector<TopoDS_Face> faces;
    gp_Ax3 fr;
    sketchRegionFaces(doc, model, sid, faces, fr);
    t.sketchId = QString::fromStdString(sid);
    t.featureId = t.sketchId;
    t.regionIndex = k;
    t.regionCount = int(faces.size());
    t.face = regionName(sid, std::max(0, k));
    t.describe = QString("bölge %1:r%2").arg(t.sketchId).arg(std::max(0, k));
    if (faces.empty()) {
      t.kind = Kind::Unsupported;
      sketch::RegionResult rr;
      gp_Ax3 f2;
      QString why;
      if (sketchRegionInfo(doc, model, sid, rr, f2) && !rr.openPoints.empty())
        why = QString(" %1 — Doldur (F) boşluğu gösterir.").arg(QString::fromStdString(rr.profile.message));
      t.refusal = "Çek: sketch'te kapalı bölge yok (açık kontur)." + why;
      return t;
    }
    // SC-PULLUPTO: every selected region (Ctrl+click), grouped per sketch; a whole-sketch item = all its regions
    for (const auto& s2 : sel) {
      std::string sid2;
      char ty2 = 0;
      int k2 = -1;
      QList<int> idx;
      if ((s2.kind == SelItem::Kind::SketchRegion || s2.kind == SelItem::Kind::Face) && parseSketchItemName(s2.name, &sid2, &ty2, &k2) && ty2 == 'r') {
        idx << k2;
      } else if (s2.kind == SelItem::Kind::Sketch) {
        sid2 = s2.name.toStdString();
        std::vector<TopoDS_Face> f3;
        gp_Ax3 fr3;
        sketchRegionFaces(doc, model, sid2, f3, fr3);
        for (int q = 0; q < int(f3.size()); ++q) idx << q;
      } else {
        continue;
      }
      const QString q = QString::fromStdString(sid2);
      bool merged = false;
      for (auto& rs : t.regionSets)
        if (rs.first == q) { for (int x : idx) if (!rs.second.contains(x)) rs.second << x; merged = true; }
      if (!merged) t.regionSets.append({q, idx});
    }
    if (t.regionSets.isEmpty()) t.regionSets.append({t.sketchId, {std::max(0, k)}});
    for (auto& rs : t.regionSets) std::sort(rs.second.begin(), rs.second.end());
    int nreg = 0;
    for (const auto& rs : t.regionSets) nreg += rs.second.size();
    if (nreg > 1)
      t.describe = t.regionSets.size() == 1 ? QString("%1 bölge (%2)").arg(nreg).arg(t.sketchId)
                                            : QString("%1 bölge, %2 sketch").arg(nreg).arg(t.regionSets.size());
    if (t.regionSets.size() > 1) t.message = "Her sketch kendi normali boyunca çekilir (sketch başına bir Extrude, tek geri alma adımı).";
    if (!opt.revolveAxis.isEmpty()) {
      t.kind = Kind::NewRevolve;
      t.fieldLabel = "Açı";
      t.unit = "°";
      t.current = 360;
    } else {
      t.kind = Kind::NewExtrude;
      t.fieldLabel = "Mesafe";
      t.current = 0;
    }
    return t;
  }
  if (it.kind == SelItem::Kind::Datum) {
    t.kind = Kind::MoveDatum;
    t.featureId = it.name;
    t.describe = it.name;
    t.refusal = "Düzlem Taşı (M) aracıyla ötelenir / döndürülür.";
    return t;
  }
  if (it.kind == SelItem::Kind::Body) {
    t.kind = Kind::Unsupported;
    t.refusal = "Çek bir gövdeye uygulanmaz: bir yüz, kenar ya da sketch bölgesi seçin.";
    return t;
  }
  if (it.kind == SelItem::Kind::SketchCurve) {
    t.kind = Kind::Unsupported;
    t.refusal = "Çek: önce bir sketch bölgesi seçin; döndürme ekseni için çizgiye Alt+tık.";
    return t;
  }
  if (it.kind != SelItem::Kind::Face) { t.kind = Kind::Unsupported; t.refusal = unsupportedFaceMessage(); return t; }
  if (sel.size() > 1) {
    // several faces: only allowed when they all map to the same field of one feature (e.g. two fillet faces)
    Target first = analyze(doc, model, {sel.first()}, opt);
    for (int i = 1; i < sel.size(); ++i) {
      const Target o = analyze(doc, model, {sel[i]}, opt);
      if (o.kind != first.kind || o.featureId != first.featureId || o.prop != first.prop || o.kind == Kind::FaceExtrude ||
          o.kind == Kind::RevolveRadius) {
        Target u;
        u.kind = Kind::Unsupported;
        u.refusal = "Çek: birden çok farklı yüz seçili; tek bir yüz seçin.";
        return u;
      }
    }
    first.describe = QString("%1 yüz").arg(sel.size());
    return first;
  }
  // ---- model faces
  const QString base = stripSplit(it.name);
  t.face = it.name;
  t.describe = it.name;
  const int dot = base.indexOf('.');
  const std::string fid = (dot > 0 ? base.left(dot) : base).toStdString();
  const QString rest = dot > 0 ? base.mid(dot + 1) : QString();
  const Feature* f = doc.feature(fid);
  if (!f) {
    if (faceExtrudeTarget(t, doc, model, it.name)) return t;
    t.kind = Kind::Unsupported;
    t.refusal = unsupportedFaceMessage();
    return t;
  }
  t.featureId = QString::fromStdString(fid);
  static const QRegularExpression reF("^f\\d+$"), reC("^c\\d+$"), reHole("^h\\d+\\.(wall|cb_wall|bottom|cb_floor|cs)$"),
      reSide("^side\\.e(\\d+)$");
  switch (f->type) {
    case FeatureType::Extrude: {
      if (rest == "end" && !f->prop("upTo").empty()) {   // SC-PULLUPTO: Up To extrude -> its offset
        t.kind = Kind::UpToOffset;
        t.prop = "upToOffset";
        t.fieldLabel = "Ofset";
        t.delta = true;
        t.sketchId = QString::fromStdString(f->prop("sketch"));
        setField(t, doc, model, f->prop("upToOffset", "0"));
        if (!t.refusal.isEmpty()) return t;
        t.message = QString("Kadar: %1 (ilişkisel); çekmek ofseti değiştirir.").arg(QString::fromStdString(model::upToLabel(f->prop("upTo"))));
        return t;
      }
      if (rest == "end") {
        t.kind = Kind::ExtrudeEnd;
        t.prop = "distance";
        t.fieldLabel = "Mesafe";
        t.delta = true;
        setField(t, doc, model, f->prop("distance", "10"));
        return t;
      }
      if (rest == "start") {
        const std::string dir = f->prop("direction", "normal");
        const std::string sk = f->prop("sketch");
        int users = 0;
        for (const auto& g : doc.features)
          if (!g.suppressed && g.id != f->id && g.prop("sketch") == sk) ++users;
        if (dir == "symmetric" || users > 0 || f->prop("through") == "1") {
          t.kind = Kind::Unsupported;
          t.refusal = "Çek: bu taban yüzü taşınamaz (sketch başka özellikçe kullanılıyor ya da simetrik/boydan boya).";
          return t;
        }
        t.kind = Kind::ExtrudeStart;
        t.prop = "distance";
        t.fieldLabel = "Mesafe";
        t.delta = true;
        t.sketchId = QString::fromStdString(sk);
        setField(t, doc, model, f->prop("distance", "10"));
        // the sketch offset must be editable too
        if (const Feature* s = doc.feature(sk); s && s->sketch) {
          QString pp, mm;
          if (binding(doc, s->sketch->plane.offsetExpr, "Düzlem ofseti", &pp, &mm) == 2 && t.refusal.isEmpty()) t.refusal = mm;
        }
        return t;
      }
      const auto m = reSide.match(rest);
      if (m.hasMatch()) {
        const int eid = m.captured(1).toInt();
        const Feature* s = doc.feature(f->prop("sketch"));
        if (s && s->sketch) {
          for (const auto& c : s->sketch->constraints) {
            if ((c.type == SkConstraintType::Diameter || c.type == SkConstraintType::Radius) && !c.refs.empty() && c.refs[0] == eid) {
              t.kind = Kind::SketchDim;
              t.featureId = QString::fromStdString(s->id);
              t.prop = QString::number(c.id);
              t.fieldLabel = c.type == SkConstraintType::Diameter ? "Çap" : "Yarıçap";
              setField(t, doc, model, c.valueExpr);
              return t;
            }
          }
        }
      }
      break;
    }
    case FeatureType::Revolve: {
      // SpaceClaim: only the sweep caps of a PARTIAL revolve change the angle. Profile faces ("rev.e<k>") are
      // offset: planar -> extrude the face, cylindrical -> radius dimensions; never the angle.
      const double ang = evalOr(f->prop("angle", "360"), model.params, 360);
      if ((rest == "end" || rest == "start") && std::fabs(ang) < 360.0 - 1e-9) {
        t.kind = Kind::RevolveAngle;
        t.prop = "angle";
        t.fieldLabel = "Açı";
        t.unit = "°";
        setField(t, doc, model, f->prop("angle", "360"));
        return t;
      }
      if (rest.startsWith("rev.")) {
        if (faceExtrudeTarget(t, doc, model, it.name)) return t;
        revolveRadiusTarget(t, doc, model, *f, rest);
        return t;
      }
      break;
    }
    case FeatureType::Fillet:
      if (reF.match(rest).hasMatch()) {
        t.kind = Kind::FilletRadius;
        t.prop = "radius";
        t.fieldLabel = "Yarıçap";
        setField(t, doc, model, f->prop("radius", "1"));
        return t;
      }
      break;
    case FeatureType::Chamfer:
      if (reC.match(rest).hasMatch()) {
        t.kind = Kind::ChamferDistance;
        t.prop = "distance";
        t.fieldLabel = "Mesafe";
        setField(t, doc, model, f->prop("distance", "1"));
        return t;
      }
      break;
    case FeatureType::Hole: {
      const auto m = reHole.match(rest);
      if (!m.hasMatch()) break;
      const QString part = m.captured(1);
      if (part == "wall") { t.kind = Kind::HoleDiameter; t.prop = "diameter"; t.fieldLabel = "Çap"; setField(t, doc, model, f->prop("diameter", "5")); return t; }
      if (part == "cb_wall") { t.kind = Kind::HoleDiameter; t.prop = "cbDiameter"; t.fieldLabel = "Çap"; setField(t, doc, model, f->prop("cbDiameter", "10")); return t; }
      if (part == "bottom") {
        if (f->prop("through") == "1") { t.kind = Kind::Unsupported; t.refusal = "Çek: boydan boya deliğin tabanı yok."; return t; }
        t.kind = Kind::HoleDepth;
        t.prop = "depth";
        t.fieldLabel = "Derinlik";
        setField(t, doc, model, f->prop("depth", "10"));
        return t;
      }
      if (part == "cb_floor") { t.kind = Kind::HoleDepth; t.prop = "cbDepth"; t.fieldLabel = "Derinlik"; setField(t, doc, model, f->prop("cbDepth", "3")); return t; }
      break;
    }
    default: break;
  }
  // no direct parametric mapping: a planar face is extruded (offset), anything else is refused (never reinterpreted)
  if (faceExtrudeTarget(t, doc, model, it.name)) return t;
  t.kind = Kind::Unsupported;
  t.refusal = unsupportedFaceMessage();
  return t;
}

namespace {
QString opFor(const Options& opt, const model::ModelResult& model, double v, QString* err) {
  const bool noBody = model.bodies.empty();
  if (opt.merge == "add") return noBody ? "new" : "join";
  if (opt.merge == "cut") { if (noBody) *err = "Çek (Kes): kesilecek gövde yok."; return "cut"; }
  if (opt.merge == "nomerge") return "new";
  if (opt.merge == "intersect") { if (noBody) *err = "Çek (Kesişim): gövde yok."; return "intersect"; }
  if (noBody) return "new";
  return v < 0 ? "cut" : "join";
}
void applyBodyOps(Feature& f, const QString& op, const Options& opt) {
  f.props["op"] = op.toStdString();
  if (op == "new" && !opt.bodyName.trimmed().isEmpty()) f.props["bodyName"] = opt.bodyName.trimmed().toStdString();
  if (op != "new" && !opt.targets.trimmed().isEmpty()) f.props["targets"] = opt.targets.trimmed().toStdString();
  if ((op == "cut" || op == "intersect") && opt.keepTool) f.props["keepTool"] = "1";
}
// writes the new value into a field following the binding rule; returns false + refusal for derived
bool writeField(Result& r, Document& cand, const Target& t, std::string& slot, double newV, const QString& typed, bool typedIsNumber) {
  if (!t.boundParam.isEmpty()) {
    r.paramName = t.boundParam;
    r.paramExpr = fmt(newV);
    r.status = QString("%1 = %2 (parametre güncellendi)").arg(t.boundParam, fmt(newV));
    return true;
  }
  slot = (!typedIsNumber && !t.delta) ? typed.trimmed().toStdString() : formatNumber(newV);
  r.doc = cand;
  return true;
}
} // namespace

Result apply(const Document& doc, const model::ModelResult& model, const Target& t, const QString& value, bool absolute,
             const Options& opt, const Evaluator& eval) {
  Result r;
  if (!t.valid()) {
    r.refusal = !t.refusal.isEmpty() ? t.refusal : t.kind == Kind::None ? QString("Çek: önce bir yüz, kenar ya da sketch bölgesi seçin.") : unsupportedFaceMessage();
    return r;
  }
  double v = 0;
  if (!eval || !eval(value, &v) || !std::isfinite(v)) { r.refusal = "Geçersiz ifade: " + value; return r; }
  const bool num = isPlainNumber(value.toStdString());
  QString err;
  switch (t.kind) {
    case Kind::NewExtrude: {
      if (std::fabs(v) < 1e-9) { r.refusal = "Çek: mesafe sıfır olamaz."; return r; }
      Feature f;
      f.id = doc.uniqueFeatureId(FeatureType::Extrude);
      f.type = FeatureType::Extrude;
      f.props["sketch"] = t.sketchId.toStdString();
      QString dist = num ? fmt(std::fabs(v)) : value.trimmed();
      if (!num && v < 0) dist = dist.startsWith('-') ? dist.mid(1).trimmed() : fmt(std::fabs(v));
      f.props["distance"] = dist.toStdString();
      f.props["direction"] = opt.bothSides ? "symmetric" : (v < 0 ? "reverse" : "normal");
      const QString op = opFor(opt, model, v, &err);
      if (!err.isEmpty()) { r.refusal = err; return r; }
      // integration: "Kes (Cut)" chosen in the Options panel with a positive value on a sketch lying ON a face of the
      // solid cuts into the material (SpaceClaim: the cut goes into the body), not into the air above the face
      if (opt.merge == "cut" && v > 0 && !opt.bothSides) {
        const Feature* sf = doc.feature(t.sketchId.toStdString());
        if (sf && sf->sketch && sf->sketch->plane.kind == SketchPlaneKind::OnFace) f.props["direction"] = "reverse";
      }
      applyBodyOps(f, op, opt);
      if (!opt.draft.trimmed().isEmpty() && opt.draft.trimmed() != "0") f.props["draft"] = opt.draft.trimmed().toStdString();
      // SC-PULLUPTO: only the selected regions; further sketches -> one Extrude each (same value, same options)
      Document ids = doc;
      QStringList made;
      for (int si = 0; si < t.regionSets.size(); ++si) {
        Feature g = f;
        g.props["sketch"] = t.regionSets[si].first.toStdString();
        const QString rp = regionsProp(doc, model, t.regionSets[si].first, t.regionSets[si].second);
        if (rp.isEmpty()) g.props.erase("regions"); else g.props["regions"] = rp.toStdString();
        if (si > 0) {
          g.id = ids.uniqueFeatureId(FeatureType::Extrude);
          if (opt.merge == "cut" && v > 0 && !opt.bothSides) {
            const Feature* sf2 = doc.feature(g.props["sketch"]);
            g.props["direction"] = sf2 && sf2->sketch && sf2->sketch->plane.kind == SketchPlaneKind::OnFace ? "reverse" : "normal";
          }
          r.moreFeatures << g;
        } else {
          f = g;
        }
        ids.features.push_back(g);
        made << QString::fromStdString(g.id);
      }
      r.ok = true;
      r.isNewFeature = true;
      r.feature = f;
      r.featureId = QString::fromStdString(f.id);
      r.prop = "distance";
      r.valueExpr = dist;
      r.undoText = QString("Çek: %1 = %2").arg(made.join("+"), dist);
      r.resultFace = r.featureId + ".end";
      r.status = QString("Çek: %1 oluşturuldu (%2, mesafe %3%4).").arg(made.join(", "), op, v < 0 ? "-" : "", dist);
      return r;
    }
    case Kind::NewRevolve: {
      if (std::fabs(v) < 1e-9) { r.refusal = "Çek: açı sıfır olamaz."; return r; }
      Feature f;
      f.id = doc.uniqueFeatureId(FeatureType::Revolve);
      f.type = FeatureType::Revolve;
      f.props["sketch"] = t.sketchId.toStdString();
      f.props["axis"] = opt.revolveAxis.toStdString();
      if (!t.regionSets.isEmpty()) {   // SC-PULLUPTO: the selected regions of the axis' sketch
        const QString rp = regionsProp(doc, model, t.sketchId, t.regionSets.first().second);
        if (!rp.isEmpty()) f.props["regions"] = rp.toStdString();
      }
      const QString ang = num ? fmt(std::min(360.0, std::fabs(v))) : value.trimmed();
      f.props["angle"] = ang.toStdString();
      const QString op = opFor(opt, model, v, &err);
      if (!err.isEmpty()) { r.refusal = err; return r; }
      applyBodyOps(f, op, opt);
      r.ok = true;
      r.isNewFeature = true;
      r.feature = f;
      r.featureId = QString::fromStdString(f.id);
      r.prop = "angle";
      r.valueExpr = ang;
      r.undoText = QString("Çek: %1 = %2°").arg(r.featureId, ang);
      r.status = QString("Çek: %1 oluşturuldu (eksen %2, açı %3°).").arg(r.featureId, opt.revolveAxis, ang);
      return r;
    }
    case Kind::NewFillet:
    case Kind::NewChamfer: {
      if (v <= 1e-9) { r.refusal = QString("Çek: %1 pozitif olmalı.").arg(t.fieldLabel); return r; }
      const bool ch = t.kind == Kind::NewChamfer;
      Feature f;
      f.id = doc.uniqueFeatureId(ch ? FeatureType::Chamfer : FeatureType::Fillet);
      f.type = ch ? FeatureType::Chamfer : FeatureType::Fillet;
      for (const QString& e : t.refs) f.refs.push_back(e.toStdString());
      const QString val = num ? fmt(v) : value.trimmed();
      f.props[ch ? "distance" : "radius"] = val.toStdString();
      r.ok = true;
      r.isNewFeature = true;
      r.feature = f;
      r.featureId = QString::fromStdString(f.id);
      r.prop = ch ? "distance" : "radius";
      r.valueExpr = val;
      r.undoText = QString("Çek: %1 = %2").arg(r.featureId, val);
      r.status = QString("Çek: %1 (%2 kenar, %3 %4).").arg(r.featureId).arg(t.refs.size()).arg(t.fieldLabel.toLower(), val);
      return r;
    }
    case Kind::ExtrudeEnd:
    case Kind::FilletRadius:
    case Kind::ChamferDistance:
    case Kind::HoleDiameter:
    case Kind::HoleDepth:
    case Kind::RevolveAngle: {
      const double newV = (t.delta && !absolute) ? t.current + v : v;
      if (newV <= 1e-9) { r.refusal = QString("Çek: %1 sıfır veya negatif olamaz (%2).").arg(t.fieldLabel, fmt(newV)); return r; }
      Document cand = doc;
      Feature* f = cand.feature(t.featureId.toStdString());
      if (!f) { r.refusal = "Özellik bulunamadı."; return r; }
      std::string& slot = f->props[t.prop.toStdString()];
      writeField(r, cand, t, slot, newV, value, num || (t.delta && !absolute));
      if (r.paramName.isEmpty()) r.doc = cand;
      r.ok = true;
      r.featureId = t.featureId;
      r.prop = t.prop;
      r.valueExpr = r.paramName.isEmpty() ? QString::fromStdString(slot) : r.paramName;
      r.undoText = QString("Çek: %1.%2 = %3").arg(t.featureId, t.prop, r.paramName.isEmpty() ? r.valueExpr : r.paramExpr);
      r.resultFace = t.face;
      if (r.status.isEmpty()) r.status = QString("Çek: %1 %2 = %3").arg(t.featureId, t.fieldLabel.toLower(), r.valueExpr);
      return r;
    }
    case Kind::ExtrudeStart: {
      const double d = absolute ? v - t.current : v;
      const double newDist = t.current + d;
      if (newDist <= 1e-9) { r.refusal = QString("Çek: mesafe sıfır veya negatif olamaz (%1).").arg(fmt(newDist)); return r; }
      Document cand = doc;
      Feature* f = cand.feature(t.featureId.toStdString());
      Feature* s = cand.feature(t.sketchId.toStdString());
      if (!f || !s || !s->sketch) { r.refusal = "Özellik bulunamadı."; return r; }
      const bool rev = f->prop("direction", "normal") == "reverse";
      std::string& off = s->sketch->plane.offsetExpr;
      const double o = evalOr(off, model.params, 0);
      const double newO = rev ? o + d : o - d;
      QString pp;
      if (binding(cand, off, "Düzlem ofseti", &pp, nullptr) == 1) cand.params.setExpression(pp.toStdString(), formatNumber(newO));
      else off = formatNumber(newO);
      if (!t.boundParam.isEmpty()) cand.params.setExpression(t.boundParam.toStdString(), formatNumber(newDist));
      else f->props["distance"] = formatNumber(newDist);
      r.ok = true;
      r.doc = cand;
      r.featureId = t.featureId;
      r.prop = "distance";
      r.valueExpr = t.boundParam.isEmpty() ? fmt(newDist) : t.boundParam;
      r.undoText = QString("Çek: %1 tabanı %2 mm").arg(t.featureId, fmt(d));
      r.resultFace = t.face;
      r.status = QString("Çek: %1 mesafe = %2, sketch ofseti = %3").arg(t.featureId, fmt(newDist), fmt(newO));
      return r;
    }
    case Kind::FaceExtrude: {
      if (std::fabs(v) < 1e-9) { r.refusal = "Çek: mesafe sıfır olamaz."; return r; }
      Feature sk;
      QString ferr;
      bool inward = false;
      if (!faceSketch(doc, model, t.face, &sk, &ferr, &inward)) { r.refusal = "Çek: " + ferr; return r; }
      // Extrude "normal" follows the sketch frame normal; the pull is along the face's OUTWARD normal
      const char* outDir = inward ? "reverse" : "normal";
      const char* inDir = inward ? "normal" : "reverse";
      Feature f;
      f.id = doc.uniqueFeatureId(FeatureType::Extrude);
      f.type = FeatureType::Extrude;
      f.props["sketch"] = sk.id;
      QString dist = num ? fmt(std::fabs(v)) : value.trimmed();
      if (!num && v < 0) dist = dist.startsWith('-') ? dist.mid(1).trimmed() : fmt(std::fabs(v));
      f.props["distance"] = dist.toStdString();
      f.props["direction"] = opt.bothSides ? "symmetric" : (v < 0 ? inDir : outDir);
      const QString op = opFor(opt, model, v, &err);
      if (!err.isEmpty()) { r.refusal = err; return r; }
      // "Kes" with a positive value cuts into the material below the face (as for a sketch on a face)
      if (opt.merge == "cut" && v > 0 && !opt.bothSides) f.props["direction"] = inDir;
      applyBodyOps(f, op, opt);
      // Add / Cut act on the body that owns the face (SpaceClaim), not on whatever else the tool touches
      if (op != "new" && opt.targets.trimmed().isEmpty() && !t.body.isEmpty()) f.props["targets"] = t.body.toStdString();
      f.props["pullFace"] = t.face.toStdString();   // model: the new end face inherits the pulled face's names
      if (!opt.draft.trimmed().isEmpty() && opt.draft.trimmed() != "0") f.props["draft"] = opt.draft.trimmed().toStdString();
      r.ok = true;
      r.isNewFeature = true;
      r.hasPreFeature = true;
      r.preFeature = sk;
      r.feature = f;
      r.featureId = QString::fromStdString(f.id);
      r.prop = "distance";
      r.valueExpr = dist;
      r.undoText = QString("Çek: %1 yüzü %2%3 (%4)").arg(t.face, v < 0 ? "-" : "", dist, r.featureId);
      r.resultFace = r.featureId + ".end";
      const QString opTr = op == "join" ? "ekle" : op == "cut" ? "kes" : op == "new" ? "yeni gövde" : op;
      r.status = QString("Çek: %1 yüzü %2%3 mm çekildi → %4 + %5 (%6).")
                     .arg(t.face, v < 0 ? "-" : "", dist, QString::fromStdString(sk.id), r.featureId, opTr);
      return r;
    }
    case Kind::RevolveRadius: {
      const double dr = absolute ? v - t.current : t.sign * v;
      const double newR = t.current + dr;
      if (newR <= 1e-9) { r.refusal = QString("Çek: yarıçap sıfır veya negatif olamaz (%1).").arg(fmt(newR)); return r; }
      Document cand = doc;
      Feature* s = cand.feature(t.featureId.toStdString());
      if (!s || !s->sketch) { r.refusal = "Sketch bulunamadı."; return r; }
      std::vector<SkConstraint*> cs;
      for (auto& c : s->sketch->constraints)
        if (t.dims.contains(QString::number(c.id))) cs.push_back(&c);
      if (cs.empty()) { r.refusal = "Ölçü bulunamadı."; return r; }
      // one plain-number parameter behind every dimension: change the parameter (binding rule)
      QString p0;
      bool allSameParam = true;
      for (auto* c : cs) {
        QString p;
        if (binding(cand, c->valueExpr, "Yarıçap", &p, nullptr) != 1 || (!p0.isEmpty() && p != p0)) { allSameParam = false; break; }
        p0 = p;
      }
      QStringList exprs;
      if (allSameParam && !p0.isEmpty()) {
        const double pv = evalOr(cs.front()->valueExpr, model.params, 0);
        r.paramName = p0;
        r.paramExpr = fmt(pv + (pv < 0 ? -dr : dr));
        r.status = QString("%1 = %2 (parametre güncellendi)").arg(p0, r.paramExpr);
      } else {
        for (auto* c : cs) {
          const double cv = evalOr(c->valueExpr, model.params, 0);
          const double d = cv < 0 ? -dr : dr;
          c->valueExpr = isPlainNumber(c->valueExpr) ? formatNumber(cv + d) : addOffset(c->valueExpr, d);
          exprs << QString::fromStdString(c->valueExpr);
        }
        r.doc = cand;
      }
      r.ok = true;
      r.featureId = t.featureId;
      r.prop = t.dims.first();
      r.valueExpr = r.paramName.isEmpty() ? exprs.first() : r.paramName;
      r.undoText = QString("Çek: %1 yarıçap %2 → %3").arg(t.face, fmt(t.current), fmt(newR));
      r.resultFace = t.face;
      if (r.status.isEmpty()) r.status = QString("Çek: %1 yarıçapı %2 → %3 mm (%4: %5)").arg(t.face, fmt(t.current), fmt(newR), t.featureId, exprs.join(", "));
      return r;
    }
    case Kind::UpToOffset: {   // SC-PULLUPTO
      const double newV = absolute ? v : t.current + v;
      Document cand = doc;
      Feature* f = cand.feature(t.featureId.toStdString());
      if (!f) { r.refusal = "Özellik bulunamadı."; return r; }
      std::string& slot = f->props["upToOffset"];
      if (!t.boundParam.isEmpty()) {
        r.paramName = t.boundParam;
        r.paramExpr = fmt(newV);
      } else {
        slot = formatNumber(newV);
        r.doc = cand;
      }
      r.ok = true;
      r.featureId = t.featureId;
      r.prop = t.prop;
      r.valueExpr = r.paramName.isEmpty() ? QString::fromStdString(slot) : r.paramName;
      r.undoText = QString("Çek: %1 Kadar ofseti = %2").arg(t.featureId, fmt(newV));
      r.resultFace = t.face;
      r.status = QString("Çek: %1 Kadar ofseti %2 mm (hedef ilişkisel kalır).").arg(t.featureId, fmt(newV));
      return r;
    }
    case Kind::SketchDim: {
      if (v <= 1e-9) { r.refusal = QString("Çek: %1 pozitif olmalı.").arg(t.fieldLabel); return r; }
      Document cand = doc;
      Feature* s = cand.feature(t.featureId.toStdString());
      if (!s || !s->sketch) { r.refusal = "Sketch bulunamadı."; return r; }
      for (auto& c : s->sketch->constraints) {
        if (QString::number(c.id) != t.prop) continue;
        writeField(r, cand, t, c.valueExpr, v, value, num);
        if (r.paramName.isEmpty()) r.doc = cand;
        r.ok = true;
        r.featureId = t.featureId;
        r.prop = t.prop;
        r.valueExpr = r.paramName.isEmpty() ? QString::fromStdString(c.valueExpr) : r.paramName;
        r.undoText = QString("Çek: %1 %2 = %3").arg(t.featureId, t.fieldLabel.toLower(), r.paramName.isEmpty() ? r.valueExpr : r.paramExpr);
        r.resultFace = t.face;
        if (r.status.isEmpty()) r.status = QString("Çek: %1 %2 = %3").arg(t.featureId, t.fieldLabel.toLower(), r.valueExpr);
        return r;
      }
      r.refusal = "Ölçü bulunamadı.";
      return r;
    }
    default: break;
  }
  r.refusal = unsupportedFaceMessage();
  return r;
}

// ================================================================ SC-PULLUPTO: Up To / Ruler
QString regionsProp(const Document& doc, const model::ModelResult& model, const QString& sid, const QList<int>& indices) {
  sketch::RegionResult rr;
  gp_Ax3 fr;
  QList<int> idx = indices;
  std::sort(idx.begin(), idx.end());
  idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
  if (sketchRegionInfo(doc, model, sid.toStdString(), rr, fr) && rr.legacy && idx.size() == int(rr.profile.faces.size())) {
    bool all = true;
    for (int i = 0; i < idx.size(); ++i) all = all && idx[i] == i;
    if (all) return QString();
  }
  QStringList out;
  for (int i : idx) out << QString::number(i);
  return out.join(",");
}

bool supportsUpTo(const Target& t) {
  return t.valid() && (t.kind == Kind::NewExtrude || t.kind == Kind::FaceExtrude || t.kind == Kind::ExtrudeEnd || t.kind == Kind::UpToOffset);
}

namespace {
// frame of the plane the target is extruded from
bool targetFrame(const Document& doc, const model::ModelResult& model, const Target& t, gp_Ax3* fr, bool* inward, QString* err) {
  *inward = false;
  if (t.kind == Kind::NewExtrude) {
    if (!sketchFrameOf(model, t.sketchId.toStdString(), *fr)) { *err = "sketch çerçevesi yok"; return false; }
    return true;
  }
  if (t.kind == Kind::FaceExtrude) {
    QString e;
    if (!faceSketch(doc, model, t.face, nullptr, &e, inward, fr)) { *err = e; return false; }
    return true;
  }
  if (t.kind == Kind::ExtrudeEnd || t.kind == Kind::UpToOffset) {
    const Feature* f = doc.feature(t.featureId.toStdString());
    if (!f || !sketchFrameOf(model, f->prop("sketch"), *fr)) { *err = "Extrude'un sketch çerçevesi yok"; return false; }
    return true;
  }
  *err = "Kadar bu seçim için desteklenmiyor (bölge, düzlemsel yüz ya da Extrude uç yüzü seçin)";
  return false;
}
bool refersToSelf(const Target& t, const QString& ref) {
  const QString s = QString::fromStdString(model::upToSketchOf(ref.toStdString()));
  if (s.isEmpty()) return ref == t.face;
  for (const auto& rs : t.regionSets) if (rs.first == s) return true;
  if (t.kind == Kind::ExtrudeEnd || t.kind == Kind::UpToOffset) return s == t.sketchId;
  return false;
}
} // namespace

bool upToPreview(const Document& doc, const model::ModelResult& model, const Target& t, const QString& ref, double* s, QString* err,
                 double* outward) {
  QString e;
  gp_Ax3 fr;
  bool inward = false;
  if (!supportsUpTo(t)) { if (err) *err = "Kadar bu seçim için desteklenmiyor (bölge, düzlemsel yüz ya da Extrude uç yüzü seçin)."; return false; }
  if (refersToSelf(t, ref)) { if (err) *err = "Kadar hedefi, çekilen öğenin kendisi (ya da kendi sketch'i) olamaz."; return false; }
  if (!targetFrame(doc, model, t, &fr, &inward, &e)) { if (err) *err = e; return false; }
  std::string me, w;
  if (!model::upToDistance(model, fr, ref.toStdString(), s, &me, &w)) { if (err) *err = QString::fromStdString(me); return false; }
  if (std::fabs(*s) < 1e-9) { if (err) *err = "Kadar hedefi çekilen düzlemin üzerinde (uzaklık 0)."; return false; }
  if (outward) *outward = inward ? -*s : *s;
  if (err) *err = QString::fromStdString(w);
  return true;
}

Result applyUpTo(const Document& doc, const model::ModelResult& model, const Target& t, const QString& ref, const QString& offsetExpr,
                 const Options& opt, const Evaluator& eval) {
  Result r;
  double s = 0, sOut = 0;
  QString err;
  if (!upToPreview(doc, model, t, ref, &s, &err, &sOut)) { r.refusal = "Çek (Kadar): " + err; return r; }
  QString off = offsetExpr.trimmed();
  double offV = 0;
  if (!off.isEmpty()) {
    if (!eval || !eval(off, &offV) || !std::isfinite(offV)) { r.refusal = "Geçersiz ifade: " + off; return r; }
    if (isPlainNumber(off.toStdString())) off = fmt(offV);
  }
  if (std::fabs(offV) < 1e-12 && isPlainNumber(off.toStdString())) off.clear();
  const double d = std::fabs(s) + offV;
  if (d <= 1e-9) { r.refusal = QString("Çek (Kadar): hedefe uzaklık %1 mm, ofsetle %2 mm — pozitif olmalı.").arg(fmt(std::fabs(s)), fmt(d)); return r; }
  const QString label = QString::fromStdString(model::upToLabel(ref.toStdString()));
  const QString offTxt = off.isEmpty() ? QString() : QString(", ofset %1").arg(off);
  auto setUpTo = [&](Feature& f) {
    f.props["upTo"] = ref.toStdString();
    if (off.isEmpty()) f.props.erase("upToOffset"); else f.props["upToOffset"] = off.toStdString();
    f.props["distance"] = formatNumber(d);   // informative (Kadar wins while upTo is set)
    if (f.prop("direction") != "symmetric") f.props["direction"] = s > 0 ? "normal" : "reverse";
    f.props.erase("through");
  };
  QString errOp;
  switch (t.kind) {
    case Kind::NewExtrude: {
      Document ids = doc;
      QStringList made;
      QList<Feature> fs;
      for (int si = 0; si < t.regionSets.size(); ++si) {
        Target one = t;
        one.sketchId = t.regionSets[si].first;
        double s1 = s;
        if (si > 0) {
          QString e1;
          one.regionSets = {t.regionSets[si]};
          if (!upToPreview(doc, model, one, ref, &s1, &e1)) { r.refusal = QString("Çek (Kadar): %1: %2").arg(one.sketchId, e1); return r; }
        }
        Feature f;
        f.id = ids.uniqueFeatureId(FeatureType::Extrude);
        f.type = FeatureType::Extrude;
        f.props["sketch"] = one.sketchId.toStdString();
        const QString rp = regionsProp(doc, model, one.sketchId, t.regionSets[si].second);
        if (!rp.isEmpty()) f.props["regions"] = rp.toStdString();
        if (opt.bothSides) f.props["direction"] = "symmetric";
        f.props["upTo"] = ref.toStdString();
        if (!off.isEmpty()) f.props["upToOffset"] = off.toStdString();
        f.props["distance"] = formatNumber(std::fabs(s1) + offV);
        if (!opt.bothSides) f.props["direction"] = s1 > 0 ? "normal" : "reverse";
        const QString op = opFor(opt, model, +1.0, &errOp);
        if (!errOp.isEmpty()) { r.refusal = errOp; return r; }
        applyBodyOps(f, op, opt);
        if (!opt.draft.trimmed().isEmpty() && opt.draft.trimmed() != "0") f.props["draft"] = opt.draft.trimmed().toStdString();
        ids.features.push_back(f);
        fs << f;
        made << QString::fromStdString(f.id);
      }
      if (fs.isEmpty()) { r.refusal = "Çek (Kadar): bölge yok."; return r; }
      r.ok = true;
      r.isNewFeature = true;
      r.feature = fs.takeFirst();
      r.moreFeatures = fs;
      r.featureId = QString::fromStdString(r.feature.id);
      r.prop = "upToOffset";
      r.valueExpr = off.isEmpty() ? "0" : off;
      r.resultFace = r.featureId + ".end";
      r.undoText = QString("Çek: %1 Kadar %2").arg(made.join("+"), label);
      r.status = QString("Çek (Kadar): %1 → %2 (%3 mm%4, ilişkisel: hedef değişince güncellenir).").arg(made.join(", "), label, fmt(d), offTxt);
      return r;
    }
    case Kind::FaceExtrude: {
      Feature sk;
      QString ferr;
      bool inward = false;
      if (!faceSketch(doc, model, t.face, &sk, &ferr, &inward)) { r.refusal = "Çek: " + ferr; return r; }
      Feature f;
      f.id = doc.uniqueFeatureId(FeatureType::Extrude);
      f.type = FeatureType::Extrude;
      f.props["sketch"] = sk.id;
      if (opt.bothSides) f.props["direction"] = "symmetric";
      setUpTo(f);
      const QString op = opFor(opt, model, sOut, &errOp);   // auto: outward -> Ekle, into the material -> Kes
      if (!errOp.isEmpty()) { r.refusal = errOp; return r; }
      applyBodyOps(f, op, opt);
      if (op != "new" && opt.targets.trimmed().isEmpty() && !t.body.isEmpty()) f.props["targets"] = t.body.toStdString();
      f.props["pullFace"] = t.face.toStdString();
      if (!opt.draft.trimmed().isEmpty() && opt.draft.trimmed() != "0") f.props["draft"] = opt.draft.trimmed().toStdString();
      r.ok = true;
      r.isNewFeature = true;
      r.hasPreFeature = true;
      r.preFeature = sk;
      r.feature = f;
      r.featureId = QString::fromStdString(f.id);
      r.prop = "upToOffset";
      r.valueExpr = off.isEmpty() ? "0" : off;
      r.resultFace = r.featureId + ".end";
      r.undoText = QString("Çek: %1 yüzü Kadar %2 (%3)").arg(t.face, label, r.featureId);
      r.status = QString("Çek (Kadar): %1 yüzü → %2 (%3 mm%4, %5, ilişkisel).").arg(t.face, label, fmt(d), offTxt, op);
      return r;
    }
    case Kind::ExtrudeEnd:
    case Kind::UpToOffset: {
      Document cand = doc;
      Feature* f = cand.feature(t.featureId.toStdString());
      if (!f) { r.refusal = "Özellik bulunamadı."; return r; }
      setUpTo(*f);
      r.ok = true;
      r.doc = cand;
      r.featureId = t.featureId;
      r.prop = "upToOffset";
      r.valueExpr = off.isEmpty() ? "0" : off;
      r.resultFace = t.face;
      r.undoText = QString("Çek: %1 Kadar %2").arg(t.featureId, label);
      r.status = QString("Çek (Kadar): %1 → %2 (%3 mm%4, ilişkisel).").arg(t.featureId, label, fmt(d), offTxt);
      return r;
    }
    default: break;
  }
  r.refusal = "Çek (Kadar): bu seçim için desteklenmiyor.";
  return r;
}

} // namespace pcd::pull
