#pragma once
// PersistCAD viewer — Qt OpenGL 3D viewer for OCCT shapes with persistent-name face picking.
// OWNER: Agent D. Shared by designer and runner. Interface frozen.
// Rendering: OCCT BRepMesh tessellation -> own VBOs (QOpenGLWidget, GL 3.3 core or 2.1 fallback),
// per-face ID picking buffer, edges drawn as lines, orbit/pan/zoom mouse navigation.
#include <QColor>
#include <QImage>
#include <QOpenGLWidget>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector3D>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pc::viewer {

// v2: datum plane overlay (translucent square + outline + normal arrow + label).
struct DatumPlaneDisplay {
  QString name;
  double origin[3] = {0, 0, 0};
  double u[3] = {1, 0, 0};
  double v[3] = {0, 1, 0};
  double halfSize = 50;
  bool highlighted = false;
};

// v6 (additive): screen-aligned text label anchored at a world point (e.g. sketch dimension "W = 40").
struct Label3D {
  double pos[3] = {0, 0, 0};
  QString text;
};

// SC-2D-IMPORT (additive): display style of a named edge (imported drawing layers: colour + line pattern).
struct EdgeStyle {
  QColor color;
  int pattern = 0;   // 0 solid, 1 dashed, 2 center (long-short), 3 dotted
};

class ShapeViewer : public QOpenGLWidget {
  Q_OBJECT
  // SC-UX (additive, WP-B): read-only camera state for test harnesses (ACCEPTANCE.md "test kancaları").
  Q_PROPERTY(double cameraAz READ cameraAz)
  Q_PROPERTY(double cameraEl READ cameraEl)
  Q_PROPERTY(double halfH READ viewHalfHeight)
  Q_PROPERTY(QVector3D target READ cameraTarget)
  Q_PROPERTY(bool ghostVisible READ ghostVisible)
public:
  explicit ShapeViewer(QWidget* parent = nullptr);
  ~ShapeViewer() override;

  // Replace displayed model. faceNames: persistent name -> face (from model::ModelResult).
  // keepCamera=true keeps the current view (used during parameter updates).
  void setModel(const TopoDS_Shape& shape,
                const std::map<std::string, TopoDS_Face>& faceNames,
                const std::map<std::string, TopoDS_Edge>& edgeNames = {},
                bool keepCamera = true);
  void clearModel();

  // Semi-transparent ghost of a previous/candidate shape (e.g. preview while validating).
  void setGhost(const TopoDS_Shape& shape);
  void clearGhost();

  // Highlight faces/edges by persistent name (issue display, selection).
  void setHighlightedFaces(const QStringList& names, const QColor& color = QColor(230, 60, 40));
  void setHighlightedEdges(const QStringList& names, const QColor& color = QColor(255, 140, 0));
  void clearHighlights();

  // Optional: show the active sketch plane (grid) — origin/u/v/normal in world coords.
  void setSketchPlane(bool visible, const double origin[3] = nullptr, const double u[3] = nullptr,
                      const double v[3] = nullptr);

  // v2: datum planes (replaces the previous set). Clicking a plane emits datumPicked(name).
  void setDatumPlanes(const std::vector<DatumPlaneDisplay>& planes);
  // v2: per-face base colours (e.g. one colour per body); faces not listed use the default colour.
  void setFaceColors(const std::map<std::string, QColor>& faceColors);
  // v2: hide faces (e.g. hidden bodies) without rebuilding the model.
  void setHiddenFaces(const QStringList& names);
  // SC-2D-IMPORT (additive): named edges drawn in their own colour / pattern (picking unchanged). {} = none.
  void setEdgeStyles(const std::map<std::string, EdgeStyle>& styles);
  int styledEdgeCount() const;

  // SC23 WP-S (additive): Trimetric = unequal foreshortening of the three axes (SpaceClaim Views panel, Ctrl+1 [G])
  enum class View { Iso, Top, Bottom, Front, Back, Left, Right, Trimetric };
  void setView(View v);
  void fitAll();
  void setShowEdges(bool on);
  void setShowAxes(bool on);

  // v2 (additive): hit tests for context menus. "" if nothing is under the widget position.
  QString edgeAt(const QPoint& widgetPos);
  QString datumAt(const QPoint& widgetPos);

  // Offscreen render (used for thumbnails, tests and screenshots).
  QImage renderToImage(int w, int h);

