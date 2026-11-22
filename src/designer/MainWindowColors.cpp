// SC-COLOR: SpaceClaim-like face / body colours (docs/colors/COLORS.md).
//   * Stored in Document::view.colors (one NamedSelection-like group per colour; Face + Body persistent references).
//     They are resolved like hidden faces after every regeneration (pc::model::resolveViewColors): a coloured face
//     that a later feature splits keeps its colour in every piece, a consumed face is ignored. A Body member colours
//     every face of the body unless a face has its own colour.
//   * Every change is ONE undo step (ColorCommand -> applyViewState), no regeneration; saved in the .pcad "view".
//   * Entry points: ribbon Görüntü > Stil (Renk palette + Rengi sıfırla), viewport context menu "Renk ▸",
//     Özellikler panel "Renk" row (face / body). STEP export + Save-as-App carry the colours.
#include "Icons.h"
#include "MainWindow.h"
#include "Ribbon.h"
#include "pc/ViewColors.h"
#include <QActionGroup>
#include <QColorDialog>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QStatusBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QWidgetAction>

using namespace pc;

namespace pcd {

class ColorCommand : public QUndoCommand {
public:
  ColorCommand(MainWindow* w, ViewState before, ViewState after, const QString& text)
      : QUndoCommand(text), w_(w), before_(std::move(before)), after_(std::move(after)) {}
  void redo() override { w_->applyViewState(after_); }
  void undo() override { w_->applyViewState(before_); }

private:
  MainWindow* w_;
  ViewState before_, after_;
};

namespace {
// SpaceClaim-like palette: 5 rows x 8 (basic, light, dark, pastel, greys)
const char* const kSwatches[] = {
    "#ff0000", "#ff8000", "#ffd400", "#00b050", "#00c8c8", "#0070ff", "#7030a0", "#ff00ff",
    "#ff7f7f", "#ffbf7f", "#ffec7f", "#7fd7a0", "#7fe3e3", "#7fb7ff", "#b78fd0", "#ff7fff",
    "#a00000", "#a05000", "#a08400", "#006030", "#007878", "#003c9c", "#401060", "#a000a0",
    "#f4cccc", "#fce5cd", "#fff2cc", "#d9ead3", "#d0e0e3", "#cfe2f3", "#d9d2e9", "#ead1dc",
    "#000000", "#404040", "#808080", "#a0a0a0", "#c0c0c0", "#e0e0e0", "#f0f0f0", "#ffffff"};

QIcon swatchIcon(const QColor& c, int sz = 16) {
  QPixmap pm(sz, sz);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(c.darker(160), 1.2));
  p.setBrush(c);
  p.drawRoundedRect(QRectF(1.5, 1.5, sz - 3, sz - 3), 3, 3);
  return QIcon(pm);
}

QIcon paletteIcon(const QColor& c, int sz = 32) {   // "paint" button: brush over the current colour
  QPixmap pm(sz, sz);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const double u = sz / 32.0;
  p.setPen(Qt::NoPen);
  p.setBrush(c);
  p.drawRoundedRect(QRectF(3 * u, 21 * u, 26 * u, 8 * u), 2 * u, 2 * u);
  p.setPen(QPen(QColor(60, 70, 85), 1.4 * u));
  p.setBrush(QColor(235, 238, 244));
  QPolygonF bristle;
  bristle << QPointF(7 * u, 19 * u) << QPointF(13 * u, 8 * u) << QPointF(18 * u, 11 * u) << QPointF(12 * u, 21 * u);
  p.drawPolygon(bristle);
  p.setBrush(c);
  QPolygonF tip;
  tip << QPointF(13 * u, 8 * u) << QPointF(21 * u, 3 * u) << QPointF(24 * u, 7 * u) << QPointF(18 * u, 11 * u);
  p.drawPolygon(tip);
  return QIcon(pm);
}

bool sameColors(const ViewState& a, const ViewState& b) {
  if (a.colors.size() != b.colors.size()) return false;
  for (size_t i = 0; i < a.colors.size(); ++i) {
    const auto &x = a.colors[i], &y = b.colors[i];
    if (x.color != y.color || x.members.size() != y.members.size()) return false;
    for (size_t k = 0; k < x.members.size(); ++k)
      if (x.members[k].kind != y.members[k].kind || x.members[k].ref != y.members[k].ref) return false;
  }
  return true;
}
} // namespace

