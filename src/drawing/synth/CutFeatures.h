#pragma once
// R3 — non-axisymmetric cut features on revolved bodies (tangential slots, OD feed grooves) from transverse views.
// WP-3 private header (not part of the frozen contract).
//
// Per slot/groove family T (tag pri / sec / slt / grv) the document gets named parameters
//   N_T (count), W_T (width), H_T (axial height | face-slot depth | groove radial depth), OFS_T (centreline offset;
//   = DSC_T/2 - W_T/2 when the notes say "outer wall tangent to Ø.."), RI_T / RO_T (wall radii), PHI_T (phase),
//   X0_T / X1_T (axial extent along the revolve axis)
// and the features
//   PL_T  Plane  axisAngle about Z, angle PHI_T, offset ∓OFS_T   (plane through the slot centreline, parallel to Z)
//   SK_T  Sketch OnPlane PL_T: rectangle u = along the slot, v = Z (axial)
//   CUT_T Extrude cut, symmetric, distance W_T, targets = the body that owns the feature
//   PAT_T Pattern circular about Z, count N_T, 360°
// Handedness: the view is taken as seen from downstream; flow sign s (+1 = towards +Z). A view angle a maps to
// a (s=+1) or 180-a (s=-1); the swirl (angular momentum about +Z) is s*senseView; plane offset = -sign*OFS so the
// inflow (-u) has that angular momentum (see R3_TRANSVERSE.md §3).
#include "../interp/Transverse.h"
#include "pc/Document.h"

namespace pc::drawing::synth {

struct CutOptions {
  bool slots = true, grooves = true;
  double margin = 0.5;              // mm: cut tools run this far beyond the OD / faces
  double zOfX0 = interp::kNaN;      // Z of drawing x = 0 (forward face); NaN = min Z of all bodies
};

struct CutReport {
  bool ok = false;                  // all added features regenerated
  std::vector<std::string> features, params, warnings;
  std::map<std::string, std::string> targetOf;   // tag -> body name
};

// Appends params + features to `doc` (which must already hold the revolved bodies, axis Z).
CutReport addTransverseCuts(pc::Document& doc, const interp::TransverseResult& tr, const CutOptions& = {});

// Synthesize hook: analyzeTransverse(ir.norm) and, if it found families, addTransverseCuts on the candidate doc.
// Warnings go to `warnings`, a summary to attrs["r3.cuts"]. Returns the number of features added.
int applyTransverseCuts(const DrawingIR& ir, pc::Document& doc, std::vector<std::string>& warnings, Attrs& attrs);

}  // namespace pc::drawing::synth
