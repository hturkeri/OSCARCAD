#pragma once
// Numeric-expression field used by every feature / dimension dialog:
//   [ expression line edit (with parameter-name completer) ] [ = live value ] [ parameter picker ▾ ] [ Yeni… ]
// The picker lists the document parameters as "name = value unit"; choosing one binds the field to it
// (the expression becomes the parameter name), "— değer —" unbinds it (the current value is written as a number)
// and "Yeni parametre…" opens NewParameterDialog. Works on a *working* Document (parameters may be added to it).
//
// New parameters are added to the working document AND handed to the parameter-commit hook installed by the
// owning window (MainWindow commits them to the real document at once, so they show up in the parameter table
// immediately and survive closing the sketch / cancelling the dialog).
#include "pc/Document.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QStringListModel>
#include <QToolButton>
#include <QWidget>
#include <functional>

namespace pcd {

QString paramUnitText(pc::ParamKind k);      // "mm", "°", "adet", "-"
pc::ParamKind paramKindFromText(const QString& t);

// ---- parameter-commit hook (per top-level window) -----------------------------------------------
using ParameterCommitFn = std::function<void(const pc::Parameter&)>;
void installParameterCommitHook(QWidget* scope, ParameterCommitFn fn);
void removeParameterCommitHook(QWidget* scope);
// Called by the widgets below after they created `p` in their working document. Walks the parent chain of
// `from` to the first widget with an installed hook.
void commitNewParameter(QWidget* from, const pc::Parameter& p);

class ExprEdit : public QWidget {
  Q_OBJECT
public:
  explicit ExprEdit(pc::Document* work, QWidget* parent = nullptr);
  QString text() const { return edit_->text().trimmed(); }
  void setText(const QString& t);
  bool isValid() const { return valid_; }
  double value() const { return value_; }
  void setAllowNewParam(bool on);             // shows/hides the picker's "Yeni parametre…" and the Yeni button
  void setPickerVisible(bool on);             // parameter picker + Yeni button (off for plain value fields)
  void setPlaceholder(const QString& s) { edit_->setPlaceholderText(s); }
  // Suggested name / unit kind for parameters created from this field (e.g. "Extrude1_distance", Length).
  void setParamHint(const QString& suggestedName, pc::ParamKind kind) { hintName_ = suggestedName; hintKind_ = kind; }
  void refreshParams();   // call when the working document's parameters changed
  QLineEdit* lineEdit() const { return edit_; }
  QComboBox* picker() const { return pick_; }
  QString boundParameter() const;             // parameter name if the expression is exactly one parameter

signals:
  void textChanged();
  void parameterCreated(const QString& name);

public slots:
  void createParameter();                     // opens NewParameterDialog; binds the field on success

private slots:
  void onTextEdited(const QString& t);
  void onActivated(const QString& word);

private:
  void evaluate();
  void syncPicker();
  void onPicked(int index);
  pc::Document* work_;
  QLineEdit* edit_;
  QLabel* preview_;
  QComboBox* pick_;
  QToolButton* newBtn_;
  QCompleter* completer_;
  QStringListModel* model_;
  bool valid_ = false;
  bool allowNew_ = true;
  bool syncing_ = false;
  double value_ = 0;
  QString hintName_;
  pc::ParamKind hintKind_ = pc::ParamKind::Length;
};

// "Yeni parametre" — name, value (expression), min, max, unit kind, "Uygulamada göster", label, group, description.
class NewParameterDialog : public QDialog {
  Q_OBJECT
public:
  NewParameterDialog(pc::Document* work, const QString& suggestedName, pc::ParamKind kind, const QString& valueExpr,
                     QWidget* parent = nullptr);
  pc::Parameter parameter() const;           // valid after accept()
  void accept() override;
private:
  pc::Document* work_;
  QLineEdit *name_, *min_, *max_, *label_, *group_, *desc_;
  ExprEdit* value_;
  QComboBox* kind_;
  QCheckBox* exposed_;
};

// Modal dialog asking for a dimension expression: free expression, pick an existing parameter, or create a
// named parameter (inline "Yeni adlandırılmış parametre" + name + "Uygulamada göster", or the full dialog).
class DimensionDialog : public QDialog {
  Q_OBJECT
public:
  DimensionDialog(pc::Document* work, const QString& title, const QString& expr, bool isAngle, QWidget* parent = nullptr);
  QString expression() const;   // final expression (parameter name if a new parameter was created)
private:
  ExprEdit* edit_;
  QCheckBox* newParam_;
  QLineEdit* name_;
  QCheckBox* exposed_;
  pc::Document* work_;
  bool angle_;
  void accept() override;
};

} // namespace pcd
