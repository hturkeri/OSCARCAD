// SC23 (WP-O): the Structure ("Yapı") panel content. MainWindow::rebuildStructure() moved here UNCHANGED from
// MainWindowShell.cpp in P0-SC23 (docs/sc2023/ANALYSIS.md §5.2 #5); WP-O extends it (Yüzeyler / Eğriler / Gövdeler).
#include "MainWindow.h"
#include "SketchRegions.h"
#include "StructurePanel.h"
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <QColor>
#include <QFileInfo>
#include <TopoDS_Wire.hxx>
#include <map>

using namespace pc;

namespace pcd {

void MainWindow::rebuildStructure() {
  if (!structure_) return;
  QList<StructurePanel::BodyRow> bodies;
  const bool multi = model_.bodies.size() > 1;
  static const QColor pal[] = {QColor(150, 180, 226), QColor(240, 170, 100), QColor(132, 200, 150), QColor(204, 152, 212),
                               QColor(236, 206, 104), QColor(120, 200, 210), QColor(226, 132, 132), QColor(176, 176, 176)};
  for (size_t i = 0; i < model_.bodies.size(); ++i) {
    const auto& b = model_.bodies[i];
    StructurePanel::BodyRow r;
    r.name = QString::fromStdString(b.name);
    r.createdBy = QString::fromStdString(b.createdBy);
    r.faces = int(b.faces.size());
    try { if (!b.shape.IsNull()) r.volume = model::massProperties(b.shape).volume; } catch (...) {}
    r.color = multi && showBodyColors_ ? pal[i % 8] : QColor(158, 182, 212);
    r.visible = !hiddenBodies_.count(b.name);
    r.transparent = transparentBodies_.count(b.name) > 0;
    for (const auto& f : b.faces)
      if (hiddenOwnFaces_.count(f)) {
        const QString fn = QString::fromStdString(f);
        r.hiddenFaces << qMakePair(fn, fn + nsHoverSuffix(fn));
      }
    bodies << r;
  }
  QList<StructurePanel::PlaneRow> planes;
  QList<StructurePanel::SketchRow> sketches;
  std::map<std::string, bool> ok;
  for (const auto& s : model_.status) ok[s.featureId] = s.ok;
  for (const auto& f : doc_.features) {
    if (f.type == FeatureType::Plane) {
      StructurePanel::PlaneRow p;
      p.id = QString::fromStdString(f.id);
      p.kind = QString::fromStdString(f.prop("kind", "edgeNormal"));
      p.suppressed = f.suppressed;
      p.ok = !ok.count(f.id) || ok[f.id];
      planes << p;
    } else if (f.type == FeatureType::Sketch && f.sketch) {
      StructurePanel::SketchRow s;
      s.id = QString::fromStdString(f.id);
      const auto& pl = f.sketch->plane;
      s.plane = pl.kind == SketchPlaneKind::XY ? "XY" : pl.kind == SketchPlaneKind::XZ ? "XZ" : pl.kind == SketchPlaneKind::YZ ? "YZ"
                : pl.kind == SketchPlaneKind::OnPlane ? QString::fromStdString(pl.planeRef) : QString("yüz");
      s.shown = sketchShownIn3D(f.id);
      for (const auto& g : doc_.features) if (!g.suppressed && g.prop("sketch") == f.id) s.consumed = true;
      s.ok = !ok.count(f.id) || ok[f.id];
      sketches << s;
    }
  }
  // SC23 (O-P0-6, ANALYSIS §4.6): derived types. Yüzeyler = regions of sketches no solid feature consumed yet
  // (per region: Extrude / Revolve `regions=` lists), Eğriler = sketches with open ends / without any region.
  QList<StructurePanel::SurfaceRow> surfaces;
  QList<StructurePanel::CurveRow> curves;
  const int stop = rollback_ >= 0 ? rollback_ : int(doc_.features.size()) - 1;
  for (int i = 0; i <= stop && i < int(doc_.features.size()); ++i) {
    const Feature& f = doc_.features[size_t(i)];
    if (f.type != FeatureType::Sketch || !f.sketch || f.suppressed || (ok.count(f.id) && !ok[f.id])) continue;
    bool all = false;
    const std::set<int> used = consumedRegions(doc_, f.id, &all);
    if (all) continue;
    int nCurves = 0;
    for (const auto& e : f.sketch->entities) if (!e.construction) ++nCurves;
    if (nCurves == 0) continue;
    pc::sketch::RegionResult rr;
    gp_Ax3 fr;
    if (!sketchRegionInfo(doc_, model_, f.id, rr, fr)) continue;
    const int n = int(rr.profile.faces.size());
    for (int k = 0; k < n; ++k) {
      if (used.count(k)) continue;
      StructurePanel::SurfaceRow r;
      r.name = regionName(f.id, k);
      r.sketch = QString::fromStdString(f.id);
      r.index = k;
      r.area = k < int(rr.areas.size()) ? rr.areas[size_t(k)] : 0.0;
      try {   // exact B-rep area (the arrangement's areas are polygon estimates)
        GProp_GProps ga;
        BRepGProp::SurfaceProperties(rr.profile.faces[size_t(k)], ga);
        if (ga.Mass() > 0) r.area = ga.Mass();
      } catch (...) {}
      r.holes = k < int(rr.holes.size()) ? rr.holes[size_t(k)] : 0;
      try {
        const TopoDS_Wire w = BRepTools::OuterWire(rr.profile.faces[size_t(k)]);
        GProp_GProps g;
        if (!w.IsNull()) { BRepGProp::LinearProperties(w, g); r.perimeter = g.Mass(); }
      } catch (...) {}
      surfaces << r;
    }
    if (n == 0 || !rr.openPoints.empty()) {
      StructurePanel::CurveRow c;
      c.sketch = QString::fromStdString(f.id);
      c.openEnds = int(rr.openPoints.size());
      c.curves = nCurves;
      curves << c;
    }
  }
  const QString title = QString("%1%2").arg(QString::fromStdString(doc_.name), path_.isEmpty() ? QString() : " (" + QFileInfo(path_).fileName() + ")");
  structure_->rebuild(title, bodies, planes, sketches, surfaces, curves, isWindowModified());
  visTreeSig_ = visTreeSig();
  structure_->syncSelection(selection_->items());
}

} // namespace pcd
