// Named selections (Adlandırılmış seçimler) in the designer — SpaceClaim "Groups > Named Selections".
//   * Ctrl+G with model faces / edges / bodies selected in 3D (or Gruplar > "Adlandırılmış seçim oluştur", or right click
//     on a face / edge) creates a selection named Grup<k>; its name opens in the inline editor: type, Enter. Create +
//     typed name = ONE undo step. Measurable sketch / dimension selections keep making parameters (ParamSelect).
//   * Gruplar panel: click = select + highlight in the selection's colour, F2 / double click = rename, Del = delete,
//     right click = Yeniden adlandır / Sil / Seçimi değiştir / ekle / çıkar / Renk / STEP'e aktar / Yeniden bağla /
//     Vurguyu göster.
//   * After every regeneration the selections are resolved (pc::model::NamedSelectionManager); status changes
//     (Bölündü / Şüpheli / Kayıp ...) show in the panel and as a non-modal status-bar message — never a dialog.
//   * Edits that only touch named selections are NsCommand undo steps that do NOT regenerate (geometry unchanged).
// Model: src/model/pc/NamedSelections.h, docs/named_selections/MODEL.md. Manual: docs/KULLANIM_KILAVUZU.md §3.6.
#include "Icons.h"
#include "MainWindow.h"
#include "NamedSelectionPanel.h"
#include "PropertiesPanel.h"
#include "StatusBarWidgets.h"
#include "StructurePanel.h"
#include <QAbstractItemDelegate>
#include <QColorDialog>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoCommand>
#include <algorithm>
#include <set>

using namespace pc;

namespace pcd {

// ------------------------------------------------------------------------------ undo step (no regeneration)
class NsCommand : public QUndoCommand {
public:
  NsCommand(MainWindow* w, Document before, Document after, const QString& text, std::string createdId, std::string renamedId)
      : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)), created_(std::move(createdId)),
        renamed_(std::move(renamedId)) {}
  int id() const override { return 0x4E53; }   // 'NS'
  // "Ctrl+G, type the name, Enter" is ONE step: the rename typed into the editor opened by the creation joins it
  bool mergeWith(const QUndoCommand* other) override {
    const auto* o = static_cast<const NsCommand*>(other);
    if (created_.empty() || o->renamed_ != created_) return false;
    after_ = o->after_;
    std::string name;
    for (const auto& ns : after_.namedSelections) if (ns.id == created_) name = ns.name;
    setText(QString("Adlandırılmış seçim oluştur: %1").arg(QString::fromStdString(name)));
    return true;
  }
  void redo() override { w_->applyNsDocument(after_); }
  void undo() override { w_->applyNsDocument(before_); }

private:
  MainWindow* w_;
  Document before_, after_;
  std::string created_, renamed_;
};

namespace {
QString kindText(NsMemberKind k) { return k == NsMemberKind::Edge ? "kenar" : k == NsMemberKind::Body ? "gövde" : "yüz"; }
QStringList qlist(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& s : v) l << QString::fromStdString(s);
  return l;
}
QIcon colorIcon(const QColor& c) {
  QPixmap pm(16, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.darker(150), 1));
  p.setBrush(c);
  p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
  return QIcon(pm);
}
// same palette as pc::model::ns (create's default colours), so "Renk" offers the familiar ones
const char* kPalette[] = {"#E8A33D", "#3D8BE8", "#4CB050", "#D9534F", "#9B59B6", "#1ABC9C", "#F1C40F", "#7F8C8D"};
} // namespace

// ------------------------------------------------------------------------------ construction
void MainWindow::createNamedSelectionPanel(QWidget* groupsPage) {
  nsPanel_ = new NamedSelectionPanel;
  if (auto* sp = qobject_cast<QSplitter*>(groupsPage)) {
    sp->addWidget(nsPanel_);
    sp->setStretchFactor(0, 3);
    sp->setStretchFactor(1, 2);
  } else if (groupsPage && groupsPage->layout()) {
    groupsPage->layout()->addWidget(nsPanel_);
  }
  connect(nsPanel_, &NamedSelectionPanel::activated, this, [this](const QString& id) { selectNamedSelection(id); });
  connect(nsPanel_, &NamedSelectionPanel::renameRequested, this, [this](const QString& id, const QString& to) {
    const bool join = nsPendingRename_ == id.toStdString();   // decided now: the editor closes right after this
    nsPendingRename_.clear();
    QTimer::singleShot(0, this, [this, id, to, join] { nsRename(id, to, join); });
  });
  connect(nsPanel_->tree()->itemDelegate(), &QAbstractItemDelegate::closeEditor, this, [this] { nsPendingRename_.clear(); });
  connect(nsPanel_, &NamedSelectionPanel::contextMenuRequested, this,
          [this](const QString& id, int member, const QPoint& g) { showNamedSelectionMenu(id, member, g); });
  connect(nsPanel_, &NamedSelectionPanel::createRequested, this, [this] { createNamedSelectionFromSelection(true); });
  connect(nsPanel_, &NamedSelectionPanel::replaceRequested, this, [this](const QString& id) { replaceNamedSelectionMembers(id); });
  connect(nsPanel_, &NamedSelectionPanel::deleteRequested, this, [this](const QString& id) { removeNamedSelection(id); });
  nsMgr_.subscribe([this](const model::NsChange& c) {
    if (c.kind != model::NsChange::Kind::StatusChanged || c.oldStatus == c.newStatus) return;
    QString d = QString::fromStdString(c.diagnostic);
    if (d.size() > 140) d = d.left(137) + "...";
    nsMsgs_ << QString("Adlandırılmış seçim '%1': %2 → %3%4")
                   .arg(QString::fromStdString(c.name), nsStatusText(c.oldStatus), nsStatusText(c.newStatus),
                        d.isEmpty() ? QString() : " (" + d + ")");
  });
}