  // ---- v4 (additive): section view. View-only: the model is never modified. The model (faces, edges,
  // highlights, ghost) is clipped on the GPU and every solid crossing a plane gets a capped, hatched cut
  // face in a darker tone of the body colour. Cut faces are not pickable (they occlude what is behind:
  // a click on a cap reports "" like the background); faces in the removed region cannot be picked.
  // Datum planes and the sketch grid are not clipped.
  //
  // Generic single plane: the half-space (p - origin) . normal >= 0 is KEPT.
  // enabled=false (or a null/zero normal) switches the section off.
  void setSectionPlane(bool enabled, const double origin[3] = nullptr, const double normal[3] = nullptr);
  // Presets relative to the bounding box of the model (re-evaluated whenever the model changes):
  //   XZ      plane normal to Y, removes y < c   (the half facing the default isometric camera)
  //   YZ      plane normal to X, removes x > c
  //   XY      plane normal to Z, removes z > c
  //   Quarter XZ + YZ through the bbox centre, removes the quadrant x > cx && y < cy
  // offset: -1..1, position of the plane as a fraction of the bbox half-extent along its normal axis
  // (0 = bbox centre; for Quarter it shifts both planes). flip keeps the opposite side
  // (Quarter: removes the opposite quadrant).
  enum class SectionPreset { None, XZ, YZ, XY, Quarter };
  void setSectionPreset(SectionPreset preset, double offset = 0.0, bool flip = false);
  SectionPreset sectionPreset() const;   // None when off or when a generic plane is set
  bool sectionEnabled() const;
  // World coordinate (mm) of the preset plane along its axis (Y for XZ, X for YZ / Quarter, Z for XY).
  double sectionPosition() const;
  // "xz" | "yz" | "xy" | "quarter" (also "ceyrek", "çeyrek") | "none"; ok=false for anything else.
  static SectionPreset sectionPresetFromString(const QString& text, bool* ok = nullptr);
  // v4 (optional): translucent faces (e.g. a whole body) - still pickable, drawn as a see-through shell.
  void setTransparentFaces(const QStringList& names, double opacity = 0.28);
  // v6 (additive): labels drawn on top of the scene (no depth test, not clipped by the section). Replaces the set.
  void setLabels(const std::vector<Label3D>& labels);

  // ---- SC-UX (additive, WP-B; SPEC §6.2). Defaults keep the legacy behaviour, so the runner is unaffected. ----
  // Navigation scheme. Legacy: left drag = orbit (turntable), middle / Shift+left = pan, Ctrl+click = edge pick.
  // SpaceClaim: middle drag = spin (free rotation about the view centre), Shift+middle = pan, Ctrl+middle
  // (up/down) = zoom, wheel = zoom to cursor, left drag = leftDragMode() (None: nothing), plain click picks the
  // edge within 6 px before the face (facePicked / edgePicked / datumPicked + picked()), Ctrl/Shift/Alt clicks
  // are reported with their modifiers (selection is managed by the tools, not by the viewer), double / triple
  // click emit doubleClicked / tripleClicked, Shift+middle double click = look at the planar face under the
  // cursor (setViewToFace). Hover pre-highlights the face or edge under the cursor.
  enum class NavigationScheme { Legacy, SpaceClaim };
  void setNavigationScheme(NavigationScheme s);
  NavigationScheme navigationScheme() const;
  enum class DragMode { None, Spin, Pan, Zoom };              // left-drag mode (Orient group toggles)
  void setLeftDragMode(DragMode m);
  DragMode leftDragMode() const;
  // Projections (widget pixels; the view is orthographic, so plane -> screen is exactly affine).
  QPointF worldToScreen(const double p[3]) const;
  bool screenRay(const QPointF& px, double origin[3], double dir[3]) const;   // dir = unit view direction (into the screen)
  QTransform planeToScreen(const double o[3], const double u[3], const double v[3]) const;   // (s,t) -> o + s u + t v -> px
  double pixelsPerMm() const;                                  // widget pixels per model millimetre
  // Camera
  void setPlanView(const double o[3], const double u[3], const double v[3], bool fit);  // look along -normal, v up
  void setViewToFace(const QString& faceName);
  void setHomeView();                                          // isometric + fit
  void fitToBox(const double mn[3], const double mx[3]);
  bool previousView();                                         // 20-step camera history
  bool nextView();
  double cameraAz() const;                                     // degrees, from the eye direction
  double cameraEl() const;
  double viewHalfHeight() const;                               // half of the visible height (mm)
  QVector3D cameraTarget() const;
  void viewDirection(double eyeToTarget[3]) const;             // unit vector from the eye into the scene
  // Picking
  enum PickKind { PickFace = 1, PickEdge = 2, PickDatum = 4 };
  QString pickAt(const QPoint& px, int kinds = PickFace | PickEdge | PickDatum, int* kindOut = nullptr);
  void setPreselection(const QString& name);                   // hover highlight (face or edge); "" clears
  QString preselection() const;
  bool ghostVisible() const;
  // Area-weighted centroid and mean normal of a displayed face (from the tessellation). false if unknown.
  bool faceCentroid(const QString& name, double c[3], double n[3]) const;
  QStringList displayedFaceNames() const;

signals:
  // SC-UX (additive)
  void cameraChanged();
  void doubleClicked(const QString& name, int kind);
  void tripleClicked(const QString& name, int kind);
  // Every left click in the SpaceClaim scheme (kind = PickKind or 0 for background, clickCount 1/2/3).
  void picked(const QString& name, int kind, Qt::KeyboardModifiers modifiers, int clickCount, const QPoint& pos);
  void hovered(const QString& name, int kind);
  void facePicked(const QString& name);       // left click on a face ("" = background)
  void faceHovered(const QString& name);
  void edgePicked(const QString& name);       // ctrl+click near an edge
  void datumPicked(const QString& name);      // v2: click on a datum plane
  void sectionChanged();                      // v4: section planes changed (also after a model update)

protected:
  void initializeGL() override;
  void resizeGL(int w, int h) override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;   // SC-UX (additive)

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

} // namespace pc::viewer
