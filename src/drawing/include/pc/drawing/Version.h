#pragma once
// PersistCAD drawing import — contract version. FROZEN (P0, "DrawingIR 1.0").
// Change requests: docs/CONTRACT_REQUESTS.md, prefix "DRW:". A change that alters the JSON layout bumps the minor
// (additive) or major (breaking) number AND the schema id below.
#define PC_DRAWING_IR_VERSION_MAJOR 1
#define PC_DRAWING_IR_VERSION_MINOR 0
#define PC_DRAWING_IR_VERSION_STRING "DrawingIR 1.0"

namespace pc::drawing {
inline constexpr const char* kDrawingIRVersion = PC_DRAWING_IR_VERSION_STRING;  // human readable
inline constexpr const char* kIRVersion = "1.0";                                // DrawingIR::irVersion / JSON "irVersion"
inline constexpr const char* kSchemaId = "pcdrw/1";                             // DrawingIR::schema  / JSON "schema"
inline constexpr const char* kIRFileSuffix = ".pcdrw.json";                    // side-car file next to the drawing
}  // namespace pc::drawing
