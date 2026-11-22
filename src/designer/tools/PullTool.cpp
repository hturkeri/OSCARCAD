// SC-UX (WP-B) — Pull tool. See PullTool.h.
#include "PullTool.h"
#include "SketchRegions.h"
#include "ViewportOverlay.h"
#include "pc/Expression.h"
#include "pc/ShapeViewer.h"
#include "pc/UpTo.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <TopoDS_Compound.hxx>
#include <cmath>

namespace pcd {

using pull::Kind;

PullTool::PullTool(QObject* parent) : ToolBase(parent) {
  commitTimer_.setSingleShot(true);
  commitTimer_.setInterval(600);
  connect(&commitTimer_, &QTimer::timeout, this, [this] {
    if (st_ == St::Pending && !held_) commitValue(QString::fromStdString(pc::formatNumber(dragValue_)));
  });
}

QList<ToolGuide> PullTool::guides() const {
  return {{"select", "Seç", "select", "Seç (Select): çekilecek yüz, kenar ya da bölge"},
          {"direction", "Yön", "pulldir", "Yön (Direction): ok başına tıklamak yönü çevirir"},
          {"revolve", "Döndür", "revolve", "Döndür (Revolve): eksen için sketch çizgisine Alt+tık"},
          {"upto", "Kadar", "upto", "Kadar (Up To, U): hedefe kadar çek — köşe, kenar, yüz, düzlem ya da başka bir sketch'in eğrisi / noktası (ilişkisel)"},
          {"ruler", "Cetvel", "ruler", "Cetvel (Ruler): bir referans seçin, ona olan mesafeyi yazın"}};   // SC-PULLUPTO
}

void PullTool::setActiveGuide(const QString& g) {
  if (g == guide_) return;
  for (const auto& gd : guides())
    if (gd.key == g && !gd.enabled) return;
  guide_ = g;
  if (g != "upto" && g != "ruler") clearUpTo();   // SC-PULLUPTO
  if (g == "upto" || g == "ruler") {
    clearUpTo();
    const bool ok = pull::supportsUpTo(tgt_);
    const QString what = g == "upto" ? "Kadar (Up To)" : "Cetvel (Ruler)";
    if (!ok)
      setPrompt(QString("Çek: %1 — önce çekilecek sketch bölgesini, düzlemsel yüzü ya da Extrude uç yüzünü seçin.").arg(what));
    else if (g == "upto")
      setPrompt("Çek: Kadar (Up To) — hedefi tıklayın: köşe, kenar, yüz, düzlem ya da başka bir sketch'in eğrisi / noktası. "
                "Uzunluk hedefe bağlı kalır (ilişkisel).");
    else
      setPrompt("Çek: Cetvel (Ruler) — referansı tıklayın (köşe, kenar, yüz, düzlem, sketch noktası), sonra ona olan mesafeyi yazın.");
    emit guidesChanged();
    return;
  }
  if (g == "revolve") setPrompt("Çek: Döndür — dönme ekseni için bölgenin sketch'inde bir çizgiye (Alt+)tıklayın.");
  else if (g == "direction") setPrompt("Çek: Yön — ok başına tıklamak çekme yönünü tersine çevirir.");
  else armedPrompt();
  emit guidesChanged();
}

QList<ToolOption> PullTool::options() const {
  QList<ToolOption> o;
  // SC23 (O-P1-2): SpaceClaim 2023 R1 shows "General" with Add / Cut / No merge at the top of Options-Pull [G]
  ToolOption m;
  m.key = "merge";
  m.label = "Birleştirme";
  m.type = ToolOption::Type::Choice;
  m.choices = {"auto|Otomatik", "add|Ekle (Add)", "cut|Kes (Cut)", "nomerge|Birleştirme yok (No Merge)", "intersect|Kesişim"};
  m.value = opt_.merge;
  m.group = "Genel";
  m.tip = "Ekle: mevcut gövdeye ekler · Kes: gövdeden çıkarır · Birleştirme yok: ayrı gövde · Otomatik: yöne göre";
  m.onMiniToolbar = true;
  o << m;
  ToolOption bn;
  bn.key = "bodyName";
  bn.label = "Yeni gövde adı";
  bn.type = ToolOption::Type::Text;
  bn.value = opt_.bodyName;
  bn.group = "Genel";
  bn.enabled = opt_.merge == "nomerge";
  o << bn;
  ToolOption kt;
  kt.key = "keepTool";
  kt.label = "Kesiciyi koru";
  kt.type = ToolOption::Type::Bool;
  kt.value = opt_.keepTool;
  kt.group = "Genel";
  kt.enabled = opt_.merge == "cut" || opt_.merge == "intersect";
  o << kt;
  // [B] "Thicken surfaces": the engine has no surface bodies / Thicken yet (SC23-CR-1/2). A region is always pulled
  // into a solid, so the row is shown checked and disabled, with the reason (ANALYSIS §4.6).
  ToolOption th;
  th.key = "thicken";
  th.label = "Yüzeyleri kalınlaştır (Thicken surfaces)";
  th.type = ToolOption::Type::Bool;
  th.value = true;
  th.group = "Genel";
  th.enabled = false;
  th.tip = "Her zaman açık: PersistCAD'de yüzey gövdesi yok, bölge çekilince katı olur. "
           "Kalınlaştırmasız yüzey çekme motor değişikliği bekliyor (SC23-CR-1/2).";
  o << th;
  ToolOption un;   // SC23 (O-P1-3)
  un.key = "unit";
  un.label = "Birim";
  un.type = ToolOption::Type::Info;
  un.value = QString("Belge birimi: %1 (yazarken: mm, cm, m, in, \", °, deg, rad)").arg(kDocumentLengthUnit);
  un.group = "Genel";
  un.tip = "Değer alanına birimle yazılabilir (15 mm, 1.5 cm, 0.5 in); değer belge birimine çevrilir.";
  o << un;
  ToolOption e;
  e.key = "edge";
  e.label = "Kenar";
  e.type = ToolOption::Type::Choice;
  e.choices = {"round|Yuvarlat (Round)", "chamfer|Pah (Chamfer)"};
  e.value = opt_.edge;
  e.group = "Kenar";
  e.onMiniToolbar = true;
  o << e;
  ToolOption b;
  b.key = "bothSides";
  b.label = "İki yöne çek (Pull Both Sides)";
  b.type = ToolOption::Type::Bool;
  b.value = opt_.bothSides;
  b.group = "Yön";
  b.onMiniToolbar = true;
  o << b;
  ToolOption dr;
  dr.key = "draft";
  dr.label = "Eğim açısı (°)";
  dr.type = ToolOption::Type::Expr;
  dr.value = opt_.draft;
  dr.group = "Yön";
  dr.paramHint = "Pull_draft";
  dr.paramKind = pc::ParamKind::Angle;
  o << dr;
  ToolOption g;
  g.key = "createGroup";
  g.label = "Grup oluştur (Ctrl+G)";
  g.type = ToolOption::Type::Action;
  g.group = "Ölçü";
  g.icon = "group";
  g.onMiniToolbar = true;
  o << g;
  ToolOption ar;
  ar.key = "applyOnRelease";
  ar.label = "Bırakınca hemen uygula";
  ar.type = ToolOption::Type::Bool;
  ar.value = applyOnRelease_;
  ar.group = "Ölçü";
  o << ar;
  return o;
}

void PullTool::setOption(const QString& key, const QVariant& v) {
  if (key == "merge") opt_.merge = v.toString();
  else if (key == "edge") opt_.edge = v.toString();
  else if (key == "bothSides") opt_.bothSides = v.toBool();
  else if (key == "keepTool") opt_.keepTool = v.toBool();
  else if (key == "bodyName") opt_.bodyName = v.toString();
  else if (key == "draft") opt_.draft = v.toString();
  else if (key == "targets") opt_.targets = v.toString();
  else if (key == "applyOnRelease") applyOnRelease_ = v.toBool();
  else if (key == "createGroup") { createGroup(); return; }
  else return;
  if (host_ && (key == "edge")) retarget();
  emit optionsChanged();
}

void PullTool::onActivate() {
  guide_ = "select";
  flip_ = 1.0;
  opt_.revolveAxis.clear();
  haveAxis_ = false;
  if (host_ && host_->selection())
    selConn_ = connect(host_->selection(), &SelectionModel::changed, this, [this] {
      const bool had = st_ == St::Typing || st_ == St::Pending || st_ == St::Dragging;
      if (had) { commitTimer_.stop(); closeBox(); clearGhost(); held_ = false; }
      retarget();
      if (had) emitMomentary(OpState::Cancelled, QString());   // SC23 §4.4: selection change = cancel + retarget
    });
  if (host_ && host_->viewer() && !box_) {
    box_ = new DimensionBox(host_->viewer());
    connect(box_, &DimensionBox::committed, this, [this] {
      const bool total = box_->fieldCount() > 1 && box_->isLocked(1);
      commitValue(box_->text(total ? 1 : 0), total);
    });
    connect(box_, &DimensionBox::cancelled, this, [this] { cancel(); });
    connect(box_, &DimensionBox::textEdited, this, [this](int i) {
      double v = 0;
      if (!upToRef_.isEmpty() && guide_ == "ruler") {   // SC-PULLUPTO: live preview of the ruler value
        if (box_->value(i, &v)) {
          auto ev = [this](const QString& x, double* y) { return host_->evaluateExpression(x, y); };
          const pull::Result r = pull::applyUpTo(host_->document(), host_->modelResult(), tgt_, upToRef_,
                                                 QString::fromStdString(pc::formatNumber(-v)), opt_, ev);
          if (r.ok) previewResult(r); else clearGhost();
        }
        return;
      }
      if (box_->value(i, &v)) updateGhost(i == 1 ? v - tgt_.current : v);
    });
    connect(box_, &DimensionBox::createGroupRequested, this, [this] { createGroup(); });
    connect(box_, &DimensionBox::invalidCommit, this, [this](int, const QString& why) { refuse("Çek: " + why); });   // SC23
    connect(box_, &DimensionBox::textEdited, this, [this](int) {   // SC23: typing replaces an earlier refusal
      if (!errReason_.isEmpty()) errReason_.clear();
      reportState();
    });
  }
  // SC23 (O-P0-3): units, zero / reason (tooltip), expressions through the host's evaluator
  if (box_) {
    box_->setReasonValidator([this](const QString& t, double* v, QString* why) {
      if (!host_) return false;
      auto ev = [this](const QString& x, double* y, QString* err) { return host_->evaluateExpression(x, y, err); };
      const ValueInput::Result r = ValueInput::evaluate(t, ev, valueOptions());
      if (!r.acceptable()) { if (why) *why = r.reason; return false; }
      if (v) *v = r.value;
      return true;
    });
    box_->setFieldTip(QString("Belge birimi: %1. Birimle yazılabilir: 15 mm, 1.5 cm, 0.5 in (açı: 90°, 1.2 rad). "
                              "Negatif = ters yön. Enter uygular, Esc iptal eder.").arg(kDocumentLengthUnit));
  }
  // SC23 (O-P0-2): focus moving away from the viewer / the window losing activation stops the auto-commit
  hostWin_ = host_ ? host_->hostWindow() : nullptr;
  if (hostWin_) hostWin_->installEventFilter(this);
  focusConn_ = connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
    if (!host_ || !commitTimer_.isActive()) return;
    QWidget* v = host_->viewer();
    if (now && v && (now == v || v->isAncestorOf(now))) return;
    holdPreview();
  });
  held_ = false;
  errReason_.clear();
  gate_.reset();
  retarget();
}

