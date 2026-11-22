// Shared robustness helpers for the GUI harnesses (header-only; each harness is a separate executable).
//
// Why: the harnesses passed on Linux/Xvfb but failed or hung on Windows (other fonts, DPI scaling, slower machine).
// The concrete failure was QTest::mouseClick(checkBox) at the *widget centre*: a QCheckBox in a spanning form row is
// stretched wider than its text, and only the indicator + text are clickable (SE_CheckBoxClickRect). With a narrower
// font (Segoe UI 9pt) the centre lies right of the text, the click is ignored, the named parameter is never created,
// and a later dialog whose expression references it ("W/2") refuses OK -> warning box -> dialog stays open forever.
//
// Rules implemented here:
//   * checkboxes are clicked on their indicator (style sub-element rect), then verified; programmatic fallback + [note];
//   * typed text is verified; programmatic fallback + [note] (only into enabled fields, so app wiring is still tested);
//   * completer popups are hidden before dialog buttons are clicked (a popup eats the click on Windows);
//   * every modal dialog has a close deadline; a modal still open after it is dumped, force-closed and reported FAIL;
//   * a stall watchdog (no check()/note() progress for PC_HARNESS_WATCHDOG_S s, default 240) dumps the modal state
//     and exits non-zero instead of hanging until the ctest timeout.
#pragma once
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTest>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

namespace pch {

inline std::atomic<qint64>& lastProgress() {
  static std::atomic<qint64> t{QDateTime::currentMSecsSinceEpoch()};
  return t;
}
// Call from check()/note(): the watchdog measures time since the last progress.
inline void kick() { lastProgress() = QDateTime::currentMSecsSinceEpoch(); }

inline std::function<void(bool, const QString&)>& checkFn() {
  static std::function<void(bool, const QString&)> f;
  return f;
}
inline void hnote(const QString& s) {
  std::printf("  [note] %s\n", qPrintable(s));
  std::fflush(stdout);
  kick();
}
inline void hfail(const QString& s) {
  if (checkFn()) checkFn()(false, s);
  else { std::printf("[FAIL] %s\n", qPrintable(s)); std::fflush(stdout); }
}

inline QString widgetLine(QWidget* w) {
  QString s = QString("%1").arg(w->metaObject()->className());
  if (!w->objectName().isEmpty()) s += " #" + w->objectName();
  if (w->isWindow() && !w->windowTitle().isEmpty()) s += " title='" + w->windowTitle() + "'";
  if (auto* b = qobject_cast<QAbstractButton*>(w)) {
    s += " text='" + b->text() + "'";
    if (b->isCheckable()) s += b->isChecked() ? " [x]" : " [ ]";
  } else if (auto* l = qobject_cast<QLineEdit*>(w)) s += " text='" + l->text() + "'";
  else if (auto* l = qobject_cast<QLabel*>(w)) s += " text='" + l->text().left(80) + "'";
  else if (auto* c = qobject_cast<QComboBox*>(w)) s += " current='" + c->currentText() + "'";
  if (auto* mb = qobject_cast<QMessageBox*>(w)) s += " msg='" + mb->text().left(160).replace('\n', ' ') + "'";
  if (!w->isEnabled()) s += " (disabled)";
  if (!w->isVisible()) s += " (hidden)";
  return s;
}
inline void dumpTree(QWidget* w, int depth = 0) {
  if (!w || depth > 12) return;
  std::printf("    %*s%s\n", depth * 2, "", qPrintable(widgetLine(w)));
  for (QObject* o : w->children())
    if (auto* c = qobject_cast<QWidget*>(o)) if (c->isVisible() || c->inherits("QAbstractButton")) dumpTree(c, depth + 1);
}
// Prints the active modal widget tree (and the other visible windows) — called before force-closing / exiting.
inline void dumpModalState(const QString& why) {
  std::printf("  [diag] %s\n", qPrintable(why));
  QWidget* m = QApplication::activeModalWidget();
  std::printf("  [diag] active modal widget: %s\n", m ? qPrintable(widgetLine(m)) : "<none>");
  if (m) dumpTree(m);
  for (QWidget* t : QApplication::topLevelWidgets())
    if (t->isVisible() && t != m) std::printf("  [diag] visible window: %s%s\n", qPrintable(widgetLine(t)), t->isModal() ? " (modal)" : "");
  std::fflush(stdout);
}

// ------------------------------------------------------------------ stall watchdog
inline void startWatchdog(std::function<int()> failCount) {
  int secs = qEnvironmentVariableIntValue("PC_HARNESS_WATCHDOG_S");
  if (secs <= 0) secs = 240;
  kick();
  std::thread([secs, failCount] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (QDateTime::currentMSecsSinceEpoch() - lastProgress().load() < qint64(secs) * 1000) continue;
      std::printf("[FAIL] watchdog: no progress for %d s — harness is stuck (exiting instead of hanging)\n", secs);
      std::fflush(stdout);
      static std::atomic<bool> dumped{false};
      QMetaObject::invokeMethod(qApp, [failCount] {   // runs in the GUI thread (nested modal loops deliver it too)
        dumpModalState("watchdog fired");
        dumped = true;
        std::printf("%d failure(s) + watchdog\n", failCount ? failCount() : 0);
        std::fflush(stdout);
        std::_Exit(100 + (failCount ? failCount() : 0));
      }, Qt::QueuedConnection);
      std::this_thread::sleep_for(std::chrono::seconds(10));
      if (!dumped) std::printf("  [diag] GUI thread did not respond within 10 s (busy outside the event loop)\n");
      std::fflush(stdout);
      std::_Exit(100 + (failCount ? failCount() : 0));
    }
  }).detach();
}

