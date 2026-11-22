#pragma once
// WP-2 PRIVATE OCR API (not part of the frozen contract). Implemented by ocr/Ocr.cpp (OpenCV + Tesseract) and by
// ocr/nodeps/OcrUnavailable.cpp (same API, reports "unavailable"). Used only by src/drawing/raster/.
// No OpenCV / Tesseract types appear here so that both implementations share it.
#include "pc/drawing/Primitives.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pc::drawing::ocr {

// 8-bit gray view, dark text on light background, rows top-to-bottom.
struct GrayView {
  int width = 0, height = 0;
  std::size_t stride = 0;
  const uint8_t* data = nullptr;
};

struct Result {
  bool ok = false;
  std::string text;                           // best line reading (UTF-8, trimmed)
  float conf = 0;                             // 0..1 (Tesseract mean word confidence / 100)
  std::vector<TextHypothesis> alternatives;   // n-best variants built from per-symbol choices (best first)
  std::string error;
};

bool available();                             // compiled with Tesseract
std::string engineVersion();                  // "tesseract 5.3.4" or ""

// Locate a tessdata directory containing <lang>.traineddata for every language in `languages` ("eng+tur"):
// `requested` (if non-empty), <exe>/../share/persistcad/tessdata, <exe>/share/persistcad/tessdata, $TESSDATA_PREFIX,
// $TESSDATA_PREFIX/tessdata, system locations. Returns "" if none.
std::string resolveTessdataDir(const std::string& requested, const std::string& languages);

// One Tesseract instance (not thread-safe; use one per thread). LSTM engine.
class Engine {
 public:
  Engine(const std::string& tessdataDir, const std::string& languages);
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  bool ok() const;
  const std::string& error() const;
  const std::string& languages() const;
  // psm: Tesseract page segmentation mode (7 = single line, 8 = single word, 6 = block, 11 = sparse).
  // whitelist: "" = none. dpi: source resolution hint.
  Result recognize(const GrayView& img, int psm, const std::string& whitelist, double dpi, int maxAlternatives = 4);

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace pc::drawing::ocr
