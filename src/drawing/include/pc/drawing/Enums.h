#pragma once
// PersistCAD drawing import — every enum of the contract + its canonical string names. FROZEN (DrawingIR 1.0).
// The string names are the JSON representation (pcdrw/1) and the UserEdit payload representation. They are the
// C++ enumerator spellings. Never reorder or rename; new enumerators may only be APPENDED (minor version bump).
#include <cstddef>
#include <string>
#include <string_view>

namespace pc::drawing {

enum class SourceKind { VectorPdf, ScannedPdf, Dxf, RasterImage, Photo };

enum class PrimKind {
  Line,         // p0, p1
  Arc,          // center, radius, a0, a1
  Circle,       // center, radius
  Ellipse,      // center, radius (major), minorRatio, rotDeg, a0, a1 (iso views / DXF; sketch module has no ellipse)
  Polyline,     // pts (open/closed) — raw flattened curves before arc refit
  Bezier,       // pts = 1 + 3*k control points (cubic chain: P0 C1 C2 P1 C1 C2 P2 ...)
  Arrowhead,    // tip = p0, base centre = p1, direction = unit(p1 - p0) points from tip INTO the arrow body,
                // size = |p1-p0|; style.filled = filled triangle, false = open (two strokes)
  HatchRegion,  // pts = boundary polygon (closed); hatchAngleDeg, hatchPitch (section material)
  Image         // pts = 4 corners of a placed bitmap (CCW from the image's lower-left)
};

enum class ProjectionMethod { Unknown, FirstAngle /*ISO E, TS/ISO 128*/, ThirdAngle /*ISO A, US*/ };

enum class ViewRole { Unknown, Front, Top, Bottom, Left, Right, Rear, Section, Detail, Auxiliary, Isometric, Ignore };

enum class LineRole { Unknown, Visible, Hidden, Center, Dimension, Extension, Leader, Hatch, Cutting, Thread,
                      Border, TitleBlock, Symbol, Phantom };

enum class DimKind { Linear, Aligned, Diameter, Radius, Angle, Chamfer, Thread, Ordinate, Count, Note, Unknown };

// DXF DIMENSION type (group 70 & 7): 0 Linear(rotated), 1 Aligned, 2 Angular, 3 Diameter, 4 Radius, 5 Angular3P,
// 6 Ordinate. ArcLength = ARC_DIMENSION entity.
enum class NativeDimKind { Linear, Aligned, Angular, Diameter, Radius, Angular3P, Ordinate, ArcLength, Unknown };

enum class Adjacency { None, RightOf, LeftOf, Above, Below };   // relative sheet placement of view b w.r.t. view a

enum class ParamTag { FromDimension /*[D]*/, Measured /*[Ö]*/, Assumed /*[V]*/, Derived /*[T]*/ };

enum class Strategy { Revolve, ExtrudeTwoView, ExtrudeSingleView /*depth from text, e.g. "t=5"*/, SectionRevolve,
                      MultiFeature, SketchOnly };

// Pipeline stages. Sniff (S0) is not a stage of the IR (it has no output section); Ingest = S1 ... Synthesize = S7.
enum class Stage { Ingest = 1, Normalize, Layout, Annotate, Views, Sketch, Synthesize };
inline constexpr Stage kFirstStage = Stage::Ingest;
inline constexpr Stage kLastStage = Stage::Synthesize;

enum class Severity { Info, Warning, Error };

// Outcome of a stage / API call.
enum class Status {
  Ok,              // done (may still carry warnings)
  NotImplemented,  // P0 stub — message "uygulanmadı"
  Unavailable,     // optional dependency not compiled in (e.g. built without OpenCV) — message names the dependency
  Cancelled,       // ProgressSink::cancelled() returned true — message "İptal edildi"
  InvalidInput,    // unreadable / unsupported file, missing prerequisite section in the IR
  Failed           // anything else
};

// ----------------------------------------------------------------------------------------- names (JSON strings)
template <class E> struct EnumNames;   // specialised below: names[], count, first (underlying value of names[0])

#define PC_DRAWING_ENUM_NAMES(E, FIRST, ...)                                           \
  template <> struct EnumNames<E> {                                                    \
    static constexpr const char* names[] = {__VA_ARGS__};                              \
    static constexpr std::size_t count = sizeof(names) / sizeof(names[0]);             \
    static constexpr int first = FIRST;                                                \
  };
PC_DRAWING_ENUM_NAMES(SourceKind, 0, "VectorPdf", "ScannedPdf", "Dxf", "RasterImage", "Photo")
PC_DRAWING_ENUM_NAMES(PrimKind, 0, "Line", "Arc", "Circle", "Ellipse", "Polyline", "Bezier", "Arrowhead", "HatchRegion", "Image")
PC_DRAWING_ENUM_NAMES(ProjectionMethod, 0, "Unknown", "FirstAngle", "ThirdAngle")
PC_DRAWING_ENUM_NAMES(ViewRole, 0, "Unknown", "Front", "Top", "Bottom", "Left", "Right", "Rear", "Section", "Detail",
                      "Auxiliary", "Isometric", "Ignore")
PC_DRAWING_ENUM_NAMES(LineRole, 0, "Unknown", "Visible", "Hidden", "Center", "Dimension", "Extension", "Leader", "Hatch",
                      "Cutting", "Thread", "Border", "TitleBlock", "Symbol", "Phantom")
PC_DRAWING_ENUM_NAMES(DimKind, 0, "Linear", "Aligned", "Diameter", "Radius", "Angle", "Chamfer", "Thread", "Ordinate",
                      "Count", "Note", "Unknown")
PC_DRAWING_ENUM_NAMES(NativeDimKind, 0, "Linear", "Aligned", "Angular", "Diameter", "Radius", "Angular3P", "Ordinate",
                      "ArcLength", "Unknown")
PC_DRAWING_ENUM_NAMES(Adjacency, 0, "None", "RightOf", "LeftOf", "Above", "Below")
PC_DRAWING_ENUM_NAMES(ParamTag, 0, "FromDimension", "Measured", "Assumed", "Derived")
PC_DRAWING_ENUM_NAMES(Strategy, 0, "Revolve", "ExtrudeTwoView", "ExtrudeSingleView", "SectionRevolve", "MultiFeature",
                      "SketchOnly")
PC_DRAWING_ENUM_NAMES(Stage, 1, "Ingest", "Normalize", "Layout", "Annotate", "Views", "Sketch", "Synthesize")
PC_DRAWING_ENUM_NAMES(Severity, 0, "Info", "Warning", "Error")
PC_DRAWING_ENUM_NAMES(Status, 0, "Ok", "NotImplemented", "Unavailable", "Cancelled", "InvalidInput", "Failed")
#undef PC_DRAWING_ENUM_NAMES

// "?" for out-of-range values.
template <class E> inline const char* toString(E e) {
  const int i = static_cast<int>(e) - EnumNames<E>::first;
  return (i >= 0 && static_cast<std::size_t>(i) < EnumNames<E>::count) ? EnumNames<E>::names[i] : "?";
}
// Exact (case-sensitive) match; returns false and leaves `out` untouched if unknown.
template <class E> inline bool fromString(std::string_view s, E& out) {
  for (std::size_t i = 0; i < EnumNames<E>::count; ++i)
    if (s == EnumNames<E>::names[i]) { out = static_cast<E>(static_cast<int>(i) + EnumNames<E>::first); return true; }
  return false;
}

// Short Turkish labels for the wizard (ViewRole / LineRole / Strategy); defined in core/Enums.cpp (P0).
const char* labelTr(ViewRole r);
const char* labelTr(LineRole r);
const char* labelTr(Strategy s);
const char* labelTr(Stage s);

}  // namespace pc::drawing
