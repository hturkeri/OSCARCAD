// MainWindow v2: datum planes (edge-driven workflow, preview), multi-body panel, context menus.
#include <pc/DatumSizing.h>
#include "Icons.h"
#include "MainWindow.h"
#include "Import2DUi.h"   // SC-2D-IMPORT
#include "ParamSelectUi.h"
#include "StructurePanel.h"
#include "PropertiesPanel.h"
#include "ShellUtil.h"
#include "tools/MiniToolbar.h"
#include <QDockWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include "pc/NamedSelections.h"
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <algorithm>
#include <cmath>

using namespace pc;

namespace pcd {

namespace {
QColor bodyPalette(int i) {
  static const QColor pal[] = {QColor(150, 180, 226), QColor(240, 170, 100), QColor(132, 200, 150), QColor(204, 152, 212),
                               QColor(236, 206, 104), QColor(120, 200, 210), QColor(226, 132, 132), QColor(176, 176, 176)};
  return pal[size_t(i) % (sizeof(pal) / sizeof(pal[0]))];
}
QIcon swatch(const QColor& c) {
  QPixmap pm(16, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.darker(150), 1.2));
  p.setBrush(c);
  p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
  return QIcon(pm);
}
bool validBodyName(const QString& n) {
  if (n.trimmed().isEmpty()) return false;
  for (QChar c : n) if (c == ',' || c == '.' || c == '|' || c == ':' || c == '(' || c == ')' || c.isSpace()) return false;
  return true;
}
} // namespace

QStringList MainWindow::bodyNames() const {
  QStringList l;
  for (const auto& b : model_.bodies) l << QString::fromStdString(b.name);
  return l;
}

// ------------------------------------------------------------------------------ appearance
void MainWindow::applyBodyAppearance() {
  std::map<std::string, QColor> colors;
  QStringList hidden;
  const bool multi = model_.bodies.size() > 1;
  for (size_t i = 0; i < model_.bodies.size(); ++i) {
    const auto& b = model_.bodies[i];
    for (const auto& fn : b.faces) {
      if (multi && showBodyColors_) colors[fn] = bodyPalette(int(i));
    }
  }
  for (const auto& f : hiddenFaces_) hidden << QString::fromStdString(f);   // Gizle / Göster (MainWindowVis.cpp)
  if (import2d_) import2d_->appendHiddenFaces(hidden);   // SC-2D-IMPORT: bodies of hidden layers
  applyUserColors(colors);   // SC-COLOR: user face / body colours override the body palette (the tint below still wins)
  // named selections with "Vurguyu göster": their faces keep the selection colour (view state)
  for (const auto& r : nsMgr_.resolutions()) {
    if (!nsTint_.count(r.id)) continue;
    const NamedSelection* ns = nsFind(QString::fromStdString(r.id));
    if (!ns) continue;
    const QColor c(QString::fromStdString(ns->color));
    for (const auto& f : r.faces) colors[f] = c;
  }
  viewer_->setFaceColors(colors);
  viewer_->setHiddenFaces(hidden);
}

