#pragma once
#include "FeatureDialog.h"
#include "SketchEditor.h"
#include "ToolApi.h"
#include "pc/Document.h"
#include "pc/MfgChecks.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include "pc/ShapeViewer.h"
#include "pc/SectionControls.h"
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QPointer>
#include <QTimer>
#include <QMenu>
#include <set>
#include <QMainWindow>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QUndoStack>
#include <memory>
#include <map>
#include <TopoDS_Compound.hxx>

class QDockWidget;
class QTabWidget;
class QToolButton;

namespace pcd {

class Ribbon;
class StructurePanel;
class SelectionPanel;
class OptionsPanel;
class PropertiesPanel;
class StatusBarWidgets;
class ParamSelectController;
class NamedSelectionPanel;
class NsCommand;
class VisCommand;
class Import2DController;   // SC-2D-IMPORT
class LayerCommand;         // SC-2D-IMPORT
class ViewportHud;          // SC23 (ViewportHud.h)
class DocumentTabBar;       // SC23 WP-S (not created yet: docTabs_ stays nullptr until WP-S adds the class)
class ViewsPanel;           // SC23 WP-S (not created yet: viewsPanel_ stays nullptr until WP-S adds the class)

// SC-UX (WP-A): the designer main window is the SpaceClaim-like shell (ribbon, Structure / Options / Properties
// panels, status bar widgets, design modes K/X/D) and implements IDesignHost for WP-B's tools (ToolApi.h).
class MainWindow : public QMainWindow, public IDesignHost {
  Q_OBJECT
public:
  MainWindow();
  ~MainWindow() override;

  bool openFile(const QString& path);
  void newDocument();
  const pc::Document& document() const override { return doc_; }
  bool inSketchMode() const { return sketching_; }

  // --- automation helpers (CLI screenshots) ---
  void runCheck();                                   // "Şimdi Denetle"
  void enterSketchEdit(const std::string& featureId);
  void openFeatureEditor(const std::string& featureId);
  FeatureDialog* featureDialog() const { return featDlg_; }
  bool tryParameterChange(const std::string& name, const std::string& expr);  // full change pipeline
  void setReviewScreenshotPath(const QString& p) { reviewShot_ = p; }
  void selectIssueRow(int row);
  void openMfgSettings();
  QWidget* viewerWidget() { return viewer_; }
  pc::viewer::ShapeViewer* viewer() const override { return viewer_; }
  void setAutoYes(bool on) { autoYes_ = on; }
  SketchEditor* sketchEditor() const override { return sketchEd_; }
  void beginSketchOn(pc::SketchPlaneKind k);
  void finishSketchPublic(bool accept) { finishSketch(accept); }
  void createFeature(pc::FeatureType t) { newFeature(t); }
  const pc::model::ModelResult& modelResult() const override { return model_; }
  QUndoStack* undoStack() { return &undo_; }
  void rollbackPublic(int idx) { rollbackTo(idx); }
  bool renameParam(const std::string& from, const std::string& to);
  void deleteFeaturePublic(const std::string& id) { deleteFeature(id); }
  // v6: parameters that drive a feature (direct + indirect through parameter formulas), one per line
  // "NAME = value unit  [where]". Used by the tree context menu "Parametreleri göster".
  QString featureParameterSummary(const QString& featureId) const;
  // v6: an open sketch is committed ("Sketch'i Bitir") or, after confirmation, discarded before the document
  // is saved / replaced / closed. Returns false if the user cancelled (or the commit was refused).
  bool resolveOpenSketch(bool allowDiscard = true);
  // v6: finished sketches drawn as wires in the 3D view (named "sketch:<id>:e<entity>", Ctrl+click = edit).
  // Default: shown unless consumed by a solid feature; the tree context menu toggles it per sketch.
  bool sketchShownIn3D(const std::string& id) const;
  void setSketchShownIn3D(const std::string& id, bool on);
  int sketchOverlayEdgeCount() const { return sketchOverlayEdges_; }
  int sketchDimLabelCount() const { return int(sketchLabels_.size()); }
  void toggleSuppressPublic(const std::string& id) { toggleSuppress(id); }

