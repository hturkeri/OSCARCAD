#pragma once
// R3 — drawing notes / view captions -> structured feature hints (robust to OCR noise). WP-3 private header.
//
// parseNoteText() works on ONE text (a note line, a view caption, a callout) and returns every hint it finds:
//   "8x TANGENTIAL SLOTS 4 x 5, EQ. SP. 45° - CCW SWIRL"  -> FeatureHint{slot, count 8, w 4, h 5, pitch 45, CCW}
//   "CUT 10 SECONDARY SLOTS x = 10..16"                     -> FeatureHint{slot, secondary, count 10, x 10..16}
//   "3 SLOTS 0.5 x 0.5, Ap = ...; GROOVES 0.6 x 0.5"       -> two FeatureHints (slot, groove)
//   "SLOT CENTRELINE OFFSET FROM AXIS: PRIMARY 8, SECONDARY 11" -> offsets per role
//   "SCALE 2:1", "FLOW LEFT TO RIGHT", "Ø14 THROAT", "swirl chamber Ø3.2", "SECTION B-B PRIMARY SWIRLER",
//   "VIEW B - SWIRLER (2) FRONT FACE", "SLOT OUTER WALL TANGENT TO SWIRL CHAMBER", "MODELLING: (a) ... (b) ..."
// Numbers go through the WP-2 dimension grammar (parseDimText, fromOcr) so "014", "45o", "4 X 5" are repaired;
// every repair lowers the confidence (kConfCorrectionPenalty).
// parseNotes() runs it over all texts of a sheet and merges the hints (by kind + role).
#include "pc/drawing/DrawingIR.h"
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace pc::drawing::interp {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline bool has(double v) { return !std::isnan(v); }

struct FeatureHint {
  std::string kind;                 // "slot" | "groove" | "passage" | "hole"
  std::string role;                 // "primary" | "secondary" | "" (none given)
  int count = 0;                    // 0 = not given
  double width = kNaN, height = kNaN;   // "4 x 5": first = width (across the slot), second = height/depth
  double pitchDeg = kNaN;           // "EQ. SP. 45°"
  int sense = 0;                    // +1 CCW, -1 CW (as written; "viewed from downstream" convention)
  bool tangential = false;
  double offset = kNaN;             // slot centreline offset from the axis
  double x0 = kNaN, x1 = kNaN;      // axial extent "x = 3..8"
  bool cut = false;                 // "CUT ..." (modelling instruction)
  std::string viewLetter;           // caption the hint was written under ("B"), "" for general notes
  float conf = 0;
  std::vector<int> textIds;
  std::vector<std::string> corrections;
};

struct ViewCaption {                // "SECTION B-B PRIMARY SWIRLER", "VIEW B - SWIRLER (2) FRONT FACE"
  std::string letter;               // "B"
  bool section = false;
  std::string role;                 // primary / secondary / ""
  std::string subject;              // "PRIMARY SWIRLER", "SWIRLER (2) FRONT FACE"
  int partNo = -1;                  // "(2)"
  bool frontFace = false, rearFace = false;
  int textId = -1;
  Vec2 pos;                         // text centre, sheet mm
};

struct NoteHints {
  std::vector<FeatureHint> features;          // merged by (kind, role)
  std::vector<ViewCaption> captions;
  double scaleNum = 0, scaleDen = 0;          // "SCALE 2:1" (0 = none)
  int flowSign = 0;                           // +1 left->right (+x), -1 right->left, 0 unknown
  bool viewedFromDownstream = false;
  std::map<std::string, double> namedDiameters;   // "THROAT" -> 14, "SWIRL CHAMBER" -> 3.2
  std::string tangentOuterWallTo;             // "SWIRL CHAMBER" (slot outer wall tangent to that circle)
  std::vector<std::string> modellingSteps;    // "REVOLVE HALF PROFILE 360°", "CUT 8 PRIMARY SLOTS X = 3..8"
  float conf = 0;
};

// Upper-case, Turkish fold, OCR-noise canonical form used by the parser (exposed for tests).
// Ø/⌀ -> '@', ° -> '~', × -> 'X'; O/o/l/I/S/B between digits -> digit; ',' between digits -> '.'; ". ." -> "..".
std::string canonNote(const std::string& utf8, std::vector<std::string>* corrections = nullptr);

void parseNoteText(const std::string& utf8, int textId, float textConf, NoteHints& out);
NoteHints parseNotes(const PrimitiveSheet& s);
// Merge `in` into `out` (same kind and role, or role-less hint into the only family of that kind).
void mergeHint(std::vector<FeatureHint>& out, const FeatureHint& in);
const FeatureHint* findHint(const NoteHints& h, const std::string& kind, const std::string& role);

}  // namespace pc::drawing::interp
