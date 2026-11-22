// Fallback compiled when OpenCV is NOT available (PC_DRAWING_HAVE_OPENCV=0). OWNER WP-2 (keep in sync; no OpenCV).
#include "pc/drawing/Stages.h"

namespace pc::drawing {

bool loadRasterFile(const std::string&, RasterImage&, std::string* err) {
  if (err) *err = "loadRasterFile: opencv olmadan derlendi";
  return false;
}
StageResult ingestRaster(const RasterImage&, const IngestOptions&, PrimitiveSheet&, int&, ProgressSink*) {
  return StageResult::unavailable("ingestRaster", "opencv");
}

}  // namespace pc::drawing
