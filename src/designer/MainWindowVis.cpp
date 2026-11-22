// Gizle / Göster (SpaceClaim "Hide / Hide Others / Show All") for faces and bodies — view state only.
//   * Document::view.hidden / view.transparent are named-selection-like member lists (persistent face names +
//     signatures, body names). They are resolved like named selections after every regeneration
//     (pc::model::resolve): a hidden face that a later feature splits stays hidden in every piece, a face that no
//     longer exists is ignored silently (it comes back hidden if the face comes back, e.g. after unsuppress).
//   * Never touches geometry: no regeneration, volume / mfg checks / STEP / Save-as-App see the whole part.
//   * Every change is ONE undo step (VisCommand) applied without regenerating; saved in .pcad ("view").
//   * Hidden faces are neither drawn nor pickable (ShapeViewer::setHiddenFaces): a click goes through to the face
//     behind. Edges bounding a visible face stay drawn, edges only between hidden faces disappear.
// Entry points: Ctrl+H (context-sensitive: selected faces -> faces, bodies -> bodies), Alt+H (Diğerlerini gizle =
// isolate), Ctrl+Shift+H (Tümünü göster), ribbon Tasarım > Gövde / Görünürlük and Görüntü > Gizle / Göster,
// viewport / Yapı / Adlandırılmış Seçimler context menus, Yapı "Gizli yüzler (n)" rows, status bar "visStatus".
#include "Icons.h"
#include "MainWindow.h"
#include "StatusBarWidgets.h"
#include "StructurePanel.h"
#include "pc/NamedSelections.h"
#include <QStatusBar>
#include <QToolButton>
#include <QUndoCommand>
#include <algorithm>

using namespace pc;

namespace pcd {

class VisCommand : public QUndoCommand {
public:
  VisCommand(MainWindow* w, ViewState before, ViewState after, const QString& text)
      : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)) {}
  void redo() override { w_->applyViewState(after_); }
  void undo() override { w_->applyViewState(before_); }

private:
  MainWindow* w_;
  ViewState before_, after_;
};

namespace {
bool sameMembers(const NamedSelection& a, const NamedSelection& b) {
  if (a.members.size() != b.members.size()) return false;
  for (size_t i = 0; i < a.members.size(); ++i)
    if (a.members[i].kind != b.members[i].kind || a.members[i].ref != b.members[i].ref) return false;
  return true;
}
} // namespace

// ------------------------------------------------------------------------------ resolution
void MainWindow::visUpdate() {
  hiddenBodies_.clear();
  hiddenFaces_.clear();
  hiddenOwnFaces_.clear();
  transparentFaces_.clear();
  transparentBodies_.clear();
  auto run = [&](const NamedSelection& sel, std::set<std::string>& faces, std::set<std::string>* own, std::set<std::string>& bodies) {
    if (sel.members.empty() || model_.faces.empty()) return;
    model::NsResolution r;
    try { r = model::resolve(doc_, sel, model_, rollback_); } catch (...) { return; }
    for (const auto& m : r.members) {
      if (!model::isResolved(m.status)) continue;   // lost / not yet / suppressed: ignored (kept in the document)
      if (m.kind == NsMemberKind::Body) {
        if (m.status == model::NsStatus::Merged) continue;   // a hidden body merged into another one: not that one
        for (const auto& b : m.bodies) {
          bodies.insert(b);
          if (const auto* bi = model_.body(b)) faces.insert(bi->faces.begin(), bi->faces.end());
        }
      } else if (m.kind == NsMemberKind::Face) {
        for (const auto& f : m.faces) {
          if (!model_.faces.count(f)) continue;
          faces.insert(f);
          if (own) own->insert(f);
        }
      }
    }
  };
  run(doc_.view.hidden, hiddenFaces_, &hiddenOwnFaces_, hiddenBodies_);
  run(doc_.view.transparent, transparentFaces_, nullptr, transparentBodies_);
  colorsUpdate();   // SC-COLOR
}

QStringList MainWindow::hiddenFaceNames() const {
  QStringList l;
  for (const auto& f : hiddenFaces_) l << QString::fromStdString(f);
  return l;
}
QStringList MainWindow::hiddenBodyNames() const {
  QStringList l;
  for (const auto& b : hiddenBodies_) l << QString::fromStdString(b);
  return l;
}
bool MainWindow::faceHidden(const QString& face) const { return hiddenFaces_.count(face.toStdString()) > 0; }
bool MainWindow::faceTransparent(const QString& face) const { return transparentFaces_.count(face.toStdString()) > 0; }