// ------------------------------------------------------------------------------ resolution
void MainWindow::colorsUpdate() {
  userFaceColor_.clear();
  userFaceOwnColor_.clear();
  userBodyColor_.clear();
  if (doc_.view.colors.empty() || model_.faces.empty()) return;
  try {
    const model::ViewColorMap m = model::resolveViewColors(doc_, model_, rollback_);
    userFaceColor_ = m.face;
    userFaceOwnColor_ = m.faceOwn;
    userBodyColor_ = m.body;
  } catch (...) {}
}

void MainWindow::applyUserColors(std::map<std::string, QColor>& colors) const {
  for (const auto& [f, hex] : userFaceColor_) colors[f] = QColor(QString::fromStdString(hex));
}

QString MainWindow::itemColor(const SelItem& it, bool* inherited) const {
  if (inherited) *inherited = false;
  const std::string n = it.name.toStdString();
  if (it.kind == SelItem::Kind::Body) {
    auto b = userBodyColor_.find(n);
    return b == userBodyColor_.end() ? QString() : QString::fromStdString(b->second);
  }
  if (it.kind == SelItem::Kind::Face) {
    auto f = userFaceColor_.find(n);
    if (f == userFaceColor_.end()) return QString();
    if (inherited) *inherited = !userFaceOwnColor_.count(n);
    return QString::fromStdString(f->second);
  }
  return QString();
}

Document MainWindow::appPayloadDocument() const {
  Document copy = doc_;
  copy.view = ViewState();   // Gizle / Göster is the designer's view state: the app always shows the whole part
  copy.view.colors = doc_.view.colors;   // face / body colours are carried into the app (the runner shows them)
  return copy;
}

// ------------------------------------------------------------------------------ command
bool MainWindow::setItemsColor(const QList<SelItem>& items, const QString& hexIn) {
  std::vector<model::ns::Ref> refs;
  for (const SelItem& s : items) {
    if (s.kind == SelItem::Kind::Face || s.kind == SelItem::Kind::Body) {
      const auto one = visRefsOf({s});
      refs.insert(refs.end(), one.begin(), one.end());
    }
  }
  if (refs.empty()) { statusBar()->showMessage("Renk: önce bir yüz ya da gövde seçin.", 4000); return false; }
  const std::string hex = hexIn.isEmpty() ? std::string() : model::normalizeColor(hexIn.toStdString());
  if (!hexIn.isEmpty() && hex.empty()) return false;
  ViewState v = doc_.view;
  std::vector<model::ns::Ref> rm = refs;
  if (hex.empty())   // reset: a body also loses the colours of its faces ("Rengi sıfırla" = default look)
    for (const auto& r : refs)
      if (r.kind == NsMemberKind::Body)
        if (const auto* bi = model_.body(r.name))
          for (const auto& f : bi->faces) rm.push_back(model::ns::face(f));
  for (auto& g : v.colors)
    if (!g.members.empty() && !visEdit(g, rm, false)) return false;
  if (!hex.empty()) {
    NamedSelection* grp = nullptr;
    for (auto& g : v.colors) if (g.color == hex) grp = &g;
    if (!grp) {
      NamedSelection g;
      g.name = "Renk " + hex;
      g.id = "view.color." + hex;
      g.color = hex;
      v.colors.push_back(std::move(g));
      grp = &v.colors.back();
    }
    if (!visEdit(*grp, refs, true)) return false;
  }
  v.colors.erase(std::remove_if(v.colors.begin(), v.colors.end(), [](const NamedSelection& g) { return g.members.empty(); }), v.colors.end());
  if (sameColors(v, doc_.view)) return false;
  int nf = 0, nb = 0;
  for (const auto& r : refs) (r.kind == NsMemberKind::Body ? nb : nf)++;
  const QString what = nb && nf ? QString("%1 yüz, %2 gövde").arg(nf).arg(nb)
                       : nb     ? (nb == 1 ? QString::fromStdString(refs.front().name) : QString("%1 gövde").arg(nb))
                                : QString("%1 yüz").arg(nf);
  const QString text = hex.empty() ? "Rengi sıfırla: " + what : QString("Renk %1: %2").arg(QString::fromStdString(hex), what);
  undo_.push(new ColorCommand(this, doc_.view, v, text));
  statusBar()->showMessage(text, 4000);
  if (!hex.empty()) { lastColor_ = QColor(QString::fromStdString(hex)); updateColorButton(); }
  return true;
}

