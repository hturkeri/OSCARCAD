#pragma once
// SC-UX shell (WP-A): Properties panel (SPEC §9). QTreeWidget with two columns (Özellik | Değer); bold
// category nodes; editable values are item widgets (ExprEdit with the parameter picker, QLineEdit, QComboBox,
// QCheckBox) whose edits go through IDesignHost::commitDocument (one undo step, mfg validation).
//
// Phase 1 content: body, face, edge, datum plane, sketch, sketch region, feature (History tab), parameter
// (Groups tab), nothing selected (document summary). Several items: their count per kind.
// Editor objectNames (test hooks): "prop.<key>", e.g. "prop.name", "prop.visible", "prop.radius".
#include "ToolApi.h"
#include <QHash>
#include <QWidget>
#include <functional>

class QMenu;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pcd {

class PropertiesPanel : public QWidget {
  Q_OBJECT
public:
  // Shell services the generic IDesignHost does not offer (view state lives in MainWindow).
  struct Hooks {
    std::function<bool(const QString& from, const QString& to)> renameBody;
    std::function<void(const QString& body, bool visible)> setBodyVisible;
    std::function<bool(const QString& body)> bodyVisible;
    std::function<void(const std::string& featureId)> showInHistory;
    std::function<QString(const QString& featureId)> parameterSummary;   // MainWindow::featureParameterSummary
    std::function<bool(const std::string& sketchId)> sketchShown;
    std::function<void(const std::string& sketchId, bool on)> setSketchShown;
    std::function<void()> openMfgSettings;
    std::function<QString()> filePath;
    // "Seçimden parametre": measured values of the current (3D or sketch) selection, one row each with a
    // "Parametre yap" button (objectName "prop.param.<key>").
    struct MeasureRow {
      QString key, label, value, button, tip;
      bool enabled = true;
      std::function<void()> fn;
    };
    std::function<QList<MeasureRow>()> measureRows;
    // Named selections (Adlandırılmış seçimler): names of the selections that contain ALL `items`, and the "Ad"
    // row commit (create / add to / rename a named selection; "" removes the items from their selections).
    std::function<QStringList(const QList<SelItem>& items)> namedSelectionsOf;
    std::function<bool(const QList<SelItem>& items, const QString& name)> nameItems;
    // SC-COLOR: "Renk" row of a face / body: effective colour ("#rrggbb" or "", *inherited = from the body) and
    // a palette menu (caller-owned by the given parent) that colours / resets exactly `items`.
    std::function<QString(const SelItem& item, bool* inherited)> colorOf;
    std::function<QMenu*(QWidget* parent, const QList<SelItem>& items)> colorMenu;
  };
  PropertiesPanel(IDesignHost* host, QWidget* parent = nullptr);
  void setHooks(Hooks h) { hooks_ = std::move(h); }

  void rebuild();                                   // from host->selection() + document + model
  QTreeWidget* tree() const { return tree_; }       // objectName "propertiesTree"
  QWidget* editor(const QString& key) const { return editors_.value(key, nullptr); }
  QString valueText(const QString& label) const;    // read-only / editor text of the first row named `label`
  QStringList rowLabels() const;                    // every property label (categories excluded)

private:
  QTreeWidgetItem* category(const QString& title);
  QTreeWidgetItem* row(QTreeWidgetItem* cat, const QString& label, const QString& value, const QString& tip = QString());
  void exprRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QString& expr,
               std::function<bool(const QString&)> commit, pc::ParamKind kind, const QString& hint);
  void comboRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QStringList& choices,
                const QString& cur, std::function<bool(const QString&)> commit);
  void textRow(QTreeWidgetItem* cat, const QString& label, const QString& key, const QString& cur,
               std::function<bool(const QString&)> commit);
  void checkRow(QTreeWidgetItem* cat, const QString& label, const QString& key, bool cur, std::function<void(bool)> commit);
  void buttonRow(QTreeWidgetItem* cat, const QString& label, const QString& text, std::function<void()> fn);
  void measureRows(const QList<Hooks::MeasureRow>& rows);
  void colorRow(QTreeWidgetItem* cat, const SelItem& item);   // SC-COLOR
  void nsSection(const QList<SelItem>& items);   // "Adlandırılmış seçim" category with the editable "Ad" row

  void showNone();
  void showBody(const QString& name);
  void showFace(const QString& name);
  void showEdge(const QString& name);
  void showDatum(const QString& id);
  void showSketch(const QString& id);
  void showRegion(const QString& name);
  void showFeature(const QString& id);
  void showParameter(const QString& name);
  void showMulti(const QList<SelItem>& items);
  void featureFields(QTreeWidgetItem* cat, const pc::Feature& f, const QStringList& onlyKeys);
  bool commitFeatureProp(const std::string& fid, const std::string& key, const QString& value);

  IDesignHost* host_;
  Hooks hooks_;
  QTreeWidget* tree_ = nullptr;
  QHash<QString, QWidget*> editors_;
};

} // namespace pcd
