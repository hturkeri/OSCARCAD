// Fallback for ocr/ when Tesseract or OpenCV is missing (WP-2). Same private API as ocr/Ocr.cpp, no dependencies.
#include "ocr/Ocr.h"
#include "ocr/OcrCommon.h"

namespace pc::drawing::ocr {

struct Engine::Impl {
  std::string error = "OCR: tesseract olmadan derlendi";
  std::string langs;
};

bool available() { return false; }
std::string engineVersion() { return {}; }
std::string resolveTessdataDir(const std::string& requested, const std::string& languages) {
  return detail::resolveTessdataDirImpl(requested, languages);
}

Engine::Engine(const std::string&, const std::string& languages) : d_(std::make_unique<Impl>()) { d_->langs = languages; }
Engine::~Engine() = default;
bool Engine::ok() const { return false; }
const std::string& Engine::error() const { return d_->error; }
const std::string& Engine::languages() const { return d_->langs; }
Result Engine::recognize(const GrayView&, int, const std::string&, double, int) {
  Result r;
  r.error = d_->error;
  return r;
}

}  // namespace pc::drawing::ocr