// ------------------------------------------------------------------------------ resolution / panel
void MainWindow::nsUpdate() {
  nsMsgs_.clear();
  try { nsMgr_.update(doc_, model_, rollback_); } catch (...) {}
  const bool quiet = nsQuiet_;
  nsQuiet_ = false;
  // status-bar indicator (permanent, non-modal): problems at a glance
  if (!nsStatusLbl_ && sbw_) {
    nsStatusLbl_ = new QLabel;
    nsStatusLbl_->setObjectName("nsStatus");
    nsStatusLbl_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    nsStatusLbl_->setContentsMargins(6, 0, 6, 0);
    sbw_->addPermanent(nsStatusLbl_);
  }
  if (nsStatusLbl_) {
    int bad = 0;
    QStringList tip;
    for (const auto& r : nsMgr_.resolutions()) {
      if (r.status != model::NsStatus::Ok && r.status != model::NsStatus::Split && r.status != model::NsStatus::Merged) ++bad;
      tip << QString("%1: %2").arg(QString::fromStdString(r.name), nsStatusText(r.status));
    }
    const int n = int(nsMgr_.resolutions().size());
    nsStatusLbl_->setVisible(n > 0);
    nsStatusLbl_->setText(bad ? QString("Ad. seçim: %1 · %2 dikkat").arg(n).arg(bad) : QString("Ad. seçim: %1").arg(n));
    nsStatusLbl_->setStyleSheet(bad ? "color:#b5651d;" : "");
    nsStatusLbl_->setToolTip("Adlandırılmış seçimler (Gruplar sekmesi)\n" + tip.join("\n"));
  }
  if (!quiet && !nsMsgs_.isEmpty()) {
    // after the commit's own prompts (a tool writes its status right after the regeneration)
    const QString m = nsMsgs_.join("  ·  ");
    statusBar()->showMessage(m, 15000);
    QTimer::singleShot(0, this, [this, m] { statusBar()->showMessage(m, 15000); });
  }
}

void MainWindow::nsRebuildPanel() {
  if (!nsPanel_) return;
  QList<NamedSelectionPanel::Row> rows;
  for (const auto& ns : doc_.namedSelections) {
    const model::NsResolution* r = nsMgr_.find(QString::fromStdString(ns.id).toStdString());
    NamedSelectionPanel::Row row;
    row.id = QString::fromStdString(ns.id);
    row.name = QString::fromStdString(ns.name);
    row.color = QString::fromStdString(ns.color);
    row.exportStep = ns.exportStep;
    row.tinted = nsTint_.count(ns.id) > 0;
    int nf = 0, ne = 0, nb = 0;
    std::set<std::string> fs, es, bs;
    if (r) {
      row.status = r->status;
      row.diagnostic = QString::fromStdString(r->diagnostic);
      for (const auto& m : r->members) {
        NamedSelectionPanel::MemberRow mr;
        mr.index = int(m.index);
        mr.kind = kindText(m.kind);
        mr.ref = QString::fromStdString(m.ref);
        mr.status = m.status;
        mr.diagnostic = QString::fromStdString(m.diagnostic);
        mr.suggestions = qlist(m.suggestions);
        if (m.kind == NsMemberKind::Face) { mr.current = qlist(m.faces); fs.insert(m.faces.begin(), m.faces.end()); }
        else if (m.kind == NsMemberKind::Edge) { mr.current = qlist(m.edges); es.insert(m.edges.begin(), m.edges.end()); }
        else { mr.current = qlist(m.bodies); bs.insert(m.bodies.begin(), m.bodies.end()); }
        row.members << mr;
      }
    }
    nf = int(fs.size()), ne = int(es.size()), nb = int(bs.size());
    QStringList parts;
    if (nf) parts << QString("%1 yüz").arg(nf);
    if (ne) parts << QString("%1 kenar").arg(ne);
    if (nb) parts << QString("%1 gövde").arg(nb);
    row.countText = parts.isEmpty() ? QString("0 / %1 üye").arg(ns.members.size()) : parts.join(", ");
    rows << row;
  }
  nsPanel_->setRows(rows);
  if (!nsActive_.isEmpty()) nsPanel_->setCurrentId(nsActive_);
}

