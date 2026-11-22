// MainWindow: change pipeline, features, sketch mode, parameters.
#include "Icons.h"
#include "MainWindow.h"
#include "Import2DUi.h"   // SC-2D-IMPORT
#include "MfgDialogs.h"
#include "Ribbon.h"
#include "SketchRegions.h"     // SC23 (O-P0-5): End Sketch profile summary
#include "ViewportHud.h"
#include "ViewportOverlay.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>

using namespace pc;

namespace pcd {

namespace {
ParamKind kindFromTextOps(const QString& t) {
  if (t == "mm") return ParamKind::Length;
  if (t == "°") return ParamKind::Angle;
  if (t == "adet") return ParamKind::Count;
  return ParamKind::Scalar;
}
bool isExprKey(const std::string& k) {
  static const std::set<std::string> non = {"sketch", "axis", "direction", "op", "kind", "source",
                                            "targets", "target", "tools", "bodyName", "keepTool", "keepTools"};
  return non.count(k) == 0;
}
bool mentions(const std::string& expr, const std::string& name) {
  try {
    for (const auto& n : referencedNames(expr)) if (n == name) return true;
  } catch (...) {}
  return false;
}
std::vector<std::string> usagesOf(const Document& d, const std::string& name) {
  std::vector<std::string> out;
  for (const auto& p : d.params.all())
    if (p.name != name && mentions(p.expression, name)) out.push_back("parametre " + p.name);
  for (const auto& f : d.features) {
    bool used = false;
    for (const auto& [k, v] : f.props) if (isExprKey(k) && mentions(v, name)) used = true;
    if (f.sketch) {
      if (mentions(f.sketch->plane.offsetExpr, name)) used = true;
      for (const auto& c : f.sketch->constraints) if (!c.valueExpr.empty() && mentions(c.valueExpr, name)) used = true;
    }
    if (used) out.push_back("özellik " + f.id);
  }
  return out;
}
} // namespace

// ------------------------------------------------------------------------------ change pipeline
bool MainWindow::tryCommit(Document cand, const QString& desc) {
  const mfg::Report savedReport = report_;
  mfg::ValidationResult vr;
  setProperty("sc23.commitError", QString());   // SC23 (O-P0-4): why the last commit was refused (read by the tools)
  // Each iteration needs an explicit user decision in the review dialog, so the loop cannot spin;
  // it ends with a clean check, "Yine de Uygula", "Vazgeç" or batch mode.
  for (;;) {
    QString checkErr;
    try {
      vr = mfg::validateChange(cand, {}, cand.mfg);
    } catch (const std::exception& e) {
      checkErr = e.what();
    } catch (...) {   // e.g. OCCT Standard_Failure (not a std::exception)
      checkErr = "bilinmeyen hata";
    }
    if (!checkErr.isEmpty()) {
      QMessageBox::critical(this, "İmalat denetimi", QString("Denetim hatası: %1").arg(checkErr));
      viewer_->clearGhost();
      return false;
    }
    if (!vr.report.hasErrors()) break;

    // SC23 (O-P0-4, ANALYSIS §4.5): a feature the engine cannot build (MODEL.FEATURE_FAILED) or a regeneration that
    // throws (MODEL.REGENERATE) is never committed — not through a modal review, not with "Yine de Uygula", not in
    // autoYes_ mode. The previous model stays; the HUD (red) and the status bar name the feature. Failures the
    // committed model already has are not the candidate's fault and do not block (only NEW failures do); neither
    // do the dependents a confirmed "Sil" accepted.
    {
      std::set<std::string> failedBefore;
      for (const auto& st : model_.status) if (!st.ok) failedBefore.insert(st.featureId);
      for (const QString& a : property("sc23.acceptFailed").toStringList()) failedBefore.insert(a.toStdString());   // deleteFeature
      QStringList msgs;
      QString firstFid;
      for (const auto& is : vr.report.issues) {
        if (is.severity != mfg::Severity::Error) continue;
        const bool regen = is.ruleId == "MODEL.REGENERATE";
        const bool feat = is.ruleId == "MODEL.FEATURE_FAILED" && !failedBefore.count(is.featureId);
        if (!regen && !feat) continue;
        const QString fid = QString::fromStdString(is.featureId);
        QString m = QString::fromStdString(is.message).trimmed();
        if (feat) {
          if (firstFid.isEmpty()) firstFid = fid;
          if (m.isEmpty() || m == fid + " oluşturulamadı.") m = QString("%1 oluşturulamadı.").arg(fid);
          else if (!m.startsWith(fid)) m = QString("%1 oluşturulamadı: %2").arg(fid, m);
        } else if (m.isEmpty()) m = "Model yeniden oluşturulamadı.";
        if (!m.endsWith('.')) m += '.';
        if (!msgs.contains(m)) msgs << m;
      }
      if (!msgs.isEmpty()) {
        QString advice = "Değeri ya da seçenekleri değiştirin.";
        if (const Feature* f = firstFid.isEmpty() ? nullptr : cand.feature(firstFid.toStdString())) {
          if (f->type == FeatureType::Extrude || f->type == FeatureType::Revolve)
            advice = "Mesafeyi küçültün ya da Birleştirme'yi 'Birleştirme yok' yapın.";
          else if (f->type == FeatureType::Fillet || f->type == FeatureType::Chamfer)
            advice = "Yarıçapı / mesafeyi küçültün.";
          else if (f->type == FeatureType::Sketch)
            advice = "Sketch'i düzeltin (kısıtlar, kendini kesen eğriler).";
        }
        const QString why = QString("%1 %2 Önceki model korundu (%3).").arg(msgs.join(' '), advice, desc);
        viewer_->clearGhost();
        report_ = savedReport;
        rebuildIssues();
        setProperty("sc23.commitError", why);
        OpStatus st;
        st.state = OpState::Invalid;
        st.toolId = tools_ ? tools_->activeId() : QString();
        st.reason = why;
        showOperationFeedback(st);
        return false;
      }
    }

    if (autoYes_ && reviewShot_.isEmpty()) break;   // batch / automation mode: never block on a modal dialog
    // Errors: show candidate as ghost + the issues, ask the user.
    if (vr.regenerated && !vr.model.shape.IsNull()) viewer_->setGhost(vr.model.shape);
    report_ = vr.report;
    rebuildIssues();
    ChangeReviewDialog dlg(vr.report, desc, this);
    if (!reviewShot_.isEmpty()) {
      const QString path = reviewShot_;
      reviewShot_.clear();
      QTimer::singleShot(900, &dlg, [this, &dlg, path] {
        dlg.grab().save(path);
        QString mainPath = path;
        mainPath.replace(".png", "_main.png");
        this->grab().save(mainPath);
        dlg.reject();
      });
    }
    dlg.exec();
    if (dlg.choice() == ChangeReviewDialog::Cancel) {
      viewer_->clearGhost();
      report_ = savedReport;
      rebuildIssues();
      return false;
    }
    if (dlg.choice() == ChangeReviewDialog::ForceApply) break;
    // Apply suggestions and re-validate. Several issues may suggest a value for the same parameter
    // (e.g. AM.MIN_WALL T>=0.4 and CNC.MIN_WALL T>=0.8): take the most severe issue's value and,
    // among equally severe ones, the value farthest from the current one (satisfies all limits).
    struct Pick { double value; int sev; double dist; };
    std::map<std::string, Pick> best;
    ParamValues curVals;
    try { curVals = cand.evaluateParams(); } catch (...) {}
    for (const auto& is : vr.report.issues) {
      if (!is.suggestedValue || is.params.empty() || !cand.params.find(is.params[0])) continue;
      // Bulk apply only fixes real problems (Info = recommendations, e.g. standard drill sizes) and
      // never replaces a derived parameter's formula (e.g. Lbore = Hb + T - 4) with a plain number.
      if (is.severity == mfg::Severity::Info) continue;
      try {
        if (!referencedNames(cand.params.find(is.params[0])->expression).empty()) continue;
      } catch (...) { continue; }
      const auto cv = curVals.find(is.params[0]);
      const double dist = cv != curVals.end() ? std::fabs(*is.suggestedValue - cv->second) : 0.0;
      const Pick pk{*is.suggestedValue, int(is.severity), dist};
      auto it = best.find(is.params[0]);
      if (it == best.end() || pk.sev > it->second.sev || (pk.sev == it->second.sev && pk.dist > it->second.dist)) best[is.params[0]] = pk;
    }
    bool changed = false;
    for (const auto& [name, pk] : best) {
      const Parameter* p = cand.params.find(name);
      const std::string e = formatNumber(pk.value);
      if (p && p->expression != e) { cand.params.setExpression(name, e); changed = true; }
    }
    if (!changed) QMessageBox::information(this, "İmalat denetimi", "Uygulanabilir bir öneri bulunamadı.");
  }
  viewer_->clearGhost();
  report_ = vr.report;
  pushCommit(cand, desc, &vr);
  rebuildIssues();
  return true;
}

bool MainWindow::tryParameterChange(const std::string& name, const std::string& expr) {
  Document cand = doc_;
  if (!cand.params.find(name)) return false;
  cand.params.setExpression(name, expr);
  return tryCommit(cand, QString("%1 = %2").arg(QString::fromStdString(name), QString::fromStdString(expr)));
}

// ------------------------------------------------------------------------------ picking
bool MainWindow::isPlanarFace(const QString& name) const {
  auto it = model_.faces.find(name.toStdString());
  if (it == model_.faces.end()) return false;
  try {
    return BRepAdaptor_Surface(it->second).GetType() == GeomAbs_Plane;
  } catch (...) { return false; }
}

SelItem::Kind MainWindow::selKindOfPick(const QString& n, int kind) {
  if (n.startsWith("sketch:")) {
    const QString part = n.section(':', 2, 2);
    if (part.startsWith('r')) return SelItem::Kind::SketchRegion;
    if (part.startsWith('p')) return SelItem::Kind::SketchPoint;
    return SelItem::Kind::SketchCurve;
  }
  if (kind == viewer::ShapeViewer::PickEdge) return SelItem::Kind::Edge;
  if (kind == viewer::ShapeViewer::PickDatum) return SelItem::Kind::Datum;
  return SelItem::Kind::Face;
}

void MainWindow::onViewerPicked(const QString& name, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos) {
  if (import2d_ && import2d_->handlePick(name, kind, mods, clicks, pos)) return;   // SC-2D-IMPORT: view select / anchor pick
  if (clicks >= 2) {
    // double / triple click: host states never use them; the tool gets them (edge loop, body), else nothing
    if (planePick_ || pick_ != Pick::None || featDlg_ || !tools_) return;
    // SpaceClaim: double click on a sketch curve in 3D edits that sketch
    if (name.startsWith("sketch:") && clicks == 2 && !name.section(':', 2, 2).startsWith('r') && !sketching_) {
      PickEvent e{SelItem{selKindOfPick(name, kind), name}, mods, Qt::LeftButton, clicks, pos};
      if (tools_->activeId() != "select" && tools_->handlePick(e)) return;
      editSketch(name.section(':', 1, 1).toStdString());
      return;
    }
    PickEvent e{SelItem{name.isEmpty() ? SelItem::Kind::Face : selKindOfPick(name, kind), name}, mods, Qt::LeftButton, clicks, pos};
    tools_->handlePick(e);
    return;
  }
  if (kind == viewer::ShapeViewer::PickEdge) onEdgePicked(name, mods, clicks, pos);
  else if (kind == viewer::ShapeViewer::PickDatum) onDatumPicked(name, mods, clicks, pos);
  else onFacePicked(name, mods, clicks, pos);
}

void MainWindow::onFacePicked(const QString& name) { onFacePicked(name, QGuiApplication::keyboardModifiers()); }
void MainWindow::onEdgePicked(const QString& name) { onEdgePicked(name, QGuiApplication::keyboardModifiers()); }
void MainWindow::onDatumPicked(const QString& name) { onDatumPicked(name, QGuiApplication::keyboardModifiers()); }

void MainWindow::onFacePicked(const QString& name, Qt::KeyboardModifiers mods, int clicks, QPoint pos) {
  // SC-UX: host-owned special states first (SPEC §7.2 #1), then the active tool, then the legacy behaviour
  // (selection + highlight + status) which also feeds the SelectionModel.
  if (pos.x() < 0) pos = viewer_->mapFromGlobal(QCursor::pos());
  if (planePick_) {
    if (name.isEmpty()) return;
    if (name.startsWith("sketch:") || !isPlanarFace(name)) { statusBar()->showMessage("Seçilen yüz düzlemsel değil; başka bir yüz ya da düzlem seçin.", 5000); return; }
    SketchPlane pl;
    pl.kind = SketchPlaneKind::OnFace;
    pl.faceRef = name.toStdString();
    cancelPlanePick();
    beginSketch(pl);
    return;
  }
  if (name.isEmpty()) {
    selectedFace_.clear();
    if (!pendingTwoEdge_.isEmpty()) {   // background click cancels the pending two-edge plane
      pendingTwoEdge_.clear();
      statusBar()->showMessage("İki kenardan düzlem iptal edildi.", 4000);
      return;
    }
    if (pick_ != Pick::None || featDlg_) { if (!featDlg_) viewer_->clearHighlights(); return; }
    PickEvent e;
    e.modifiers = mods;
    e.clickCount = clicks;
    e.viewerPos = pos;
    if (tools_ && tools_->handlePick(e)) return;
    if (!(mods & Qt::ControlModifier)) selection_->clear();
    viewer_->clearHighlights();
    return;
  }
  if (pick_ == Pick::SketchFace) {
    if (!isPlanarFace(name)) { statusBar()->showMessage("Seçilen yüz düzlemsel değil; başka bir yüz seçin.", 5000); return; }
    pick_ = Pick::None;
    SketchPlane pl;
    pl.kind = SketchPlaneKind::OnFace;
    pl.faceRef = name.toStdString();
    beginSketch(pl);
    return;
  }
  if (featDlg_ && featDlg_->wantsFaces()) { featDlg_->addFaceRef(name); return; }
  const SelItem item{name.startsWith("sketch:") ? SelItem::Kind::SketchRegion : SelItem::Kind::Face, name};
  if (!featDlg_ && tools_) {
    PickEvent e;
    e.item = item;
    e.modifiers = mods;
    e.clickCount = clicks;
    e.viewerPos = pos;
    if (tools_->handlePick(e)) { if (selection_->contains(item)) syncHistoryToFace(name); return; }
  }
  if (!selection_->accepts(item.kind)) {   // selection filter (status bar): e.g. "Kenar" ignores faces
    statusBar()->showMessage(QString("Seçim filtresi '%1': yüz seçilmedi.").arg(selFilterText(selection_->filter())), 3000);
    return;
  }
  const bool add = mods & Qt::ControlModifier;
  selectedFace_ = name;
  if (add) selection_->toggle(item);
  else selection_->set({item});
  if (featDlg_) {
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces({name}, QColor(70, 140, 235));
  }
  statusBar()->showMessage("Seçili yüz: " + name + (isPlanarFace(name) ? "  (düzlemsel: K ile bu yüzde sketch açılır)" : ""), 8000);
  syncHistoryToFace(name);
}

void MainWindow::syncHistoryToFace(const QString& name) {
  if (!name.startsWith("sketch:")) selectedFace_ = name;
  const int dot = name.indexOf('.');
  if (dot <= 0) return;
  const QString fid = name.left(dot);
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    auto* it = tree_->topLevelItem(i);
    if (it->data(0, Qt::UserRole).toString() == fid) {
      tree_->blockSignals(true);
      tree_->clearSelection();
      it->setSelected(true);
      tree_->blockSignals(false);
    }
  }
}