void PullTool::onDeactivate() {
  upToRef_.clear();   // SC-PULLUPTO
  commitTimer_.stop();
  disconnect(focusConn_);   // SC23
  if (hostWin_) hostWin_->removeEventFilter(this);
  hostWin_ = nullptr;
  held_ = false;
  errReason_.clear();
  gate_.reset();
  closeBox();
  clearGhost();
  disconnect(selConn_);
  if (host_) host_->setToolEmphasis({}, {});
  st_ = St::Idle;
  pressed_ = dragged_ = false;
  setOperation(OpState::Idle);
}

void PullTool::pruneSelection() {
  if (!host_ || !host_->selection()) return;
  const auto& model = host_->modelResult();
  const auto& doc = host_->document();
  QList<SelItem> keep;
  bool changed = false;
  for (const auto& it : host_->selection()->items()) {
    bool ok = true;
    if (it.kind == SelItem::Kind::Face) ok = model.faces.count(it.name.toStdString()) > 0;
    else if (it.kind == SelItem::Kind::Edge) ok = model.edges.count(it.name.toStdString()) > 0;
    else if (it.kind == SelItem::Kind::SketchRegion) {
      std::string sid;
      int k = 0;
      std::vector<TopoDS_Face> f;
      gp_Ax3 fr;
      ok = parseSketchItemName(it.name, &sid, nullptr, &k) && !sketchConsumed(doc, sid) && sketchRegionFaces(doc, model, sid, f, fr) &&
           k < int(f.size()) && !consumedRegions(doc, sid).count(k);   // SC-PULLUPTO: regions pulled already
    }
    if (ok) keep << it; else changed = true;
  }
  if (changed) host_->selection()->set(keep);
}

void PullTool::documentChanged() {
  if (!host_ || selfChange_) return;   // our own commit / parameter creation: the tool re-targets itself
  if (st_ == St::Typing || st_ == St::Pending || st_ == St::Dragging) {
    // SC23 (ANALYSIS §4.4): the document changed under a preview (Geri Al / Yinele from the toolbar, a parameter
    // edit, ...): the preview belongs to the old document -> dropped, never committed
    dropPreviewForExternalChange();
    return;
  }
  pruneSelection();
  retarget();
}

void PullTool::dropPreviewForExternalChange() {
  commitTimer_.stop();
  pressed_ = dragged_ = false;
  held_ = false;
  upToRef_.clear();
  closeBox();
  clearGhost();
  st_ = tgt_.valid() ? St::Armed : St::Idle;
  pruneSelection();
  retarget();
  setPrompt("Çek: önizleme iptal edildi (belge değişti).");
  emitMomentary(OpState::Cancelled, prompt());
}

void PullTool::armedPrompt() {
  if (!host_) return;
  if (tgt_.kind == Kind::None) {
    setPrompt("Çek (Pull): bir yüz, kenar ya da sketch bölgesi seçin; oku sürükleyin ya da bir değer yazın.");
    return;
  }
  if (!tgt_.valid()) {
    setPrompt(tgt_.refusal.startsWith("Çek") ? tgt_.refusal : "Çek: " + tgt_.refusal);
    return;
  }
  QString what;
  switch (tgt_.kind) {
    case Kind::NewExtrude: what = "yeni Extrude"; break;
    case Kind::NewRevolve: what = QString("yeni Revolve (eksen e%1)").arg(opt_.revolveAxis); break;
    case Kind::NewFillet: what = "yeni Fillet"; break;
    case Kind::NewChamfer: what = "yeni Chamfer"; break;
    case Kind::FaceExtrude: what = "yüzü uzat (yeni Extrude; + dışa, - içe keser)"; break;
    case Kind::RevolveRadius: what = QString("yarıçap ofseti (%1)").arg(tgt_.featureId); break;
    default: what = QString("%1.%2").arg(tgt_.featureId, tgt_.prop); break;
  }
  QString s = QString("Çek: %1 seçili → %2. Oku sürükleyin ya da %3 yazın (Enter).").arg(tgt_.describe, what, tgt_.fieldLabel.toLower());
  if (!tgt_.boundParam.isEmpty()) s += QString(" Değer %1 parametresine yazılır.").arg(tgt_.boundParam);
  if (!tgt_.message.isEmpty()) s += " " + tgt_.message;
  if (guide_ == "upto" && pull::supportsUpTo(tgt_)) s = QString("Çek: %1 seçili → Kadar (Up To): hedefi tıklayın (köşe, kenar, yüz, düzlem, başka sketch'in eğrisi / noktası).").arg(tgt_.describe);
  else if (guide_ == "ruler" && pull::supportsUpTo(tgt_)) s = QString("Çek: %1 seçili → Cetvel: referansı tıklayın, mesafeyi yazın.").arg(tgt_.describe);
  else if (pull::supportsUpTo(tgt_)) s += " Kadar için U.";
  setPrompt(s);
}

