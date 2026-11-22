#pragma once
// SC-UX (WP-B) — on-the-fly dimension fields (SPEC §2.4, §5).
//
// A DimensionBox manages 1..3 small framed fields ("60 mm") that float over an overlay widget (sketch overlay or
// viewer). While a tool is live the fields show the cursor-driven values (read-only look). Typing a digit / '.' /
// '-' / '(' (or Space) starts editing a field: the field takes the keyboard focus, Tab locks it and moves to the
// next one (Shift+Tab back), Enter commits (committed()), Esc stops editing (cancelled()). A typed (locked) field
// keeps its text while the cursor moves; unlocked fields follow setLiveValue(). Every field accepts expressions;
// a validator (set by the owner) colours invalid text red and blocks Enter. Ctrl+G emits createGroupRequested().
// Shift+Up/Down nudges the primary field, Shift+Left/Right the secondary one (Alt+Shift small, Ctrl+Shift big step).
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QFrame;
class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;
class QCompleter;
class QStringListModel;

namespace pcd {

class DimensionBox : public QObject {
  Q_OBJECT
public:
  explicit DimensionBox(QWidget* overlay);
  ~DimensionBox() override;

  struct FieldSpec { QString key; QString label; QString unit; };
  void setFields(const QList<FieldSpec>& fields);     // resets locks / texts
  int fieldCount() const { return int(fields_.size()); }
  QString fieldKey(int i) const;

  void setLiveValue(int i, double v);                  // ignored for locked / edited fields
  double liveValue(int i) const;
  void setAnchor(int i, const QPointF& overlayPx);     // field i centred at this overlay pixel
  void setCompactAnchor(const QPoint& cursorPx);       // all fields stacked at cursor + 18 px
  void setVisible(bool on);
  bool isVisible() const { return visible_; }

  bool isEditing() const { return editing_ >= 0; }
  int editingField() const { return editing_; }
  void beginEdit(int i, const QString& initialText);   // initialText replaces the field text ("" = select all)
  void stopEdit();                                     // back to live display, locks cleared
  bool isLocked(int i) const;
  bool isTyped(int i) const;                           // typed into during the current edit (not yet locked)
  void setLocked(int i, bool on);
  QString text(int i) const;                           // locked/edited text, else the live value
  void setText(int i, const QString& t, bool lock);
  bool value(int i, double* v) const;                  // evaluated through the validator
  bool allValid() const;
  void lockCurrent();                                  // lock the field being edited (if its text is valid)
  void focusNext(int step = 1);

  using Validator = std::function<bool(const QString& text, double* value)>;
  void setValidator(Validator v) { validator_ = std::move(v); reasonValidator_ = nullptr; }
  // SC23 (O-P0-3): a validator that also says WHY a text is refused (tools/ValueInput). Replaces setValidator().
  using ReasonValidator = std::function<bool(const QString& text, double* value, QString* reason)>;
  void setReasonValidator(ReasonValidator v) { reasonValidator_ = std::move(v); validator_ = nullptr; }
  // SC23: why field i is refused ("" = valid or empty). Shown as the field's tooltip while the text is invalid.
  QString reason(int i) const;
  // SC23 (O-P1-3): tooltip of the fields while their text is valid (unit help, e.g. "Belge birimi: mm ...").
  void setFieldTip(const QString& tip);
  void setCompletions(const QStringList& words);
  void setStep(double normal) { step_ = normal; }
  void setShowGroupButton(bool on);
  QWidget* fieldWidget(int i) const;
  QLineEdit* lineEdit(int i) const;

signals:
  void committed();
  void cancelled();
  void editingChanged(bool editing);
  void lockedChanged(int field);
  void textEdited(int field);
  void createGroupRequested();
  // SC23: Enter on an invalid text (the box stays open; the reason is in the tooltip and here).
  void invalidCommit(int field, const QString& reason);

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  struct Field {
    FieldSpec spec;
    QFrame* frame = nullptr;
    QLabel* lock = nullptr;
    QLabel* label = nullptr;
    QLineEdit* edit = nullptr;
    QLabel* unit = nullptr;
    QToolButton* group = nullptr;
    bool locked = false;
    bool dirty = false;      // typed in during the current edit
    double live = 0;
    QPointF anchor;
  };
  void clear();
  void restyle(int i);
  void showLive(int i);
  void nudge(int field, int sign, Qt::KeyboardModifiers m);
  int indexOf(QObject* o) const;
  QPointer<QWidget> overlay_;
  std::vector<Field> fields_;
  bool visible_ = false;
  int editing_ = -1;
  bool showGroup_ = true;
  double step_ = 1.0;
  Validator validator_;
  ReasonValidator reasonValidator_;
  QString fieldTip_;
  QCompleter* completer_ = nullptr;
  QStringListModel* words_ = nullptr;
};

QString formatDim(double v);   // "60", "12.5", "0.333"

} // namespace pcd
