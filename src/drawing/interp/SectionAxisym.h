#pragma once
// R2 — axisymmetric SECTION views (SECTION A-A of turned parts / assemblies). Not part of the frozen contract;
// self-contained header (public pc/drawing headers only) so tests can include it directly.
//
//  1. hatch analysis: thin, non axis-parallel line families clipped by profile edges -> hatched faces = material;
//     faces grouped by hatch family (angle + pitch) -> parts (bodies); hatch lines never enter the profile;
//  2. axis: dashed centreline (horizontal or vertical) with the best mirror match of the hatched material; the
//     profile is split at the axis, the half with more material is kept per part (mirror IoU reported);
//  3. dimensions (TextRuns): Ø -> radius from the axis, axial lengths (extension-line pairs), chamfers "a x 45°",
//     included cone angles "90°" -> sketch constraints with named parameters D_.., L_.., C_.., A_..;
//     remaining DOF closed with measured [Ö] values;
//  4. one SketchCandidate per part (purpose kPurpose) -> synth/RevolveMulti builds one body per part.
// Hook: interp/Pipeline.cpp (Stage::Sketch) and synth/Synthesize.cpp. Doc: docs/drawing_import/R2_SECTION_AXISYM.md
#include "pc/drawing/DrawingIR.h"
#include "pc/drawing/Stages.h"
#include <string>
#include <vector>

namespace pc::drawing::r2 {

inline constexpr const char* kPurpose = "revolve-half-profile-part";   // SketchCandidate::purpose of a part profile

struct HatchFamily {
  double angleDeg = 0;              // [0,180)
  double pitch = 0;                 // sheet mm (0 = unknown: single line)
  int lines = 0;
};

struct ProfileSeg {                 // part mm (x = radius, y = axial)
  bool arc = false;
  Vec2 a, b, c;                     // start, end, (arc) centre; arc runs a -> b, CCW if ccw
  double r = 0;
  bool ccw = true;
  int src = -1;                     // norm primitive id
};

struct AxisymPart {
  int index = 0;                    // 0-based, ordered by axial start then radius
  std::string name;                 // from a "(n) NAME" leader label, else "Part<n>"
  HatchFamily family;
  std::vector<int> hatchLineIds;
  double area[2] = {0, 0};          // half-profile area (part mm^2) on side 0 (+normal of axis) / 1
  int side = 0;                     // side used for the profile
  double mirrorIoU = 0;             // side 0 vs mirrored side 1
  std::vector<std::vector<Vec2>> loops;   // closed half-profile loops, part mm: x = radius, y = axial position (arcs sampled)
  std::vector<ProfileSeg> segs;     // the same loops as lines / arcs (sketch entities)
  double pappusVolume = 0;          // 2*pi*sum(r dA) of the loops (mm^3) — independent of OCCT
  int sketchId = -1;
};

struct AxisymSection {
  bool found = false;
  std::string reason;               // why not found (Turkish)
  Box2 box;                         // sheet mm, hatched material of the section
  bool horizontal = true;           // axis direction on the sheet
  int axisPrim = -1;
  Vec2 axisPoint;                   // a point of the axis (sheet)
  Vec2 axisDir{1, 0};               // unit, sheet; axial coordinate grows along it
  Vec2 sideNormal{0, 1};            // side 0 = dot(p - axisPoint, sideNormal) > 0
  double axial0 = 0;                // sheet coordinate (along axisDir) of part x = 0 (front face)
  double scale = 1;                 // part mm per sheet mm
  std::string scaleEvidence;
  double titleScale = 0;            // part/sheet from the title block text ("10:1" -> 0.1), 0 = none
  double mirrorIoU = 0;             // whole section
  std::vector<int> hatchLineIds;    // removed from the profile geometry
  std::vector<int> profilePrimIds;  // visible edges used
  std::vector<AxisymPart> parts;
  std::vector<std::string> notes;

  Vec2 toPart(Vec2 sheet) const {   // -> (radius >= 0, axial)
    Vec2 d = sheet - axisPoint;
    return {std::abs(dot(d, sideNormal)) * scale, dot(d, axisDir) * scale - axial0 * scale};
  }
  Vec2 toPartSigned(Vec2 sheet) const {   // -> (signed radius, axial): side 0 positive
    Vec2 d = sheet - axisPoint;
    return {dot(d, sideNormal) * scale, dot(d, axisDir) * scale - axial0 * scale};
  }
};

// Pure analysis of ir.norm (+ ir.raw lines consumed by S2 hatch regions; + ir.layout title scale if present).
AxisymSection analyzeAxisymSection(const DrawingIR& ir);

// S6 hook: analyze, then append one SketchCandidate (purpose kPurpose) per part to ir.sketches; analysis summary
// in ir.attrs["r2.section.*"]. No-op if no axisymmetric section view is found. Returns number of sketches added.
int addAxisymSectionSketches(DrawingIR& ir, const InterpretOptions& opt, StageResult& res);

}  // namespace pc::drawing::r2
