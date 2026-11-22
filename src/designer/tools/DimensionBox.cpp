// SC-UX (WP-B) — on-the-fly dimension fields. See DimensionBox.h.
#include "DimensionBox.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QStringListModel>
#include <QToolButton>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace pcd {

QString formatDim(double v) {
  if (!std::isfinite(v)) return "?";
  double r = std::round(v * 1000.0) / 1000.0;
  if (std::fabs(r) < 5e-4) r = 0;
  QString s = QString::number(r, 'f', 3);
  while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
    const bool dot = s.endsWith('.');
    s.chop(1);
    if (dot) break;
  }
  return s;
}

namespace {
QPixmap lockPixmap(bool locked) {
  QPixmap pm(10, 12);
  pm.fill(Qt::transparent);
  if (!locked) return pm;
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(40, 90, 170), 1.3));
  p.setBrush(Qt::NoBrush);
  p.drawArc(QRectF(2, 0.8, 6, 7), 0, 180 * 16);
  p.drawLine(QPointF(2, 4.3), QPointF(2, 6));
  p.drawLine(QPointF(8, 4.3), QPointF(8, 6));
  p.setBrush(QColor(40, 90, 170));
  p.drawRoundedRect(QRectF(1, 5.5, 8, 6), 1.2, 1.2);
  return pm;
}
} // namespace

DimensionBox::DimensionBox(QWidget* overlay) : QObject(overlay), overlay_(overlay) {
  words_ = new QStringListModel(this);
}

DimensionBox::~DimensionBox() { clear(); }

void DimensionBox::clear() {
  for (auto& f : fields_)
    if (f.frame) f.frame->deleteLater();
  fields_.clear();
  editing_ = -1;
}

QString DimensionBox::fieldKey(int i) const { return i >= 0 && i < fieldCount() ? fields_[size_t(i)].spec.key : QString(); }
QWidget* DimensionBox::fieldWidget(int i) const { return i >= 0 && i < fieldCount() ? fields_[size_t(i)].frame : nullptr; }
QLineEdit* DimensionBox::lineEdit(int i) const { return i >= 0 && i < fieldCount() ? fields_[size_t(i)].edit : nullptr; }

void DimensionBox::setFields(const QList<FieldSpec>& specs) {
  const bool wasEditing = editing_ >= 0;
  clear();
  if (!overlay_) return;
  for (int i = 0; i < specs.size(); ++i) {
    Field f;
    f.spec = specs[i];
    f.frame = new QFrame(overlay_);
    f.frame->setObjectName(QString("dimField.%1").arg(specs[i].key));
    f.frame->setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(f.frame);
    lay->setContentsMargins(4, 1, 3, 1);
    lay->setSpacing(3);
    f.lock = new QLabel(f.frame);
    f.lock->setPixmap(lockPixmap(false));
    f.lock->setFixedSize(10, 12);
    lay->addWidget(f.lock);
    if (!specs[i].label.isEmpty()) {
      f.label = new QLabel(specs[i].label, f.frame);
      f.label->setStyleSheet("color:#4a5f80; font-size:8pt; background:transparent; border:none;");
      lay->addWidget(f.label);
    }
    f.edit = new QLineEdit(f.frame);
    f.edit->setObjectName(QString("dimEdit.%1").arg(specs[i].key));
    f.edit->setFrame(false);
    f.edit->setFixedWidth(62);
    f.edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    f.edit->setReadOnly(true);
    f.edit->setFocusPolicy(Qt::NoFocus);
    f.edit->installEventFilter(this);
    auto* comp = new QCompleter(words_, f.edit);
    comp->setCaseSensitivity(Qt::CaseInsensitive);
    comp->setCompletionMode(QCompleter::PopupCompletion);
    f.edit->setCompleter(comp);
    connect(f.edit, &QLineEdit::textEdited, this, [this, i] {
      if (i < fieldCount()) {
        fields_[size_t(i)].dirty = true;
        if (fields_[size_t(i)].locked) { fields_[size_t(i)].locked = false; }   // re-typing a locked field re-locks on Tab/Enter
      }
      restyle(i);
      emit textEdited(i);
    });
    lay->addWidget(f.edit);
    f.unit = new QLabel(specs[i].unit, f.frame);
    f.unit->setStyleSheet("color:#4a5f80; background:transparent; border:none;");
    lay->addWidget(f.unit);
    if (showGroup_ && i == specs.size() - 1) {
      f.group = new QToolButton(f.frame);
      f.group->setObjectName("dimGroup");
      f.group->setText("P");
      f.group->setToolTip("Grup oluştur (Create Group) — değeri parametre yap (Ctrl+G)");
      f.group->setFocusPolicy(Qt::NoFocus);
      f.group->setAutoRaise(true);
      f.group->setFixedSize(16, 16);
      f.group->setStyleSheet("QToolButton{font-weight:bold; color:#1f5fbf; border:1px solid #9db6dd; border-radius:2px; "
                             "background:#eef4ff; padding:0px;} QToolButton:hover{background:#d6e6ff;}");
      connect(f.group, &QToolButton::clicked, this, &DimensionBox::createGroupRequested);
      lay->addWidget(f.group);
    }
    f.frame->adjustSize();
    f.frame->setVisible(visible_);
    fields_.push_back(f);
    showLive(i);
    restyle(i);
  }
  if (wasEditing) emit editingChanged(false);
}

