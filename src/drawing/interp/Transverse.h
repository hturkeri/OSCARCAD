#pragma once
// R3 — transverse (end / cross-section) view analysis for non-axisymmetric features of turned parts.
// WP-3 private header (not part of the frozen contract).
//
// A transverse view is a set of concentric circles/arcs (sheet) with repeated features around the centre:
//   * tangential slots: two parallel walls on the SAME side of the centre (signed offsets d1, d2 w.r.t. the
//     wall's outward direction t):  offset e = (d1+d2)/2, width W = |d1-d2|; sense in the view = sign(e)
//     (+ = CCW: inflow -t with angular momentum cross(e*n, -t) = e, n = left normal of t);
//   * radial notches (OD feed grooves): two parallel walls symmetric about a radial line: W = |d1-d2|,
//     bottom radius = along-t position of the inner wall ends;
//   * families = same (type, W, |e|, sign e) at N distinct, equally spaced angles (hatch lines of a section are
//     parallel -> one angle -> rejected).
// Scale: the view is calibrated from the Ø texts around it (Ø value / circle diameter vote), fallback note scale.
// Axial link: caption ("SECTION B-B" / "VIEW B") -> section marker letter on the longitudinal view
// (x = (marker.x - forward face.x) * f) -> notes range "x = a..b" (role / caption letter); face views
// ("FRONT FACE") -> face-relative slots of depth H. Flow direction: notes "FLOW LEFT TO RIGHT", else an
// "ORIFICE" callout left/right of the longitudinal view, else +x.
#include "Notes.h"

namespace pc::drawing::interp {

struct SlotFamily {
  std::string kind;                 // "slot" (tangential) | "groove" (radial notch on the OD, axial groove)
  std::string role;                 // "primary" | "secondary" | ""
  std::string tag;                  // parameter suffix: pri / sec / slt / grv (unique per drawing)
  int count = 0, countGeom = 0;
  double pitchDeg = 0;
  double phaseDeg = 0;              // view angle of instance 0's outward direction t (0..360)
  int senseView = 0;                // slots: +1 CCW / -1 CW as drawn in the view
  int senseText = 0;                // from notes (0 = not given)
  double width = 0, offset = 0;     // part mm (offset > 0)
  double widthGeom = 0, offsetGeom = 0;
  double height = kNaN;             // slot axial height / face-slot depth / groove radial depth (notes or geometry)
  double rIn = 0, rOut = 0;         // slot: wall end radii; groove: bottom radius, OD
  bool throughWall = false;         // slot: inner wall ends on one circle (opens into a bore)
  double uIn = 0;                   // closed slot: along-t position of the inner end (part mm)
  double tangentDia = kNaN;         // "outer wall tangent to Ø.."  -> offset = D/2 - W/2
  std::string tangentName;
  double x0 = kNaN, x1 = kNaN;      // axial extent (part x from the forward face) — through-wall slots
  bool faceRelative = false;        // VIEW .. FRONT FACE: slot cut from the face, depth = height
  bool frontFace = true;
  bool alongBody = false;           // grooves: full length of the body
  std::string axialEvidence;
  std::vector<int> primIds, textIds;
  std::vector<std::string> warnings;
  float conf = 0;
};

struct TransverseView {
  Vec2 centre;                      // sheet mm
  double factor = 1;                // part mm per sheet mm
  std::string scaleEvidence;
  std::vector<double> radii;        // part mm, ascending
  std::string letter, caption;      // "B", "SECTION B-B PRIMARY SWIRLER"
  std::string role;
  bool section = false;
  int partNo = -1;
  double xMarker = kNaN;            // axial station of the section marker (part mm)
  std::vector<SlotFamily> families;
  float conf = 0;
};

struct TransverseResult {
  NoteHints notes;
  int flowSign = 1;                 // +1: flow towards +x (drawing left -> right)
  std::string flowEvidence;
  bool viewedFromDownstream = true;
  double faceXSheet = kNaN;         // forward face (x = 0) of the longitudinal view, sheet mm
  std::vector<TransverseView> views;
  std::vector<std::string> warnings;
};

TransverseResult analyzeTransverse(const PrimitiveSheet& norm);
std::string summary(const SlotFamily& f);   // "8x slot W4 OFS8 CCW x=3..8"

// Pipeline hook (after S5): runs analyzeTransverse on ir.norm, writes "r3.transverse.<tag>" summaries into
// ir.views.attrs and Info diagnostics "r3.transverse" into `r`.
void attachTransverse(DrawingIR& ir, StageResult& r);

}  // namespace pc::drawing::interp
