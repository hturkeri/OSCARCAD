#pragma once
// PersistCAD drawing import — pipeline driver and document merge [WP-3]. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Stages.h"

namespace pc::drawing {

struct PipelineOptions {
  IngestOptions ingest;
  NormalizeOptions norm;
  InterpretOptions interp;
  bool rasterDefaults = true;       // raster sources: replace norm with NormalizeOptions::forRaster(dpi), strict mode
};

// Dispatch S1 by sniff(path) (vector PDF / DXF / raster file / scanned PDF via extractPdfPageImage or
// ingest.pdfRasterizer). Fills ir.raw (after clearFromStage(ir, Ingest)).
StageResult ingest(const std::string& path, DrawingIR& ir, const PipelineOptions&, ProgressSink* = nullptr);
// Run one stage on `ir` (prerequisite sections must exist) and then applyUserEdits(ir, stage).
StageResult runStage(const std::string& path, DrawingIR& ir, Stage stage, const PipelineOptions&, ProgressSink* = nullptr);
// Runs stages [from, to]: clearFromStage(ir, from), then runStage for each. Stops at the first non-Ok stage and
// returns its result; diagnostics of every stage are appended to ir.diagnostics.
StageResult runPipeline(const std::string& path, DrawingIR& ir, Stage from, Stage to, const PipelineOptions&,
                        ProgressSink* = nullptr);
// Re-apply ir.edits whose stage == `stage` (UserEdit.h catalogue).
StageResult applyUserEdits(DrawingIR& ir, Stage stage);

// Merge a chosen synthesis candidate into an existing document (feature id / parameter name clash resolution:
// suffix "_2", ...). Returns ids of added features; `renamedParams` (optional) receives old -> new names.
// Undo is the caller's (designer) business.
std::vector<std::string> mergeIntoDocument(const SynthesisCandidate& c, pc::Document& doc, std::string* err = nullptr,
                                           std::map<std::string, std::string>* renamedParams = nullptr);

}  // namespace pc::drawing
