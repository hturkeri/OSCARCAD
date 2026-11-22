#pragma once
// "Seçimden parametre" — the select -> parameter workflow (SpaceClaim feel), sketch and 3D mode:
//   * badge   (objectName "paramBadge"): floating next to the selection with the measured value(s), a "P" button
//              per value ("paramBadge.P" for the first, "paramBadge.P.<key>" for the others), "Ø⇄R" toggle
//              ("paramBadge.toggle") for circles / arcs and a bind button ("paramBadge.bind") = existing parameter;
//   * popup   (objectName "quickParamPopup"): compact inline form — Ölçü, Bağla (new / existing parameter), Ad,
//              Değer + unit, Min/Maks (suggested ±50 %), Uygulamada göster; Enter creates, Esc closes;
//   * Properties rows "Seçimden parametre" with "Parametre yap" buttons (PropertiesPanel::Hooks::measureRows);
//   * context menus: "Parametre olarak ekle ▸ Uzunluk / Çap / Yarıçap / …", "Parametreye bağla ▸ …";
//   * Ctrl+G (Grup oluştur) with a measurable selection opens the popup for the first value.
// Creating = ONE undo step: in sketch mode a sketch-local step that carries the parameter (undo removes both), in
// 3D one document commit (parameter + bound feature field / sketch dimension).
#include "ParamSelect.h"
#include "PropertiesPanel.h"
#include "ToolApi.h"
#include <QFrame>
#include <QPointer>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;
class QUndoStack;

namespace pcd {

class QuickParamPopup : public QFrame {
  Q_OBJECT
public:
  using Evaluator = std::function<bool(const QString& expr, double* v, QString* err)>;
  explicit QuickParamPopup(QWidget* parent);
  // `ctx`: document whose parameters are visible (names / bind list); `bindTo`: preselect an existing parameter.
  void setup(const QList<psel::Measure>& measures, int index, const pc::Document& ctx, const QString& bindTo, Evaluator eval);
  int measureIndex() const;
  QString bindParam() const;            // "" = new parameter
  pc::Parameter parameter() const;      // the new parameter (name, expression, kind, min/max, exposed)
  QLineEdit* nameEdit() const { return name_; }
  QLineEdit* valueEdit() const { return value_; }
  void showError(const QString& text);
signals:
  void accepted();
  void cancelled();
protected:
  void keyPressEvent(QKeyEvent* e) override;
private:
  void onMeasure(int i);
  void onTarget(int i);
  void tryAccept();
  QList<psel::Measure> ms_;
  const pc::Document* ctx_ = nullptr;
  Evaluator eval_;
  QLabel* title_ = nullptr;
  QComboBox *measure_ = nullptr, *target_ = nullptr;
  QLineEdit *name_ = nullptr, *value_ = nullptr, *min_ = nullptr, *max_ = nullptr;
  QLabel *unit_ = nullptr, *err_ = nullptr, *live_ = nullptr;
  QCheckBox *limits_ = nullptr, *exposed_ = nullptr;
  QPushButton *ok_ = nullptr, *cancel_ = nullptr;
  QStringList taken_;
};

class ParamSelectController : public QObject {
  Q_OBJECT
public:
  struct Hooks {
    std::function<bool(const pc::Document& cand, const QString& text)> commitNoValidate;   // parameter add / remove
    std::function<void(const QString& name)> selectParam;                                   // Gruplar row
    std::function<QUndoStack*()> undoStack;
    std::function<QString()> activeToolId;
  };
  ParamSelectController(IDesignHost* host, Hooks hooks, QObject* parent);

  const QList<psel::Measure>& measures() const { return ms_; }
  int primaryIndex() const;                      // index of the value Ctrl+G / the first P button uses
  bool triggerPrimary();                         // Ctrl+G: opens the popup; false = nothing measurable selected
  bool openPopup(int index, const QString& bindTo = QString());
  // Core: parameter `p` (new) or existing parameter `bindTo` bound to measure `index`. Asks about conflicts.
  bool apply(int index, const pc::Parameter& p, const QString& bindTo = QString());
  void populateContextMenu(QMenu* m);            // 3D viewer / edge menus
  void showSketchContextMenu(const QPoint& globalPos);
  QList<PropertiesPanel::Hooks::MeasureRow> propertyRows();
  QFrame* badge() const { return badge_; }
  QuickParamPopup* popup() const { return popup_; }
  QMenu* lastMenu() const { return lastMenu_; }
  void closePopup(bool refreshAfter = true);

public slots:
  void refresh();                                // selection / document / mode / tool changed
  void reposition();                             // camera / resize

signals:
  void measuresChanged();

private:
  bool sketchMode() const;
  bool active() const;                           // the badge may show (Seç tool, no dialog)
  QString signature() const;
  void rebuildBadge();
  bool anchor(QPointF* p) const;
  QStringList displayKeys() const;
  void onSketchParamsUndone(const QStringList& names);
  void onSketchParamsRedone(const std::vector<pc::Parameter>& params);
  int askConflict(const psel::BindResult& r, bool failed);   // 0 cancel, 1 replace, 2 reference
  bool evaluate(const QString& expr, double* v, QString* err) const;

  IDesignHost* host_;
  Hooks hooks_;
  QList<psel::Measure> ms_;
  QString sig_;
  bool showRadius_ = false;     // circles: badge shows R instead of Ø
  QPointer<QFrame> badge_;
  QPointer<QuickParamPopup> popup_;
  QPointer<QMenu> lastMenu_;
  int popupIndex_ = -1;
  bool applying_ = false;
};

} // namespace pcd