  // --- v2: datum planes & bodies ---
  bool createPlaneOnEdge(const QString& edge, const QString& kind = "edgeNormal");  // commits through the pipeline
  void openPlaneDialog(const QString& edge = QString(), const QString& kind = "edgeNormal");
  void beginSketchOnPlane(const std::string& planeFeatureId);
  void showEdgeMenu(const QString& edge, const QPoint& globalPos);
  void showDatumMenu(const QString& plane, const QPoint& globalPos);
  bool renameBody(const QString& from, const QString& to);
  void setBodyVisible(const QString& body, bool visible);
  bool bodyVisible(const QString& body) const { return !hiddenBodies_.count(body.toStdString()); }
  void selectBody(const QString& body);
  QStringList bodyNames() const;
  QTreeWidget* bodiesTree() const { return bodiesTree_; }
  QTreeWidget* featureTree() const { return tree_; }
  QTableWidget* issuesTable() const { return issueTable_; }
  QString selectedEdge() const;                    // SC-UX: last edge of the SelectionModel (else the last picked)
  QString selectedDatum() const { return selectedDatum_; }
  void updatePlanePreviewNow() { updatePlanePreview(); }
  // --- v4: section view (view-only state, never saved in the document) ---
  pc::viewer::SectionControls* sectionControls() const { return section_; }

  // --- SC-UX shell (WP-A) ---
  Ribbon* ribbon() const { return ribbon_; }
  ToolManager* toolManager() const { return tools_; }
  StructurePanel* structurePanel() const { return structure_; }
  OptionsPanel* optionsPanel() const { return options_; }
  PropertiesPanel* propertiesPanel() const { return props_; }
  StatusBarWidgets* statusWidgets() const { return sbw_; }
  QTableView* parameterTable() const { return paramView_; }
  bool planePickActive() const { return planePick_; }   // "Sketch düzlemi seç" state (K without a plane)
  void resetLayout();                                    // "Düzeni sıfırla"
  void showMassProperties();                             // Ölçüm > Kütle özellikleri
  void hideSelectedBodies();                             // Ctrl+H: selected faces / bodies (context-sensitive)
  void hideOtherBodies();                                // Alt+H: Diğerlerini gizle (isolate the selection)
  void showAllBodies();                                  // Ctrl+Shift+H: Tümünü göster

  // --- Gizle / Göster (MainWindowVis.cpp): view state in Document::view, resolved like named selections ---
  // Each call that changes something is ONE undo step without regeneration; returns false if nothing changed.
  bool hideItems(const QList<SelItem>& items);           // Face / Body items (others ignored)
  bool showItems(const QList<SelItem>& items);
  bool isolateItems(const QList<SelItem>& items);        // hide everything else (Diğerlerini gizle)
  bool showAllItems();                                   // Tümünü göster
  bool setItemsTransparent(const QList<SelItem>& items, bool on);   // Saydam yap / Saydamlığı kaldır
  void toggleSelectionTransparent();
  void setFaceVisible(const QString& face, bool visible);
  bool faceHidden(const QString& face) const;            // hidden itself or through its body
  bool faceTransparent(const QString& face) const;
  QStringList hiddenFaceNames() const;                   // every face not drawn (incl. faces of hidden bodies)
  QStringList hiddenBodyNames() const;
  QStringList hiddenFacesOfBody(const QString& body) const;   // faces hidden one by one in this body
  int hiddenOwnFaceCount() const;                        // faces hidden one by one (not through a hidden body)
  QString visibilityStatusText() const;                  // "n gizli yüz, m gizli gövde" ("" = nothing hidden)
  void createGroupInteractive();                         // Ctrl+G outside a tool: new parameter (Groups)
  // Left-drag mode of the Orient group (Çevir / Kaydır / Yakınlaştır): 0 none, 1 spin, 2 pan, 3 zoom.
  int leftDragMode() const { return leftDrag_; }
  // "Seçimden parametre": badge / quick popup / menus / Properties rows for the current selection
  ParamSelectController* paramSelect() const { return paramSel_; }
  Import2DController* import2d() const { return import2d_; }   // SC-2D-IMPORT: 2B çizim, Katmanlar, görünüş yerleştir

