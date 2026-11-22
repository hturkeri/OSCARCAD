// WP-1 developer tool (disabled test): PC_WP1_DUMP=<file> [PC_WP1_NORM=1] dumps ingest (+normalize) JSON to
// <file>.raw.json / .norm.json.  Run: pc_drawing_tests --gtest_also_run_disabled_tests --gtest_filter=Wp1Tool.*
#include "pc/drawing/Drawing.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>

using namespace pc::drawing;

TEST(Wp1Tool, DISABLED_Dump) {
  const char* f = std::getenv("PC_WP1_DUMP");
  if (!f) GTEST_SKIP();
  const std::string path = f;
  PrimitiveSheet raw; int id = 1; IngestOptions o;
  const bool dxfFile = path.size() > 4 && (path.substr(path.size() - 4) == ".dxf" || path.substr(path.size() - 4) == ".DXF");
  StageResult r = dxfFile ? ingestDxf(path, o, raw, id) : ingestPdfVector(path, o, raw, id);
  std::cout << "ingest: " << toString(r.status) << " " << r.message << "\n";
  for (auto& d : r.diagnostics) std::cout << "  " << d.code << ": " << d.message << "\n";
  std::ofstream(path + ".raw.json") << toJson(raw).dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
  if (std::getenv("PC_WP1_NORM")) {
    PrimitiveSheet n; NormalizeOptions no;
    StageResult rn = normalize(raw, no, n, id);
    std::cout << "normalize: " << toString(rn.status) << " " << rn.message << "\n";
    for (auto& d : rn.diagnostics) std::cout << "  " << d.code << ": " << d.message << "\n";
    std::ofstream(path + ".norm.json") << toJson(n).dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
  }
}