void PullTool::retarget() {
  if (!host_ || inRetarget_) return;
  inRetarget_ = true;
  const QList<SelItem> sel = host_->selection() ? host_->selection()->items() : QList<SelItem>();
  // the revolve axis belongs to the selected region's sketch
  if (!opt_.revolveAxis.isEmpty()) {
    std::string sid;
    char ty = 0;
    if (sel.isEmpty() || !parseSketchItemName(sel.first().name, &sid, &ty, nullptr) || ty != 'r' ||
        QString::fromStdString(sid) != tgt_.sketchId) {
      opt_.revolveAxis.clear();
      haveAxis_ = false;
      if (guide_ == "revolve") { guide_ = "select"; emit guidesChanged(); }
    }
  }
  upToRef_.clear();   // SC-PULLUPTO: a new selection drops the Up To / ruler target
  tgt_ = pull::analyze(host_->document(), host_->modelResult(), sel, opt_);
  st_ = tgt_.valid() ? St::Armed : St::Idle;
  computeHandle();
  QStringList emph;   // SC-PULLUPTO: the regions that will be pulled (selection may span several sketches)
  if (tgt_.kind == Kind::NewExtrude || tgt_.kind == Kind::NewRevolve)
    for (const auto& rs : tgt_.regionSets)
      for (int k : rs.second) emph << regionName(rs.first.toStdString(), k);
  host_->setToolEmphasis(emph, {});
  errReason_.clear();   // SC23: a new target replaces an earlier refusal
  held_ = false;
  armedPrompt();
  repaintOverlay();
  inRetarget_ = false;
  reportState();
}

void PullTool::computeHandle() {
  haveHandle_ = false;
  if (!host_ || !tgt_.valid()) return;
  const auto& doc = host_->document();
  const auto& model = host_->modelResult();
  if (tgt_.kind == Kind::NewFillet || tgt_.kind == Kind::NewChamfer) {
    gp_Pnt m;
    gp_Dir d;
    if (edgeMidInward(model, tgt_.refs.first(), m, d)) { anchor_ = m; dir_ = d; haveHandle_ = true; }
    return;
  }
  TopoDS_Face f;
  if (!faceByName(doc, model, tgt_.face, f)) return;
  gp_Pnt c;
  gp_Dir n;
  if (!faceCentroidNormal(f, c, n)) return;
  anchor_ = c;
  dir_ = n;
  if (tgt_.kind == Kind::FaceExtrude || tgt_.kind == Kind::ExtrudeEnd || tgt_.kind == Kind::ExtrudeStart) {
    // a ring / L-shaped face: its centroid lies in the hole -> put the arrow on the face itself
    try {
      Handle(Geom_Surface) gs = BRep_Tool::Surface(f);
      GeomAPI_ProjectPointOnSurf pr(c, gs);
      double u0 = 0, v0 = 0;
      if (pr.NbPoints() > 0) pr.LowerDistanceParameters(u0, v0);
      BRepClass_FaceClassifier fc(f, gp_Pnt2d(u0, v0), 1e-7);
      if (fc.State() == TopAbs_OUT) {
        double ua, ub, va, vb;
        BRepTools::UVBounds(f, ua, ub, va, vb);
        double best = 1e300;
        for (int i = 1; i < 24; ++i)
          for (int j = 1; j < 24; ++j) {
            const gp_Pnt2d uv(ua + (ub - ua) * i / 24.0, va + (vb - va) * j / 24.0);
            if (BRepClass_FaceClassifier(f, uv, 1e-7).State() != TopAbs_IN) continue;
            const gp_Pnt p = gs->Value(uv.X(), uv.Y());
            if (p.Distance(c) < best) { best = p.Distance(c); anchor_ = p; }
          }
      }
    } catch (...) {
    }
  }
  if (tgt_.kind == Kind::NewExtrude || tgt_.kind == Kind::NewRevolve) {
    gp_Ax3 fr;
    if (sketchFrameOf(model, tgt_.sketchId.toStdString(), fr)) dir_ = fr.Direction();
  }
  haveHandle_ = true;
  if (tgt_.kind == Kind::NewRevolve) {
    haveAxis_ = false;
    gp_Ax3 fr;
    auto it = model.solvedSketches.find(tgt_.sketchId.toStdString());
    if (it != model.solvedSketches.end() && sketchFrameOf(model, tgt_.sketchId.toStdString(), fr)) {
      const auto& sk = it->second;
      const pc::SkEntity* e = sk.entity(opt_.revolveAxis.toInt());
      if (e && e->type == pc::SkEntityType::Line) {
        const pc::SkPoint *a = sk.point(e->points[0]), *b = sk.point(e->points[1]);
        if (a && b) {
          const gp_Pnt A(fr.Location().XYZ() + fr.XDirection().XYZ() * a->x + fr.YDirection().XYZ() * a->y);
          const gp_Pnt B(fr.Location().XYZ() + fr.XDirection().XYZ() * b->x + fr.YDirection().XYZ() * b->y);
          if (A.Distance(B) > 1e-9) { axis_ = gp_Ax1(A, gp_Dir(gp_Vec(A, B))); haveAxis_ = true; }
        }
      }
    }
  }
}

int PullTool::handleCount() const { return (haveHandle_ && st_ != St::Idle) ? 1 : 0; }

bool PullTool::arrowGeometry(QPointF* base, QPointF* tip, QPointF* unit, double* ppm) const {
  if (!haveHandle_ || !host_ || !host_->viewer()) return false;
  QPointF b, u;
  double p = 0;
  const gp_Dir d = flip_ > 0 ? dir_ : dir_.Reversed();
  if (!screenDir(anchor_, d, &b, &u, &p)) {
    u = QPointF(0.7071, -0.7071);   // looking along the arrow: draw it diagonally
    p = std::max(p, 1e-6);
  }
  if (base) *base = b;
  if (unit) *unit = u;
  if (ppm) *ppm = p;
  if (tip) *tip = b + u * 62.0;
  return true;
}

QPointF PullTool::handleTip() const {
  QPointF t;
  arrowGeometry(nullptr, &t, nullptr, nullptr);
  return t;
}
QPointF PullTool::handleBase() const {
  QPointF b;
  arrowGeometry(&b, nullptr, nullptr, nullptr);
  return b;
}

bool PullTool::nearArrow(const QPointF& p, bool* onHead) const {
  QPointF b, t;
  if (!arrowGeometry(&b, &t, nullptr, nullptr)) return false;
  const QPointF ab = t - b, ap = p - b;
  const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
  const double s = l2 > 0 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0) : 0;
  const QPointF c = b + ab * s;
  const double d = std::hypot(p.x() - c.x(), p.y() - c.y());
  if (onHead) *onHead = s > 0.66;
  return d < 10.0;
}

double PullTool::stepFor(double ppm) const {
  if (ppm > 40) return 0.1;
  if (ppm > 8) return 0.5;
  if (ppm > 2) return 1.0;
  return 5.0;
}