void MainWindow::applyDatumPlanes(const model::DatumPlane* preview, const std::string& previewId) {
  std::vector<viewer::DatumPlaneDisplay> out;
  std::set<std::string> hot;
  if (!selectedDatum_.isEmpty()) hot.insert(selectedDatum_.toStdString());
  if (tree_ && !sketching_) {
    const std::string cur = currentTreeFeature();
    if (const Feature* f = cur.empty() ? nullptr : doc_.feature(cur))
      if (f->sketch && f->sketch->plane.kind == SketchPlaneKind::OnPlane) hot.insert(f->sketch->plane.planeRef);
  }
  auto push = [&](const std::string& id, const model::DatumPlane& dp, const QString& label, bool highlighted) {
    viewer::DatumPlaneDisplay d;
    d.name = label;
    const gp_Pnt o = dp.frame.Location();
    const gp_Dir u = dp.frame.XDirection(), v = dp.frame.YDirection();
    d.origin[0] = o.X(); d.origin[1] = o.Y(); d.origin[2] = o.Z();
    d.u[0] = u.X(); d.u[1] = u.Y(); d.u[2] = u.Z();
    d.v[0] = v.X(); d.v[1] = v.Y(); d.v[2] = v.Z();
    if (preview && id == previewId) {
      d.halfSize = dp.displaySize > 0 ? dp.displaySize : 20;   // preview: caller stored the computed half-size
    } else {
      std::vector<std::string> refs;
      if (const Feature* pf = doc_.feature(id)) refs = pf->refs;
      d.halfSize = viewer::datumPlaneHalfSize(model_.shape, refs, model_.edges, model_.faces, dp.displaySize * 0.4);
    }
    d.highlighted = highlighted;
    out.push_back(d);
  };
  if (showPlanes_) {
    for (const auto& [id, dp] : model_.datumPlanes) {
      if (!previewId.empty() && id == previewId) continue;
      // Plane prop show=0: construction plane, drawn only while it is selected / its sketch is current
      const Feature* pf = doc_.feature(id);
      if (pf && pf->prop("show", "1") == "0" && !hot.count(id)) continue;
      push(id, dp, QString::fromStdString(id), hot.count(id) > 0);
    }
  }
  if (preview) push(previewId, *preview, QString::fromStdString(previewId) + " (önizleme)", true);
  if (planePick_) {
    // SC-UX "Sketch düzlemi seç": the three origin planes as pickable squares (SPEC §2.1)
    double half = 30;
    if (!model_.shape.IsNull()) {
      try {
        const auto mp = model::massProperties(model_.shape);
        half = std::max({mp.xmax - mp.xmin, mp.ymax - mp.ymin, mp.zmax - mp.zmin, 20.0}) * 0.22;
      } catch (...) {}
    }
    struct O { const char* n; double u[3]; double v[3]; };
    const O origin[] = {{"XY", {1, 0, 0}, {0, 1, 0}}, {"XZ", {1, 0, 0}, {0, 0, 1}}, {"YZ", {0, 1, 0}, {0, 0, 1}}};
    for (const O& o : origin) {
      viewer::DatumPlaneDisplay d;
      d.name = o.n;
      for (int i = 0; i < 3; ++i) { d.u[i] = o.u[i]; d.v[i] = o.v[i]; d.origin[i] = 0; }
      d.halfSize = half;
      d.highlighted = true;
      out.push_back(d);
    }
  }
  viewer_->setDatumPlanes(out);
}

// ------------------------------------------------------------------------------ bodies panel
void MainWindow::rebuildBodies() {
  if (!bodiesTree_) return;
  updatingBodies_ = true;
  QString keep;
  const auto sel = bodiesTree_->selectedItems();
  if (!sel.isEmpty()) keep = sel.first()->data(0, Qt::UserRole).toString();
  bodiesTree_->clear();
  std::set<std::string> alive;
  const bool multi = model_.bodies.size() > 1;
  for (size_t i = 0; i < model_.bodies.size(); ++i) {
    const auto& b = model_.bodies[i];
    alive.insert(b.name);
    auto* it = new QTreeWidgetItem(bodiesTree_);
    const QString n = QString::fromStdString(b.name);
    it->setData(0, Qt::UserRole, n);
    it->setText(0, n);
    it->setIcon(0, swatch(multi ? bodyPalette(int(i)) : QColor(158, 182, 212)));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(0, hiddenBodies_.count(b.name) ? Qt::Unchecked : Qt::Checked);
    double vol = 0;
    try { if (!b.shape.IsNull()) vol = model::massProperties(b.shape).volume; } catch (...) {}
    it->setText(1, QString::number(vol, 'f', 1));
    it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    it->setText(2, QString::fromStdString(b.createdBy));
    it->setToolTip(0, QString("%1 yüz · çift tıklayarak yeniden adlandırın").arg(b.faces.size()));
    if (n == keep) it->setSelected(true);
  }
  bodiesTree_->resizeColumnToContents(0);
  bodiesTree_->resizeColumnToContents(1);
  updatingBodies_ = false;
}