void MainWindow::onEdgePicked(const QString& name, Qt::KeyboardModifiers mods, int clicks, QPoint pos) {
  if (pos.x() < 0) pos = viewer_->mapFromGlobal(QCursor::pos());
  if (planePick_) return;
  if (name.isEmpty()) { onFacePicked(QString(), mods, clicks, pos); return; }
  const bool sc = spaceClaimNavigation();
  if (name.startsWith("sketch:")) {
    if (featDlg_) return;
    if (sc) {
      // SpaceClaim scheme: a plain click on a sketch curve is a pick (tools: Alt+click = revolve axis, Select:
      // curve selection); double click edits the sketch (onViewerPicked)
      if (tools_) {
        PickEvent e{SelItem{SelItem::Kind::SketchCurve, name}, mods, Qt::LeftButton, clicks, pos};
        if (tools_->handlePick(e)) return;
      }
      selection_->set({SelItem{SelItem::Kind::SketchCurve, name}});
      statusBar()->showMessage("Sketch eğrisi: " + name + "  (çift tık / K: sketch'i düzenle)", 6000);
      return;
    }
    // legacy scheme (v6): Ctrl+click on a sketch wire in 3D -> edit that sketch
    const QString sid = name.section(':', 1, 1);
    statusBar()->showMessage("Sketch düzenleniyor: " + sid, 4000);
    enterSketchEdit(sid.toStdString());
    return;
  }
  if (featDlg_ && featDlg_->wantsEdges()) { featDlg_->addEdgeRef(name); return; }
  if (!pendingTwoEdge_.isEmpty()) {   // second edge of "İki kenardan düzlem"
    const QString first = pendingTwoEdge_;
    if (name == first) { statusBar()->showMessage("İkinci kenar birinciden farklı olmalı.", 5000); return; }
    pendingTwoEdge_.clear();
    Feature f;
    f.type = FeatureType::Plane;
    f.id = doc_.uniqueFeatureId(FeatureType::Plane);
    f.props = {{"kind", "twoEdges"}, {"offset", "0"}, {"rotate", "0"}};
    f.refs = {first.toStdString(), name.toStdString()};
    viewer_->setHighlightedEdges({first, name}, QColor(255, 140, 0));
    if (insertFeatureCommit(f, "Ekle: " + QString::fromStdString(f.id)))
      statusBar()->showMessage("İki kenardan düzlem oluşturuldu: " + QString::fromStdString(f.id), 6000);
    return;
  }
  const SelItem item{SelItem::Kind::Edge, name};
  selectedEdge_ = name;
  if (!featDlg_ && tools_) {
    PickEvent e;
    e.item = item;
    e.modifiers = mods;
    e.clickCount = clicks;
    e.viewerPos = pos;
    if (tools_->handlePick(e)) return;
  }
  if (selection_->accepts(item.kind)) {
    if (sc && (mods & Qt::ControlModifier)) selection_->toggle(item);
    else {
      selFromLegacy_ = true;
      selection_->set({item});
      selFromLegacy_ = false;
    }
  }
  viewer_->setHighlightedEdges({name}, QColor(255, 140, 0));
  statusBar()->showMessage("Seçili kenar: " + name + (sc ? "  (sağ tık: kenar menüsü)" : ""), 8000);
  // SPEC §2.11: in the SpaceClaim scheme the edge menu is on the right button only (viewerContextMenu);
  // the legacy scheme keeps the Ctrl+click menu
  if (!sc) showEdgeMenu(name, QCursor::pos());
}

