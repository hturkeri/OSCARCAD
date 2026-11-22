// SC23 viewport HUD (ANALYSIS §5.1 S-P0-2; API frozen in ViewportHud.h, this file is WP-S). See ViewportHud.h.
//
// Look (reference docs/sc2023/spaceclaim-2023r1-pull.jpg, "Pull 1 face" top-left [G]):
//   line 1   "<tool> · <selection>"  dark grey text on a light translucent pill (bold tool name)
//   line 2   message strip: severity icon + text, never colour alone (PROMPT §11); Info = dark blue-grey,
//            Warning = amber, Error = red. Word-wrapped up to 520 px. A click on the strip dismisses it.
//   below    guideArea(): a FIXED slot (tool line + one message line), so the tool guides do not jump
// The HUD body is transparent for the mouse; the strip is a sibling that takes the mouse (see the header).
#include "ViewportHud.h"
#include "Icons.h"
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <algorithm>

namespace pcd {

namespace {
constexpr int kMargin = 8;     // distance from the viewer's top-left corner
constexpr int kGap = 4;        // between the tool line, the message strip and the guide area
constexpr int kMaxMsgW = 520;  // message strip wraps beyond this width
constexpr int kIconW = 16;
const char* kTitleStyle =
    "QLabel { background: rgba(255,255,255,190); color: #2b3a4f; border: 1px solid rgba(70,90,120,70);"
    " border-radius: 3px; padding: 2px 8px; }";
QString messageStyle(ViewportHud::Severity s) {
  // left padding leaves room for the severity icon (child label "viewportHud.messageIcon")
  const QString pad = QString("padding: 3px 8px 3px %1px;").arg(kIconW + 10);
  switch (s) {
    case ViewportHud::Severity::Error:
      return "QLabel#viewportHud\\.message { background: rgba(196,38,38,230); color: white; border: 1px solid #8f1414;"
             " border-radius: 3px; " + pad + " }";
    case ViewportHud::Severity::Warning:
      return "QLabel#viewportHud\\.message { background: rgba(250,205,70,235); color: #2a2000; border: 1px solid #b88a00;"
             " border-radius: 3px; " + pad + " }";
    case ViewportHud::Severity::Info: break;
  }
  return "QLabel#viewportHud\\.message { background: rgba(38,58,88,215); color: white; border: 1px solid rgba(20,30,50,160);"
         " border-radius: 3px; " + pad + " }";
}
QString iconKey(ViewportHud::Severity s) {
  switch (s) {
    case ViewportHud::Severity::Error: return "hud.error";
    case ViewportHud::Severity::Warning: return "warn";
    case ViewportHud::Severity::Info: break;
  }
  return "info";
}
QString escaped(const QString& s) { return s.toHtmlEscaped(); }
// top of the HUD. The sketch overlay's "Sketch: … DOF" badge is painted in the bottom-right corner (SketchEditor), so
// the HUD keeps the same top-left slot in 3D and in sketch mode.
int topY(const QWidget*) { return kMargin; }
} // namespace

ViewportHud::ViewportHud(QWidget* viewer) : QWidget(viewer), viewer_(viewer) {
  setObjectName("viewportHud");
  setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks reach the viewer
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::NoFocus);
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  title_ = new QLabel(this);
  title_->setObjectName("viewportHud.tool");
  title_->setStyleSheet(kTitleStyle);
  title_->setTextFormat(Qt::RichText);
  QFont f = title_->font();
  f.setPointSizeF(f.pointSizeF() * 1.08);
  title_->setFont(f);
  lay->addWidget(title_);

  // sibling of the HUD (see header): not mouse-transparent, so a click on the message does not pick through it
  message_ = new QLabel(viewer);
  message_->setObjectName("viewportHud.message");
  message_->setTextFormat(Qt::PlainText);
  message_->setWordWrap(true);
  message_->setFocusPolicy(Qt::NoFocus);
  message_->setCursor(Qt::PointingHandCursor);
  message_->setToolTip("Kapatmak için tıklayın");
  message_->setStyleSheet(messageStyle(Severity::Info));
  message_->installEventFilter(this);
  auto* icon = new QLabel(message_);
  icon->setObjectName("viewportHud.messageIcon");
  icon->setAttribute(Qt::WA_TransparentForMouseEvents);
  icon->setFixedSize(kIconW, kIconW);
  icon->setStyleSheet("background: transparent; border: none; padding: 0px;");
  message_->hide();