  // --- Named selections / Adlandırılmış seçimler (MainWindowNs.cpp, docs/named_selections/MODEL.md) ---
  // SpaceClaim "Groups > Named Selections": Ctrl+G with faces / edges / bodies selected in 3D (sketch / dimension
  // selections keep making parameters), Gruplar panel section, viewport and Properties "Ad". Every edit is ONE undo
  // step; selections live in the Document (.pcad, Save-as-App, STEP names).
  NamedSelectionPanel* namedSelectionPanel() const { return nsPanel_; }
  const pc::model::NamedSelectionManager& namedSelectionManager() const { return nsMgr_; }
  bool canCreateNamedSelection() const;                   // 3D, selection holds model faces / edges / bodies
  bool createNamedSelectionFromSelection(bool startRename = true);
  bool renameNamedSelection(const QString& nameOrId, const QString& to);
  bool removeNamedSelection(const QString& nameOrId);
  void selectNamedSelection(const QString& nameOrId);      // = click in the panel: select + highlight its entities
  bool replaceNamedSelectionMembers(const QString& nameOrId);           // "Seçimi değiştir"
  bool editNamedSelectionMembers(const QString& nameOrId, bool add);    // "Seçili ögeleri ekle / çıkar"
  bool rebindNamedSelectionMember(const QString& nameOrId, int memberIndex, const SelItem& to);   // explicit only
  bool setNamedSelectionColor(const QString& nameOrId, const QColor& c);
  bool setNamedSelectionExportStep(const QString& nameOrId, bool on);
  void setNamedSelectionTint(const QString& nameOrId, bool on);         // view state: tint its faces permanently
  bool namedSelectionTinted(const QString& nameOrId) const;
  QStringList namedSelectionsOf(const QList<SelItem>& items) const;     // selections containing ALL items
  bool nameItems(const QList<SelItem>& items, const QString& name);     // Properties "Ad"
  void showNamedSelectionMenu(const QString& nameOrId, int memberIndex, const QPoint& globalPos);
  bool namedSelectionLabelsShown() const { return showNsLabels_; }
  QStringList namedSelectionLabelTexts() const { return nsLabelTexts_; }   // labels drawn in the viewer
  // test hooks: last face highlight passed to the viewer (names + colour)
  QStringList lastHighlightedFaces() const { return lastHlFaces_; }
  QColor lastHighlightColor() const { return lastHlColor_; }
  QString hoverInfoText() const;

  // --- IDesignHost (ToolApi.h) ---
  QWidget* hostWindow() override { return this; }
  int rollbackIndex() const override { return rollback_; }
  SelectionModel* selection() const override { return selection_; }
  DesignMode designMode() const override;
  bool setDesignMode(DesignMode m) override;
  bool beginSketch(const pc::SketchPlane& plane) override;
  bool editSketch(const std::string& sketchFeatureId) override;
  bool finishSketch(bool accept) override;
  std::string openSketchId() const override { return sketching_ ? editSketchId_ : std::string(); }
  bool beginSketchPlanePick() override;
  bool openSketchFrame(double origin[3], double u[3], double v[3], double normal[3]) const override;
  pc::Document* expressionContext() override;
  bool commitDocument(const pc::Document& cand, const QString& undoText) override;
  bool insertFeature(const pc::Feature& f, const QString& undoText) override;
  bool changeParameter(const std::string& name, const std::string& expr) override;
  bool deleteFeatureInteractive(const std::string& featureId) override;
  bool setFeatureSuppressed(const std::string& featureId, bool suppressed) override;
  pc::model::ModelResult evaluateCandidate(const pc::Document& cand) const override;
  bool evaluateExpression(const QString& expr, double* value, QString* error = nullptr) const override;
  QString createParameterInteractive(const QString& suggestedName, pc::ParamKind kind, const QString& valueExpr,
                                     QWidget* parent) override;
  void setGhost(const TopoDS_Shape& s) override;
  void showPrompt(const QString& text, int ms = 0) override;
  void showQuickMeasure(const QString& text) override;
  void setToolEmphasis(const QStringList& faces, const QStringList& edges) override;
  void openClassicEditor(const std::string& featureId) override { openFeatureEditor(featureId); }
  bool featureDialogOpen() const override { return featDlg_ != nullptr; }
  void refreshViewer(bool keepCamera = true) override { updateViewer(keepCamera); }
  void showOperationFeedback(const pcd::OpStatus& status) override;   // [SC23] DesignMode.cpp (WP-O)

  // ======================================================================================================
  // SC23 FROZEN (P0-SC23, docs/sc2023/P0_CONTRACT.md). Members and declarations both SC23 work packages build
  // on. Do not rename / remove; changes go through docs/CONTRACT_REQUESTS.md with the prefix "SC23:".
  //   WP-S (shell/layout): hud_ texts + ViewportHud.cpp, docTabs_, viewsPanel_, actViewTrimetric_, buildHud()
  //   WP-O (operation/state): showOperationFeedback (DesignMode.cpp), rebuildStructure (MainWindowStructure.cpp)
  // ======================================================================================================
  ViewportHud* viewportHud() const { return hud_; }
  DocumentTabBar* documentTabs() const { return docTabs_; }
  ViewsPanel* viewsPanel() const { return viewsPanel_; }