// ------------------------------------------------------------------------------ features
void MainWindow::newFeature(FeatureType t) {
  if (sketching_) return;
  if (t == FeatureType::Sketch) { newSketchFlow(); return; }
  if (t == FeatureType::Combine && model_.bodies.size() < 2) {
    statusBar()->showMessage("Combine için en az iki gövde gerekir (Extrude'da 'Ayrı gövde' seçin).", 8000);
    if (!autoYes_) QMessageBox::information(this, "Combine", "Gövdeleri birleştirmek için modelde en az iki gövde olmalı.\nExtrude/Revolve işleminde 'Ayrı gövde (New body)' seçin.");
    return;
  }
  Feature f;
  f.type = t;
  f.id = doc_.uniqueFeatureId(t);
  if (t == FeatureType::Plane) {
    f.props["kind"] = "edgeNormal";
    if (!selectedEdge_.isEmpty()) f.refs = {selectedEdge_.toStdString()};
  }
  openFeatureDialog(f, true);
}

void MainWindow::newSketchFlow() {
  QStringList items = {"XY düzlemi", "XZ düzlemi", "YZ düzlemi", "Seçili yüzde (OnFace)", "Seçili düzlemde (datum)"};
  bool ok = false;
  {  // the selected datum may be stale (undo, delete, suppress, rollback)
    const Feature* sd = selectedDatum_.isEmpty() ? nullptr : doc_.feature(selectedDatum_.toStdString());
    if (!sd || sd->type != FeatureType::Plane || sd->suppressed || !model_.datumPlanes.count(sd->id)) selectedDatum_.clear();
  }
  const int defIdx = !selectedDatum_.isEmpty() ? 4 : selectedFace_.isEmpty() ? 0 : 3;
  const QString choice = QInputDialog::getItem(this, "Yeni sketch", "Sketch düzlemi:", items, defIdx, false, &ok);
  if (!ok) return;
  SketchPlane pl;
  const int idx = items.indexOf(choice);
  if (idx == 4) {
    std::string pid = selectedDatum_.toStdString();
    if (pid.empty()) {
      std::vector<std::string> planes;
      for (const auto& f : doc_.features)
        if (f.type == FeatureType::Plane && !f.suppressed && model_.datumPlanes.count(f.id)) planes.push_back(f.id);
      if (planes.empty()) {
        statusBar()->showMessage("Henüz datum düzlemi yok: bir kenara Ctrl+tıklayıp 'Bu kenara dik düzlem' seçin veya Ekle > Düzlem.", 9000);
        return;
      }
      if (planes.size() == 1) pid = planes[0];
      else {
        pick_ = Pick::SketchDatum;
        statusBar()->showMessage("Sketch için 3B görünümde bir datum düzlemine tıklayın...", 20000);
        return;
      }
    }
    beginSketchOnPlane(pid);
    return;
  }
  if (idx == 3) {
    if (!selectedFace_.isEmpty() && isPlanarFace(selectedFace_)) {
      pl.kind = SketchPlaneKind::OnFace;
      pl.faceRef = selectedFace_.toStdString();
    } else {
      pick_ = Pick::SketchFace;
      statusBar()->showMessage("Sketch için 3B görünümde düzlemsel bir yüze tıklayın...", 20000);
      return;
    }
  } else {
    pl.kind = idx == 0 ? SketchPlaneKind::XY : idx == 1 ? SketchPlaneKind::XZ : SketchPlaneKind::YZ;
  }
  beginSketch(pl);
}