void DimensionBox::setShowGroupButton(bool on) { showGroup_ = on; }

void DimensionBox::setCompletions(const QStringList& words) { words_->setStringList(words); }

void DimensionBox::showLive(int i) {
  Field& f = fields_[size_t(i)];
  if (f.locked || (i == editing_ && f.dirty)) return;
  f.edit->setText(formatDim(f.live));
  if (i == editing_) f.edit->selectAll();   // still follows the cursor until the user types
}

bool DimensionBox::isTyped(int i) const { return i >= 0 && i < fieldCount() && fields_[size_t(i)].dirty; }

void DimensionBox::setLiveValue(int i, double v) {
  if (i < 0 || i >= fieldCount()) return;
  fields_[size_t(i)].live = v;
  showLive(i);
}
double DimensionBox::liveValue(int i) const { return i >= 0 && i < fieldCount() ? fields_[size_t(i)].live : 0.0; }

void DimensionBox::setAnchor(int i, const QPointF& px) {
  if (i < 0 || i >= fieldCount() || !overlay_) return;
  Field& f = fields_[size_t(i)];
  f.anchor = px;
  f.frame->adjustSize();
  QPoint tl = (px - QPointF(f.frame->width() / 2.0, f.frame->height() / 2.0)).toPoint();
  tl.setX(std::clamp(tl.x(), 2, std::max(2, overlay_->width() - f.frame->width() - 2)));
  tl.setY(std::clamp(tl.y(), 2, std::max(2, overlay_->height() - f.frame->height() - 2)));
  f.frame->move(tl);
}

void DimensionBox::setCompactAnchor(const QPoint& c) {
  int y = c.y() + 18;
  for (int i = 0; i < fieldCount(); ++i) {
    Field& f = fields_[size_t(i)];
    f.frame->adjustSize();
    setAnchor(i, QPointF(c.x() + 18 + f.frame->width() / 2.0, y + f.frame->height() / 2.0));
    y += f.frame->height() + 3;
  }
}

void DimensionBox::setVisible(bool on) {
  visible_ = on;
  for (auto& f : fields_) {
    f.frame->setVisible(on);
    if (on) f.frame->raise();
  }
  if (!on && editing_ >= 0) stopEdit();
}

bool DimensionBox::isLocked(int i) const { return i >= 0 && i < fieldCount() && fields_[size_t(i)].locked; }

void DimensionBox::setLocked(int i, bool on) {
  if (i < 0 || i >= fieldCount()) return;
  fields_[size_t(i)].locked = on;
  if (!on) showLive(i);
  restyle(i);
  emit lockedChanged(i);
}

QString DimensionBox::text(int i) const {
  if (i < 0 || i >= fieldCount()) return QString();
  return fields_[size_t(i)].edit->text().trimmed();
}

void DimensionBox::setText(int i, const QString& t, bool lock) {
  if (i < 0 || i >= fieldCount()) return;
  fields_[size_t(i)].edit->setText(t);
  if (lock) fields_[size_t(i)].locked = true;
  restyle(i);
  if (lock) emit lockedChanged(i);
}

