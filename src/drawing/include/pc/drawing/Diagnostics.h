#pragma once
// PersistCAD drawing import — results and diagnostics. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Enums.h"
#include <string>
#include <utility>
#include <vector>

namespace pc::drawing {

// One finding. `code` is a stable machine id ("pdf.font.noToUnicode", "dim.outOfBand", "dep.missing.opencv",
// "stub.notImplemented"): prefix = module (sniff, pdf, dxf, norm, raster, ocr, dimtext, layout, annot, views,
// sketch, synth, pipeline, eval, ir). `message` is Turkish, shown in the wizard / report.
struct Diagnostic {
  Severity severity = Severity::Info;
  std::string code;
  std::string message;
  int stage = 0;                    // static_cast<int>(Stage) or 0 = sniff / not stage-bound
  std::vector<int> ids;             // element ids the finding refers to (primitives, texts, dims, views ...)
};

struct StageResult {
  Status status = Status::Failed;
  std::string message;              // Turkish one-liner ("uygulanmadı", "İptal edildi", "OpenCV olmadan derlendi")
  std::vector<Diagnostic> diagnostics;

  bool ok() const { return status == Status::Ok; }
  explicit operator bool() const { return ok(); }

  static StageResult success(std::string msg = {}) { return {Status::Ok, std::move(msg), {}}; }
  static StageResult failure(Status s, std::string msg) { return {s, std::move(msg), {}}; }
  static StageResult notImplemented(const char* what) {
    StageResult r{Status::NotImplemented, std::string(what) + ": uygulanmadı", {}};
    r.diagnostics.push_back({Severity::Error, "stub.notImplemented", r.message, 0, {}});
    return r;
  }
  static StageResult unavailable(const char* what, const char* dependency) {
    StageResult r{Status::Unavailable, std::string(what) + ": " + dependency + " olmadan derlendi", {}};
    r.diagnostics.push_back({Severity::Error, std::string("dep.missing.") + dependency, r.message, 0, {}});
    return r;
  }
  StageResult& warn(std::string code, std::string msg, std::vector<int> ids = {}, int stage = 0) {
    diagnostics.push_back({Severity::Warning, std::move(code), std::move(msg), stage, std::move(ids)});
    return *this;
  }
};

}  // namespace pc::drawing
