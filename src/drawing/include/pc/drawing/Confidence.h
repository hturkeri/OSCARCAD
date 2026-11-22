#pragma once
// PersistCAD drawing import — confidence convention. FROZEN (DrawingIR 1.0).
// Every recognised element carries `conf` in [0,1]:
//   1.0        taken verbatim from a native CAD entity (DXF entity, DIMENSION)
//   0.9 .. <1  exact vector evidence (PDF path, ToUnicode text)
//   0.6 .. 0.9 heuristic (role rules, OCR with grammar support, re-fitted arcs)
//   < 0.6      guess: the wizard shows it red; in strict mode it is never used without user confirmation.
// User-confirmed / user-created elements get conf = 1 (kConfUser).
namespace pc::drawing {

inline constexpr float kConfNative = 1.0f;
inline constexpr float kConfUser = 1.0f;
inline constexpr float kConfExact = 0.9f;      // lower bound of "exact vector evidence"; wizard green  >= this
inline constexpr float kConfHeuristic = 0.6f;  // lower bound of "heuristic";             wizard yellow >= this
// Raster (OCR) dimension confidences are capped: a grammar correction multiplies conf by kConfCorrectionPenalty.
inline constexpr float kConfCorrectionPenalty = 0.8f;

enum class ConfBand { Low /*red*/, Medium /*yellow*/, High /*green*/ };
inline ConfBand confBand(float c) { return c >= kConfExact ? ConfBand::High : (c >= kConfHeuristic ? ConfBand::Medium : ConfBand::Low); }
// strict mode gate (InterpretOptions::strict)
inline bool usableInStrict(float c) { return c >= kConfHeuristic; }

}  // namespace pc::drawing
