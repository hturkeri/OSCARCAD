#pragma once
// WP-3 test helpers: fixture paths, expectations ("wp3Expect" block of the hand-made IR fixtures), pipeline runner.
#include "pc/drawing/Drawing.h"
#include "pc/Model.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace wp3 {

inline std::string fixture(const std::string& name) {
  return std::string(PC_DRAWING_TEST_DIR) + "/fixtures/wp3/" + name + ".pcdrw.json";
}

inline nlohmann::json expect(const std::string& name) {
  std::ifstream f(fixture(name));
  nlohmann::json j = nlohmann::json::parse(f);
  return j.value("wp3Expect", nlohmann::json::object());
}

inline pc::drawing::StageResult run(const std::string& name, pc::drawing::DrawingIR& ir,
                                    pc::drawing::PipelineOptions opts = {},
                                    pc::drawing::Stage from = pc::drawing::Stage::Ingest,
                                    pc::drawing::Stage to = pc::drawing::Stage::Synthesize) {
  return pc::drawing::runPipeline(fixture(name), ir, from, to, opts);
}

inline const pc::drawing::SynthesisCandidate* chosen(const pc::drawing::DrawingIR& ir) {
  for (auto& s : ir.synthesis) if (s.id == ir.chosenSynthesis) return &s;
  return ir.synthesis.empty() ? nullptr : &ir.synthesis.front();
}

inline const pc::drawing::ParamProposal* param(const pc::drawing::DrawingIR& ir, const std::string& n) {
  for (auto& c : ir.sketches) for (auto& p : c.params) if (p.param.name == n) return &p;
  return nullptr;
}

inline double paramValue(const pc::Document& d, const std::string& n) {
  auto v = d.params.evaluate();
  return v && v->count(n) ? v->at(n) : -1;
}

}  // namespace wp3
