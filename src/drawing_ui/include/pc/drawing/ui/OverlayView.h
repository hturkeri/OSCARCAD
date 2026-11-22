#pragma once
// pc_drawing_ui — OverlayView: sheet background (raster / rendered PDF) + DrawingIR layers + confidence colours +
// editing tools. OWNER WP-2 (implementation). FROZEN API (DrawingIR 1.0): WP-3's wizard pages use only this header.
//
// The view NEVER mutates the IR: every user correction is emitted as a pc::drawing::UserEdit (UserEdit.h catalogue);
// the wizard appends it to ir.edits, re-runs the pipeline from that stage and calls refresh().
// Colours (SPEC §2.12 step 3): Visible black, Hidden blue dashed, Center red dash-dot, Dimension/Extension/Leader
// green, Hatch grey; confidence glow >=0.9 none/green, 0.6..0.9 yellow, <0.6 red.
#include "pc/drawing/DrawingIR.h"
#include <QGraphicsView>
#include <QImage>
#include <QRectF>
#include <cstdint>
#include <memory>
#include <vector>

namespace pc::drawing::ui {

enum OverlayLayer : uint32_t {
  LayerBackground = 1u << 0,   // raster / rendered PDF page
  LayerPrimitives = 1u << 1,   // norm primitives (raw if no norm)
  LayerTexts = 1u << 2,
  LayerRoles = 1u << 3,        // colour primitives by annot.roles (else by style)
  LayerConfidence = 1u << 4,   // glow by conf band
  LayerLayout = 1u << 5,       // border, title block, note blocks, view boxes + labels
  LayerDimensions = 1u << 6,   // dimension bindings (anchors, geometry highlight, residual colour)
  LayerViews = 1u << 7,        // axes, view relations
  LayerSketch = 1u << 8,       // current sketch candidate mapped back to the sheet
  LayerRaw = 1u << 9,          // draw ir.raw instead of ir.norm
  LayerDefault = LayerBackground | LayerPrimitives | LayerTexts | LayerRoles | LayerConfidence | LayerLayout
};

enum class OverlayTool {
  Select,        // click / rubber band; emits selectionChanged
  EditBoxes,     // drag/resize layout boxes -> setBorder/setTitleBlockBox/setViewBox; draw new box -> addView
  DrawLine, DrawArc /*3 clicks: start, through, end*/, DrawCircle /*centre, radius*/,   // -> addPrim
  Snap,          // drag an end point -> snapPoint
  PickGeometry   // two-click binding for the dimension table -> geometryPicked
};

class OverlayView : public QGraphicsView {
  Q_OBJECT
 public:
  explicit OverlayView(QWidget* parent = nullptr);
  ~OverlayView() override;

  void setIR(const DrawingIR* ir);  // non-owning; nullptr clears. Call refresh() after the IR changed.
  const DrawingIR* ir() const;
  void refresh();                   // rebuild scene items from the IR (keeps zoom + selection where ids survive)

  // Background image placed by `pxToSheet` (rectified px, y down -> sheet mm, see RasterInfo::pxToSheet).
  void setBackground(const QImage& img, const Affine2& pxToSheet);
  void clearBackground();

  void setLayers(uint32_t mask);    // OverlayLayer bits
  uint32_t layers() const;
  void setTool(OverlayTool t);
  OverlayTool tool() const;
  void setStage(Stage s);           // stage the emitted edits belong to (wizard page); default Normalize
  void setCurrentSketch(int sketchCandidateId);   // LayerSketch; -1 none

  void setSelection(const std::vector<int>& ids);   // element ids (prims, texts, dims, views)
  std::vector<int> selection() const;
  void highlight(const std::vector<int>& ids);      // transient emphasis (dimension table row hover)
  void zoomToSheet();
  void zoomToBox(const Box2& sheetBox, double marginMm = 5);

  // Headless tests: render the current scene to an image.
  QImage renderToImage(int widthPx);

 signals:
  void selectionChanged(const std::vector<int>& ids);
  void elementActivated(int id);                       // double click
  void editRequested(const pc::drawing::UserEdit& edit);
  void geometryPicked(const std::vector<int>& primIds, const std::vector<pc::drawing::Vec2>& pointsSheet);  // PickGeometry
  void cursorMoved(double xSheetMm, double ySheetMm);

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace pc::drawing::ui
