// pc_drawing_cli — headless "Teknik resimden içe aktar" (WP-3).
//   pc_drawing_cli <in.dxf|pdf|png|pcdrw.json> --out part.pcad --ir ir.pcdrw.json
//                  [--from Stage] [--to Stage] [--strategy N|Revolve|ExtrudeTwoView|...] [--tolmid] [--strict]
//                  [--no-reproject] [--quiet]
// Exit: 0 ok, 1 usage, 2 pipeline failed, 3 nothing to merge / save failed.
#include "pc/drawing/Drawing.h"
#include "pc/Model.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace pc::drawing;

static void usage() {
  std::fprintf(stderr,
               "Kullanım: pc_drawing_cli <girdi> --out parca.pcad [--ir ir.pcdrw.json] [--from Aşama] [--to Aşama]\n"
               "          [--strategy N|Revolve|SectionRevolve|ExtrudeTwoView|ExtrudeSingleView|SketchOnly] [--tolmid]\n"
               "          [--strict] [--no-reproject] [--quiet]\n"
               "Aşamalar: Ingest Normalize Layout Annotate Views Sketch Synthesize\n");
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 1; }
  std::string in, out, irPath, strategy;
  Stage from = Stage::Ingest, to = Stage::Synthesize;
  PipelineOptions opts;
  bool quiet = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { if (i + 1 >= argc) { usage(); std::exit(1); } return argv[++i]; };
    if (a == "--out") out = next();
    else if (a == "--ir") irPath = next();
    else if (a == "--from") { if (!fromString(next(), from)) { usage(); return 1; } }
    else if (a == "--to") { if (!fromString(next(), to)) { usage(); return 1; } }
    else if (a == "--strategy") strategy = next();
    else if (a == "--tolmid") opts.interp.preferTolMid = true;
    else if (a == "--strict") opts.interp.strict = true;
    else if (a == "--no-reproject") opts.interp.verifyByReprojection = false;
    else if (a == "--quiet") quiet = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (in.empty()) in = a;
    else { usage(); return 1; }
  }
  if (in.empty()) { usage(); return 1; }
  DrawingIR ir;
  if (from != Stage::Ingest) {
    std::string err;
    if (!loadIR(ir, irPath.empty() ? in : irPath, &err)) {
      std::fprintf(stderr, "IR okunamadı (%s); baştan çalıştırılıyor\n", err.c_str());
      from = Stage::Ingest;
    }
  }
  ProgressSink ps;
  if (!quiet) ps.progress = [](double f, const std::string& m) { std::fprintf(stderr, "[%3d%%] %s\n", int(f * 100), m.c_str()); };
  StageResult r = runPipeline(in, ir, from, to, opts, &ps);
  auto printDiag = [&] {
    for (auto& d : ir.diagnostics)
      if (d.severity != Severity::Info)
        std::printf("  %s %s: %s\n", d.severity == Severity::Error ? "HATA " : "UYARI", d.code.c_str(), d.message.c_str());
  };
  if (!r.ok()) {
    std::printf("Başarısız (%s): %s\n", toString(r.status), r.message.c_str());
    printDiag();
    if (!irPath.empty()) saveIR(ir, irPath);
    return 2;
  }
  // ---- report
  const auto& L = ir.layout;
  std::printf("Ölçek: %g:%g (%s)", L.title.scale.num, L.title.scale.den, L.title.scale.evidence.c_str());
  if (L.scaleFromDims.conf > 0) std::printf("  ölçülerden %g:%g", L.scaleFromDims.num, L.scaleFromDims.den);
  std::printf("\nProjeksiyon: %s\n", toString(ir.views.projection));
  for (auto& v : L.views)
    std::printf("Görünüş %d %-8s [%.1f,%.1f]-[%.1f,%.1f] %zu ilkel, %s rol=%s %s\n", v.id, v.label.c_str(), v.box.lo.x, v.box.lo.y, v.box.hi.x, v.box.hi.y, v.primIds.size(), v.scale.evidence.c_str(), labelTr(v.role),
                ir.views.rotational.count(v.id) && ir.views.rotational.at(v.id) ? "(döner)" : "");
  for (auto& d : ir.annot.dims)
    std::printf("Ölçü %-14s tür=%-9s ölçülen=%-9.4g fark=%-8.3g %s%s\n", d.text.raw.c_str(), toString(d.kind), d.measuredPart,
                d.residual, d.driving ? "" : "(referans)", d.outOfBand ? " UYUMSUZ" : "");
  for (auto& s : ir.sketches) {
    std::printf("Sketch %d %s DOF=%d sapma=%.3g\n", s.id, s.purpose.c_str(), s.dof, s.maxGeomDeviation);
    for (auto& p : s.params)
      std::printf("   %-8s = %-10s %s\n", p.param.name.c_str(), p.param.expression.c_str(), p.param.description.c_str());
  }
  for (auto& s : ir.synthesis)
    std::printf("Aday %d %-18s puan=%.2f hacim=%.6g izdüşüm=%.3g  %s\n", s.id, toString(s.strategy), s.score, s.volume,
                s.reprojectionError, s.explanation.c_str());
  printDiag();
  if (to != Stage::Synthesize || ir.synthesis.empty()) {
    if (!irPath.empty()) { std::string e; if (!saveIR(ir, irPath, &e)) std::fprintf(stderr, "IR yazılamadı: %s\n", e.c_str()); }
    return ir.synthesis.empty() && to == Stage::Synthesize ? 3 : 0;
  }
  // ---- choose
  const SynthesisCandidate* chosen = &ir.synthesis.front();
  if (!strategy.empty()) {
    Strategy st;
    bool isName = fromString(strategy, st);
    for (size_t i = 0; i < ir.synthesis.size(); ++i)
      if ((isName && ir.synthesis[i].strategy == st) || (!isName && std::to_string(i) == strategy)) { chosen = &ir.synthesis[i]; break; }
  }
  pc::Document doc;
  doc.name = "TeknikResim";
  std::string err;
  auto ids = mergeIntoDocument(*chosen, doc, &err);
  if (ids.empty()) { std::printf("Birleştirme başarısız: %s\n", err.c_str()); return 3; }
  std::printf("Seçilen: %s (%zu özellik)\n", labelTr(chosen->strategy), ids.size());
  if (!out.empty()) {
    if (!pc::saveDocument(doc, out, &err)) { std::printf("Kaydedilemedi: %s\n", err.c_str()); return 3; }
    std::printf("Yazıldı: %s\n", out.c_str());
  }
  if (!irPath.empty()) {
    ir.chosenSynthesis = chosen->id;
    if (!saveIR(ir, irPath, &err)) std::fprintf(stderr, "IR yazılamadı (%s)\n", err.c_str());
  }
  return 0;
}