bool DimensionBox::value(int i, double* v) const {
  const QString t = text(i);
  if (t.isEmpty()) return false;
  if (reasonValidator_) return reasonValidator_(t, v, nullptr);
  if (validator_) return validator_(t, v);
  bool ok = false;
  const double d = t.toDouble(&ok);
  if (ok && v) *v = d;
  return ok;
}

QString DimensionBox::reason(int i) const {
  const QString t = text(i);
  if (t.isEmpty()) return QString();
  QString why;
  double v = 0;
  if (reasonValidator_) {
    if (reasonValidator_(t, &v, &why)) return QString();
    return why.isEmpty() ? QString("‘%1’ bir sayı, parametre adı ya da ifade değil.").arg(t) : why;
  }
  if (value(i, &v)) return QString();
  return QString("‘%1’ bir sayı, parametre adı ya da ifade değil.").arg(t);
}

void DimensionBox::setFieldTip(const QString& tip) {
  fieldTip_ = tip;
  for (int i = 0; i < fieldCount(); ++i) restyle(i);
}

bool DimensionBox::allValid() const {
  for (int i = 0; i < fieldCount(); ++i)
    if ((fields_[size_t(i)].locked || i == editing_) && !value(i, nullptr)) return false;
  return true;
}

void DimensionBox::restyle(int i) {
  if (i < 0 || i >= fieldCount()) return;
  Field& f = fields_[size_t(i)];
  const bool ed = i == editing_;
  const bool bad = (ed || f.locked) && !f.edit->text().trimmed().isEmpty() && !value(i, nullptr);
  QString border = bad ? "2px solid #d23c32" : ed ? "2px solid #2a7de1" : f.locked ? "1px solid #3b6fc0" : "1px solid #8ea3c4";
  QString bg = bad ? "#fff0ee" : ed ? "#ffffff" : f.locked ? "#eef4ff" : "rgba(255,255,255,235)";
  f.frame->setStyleSheet(QString("QFrame#%1{background:%2; border:%3; border-radius:3px;}")
                             .arg(f.frame->objectName().replace('.', "\\."), bg, border));
  f.edit->setStyleSheet(QString("QLineEdit{background:transparent; border:none; font-weight:%1; color:%2;}")
                            .arg(ed || f.locked ? "bold" : "normal", bad ? "#c0302a" : ed ? "#10305f" : "#1d3554"));
  f.lock->setPixmap(lockPixmap(f.locked));
  // SC23 (O-P0-3): the reason of a refused text is the field's tooltip (shown on hover, read by tests)
  const QString tip = bad ? reason(i) : fieldTip_;
  f.frame->setToolTip(tip);
  f.edit->setToolTip(tip);
  f.frame->adjustSize();
  if (!f.anchor.isNull()) setAnchor(i, f.anchor);
}

void DimensionBox::beginEdit(int i, const QString& initial) {
  if (i < 0 || i >= fieldCount()) return;
  const int prev = editing_;
  if (prev >= 0 && prev != i) {
    Field& p = fields_[size_t(prev)];
    p.edit->setReadOnly(true);
    p.edit->setFocusPolicy(Qt::NoFocus);
    restyle(prev);
  }
  editing_ = i;
  if (!visible_) setVisible(true);
  Field& f = fields_[size_t(i)];
  f.edit->setReadOnly(false);
  f.edit->setFocusPolicy(Qt::StrongFocus);
  if (!initial.isEmpty()) { f.edit->setText(initial); f.dirty = true; }
  f.frame->raise();
  f.edit->setFocus(Qt::OtherFocusReason);
  if (initial.isEmpty()) f.edit->selectAll();
  else f.edit->end(false);
  restyle(i);
  if (prev < 0) emit editingChanged(true);
}

void DimensionBox::stopEdit() {
  const int prev = editing_;
  editing_ = -1;
  for (int i = 0; i < fieldCount(); ++i) {
    Field& f = fields_[size_t(i)];
    f.locked = false;
    f.dirty = false;
    f.edit->setReadOnly(true);
    f.edit->setFocusPolicy(Qt::NoFocus);
    showLive(i);
    restyle(i);
  }
  if (overlay_) overlay_->setFocus(Qt::OtherFocusReason);
  if (prev >= 0) emit editingChanged(false);
}