bool MainWindow::beginSketch(const SketchPlane& plane) {
  if (sketching_) return false;
  cancelPlanePick();
  work_ = doc_;
  Sketch sk = Sketch::create();
  sk.plane = plane;
  editSketchId_ = work_.uniqueFeatureId(FeatureType::Sketch);
  editSketchNew_ = true;
  editSketchIndex_ = rollback_ >= 0 ? rollback_ + 1 : int(work_.features.size());
  newPlane_ = plane;
  sketchEd_->setContext(&work_);
  const char* pn = plane.kind == SketchPlaneKind::XY ? "XY" : plane.kind == SketchPlaneKind::XZ ? "XZ" : plane.kind == SketchPlaneKind::YZ ? "YZ" : plane.kind == SketchPlaneKind::OnPlane ? "düzlem" : "yüz";
  sketchEd_->setSketch(sk, QString("%1 (%2%3)").arg(QString::fromStdString(editSketchId_), pn,
                                                  plane.kind == SketchPlaneKind::OnFace ? ": " + QString::fromStdString(plane.faceRef)
                                                  : plane.kind == SketchPlaneKind::OnPlane ? ": " + QString::fromStdString(plane.planeRef) : QString()));
  setSketchMode(true);
  return true;
}

void MainWindow::beginSketchOn(SketchPlaneKind k) {
  SketchPlane pl;
  pl.kind = k;
  beginSketch(pl);
}

void MainWindow::enterSketchEdit(const std::string& id) {
  const Feature* f = doc_.feature(id);
  if (!f || f->type != FeatureType::Sketch || !f->sketch) return;
  if (sketching_) return;
  work_ = doc_;
  editSketchId_ = id;
  editSketchNew_ = false;
  editSketchIndex_ = doc_.indexOf(id);
  sketchEd_->setContext(&work_);
  QString title = QString::fromStdString(id);
  if (f->sketch->plane.kind == SketchPlaneKind::OnPlane) title += " (düzlem: " + QString::fromStdString(f->sketch->plane.planeRef) + ")";
  sketchEd_->setSketch(*f->sketch, title);
  setSketchMode(true);
}

void MainWindow::setSketchMode(bool on) {
  sketching_ = on;
  if (on) {
    labelSketchId_ = editSketchId_;
    cancelPlanePick();
    if (section_->sectionOn()) section_->setSectionOn(false);
  }
  if (on && sketchOffset_) {
    updatingOffset_ = true;
    sketchOffset_->refreshParams();
    sketchOffset_->setText(QString::fromStdString(sketchEd_->sketch().plane.offsetExpr));
    updatingOffset_ = false;
  }
  updateTitle();
  // SPEC §6.1: the sketch editor is WP-B's transparent overlay on the viewer (no page switch). Its plane frame
  // comes from the model (pc::model::sketchFrame), the other sketches / solids stay visible (solids see-through).
  if (on) {
    selFromLegacy_ = true;
    selection_->clear();
    selFromLegacy_ = false;
    syncSketchFrame();
    sketchEd_->setGeometry(viewer_->rect());
    sketchEd_->show();
    sketchEd_->raise();
    updateViewer(true);            // hides the edited sketch's 3D wires / regions, applies the transparency
    sketchEd_->planView();         // SpaceClaim: look at the sketch plane (V)
    sketchEd_->setFocus();
  } else {
    sketchEd_->hide();
    viewer_->setSketchPlane(false);
    updateViewer(true);
    viewer_->setFocus();
  }
  // ribbon: owned by the shell (MainWindowShell.cpp, syncSketchRibbon on designModeChanged, emitted by
  // notifyDesignMode() below): Sketch tab selected while sketching, sketch groups disabled (not hidden) in 3D.
  setHistoryEnabled(!on);
  insertMenu_->setEnabled(!on);
  for (QAction* a : featureActions_) a->setEnabled(!on);
  actUndo_->setEnabled(on ? true : undo_.canUndo());
  actRedo_->setEnabled(on ? true : undo_.canRedo());
  if (on) {
    if (featDlg_) featDlg_->reject();
    sketchEd_->setFocus();
    statusBar()->showMessage("Sketch modu: araç seçin (L, R, C), çizin, kısıt/ölçü ekleyin; bitince 'Sketch'i Bitir' ya da D.", 8000);
  } else {
    lblDof_->setStyleSheet("");
    lblDof_->setText("Sketch DOF: -");   // no stale value of the closed sketch in 3D
    statusBar()->clearMessage();   // v6: no stale sketch texts ("2 öğe seçili", tool hints) after leaving
    updateStatus();
    viewer_->update();
  }
  notifyDesignMode();
}