int MainWindow::hiddenOwnFaceCount() const {
  int n = 0;
  for (const auto& f : hiddenOwnFaces_)
    if (!hiddenBodies_.count(model_.bodyOfFace(f))) ++n;   // faces of hidden bodies are counted as the body
  return n;
}

QString MainWindow::visibilityStatusText() const {
  const int nf = hiddenOwnFaceCount(), nb = int(hiddenBodies_.size());
  if (!nf && !nb) return QString();
  return QString("%1 gizli yüz, %2 gizli gövde").arg(nf).arg(nb);
}

QStringList MainWindow::hiddenFacesOfBody(const QString& body) const {
  QStringList l;
  const auto* b = model_.body(body.toStdString());
  if (!b) return l;
  for (const auto& f : b->faces) if (hiddenOwnFaces_.count(f)) l << QString::fromStdString(f);
  return l;
}

void MainWindow::updateVisStatus() {
  if (!visStatus_ && sbw_) {
    visStatus_ = new QToolButton;
    visStatus_->setObjectName("visStatus");
    visStatus_->setAutoRaise(true);
    visStatus_->setIcon(makeIcon("hide", 16));
    visStatus_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    visStatus_->setToolTip("Gizli öğeler — tıklayın: Tümünü göster (Ctrl+Shift+H)");
    connect(visStatus_, &QToolButton::clicked, this, [this] { showAllItems(); });
    sbw_->addPermanent(visStatus_);
  }
  if (!visStatus_) return;
  const QString t = visibilityStatusText();
  visStatus_->setText(t);
  visStatus_->setVisible(!t.isEmpty());
}

QString MainWindow::visTreeSig() const {
  QString s;
  for (const auto& f : hiddenOwnFaces_) s += QString::fromStdString(f) + ';';
  s += '|';
  for (const auto& b : transparentBodies_) s += QString::fromStdString(b) + ';';
  return s;
}