  timer_ = new QTimer(this);
  timer_->setSingleShot(true);
  connect(timer_, &QTimer::timeout, this, [this] { clearMessage(); });

  if (viewer) {
    viewer->installEventFilter(this);
  }
  hide();
  relayout();
}

ViewportHud::~ViewportHud() {
  if (viewer_) viewer_->removeEventFilter(this);
  delete message_.data();
}

void ViewportHud::setToolText(const QString& text) {
  if (tool_ == text) return;
  tool_ = text;
  relayout();
}

void ViewportHud::setSelectionText(const QString& text) {
  if (sel_ == text) return;
  sel_ = text;
  relayout();
}

void ViewportHud::showMessage(const QString& text, Severity severity, int ms) {
  timer_->stop();
  msg_ = text;
  sev_ = severity;
  if (message_) {
    message_->setStyleSheet(messageStyle(severity));
    if (auto* icon = message_->findChild<QLabel*>("viewportHud.messageIcon"))
      icon->setPixmap(makeIcon(iconKey(severity), kIconW).pixmap(kIconW, kIconW));
    message_->setAccessibleName(QString("%1: %2").arg(severity == Severity::Error ? "Hata" : severity == Severity::Warning ? "Uyarı" : "Bilgi", text));
  }
  relayout();
  if (ms > 0 && !text.isEmpty()) timer_->start(ms);
}

void ViewportHud::clearMessage() {
  timer_->stop();
  if (msg_.isEmpty()) return;
  msg_.clear();
  sev_ = Severity::Info;
  relayout();
}

QRect ViewportHud::guideArea() const {
  // fixed slot under the tool line and ONE message line, so the guides do not jump when a message appears
  const QFontMetrics fm(title_->font());
  const int line = std::max(title_->sizeHint().height(), fm.height() + 8);
  const int msgLine = QFontMetrics(font()).height() + 10;
  const int top = topY(viewer_) + line + kGap + msgLine + kGap;
  const int vh = viewer_ ? viewer_->height() : 0;
  const int vw = viewer_ ? viewer_->width() : 0;
  return QRect(kMargin, top, std::max(0, std::min(240, vw - 2 * kMargin)), std::max(0, vh - top - kMargin));
}

bool ViewportHud::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) relayout();
  if (o == message_ && e->type() == QEvent::MouseButtonPress) {
    if (static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) clearMessage();
    return true;   // never picks through the strip
  }
  if (o == message_ && (e->type() == QEvent::MouseButtonRelease || e->type() == QEvent::MouseButtonDblClick)) return true;
  return QWidget::eventFilter(o, e);
}

void ViewportHud::relayout() {
  QString rich;
  if (!tool_.isEmpty()) rich = "<b>" + escaped(tool_) + "</b>";
  if (!sel_.isEmpty()) rich = rich.isEmpty() ? escaped(sel_) : rich + QStringLiteral(" &middot; ") + escaped(sel_);
  title_->setText(rich);
  const int maxW = viewer_ ? std::max(40, viewer_->width() - 2 * kMargin) : 400;
  if (rich.isEmpty()) {
    hide();
  } else {
    const QSize sh = title_->sizeHint();
    setGeometry(kMargin, topY(viewer_), std::min(sh.width(), maxW), sh.height());
    show();
    raise();
  }
  if (message_) {
    message_->setText(msg_);
    if (msg_.isEmpty()) {
      message_->hide();
    } else {
      const int line = std::max(title_->sizeHint().height(), QFontMetrics(title_->font()).height() + 8);
      const int y = topY(viewer_) + line + kGap;
      const int w = std::min({message_->sizeHint().width(), maxW, kMaxMsgW});
      const int h = message_->heightForWidth(w) > 0 ? message_->heightForWidth(w) : message_->sizeHint().height();
      message_->setGeometry(kMargin, y, w, h);
      if (auto* icon = message_->findChild<QLabel*>("viewportHud.messageIcon")) icon->move(6, std::max(2, (std::min(h, 24) - kIconW) / 2));
      message_->show();
      message_->raise();
    }
  }
  emit layoutChanged();
}

} // namespace pcd