void PullTool::paintOverlay(QPainter& p, const ViewportOverlay&) {
  if (!handleCount()) return;
  QPointF b, t, u;
  double ppm = 0;
  if (!arrowGeometry(&b, &t, &u, &ppm)) return;
  if (tgt_.kind == Kind::NewRevolve) {
    // revolve: an arc around the axis' screen projection instead of the arrow
    const QPointF ap = toScreen(axis_.Location());
    const QPointF bp = toScreen(gp_Pnt(axis_.Location().XYZ() + axis_.Direction().XYZ() * 10.0));
    p.save();
    p.setPen(QPen(QColor(230, 120, 20), 1.5, Qt::DashDotLine));
    QPointF ad = bp - ap;
    const double al = std::hypot(ad.x(), ad.y());
    if (al > 1e-6) { ad /= al; p.drawLine(ap - ad * 2000, ap + ad * 2000); }
    p.restore();
    const double r = 44;
    const double startDeg = std::atan2(-(b.y() - ap.y()), b.x() - ap.x()) * 180 / M_PI;
    ViewportOverlay::drawRevolveArc(p, b, r, startDeg, 300, hot_ || st_ == St::Dragging);
    ViewportOverlay::drawTag(p, b + QPointF(0, -r - 14), "Döndür", QColor(230, 120, 20));
    return;
  }
  QPointF tip = t;
  if (st_ == St::Dragging || st_ == St::Pending) {
    // the arrow travels with the dragged value
    const QPointF moved = b + u * (dragValue_ * flip_ * ppm * (flip_ > 0 ? 1 : -1));
    (void)moved;
    tip = b + u * (62.0 + std::clamp(dragValue_ * ppm, -400.0, 400.0));
    if (std::hypot(tip.x() - b.x(), tip.y() - b.y()) < 30) tip = b + u * 30.0;
    ViewportOverlay::drawTag(p, tip + u * 16 + QPointF(0, -14), QString("%1 %2").arg(formatDim(dragValue_), tgt_.unit), QColor(40, 90, 170));
  }
  ViewportOverlay::drawPullArrow(p, b, tip, hot_ || st_ == St::Dragging);
}

void PullTool::openBox(const QString& initial, int field) {
  if (!box_ || !tgt_.valid()) return;
  if (!upToRef_.isEmpty() && guide_ != "ruler") { upToRef_.clear(); clearGhost(); st_ = St::Armed; }   // SC-PULLUPTO: typing replaces Kadar
  if (st_ != St::Typing || box_->fieldCount() == 0) {
    QList<DimensionBox::FieldSpec> f{{"v", tgt_.fieldLabel, tgt_.unit}};
    if (tgt_.delta) f << DimensionBox::FieldSpec{"total", "Toplam", tgt_.unit};
    box_->setFields(f);
    boxLabel_ = tgt_.fieldLabel + "|" + tgt_.unit;
    box_->setStep(1.0);
    QStringList words;
    for (const auto& prm : host_->document().params.all()) words << QString::fromStdString(prm.name);
    box_->setCompletions(words);
    const double v = st_ == St::Pending ? dragValue_ : (tgt_.kind == Kind::NewRevolve ? 360.0 : 0.0);
    box_->setLiveValue(0, tgt_.delta ? v : (tgt_.kind == Kind::NewExtrude || tgt_.kind == Kind::NewRevolve || tgt_.kind == Kind::NewFillet ||
                                                    tgt_.kind == Kind::NewChamfer || tgt_.kind == Kind::FaceExtrude ? v : tgt_.current));
    if (tgt_.delta) box_->setLiveValue(1, tgt_.current + v);
    QPointF tip = handleTip();
    box_->setCompactAnchor(tip.toPoint());
    box_->setVisible(true);
  }
  commitTimer_.stop();
  held_ = false;
  st_ = St::Typing;
  box_->beginEdit(field, initial);
  repaintOverlay();
  reportState();
}

void PullTool::closeBox() {
  if (!box_) return;
  if (box_->isEditing()) box_->stopEdit();
  box_->setVisible(false);
  if (st_ == St::Typing || st_ == St::Pending) st_ = tgt_.valid() ? St::Armed : St::Idle;
  held_ = false;
  if (host_ && host_->viewer()) host_->viewer()->setFocus();
  reportState();
}

void PullTool::clearGhost() {
  if (host_) host_->setGhost(TopoDS_Shape());
}

void PullTool::updateGhost(double value, bool force) {
  if (!host_ || !tgt_.valid()) return;
  if (!force && ghostClock_.isValid() && ghostClock_.elapsed() < 80) return;   // <= 12 Hz
  ghostClock_.restart();
  try {
    TopoDS_Shape g;
    const auto& doc = host_->document();
    const auto& model = host_->modelResult();
    if (tgt_.kind == Kind::NewExtrude && std::fabs(value) > 1e-9) {
      TopoDS_Compound c;
      BRep_Builder bb;
      bb.MakeCompound(c);
      for (const auto& rs : tgt_.regionSets) {   // SC-PULLUPTO: only the selected regions, each along its sketch normal
        std::vector<TopoDS_Face> faces;
        gp_Ax3 fr;
        if (!sketchRegionFaces(doc, model, rs.first.toStdString(), faces, fr)) continue;
        gp_Vec v(fr.Direction());
        v *= value * flip_;
        for (int k : rs.second) {
          if (k < 0 || k >= int(faces.size())) continue;
          TopoDS_Shape base = faces[size_t(k)];
          if (opt_.bothSides) {
            gp_Trsf tr;
            tr.SetTranslation(-v / 2);
            base = BRepBuilderAPI_Transform(base, tr, true).Shape();
          }
          bb.Add(c, BRepPrimAPI_MakePrism(base, v).Shape());
        }
      }
      g = c;
    } else if (tgt_.kind == Kind::NewRevolve && haveAxis_ && std::fabs(value) > 1e-9) {
      std::vector<TopoDS_Face> faces;
      gp_Ax3 fr;
      if (sketchRegionFaces(doc, model, tgt_.sketchId.toStdString(), faces, fr)) {
        TopoDS_Compound c;
        BRep_Builder bb;
        bb.MakeCompound(c);
        const double ang = std::min(360.0, std::fabs(value)) * M_PI / 180.0;
        const QList<int> sel = tgt_.regionSets.isEmpty() ? QList<int>() : tgt_.regionSets.first().second;   // SC-PULLUPTO
        for (int k = 0; k < int(faces.size()); ++k)
          if (sel.isEmpty() || sel.contains(k)) bb.Add(c, BRepPrimAPI_MakeRevol(faces[size_t(k)], axis_, ang).Shape());
        g = c;
      }
    } else if ((tgt_.kind == Kind::ExtrudeEnd || tgt_.kind == Kind::ExtrudeStart || tgt_.kind == Kind::FaceExtrude) && std::fabs(value) > 1e-9) {
      TopoDS_Face f;
      if (faceByName(doc, model, tgt_.face, f)) {
        gp_Vec v(dir_);
        v *= value * flip_;
        g = BRepPrimAPI_MakePrism(f, v).Shape();
      }
    }
    host_->setGhost(g);
  } catch (...) {
  }
}

bool PullTool::viewerMousePress(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !handleCount() || e->modifiers() != Qt::NoModifier) return false;
  bool head = false;
  if (!nearArrow(e->position(), &head)) return false;
  pressed_ = true;
  dragged_ = false;
  pressOnHead_ = head;
  pressPos_ = e->pos();
  return true;
}