void MainWindow::appendNsLabels(std::vector<viewer::Label3D>& labels) {
  nsLabelTexts_.clear();
  if (!showNsLabels_ || sketching_) return;
  for (const auto& r : nsMgr_.resolutions()) {
    if (!model::isResolved(r.status)) continue;
    NsSignature best;
    for (const auto& f : r.faces) {
      if (hiddenFaces_.count(f)) continue;   // hidden face / face of a hidden body (Gizle / Göster)
      const NsSignature s = model::faceSignature(model_, f);
      if (s.valid && (!best.valid || s.size > best.size)) best = s;
    }
    if (!best.valid)
      for (const auto& e : r.edges) {
        const NsSignature s = model::edgeSignature(model_, e);
        if (s.valid && (!best.valid || s.size > best.size)) best = s;
      }
    if (!best.valid)
      for (const auto& b : r.bodies) if (!best.valid) best = model::bodySignature(model_, b);
    if (!best.valid) continue;
    viewer::Label3D l;
    // a plane label floats a little above its face (outward normal)
    const double lift = best.surface == "plane" ? 0.5 : 0.0;
    l.pos[0] = best.cx + lift * best.nx;
    l.pos[1] = best.cy + lift * best.ny;
    l.pos[2] = best.cz + lift * best.nz;
    // Split is normal (the name covers every piece): only states that need a look are spelled out
    const bool plain = r.status == model::NsStatus::Ok || r.status == model::NsStatus::Split;
    l.text = QString::fromStdString(r.name) + (plain ? QString() : " (" + nsStatusText(r.status) + ")");
    labels.push_back(l);
    nsLabelTexts_ << l.text;
  }
}

void MainWindow::nsApplyDecorations() {
  applyBodyAppearance();
  std::vector<viewer::Label3D> labels = sketchLabels_;
  appendNsLabels(labels);
  viewer_->setLabels(labels);
  if (actNsLabels_ && actNsLabels_->isChecked() != showNsLabels_) {
    const QSignalBlocker b(actNsLabels_);
    actNsLabels_->setChecked(showNsLabels_);
  }
}

void MainWindow::applyNsDocument(const Document& d) {
  doc_ = d;
  exprWork_ = doc_;
  if (sketching_) work_.namedSelections = d.namedSelections;
  if (dlgWork_) dlgWork_->namedSelections = d.namedSelections;
  nsUpdate();
  nsRebuildPanel();
  nsApplyDecorations();
  if (!selection_->isEmpty()) highlightSelection();
  schedulePanelsRefresh();
  if (tools_) tools_->onDocumentChanged();
  updateTitle();
}

bool MainWindow::nsCommit(const Document& cand, const QString& text, const std::string& createdId, const std::string& renamedId) {
  nsPendingRename_.clear();
  undo_.push(new NsCommand(this, doc_, cand, text, createdId, renamedId));
  return true;
}

// ------------------------------------------------------------------------------ lookups
const NamedSelection* MainWindow::nsFind(const QString& nameOrId) const {
  const std::string k = nameOrId.toStdString();
  for (const auto& ns : doc_.namedSelections) if (ns.id == k) return &ns;
  for (const auto& ns : doc_.namedSelections) if (sameSelectionName(ns.name, k)) return &ns;
  return nullptr;
}

std::vector<model::ns::Ref> MainWindow::nsRefsOf(const QList<SelItem>& items) const {
  std::vector<model::ns::Ref> refs;
  for (const SelItem& it : items) {
    const std::string n = it.name.toStdString();
    if (it.kind == SelItem::Kind::Face && model_.faces.count(n)) refs.push_back(model::ns::face(n));
    else if (it.kind == SelItem::Kind::Edge && !it.name.startsWith("sketch:") && model_.edges.count(n)) refs.push_back(model::ns::edge(n));
    else if (it.kind == SelItem::Kind::Body && model_.body(n)) refs.push_back(model::ns::body(n));
  }
  return refs;
}