// ------------------------------------------------------------------------------ apply (no regeneration)
void MainWindow::applyViewState(const ViewState& v) {
  doc_.view = v;
  exprWork_.view = v;
  if (sketching_) work_.view = v;
  if (dlgWork_) dlgWork_->view = v;
  visUpdate();
  // hidden items cannot stay selected (SpaceClaim deselects what it hides)
  if (selection_ && !selection_->isEmpty()) {
    QList<SelItem> keep;
    for (const SelItem& s : selection_->items()) {
      const std::string n = s.name.toStdString();
      if (s.kind == SelItem::Kind::Face && hiddenFaces_.count(n)) continue;
      if (s.kind == SelItem::Kind::Body && hiddenBodies_.count(n)) continue;
      keep << s;
    }
    if (keep.size() != selection_->count()) selection_->set(keep);
  }
  applyBodyAppearance();
  applySketchTransparency();
  // check boxes are updated in place (callers may hold tree items, e.g. a check box being toggled); the Yapı tree
  // is rebuilt only when its "Gizli yüzler" rows / transparent marks change
  if (bodiesTree_) {
    updatingBodies_ = true;
    for (int i = 0; i < bodiesTree_->topLevelItemCount(); ++i) {
      QTreeWidgetItem* it = bodiesTree_->topLevelItem(i);
      it->setCheckState(0, bodyVisible(it->data(0, Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
    updatingBodies_ = false;
  }
  if (structure_) {
    if (visTreeSig() != visTreeSig_) rebuildStructure();
    else for (const auto& b : model_.bodies) structure_->setBodyChecked(QString::fromStdString(b.name), !hiddenBodies_.count(b.name));
  }
  if (selection_ && !selection_->isEmpty()) highlightSelection();
  schedulePanelsRefresh();
  updateVisStatus();
  updateTitle();
}

bool MainWindow::visCommit(const ViewState& v, const QString& text) {
  if (sameMembers(v.hidden, doc_.view.hidden) && sameMembers(v.transparent, doc_.view.transparent)) return false;
  undo_.push(new VisCommand(this, doc_.view, v, text));
  return true;
}

// Edit one of the view lists through the named-selection editing functions (they need a document holding it).
bool MainWindow::visEdit(NamedSelection& sel, const std::vector<model::ns::Ref>& refs, bool add) {
  if (refs.empty()) return false;
  Document scratch = doc_;
  scratch.namedSelections = {sel};
  std::string err;
  bool ok = true;
  if (add) ok = model::ns::addMembers(scratch, sel.name, refs, model_, rollback_, &err);
  else
    for (const auto& r : refs) model::ns::removeMembers(scratch, sel.name, {r}, model_, rollback_, &err);
  if (!ok) { statusBar()->showMessage("Görünürlük: " + QString::fromStdString(err), 5000); return false; }
  sel.members = scratch.namedSelections.front().members;
  return true;
}

std::vector<model::ns::Ref> MainWindow::visRefsOf(const QList<SelItem>& items) const {
  std::vector<model::ns::Ref> refs;
  for (const SelItem& it : items) {
    const std::string n = it.name.toStdString();
    if (it.kind == SelItem::Kind::Face && model_.faces.count(n)) refs.push_back(model::ns::face(n));
    else if (it.kind == SelItem::Kind::Body && model_.body(n)) refs.push_back(model::ns::body(n));
  }
  return refs;
}

// ------------------------------------------------------------------------------ commands
bool MainWindow::hideItems(const QList<SelItem>& items) {
  const auto refs = visRefsOf(items);
  if (refs.empty()) return false;
  ViewState v = doc_.view;
  if (!visEdit(v.hidden, refs, true)) return false;
  int nf = 0, nb = 0;
  for (const auto& r : refs) (r.kind == NsMemberKind::Body ? nb : nf)++;
  QString text = nb && nf ? QString("Gizle: %1 yüz, %2 gövde").arg(nf).arg(nb)
                 : nb     ? QString("Gizle: %1").arg(nb == 1 ? QString::fromStdString(refs.front().name) : QString("%1 gövde").arg(nb))
                          : QString("Gizle: %1 yüz").arg(nf);
  if (!visCommit(v, text)) return false;
  statusBar()->showMessage(text + "  (Tümünü göster: Ctrl+Shift+H)", 4000);
  return true;
}

bool MainWindow::showItems(const QList<SelItem>& items) {
  const auto refs = visRefsOf(items);
  if (refs.empty()) return false;
  ViewState v = doc_.view;
  if (!visEdit(v.hidden, refs, false)) return false;
  return visCommit(v, QString("Göster: %1").arg(refs.size() == 1 ? QString::fromStdString(refs.front().name) : QString("%1 öğe").arg(refs.size())));
}

bool MainWindow::isolateItems(const QList<SelItem>& items) {
  std::set<std::string> selBodies, selFaces, faceBodies;
  for (const SelItem& s : items) {
    const std::string n = s.name.toStdString();
    if (s.kind == SelItem::Kind::Body && model_.body(n)) selBodies.insert(n);
    else if (s.kind == SelItem::Kind::Face && model_.faces.count(n)) {
      selFaces.insert(n);
      faceBodies.insert(model_.bodyOfFace(n));
    }
  }
  if (selBodies.empty() && selFaces.empty()) return false;
  // isolate: everything except the selection is hidden; bodies picked whole keep all their faces, bodies with
  // picked faces keep only those faces
  std::vector<model::ns::Ref> refs;
  for (const auto& b : model_.bodies) {
    if (selBodies.count(b.name)) continue;
    if (!faceBodies.count(b.name)) { refs.push_back(model::ns::body(b.name)); continue; }
    for (const auto& f : b.faces) if (!selFaces.count(f)) refs.push_back(model::ns::face(f));
  }
  ViewState v = doc_.view;
  v.hidden.members.clear();
  if (!refs.empty() && !visEdit(v.hidden, refs, true)) return false;
  const bool ok = visCommit(v, "Diğerlerini gizle");
  if (ok) statusBar()->showMessage("Diğerleri gizlendi  (Tümünü göster: Ctrl+Shift+H)", 4000);
  return ok;
}

bool MainWindow::showAllItems() {
  ViewState v = doc_.view;
  v.hidden.members.clear();
  if (!visCommit(v, "Tümünü göster")) {
    statusBar()->showMessage("Gizli öğe yok.", 3000);
    return false;
  }
  statusBar()->showMessage("Tümü gösteriliyor.", 3000);
  return true;
}

bool MainWindow::setItemsTransparent(const QList<SelItem>& items, bool on) {
  const auto refs = visRefsOf(items);
  if (refs.empty()) return false;
  ViewState v = doc_.view;
  if (!visEdit(v.transparent, refs, on)) return false;
  return visCommit(v, on ? "Saydam yap" : "Saydamlığı kaldır");
}

void MainWindow::setBodyVisible(const QString& body, bool visible) {
  const QList<SelItem> it{SelItem{SelItem::Kind::Body, body}};
  if (visible) showItems(it);
  else hideItems(it);
  if (structure_) structure_->setBodyChecked(body, bodyVisible(body));   // a refused change re-syncs the box
}

void MainWindow::setFaceVisible(const QString& face, bool visible) {
  const QList<SelItem> it{SelItem{SelItem::Kind::Face, face}};
  if (visible) showItems(it);
  else hideItems(it);
}

QList<SelItem> MainWindow::visTargetsOfSelection() const {
  QList<SelItem> items;
  for (const SelItem& s : selection_->items())
    if (s.kind == SelItem::Kind::Face || s.kind == SelItem::Kind::Body) items << s;
  if (items.isEmpty() && !selectedFace_.isEmpty() && model_.faces.count(selectedFace_.toStdString()))
    items << SelItem{SelItem::Kind::Face, selectedFace_};
  return items;
}

void MainWindow::hideSelectedBodies() {   // Ctrl+H — context-sensitive: faces -> faces, bodies -> bodies
  const QList<SelItem> items = visTargetsOfSelection();
  if (items.isEmpty()) { statusBar()->showMessage("Gizle (Ctrl+H): önce bir yüz ya da gövde seçin.", 4000); return; }
  hideItems(items);
}

void MainWindow::hideOtherBodies() {     // Alt+H
  const QList<SelItem> items = visTargetsOfSelection();
  if (items.isEmpty()) { statusBar()->showMessage("Diğerlerini gizle (Alt+H): önce bir yüz ya da gövde seçin.", 4000); return; }
  isolateItems(items);
}

void MainWindow::showAllBodies() { showAllItems(); }   // Ctrl+Shift+H

void MainWindow::toggleSelectionTransparent() {
  const QList<SelItem> items = visTargetsOfSelection();
  if (items.isEmpty()) { statusBar()->showMessage("Saydam yap: önce bir yüz ya da gövde seçin.", 4000); return; }
  bool allOn = true;
  for (const SelItem& s : items) {
    if (s.kind == SelItem::Kind::Body) { if (!transparentBodies_.count(s.name.toStdString())) allOn = false; }
    else if (!transparentFaces_.count(s.name.toStdString())) allOn = false;
  }
  setItemsTransparent(items, !allOn);
}

// Adds the Gizle / Göster items for `face` (under the cursor, may be empty) to a viewport context menu.
void MainWindow::addVisibilityMenu(QMenu* m, const QString& face) {
  const QList<SelItem> targets = visTargetsOfSelection();
  m->addSeparator();
  if (!targets.isEmpty()) {
    int nf = 0, nb = 0;
    for (const SelItem& s : targets) (s.kind == SelItem::Kind::Body ? nb : nf)++;
    QAction* h = m->addAction(makeIcon("hide", 16), (nb && !nf ? "Gövdeyi gizle" : nf > 1 ? QString("Gizle (%1 yüz)").arg(nf) : QString("Gizle")) + "\tCtrl+H",
                              this, [this] { hideSelectedBodies(); });
    h->setObjectName("ctxHide");
    h->setToolTip("Seçili yüzleri / gövdeleri gizle (görünüm durumu; geometri değişmez)");
    m->addAction(makeIcon("hideothers", 16), "Diğerlerini gizle\tAlt+H", this, [this] { hideOtherBodies(); })->setObjectName("ctxHideOthers");
  }
  const QString body = face.isEmpty() ? QString() : QString::fromStdString(model_.bodyOfFace(face.toStdString()));
  if (!body.isEmpty()) {
    m->addAction(makeIcon("hide", 16), "Gövdeyi gizle: " + body, this, [this, body] { setBodyVisible(body, false); })->setObjectName("ctxHideBody");
    m->addAction(makeIcon("hideothers", 16), "Yalnız bu gövde", this, [this, body] { isolateItems({SelItem{SelItem::Kind::Body, body}}); })
        ->setObjectName("ctxIsolateBody");
    const QStringList hf = hiddenFacesOfBody(body);
    if (!hf.isEmpty()) {
      QList<SelItem> its;
      for (const QString& f : hf) its << SelItem{SelItem::Kind::Face, f};
      m->addAction(makeIcon("show", 16), QString("Bu gövdenin gizli yüzlerini göster (%1)").arg(hf.size()), this, [this, its] { showItems(its); })
          ->setObjectName("ctxShowBodyFaces");
    }
  }
  if (!targets.isEmpty()) {
    bool allOn = true;
    for (const SelItem& s : targets)
      if (s.kind == SelItem::Kind::Body ? !transparentBodies_.count(s.name.toStdString()) : !transparentFaces_.count(s.name.toStdString())) allOn = false;
    m->addAction(makeIcon("transparent", 16), allOn ? "Saydamlığı kaldır" : "Saydam yap", this, [this] { toggleSelectionTransparent(); })
        ->setObjectName("ctxTransparent");
  }
  if (!hiddenFaces_.empty()) {
    const QString t = visibilityStatusText();
    m->addAction(makeIcon("showall", 16), "Tümünü göster  (" + t + ")\tCtrl+Shift+H", this, [this] { showAllItems(); })->setObjectName("ctxShowAll");
  }
}

} // namespace pcd