  bool spaceClaimNavigation() const { return !navFallback_; }

signals:
  void designModeChanged(pcd::DesignMode m);

protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

private slots:
  void onFaceHovered(const QString& name);

private:
  // Picks. mods / clicks / pos come from ShapeViewer::picked (SpaceClaim scheme); the legacy signals pass the
  // current keyboard modifiers. The host's special states are handled first, then the active tool, then the
  // legacy behaviour (SelectionModel).
  void onViewerPicked(const QString& name, int kind, Qt::KeyboardModifiers mods, int clicks, const QPoint& pos);
  void onFacePicked(const QString& name, Qt::KeyboardModifiers mods, int clicks = 1, QPoint pos = QPoint(-1, -1));
  void onFacePicked(const QString& name);
  void onEdgePicked(const QString& name, Qt::KeyboardModifiers mods, int clicks = 1, QPoint pos = QPoint(-1, -1));
  void onEdgePicked(const QString& name);
  void onDatumPicked(const QString& name, Qt::KeyboardModifiers mods, int clicks = 1, QPoint pos = QPoint(-1, -1));
  void onDatumPicked(const QString& name);
  static SelItem::Kind selKindOfPick(const QString& name, int viewerKind);
  void syncHistoryToFace(const QString& face);
  void syncSketchFrame();                 // overlay frame of the open sketch (offset / parameters changed)
  void notifyDesignMode();                // tools_->onDesignModeChanged + designModeChanged, after the mode is in effect
  std::set<std::string> regionSketches() const;   // sketches whose regions are shown (pickable) in 3D
  void applySketchTransparency();

private:
  friend class DocCommand;
  friend class NsCommand;
  friend class VisCommand;
  friend class Import2DController;   // SC-2D-IMPORT
  friend class LayerCommand;         // SC-2D-IMPORT
  Import2DController* import2d_ = nullptr;   // SC-2D-IMPORT
  friend class ColorCommand;   // SC-COLOR
  // Gizle / Göster (MainWindowVis.cpp)
  void visUpdate();                       // resolve doc_.view against model_ (after regeneration / view change)
  void updateVisStatus();
  void applyViewState(const pc::ViewState& v);   // undo / redo: no regeneration
  bool visCommit(const pc::ViewState& v, const QString& text);
  bool visEdit(pc::NamedSelection& sel, const std::vector<pc::model::ns::Ref>& refs, bool add);
  std::vector<pc::model::ns::Ref> visRefsOf(const QList<SelItem>& items) const;
  QList<SelItem> visTargetsOfSelection() const;
  void addVisibilityMenu(QMenu* m, const QString& face);
public:   // SC-COLOR
  bool setItemsColor(const QList<SelItem>& items, const QString& hex);   // hex "" = reset; ONE undo step, no regeneration
  bool colorSelection(const QColor& c);                                  // invalid colour = reset
  bool resetSelectionColor();
  QString itemColor(const SelItem& it, bool* inherited = nullptr) const; // "#rrggbb" or "" (effective colour)
  QMenu* makeColorMenu(QWidget* parent, std::function<QList<SelItem>()> targets);
  void addColorMenu(QMenu* m, const QString& face);
  pc::Document appPayloadDocument() const;   // document for Save-as-App: hidden/transparent cleared, view.colors kept
private:
  std::set<std::string> hiddenFaces_, hiddenOwnFaces_, transparentFaces_, transparentBodies_;
  // SC-COLOR: face / body colours (MainWindowColors.cpp, docs/colors/COLORS.md)
  void colorsUpdate();                    // resolve doc_.view.colors against model_ (called from visUpdate)
  void applyUserColors(std::map<std::string, QColor>& colors) const;   // overlay on the per-face colour map
  void fillColorMenu(QMenu* m, std::function<QList<SelItem>()> targets);
  void createColorRibbonGroup();
  void updateColorButton();
  std::map<std::string, std::string> userFaceColor_, userFaceOwnColor_, userBodyColor_;   // "#rrggbb"
  QColor lastColor_{255, 0, 0};
  QAction *actColor_ = nullptr, *actColorReset_ = nullptr;
  QToolButton* visStatus_ = nullptr;
  QString visTreeSig() const;             // hidden-face rows + transparent bodies shown in the Yapı tree
  QString visTreeSig_;
  QAction* actTransparent_ = nullptr;
  // named selections (MainWindowNs.cpp)
  void createNamedSelectionPanel(QWidget* groupsPage);
  void nsUpdate();                        // resolve against model_ (notifications -> non-modal status message)
  void nsRebuildPanel();
  void nsApplyDecorations();              // labels + tints (no regeneration)
  void appendNsLabels(std::vector<pc::viewer::Label3D>& labels);
  void applyNsDocument(const pc::Document& d);   // NS-only undo steps: the geometry is unchanged
  bool nsCommit(const pc::Document& cand, const QString& text, const std::string& createdId = std::string(),
                const std::string& renamedId = std::string());
  std::vector<pc::model::ns::Ref> nsRefsOf(const QList<SelItem>& items) const;
  const pc::NamedSelection* nsFind(const QString& nameOrId) const;
  QList<SelItem> nsItemsOf(const pc::model::NsResolution& r) const;
  QString nsHoverSuffix(const QString& entity) const;
  bool nsRename(const QString& nameOrId, const QString& to, bool joinCreateStep);
  QLabel* nsStatusLbl_ = nullptr;
  NamedSelectionPanel* nsPanel_ = nullptr;
  pc::model::NamedSelectionManager nsMgr_;
  bool nsQuiet_ = true;                   // next update: no status messages (new / opened document)
  QStringList nsMsgs_;
  QString nsActive_;                      // id of the selection clicked in the panel (its colour highlights)
  QList<SelItem> nsActiveItems_;
  std::set<std::string> nsTint_;          // ids with "Vurguyu göster" (view state, not saved)
  std::string nsPendingRename_;           // created by Ctrl+G, inline editor open: the typed name joins that step
  bool showNsLabels_ = false;
  QAction* actNsLabels_ = nullptr;
  QStringList nsLabelTexts_;
  QStringList lastHlFaces_;
  QColor lastHlColor_;
  // construction (MainWindowShell.cpp)
  void buildActions();
  void buildMenus();
  void buildRibbon();
  void buildDocks();
  void buildStatusBar();
  void createToolManager();
  void wireSelection();
  void restoreLayout();
  void saveLayout();

