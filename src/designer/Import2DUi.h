#pragma once
// SC-2D-IMPORT — UI of the "2D drawing -> 3D" workflow (SpaceClaim video "Convert 2D Drawings to 3D Models"):
//   Dosya > İçe aktar > "2B çizim (eğri olarak)…" + drag & drop of .dxf / .pdf, the "Katmanlar" tab (Yapı panel),
//   "Görünüş seç" (one click = connected view) and "Görünüşü yerleştir" (fold a view 90° about an edge + snap).
// Owned by MainWindow (friend); every hook in the shared MainWindow files is one line marked "SC-2D-IMPORT".
#include "Import2D.h"
#include "ToolApi.h"
#include "pc/ShapeViewer.h"
#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUndoCommand>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace pcd {

class MainWindow;
class Import2DController;

// "Katmanlar" tab: layers with visibility check box, colour swatch, line style, member count; context menu.
class LayersPanel : public QWidget {
  Q_OBJECT
public:
  explicit LayersPanel(Import2DController* c, QWidget* parent = nullptr);
  QTreeWidget* tree() const { return tree_; }               // objectName "layersTree"
  void rebuild();
  QTreeWidgetItem* row(const QString& layer) const;
  QMenu* buildMenu(const QString& layer);                   // objectName "layersContextMenu"
  QPushButton* viewButton() const { return viewBtn_; }      // "layersViewSelect" (mirrors actViewSelect)
private:
  QPushButton* viewBtn_ = nullptr;
  Import2DController* c_;
  QTreeWidget* tree_ = nullptr;
  bool updating_ = false;
};

// Non-modal "Görünüşü yerleştir" dialog (objectName "import2dPlaceDialog").
class PlaceViewDialog : public QDialog {
  Q_OBJECT
public:
  PlaceViewDialog(Import2DController* c, const std::vector<import2d::CurveRef>& cluster, QWidget* parent);
  ~PlaceViewDialog() override;
  const import2d::Placement& placement() const { return pl_; }
  const std::vector<import2d::CurveRef>& cluster() const { return cluster_; }
  int pickTarget() const { return pickTarget_; }             // 0 none, 1 source anchor, 2 target anchor
  void setAnchor(int which, const gp_Pnt& p);                // picked / typed anchor
  void recompute();
  bool apply();                                              // "Uygula": ONE undo step
private:
  void updatePreview();
  void syncFields();
  Import2DController* c_;
  std::vector<import2d::CurveRef> cluster_;
  import2d::Placement pl_;
  bool userSrc_ = false, userDst_ = false;
  int pickTarget_ = 0;
  QComboBox* edge_ = nullptr;
  QDoubleSpinBox* angle_ = nullptr;
  QLineEdit *src_ = nullptr, *dst_ = nullptr;
  QPushButton *pickSrc_ = nullptr, *pickDst_ = nullptr;
  QLabel* info_ = nullptr;
};

class Import2DController : public QObject {
  Q_OBJECT
public:
  explicit Import2DController(MainWindow* w);
  MainWindow* window() const { return w_; }

  // ---- import
  // Imports a drawing as curves (ONE undo step). plane: XY/XZ/YZ, or OnPlane with planeRef. scale 0 = automatic;
  // an uncertain PDF scale asks the user unless `interactive` is false (then the suggestion is used).
  bool importFile(const QString& path, const pc::SketchPlane& plane = pc::SketchPlane(), double scale = 0, bool interactive = true);
  QString lastMessage() const { return lastMsg_; }
  QAction* importAction() const { return actImport_; }      // objectName "actImport2D"
  QAction* viewSelectAction() const { return actViewSel_; } // "actViewSelect" (checkable)
  QAction* placeViewAction() const { return actPlace_; }    // "actPlaceView"

  // ---- layers (view state; ONE undo step each, no regeneration)
  LayersPanel* layersPanel() const { return panel_; }
  QStringList layerNames() const;                           // "Katman0" first
  bool layerVisible(const QString& layer) const;
  bool setLayerVisible(const QString& layer, bool on);
  bool showOnlyLayer(const QString& layer);                 // "Yalnız bunu göster"
  bool showAllLayers();
  bool setLayerColor(const QString& layer, const QColor& c);
  bool setLayerLineStyle(const QString& layer, const QString& style);
  bool renameLayer(const QString& from, const QString& to);
  QString newLayer(const QString& name = QString());       // returns the name ("" on failure)
  bool moveSelectionToLayer(const QString& layer);          // curves / sketches / bodies of the selection
  bool selectLayerCurves(const QString& layer);
  int layerMemberCount(const QString& layer) const;
  QString layerOfSketch(const QString& sid) const;

  // ---- views
  bool selectViewAt(const QString& curveName, bool add = false);   // "Görünüş seç"
  QList<SelItem> selectedCurves() const;
  PlaceViewDialog* openPlaceDialog();                       // from the selected curves; nullptr + message if none
  PlaceViewDialog* placeDialog() const { return placeDlg_; }
  // Non-interactive placement (tests / scripting): default fold, optional forced edge / angle.
  bool placeSelection(import2d::FoldEdge edge = import2d::FoldEdge::Auto, double angleDeg = 90);
  std::vector<std::string> visibleImportedSketches() const;

  // ---- hooks called by MainWindow (one line each, "SC-2D-IMPORT")
  void refresh();                                           // refreshAll
  bool sketchHidden(const std::string& sid) const;          // sketchShownIn3D: hidden layer
  void appendHiddenFaces(QStringList& hidden) const;        // applyBodyAppearance: bodies of hidden layers
  void appendLabels(std::vector<pc::viewer::Label3D>& labels) const;   // updateViewer: notes
  void decorateViewer();                                    // updateViewer: layer colours / line styles
  bool handlePick(const QString& name, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos);
  bool showCurveMenu(const QString& edge, const QPoint& globalPos);   // right click on an imported curve
  int noteLabelCount() const { return noteLabels_; }

  // used by the panel / dialog
  void showStatus(const QString& msg, int ms = 6000);
  bool commitLayers(const pc::LayerTable& t, const QString& text);
  gp_Pnt nearestSnap(const QString& curveName, const QPoint& pos, bool* ok) const;
  void setGhostCurves(const std::vector<import2d::CurveRef>& curves, const gp_Trsf& T);
  void clearGhost();
  void placeDialogClosed();

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void install();
  MainWindow* w_;
  LayersPanel* panel_ = nullptr;
  QAction *actImport_ = nullptr, *actViewSel_ = nullptr, *actPlace_ = nullptr;
  QPointer<PlaceViewDialog> placeDlg_;
  QString lastMsg_;
  mutable int noteLabels_ = 0;
};

// Undo step of a layer-only change (no regeneration).
class LayerCommand : public QUndoCommand {
public:
  LayerCommand(MainWindow* w, pc::LayerTable before, pc::LayerTable after, const QString& text);
  void undo() override;
  void redo() override;
private:
  void apply(const pc::LayerTable& t);
  MainWindow* w_;
  pc::LayerTable before_, after_;
};

} // namespace pcd