void MainWindow::selectBody(const QString& body) {
  const model::BodyInfo* b = model_.body(body.toStdString());
  if (!b) return;
  QStringList faces;
  for (const auto& f : b->faces) faces << QString::fromStdString(f);
  viewer_->clearHighlights();
  viewer_->setHighlightedFaces(faces, QColor(255, 176, 64));
  if (bodiesTree_) {
    updatingBodies_ = true;
    bodiesTree_->clearSelection();
    for (int i = 0; i < bodiesTree_->topLevelItemCount(); ++i)
      if (bodiesTree_->topLevelItem(i)->data(0, Qt::UserRole).toString() == body) bodiesTree_->topLevelItem(i)->setSelected(true);
    updatingBodies_ = false;
  }
  selFromLegacy_ = true;
  selection_->set({SelItem{SelItem::Kind::Body, body}});
  selFromLegacy_ = false;
  double vol = 0;
  try { vol = model::massProperties(b->shape).volume; } catch (...) {}
  statusBar()->showMessage(QString("Gövde %1: %2 yüz, hacim %3 mm³ (oluşturan: %4)").arg(body).arg(b->faces.size()).arg(vol, 0, 'f', 1).arg(QString::fromStdString(b->createdBy)), 8000);
}

bool MainWindow::renameBody(const QString& from, const QString& to) {
  if (from == to) return true;
  if (featDlg_) {   // the open dialog's OK would overwrite this change with its own document snapshot
    QMessageBox::information(this, "Gövde adı", "Önce açık özellik diyaloğunu kapatın.");
    return false;
  }
  if (!validBodyName(to)) {
    QMessageBox::warning(this, "Gövde adı", "Geçersiz ad: boşluk ve , . | : ( ) karakterleri kullanılamaz.");
    return false;
  }
  if (model_.body(to.toStdString())) {
    QMessageBox::warning(this, "Gövde adı", "Bu adda bir gövde zaten var.");
    return false;
  }
  const model::BodyInfo* b = model_.body(from.toStdString());
  if (!b) return false;
  Document cand = doc_;
  Feature* creator = cand.feature(b->createdBy);
  if (!creator || (creator->type != FeatureType::Extrude && creator->type != FeatureType::Revolve && creator->type != FeatureType::Pattern)) {
    QMessageBox::information(this, "Gövde adı", "Bu gövde adı, onu oluşturan Extrude/Revolve/Pattern özelliği üzerinden değiştirilebilir.");
    return false;
  }
  // The creator's bodyName names the whole body; split pieces "<stem>:k" (and ":dk") follow it. Renaming a
  // piece therefore renames the stem: "Body:2" -> "Yeni" gives "Yeni:1", "Yeni:2" (piece suffixes are kept).
  const QString stem = from.contains(':') ? from.left(from.indexOf(':')) : from;
  for (const auto& ob : model_.bodies) {
    const QString on = QString::fromStdString(ob.name);
    if ((on == to || on.startsWith(to + ":")) && !(on == stem || on.startsWith(stem + ":"))) {
      QMessageBox::warning(this, "Gövde adı", "Bu adda bir gövde zaten var.");
      return false;
    }
  }
  creator->props["bodyName"] = to.toStdString();
  auto mapName = [&](const QString& n) -> QString {
    const QString t = n.trimmed();
    if (t == stem) return to;
    if (t.startsWith(stem + ":")) return to + t.mid(stem.size());
    return n;
  };
  // rewrite references in other features
  auto rewrite = [&](std::string& list) {
    QStringList parts = QString::fromStdString(list).split(',', Qt::SkipEmptyParts);
    for (QString& p : parts) p = mapName(p);
    list = parts.join(",").toStdString();
  };
  for (auto& f : cand.features) {
    if (f.props.count("targets")) rewrite(f.props["targets"]);
    if (f.props.count("tools")) rewrite(f.props["tools"]);
    if (f.props.count("target")) f.props["target"] = mapName(QString::fromStdString(f.props["target"])).trimmed().toStdString();
  }
  // named selections with Body members follow the rename (docs/named_selections/MODEL.md)
  model::ns::renameBodyRefs(cand, stem.toStdString(), to.toStdString());
  // Gizle / Göster: hidden / transparent body members follow the rename too (pieces "<stem>:k" included)
  std::vector<NamedSelection*> viewSels{&cand.view.hidden, &cand.view.transparent};
  for (auto& cg : cand.view.colors) viewSels.push_back(&cg);   // SC-COLOR
  for (NamedSelection* vs : viewSels)
    for (auto& m : vs->members) {
      if (m.kind == NsMemberKind::Body) m.ref = mapName(QString::fromStdString(m.ref)).toStdString();
      if (!m.sig.body.empty()) m.sig.body = mapName(QString::fromStdString(m.sig.body)).toStdString();
    }
  return tryCommit(cand, QString("Gövdeyi yeniden adlandır: %1 -> %2").arg(from, to));
}