  // document state
  void applyDocument(const pc::Document& d, bool useCache);
  void refreshAll(bool keepCamera);
  void updateViewer(bool keepCamera);
  void rebuildTree();
  void rebuildParams();
  void rebuildIssues();
  void updateStatus();
  void updateTitle();
  void regenerateModel();
  void pushCommit(const pc::Document& cand, const QString& desc, const pc::mfg::ValidationResult* vr);

  // change pipeline
  bool tryCommit(pc::Document cand, const QString& desc);
  bool commitNoValidate(pc::Document cand, const QString& desc);

  // file ops
  bool maybeSave();
  void fileOpen();
  bool fileSave();
  bool fileSaveAs();
  void saveAsApp();
  void exportShape(bool step);
  void addRecent(const QString& p);
  void rebuildRecentMenu();

  // features
  void newFeature(pc::FeatureType t);
  void newSketchFlow();
  void openFeatureDialog(const pc::Feature& f, bool isNew);
  void onFeatureDialogAccepted(FeatureDialog* dlg, pc::Document* work);
  void deleteFeature(const std::string& id);
  void toggleSuppress(const std::string& id);
  void rollbackTo(int idx);
  void treeContextMenu(const QPoint& pos);
  std::string currentTreeFeature() const;
  bool isPlanarFace(const QString& name) const;
  bool insertFeatureCommit(const pc::Feature& nf, const QString& desc);
  void applyBodyAppearance();
  std::vector<pc::Parameter> dlgNewParams_;
  pc::Document* dlgWork_ = nullptr;           // working copy of the open feature dialog (params kept in sync)
  void onParameterCreated(const pc::Parameter& p);   // v6: commit hook for ExprEdit / dimension dialogs
  pc::Document sketchCandidate() const;       // v6: CURRENT document + the edited sketch
  void selectParamRow(const QString& name);
  ExprEdit* sketchOffset_ = nullptr;          // v6: sketch plane offset field (ribbon "Sketch Oturumu" group)
  bool updatingOffset_ = false;
  std::map<std::string, bool> sketchShown_;   // explicit per-sketch 3D visibility (view state, not saved)
  int sketchOverlayEdges_ = 0;
  mutable std::vector<pc::viewer::Label3D> sketchLabels_;   // filled by addSketchOverlay
  std::string labelSketchId_;                  // sketch whose dimension values are labelled in 3D
  bool showSketchDims_ = true;
  int addSketchOverlay(TopoDS_Compound& comp, std::map<std::string, TopoDS_Edge>& names) const;   // v3: params created by the open feature dialog (merged on OK)
  void applyDatumPlanes(const pc::model::DatumPlane* preview = nullptr, const std::string& previewId = std::string());
  void rebuildBodies();
  void updatePlanePreview();
  void bodiesContextMenu(const QPoint& pos);
  void viewerContextMenu(const QPoint& pos);