bool PullTool::viewerMouseMove(QMouseEvent* e) {
  if (!pressed_) {
    const bool h = handleCount() && nearArrow(e->position(), nullptr);
    if (h != hot_) { hot_ = h; repaintOverlay(); }
    return false;
  }
  if (!dragged_ && (e->pos() - pressPos_).manhattanLength() <= 3) return true;
  if (!dragged_) {
    dragged_ = true;
    if (!upToRef_.isEmpty()) { upToRef_.clear(); if (box_) box_->setVisible(false); }   // SC-PULLUPTO: a drag replaces it
    commitTimer_.stop();
    if (box_ && box_->isEditing()) box_->stopEdit();
    st_ = St::Dragging;
  }
  QPointF b, u;
  double ppm = 0;
  arrowGeometry(&b, nullptr, &u, &ppm);
  const QPointF d = e->position() - QPointF(pressPos_);
  const double px = d.x() * u.x() + d.y() * u.y();
  const double step = stepFor(ppm);
  double v = ppm > 1e-3 ? px / ppm : -d.y() * 0.5;
  v = std::round(v / step) * step;
  if (tgt_.kind == Kind::NewRevolve) v = std::clamp(std::round(std::fabs(px) / 2.0) * 5.0, 5.0, 360.0);
  dragValue_ = v;
  updateGhost(v);
  if (box_) {
    // the box may still carry the fields of an earlier target (e.g. "Açı" of a revolve): always this target's fields
    const bool stale = box_->fieldCount() != (tgt_.delta ? 2 : 1) || box_->fieldKey(0) != "v" || boxLabel_ != tgt_.fieldLabel + "|" + tgt_.unit;
    if (stale && !box_->isEditing()) {
      QList<DimensionBox::FieldSpec> f{{"v", tgt_.fieldLabel, tgt_.unit}};
      if (tgt_.delta) f << DimensionBox::FieldSpec{"total", "Toplam", tgt_.unit};
      box_->setFields(f);
      boxLabel_ = tgt_.fieldLabel + "|" + tgt_.unit;
    }
    box_->setLiveValue(0, v);
    if (tgt_.delta) box_->setLiveValue(1, tgt_.current + v);
    box_->setCompactAnchor(e->pos());
    box_->setVisible(true);
  }
  setPrompt(QString("Çek: %1 = %2 %3 (bırakın; 600 ms sonra uygulanır, yazmak için rakam / Tab)").arg(tgt_.fieldLabel, formatDim(v), tgt_.unit));
  repaintOverlay();
  errReason_.clear();
  held_ = false;
  reportState();
  return true;
}

bool PullTool::viewerMouseRelease(QMouseEvent* e) {
  if (!pressed_ || e->button() != Qt::LeftButton) return false;
  pressed_ = false;
  if (!dragged_) {
    if (pressOnHead_ && tgt_.kind != Kind::NewRevolve) {
      flip_ = -flip_;
      setPrompt("Çek: yön tersine çevrildi.");
      repaintOverlay();
    }
    return true;
  }
  dragged_ = false;
  if (std::fabs(dragValue_) < 1e-9) {
    st_ = St::Armed;
    clearGhost();
    if (box_) box_->setVisible(false);
    repaintOverlay();
    reportState();
    return true;
  }
  updateGhost(dragValue_, true);
  st_ = St::Pending;
  held_ = false;
  if (applyOnRelease_) commitValue(QString::fromStdString(pc::formatNumber(dragValue_)));
  else commitTimer_.start();
  repaintOverlay();
  reportState();
  return true;
}

bool PullTool::wantsKey(const QKeyEvent* e) const {
  if (!host_) return false;
  const int k = e->key();
  const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
  if (st_ == St::Typing) return true;
  if (tgt_.kind == Kind::None) return false;
  if (m & (Qt::ControlModifier | Qt::AltModifier)) return false;
  const QString t = e->text();
  if (!t.isEmpty() && (t[0].isDigit() || t[0] == '.' || t[0] == '-' || t[0] == '(' || t[0] == ',')) return true;
  if (k == Qt::Key_Space) return tgt_.valid();
  if (k == Qt::Key_U && m == Qt::NoModifier) return pull::supportsUpTo(tgt_);   // SC-PULLUPTO: Kadar guide
  if ((k == Qt::Key_Return || k == Qt::Key_Enter) && st_ == St::Pending) return true;
  if ((k == Qt::Key_Tab || k == Qt::Key_Backtab) && st_ == St::Pending) return true;
  return false;
}