QList<SelItem> MainWindow::nsItemsOf(const model::NsResolution& r) const {
  QList<SelItem> items;
  auto add = [&](SelItem::Kind k, const std::string& n) {
    const SelItem s{k, QString::fromStdString(n)};
    if (!items.contains(s)) items << s;
  };
  for (const auto& m : r.members) {
    if (m.kind == NsMemberKind::Face) for (const auto& f : m.faces) add(SelItem::Kind::Face, f);
    else if (m.kind == NsMemberKind::Edge) for (const auto& e : m.edges) add(SelItem::Kind::Edge, e);
    else for (const auto& b : m.bodies) add(SelItem::Kind::Body, b);
  }
  return items;
}

QStringList MainWindow::namedSelectionsOf(const QList<SelItem>& items) const {
  QStringList out;
  if (items.isEmpty()) return out;
  for (const auto& r : nsMgr_.resolutions()) {
    bool all = true;
    for (const SelItem& it : items) {
      const std::string n = it.name.toStdString();
      const auto& v = it.kind == SelItem::Kind::Face ? r.faces : it.kind == SelItem::Kind::Edge ? r.edges : r.bodies;
      if (!(it.kind == SelItem::Kind::Face || it.kind == SelItem::Kind::Edge || it.kind == SelItem::Kind::Body) ||
          std::find(v.begin(), v.end(), n) == v.end()) { all = false; break; }
    }
    if (all) out << QString::fromStdString(r.name);
  }
  return out;
}

QString MainWindow::nsHoverSuffix(const QString& entity) const {
  if (entity.isEmpty()) return QString();
  QStringList names = qlist(nsMgr_.selectionsOfFace(entity.toStdString()));
  if (names.isEmpty())
    for (const auto& r : nsMgr_.resolutions())
      if (std::find(r.edges.begin(), r.edges.end(), entity.toStdString()) != r.edges.end()) names << QString::fromStdString(r.name);
  return names.isEmpty() ? QString() : QString::fromUtf8(" — Ad: ") + names.join(", ");
}

QString MainWindow::hoverInfoText() const { return lblHover_ ? lblHover_->text() : QString(); }

bool MainWindow::namedSelectionTinted(const QString& nameOrId) const {
  const NamedSelection* ns = nsFind(nameOrId);
  return ns && nsTint_.count(ns->id);
}

// ------------------------------------------------------------------------------ editing
bool MainWindow::canCreateNamedSelection() const {
  if (sketching_ || planePick_ || featDlg_) return false;
  return !nsRefsOf(selection_->items()).empty();
}

bool MainWindow::createNamedSelectionFromSelection(bool startRename) {
  if (sketching_) {
    statusBar()->showMessage("Adlandırılmış seçim: sketch modundan çıkın (D) ve 3B görünümde yüz / kenar / gövde seçin.", 6000);
    return false;
  }
  if (featDlg_) { statusBar()->showMessage("Adlandırılmış seçim: önce açık özellik penceresini kapatın.", 5000); return false; }
  const auto refs = nsRefsOf(selection_->items());
  if (refs.empty()) {
    statusBar()->showMessage("Adlandırılmış seçim oluştur (Ctrl+G): önce 3B görünümde yüz, kenar ya da gövde seçin "
                             "(Ctrl+tık ile birden çok).", 6000);
    return false;
  }
  Document cand = doc_;
  std::string err, id;
  const std::string name = model::ns::suggestName(doc_, "Grup");
  if (!model::ns::create(cand, name, refs, model_, rollback_, &err, &id)) {
    statusBar()->showMessage("Adlandırılmış seçim oluşturulamadı: " + QString::fromStdString(err), 8000);
    return false;
  }
  const QList<SelItem> sel = selection_->items();
  nsCommit(cand, QString("Adlandırılmış seçim oluştur: %1").arg(QString::fromStdString(name)), id);
  nsActive_ = QString::fromStdString(id);
  nsActiveItems_ = sel;
  highlightSelection();
  if (structure_) structure_->setCurrentPage("groups");
  if (dockStructure_ && !dockStructure_->isVisible()) dockStructure_->show();
  nsPanel_->setCurrentId(nsActive_);
  int nf = 0, ne = 0, nb = 0;
  for (const auto& r : refs) (r.kind == NsMemberKind::Face ? nf : r.kind == NsMemberKind::Edge ? ne : nb)++;
  QStringList what;
  if (nf) what << QString("%1 yüz").arg(nf);
  if (ne) what << QString("%1 kenar").arg(ne);
  if (nb) what << QString("%1 gövde").arg(nb);
  statusBar()->showMessage(QString("Adlandırılmış seçim %1 oluşturuldu (%2) — adı yazıp Enter'a basın (Esc: %1 kalır).")
                               .arg(QString::fromStdString(name), what.join(", ")), 10000);
  if (startRename) {
    nsPendingRename_ = id;
    nsPanel_->beginRename(nsActive_);
  }
  return true;
}