  // params
  void onParamItemChanged(QStandardItem* it);
  void addParameter();
  void removeParameter();
  bool renameParameter(pc::Document& d, const std::string& from, const std::string& to);
  void applySuggestion(int issueRow);

  // sketch mode
  void setSketchMode(bool on);

  // ---- SC-UX shell (DesignMode.cpp / MainWindowShell.cpp)
  void syncModeActions();
  void cancelPlanePick(const QString& msg = QString());
  void applyPlanePickPreview();
  void updatePlanPreviewHover(const QString& face);
  bool sketchPlaneFromSelection(pc::SketchPlane* out, std::string* editId, std::string* datumId) const;
  void onToolChanged(const QString& id);
  void bridgeStubTool(const QString& id);
  void refreshOptionsPanel();
  void schedulePanelsRefresh();
  void refreshPanels();
  void rebuildStructure();
  void onSelectionChanged();
  void highlightSelection();
  void forwardPick(const pcd::SelItem& item, int clickCount);
  void structureContextMenu(const pcd::SelItem& item, const QPoint& globalPos);
  void deleteSelectionCommand();
  void homeView();
  void planView();
  void zoomExtents();
  void zoomStep(int dir);
  void previousView();
  void nextView();
  void setLeftDragMode(int m);
  bool navigationFilter(QEvent* e);
  bool detectNavigationScheme();      // true: the viewer offers NavigationScheme::SpaceClaim (now applied)
  void selectFeatureInHistory(const std::string& id);
  void setHistoryEnabled(bool on);
  QStringList facesOfSelection() const;

  // ---- state
  pc::Document doc_;
  pc::model::ModelResult model_;
  pc::mfg::Report report_;
  QString path_;
  double regenMs_ = 0;
  int rollback_ = -1;
  QUndoStack undo_;
  bool pendingValid_ = false;
  pc::model::ModelResult pendingModel_;
  QString selectedFace_;
  enum class Pick { None, SketchFace, SketchDatum } pick_ = Pick::None;
  QString selectedEdge_, selectedDatum_, pendingTwoEdge_;
  std::set<std::string> hiddenBodies_;
  bool showPlanes_ = true;
  QPointer<QMenu> ctxMenu_;
  QTimer* previewTimer_ = nullptr;
  QPointer<FeatureDialog> previewDlg_;
  pc::Document* previewWork_ = nullptr;
  QTreeWidget* bodiesTree_ = nullptr;
  bool updatingBodies_ = false;
  bool updatingParams_ = false;
  bool autoYes_ = false;
  QString reviewShot_;

  // sketch editing state
  bool sketching_ = false;
  pc::Document work_;
  std::string editSketchId_;
  bool editSketchNew_ = false;
  int editSketchIndex_ = -1;
  pc::SketchPlane newPlane_;

  // feature dialog
  FeatureDialog* featDlg_ = nullptr;

  // ---- widgets
  pc::viewer::ShapeViewer* viewer_ = nullptr;
  pc::viewer::SectionControls* section_ = nullptr;
  SketchEditor* sketchEd_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QTableView* paramView_ = nullptr;
  QStandardItemModel* paramModel_ = nullptr;
  QTableWidget* issueTable_ = nullptr;
  QLabel *lblRegen_ = nullptr, *lblVol_ = nullptr, *lblDof_ = nullptr, *lblIssues_ = nullptr, *lblHover_ = nullptr;
  QMenu *recentMenu_ = nullptr, *insertMenu_ = nullptr;
  QAction *actUndo_ = nullptr, *actRedo_ = nullptr, *actSave_ = nullptr;
  QList<QAction*> featureActions_;

