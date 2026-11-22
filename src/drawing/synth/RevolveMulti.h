#pragma once
// R2 — S7 multi-body revolve of an axisymmetric section: one Sketch + Revolve(360°, op=new, bodyName) per part
// profile produced by interp/SectionAxisym (SketchCandidate::purpose == r2::kPurpose), regenerated with pc::model
// (BODIES contract: every part a separate body), verified by
//   * HLR re-projection of the half model (cut by the section plane) against the section's visible edges, and
//   * per-body volume vs the Pappus volume of the drawn half profile (independent of OCCT).
// Self-contained header (public headers only). Hook: synth/Synthesize.cpp. Doc: docs/drawing_import/R2_SECTION_AXISYM.md
#include "pc/drawing/DrawingIR.h"
#include "pc/drawing/Stages.h"

namespace pc::drawing::r2 {

// Appends one SynthesisCandidate (strategy SectionRevolve, attrs "r2.*") if ir.sketches holds part profiles;
// existing single-profile Revolve/SectionRevolve candidates of the same view are demoted (score x0.5) when the
// multi-body candidate regenerates. Returns the candidate id or -1. Does not sort ir.synthesis.
int addRevolveMultiCandidate(DrawingIR& ir, const InterpretOptions& opt, StageResult& res);

}  // namespace pc::drawing::r2