bool MainWindow::nsRename(const QString& nameOrId, const QString& to0, bool joinCreateStep) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return false;
  const QString to = to0.trimmed();
  const QString from = QString::fromStdString(ns->name);
  const std::string id = ns->id;
  if (to == from) { nsRebuildPanel(); return true; }
  Document cand = doc_;
  std::string err;
  if (!model::ns::rename(cand, ns->name, to.toStdString(), &err)) {
    statusBar()->showMessage(QString("'%1' adı verilemedi: %2").arg(to, QString::fromStdString(err)), 8000);
    nsRebuildPanel();   // back to the stored name
    if (joinCreateStep) {   // still naming the new selection: let the user type again
      nsPendingRename_ = id;
      nsPanel_->beginRename(QString::fromStdString(id));
    }
    return false;
  }
  nsCommit(cand, QString("Adlandırılmış seçimi yeniden adlandır: %1 → %2").arg(from, to), std::string(), joinCreateStep ? id : std::string());
  statusBar()->showMessage(QString("Adlandırılmış seçim: %1").arg(to) +
                               (joinCreateStep ? QString(" — ad sonraki işlemlerle değişmez.") : QString(" (önceki ad: %1)").arg(from)), 6000);
  return true;
}

bool MainWindow::renameNamedSelection(const QString& nameOrId, const QString& to) { return nsRename(nameOrId, to, false); }

bool MainWindow::removeNamedSelection(const QString& nameOrId) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return false;
  const QString name = QString::fromStdString(ns->name);
  const std::string id = ns->id;
  Document cand = doc_;
  std::string err;
  if (!model::ns::remove(cand, ns->name, &err)) { statusBar()->showMessage(QString::fromStdString(err), 6000); return false; }
  if (nsActive_.toStdString() == id) { nsActive_.clear(); nsActiveItems_.clear(); }
  nsTint_.erase(id);
  nsCommit(cand, QString("Adlandırılmış seçimi sil: %1").arg(name));
  statusBar()->showMessage(QString("Adlandırılmış seçim %1 silindi (geometri değişmedi) — Ctrl+Z ile geri alınır.").arg(name), 6000);
  return true;
}

void MainWindow::selectNamedSelection(const QString& nameOrId) {
  const NamedSelection* ns = nsFind(nameOrId);
  const model::NsResolution* r = ns ? nsMgr_.find(ns->id) : nullptr;
  if (!ns || !r) return;
  const QList<SelItem> items = nsItemsOf(*r);
  nsPanel_->setCurrentId(QString::fromStdString(ns->id));
  if (items.isEmpty()) {
    statusBar()->showMessage(QString("%1: %2 — %3").arg(QString::fromStdString(ns->name), nsStatusText(r->status),
                                                         r->diagnostic.empty() ? QString("gösterilecek öğe yok") : QString::fromStdString(r->diagnostic)), 10000);
    return;
  }
  nsActive_ = QString::fromStdString(ns->id);
  nsActiveItems_ = items;
  if (selection_->items() == items) highlightSelection();
  else selection_->set(items);   // onSelectionChanged keeps nsActive_ (same items) and highlights in its colour
  QString what = QString("%1 öğe").arg(items.size());
  if (r->faces.size() == size_t(items.size())) what = QString("%1 yüz").arg(items.size());
  statusBar()->showMessage(QString("%1: %2 seçildi (%3)%4").arg(QString::fromStdString(ns->name), what, nsStatusText(r->status),
                                                                 r->diagnostic.empty() ? QString() : " — " + QString::fromStdString(r->diagnostic)), 8000);
}

bool MainWindow::replaceNamedSelectionMembers(const QString& nameOrId) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return false;
  const auto refs = nsRefsOf(selection_->items());
  if (refs.empty()) { statusBar()->showMessage("Seçimi değiştir: önce 3B görünümde yeni yüz / kenar / gövdeleri seçin.", 6000); return false; }
  Document cand = doc_;
  for (auto& s : cand.namedSelections) if (s.id == ns->id) s.members.clear();
  std::string err;
  if (!model::ns::addMembers(cand, ns->name, refs, model_, rollback_, &err)) {
    statusBar()->showMessage("Seçimi değiştir: " + QString::fromStdString(err), 6000);
    return false;
  }
  nsCommit(cand, QString("Adlandırılmış seçimi değiştir: %1").arg(QString::fromStdString(ns->name)));
  statusBar()->showMessage(QString("%1 artık geçerli seçimi kapsıyor (%2 öğe).").arg(QString::fromStdString(ns->name)).arg(refs.size()), 6000);
  return true;
}