void DimensionBox::lockCurrent() {
  if (editing_ < 0) return;
  Field& f = fields_[size_t(editing_)];
  const QString t = f.edit->text().trimmed();
  if (f.dirty && !t.isEmpty() && value(editing_, nullptr) && !f.locked) {
    f.locked = true;
    emit lockedChanged(editing_);
  }
  f.dirty = false;
  restyle(editing_);
}

void DimensionBox::focusNext(int step) {
  if (fieldCount() == 0) return;
  const int cur = editing_ < 0 ? 0 : editing_;
  const int n = fieldCount();
  beginEdit(((cur + step) % n + n) % n, QString());
}

int DimensionBox::indexOf(QObject* o) const {
  for (int i = 0; i < fieldCount(); ++i) if (fields_[size_t(i)].edit == o) return i;
  return -1;
}

void DimensionBox::nudge(int field, int sign, Qt::KeyboardModifiers m) {
  if (field < 0 || field >= fieldCount()) return;
  double step = step_;
  if (m & Qt::AltModifier) step /= 10.0;
  if (m & Qt::ControlModifier) step *= 10.0;
  double v = 0;
  if (!value(field, &v)) v = fields_[size_t(field)].live;
  if (editing_ != field) beginEdit(field, QString());
  fields_[size_t(field)].edit->setText(formatDim(v + sign * step));
  fields_[size_t(field)].locked = true;
  restyle(field);
  emit lockedChanged(field);
  emit textEdited(field);
}

bool DimensionBox::eventFilter(QObject* o, QEvent* e) {
  const int i = indexOf(o);
  if (i < 0) return QObject::eventFilter(o, e);
  if (e->type() == QEvent::ShortcutOverride || e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    const int key = k->key();
    const Qt::KeyboardModifiers m = k->modifiers();
    const bool shiftArrow = (m & Qt::ShiftModifier) && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right);
    const bool ours = key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Return || key == Qt::Key_Enter ||
                      key == Qt::Key_Escape || shiftArrow || (key == Qt::Key_G && (m & Qt::ControlModifier));
    if (e->type() == QEvent::ShortcutOverride) {
      if (ours) { e->accept(); return true; }
      if (k->matches(QKeySequence::Undo) || k->matches(QKeySequence::Redo)) {
        // SC23 (ANALYSIS §4.4): Ctrl+Z during a preview cancels the preview first; the event stays NOT accepted so
        // the Geri Al / Yinele shortcut runs afterwards (the line edit's own text undo is bypassed)
        emit cancelled();
        return true;
      }
      return false;
    }
    // completer popup open: let it handle Enter / Esc
    if (auto* le = qobject_cast<QLineEdit*>(o); le && le->completer() && le->completer()->popup() &&
        le->completer()->popup()->isVisible() && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape))
      return false;
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
      lockCurrent();
      focusNext(key == Qt::Key_Backtab || (m & Qt::ShiftModifier) ? -1 : 1);
      return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      lockCurrent();
      if (!allValid()) {
        restyle(i);
        int bad = i;
        for (int k = 0; k < fieldCount(); ++k)
          if ((fields_[size_t(k)].locked || k == editing_) && !value(k, nullptr)) { bad = k; break; }
        QString why = reason(bad);
        if (why.isEmpty()) why = QString("Değer girin.");
        emit invalidCommit(bad, why);
        return true;
      }
      emit committed();
      return true;
    }
    if (key == Qt::Key_Escape) {
      emit cancelled();
      return true;
    }
    if (key == Qt::Key_G && (m & Qt::ControlModifier)) {
      emit createGroupRequested();
      return true;
    }
    if (shiftArrow) {
      const int fld = (key == Qt::Key_Up || key == Qt::Key_Down) ? 0 : 1;
      nudge(fld, (key == Qt::Key_Up || key == Qt::Key_Right) ? 1 : -1, m);
      return true;
    }
  }
  return QObject::eventFilter(o, e);
}

} // namespace pcd