  // ---- SC-UX shell
  Ribbon* ribbon_ = nullptr;
  StructurePanel* structure_ = nullptr;
  SelectionPanel* selPanel_ = nullptr;
  OptionsPanel* options_ = nullptr;
  PropertiesPanel* props_ = nullptr;
  QTabWidget* optionsTabs_ = nullptr;
  StatusBarWidgets* sbw_ = nullptr;
  SelectionModel* selection_ = nullptr;
  ToolManager* tools_ = nullptr;
  QDockWidget *dockStructure_ = nullptr, *dockOptions_ = nullptr, *dockIssues_ = nullptr;
  QWidget* paramsPage_ = nullptr;
  QWidget* miniToolbar_ = nullptr;
  QWidget* guideBar_ = nullptr;
  QMenu *menuFile_ = nullptr, *menuViews_ = nullptr, *menuView_ = nullptr, *menuPattern_ = nullptr;
  QAction *actNew_ = nullptr, *actOpen_ = nullptr, *actSaveAs_ = nullptr, *actSaveAsApp_ = nullptr, *actQuit_ = nullptr;
  QAction *actMfgSettings_ = nullptr, *actCheckNow_ = nullptr, *actFit_ = nullptr, *actHome_ = nullptr, *actPlanView_ = nullptr;
  QAction *actPrevView_ = nullptr, *actNextView_ = nullptr, *actZoomIn_ = nullptr, *actZoomOut_ = nullptr;
  QAction *actSpin_ = nullptr, *actPan_ = nullptr, *actZoomDrag_ = nullptr;
  QAction *actModeSketch_ = nullptr, *actModeSection_ = nullptr, *actMode3D_ = nullptr;
  QAction *actCreateGroup_ = nullptr, *actHideBody_ = nullptr, *actHideOthers_ = nullptr, *actShowAll_ = nullptr;
  QAction *actDelete_ = nullptr, *actResetLayout_ = nullptr, *actMass_ = nullptr, *actMeasure_ = nullptr, *actFileMenu_ = nullptr;
  QAction *actFinishSketch_ = nullptr, *actCancelSketch_ = nullptr, *actSketchFit_ = nullptr;
  QAction *actClassicCombine_ = nullptr, *actBodyCombine_ = nullptr;
  QList<QAction*> sketchContextActions_;      // finish / cancel / sketch fit: enabled while sketching
  QByteArray defaultLayout_;
  bool planePick_ = false;
  QString planePickHoverFace_;
  int leftDrag_ = 0;
  // shell-side navigation fallback (until WP-B's ShapeViewer::setNavigationScheme exists)
  bool navFallback_ = false;
  bool navSynth_ = false;
  int navMode_ = 0;                // 0 none, 1 spin (left-synth), 2 pan (passthrough), 3 zoom (wheel synth), 4 swallow
  QPoint navLast_, navPress_;
  bool navMoved_ = false;
  bool selFromLegacy_ = false;     // selection set by a legacy path that already highlighted
  bool panelsQueued_ = false;
  bool bridgingTool_ = false;
  bool toolsReal_ = false;         // ToolManager has real IActiveTool objects (WP-B), not the P0 stub
  pc::Document exprWork_;          // expressionContext() outside sketch / dialog (copy of doc_)
  QStringList toolEmphFaces_, toolEmphEdges_;
  QString hoverFace_;
  bool showBodyColors_ = true;
  ParamSelectController* paramSel_ = nullptr;
  void createParamSelect();

  // ---- SC23 FROZEN (see the block above showOperationFeedback)
  void buildHud();                         // MainWindowShell.cpp (WP-S): hud_ over viewer_ + its wiring
  void updateHudToolText();                // short active-tool name ("Çek")
  void updateHudSelectionText();           // short selection summary ("1 bölge")
  ViewportHud* hud_ = nullptr;             // child of viewer_, objectName "viewportHud"
  DocumentTabBar* docTabs_ = nullptr;      // WP-S placeholder (S-P1-2 "Part1*")
  ViewsPanel* viewsPanel_ = nullptr;       // WP-S placeholder (S-P1-4 Görünümler)
  QAction* actViewTrimetric_ = nullptr;    // WP-S placeholder (S-P1-4 Trimetrik, Ctrl+1)
};

} // namespace pcd