bool MainWindow::colorSelection(const QColor& c) {
  if (!c.isValid()) return setItemsColor(visTargetsOfSelection(), QString());
  return setItemsColor(visTargetsOfSelection(), c.name());
}

bool MainWindow::resetSelectionColor() { return setItemsColor(visTargetsOfSelection(), QString()); }

// ------------------------------------------------------------------------------ UI
// Palette + "Diğer renkler…" + "Rengi sıfırla" in `m`; `targets` is evaluated when an entry is chosen.
void MainWindow::fillColorMenu(QMenu* m, std::function<QList<SelItem>()> targets) {
  auto* wa = new QWidgetAction(m);
  auto* box = new QWidget;
  box->setObjectName("colorPalette");
  auto* grid = new QGridLayout(box);
  grid->setContentsMargins(6, 6, 6, 6);
  grid->setSpacing(3);
  int i = 0;
  for (const char* c : kSwatches) {
    auto* b = new QToolButton(box);
    b->setObjectName(QString("colorSwatch.") + c);
    b->setFixedSize(22, 22);
    b->setToolTip(QString(c).toUpper());
    b->setStyleSheet(QString("QToolButton { background:%1; border:1px solid #6b7686; border-radius:3px; } "
                             "QToolButton:hover { border:2px solid #1f6feb; }").arg(c));
    const QString hex = c;
    connect(b, &QToolButton::clicked, this, [this, m, targets, hex] {
      m->close();
      for (QMenu* p = m; p; p = qobject_cast<QMenu*>(p->parentWidget())) p->close();
      setItemsColor(targets(), hex);
    });
    grid->addWidget(b, i / 8, i % 8);
    ++i;
  }
  wa->setDefaultWidget(box);
  m->addAction(wa);
  m->addSeparator();
  QAction* more = m->addAction(swatchIcon(lastColor_), "Diğer renkler…");
  more->setObjectName("colorMore");
  connect(more, &QAction::triggered, this, [this, targets] {
    QColorDialog dlg(lastColor_, this);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true);   // native dialogs are unavailable / unreliable headless
    dlg.setWindowTitle("Renk");
    dlg.setObjectName("colorDialog");
    if (dlg.exec() == QDialog::Accepted && dlg.currentColor().isValid()) setItemsColor(targets(), dlg.currentColor().name());
  });
  QAction* reset = m->addAction("Rengi sıfırla");
  reset->setObjectName("colorReset");
  connect(reset, &QAction::triggered, this, [this, targets] { setItemsColor(targets(), QString()); });
}

QMenu* MainWindow::makeColorMenu(QWidget* parent, std::function<QList<SelItem>()> targets) {
  auto* m = new QMenu(parent);
  m->setObjectName("colorMenu");
  fillColorMenu(m, std::move(targets));
  return m;
}

void MainWindow::updateColorButton() {
  if (actColor_) actColor_->setIcon(paletteIcon(lastColor_));
}

void MainWindow::createColorRibbonGroup() {
  lastColor_ = QColor("#ff0000");
  actColor_ = new QAction(paletteIcon(lastColor_), "Renk", this);
  actColor_->setObjectName("actColor");
  actColor_->setToolTip("Renk: seçili yüzlere / gövdelere renk ver (yüz -> yüz rengi, gövde -> gövde rengi)");
  actColor_->setMenu(makeColorMenu(this, [this] { return visTargetsOfSelection(); }));
  actColorReset_ = new QAction(makeIcon("delete", 16), "Rengi sıfırla", this);
  actColorReset_->setObjectName("actColorReset");
  actColorReset_->setToolTip("Seçili yüz / gövde rengini varsayılana döndür");
  connect(actColorReset_, &QAction::triggered, this, [this] { resetSelectionColor(); });
  auto* g = ribbon_->addGroup("display", "style", "Stil");
  g->addLarge(actColor_);
  g->addSmall(actColorReset_);
}

// "Renk ▸" in the viewport context menu (the target is the current selection, like Gizle)
void MainWindow::addColorMenu(QMenu* m, const QString& face) {
  Q_UNUSED(face);
  if (visTargetsOfSelection().isEmpty()) return;
  QMenu* cm = m->addMenu(swatchIcon(lastColor_), "Renk");
  cm->setObjectName("ctxColorMenu");
  cm->menuAction()->setObjectName("ctxColorMenu");
  fillColorMenu(cm, [this] { return visTargetsOfSelection(); });
}

} // namespace pcd
