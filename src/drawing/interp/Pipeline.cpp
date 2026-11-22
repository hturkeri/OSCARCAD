// Pipeline driver: S1 dispatch (sniff / extension), stage runner with user-edit replay, full pipeline.
#include "interp/InterpUtil.h"
#include "interp/SectionAxisym.h"   // R2 hook
#include "interp/Transverse.h"   // R3 hook
#include "pc/drawing/Features.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>

namespace pc::drawing {
using namespace interp;

namespace {

std::string lowerExt(const std::string& path) {
  auto dot = path.find_last_of('.');
  std::string e = dot == std::string::npos ? "" : path.substr(dot + 1);
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return e;
}
bool endsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && std::equal(suf.rbegin(), suf.rend(), s.rbegin(), [](char a, char b) {
           return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
         });
}

// Ingest-stage edits (setSourceKind / setPage / setDpi) are applied to the options BEFORE ingest.
PipelineOptions withIngestEdits(const DrawingIR& ir, PipelineOptions o, SourceKind* forcedKind) {
  for (auto& e : ir.edits) {
    if (e.stage != Stage::Ingest) continue;
    try {
      auto j = nlohmann::json::parse(e.payload);
      if (e.op == edit_op::setPage) o.ingest.page = j.value("page", o.ingest.page);
      else if (e.op == edit_op::setDpi) o.ingest.rasterDpiOverride = j.value("dpi", 0.0);
      else if (e.op == edit_op::setSourceKind && forcedKind) {
        SourceKind k;
        if (fromString(j.value("kind", std::string()), k)) *forcedKind = k;
      }
    } catch (...) {}
  }
  return o;
}

}  // namespace

StageResult ingest(const std::string& path, DrawingIR& ir, const PipelineOptions& opts0, ProgressSink* prog) {
  SourceKind forced = static_cast<SourceKind>(-1);
  PipelineOptions opts = withIngestEdits(ir, opts0, &forced);
  clearFromStage(ir, Stage::Ingest);
  report(prog, 0.0, "Dosya okunuyor");
  // ---- DrawingIR side-car / fixture (*.pcdrw.json): S1+S2 come from the file
  if (endsWith(path, kIRFileSuffix) || lowerExt(path) == "json") {
    DrawingIR loaded;
    std::string err;
    if (!loadIR(loaded, path, &err)) return StageResult::failure(Status::InvalidInput, "IR okunamadı: " + err);
    ir.raw = loaded.raw.prims.empty() && loaded.raw.texts.empty() ? loaded.norm : loaded.raw;
    ir.norm = loaded.norm;
    ir.nextId = std::max(ir.nextId, loaded.nextId);
    if (ir.edits.empty()) ir.edits = loaded.edits;
    for (auto& [k, v] : loaded.attrs) if (!ir.attrs.count(k)) ir.attrs[k] = v;
    ir.attrs["interp.irInput"] = ir.norm.prims.empty() ? "raw" : "norm";
    StageResult r = StageResult::success("IR dosyası okundu (" + std::to_string(ir.raw.prims.size()) + " ilkel)");
    return r;
  }
  ir.attrs.erase("interp.irInput");
  SniffResult sn = sniff(path);
  SourceKind kind;
  std::string ext = lowerExt(path);
  if (sn.result.ok()) kind = sn.kind;
  else if (ext == "dxf") kind = SourceKind::Dxf;
  else if (ext == "pdf") kind = SourceKind::VectorPdf;
  else if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tif" || ext == "tiff" || ext == "bmp") kind = SourceKind::RasterImage;
  else return StageResult::failure(Status::InvalidInput, "Desteklenmeyen dosya türü: " + path);
  if (static_cast<int>(forced) >= 0) kind = forced;
  StageResult r;
  int nextId = ir.nextId;
  switch (kind) {
    case SourceKind::Dxf: r = ingestDxf(path, opts.ingest, ir.raw, nextId, prog); break;
    case SourceKind::VectorPdf: r = ingestPdfVector(path, opts.ingest, ir.raw, nextId, prog); break;
    case SourceKind::ScannedPdf: {
      RasterImage img;
      std::string err;
      bool ok = extractPdfPageImage(path, opts.ingest.page, img, &err);
      if (!ok && opts.ingest.pdfRasterizer) ok = opts.ingest.pdfRasterizer(path, opts.ingest.page, opts.ingest.pdfRenderDpi, img, &err);
      if (!ok) { r = StageResult::failure(Status::Unavailable, "Taranmış PDF sayfası görüntüye çevrilemedi: " + err); break; }
      r = ingestRaster(img, opts.ingest, ir.raw, nextId, prog);
      break;
    }
    case SourceKind::RasterImage: case SourceKind::Photo: {
      RasterImage img;
      std::string err;
      if (!loadRasterFile(path, img, &err)) {
        r = buildFeatures().opencv ? StageResult::failure(Status::InvalidInput, "Görüntü okunamadı: " + err)
                                   : StageResult::unavailable("Görüntü okuma", "opencv");
        break;
      }
      IngestOptions io = opts.ingest;
      io.rasterIsPhoto = io.rasterIsPhoto || kind == SourceKind::Photo;
      r = ingestRaster(img, io, ir.raw, nextId, prog);
      break;
    }
  }
  ir.nextId = std::max(ir.nextId, nextId);
  if (r.ok() && ir.raw.file.empty()) ir.raw.file = path;
  return r;
}