bool MainWindow::editNamedSelectionMembers(const QString& nameOrId, bool add) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return false;
  const auto refs = nsRefsOf(selection_->items());
  if (refs.empty()) { statusBar()->showMessage("Önce 3B görünümde yüz / kenar / gövde seçin.", 5000); return false; }
  Document cand = doc_;
  std::string err;
  const bool ok = add ? model::ns::addMembers(cand, ns->name, refs, model_, rollback_, &err)
                      : model::ns::removeMembers(cand, ns->name, refs, model_, rollback_, &err);
  if (!ok) { statusBar()->showMessage(QString::fromStdString(err), 6000); return false; }
  nsCommit(cand, QString(add ? "Adlandırılmış seçime ekle: %1" : "Adlandırılmış seçimden çıkar: %1").arg(QString::fromStdString(ns->name)));
  return true;
}

bool MainWindow::rebindNamedSelectionMember(const QString& nameOrId, int memberIndex, const SelItem& to) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns || memberIndex < 0) return false;
  const auto refs = nsRefsOf({to});
  if (refs.empty()) { statusBar()->showMessage("Yeniden bağla: hedef modelde yok.", 5000); return false; }
  Document cand = doc_;
  std::string err;
  if (!model::ns::rebindMember(cand, ns->name, size_t(memberIndex), refs.front(), model_, &err)) {
    statusBar()->showMessage("Yeniden bağlanamadı: " + QString::fromStdString(err), 8000);
    return false;
  }
  nsCommit(cand, QString("Adlandırılmış seçimi yeniden bağla: %1 → %2").arg(QString::fromStdString(ns->name), to.name));
  statusBar()->showMessage(QString("%1: üye %2 artık %3 (siz seçtiniz; otomatik bağlama yapılmaz).")
                               .arg(QString::fromStdString(ns->name)).arg(memberIndex + 1).arg(to.name), 8000);
  return true;
}

bool MainWindow::setNamedSelectionColor(const QString& nameOrId, const QColor& c) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns || !c.isValid()) return false;
  const std::string col = c.name().toUpper().toStdString();
  if (col == ns->color) return true;
  Document cand = doc_;
  std::string err;
  if (!model::ns::setColor(cand, ns->name, col, &err)) { statusBar()->showMessage(QString::fromStdString(err), 5000); return false; }
  return nsCommit(cand, QString("Adlandırılmış seçim rengi: %1").arg(QString::fromStdString(ns->name)));
}

bool MainWindow::setNamedSelectionExportStep(const QString& nameOrId, bool on) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return false;
  if (ns->exportStep == on) return true;
  Document cand = doc_;
  std::string err;
  if (!model::ns::setExportStep(cand, ns->name, on, &err)) { statusBar()->showMessage(QString::fromStdString(err), 5000); return false; }
  return nsCommit(cand, QString("%1: STEP'e aktar %2").arg(QString::fromStdString(ns->name), on ? "açık" : "kapalı"));
}

void MainWindow::setNamedSelectionTint(const QString& nameOrId, bool on) {
  const NamedSelection* ns = nsFind(nameOrId);
  if (!ns) return;
  if (on) nsTint_.insert(ns->id);
  else nsTint_.erase(ns->id);
  nsApplyDecorations();
  nsRebuildPanel();
}

bool MainWindow::nameItems(const QList<SelItem>& items, const QString& name0) {
  const QString name = name0.trimmed();
  const auto refs = nsRefsOf(items);
  if (refs.empty()) return false;
  const QStringList cur = namedSelectionsOf(items);
  Document cand = doc_;
  std::string err;
  if (name.isEmpty()) {   // clear: take the items out of their selections
    if (cur.isEmpty()) return true;
    for (const QString& n : cur)
      if (!model::ns::removeMembers(cand, n.toStdString(), refs, model_, rollback_, &err)) {
        statusBar()->showMessage(QString::fromStdString(err), 6000);
        return false;
      }
    return nsCommit(cand, QString("Adlandırılmış seçimden çıkar: %1").arg(cur.join(", ")));
  }
  for (const QString& n : cur) if (sameSelectionName(n.toStdString(), name.toStdString())) return true;
  if (cur.size() == 1 && !nsFind(name)) {
    // the selection consists of exactly these items: the "Ad" edit renames it
    if (const model::NsResolution* r = nsMgr_.find(cur.front().toStdString())) {
      std::set<std::string> mine, sel;
      for (const auto& m : r->members) {
        const auto& v = m.kind == NsMemberKind::Face ? m.faces : m.kind == NsMemberKind::Edge ? m.edges : m.bodies;
        mine.insert(v.begin(), v.end());
      }
      for (const auto& rf : refs) sel.insert(rf.name);
      if (mine == sel) return renameNamedSelection(cur.front(), name);
    }
  }
  if (const NamedSelection* ex = nsFind(name)) {
    if (!model::ns::addMembers(cand, ex->name, refs, model_, rollback_, &err)) {
      statusBar()->showMessage(QString::fromStdString(err), 6000);
      return false;
    }
    statusBar()->showMessage(QString("Seçili öğeler %1 adlandırılmış seçimine eklendi.").arg(QString::fromStdString(ex->name)), 6000);
    return nsCommit(cand, QString("Adlandırılmış seçime ekle: %1").arg(QString::fromStdString(ex->name)));
  }
  std::string id;
  if (!model::ns::create(cand, name.toStdString(), refs, model_, rollback_, &err, &id)) {
    statusBar()->showMessage(QString("'%1' adı verilemedi: %2").arg(name, QString::fromStdString(err)), 8000);
    return false;
  }
  statusBar()->showMessage(QString("Adlandırılmış seçim %1 oluşturuldu — ad sonraki işlemlerle değişmez.").arg(name), 6000);
  return nsCommit(cand, QString("Adlandırılmış seçim oluştur: %1").arg(name));
}