void MainWindow::bodiesContextMenu(const QPoint& pos) {
  auto* it = bodiesTree_->itemAt(pos);
  if (!it) return;
  const QString n = it->data(0, Qt::UserRole).toString();
  QMenu m(this);
  m.addAction("Yeniden adlandır...", this, [this, n] {
    bool ok = false;
    const QString nn = QInputDialog::getText(this, "Gövdeyi yeniden adlandır", "Yeni ad:", QLineEdit::Normal, n, &ok);
    if (ok) renameBody(n, nn);
  });
  const bool vis = !hiddenBodies_.count(n.toStdString());
  m.addAction(vis ? "Gizle" : "Göster", this, [this, n, vis] { setBodyVisible(n, !vis); });
  m.addAction("Yalnız bunu göster", this, [this, n] { isolateItems({SelItem{SelItem::Kind::Body, n}}); });
  m.addAction("Tümünü göster", this, [this] { showAllItems(); });
  m.exec(bodiesTree_->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------------------ planes
bool MainWindow::createPlaneOnEdge(const QString& edge, const QString& kind) {
  if (edge.isEmpty() || sketching_) return false;
  Feature f;
  f.type = FeatureType::Plane;
  f.id = doc_.uniqueFeatureId(FeatureType::Plane);
  f.props = {{"kind", kind.toStdString()}, {"offset", "0"}, {"rotate", "0"}};
  if (kind == "edgeNormal" || kind == "edgeAngle") f.props["t"] = kind == "edgeNormal" ? "0.5" : "0.5";
  if (kind == "edgeAngle") f.props["angle"] = "45";
  f.refs = {edge.toStdString()};
  const std::string id = f.id;
  const bool ok = insertFeatureCommit(f, "Ekle: " + QString::fromStdString(id));
  if (ok) {
    selectedDatum_ = QString::fromStdString(id);
    applyDatumPlanes();
    statusBar()->showMessage(QString("Düzlem oluşturuldu: %1 — üzerine tıklayıp 'Bu düzlemde sketch başlat' seçebilirsiniz.").arg(selectedDatum_), 9000);
  }
  return ok;
}

void MainWindow::openPlaneDialog(const QString& edge, const QString& kind) {
  if (sketching_) return;
  Feature f;
  f.type = FeatureType::Plane;
  f.id = doc_.uniqueFeatureId(FeatureType::Plane);
  f.props["kind"] = kind.toStdString();
  if (kind == "edgeAngle") { f.props["angle"] = "45"; f.props["t"] = "0.5"; }
  if (!edge.isEmpty()) f.refs = {edge.toStdString()};
  openFeatureDialog(f, true);
}

void MainWindow::beginSketchOnPlane(const std::string& id) {
  const Feature* f = doc_.feature(id);
  if (!f || f->type != FeatureType::Plane || sketching_) return;
  if (f->suppressed || !model_.datumPlanes.count(id)) {   // suppressed, failed or after the rollback point
    statusBar()->showMessage(QString("Düzlem %1 şu an kullanılamıyor (bastırılmış, hatalı veya geri sarma noktasından sonra).").arg(QString::fromStdString(id)), 8000);
    return;
  }
  SketchPlane pl;
  pl.kind = SketchPlaneKind::OnPlane;
  pl.planeRef = id;
  selectedDatum_ = QString::fromStdString(id);
  beginSketch(pl);
}

void MainWindow::showEdgeMenu(const QString& edge, const QPoint& globalPos) {
  // An open (modeless) feature dialog commits its own document snapshot on OK: committing a plane from the
  // menu meanwhile would be silently dropped by that commit, so the menus are disabled while it is open.
  if (edge.isEmpty() || sketching_ || featDlg_) return;
  if (ctxMenu_) ctxMenu_->close();
  auto* m = new QMenu(this);
  m->setObjectName("edgeContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  QString shortName = edge;
  if (shortName.size() > 46) shortName = shortName.left(43) + "...";
  auto* hdr = m->addAction(makeIcon("select"), "Kenar: " + shortName + nsHoverSuffix(edge));
  hdr->setEnabled(false);
  if (!edge.startsWith("sketch:") && model_.edges.count(edge.toStdString())) {
    auto* an = m->addAction(makeIcon("namedsel", 16), "Adlandırılmış seçim oluştur\tCtrl+G");
    an->setObjectName("actNsCreateHere");
    connect(an, &QAction::triggered, this, [this] { createNamedSelectionFromSelection(true); });
  }
  m->addSeparator();
  auto* a1 = m->addAction(makeIcon("plane"), "Bu kenara dik düzlem");
  a1->setObjectName("actPlaneNormal");
  connect(a1, &QAction::triggered, this, [this, edge] { createPlaneOnEdge(edge, "edgeNormal"); });
  auto* a2 = m->addAction(makeIcon("plane"), "Bu kenardan açılı düzlem");
  a2->setObjectName("actPlaneAngle");
  connect(a2, &QAction::triggered, this, [this, edge] { openPlaneDialog(edge, "edgeAngle"); });
  auto* a3 = m->addAction(makeIcon("plane"), "İki kenardan düzlem");
  a3->setObjectName("actPlaneTwoEdges");
  connect(a3, &QAction::triggered, this, [this, edge] {
    pendingTwoEdge_ = edge;
    viewer_->setHighlightedEdges({edge}, QColor(255, 140, 0));
    statusBar()->showMessage("İkinci kenarı 3B görünümde Ctrl+tıklayarak seçin (aynı düzlemde, kesişen veya paralel düz kenar)...", 20000);
  });
  m->addSeparator();
  auto* a4 = m->addAction("Düzlem diyaloğu...");
  a4->setObjectName("actPlaneDialog");
  connect(a4, &QAction::triggered, this, [this, edge] { openPlaneDialog(edge, "edgeNormal"); });
  if (paramSel_) paramSel_->populateContextMenu(m);
  if (tools_) tools_->populateContextMenu(m, {SelItem{SelItem::Kind::Edge, edge}});
  ctxMenu_ = m;
  m->popup(globalPos);
}

void MainWindow::showDatumMenu(const QString& plane, const QPoint& globalPos) {
  if (plane.isEmpty() || sketching_ || featDlg_) return;
  if (ctxMenu_) ctxMenu_->close();
  auto* m = new QMenu(this);
  m->setObjectName("datumContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  auto* hdr = m->addAction(makeIcon("plane"), "Düzlem: " + plane);
  hdr->setEnabled(false);
  m->addSeparator();
  auto* a1 = m->addAction(makeIcon("sketch"), "Bu düzlemde sketch başlat");
  a1->setObjectName("actSketchOnPlane");
  connect(a1, &QAction::triggered, this, [this, plane] { beginSketchOnPlane(plane.toStdString()); });
  auto* a2 = m->addAction("Düzlemi düzenle...");
  a2->setObjectName("actEditPlane");
  connect(a2, &QAction::triggered, this, [this, plane] { openFeatureEditor(plane.toStdString()); });
  if (QAction* mv = tools_ ? tools_->action("move") : nullptr) { m->addSeparator(); m->addAction(mv); }
  if (tools_) tools_->populateContextMenu(m, {SelItem{SelItem::Kind::Datum, plane}});
  ctxMenu_ = m;
  m->popup(globalPos);
}

void MainWindow::onDatumPicked(const QString& name, Qt::KeyboardModifiers mods, int clicks, QPoint pos) {
  if (sketching_ || name.isEmpty()) return;
  if (pos.x() < 0) pos = viewer_->mapFromGlobal(QCursor::pos());
  if (planePick_) {
    SketchPlane pl;
    if (name == "XY" || name == "XZ" || name == "YZ") {
      pl.kind = name == "XY" ? SketchPlaneKind::XY : name == "XZ" ? SketchPlaneKind::XZ : SketchPlaneKind::YZ;
      cancelPlanePick();
      beginSketch(pl);
    } else {
      cancelPlanePick();
      beginSketchOnPlane(name.toStdString());
    }
    return;
  }
  if (pick_ == Pick::SketchDatum) {
    pick_ = Pick::None;
    beginSketchOnPlane(name.toStdString());
    return;
  }
  if (featDlg_ && featDlg_->wantsFaces() && featDlg_->planeKind() == "faceOffset") { /* datum is not a face: ignore */ }
  const SelItem item{SelItem::Kind::Datum, name};
  if (!featDlg_ && tools_) {
    PickEvent e;
    e.item = item;
    e.modifiers = mods;
    e.clickCount = clicks;
    e.viewerPos = pos;
    if (tools_->handlePick(e)) { selectedDatum_ = name; applyDatumPlanes(); return; }
  }
  selectedDatum_ = name;
  if (selection_->accepts(item.kind)) {
    if (mods & Qt::ControlModifier) selection_->toggle(item);
    else selection_->set({item});
  }
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    auto* it = tree_->topLevelItem(i);
    if (it->data(0, Qt::UserRole).toString() == name) {
      tree_->blockSignals(true);
      tree_->clearSelection();
      it->setSelected(true);
      tree_->blockSignals(false);
    }
  }
  applyDatumPlanes();
  statusBar()->showMessage("Seçili düzlem: " + name + "  (K: bu düzlemde sketch" + (spaceClaimNavigation() ? ", sağ tık: menü)" : ")"), 8000);
  if (!spaceClaimNavigation()) showDatumMenu(name, QCursor::pos());   // SpaceClaim scheme: right click only
}

void MainWindow::viewerContextMenu(const QPoint& pos) {
  if (sketching_ || planePick_) return;
  const QPoint g = viewer_->mapToGlobal(pos);
  const QString e = viewer_->edgeAt(pos);
  if (!e.isEmpty() && import2d_ && import2d_->showCurveMenu(e, g)) return;   // SC-2D-IMPORT: imported curve menu
  if (!e.isEmpty()) {
    selectedEdge_ = e;
    if (!selection_->contains(SelItem{SelItem::Kind::Edge, e}) && selection_->accepts(SelItem::Kind::Edge)) selection_->set({SelItem{SelItem::Kind::Edge, e}});
    viewer_->setHighlightedEdges({e}, QColor(255, 140, 0));
    showEdgeMenu(e, g);
    return;
  }
  const QString d = viewer_->datumAt(pos);
  if (!d.isEmpty()) {
    selectedDatum_ = d;
    if (!selection_->contains(SelItem{SelItem::Kind::Datum, d})) selection_->set({SelItem{SelItem::Kind::Datum, d}});
    applyDatumPlanes();
    showDatumMenu(d, g);
    return;
  }
  // SC-UX (SPEC §3.10): general menu + the face under the cursor
  if (featDlg_) return;
  if (ctxMenu_) ctxMenu_->close();
  auto* m = new QMenu(this);
  m->setObjectName("viewContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  const QString face = model_.faces.count(hoverFace_.toStdString()) ? hoverFace_ : QString();
  QList<SelItem> under;
  if (!face.isEmpty()) {
    under << SelItem{SelItem::Kind::Face, face};
    if (!selection_->contains(under.first()) && selection_->accepts(SelItem::Kind::Face)) selection_->set(under);
  }
  m->setToolTipsVisible(true);
  // SC23 WP-S (S-P1-7): SpaceClaim 2023 R1 face menu items [G] that PersistCAD can back with real commands.
  // Clip with Volume / Spin Center / Anchor Pull Handle / Suppress for Physics are not added ([D] / no engine).
  {
    const Feature* lastSketch = nullptr;
    for (const auto& f : doc_.features) if (f.type == FeatureType::Sketch && f.sketch) lastSketch = &f;
    QAction* a = m->addAction(makeIcon("sketch", 16), "Son sketch düzlemini kullan (Use Last Sketch Plane)");
    a->setObjectName("ctxLastSketchPlane");
    if (lastSketch) {
      const SketchPlane pl = lastSketch->sketch->plane;
      a->setToolTip(QString("%1 düzleminde yeni sketch açar").arg(QString::fromStdString(lastSketch->id)));
      connect(a, &QAction::triggered, this, [this, pl] { beginSketch(pl); });
    } else {
      a->setEnabled(false);
      a->setToolTip("Belgede henüz sketch yok");
    }
    QMenu* vm = m->addMenu(makeIcon("views", 16), "Görünüm (View)");
    vm->setObjectName("ctxViewMenu");
    for (QAction* va : menuViews_->actions()) vm->addAction(va);
  }
  m->addSeparator();
  for (const char* id : {"select", "pull", "move"}) if (QAction* a = tools_->action(id)) m->addAction(a);
  m->addSeparator();
  m->addAction(actHome_);
  m->addAction(actPlanView_);
  m->addAction(actFit_);
  m->addSeparator();
  m->addAction(section_->toggleAction());
  if (!face.isEmpty()) {
    m->addSeparator();
    const std::string fid = face.section('.', 0, 0).toStdString();
    auto* hdr = m->addAction(makeIcon("pull", 16), "Yüz: " + (face.size() > 46 ? face.left(43) + "..." : face) + nsHoverSuffix(face));
    hdr->setEnabled(false);
    // SpaceClaim: right click on the selected faces > Create Named Selection (= Ctrl+G)
    auto* an = m->addAction(makeIcon("namedsel", 16), "Adlandırılmış seçim oluştur\tCtrl+G");
    an->setObjectName("actNsCreateHere");
    an->setToolTip("Seçili yüz(ler)den adlandırılmış seçim oluştur; ad Gruplar panelinde yazılır");
    connect(an, &QAction::triggered, this, [this] { createNamedSelectionFromSelection(true); });
    if (isPlanarFace(face)) {
      m->addAction(makeIcon("sketch", 16), "Bu yüzde sketch aç (K)", this, [this, face] {
        SketchPlane pl;
        pl.kind = SketchPlaneKind::OnFace;
        pl.faceRef = face.toStdString();
        beginSketch(pl);
      });
      m->addAction(makeIcon("plane", 16), "Bu yüzden düzlem", this, [this, face] {
        Feature f;
        f.type = FeatureType::Plane;
        f.id = doc_.uniqueFeatureId(FeatureType::Plane);
        f.props = {{"kind", "faceOffset"}, {"offset", "0"}, {"rotate", "0"}};
        f.refs = {face.toStdString()};
        if (insertFeatureCommit(f, "Ekle: " + QString::fromStdString(f.id))) {
          selectedDatum_ = QString::fromStdString(f.id);
          applyDatumPlanes();
        }
      });
    }
    if (doc_.feature(fid)) {
      m->addAction("Oluşturan özelliği düzenle...", this, [this, fid] { openFeatureEditor(fid); });
      m->addAction("Geçmişte göster", this, [this, fid] { selectFeatureInHistory(fid); });
    }
  }
  addColorMenu(m, face);        // SC-COLOR: Renk ▸
  addVisibilityMenu(m, face);   // Gizle / Diğerlerini gizle / Gövdeyi gizle / Saydam yap / Tümünü göster
  {  // SC23 WP-S (S-P1-7): Inverse Visibility, Locate in Structure Tree, clipboard (reasoned disabled), Delete, Properties
    const QStringList hiddenB = hiddenBodyNames();
    QAction* inv = m->addAction(makeIcon("hideothers", 16), "Görünürlüğü tersine çevir (Inverse Visibility)");
    inv->setObjectName("ctxInverseVisibility");
    if (hiddenB.isEmpty() && hiddenOwnFaceCount() == 0) {
      inv->setEnabled(false);
      inv->setToolTip("Gizli gövde yok: tersine çevirmek her şeyi gizlerdi");
    } else {
      inv->setToolTip("Gizli gövdeler görünür, görünür gövdeler gizli olur (tek geri alma adımı)");
      connect(inv, &QAction::triggered, this, [this, hiddenB] {
        std::vector<model::ns::Ref> refs;
        for (const auto& b : model_.bodies) if (!hiddenB.contains(QString::fromStdString(b.name))) refs.push_back(model::ns::body(b.name));
        ViewState v = doc_.view;
        v.hidden.members.clear();
        if (!refs.empty() && !visEdit(v.hidden, refs, true)) return;
        if (visCommit(v, "Görünürlüğü tersine çevir")) statusBar()->showMessage("Görünürlük tersine çevrildi  (Tümünü göster: Ctrl+Shift+H)", 4000);
      });
    }
    m->addSeparator();
    QAction* loc = m->addAction(makeIcon("bodies", 16), "Yapı ağacında bul (Locate in Structure Tree)");
    loc->setObjectName("ctxLocateInTree");
    loc->setEnabled(!selection_->isEmpty());
    loc->setToolTip(selection_->isEmpty() ? QString("Önce bir öğe seçin") : QString("Seçimi Yapı ağacında gösterir"));
    connect(loc, &QAction::triggered, this, [this] {
      dockStructure_->show();
      setDockCollapsed(dockStructure_, false);
      structure_->setCurrentPage("structure");
      structure_->syncSelection(selection_->items());
      if (QTreeWidgetItem* it = structure_->tree()->currentItem()) structure_->tree()->scrollToItem(it);
      structure_->tree()->setFocus();
    });
    m->addSeparator();
    for (auto [name, text] : {std::pair{"ctxCut", "Kes (Cut)"}, std::pair{"ctxCopy", "Kopyala (Copy)"}, std::pair{"ctxPaste", "Yapıştır (Paste)"}}) {
      QAction* a = m->addAction(QString::fromUtf8(text));
      a->setObjectName(name);
      a->setEnabled(false);
      a->setToolTip("Pasif: PersistCAD'de pano (clipboard) desteği yok");
    }
    m->addAction(actDelete_);
    m->addSeparator();
    QAction* pr = m->addAction(makeIcon("props", 16), "Özellikler (Properties)");
    pr->setObjectName("ctxProperties");
    pr->setToolTip("Seçili öğenin özelliklerini Özellikler panelinde gösterir");
    connect(pr, &QAction::triggered, this, [this] {
      dockOptions_->show();
      setDockCollapsed(dockOptions_, false);
      optionsTabs_->setCurrentIndex(1);
      props_->setFocus();
    });
  }
  if (paramSel_) paramSel_->populateContextMenu(m);
  if (tools_) tools_->populateContextMenu(m, under);
  ctxMenu_ = m;
  m->popup(g);
  // [G] §4: the mini-toolbar appears together with the context menu (right above it)
  if (auto* mt = qobject_cast<MiniToolbar*>(miniToolbar_)) mt->showForContextMenu(pos);
}

// ------------------------------------------------------------------------------ live preview
void MainWindow::updatePlanePreview() {
  FeatureDialog* dlg = previewDlg_;
  if (!dlg || !previewWork_) return;
  const Feature nf = dlg->feature();
  const bool needsRef = nf.refs.empty() && nf.prop("kind", "edgeNormal") != "axisAngle";
  if (needsRef) {
    dlg->setPreviewStatus("Önizleme: 3B görünümde kenar/yüz seçin.", false);
    applyDatumPlanes();
    return;
  }
  Document cand = *previewWork_;
  int idx;
  if (dlg->isNewFeature()) {
    idx = rollback_ >= 0 ? rollback_ + 1 : int(cand.features.size());
    idx = std::min<int>(idx, int(cand.features.size()));
    cand.features.insert(cand.features.begin() + idx, nf);
  } else {
    idx = cand.indexOf(nf.id);
    if (idx < 0) return;
    cand.features[size_t(idx)] = nf;
  }
  try {
    const ParamValues vals = cand.evaluateParams();
    const model::ModelResult res = model::regenerate(cand, vals, idx);
    for (const auto& st : res.status) {
      if (st.featureId == nf.id && !st.ok) {
        dlg->setPreviewStatus("Önizleme hatası: " + QString::fromStdString(st.message), false);
        applyDatumPlanes();
        return;
      }
    }
    auto it = res.datumPlanes.find(nf.id);
    if (it == res.datumPlanes.end()) {
      dlg->setPreviewStatus("Önizleme: düzlem hesaplanamadı.", false);
      applyDatumPlanes();
      return;
    }
    const gp_Pnt o = it->second.frame.Location();
    dlg->setPreviewStatus(QString("Önizleme tamam: orijin (%1, %2, %3)").arg(o.X(), 0, 'f', 2).arg(o.Y(), 0, 'f', 2).arg(o.Z(), 0, 'f', 2), true);
    model::DatumPlane pv = it->second;
    pv.displaySize = viewer::datumPlaneHalfSize(res.shape, nf.refs, res.edges, res.faces, pv.displaySize * 0.4);
    applyDatumPlanes(&pv, nf.id);
  } catch (...) {
    dlg->setPreviewStatus("Önizleme: ifade veya geometri hatası.", false);
    applyDatumPlanes();
  }
}

} // namespace pcd