void MainWindow::notifyDesignMode() {
  // WP-B #4: the tools are told AFTER designMode() returns the new value
  syncModeActions();
  if (tools_) tools_->onDesignModeChanged(designMode());
  emit designModeChanged(designMode());
}

void MainWindow::syncSketchFrame() {
  if (!sketching_) return;
  double o[3], u[3], v[3], n[3];
  if (openSketchFrame(o, u, v, n)) sketchEd_->setPlaneFrame(o, u, v);
}

pc::Document MainWindow::sketchCandidate() const {
  // ROOT-CAUSE FIX (v6): the candidate is built from the CURRENT document, not from the snapshot taken when the
  // sketch was opened (work_). Parameter-table edits, renames, mfg settings, ... made while sketching used to be
  // reverted by "Sketch'i Bitir". Parameters created in the sketch are committed immediately (onParameterCreated);
  // any the sketch references but the document lacks (e.g. undone meanwhile) are restored from the working copy.
  Document cand = doc_;
  const Sketch& sk = sketchEd_->sketch();
  for (const auto& p : work_.params.all())
    if (!cand.params.find(p.name) && sketchEd_->referencesParameter(p.name)) cand.params.add(p);
  (void)sk;
  return cand;
}

bool MainWindow::resolveOpenSketch(bool allowDiscard) {
  if (!sketching_) return true;
  if (!sketchEd_->modified()) { setSketchMode(false); return true; }
  QMessageBox::StandardButton r = QMessageBox::Save;
  if (!autoYes_) {
    QMessageBox mb(QMessageBox::Question, "Sketch",
                   QString("'%1' sketch'i hâlâ açık ve kaydedilmemiş değişiklikler içeriyor.\n"
                           "Önce sketch bitirilsin mi (ölçüler ve kısıtlar modele işlenir)?").arg(QString::fromStdString(editSketchId_)),
                   QMessageBox::Save | (allowDiscard ? QMessageBox::Discard : QMessageBox::NoButton) | QMessageBox::Cancel, this);
    mb.button(QMessageBox::Save)->setText("Sketch'i Bitir");
    if (allowDiscard) mb.button(QMessageBox::Discard)->setText("Değişiklikleri at");
    mb.button(QMessageBox::Cancel)->setText("Vazgeç");
    r = QMessageBox::StandardButton(mb.exec());
  }
  if (r == QMessageBox::Cancel) return false;
  if (r == QMessageBox::Discard) { setSketchMode(false); return true; }
  finishSketch(true);
  if (sketching_) {
    statusBar()->showMessage("Sketch modele işlenemedi; işlem iptal edildi.", 8000);
    return false;
  }
  return true;
}

void MainWindow::onParameterCreated(const Parameter& p) {
  if (doc_.params.find(p.name)) return;
  Document cand = doc_;
  cand.params.add(p);
  commitNoValidate(cand, "Parametre ekle: " + QString::fromStdString(p.name));
  statusBar()->showMessage(QString("Yeni parametre '%1' parametre tablosuna eklendi.").arg(QString::fromStdString(p.name)), 6000);
}

void sc23ShowSketchSummary(MainWindow& w, const Document& doc, const model::ModelResult& model, QWidget* viewer,
                           const std::string& sid, bool toPull);   // SC23, below

bool MainWindow::finishSketch(bool accept) {
  if (!sketching_) return true;
  if (!accept) {
    if (sketchEd_->modified() && !autoYes_ &&
        QMessageBox::question(this, "Sketch",
                              "Sketch'teki geometri / ölçü / kısıt değişiklikleri atılsın mı?\n"
                              "(Oluşturulan parametreler parametre tablosunda kalır.)") != QMessageBox::Yes)
      return false;
    setSketchMode(false);
    return true;
  }
  const Sketch sk = sketchEd_->sketch();
  Document cand = sketchCandidate();
  if (editSketchNew_) {
    if (sk.entities.empty() && sk.points.size() <= 1) {
      setSketchMode(false);
      statusBar()->showMessage("Boş sketch atlandı.", 4000);
      return true;
    }
    Feature f;
    f.id = editSketchId_;
    f.type = FeatureType::Sketch;
    f.sketch = sk;
    const int idx = std::clamp(editSketchIndex_, 0, int(cand.features.size()));
    cand.features.insert(cand.features.begin() + idx, f);
    if (rollback_ >= 0) rollback_ = idx;
  } else {
    Feature* f = cand.feature(editSketchId_);
    if (!f) { setSketchMode(false); return true; }
    f->sketch = sk;
  }
  // SC23 (O-P1-1): the "Sketch'i Bitir" button continues in Çek (Pull) on the Tasarım tab, as SpaceClaim 2023 R1's
  // End Sketch Editing does [G]; D / K / X / closing / saving keep their behaviour (D -> Seç).
  const bool toPull = sender() && sender() == actFinishSketch_;
  const std::string sid = editSketchId_;
  if (tryCommit(cand, QString("Sketch %1").arg(QString::fromStdString(sid)))) {
    setSketchMode(false);
    if (toPull && tools_) {
      tools_->activate("pull");
      if (ribbon_) ribbon_->setCurrentTab("design");
    }
    sc23ShowSketchSummary(*this, doc_, model_, viewer_, sid, toPull);
    return true;
  }
  return false;
}

namespace {
struct ProfileSummary { QString text; bool warn = false; std::vector<gp_Pnt> openPts; QStringList regions; };
ProfileSummary profileSummary(const Document& doc, const model::ModelResult& model, const std::string& sid, bool toPull) {
  ProfileSummary out;
  const QString qs = QString::fromStdString(sid);
  pc::sketch::RegionResult rr;
  gp_Ax3 fr;
  if (!sketchRegionInfo(doc, model, sid, rr, fr)) {
    out.text = QString("%1: bölgeler değerlendirilemedi (sketch hatalı ya da bastırılmış).").arg(qs);
    out.warn = true;
    return out;
  }
  const int n = int(rr.profile.faces.size());
  int holed = 0, holes = 0;
  for (int h : rr.holes) { holes += h; if (h > 0) ++holed; }
  const int open = int(rr.openPoints.size());
  for (const auto& p : rr.openPoints)
    out.openPts.push_back(gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * p.X() + fr.YDirection().XYZ() * p.Y()));
  for (int k = 0; k < n; ++k) out.regions << regionName(sid, k);
  if (n > 0) {
    out.text = QString("%1: %2 bölge%3 → yüzey").arg(qs).arg(n).arg(holed ? QString(" (%1 delikli, %2 delik)").arg(holed).arg(holes) : QString());
    if (open > 0) out.text += QString("; %1 açık uç kırmızı işaretli — eğri olarak kaldı (Doldur F)").arg(open);
    out.text += toPull ? ". Çek etkin: bölgeye tıklayın, oku sürükleyin ya da mesafeyi yazın." : ". Çek (P) ile katı yapın.";
    out.warn = open > 0;
  } else if (open > 0) {
    out.text = QString("%1: kapalı bölge yok, %2 açık uç — eğri olarak kaldı. Kapatın ya da Doldur (F) ile boşluğu görün.").arg(qs).arg(open);
    out.warn = true;
  } else {
    out.text = QString("%1: kapalı bölge yok — eğri olarak kaldı.").arg(qs);
    out.warn = true;
  }
  return out;
}
} // namespace

