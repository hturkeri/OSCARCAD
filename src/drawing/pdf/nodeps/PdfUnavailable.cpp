// Fallback compiled when libqpdf is NOT available (PC_DRAWING_HAVE_QPDF=0). OWNER WP-1 (keep the signatures in sync
// with the real implementation; never add qpdf includes here).
#include "pc/drawing/Stages.h"

namespace pc::drawing {

StageResult ingestPdfVector(const std::string&, const IngestOptions&, PrimitiveSheet&, int&, ProgressSink*) {
  return StageResult::unavailable("ingestPdfVector", "qpdf");
}
bool extractPdfPageImage(const std::string&, int, RasterImage&, std::string* err) {
  if (err) *err = "extractPdfPageImage: qpdf olmadan derlendi";
  return false;
}

}  // namespace pc::drawing