// ------------------------------------------------------------------ modal close deadlines
// Every modal seen by the pump gets a deadline (default 30 s, scaled by PC_HARNESS_SLOW). A handler that knows its
// dialog legitimately stays open longer (e.g. Save as App builds the executable) calls expectCloseWithin().
inline int slowFactor() {
  const int f = qEnvironmentVariableIntValue("PC_HARNESS_SLOW");
  return f > 0 ? f : 1;
}
inline void expectCloseWithin(QWidget* m, int ms) {
  if (m) m->setProperty("pchDeadline", QDateTime::currentMSecsSinceEpoch() + qint64(ms) * slowFactor());
}
inline void forceClose(QWidget* m) {
  if (auto* mb = qobject_cast<QMessageBox*>(m)) {
    QAbstractButton* b = mb->escapeButton();
    if (!b) b = mb->defaultButton();
    if (!b && !mb->buttons().isEmpty()) b = mb->buttons().first();
    if (b) { b->click(); return; }
  }
  if (auto* d = qobject_cast<QDialog*>(m)) { d->reject(); return; }
  m->close();
}
// Called from the pump timer on every tick with the current active modal; true if it was force-closed.
inline bool enforceDeadline(QWidget* m) {
  if (!m || !m->isVisible()) return false;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!m->property("pchDeadline").isValid()) expectCloseWithin(m, 30000);
  if (now < m->property("pchDeadline").toLongLong()) return false;
  if (m->property("pchForced").toBool()) return false;
  m->setProperty("pchForced", true);
  dumpModalState(QString("modal %1 '%2' still open after its deadline").arg(m->metaObject()->className(), m->windowTitle()));
  hfail(QString("modal dialog %1 '%2' did not close (force-closed by the harness)").arg(m->metaObject()->className(), m->windowTitle()));
  forceClose(m);
  return true;
}

// ------------------------------------------------------------------ input helpers
// Hides visible completer popups (never Esc: that would reject the dialog). A popup left open swallows the next
// mouse click on Windows.
inline void hideCompleterPopups() {
  for (QWidget* tw : QApplication::topLevelWidgets())
    if (tw->isVisible() && qobject_cast<QAbstractItemView*>(tw) && (tw->windowFlags() & Qt::Popup) == Qt::Popup) tw->hide();
}
// Real click on the check box INDICATOR (not the widget centre, which may be outside the clickable text when the
// box is stretched by its layout). Verified; falls back to setChecked() with a [note].
inline bool tickCheckBox(QCheckBox* cb, bool on, const QString& what) {
  if (!cb) return false;
  if (cb->isChecked() == on) return true;
  if (cb->isEnabled() && cb->isVisible()) {
    QStyleOptionButton opt;
    opt.initFrom(cb);
    QRect r = cb->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, cb);
    if (!r.isValid() || !cb->rect().contains(r.center())) r = QRect(0, 0, std::min(16, cb->width()), cb->height());
    QTest::mouseClick(cb, Qt::LeftButton, {}, r.center());
  }
  if (cb->isChecked() != on) {
    hnote(QString("%1: click on the check box indicator did not toggle it; set programmatically").arg(what));
    cb->setChecked(on);
  }
  return cb->isChecked() == on;
}
// Types `text` like a user (select all + key clicks), then verifies; if the platform dropped keystrokes, sets the
// text programmatically (only when the field is enabled — a disabled field is a real app problem and is reported).
inline bool typeInto(QLineEdit* le, const QString& text, const QString& what) {
  if (!le) return false;
  if (!le->isEnabled()) { hnote(QString("%1: field is disabled, cannot type '%2'").arg(what, text)); return false; }
  le->setFocus();
  le->selectAll();
  bool ascii = true;
  for (QChar c : text) ascii = ascii && c.unicode() < 128;
  if (ascii) QTest::keyClicks(le, text);
  if (le->text() != text) {
    if (ascii) hnote(QString("%1: typed '%2' but the field shows '%3'; set programmatically").arg(what, text, le->text()));
    le->setText(text);
  }
  hideCompleterPopups();
  return le->text() == text;
}
// Clicks a dialog button after hiding completer popups (deferred: the caller may be inside the modal's handler).
inline void pressLater(QAbstractButton* b, int ms = 0) {
  if (!b) return;
  QPointer<QAbstractButton> pb(b);
  QTimer::singleShot(ms, b, [pb] {
    if (!pb) return;
    hideCompleterPopups();
    QTest::mouseClick(pb, Qt::LeftButton);
  });
}

} // namespace pch