// SC23 (O-P0-5, ANALYSIS §4.6): what "Sketch'i Bitir" produced — regions (with holes) become pullable surfaces,
// open ends stay curves and are marked red (Doldur's marker). Status bar + HUD. A free function (MainWindow.h is
// frozen): it only uses MainWindow's public API.
void sc23ShowSketchSummary(MainWindow& w, const Document& doc, const model::ModelResult& model, QWidget* viewer,
                           const std::string& sid, bool toPull) {
  if (sid.empty() || !doc.feature(sid)) return;
  const ProfileSummary ps = profileSummary(doc, model, sid, toPull);
  if (ps.text.isEmpty()) return;
  if (toPull && !ps.regions.isEmpty()) w.setToolEmphasis(ps.regions, {});   // ready to be clicked and pulled
  if (auto* ov = viewer ? viewer->findChild<ViewportOverlay*>("viewportOverlay") : nullptr) {
    if (ps.openPts.empty()) ov->clearMarkers();
    else ov->setGapMarkers(ps.openPts);
  }
  w.statusBar()->showMessage(ps.text);
  if (ViewportHud* hud = w.viewportHud())
    hud->showMessage(ps.text, ps.warn ? ViewportHud::Severity::Warning : ViewportHud::Severity::Info, 10000);
}

void MainWindow::openFeatureEditor(const std::string& id) {
  const Feature* f = doc_.feature(id);
  if (!f) return;
  if (f->type == FeatureType::Sketch) enterSketchEdit(id);
  else openFeatureDialog(*f, false);
}

void MainWindow::openFeatureDialog(const Feature& f, bool isNew) {
  if (featDlg_) featDlg_->reject();
  pendingTwoEdge_.clear();   // a pending "İki kenardan düzlem" pick is abandoned by any dialog
  auto* work = new Document(doc_);
  QStringList bodies;
  // editing an existing feature: only bodies that already exist before it (created by an earlier feature)
  const int fIdx = isNew ? -1 : doc_.indexOf(f.id);
  for (const auto& b : model_.bodies) {
    if (b.createdBy == f.id) continue;
    if (fIdx >= 0 && doc_.indexOf(b.createdBy) > fIdx) continue;
    bodies << QString::fromStdString(b.name);
  }
  auto* dlg = new FeatureDialog(work, f, isNew, this, bodies);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  featDlg_ = dlg;
  dlgWork_ = work;
  connect(dlg, &QObject::destroyed, this, [this, work] { if (dlgWork_ == work) dlgWork_ = nullptr; delete work; });
  connect(dlg, &FeatureDialog::refsChanged, this, [this, dlg] {
    viewer_->clearHighlights();
    viewer_->setHighlightedFaces(dlg->refFaces(), QColor(70, 140, 235));
    viewer_->setHighlightedEdges(dlg->refEdges(), QColor(255, 140, 0));
  });
  {
    // names present when the dialog opened: parameters created later by the dialog's "+P" are merged on OK
    std::set<std::string> openNames;
    for (const auto& p : doc_.params.all()) openNames.insert(p.name);
    connect(dlg, &QDialog::accepted, this, [this, dlg, work, openNames] {
      for (const auto& p : work->params.all())
        if (!openNames.count(p.name)) dlgNewParams_.push_back(p);
      onFeatureDialogAccepted(dlg, work);
      dlgNewParams_.clear();
    });
  }
  connect(dlg, &QDialog::finished, this, [this, dlg] {
    if (featDlg_ == dlg) { featDlg_ = nullptr; dlgWork_ = nullptr; viewer_->clearHighlights(); statusBar()->clearMessage(); }
    if (previewDlg_ == dlg) { previewDlg_ = nullptr; previewWork_ = nullptr; applyDatumPlanes(); }
  });
  if (f.type == FeatureType::Plane) {
    previewDlg_ = dlg;
    previewWork_ = work;
    connect(dlg, &FeatureDialog::changed, this, [this, dlg] { if (previewDlg_ == dlg) previewTimer_->start(); });
    connect(dlg, &FeatureDialog::refsChanged, this, [this, dlg] { if (previewDlg_ == dlg) previewTimer_->start(); });
    QTimer::singleShot(0, this, [this, dlg] { if (previewDlg_ == dlg) updatePlanePreview(); });
  }
  dlg->show();
  const QPoint p = mapToGlobal(QPoint(width() / 2 - 120, 90));
  dlg->move(p);
  statusBar()->showMessage(dlg->wantsFaces() ? "3B görünümde yüz/kenar seçerek listeyi doldurabilirsiniz." : QString(), 6000);
}

bool MainWindow::insertFeatureCommit(const Feature& nf, const QString& desc) {
  if (featDlg_) {   // see showEdgeMenu: the dialog's OK would drop this feature again
    statusBar()->showMessage("Önce açık özellik diyaloğunu kapatın.", 6000);
    return false;
  }
  Document cand = doc_;
  const int savedRollback = rollback_;
  const int idx = rollback_ >= 0 ? rollback_ + 1 : int(cand.features.size());
  cand.features.insert(cand.features.begin() + std::min<int>(idx, int(cand.features.size())), nf);
  if (rollback_ >= 0) rollback_ = idx;
  const bool ok = tryCommit(cand, desc);
  if (!ok) rollback_ = savedRollback;
  return ok;
}

void MainWindow::onFeatureDialogAccepted(FeatureDialog* dlg, Document* work) {
  const Feature nf = dlg->feature();
  const bool isNew = dlg->isNewFeature();
  (void)work;   // stale snapshot: never committed. Merge only this feature + new params into the CURRENT document.
  Document cand = doc_;
  for (const auto& p : dlgNewParams_)
    if (!cand.params.find(p.name)) cand.params.add(p);
  const int savedRollback = rollback_;
  if (isNew) {
    const int idx = rollback_ >= 0 ? rollback_ + 1 : int(cand.features.size());
    cand.features.insert(cand.features.begin() + std::min<int>(idx, int(cand.features.size())), nf);
    if (rollback_ >= 0) rollback_ = idx;
  } else {
    Feature* f = cand.feature(nf.id);
    if (!f) return;
    *f = nf;
  }
  const bool ok = tryCommit(cand, QString("%1 %2").arg(isNew ? "Ekle:" : "Düzenle:", QString::fromStdString(nf.id)));
  if (!ok) {
    rollback_ = savedRollback;
    // keep the user's input: re-open the dialog
    QTimer::singleShot(0, this, [this, nf, isNew] { openFeatureDialog(nf, isNew); });
  }
}