StageResult runStage(const std::string& path, DrawingIR& ir, Stage stage, const PipelineOptions& opts0, ProgressSink* prog) {
  PipelineOptions opts = opts0;
  const bool raster = ir.raw.source == SourceKind::RasterImage || ir.raw.source == SourceKind::ScannedPdf ||
                      ir.raw.source == SourceKind::Photo;
  if (raster && opts.rasterDefaults) {
    double dpi = ir.raw.raster ? ir.raw.raster->dpi : 300;
    opts.norm = NormalizeOptions::forRaster(dpi);
    opts.interp.strict = true;
    opts.interp.measuredRounding = std::max(0.1, opts.interp.measuredRounding);
  }
  StageResult r;
  try {
    switch (stage) {
      case Stage::Ingest: r = ingest(path, ir, opts, prog); break;
      case Stage::Normalize: {
        if (ir.attrs.count("interp.irInput") && ir.attrs.at("interp.irInput") == "norm" && !ir.norm.prims.empty()) {
          r = StageResult::success("Normalize edilmiş IR dosyadan alındı");
          break;
        }
        ir.norm = {};
        int nextId = ir.nextId;
        r = normalize(ir.raw, opts.norm, ir.norm, nextId, prog);
        ir.nextId = std::max(ir.nextId, nextId);
        break;
      }
      case Stage::Layout: r = analyzeLayout(ir, opts.interp, prog); break;
      case Stage::Annotate: r = annotate(ir, opts.interp, prog); break;
      case Stage::Views:
        r = buildViews(ir, opts.interp, prog);
        if (r.ok()) attachTransverse(ir, r);   // R3 hook: transverse-view slot/groove families -> views.attrs
        break;
      case Stage::Sketch: {
        r = reconstructSketches(ir, opts.interp, prog);
        // R2 hook (docs/drawing_import/R2_SECTION_AXISYM.md): axisymmetric SECTION view (hatch + centreline) ->
        // one half-profile sketch per hatch family (part); runs even if the generic S6 found nothing.
        StageResult r2s = StageResult::success("Eksenel simetrik kesit: parça profilleri");
        if (r2::addAxisymSectionSketches(ir, opts.interp, r2s) > 0 && !r.ok()) r = r2s;
        else r.diagnostics.insert(r.diagnostics.end(), r2s.diagnostics.begin(), r2s.diagnostics.end());
        break;
      }
      case Stage::Synthesize: r = synthesize(ir, opts.interp, prog); break;
    }
  } catch (const std::exception& e) {
    r = StageResult::failure(Status::Failed, std::string(labelTr(stage)) + ": " + e.what());
  }
  for (auto& d : r.diagnostics) if (d.stage == 0) d.stage = static_cast<int>(stage);
  if (r.ok()) {
    StageResult e = applyUserEdits(ir, stage);
    for (auto& d : e.diagnostics) r.diagnostics.push_back(d);
    ir.lastStage = std::max(ir.lastStage, static_cast<int>(stage));
    ir.stageVersion[stage] = libraryVersion() + " / wp3 " + toString(stage) + " v1";
  }
  ir.diagnostics.insert(ir.diagnostics.end(), r.diagnostics.begin(), r.diagnostics.end());
  return r;
}

StageResult runPipeline(const std::string& path, DrawingIR& ir, Stage from, Stage to, const PipelineOptions& opts,
                        ProgressSink* prog) {
  clearFromStage(ir, from);
  // drop diagnostics of the stages being re-run
  ir.diagnostics.erase(std::remove_if(ir.diagnostics.begin(), ir.diagnostics.end(),
                                      [&](const Diagnostic& d) { return d.stage >= static_cast<int>(from); }),
                       ir.diagnostics.end());
  StageResult last = StageResult::success();
  const int a = static_cast<int>(from), b = static_cast<int>(to);
  for (int s = a; s <= b; ++s) {
    if (isCancelled(prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    ProgressSink sub;
    if (prog) {
      sub.cancelled = prog->cancelled;
      if (prog->progress)
        sub.progress = [prog, s, a, b](double f, const std::string& m) {
          prog->progress((s - a + f) / double(b - a + 1), std::string(labelTr(static_cast<Stage>(s))) + ": " + m);
        };
    }
    StageResult r = runStage(path, ir, static_cast<Stage>(s), opts, prog ? &sub : nullptr);
    if (!r.ok()) return r;
    last = r;
  }
  return last;
}

}  // namespace pc::drawing