// ------------------------------------------------------------------------------ context menu
void MainWindow::showNamedSelectionMenu(const QString& nameOrId, int memberIndex, const QPoint& g) {
  auto* m = new QMenu(this);
  m->setObjectName("nsContextMenu");
  m->setAttribute(Qt::WA_DeleteOnClose);
  const NamedSelection* ns = nameOrId.isEmpty() ? nullptr : nsFind(nameOrId);
  QAction* create = m->addAction(makeIcon("namedsel", 16), "Adlandırılmış seçim oluştur\tCtrl+G", this, [this] { createNamedSelectionFromSelection(true); });
  create->setObjectName("ns.create");
  create->setEnabled(canCreateNamedSelection());
  if (!ns) {
    m->popup(g);
    return;
  }
  const QString id = QString::fromStdString(ns->id);
  const QString name = QString::fromStdString(ns->name);
  const model::NsResolution* r = nsMgr_.find(ns->id);
  const bool haveSel = canCreateNamedSelection();
  m->addSeparator();
  auto* hdr = m->addAction(colorIcon(QColor(QString::fromStdString(ns->color))), name + (r ? "  (" + nsStatusText(r->status) + ")" : QString()));
  hdr->setEnabled(false);
  m->addAction(makeIcon("select", 16), "Seç ve vurgula", this, [this, id] { selectNamedSelection(id); })->setObjectName("ns.select");
  m->addAction("Yeniden adlandır\tF2", this, [this, id] { nsPanel_->beginRename(id); })->setObjectName("ns.rename");
  m->addAction(makeIcon("delete", 16), "Sil\tDel", this, [this, id] { removeNamedSelection(id); })->setObjectName("ns.delete");
  m->addSeparator();
  // Gizle / Göster for the selection's faces / bodies (e.g. hide "Dis_duvar" to look inside)
  if (r && model::isResolved(r->status)) {
    const QList<SelItem> its = nsItemsOf(*r);
    bool anyShown = false, anyHidden = false;
    for (const SelItem& s : its) {
      const bool h = s.kind == SelItem::Kind::Body ? hiddenBodies_.count(s.name.toStdString()) > 0 : faceHidden(s.name);
      (h ? anyHidden : anyShown) = true;
    }
    QAction* a = m->addAction(makeIcon("hide", 16), "Gizle", this, [this, its] { hideItems(its); });
    a->setObjectName("ns.hide");
    a->setEnabled(anyShown);
    a = m->addAction(makeIcon("show", 16), "Göster", this, [this, its] { showItems(its); });
    a->setObjectName("ns.show");
    a->setEnabled(anyHidden);
    m->addAction(makeIcon("hideothers", 16), "Diğerlerini gizle", this, [this, its] { isolateItems(its); })->setObjectName("ns.hideOthers");
    m->addSeparator();
  }
  QAction* rep = m->addAction(makeIcon("namedsel.replace", 16), "Seçimi değiştir", this, [this, id] { replaceNamedSelectionMembers(id); });
  rep->setObjectName("ns.replace");
  rep->setToolTip("Üyeleri 3B görünümdeki geçerli seçimle değiştir");
  rep->setEnabled(haveSel);
  QAction* add = m->addAction(makeIcon("namedsel.add", 16), "Seçili ögeleri ekle", this, [this, id] { editNamedSelectionMembers(id, true); });
  add->setObjectName("ns.add");
  add->setEnabled(haveSel);
  QAction* rem = m->addAction(makeIcon("namedsel.remove", 16), "Seçili ögeleri çıkar", this, [this, id] { editNamedSelectionMembers(id, false); });
  rem->setObjectName("ns.remove");
  rem->setEnabled(haveSel);
  // Yeniden bağla: only for members that are not resolved (kayıp ...) or suspect; every choice is explicit
  QMenu* rb = m->addMenu(makeIcon("namedsel.rebind", 16), "Yeniden bağla");
  rb->setObjectName("ns.rebindMenu");
  int choices = 0;
  if (r) {
    const QList<SelItem> cur = selection_->items();
    const auto curRefs = nsRefsOf(cur);
    for (const auto& mr : r->members) {
      if (memberIndex >= 0 && int(mr.index) != memberIndex) continue;
      if (model::isResolved(mr.status) && mr.status != model::NsStatus::Suspect) continue;
      QMenu* sub = rb->addMenu(nsStatusIcon(mr.status), QString("Üye %1: %2 (%3)").arg(mr.index + 1).arg(QString::fromStdString(mr.ref), nsStatusText(mr.status)));
      if (!mr.diagnostic.empty()) sub->setToolTip(QString::fromStdString(mr.diagnostic));
      sub->setToolTipsVisible(true);
      const int idx = int(mr.index);
      for (const auto& sug : mr.suggestions) {
        const QString f = QString::fromStdString(sug);
        QAction* a = sub->addAction(makeIcon("pull", 14), "Öneri: " + f, this,
                                    [this, id, idx, f] { rebindNamedSelectionMember(id, idx, SelItem{SelItem::Kind::Face, f}); });
        a->setObjectName("ns.rebind.suggestion");
        a->setToolTip("Benzer yüz (imza puanı). Seçmeden önce görünümde vurgulanır.");
        connect(a, &QAction::hovered, this, [this, f] { viewer_->setHighlightedFaces({f}, QColor(255, 150, 30)); });
        ++choices;
      }
      if (curRefs.size() == 1) {
        SelItem t;
        for (const SelItem& c : cur) if (!nsRefsOf({c}).empty()) t = c;
        QAction* a = sub->addAction(makeIcon("select", 14), "Seçili ögeye bağla: " + t.name, this,
                                    [this, id, idx, t] { rebindNamedSelectionMember(id, idx, t); });
        a->setObjectName("ns.rebind.selection");
        ++choices;
      }
      if (sub->isEmpty()) sub->addAction("(öneri yok — 3B'de hedefi seçip yeniden açın)")->setEnabled(false);
    }
  }
  rb->setEnabled(choices > 0);
  if (rb->isEmpty()) rb->addAction("(kayıp üye yok)")->setEnabled(false);
  connect(rb, &QMenu::aboutToHide, this, [this] { highlightSelection(); });
  m->addSeparator();
  QMenu* cm = m->addMenu(colorIcon(QColor(QString::fromStdString(ns->color))), "Renk");
  cm->setObjectName("ns.colorMenu");
  for (const char* c : kPalette) {
    const QColor col(c);
    QAction* a = cm->addAction(colorIcon(col), QString(c), this, [this, id, col] { setNamedSelectionColor(id, col); });
    a->setCheckable(true);
    a->setChecked(QString(c).compare(QString::fromStdString(ns->color), Qt::CaseInsensitive) == 0);
  }
  cm->addSeparator();
  cm->addAction("Özel renk...", this, [this, id] {
    const NamedSelection* cur = nsFind(id);
    const QColor c = QColorDialog::getColor(cur ? QColor(QString::fromStdString(cur->color)) : Qt::white, this, "Adlandırılmış seçim rengi");
    if (c.isValid()) setNamedSelectionColor(id, c);
  });
  QAction* st = m->addAction("STEP'e aktar");
  st->setObjectName("ns.exportStep");
  st->setCheckable(true);
  st->setChecked(ns->exportStep);
  st->setToolTip("Açık: STEP dışa aktarımında üye yüzler bu adı taşır (Fluent Meshing / ANSA / Gmsh bölge adı)");
  connect(st, &QAction::toggled, this, [this, id](bool on) { setNamedSelectionExportStep(id, on); });
  QAction* tint = m->addAction("Vurguyu göster");
  tint->setObjectName("ns.tint");
  tint->setCheckable(true);
  tint->setChecked(nsTint_.count(ns->id) > 0);
  tint->setToolTip("Yüzleri seçimin rengiyle sürekli boya (yalnızca görünüm)");
  connect(tint, &QAction::toggled, this, [this, id](bool on) { setNamedSelectionTint(id, on); });
  m->popup(g);
}

} // namespace pcd