void MainWindow::deleteFeature(const std::string& id) {
  std::vector<std::string> deps;
  for (const auto& f : doc_.features)
    if (f.id != id && (f.prop("sketch") == id || f.prop("source") == id || (f.sketch && f.sketch->plane.planeRef == id))) deps.push_back(f.id);
  if (!deps.empty()) {
    QString l;
    for (const auto& d : deps) l += QString::fromStdString(d) + " ";
    if (!autoYes_ && QMessageBox::question(this, "Sil", QString("'%1' özelliğine bağlı özellikler var: %2\nYine de silinsin mi? (bağlı özellikler hata verebilir)").arg(QString::fromStdString(id), l)) != QMessageBox::Yes) return;
  }
  Document cand = doc_;
  const int i = cand.indexOf(id);
  if (i < 0) return;
  cand.features.erase(cand.features.begin() + i);
  if (rollback_ >= i) rollback_ = rollback_ > i ? rollback_ - 1 : -1;
  // SC23 (O-P0-4): the dependents the user confirmed above ("bağlı özellikler hata verebilir") may fail; any other
  // failure still refuses the deletion
  QStringList accepted;
  for (const auto& d : deps) accepted << QString::fromStdString(d);
  setProperty("sc23.acceptFailed", accepted);
  tryCommit(cand, "Sil: " + QString::fromStdString(id));
  setProperty("sc23.acceptFailed", QVariant());
}

void MainWindow::toggleSuppress(const std::string& id) {
  Document cand = doc_;
  Feature* f = cand.feature(id);
  if (!f) return;
  f->suppressed = !f->suppressed;
  tryCommit(cand, QString(f->suppressed ? "Bastır: " : "Etkinleştir: ") + QString::fromStdString(id));
}

void MainWindow::rollbackTo(int idx) {
  rollback_ = (idx >= int(doc_.features.size()) - 1) ? -1 : idx;
  regenerateModel();
  refreshAll(true);
  statusBar()->showMessage(rollback_ >= 0 ? "Model geri sarıldı: " + QString::fromStdString(doc_.features[size_t(rollback_)].id) + " özelliğine kadar" : "Geri sarma kaldırıldı", 5000);
}

