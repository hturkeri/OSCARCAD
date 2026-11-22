// SC-UX (WP-B) — Fill tool. See FillTool.h.
#include "FillTool.h"
#include "PullMapping.h"
#include "DimensionBox.h"
#include <map>
#include "SketchRegions.h"
#include "pc/Expression.h"
#include "pc/Regions.h"
#include <BRepAdaptor_Surface.hxx>
#include <QKeyEvent>
#include <QPainter>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Tool.hxx>
#include <cmath>
#include <QRegularExpression>
#include <algorithm>
#include <set>

namespace pcd {

namespace {
QString stripSplit(QString n) {
  static const QRegularExpression re(":(\\d+)$");
  return n.remove(re);
}
} // namespace

void FillTool::onActivate() {
  auto prompt = [this] {
    if (!host_) return;
    const auto faces = host_->selection() ? host_->selection()->namesOfKind(SelItem::Kind::Face) : QStringList();
    const auto edges = host_->selection() ? host_->selection()->namesOfKind(SelItem::Kind::Edge) : QStringList();
    const QStringList sks = fillSketches();
    if (!sks.isEmpty())
      setPrompt(QString("Doldur: %1 — Enter: kapalı konturlar bölge olur (boşluklar toleransla kapatılır).").arg(sks.join(", ")));
    else if (!edges.isEmpty())
      setPrompt(QString("Doldur: %1 kenar — Enter: deliği yamalar.").arg(edges.size()));
    else if (!faces.isEmpty())
      setPrompt(QString("Doldur: %1 — Enter ile uygulayın.").arg(describe(faces.first())));
    else
      setPrompt("Doldur (Fill): sketch eğrisi (kapalı konturlar → bölge), delik kenarları ya da yuvarlatma / pah / delik / kesik "
                "yüzünü seçin, Enter.");
    // SC23 (O-P0-1): nothing selected = waiting for a selection; else Enter applies
    setOperation(host_ && host_->selection() && !host_->selection()->isEmpty() ? OpState::AwaitingInput : OpState::AwaitingSelection);
  };
  if (host_ && host_->selection())
    selConn_ = connect(host_->selection(), &SelectionModel::changed, this, [this, prompt] {
      emit optionsChanged();
      prompt();
    });
  gaps_.clear();
  prompt();
}

void FillTool::onDeactivate() {
  disconnect(selConn_);
  setOperation(OpState::Idle);   // SC23
  gaps_.clear();
  if (host_) host_->setToolEmphasis({}, {});
  repaintOverlay();
}

QList<ToolOption> FillTool::options() const {
  ToolOption m;
  m.key = "mode";
  m.label = "Kesik özellikler";
  m.type = ToolOption::Type::Choice;
  m.choices = {"suppress|Bastır (Suppress)", "remove|Kaldır (sil)"};
  m.value = mode_;
  m.group = "Doldur";
  ToolOption i;
  i.key = "info";
  i.type = ToolOption::Type::Info;
  i.group = "Doldur";
  const auto faces = host_ && host_->selection() ? host_->selection()->namesOfKind(SelItem::Kind::Face) : QStringList();
  i.label = "Seçili yüzü oluşturan özellik";
  i.value = faces.isEmpty() ? QString("—") : describe(faces.first());
  ToolOption g;   // SC-PULLUPTO
  g.key = "gapTol";
  g.label = "Boşluk toleransı (mm, boş = otomatik)";
  g.type = ToolOption::Type::Text;
  g.value = gapTol_;
  g.group = "Sketch konturları";
  g.tip = "Uçları bu mesafeden yakın eğriler birleşmiş sayılır (içe aktarılan çizimler). Sketch'e kaydedilir (gapTol).";
  return {m, i, g};
}

void FillTool::setOption(const QString& key, const QVariant& v) {
  if (key == "gapTol") { gapTol_ = v.toString().trimmed(); emit optionsChanged(); return; }
  if (key != "mode") return;
  mode_ = v.toString();
  emit optionsChanged();
}

QString FillTool::describe(const QString& face) const {
  if (!host_) return QString();
  const QString base = stripSplit(face);
  const QString fid = base.section('.', 0, 0);
  const pc::Feature* f = host_->document().feature(fid.toStdString());
  if (!f) return face;
  if (f->type == pc::FeatureType::Fillet || f->type == pc::FeatureType::Chamfer) {
    static const QRegularExpression re("\\.[fc](\\d+)$");
    const auto m = re.match(base);
    return QString("%1 (kenar %2/%3)").arg(fid).arg(m.hasMatch() ? m.captured(1).toInt() + 1 : 0).arg(f->refs.size());
  }
  return QString("%1 (%2)").arg(fid, QString::fromLatin1(pc::toString(f->type)));
}

bool FillTool::pick(const PickEvent& e) {
  if (e.button != Qt::LeftButton) return false;
  return selectFromPick(e, false);
}

bool FillTool::wantsKey(const QKeyEvent* e) const {
  return (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && host_ && host_->selection() && !host_->selection()->isEmpty();
}

bool FillTool::keyPress(QKeyEvent* e) {
  if (e->key() != Qt::Key_Return && e->key() != Qt::Key_Enter) return false;
  const bool ok = applyFill();
  // SC23: the result as an operation event (Invalid keeps the reason in the HUD until the next action)
  if (ok) emitMomentary(OpState::Completed, prompt());
  else setOperation(OpState::Invalid, prompt());
  return true;
}

bool FillTool::cancel() { return false; }

bool FillTool::applyFill() {
  if (!host_) return false;
  // SC-PULLUPTO: sketch curves / regions -> regions; hole edges -> patch
  const QStringList sks = fillSketches();
  if (!sks.isEmpty()) {
    QStringList msgs;
    QList<SelItem> regs;
    bool any = false;
    gaps_.clear();
    host_->setToolEmphasis({}, {});
    for (const QString& sid : sks) {
      QString m;
      if (fillSketch(sid.toStdString(), &m, &regs)) any = true;
      msgs << m;
    }
    if (!regs.isEmpty()) host_->selection()->set(regs);
    const QString msg = msgs.join(" ");
    setPrompt(msg);
    host_->showPrompt(msg, 0);
    repaintOverlay();
    return any;
  }
  const QStringList edges = host_->selection()->namesOfKind(SelItem::Kind::Edge);
  if (!edges.isEmpty() && host_->selection()->namesOfKind(SelItem::Kind::Face).isEmpty()) {
    QString msg;
    const bool ok = fillEdges(edges, &msg);
    if (ok && host_->selection()) host_->selection()->clear();
    setPrompt(msg);
    host_->showPrompt(msg, 0);
    return ok;
  }
  const QStringList faces = host_->selection()->namesOfKind(SelItem::Kind::Face);
  if (faces.isEmpty()) { setPrompt("Doldur: önce bir yüz, delik kenarları ya da sketch eğrisi seçin."); return false; }
  QString msg;
  bool any = false;
  for (const QString& f : faces) {
    if (!host_) break;
    if (fillFace(f, &msg)) any = true;
  }
  if (host_ && host_->selection()) host_->selection()->clear();
  setPrompt(msg);
  if (host_) host_->showPrompt(msg, 0);
  return any;
}

bool FillTool::fillFace(const QString& face, QString* msg) {
  const QString base = stripSplit(face);
  const QString fid = base.section('.', 0, 0);
  const QString rest = base.mid(fid.size() + 1);
  pc::Document cand = host_->document();
  pc::Feature* f = cand.feature(fid.toStdString());
  if (!f) { *msg = "Doldur bu yüz için desteklenmiyor."; return false; }
  static const QRegularExpression reFC("^[fc](\\d+)$"), reHole("^h(\\d+)\\.");
  if ((f->type == pc::FeatureType::Fillet || f->type == pc::FeatureType::Chamfer) && reFC.match(rest).hasMatch()) {
    const int k = reFC.match(rest).captured(1).toInt();
    bool plain = true;
    for (const auto& r : f->refs) if (r.rfind("face:", 0) == 0) plain = false;
    if (f->refs.size() <= 1) {
      const bool ok = host_->deleteFeatureInteractive(fid.toStdString());
      *msg = ok ? QString("Doldur: %1 silindi (son kenarıydı).").arg(fid) : QString("Doldur: %1 silinmedi.").arg(fid);
      return ok;
    }
    if (!plain || k < 0 || k >= int(f->refs.size())) {
      *msg = QString("Doldur: %1 kenar seçicileri (face:...) içeriyor; kenarı klasik diyalogdan çıkarın.").arg(fid);
      return false;
    }
    std::vector<std::string> sorted = f->refs;
    std::sort(sorted.begin(), sorted.end());
    const std::string gone = sorted[size_t(k)];
    f->refs.erase(std::remove(f->refs.begin(), f->refs.end(), gone), f->refs.end());
    const bool ok = host_->commitDocument(cand, QString("Doldur: %1 kenarı çıkarıldı").arg(fid));
    *msg = ok ? QString("Doldur: %1 kenarı %2'dan çıkarıldı (%3 kenar kaldı).").arg(QString::fromStdString(gone), fid).arg(f->refs.size())
              : QString("Doldur: uygulanmadı.");
    return ok;
  }
  if (f->type == pc::FeatureType::Hole && reHole.match(rest).hasMatch()) {
    std::set<QString> locs;
    for (const auto& fi : host_->modelResult().info)
      if (fi.featureId == fid.toStdString())
        for (const auto& n : fi.createdFaces) {
          const QString q = QString::fromStdString(n);
          const auto m = reHole.match(q.mid(fid.size() + 1));
          if (m.hasMatch()) locs.insert(m.captured(1));
        }
    if (locs.size() > 1) { *msg = "Delik konumu sketch noktasıdır: sketch'i düzenleyin (çift tık)."; return false; }
    const bool ok = host_->deleteFeatureInteractive(fid.toStdString());
    *msg = ok ? QString("Doldur: %1 silindi.").arg(fid) : QString("Doldur: %1 silinmedi.").arg(fid);
    return ok;
  }
  if ((f->type == pc::FeatureType::Extrude || f->type == pc::FeatureType::Revolve) && f->prop("op") == "cut") {
    bool ok;
    if (mode_ == "remove") {
      ok = host_->deleteFeatureInteractive(fid.toStdString());
      *msg = ok ? QString("Doldur: kesik %1 silindi.").arg(fid) : QString("Doldur: uygulanmadı.");
    } else {
      ok = host_->setFeatureSuppressed(fid.toStdString(), true);
      *msg = ok ? QString("Doldur: kesik %1 bastırıldı (Geçmiş sekmesinden geri açılabilir).").arg(fid) : QString("Doldur: uygulanmadı.");
    }
    return ok;
  }
  if (f->type == pc::FeatureType::Shell && rest.startsWith("inner.")) {
    const std::string orig = rest.mid(6).toStdString();
    f->refs.erase(std::remove(f->refs.begin(), f->refs.end(), orig), f->refs.end());
    if (f->refs.empty()) {
      const bool ok = host_->deleteFeatureInteractive(fid.toStdString());
      *msg = ok ? QString("Doldur: %1 silindi.").arg(fid) : QString("Doldur: uygulanmadı.");
      return ok;
    }
    const bool ok = host_->commitDocument(cand, QString("Doldur: %1 yüzü kapatıldı").arg(fid));
    *msg = ok ? QString("Doldur: %1 açık yüzü kapatıldı.").arg(fid) : QString("Doldur: uygulanmadı.");
    return ok;
  }
  *msg = "Doldur bu yüz için desteklenmiyor.";
  return false;
}

// ================================================================ SC-PULLUPTO: Fill on sketches / hole edges
QStringList FillTool::fillSketches() const {
  QStringList out;
  if (!host_ || !host_->selection()) return out;
  for (const auto& it : host_->selection()->items()) {
    std::string sid;
    char ty = 0;
    QString s;
    if (it.kind == SelItem::Kind::Sketch) s = it.name;
    else if ((it.kind == SelItem::Kind::SketchCurve || it.kind == SelItem::Kind::SketchRegion || it.kind == SelItem::Kind::SketchPoint ||
              ((it.kind == SelItem::Kind::Edge || it.kind == SelItem::Kind::Face) && it.name.startsWith("sketch:"))) &&
             parseSketchItemName(it.name, &sid, &ty, nullptr))
      s = QString::fromStdString(sid);
    if (!s.isEmpty() && !out.contains(s)) out << s;
  }
  return out;
}

bool FillTool::fillSketch(const std::string& sid, QString* msg, QList<SelItem>* regions) {
  const QString qs = QString::fromStdString(sid);
  const pc::Feature* sf = host_->document().feature(sid);
  if (!sf || sf->type != pc::FeatureType::Sketch) { *msg = QString("Doldur: %1 bir sketch değil.").arg(qs); return false; }
  // a tolerance typed in the Options panel is stored on the sketch (gapTol) so that the regions stay closed
  if (!gapTol_.isEmpty() && QString::fromStdString(sf->prop("gapTol")) != gapTol_) {
    double g = 0;
    if (!host_->evaluateExpression(gapTol_, &g) || !(g > 0)) { *msg = QString("Doldur: geçersiz boşluk toleransı '%1'.").arg(gapTol_); return false; }
    pc::Document cand = host_->document();
    cand.feature(sid)->props["gapTol"] = gapTol_.toStdString();
    if (!host_->commitDocument(cand, QString("Doldur: %1 boşluk toleransı %2 mm").arg(qs, gapTol_))) {
      *msg = "Doldur: uygulanmadı.";
      return false;
    }
  }
  pc::sketch::RegionResult rr;
  gp_Ax3 fr;
  if (!sketchRegionInfo(host_->document(), host_->modelResult(), sid, rr, fr)) {
    *msg = QString("Doldur: %1 değerlendirilemedi (sketch hatalı ya da bastırılmış).").arg(qs);
    return false;
  }
  auto to3 = [&](const gp_Pnt2d& p) { return gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * p.X() + fr.YDirection().XYZ() * p.Y()); };
  QStringList emphEdges;
  for (const auto& p : rr.openPoints) gaps_.push_back(to3(p));
  for (int e : rr.openEntities) emphEdges << sketchCurveName(sid, e);
  if (!emphEdges.isEmpty()) host_->setToolEmphasis({}, emphEdges);
  const std::set<int> used = consumedRegions(host_->document(), sid);
  int added = 0;
  for (int k = 0; k < int(rr.profile.faces.size()); ++k)
    if (!used.count(k)) { regions->append(SelItem{SelItem::Kind::SketchRegion, regionName(sid, k)}); ++added; }
  const QString desc = QString::fromStdString(pc::sketch::describeRegions(rr));
  if (rr.profile.faces.empty()) {
    QString hint;
    if (rr.nearestGap > 0)
      hint = QString(" En yakın iki açık uç arası %1 mm: Seçenekler'de boşluk toleransını büyütün (> %1) ya da sketch'i düzenleyin.")
                 .arg(rr.nearestGap, 0, 'g', 4);
    *msg = QString("Doldur: %1 — kapalı kontur yok; %2 açık uç kırmızı işaretlendi.%3").arg(qs).arg(rr.openPoints.size()).arg(hint);
    return false;
  }
  *msg = QString("Doldur: %1 → %2. %3").arg(qs, desc,
                                             added ? QString("%1 bölge seçildi: Çek (P) ile 3B'ye çekin.").arg(added)
                                                   : QString("Bölgelerin hepsi zaten çekilmiş."));
  if (!rr.openPoints.empty()) *msg += QString(" Açık uçlar kırmızı işaretli.");
  return added > 0;
}

bool FillTool::fillEdges(const QStringList& edges, QString* msg) {
  const auto& model = host_->modelResult();
  // the faces of "edge(A|B)" names; the face shared by every edge is the one the hole opens into
  std::map<QString, int> count;
  std::vector<TopoDS_Edge> es;
  for (const QString& e : edges) {
    auto it = model.edges.find(e.toStdString());
    if (it == model.edges.end() || !e.startsWith("edge(")) { *msg = QString("Doldur: kenar %1 bulunamadı.").arg(e); return false; }
    es.push_back(it->second);
    const QString inner = e.mid(5, e.lastIndexOf(')') - 5);
    for (const QString& f : inner.split('|')) count[f]++;
  }
  QString open;
  for (const auto& [f, n] : count) {
    if (n != int(edges.size())) continue;
    auto fit = model.faces.find(f.toStdString());
    if (fit == model.faces.end()) continue;
    try {
      if (BRepAdaptor_Surface(fit->second, Standard_False).GetType() == GeomAbs_Plane) { open = f; break; }
    } catch (...) {}
  }
  if (open.isEmpty()) { *msg = "Doldur: seçili kenarlar tek bir düzlemsel yüzdeki bir deliğin kenarları olmalı."; return false; }
  // closed loop: every vertex used by exactly two of the selected edges
  {
    std::vector<TopoDS_Vertex> vs;
    for (const auto& e : es) {
      TopoDS_Vertex a, b;
      TopExp::Vertices(e, a, b);
      if (a.IsNull() || b.IsNull()) continue;
      if (a.IsSame(b)) continue;   // closed edge (full circle)
      vs.push_back(a);
      vs.push_back(b);
    }
    for (const auto& v : vs) {
      int n = 0;
      for (const auto& w : vs) n += v.IsSame(w) ? 1 : 0;
      if (n != 2) { *msg = "Doldur: seçili kenarlar kapalı bir halka oluşturmuyor (çift tık bir yüzün kenar halkasını seçer)."; return false; }
    }
  }
  QStringList walls;
  for (const QString& e : edges) {
    const QString inner = e.mid(5, e.lastIndexOf(')') - 5);
    for (const QString& f : inner.split('|'))
      if (f != open && !walls.contains(f)) walls << f;
  }
  // a parametric hole (Hole / cut Extrude / cut Revolve with one location): undo that feature like Fill on its face
  {
    QString fid;
    bool same = !walls.isEmpty();
    for (const QString& w : walls) {
      const QString f = w.section('.', 0, 0).section(':', 0, 0);
      if (fid.isEmpty()) fid = f;
      else if (f != fid) same = false;
    }
    const pc::Feature* f = same ? host_->document().feature(fid.toStdString()) : nullptr;
    if (f && f->type != pc::FeatureType::Hole) {   // a cut with several separate tool solids: patch only this hole
      for (const auto& fi : model.info)
        if (fi.featureId == fid.toStdString() && !fi.toolShape.IsNull()) {
          int n = 0;
          for (TopExp_Explorer x(fi.toolShape, TopAbs_SOLID); x.More(); x.Next()) ++n;
          if (n > 1) f = nullptr;
        }
    }
    if (f && (f->type == pc::FeatureType::Hole || ((f->type == pc::FeatureType::Extrude || f->type == pc::FeatureType::Revolve) && f->prop("op") == "cut"))) {
      QString m;
      if (fillFace(walls.first(), &m)) { *msg = m; return true; }
    }
  }
  // otherwise: a plug — Sketch on the open face bounded by the loop + Extrude (join) into the material as deep as the walls
  pc::Feature sk;
  QString err;
  bool inward = false;
  gp_Ax3 fr;
  if (!pull::faceSketch(host_->document(), model, open, &sk, &err, &inward, &fr, &es)) { *msg = "Doldur: " + err; return false; }
  const gp_Dir out = inward ? fr.Direction().Reversed() : fr.Direction();
  double depth = 0;
  for (const QString& w : walls) {
    auto fit = model.faces.find(w.toStdString());
    if (fit == model.faces.end()) continue;
    for (TopExp_Explorer ex(fit->second, TopAbs_VERTEX); ex.More(); ex.Next()) {
      const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()));
      depth = std::max(depth, -gp_Vec(fr.Location(), p).Dot(gp_Vec(out)));
    }
  }
  if (depth <= 1e-6) { *msg = "Doldur: deliğin derinliği bulunamadı."; return false; }
  pc::Document cand = host_->document();
  cand.features.push_back(sk);
  pc::Feature ex;
  ex.id = cand.uniqueFeatureId(pc::FeatureType::Extrude);
  ex.type = pc::FeatureType::Extrude;
  ex.props["sketch"] = sk.id;
  ex.props["distance"] = pc::formatNumber(depth);
  ex.props["direction"] = inward ? "normal" : "reverse";   // into the material
  ex.props["op"] = "join";
  const std::string body = model.bodyOfFace(open.toStdString());
  if (!body.empty()) ex.props["targets"] = body;
  cand.features.push_back(ex);
  if (host_->rollbackIndex() >= 0) {
    const bool ok = host_->insertFeature(sk, "Doldur: delik yaması") && host_->insertFeature(ex, "Doldur: delik yaması");
    *msg = ok ? QString("Doldur: delik yamalandı (%1 + %2, derinlik %3 mm).").arg(QString::fromStdString(sk.id), QString::fromStdString(ex.id), formatDim(depth))
              : QString("Doldur: uygulanmadı.");
    return ok;
  }
  const bool ok = host_->commitDocument(cand, "Doldur: delik yaması");
  *msg = ok ? QString("Doldur: delik yamalandı (%1 + %2, derinlik %3 mm).").arg(QString::fromStdString(sk.id), QString::fromStdString(ex.id), formatDim(depth))
            : QString("Doldur: uygulanmadı.");
  return ok;
}

void FillTool::paintOverlay(QPainter& p, const ViewportOverlay&) {
  if (gaps_.empty()) return;
  for (size_t i = 0; i < gaps_.size(); ++i) ViewportOverlay::drawGapMarker(p, toScreen(gaps_[i]), i == 0);   // SC23: shared marker
}

} // namespace pcd