bool PullTool::keyPress(QKeyEvent* e) {
  if (!host_) return false;
  const int k = e->key();
  if (st_ == St::Typing && box_ && box_->isEditing()) {
    static bool forwarding = false;   // keys the line edit ignores propagate back to the viewer: do not loop
    if (forwarding) return true;
    if (QLineEdit* le = box_->lineEdit(box_->editingField())) {
      QKeyEvent copy(QEvent::KeyPress, k, e->modifiers(), e->text());
      forwarding = true;
      QCoreApplication::sendEvent(le, &copy);
      forwarding = false;
      return true;
    }
  }
  if (!tgt_.valid()) {
    if (!tgt_.refusal.isEmpty()) {
      refuse(tgt_.refusal.startsWith("Çek") ? tgt_.refusal : "Çek: " + tgt_.refusal);
      return true;
    }
    return false;
  }
  if (k == Qt::Key_U && (e->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {   // SC-PULLUPTO
    setActiveGuide("upto");
    return true;
  }
  if ((k == Qt::Key_Return || k == Qt::Key_Enter) && st_ == St::Pending && !upToRef_.isEmpty()) {   // SC-PULLUPTO
    commitUpTo();
    return true;
  }
  if ((k == Qt::Key_Return || k == Qt::Key_Enter) && st_ == St::Pending) {
    commitTimer_.stop();
    held_ = false;
    commitValue(QString::fromStdString(pc::formatNumber(dragValue_)));
    return true;
  }
  if ((k == Qt::Key_Tab || k == Qt::Key_Backtab) && st_ == St::Pending) {
    openBox(QString(), k == Qt::Key_Tab ? (tgt_.delta ? 1 : 0) : 0);
    return true;
  }
  if (k == Qt::Key_Space) { openBox(QString()); return true; }
  QString t = e->text();
  if (t == ",") t = ".";
  if (!t.isEmpty()) { openBox(t); return true; }
  return false;
}

bool PullTool::cancel() {
  if (!upToRef_.isEmpty() || ((guide_ == "upto" || guide_ == "ruler") && (st_ == St::Idle || st_ == St::Armed))) {   // SC-PULLUPTO
    const bool had = !upToRef_.isEmpty();
    clearUpTo();
    if (had) { setPrompt("Çek: Kadar / Cetvel hedefi bırakıldı (yeni hedef tıklayın ya da Esc)."); return true; }
    guide_ = "select";
    emit guidesChanged();
    armedPrompt();
    return true;
  }
  if (st_ == St::Typing) {
    gate_.reset();
    errReason_.clear();
    closeBox();
    clearGhost();
    setPrompt("Çek: değer girişi iptal edildi.");
    emitMomentary(OpState::Cancelled, prompt());
    return true;
  }
  if (st_ == St::Pending || st_ == St::Dragging) {
    commitTimer_.stop();
    pressed_ = dragged_ = false;
    held_ = false;
    errReason_.clear();
    closeBox();
    clearGhost();
    st_ = tgt_.valid() ? St::Armed : St::Idle;
    repaintOverlay();
    setPrompt("Çek: sürükleme iptal edildi.");
    reportState();
    emitMomentary(OpState::Cancelled, prompt());
    return true;
  }
  if (!errReason_.isEmpty()) {   // SC23: Esc dismisses a refusal (the red HUD message)
    errReason_.clear();
    gate_.reset();
    armedPrompt();
    reportState();
    return true;
  }
  if (!opt_.revolveAxis.isEmpty()) {
    opt_.revolveAxis.clear();
    haveAxis_ = false;
    guide_ = "select";
    emit guidesChanged();
    retarget();
    return true;
  }
  return false;
}

bool PullTool::pick(const PickEvent& e) {
  if (!host_ || e.button != Qt::LeftButton) return false;
  const SelItem it = normalize(e.item);
  if (it.kind == SelItem::Kind::SketchCurve && ((e.modifiers & Qt::AltModifier) || guide_ == "revolve")) {
    std::string sid;
    int eid = -1;
    parseSketchItemName(it.name, &sid, nullptr, &eid);
    const auto& model = host_->modelResult();
    auto sk = model.solvedSketches.find(sid);
    const pc::SkEntity* en = sk != model.solvedSketches.end() ? sk->second.entity(eid) : nullptr;
    // the region of this sketch must be selected first (or is selected implicitly when the sketch has one)
    if (tgt_.sketchId.toStdString() != sid) {
      std::vector<TopoDS_Face> faces;
      gp_Ax3 fr;
      if (sketchRegionFaces(host_->document(), model, sid, faces, fr) && !faces.empty())
        host_->selection()->set({SelItem{SelItem::Kind::SketchRegion, regionName(sid, 0)}});
    }
    if (!en || en->type != pc::SkEntityType::Line) {
      setPrompt("Çek: dönme ekseni düz bir sketch çizgisi olmalı.");
      return true;
    }
    if (tgt_.sketchId.toStdString() != sid) { setPrompt("Çek: eksen, çekilen bölgenin sketch'inde olmalı."); return true; }
    opt_.revolveAxis = QString::number(eid);
    guide_ = "revolve";
    emit guidesChanged();
    retarget();
    setPrompt(QString("Çek: Döndür — eksen %1. Açı yazın (360 = tam dönüş) ve Enter.").arg(it.name));
    return true;
  }
  // SC-PULLUPTO: with the Kadar / Cetvel guide and something pullable selected, a plain click picks the TARGET
  if ((guide_ == "upto" || guide_ == "ruler") && pull::supportsUpTo(tgt_) && !e.isBackground() &&
      !(e.modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) && e.clickCount == 1)
    return pickUpToTarget(e);
  if (st_ == St::Typing || st_ == St::Pending) { commitTimer_.stop(); closeBox(); clearGhost(); held_ = false; }
  // a new pick ends the "value just pulled" context of Ctrl+G (Grup oluştur = that value as a parameter): with
  // faces picked afterwards Ctrl+G makes a named selection (host), as in SpaceClaim
  lastFid_.clear();
  return selectFromPick(e, true);
}

void PullTool::hover(const SelItem& item) {
  if (host_ && host_->viewer()) host_->viewer()->setPreselection(item.name);
}

bool PullTool::commitValue(const QString& text, bool absolute) {
  if (!host_) return false;
  auto eval = [this](const QString& s, double* v) { return host_->evaluateExpression(s, v); };
  // SC23 (O-P0-3): units, zero, very large values (second Enter), reasons
  auto evalErr = [this](const QString& s, double* v, QString* err) { return host_->evaluateExpression(s, v, err); };
  const ValueInput::Result vr = ValueInput::evaluate(text, evalErr, valueOptions());
  if (!upToRef_.isEmpty() && guide_ == "ruler") {   // SC-PULLUPTO: Cetvel value = gap to the reference
    if (!vr.acceptable()) { refuse("Çek (Cetvel): " + vr.reason); return false; }
    const QString t = vr.expr;
    return commitUpTo(pull::isPlainNumber(t.toStdString()) ? QString::fromStdString(pc::formatNumber(-vr.value)) : "-(" + t + ")");
  }
  if (!vr.acceptable()) { refuse("Çek: " + vr.reason); return false; }
  if (!gate_.pass(vr, text)) {   // |v| > max(1e5 mm, 100 x diagonal): a second Enter with the same text confirms
    refuse("Çek: " + vr.reason);
    return false;
  }
  // the arrow direction (head clicked) negates typed values of new extrusions
  QString value = vr.expr;
  if (flip_ < 0 && (tgt_.kind == Kind::NewExtrude || tgt_.kind == Kind::FaceExtrude || tgt_.delta)) value = "-(" + value + ")";
  const pull::Result r = pull::apply(host_->document(), host_->modelResult(), tgt_, value, absolute, opt_, eval);
  if (!r.ok) {
    refuse(r.refusal.startsWith("Çek") ? r.refusal : "Çek: " + r.refusal);
    return false;
  }
  return commitResult(r);
}

bool PullTool::commitResult(const pull::Result& r) {
  commitTimer_.stop();
  held_ = false;
  errReason_.clear();
  st_ = St::Armed;   // the commit refreshes the document; documentChanged() must not interfere
  if (box_) { if (box_->isEditing()) box_->stopEdit(); box_->setVisible(false); }
  clearGhost();
  setOperation(OpState::Committing, QString(), r.valueExpr, tgt_.unit);   // SC23: re-entry guard state
  if (hostWin_) hostWin_->setProperty("sc23.commitError", QString());
  bool ok = false;
  selfChange_ = true;
  if (!r.paramName.isEmpty()) ok = host_->changeParameter(r.paramName.toStdString(), r.paramExpr.toStdString());
  else if (r.isNewFeature && (r.hasPreFeature || !r.moreFeatures.isEmpty())) {
    // face pull: Sketch(on face) + Extrude as ONE undo step; under a rollback bar insertFeature keeps the bar right
    if (host_->rollbackIndex() < 0) ok = host_->commitDocument(pull::withNewFeatures(host_->document(), r, -1), r.undoText);
    else {
      ok = (!r.hasPreFeature || host_->insertFeature(r.preFeature, r.undoText)) && host_->insertFeature(r.feature, r.undoText);
      for (const auto& m : r.moreFeatures) ok = ok && host_->insertFeature(m, r.undoText);
    }
  } else if (r.isNewFeature) ok = host_->insertFeature(r.feature, r.undoText);
  else ok = host_->commitDocument(r.doc, r.undoText);
  selfChange_ = false;
  if (!ok) {
    pruneSelection();
    retarget();
    // SC23 (O-P0-4): the host says why (failed feature, named); the previous model is kept
    const QString why = hostWin_ ? hostWin_->property("sc23.commitError").toString() : QString();
    refuse(why.isEmpty() ? QString("Çek: değişiklik uygulanmadı (iptal edildi ya da imalat denetimi reddetti).") : why);
    return false;
  }
  lastFid_ = r.featureId;
  lastProp_ = r.prop;
  lastValue_ = r.paramName.isEmpty() ? r.valueExpr : r.paramExpr;
  flip_ = 1.0;
  opt_.revolveAxis.clear();
  haveAxis_ = false;
  upToRef_.clear();
  if (guide_ == "revolve" || guide_ == "upto" || guide_ == "ruler") { guide_ = "select"; emit guidesChanged(); }
  const auto& model = host_->modelResult();
  if (!r.resultFace.isEmpty() && model.faces.count(r.resultFace.toStdString()))
    host_->selection()->set({SelItem{SelItem::Kind::Face, r.resultFace}});
  else {
    pruneSelection();
    retarget();
  }
  setPrompt(r.status);
  host_->showPrompt(r.status, 0);
  gate_.reset();
  reportState();
  emitMomentary(OpState::Completed, r.status);   // SC23: "Extrude1 oluşturuldu" (HUD 2 s), then AwaitingInput
  return true;
}

bool PullTool::createGroup() {
  if (!host_) return false;
  if (box_ && (box_->isEditing() || box_->isVisible()) && tgt_.valid()) {
    const int i = box_->isEditing() ? box_->editingField() : 0;
    double v = 0;
    QString val = box_->text(i);
    if (!box_->value(i, &v)) val = formatDim(box_->liveValue(i));
    const QString fid = tgt_.kind == Kind::NewExtrude || tgt_.kind == Kind::FaceExtrude
                            ? QString::fromStdString(host_->document().uniqueFeatureId(pc::FeatureType::Extrude))
                        : tgt_.kind == Kind::NewRevolve ? QString::fromStdString(host_->document().uniqueFeatureId(pc::FeatureType::Revolve))
                        : tgt_.kind == Kind::NewFillet ? QString::fromStdString(host_->document().uniqueFeatureId(pc::FeatureType::Fillet))
                        : tgt_.kind == Kind::NewChamfer ? QString::fromStdString(host_->document().uniqueFeatureId(pc::FeatureType::Chamfer))
                        : tgt_.featureId;
    const QString prop = tgt_.prop.isEmpty() ? (tgt_.kind == Kind::NewFillet ? "radius" : tgt_.kind == Kind::NewRevolve ? "angle" : "distance") : tgt_.prop;
    selfChange_ = true;   // SC23: the new parameter's commit must not drop the open box
    const QString name = host_->createParameterInteractive(fid + "_" + prop, tgt_.unit == "°" ? pc::ParamKind::Angle : pc::ParamKind::Length,
                                                           val, host_->hostWindow());
    selfChange_ = false;
    if (name.isEmpty()) return true;
    openBox(QString(), i);
    box_->setText(i, name, true);
    setPrompt(QString("Çek: değer %1 parametresine bağlanacak — Enter ile uygulayın.").arg(name));
    return true;
  }
  if (lastFid_.isEmpty()) return false;   // nothing pulled since the last pick: the host decides (named selection / parameter)
  pc::Document cand = host_->document();
  pc::Feature* f = cand.feature(lastFid_.toStdString());
  if (!f) return false;
  const std::string cur = f->type == pc::FeatureType::Sketch ? std::string() : f->prop(lastProp_.toStdString());
  if (!cur.empty() && cand.params.find(cur)) {
    setPrompt(QString("Çek: %1.%2 zaten %3 parametresine bağlı.").arg(lastFid_, lastProp_, QString::fromStdString(cur)));
    return true;
  }
  const QString name = host_->createParameterInteractive(lastFid_ + "_" + lastProp_,
                                                         lastProp_ == "angle" ? pc::ParamKind::Angle : pc::ParamKind::Length,
                                                         QString::fromStdString(cur.empty() ? lastValue_.toStdString() : cur), host_->hostWindow());
  if (name.isEmpty()) return true;
  cand = host_->document();   // now contains the new parameter
  f = cand.feature(lastFid_.toStdString());
  if (!f) return true;
  f->props[lastProp_.toStdString()] = name.toStdString();
  if (host_->commitDocument(cand, QString("Grup oluştur: %1.%2 = %3").arg(lastFid_, lastProp_, name))) {
    lastValue_ = name;
    const QString s = QString("Çek: %1.%2 artık %3 parametresine bağlı (Gruplar).").arg(lastFid_, lastProp_, name);
    setPrompt(s);
    host_->showPrompt(s, 0);
  }
  return true;
}

void PullTool::populateContextMenu(QMenu* menu, const QList<SelItem>& under) {
  if (!host_) return;
  bool edge = false;
  for (const auto& it : under) if (it.kind == SelItem::Kind::Edge && !it.name.startsWith("sketch:")) edge = true;
  if (!edge) return;
  menu->addSeparator();
  QAction* r = menu->addAction("Yuvarlat (Çek)");
  r->setObjectName("actPullRound");
  connect(r, &QAction::triggered, this, [this, under] { setOption("edge", "round"); host_->selection()->set(under); });
  QAction* c = menu->addAction("Pahla (Çek)");
  c->setObjectName("actPullChamfer");
  connect(c, &QAction::triggered, this, [this, under] { setOption("edge", "chamfer"); host_->selection()->set(under); });
}

// ================================================================ SC-PULLUPTO: Kadar (Up To) / Cetvel (Ruler)
void PullTool::clearUpTo() {
  const bool had = !upToRef_.isEmpty();
  upToRef_.clear();
  upToS_ = 0;
  if (had) {
    if (box_) { if (box_->isEditing()) box_->stopEdit(); box_->setVisible(false); }
    clearGhost();
    if (st_ == St::Pending || st_ == St::Typing) st_ = tgt_.valid() ? St::Armed : St::Idle;
    repaintOverlay();
  }
}

QString PullTool::resolveUpToRef(const PickEvent& e, QString* why) const {
  if (!host_) return {};
  const SelItem it = normalize(e.item);
  const auto& model = host_->modelResult();
  const bool havePos = e.viewerPos.x() >= 0 && e.viewerPos.y() >= 0;
  const QPointF click(e.viewerPos);
  constexpr double snapPx = 14.0;   // a click this close to an end point means the point (vertex)
  auto nearest = [&](const std::vector<std::pair<QString, gp_Pnt>>& cands) -> QString {
    if (!havePos) return {};
    QString best;
    double bd = snapPx;
    for (const auto& [ref, p] : cands) {
      const QPointF q = toScreen(p);
      const double d = std::hypot(q.x() - click.x(), q.y() - click.y());
      if (d <= bd) { bd = d; best = ref; }
    }
    return best;
  };
  std::string sid;
  char ty = 0;
  int idx = -1;
  switch (it.kind) {
    case SelItem::Kind::SketchCurve: {
      if (!parseSketchItemName(it.name, &sid, &ty, &idx)) break;
      auto sk = model.solvedSketches.find(sid);
      gp_Ax3 fr;
      if (sk == model.solvedSketches.end() || !sketchFrameOf(model, sid, fr)) break;
      const pc::SkEntity* en = sk->second.entity(idx);
      if (!en) break;
      std::vector<std::pair<QString, gp_Pnt>> c;
      auto add = [&](int pid) {
        if (const pc::SkPoint* p = sk->second.point(pid))
          c.push_back({QString("sketch:%1:p%2").arg(QString::fromStdString(sid)).arg(pid),
                       gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * p->x + fr.YDirection().XYZ() * p->y)});
      };
      if (en->type == pc::SkEntityType::Line && en->points.size() == 2) { add(en->points[0]); add(en->points[1]); }
      if (en->type == pc::SkEntityType::Arc && en->points.size() == 3) { add(en->points[1]); add(en->points[2]); }
      const QString p = nearest(c);
      return p.isEmpty() ? it.name : p;
    }
    case SelItem::Kind::SketchPoint: return it.name;
    case SelItem::Kind::SketchRegion:
      if (parseSketchItemName(it.name, &sid, &ty, &idx)) return QString("sketch:%1:plane").arg(QString::fromStdString(sid));
      break;
    case SelItem::Kind::Sketch: return QString("sketch:%1:plane").arg(it.name);
    case SelItem::Kind::Edge: {
      auto ed = model.edges.find(it.name.toStdString());
      if (ed == model.edges.end()) break;
      std::vector<std::pair<QString, gp_Pnt>> c;
      try {
        BRepAdaptor_Curve cv(ed->second);
        if (cv.Value(cv.FirstParameter()).Distance(cv.Value(cv.LastParameter())) > 1e-7) {
          c.push_back({QString("vtx:%1:0").arg(it.name), cv.Value(cv.FirstParameter())});
          c.push_back({QString("vtx:%1:1").arg(it.name), cv.Value(cv.LastParameter())});
        }
      } catch (...) {
      }
      const QString p = nearest(c);
      return p.isEmpty() ? it.name : p;
    }
    case SelItem::Kind::Face: return it.name;
    case SelItem::Kind::Datum: return "datum:" + it.name;
    default: break;
  }
  if (why) *why = "bu öğe hedef olamaz: köşe, kenar, yüz, düzlem ya da başka bir sketch'in eğrisi / noktası seçin.";
  return {};
}

void PullTool::previewResult(const pull::Result& r) {
  if (!host_) return;
  try {
    const pc::Document cand = r.isNewFeature ? pull::withNewFeatures(host_->document(), r, host_->rollbackIndex()) : r.doc;
    const pc::model::ModelResult m = host_->evaluateCandidate(cand);
    QStringList ids{QString::fromStdString(r.feature.id)};
    if (!r.isNewFeature) ids = {r.featureId};
    for (const auto& f : r.moreFeatures) ids << QString::fromStdString(f.id);
    TopoDS_Compound c;
    BRep_Builder bb;
    bb.MakeCompound(c);
    int n = 0;
    for (const auto& fi : m.info)
      if (ids.contains(QString::fromStdString(fi.featureId)) && !fi.toolShape.IsNull()) { bb.Add(c, fi.toolShape); ++n; }
    host_->setGhost(n ? TopoDS_Shape(c) : TopoDS_Shape());
  } catch (...) {
    host_->setGhost(TopoDS_Shape());
  }
}

bool PullTool::pickUpToTarget(const PickEvent& e) {
  if (!host_) return false;
  QString why;
  const QString ref = resolveUpToRef(e, &why);
  auto refuse = [&](const QString& m) {
    const QString msg = "Çek (Kadar): " + m;
    setPrompt(msg);
    host_->showPrompt(msg, 0);
    return true;
  };
  if (ref.isEmpty()) return refuse(why);
  double s = 0;
  QString err;
  if (!pull::upToPreview(host_->document(), host_->modelResult(), tgt_, ref, &s, &err)) return refuse(err);
  commitTimer_.stop();
  if (box_ && box_->isEditing()) box_->stopEdit();
  upToRef_ = ref;
  upToS_ = s;
  const QString label = QString::fromStdString(pc::model::upToLabel(ref.toStdString()));
  const QString warn = err;   // non-fatal note (non-parallel face)
  if (guide_ == "ruler") {
    // the box shows the current gap between the pulled item's end and the reference; typing sets it
    const double cur = (tgt_.kind == Kind::ExtrudeEnd) ? tgt_.current : (tgt_.kind == Kind::UpToOffset ? std::fabs(s) + tgt_.current : 0.0);
    const double gap = std::fabs(s) - cur;
    if (box_) {
      box_->setFields({{"ruler", "Cetvel", "mm"}});
      boxLabel_ = "Cetvel|mm";
      box_->setStep(1.0);
      box_->setLiveValue(0, gap);
      box_->setCompactAnchor((e.viewerPos.x() >= 0 ? e.viewerPos : handleTip().toPoint()) + QPoint(16, 16));
      box_->setVisible(true);
      st_ = St::Typing;
      box_->beginEdit(0, QString());
    }
    const QString msg = QString("Çek (Cetvel): referans %1 (%2 mm). Uç ile referans arasındaki mesafeyi yazın, Enter (ilişkisel: Kadar + ofset).%3")
                            .arg(label, formatDim(std::fabs(s)), warn.isEmpty() ? QString() : " " + warn);
    setPrompt(msg);
    host_->showPrompt(msg, 0);
    repaintOverlay();
    return true;
  }
  auto eval = [this](const QString& x, double* v) { return host_->evaluateExpression(x, v); };
  const pull::Result r = pull::applyUpTo(host_->document(), host_->modelResult(), tgt_, ref, QString(), opt_, eval);
  if (!r.ok) { upToRef_.clear(); return refuse(r.refusal.startsWith("Çek") ? r.refusal.mid(r.refusal.indexOf(':') + 1).trimmed() : r.refusal); }
  previewResult(r);
  st_ = St::Pending;
  if (box_) {
    box_->setFields({{"v", "Kadar", "mm"}});
    boxLabel_ = "Kadar|mm";
    box_->setLiveValue(0, std::fabs(s));
    box_->setCompactAnchor((e.viewerPos.x() >= 0 ? e.viewerPos : handleTip().toPoint()) + QPoint(16, 16));
    box_->setVisible(true);
  }
  const QString msg = QString("Çek (Kadar): hedef %1 → %2 mm (ilişkisel; hedef değişince güncellenir). Enter = uygula, Esc = iptal, başka hedef tıklanabilir.%3")
                          .arg(label, formatDim(std::fabs(s)), warn.isEmpty() ? QString() : " " + warn);
  setPrompt(msg);
  host_->showPrompt(msg, 0);
  repaintOverlay();
  reportState();
  return true;
}

bool PullTool::commitUpTo(const QString& offsetExpr) {
  if (!host_ || upToRef_.isEmpty()) return false;
  auto eval = [this](const QString& x, double* v) { return host_->evaluateExpression(x, v); };
  const pull::Result r = pull::applyUpTo(host_->document(), host_->modelResult(), tgt_, upToRef_, offsetExpr, opt_, eval);
  if (!r.ok) {
    refuse(r.refusal.startsWith("Çek") ? r.refusal : "Çek: " + r.refusal);
    return false;
  }
  return commitResult(r);
}

// ================================================================ SC23 (WP-O): operation state, event contract
ValueInput::Options PullTool::valueOptions() const {
  ValueInput::Options o;
  const QString u = tgt_.unit;
  o.quantity = u == "°" ? ValueInput::Quantity::Angle : (u.isEmpty() || u == "mm") ? ValueInput::Quantity::Length : ValueInput::Quantity::Scalar;
  o.what = tgt_.fieldLabel.isEmpty() ? QString() : tgt_.fieldLabel;
  if (!upToRef_.isEmpty() && guide_ == "ruler") { o.quantity = ValueInput::Quantity::Length; o.allowZero = true; o.what = "Cetvel"; }
  if (host_) {
    try {
      const TopoDS_Shape& sh = host_->modelResult().shape;
      if (!sh.IsNull()) {
        Bnd_Box bb;
        BRepBndLib::Add(sh, bb);
        if (!bb.IsVoid()) o.modelDiagonal = std::sqrt(bb.SquareExtent());
      }
    } catch (...) {
    }
  }
  return o;
}

QStringList PullTool::secondaryRefs() const {
  QStringList out;
  if (!opt_.revolveAxis.isEmpty() && !tgt_.sketchId.isEmpty())
    out << QString("Döndürme ekseni: %1").arg(sketchCurveName(tgt_.sketchId.toStdString(), opt_.revolveAxis.toInt()));
  if (!upToRef_.isEmpty())
    out << QString("%1 hedefi: %2").arg(guide_ == "ruler" ? "Cetvel" : "Kadar", QString::fromStdString(pc::model::upToLabel(upToRef_.toStdString())));
  return out;
}

void PullTool::refuse(const QString& msg) {
  errReason_ = msg;
  setPrompt(msg);
  if (host_) host_->showPrompt(msg, 0);
  reportState();
}

void PullTool::reportState() {
  if (!host_) { setOperation(OpState::Idle); return; }
  const QString unit = tgt_.unit.isEmpty() ? QString::fromLatin1(kDocumentLengthUnit) : tgt_.unit;
  QString value;
  if (st_ == St::Typing && box_ && box_->isEditing()) value = box_->text(box_->editingField());
  else if (st_ == St::Dragging || st_ == St::Pending) value = !upToRef_.isEmpty() ? formatDim(std::fabs(upToS_)) : formatDim(dragValue_);
  // secondary references (O-P1-4): OpStatus (frozen) has no field for them; a change re-emits the status so that
  // listeners re-read the "secondaryRefs" property
  const QStringList sec = secondaryRefs();
  struct Resend {
    PullTool* t; QStringList sec; bool before;
    ~Resend() { if (sec != t->lastSecondary_) { t->lastSecondary_ = sec; emit t->operationChanged(t->operation()); } }
  } resend{this, sec, false};
  if (!errReason_.isEmpty()) { setOperation(OpState::Invalid, errReason_, value, unit); return; }
  if (tgt_.kind == Kind::None) { setOperation(OpState::AwaitingSelection); return; }
  if (!tgt_.valid()) {
    setOperation(OpState::Invalid, tgt_.refusal.startsWith("Çek") ? tgt_.refusal : "Çek: " + tgt_.refusal);
    return;
  }
  switch (st_) {
    case St::Dragging:
    case St::Pending:
      setOperation(OpState::Preview,
                   held_ ? QString("Çek: önizleme bekletiliyor (odak değişti) — Enter uygular, Esc iptal eder.") : QString(), value, unit);
      return;
    case St::Typing: {
      double v = 0;
      const bool ok = box_ && box_->isEditing() && box_->value(box_->editingField(), &v);
      setOperation(ok ? OpState::Preview : OpState::AwaitingInput, QString(), value, unit);
      return;
    }
    default:
      setOperation(OpState::AwaitingInput, QString(), QString(), unit);
      return;
  }
}

void PullTool::holdPreview() {
  if (!commitTimer_.isActive()) return;
  commitTimer_.stop();   // no implicit commit (ANALYSIS §4.4); the ghost and the value stay
  held_ = true;
  setPrompt("Çek: önizleme bekletiliyor (odak değişti) — Enter uygular, Esc iptal eder.");
  reportState();
}

bool PullTool::eventFilter(QObject* o, QEvent* e) {
  if (o == hostWin_ && e->type() == QEvent::WindowDeactivate) holdPreview();
  return ToolBase::eventFilter(o, e);
}

} // namespace pcd