void MainWindow::treeContextMenu(const QPoint& pos) {
  if (sketching_) return;
  QTreeWidgetItem* it = tree_->itemAt(pos);
  QMenu m(this);
  std::string id;
  if (it) {
    id = it->data(0, Qt::UserRole).toString().toStdString();
    const Feature* f = doc_.feature(id);
    m.addAction("Düzenle", this, [this, id] { openFeatureEditor(id); });
    {  // v6: which parameters drive this feature
      QMenu* pm = m.addMenu("Parametreleri göster");
      pm->setObjectName("menuFeatureParams");
      const QStringList lines = featureParameterSummary(QString::fromStdString(id)).split('\n', Qt::SkipEmptyParts);
      if (lines.isEmpty()) pm->addAction("(bu özellik parametre kullanmıyor)")->setEnabled(false);
      for (const QString& l : lines) {
        const QString name = l.section(' ', 0, 0);
        pm->addAction(l, this, [this, name] { selectParamRow(name); });
      }
    }
    m.addAction(f && f->suppressed ? "Etkinleştir" : "Bastır", this, [this, id] { toggleSuppress(id); });
    if (f && f->type == FeatureType::Sketch) {   // v6: sketch wires in the 3D view
      QAction* a = m.addAction("3B görünümde göster");
      a->setCheckable(true);
      a->setChecked(sketchShownIn3D(id));
      connect(a, &QAction::toggled, this, [this, id](bool on) { setSketchShownIn3D(id, on); });
    }
    m.addAction("Sil", this, [this, id] { deleteFeature(id); });
    m.addSeparator();
    const int idx = doc_.indexOf(id);
    m.addAction("Buraya kadar geri sar", this, [this, idx] { rollbackTo(idx); });
  }
  if (rollback_ >= 0) m.addAction("Geri sarmayı kaldır (tüm özellikler)", this, [this] { rollbackTo(int(doc_.features.size()) - 1); });
  if (!m.isEmpty()) m.exec(tree_->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------------------ parameters
bool MainWindow::renameParameter(Document& d, const std::string& from, const std::string& to) {
  if (!ParamTable::isValidName(to) || d.params.find(to)) return false;
  if (!d.params.rename(from, to)) return false;
  auto rw = [&](std::string& s) {
    try { s = renameIdentifier(s, from, to); } catch (...) {}
  };
  for (auto& f : d.features) {
    for (auto& [k, v] : f.props) if (isExprKey(k)) rw(v);
    if (f.sketch) {
      rw(f.sketch->plane.offsetExpr);
      for (auto& c : f.sketch->constraints) if (!c.valueExpr.empty()) rw(c.valueExpr);
    }
  }
  return true;
}

bool MainWindow::renameParam(const std::string& from, const std::string& to) {
  Document cand = doc_;
  if (!renameParameter(cand, from, to)) return false;
  if (sketching_) sketchEd_->renameParameter(from, to);   // before the commit re-solves the open sketch
  const bool ok = tryCommit(cand, QString("Yeniden adlandır: %1 -> %2").arg(QString::fromStdString(from), QString::fromStdString(to)));
  if (!ok && sketching_) sketchEd_->renameParameter(to, from);
  return ok;
}

void MainWindow::addParameter() {
  Document cand = doc_;
  std::string name;
  for (int i = 1;; ++i) { name = "P" + std::to_string(i); if (!cand.params.find(name)) break; }
  Parameter p;
  p.name = name;
  p.expression = "10";
  cand.params.add(p);
  commitNoValidate(cand, "Parametre ekle: " + QString::fromStdString(name));
}

void MainWindow::removeParameter() {
  const auto idx = paramView_->currentIndex();
  if (!idx.isValid()) return;
  const std::string name = paramModel_->item(idx.row(), 0)->data(Qt::UserRole).toString().toStdString();
  auto uses = usagesOf(doc_, name);
  if (sketching_ && sketchEd_->referencesParameter(name)) uses.push_back("açık sketch " + editSketchId_);
  if (!uses.empty()) {
    QString l;
    for (const auto& u : uses) l += "\n  - " + QString::fromStdString(u);
    QMessageBox::warning(this, "Parametre sil", QString("'%1' parametresi kullanımda, silinemez:%2").arg(QString::fromStdString(name), l));
    return;
  }
  Document cand = doc_;
  cand.params.remove(name);
  commitNoValidate(cand, "Parametre sil: " + QString::fromStdString(name));
}

void MainWindow::onParamItemChanged(QStandardItem* it) {
  if (updatingParams_ || !it) return;
  const int row = it->row(), col = it->column();
  const QString oldName = paramModel_->item(row, 0)->data(Qt::UserRole).toString();
  const QString text = it->text().trimmed();
  const bool checked = it->checkState() == Qt::Checked;
  QTimer::singleShot(0, this, [this, row, col, oldName, text, checked] {
    (void)row;
    Document cand = doc_;
    Parameter* p = cand.params.find(oldName.toStdString());
    if (!p) { rebuildParams(); return; }
    const std::string old = oldName.toStdString();
    bool validate = false, ok = true, renamedInSketch = false;
    QString desc = "Parametre " + oldName;
    switch (col) {
      case 0: {
        if (text.toStdString() == old) { rebuildParams(); return; }
        ok = renameParameter(cand, old, text.toStdString());
        if (!ok) QMessageBox::warning(this, "Parametre", "Geçersiz veya zaten var olan parametre adı.");
        else if (sketching_) { renamedInSketch = true; sketchEd_->renameParameter(old, text.toStdString()); }
        validate = true;
        desc = QString("Yeniden adlandır: %1 -> %2").arg(oldName, text);
        break;
      }
      case 1:
        if (text.toStdString() == p->expression) { rebuildParams(); return; }
        p->expression = text.toStdString();
        validate = true;
        desc = QString("%1 = %2").arg(oldName, text);
        break;
      case 3: p->kind = kindFromTextOps(text); break;
      case 4: case 5: {
        double v = col == 4 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        if (!text.isEmpty()) {
          bool k = false;
          v = text.toDouble(&k);
          if (!k) { ok = false; break; }
        }
        (col == 4 ? p->minValue : p->maxValue) = v;
        validate = true;
        break;
      }
      case 6: p->exposed = checked; break;
      case 7: p->label = text.toStdString(); break;
      case 8: p->group = text.toStdString(); break;
      case 9: p->description = text.toStdString(); break;
      default: ok = false;
    }
    if (!ok) { rebuildParams(); return; }
    bool done;
    if (validate) done = tryCommit(cand, desc);
    else done = commitNoValidate(cand, desc);
    if (!done) {
      if (renamedInSketch) sketchEd_->renameParameter(text.toStdString(), old);
      rebuildParams();
    }
  });
}

// ------------------------------------------------------------------------------ v6: parameter usage
QString MainWindow::featureParameterSummary(const QString& featureId) const {
  const Feature* f = doc_.feature(featureId.toStdString());
  if (!f) return QString();
  // direct references: name -> where
  std::map<std::string, std::set<std::string>> direct;
  auto scan = [&](const std::string& expr, const std::string& where) {
    try {
      for (const auto& n : referencedNames(expr)) if (doc_.params.find(n)) direct[n].insert(where);
    } catch (...) {}
  };
  for (const auto& [k, v] : f->props) if (isExprKey(k)) scan(v, k);
  const Sketch* sk = f->sketch ? &*f->sketch : nullptr;
  if (sketching_ && f->id == editSketchId_) sk = &sketchEd_->sketch();
  if (sk) {
    scan(sk->plane.offsetExpr, "düzlem ofseti");
    for (const auto& c : sk->constraints)
      if (!c.valueExpr.empty()) scan(c.valueExpr, std::string(toString(c.type)) + " #" + std::to_string(c.id));
  }
  // indirect: parameters used by the formulas of the direct ones
  std::map<std::string, std::string> all;
  std::vector<std::string> stack;
  for (const auto& [n, w] : direct) {
    std::string where;
    for (const auto& x : w) where += (where.empty() ? "" : ", ") + x;
    all[n] = where;
    stack.push_back(n);
  }
  while (!stack.empty()) {
    const std::string n = stack.back();
    stack.pop_back();
    const Parameter* p = doc_.params.find(n);
    if (!p) continue;
    try {
      for (const auto& m : referencedNames(p->expression))
        if (doc_.params.find(m) && !all.count(m)) { all[m] = "dolaylı: " + n; stack.push_back(m); }
    } catch (...) {}
  }
  ParamValues vals;
  try { vals = doc_.evaluateParams(); } catch (...) {}
  QStringList out;
  for (const auto& [n, where] : all) {
    const Parameter* p = doc_.params.find(n);
    auto it = vals.find(n);
    const QString u = p ? paramUnitText(p->kind) : QString();
    out << QString("%1 = %2%3   [%4]%5")
               .arg(QString::fromStdString(n), it != vals.end() ? QString::number(it->second, 'g', 7) : QString("?"),
                    u == "-" ? QString() : " " + u, QString::fromStdString(where), p && p->exposed ? "  (uygulamada)" : "");
  }
  return out.join('\n');
}

void MainWindow::selectParamRow(const QString& name) {
  for (int r = 0; r < paramModel_->rowCount(); ++r)
    if (paramModel_->item(r, 0) && paramModel_->item(r, 0)->data(Qt::UserRole).toString() == name) {
      paramView_->selectRow(r);
      paramView_->scrollTo(paramModel_->index(r, 0));
      paramView_->setFocus();
      statusBar()->showMessage("Parametre: " + name + " (tabloda değiştirince model yenilenir)", 5000);
      return;
    }
}

void MainWindow::applySuggestion(int row) {
  if (row < 0 || row >= int(report_.issues.size())) return;
  const auto& is = report_.issues[size_t(row)];
  if (!is.suggestedValue || is.params.empty()) return;
  Document cand = doc_;
  if (!cand.params.find(is.params[0])) return;
  bool derived = false;
  try { derived = !referencedNames(cand.params.find(is.params[0])->expression).empty(); } catch (...) {}
  if (derived &&
      QMessageBox::question(this, "Öneriyi uygula",
                            QString("%1 bir formülle tanımlı (%2). Formül sabit %3 değeriyle değiştirilsin mi?")
                                .arg(QString::fromStdString(is.params[0]))
                                .arg(QString::fromStdString(cand.params.find(is.params[0])->expression))
                                .arg(*is.suggestedValue, 0, 'g', 6)) != QMessageBox::Yes)
    return;
  cand.params.setExpression(is.params[0], formatNumber(*is.suggestedValue));
  tryCommit(cand, QString("Öneri: %1 = %2").arg(QString::fromStdString(is.params[0])).arg(*is.suggestedValue, 0, 'g', 6));
}

} // namespace pcd
