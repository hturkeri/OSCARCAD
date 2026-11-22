#pragma once
// PersistCAD apppkg — "Save as App": the model + exposed parameters + manufacturing rules packaged
// into a runnable configurator app. OWNER: Agent E. No Qt dependency here.
//
// Two output forms:
//  (a) .pcapp  — JSON package, opened by persistcad_runner <file.pcapp>
//  (b) standalone executable — a copy of the runner binary with the package appended:
//        [runner bytes][payload JSON bytes][uint64 LE payload size]["PCAPP_PAYLOAD_V1" 16 bytes]
//      The runner checks its own executable for the trailer at start-up.
#include "pc/Document.h"
#include <optional>
#include <string>

namespace pc::app {

struct AppDefinition {
  std::string appName = "Parametrik Parça";
  std::string version = "1.0.0";
  std::string author;
  std::string description;
  Document document;                // full model; exposed params flagged in document.params
  bool allowExportSTL = true;
  bool allowExportSTEP = true;
  bool blockOnErrors = true;        // runner refuses to apply a change with Error-level issues
  bool liveUpdate = true;           // runner validates+regenerates on every edit (else on "Uygula")
  bool showAdditiveChecks = true;
  bool showConventionalChecks = true;
};

nlohmann::json toJson(const AppDefinition& a);
AppDefinition appFromJson(const nlohmann::json& j);   // throws std::runtime_error

bool savePackage(const AppDefinition& a, const std::string& path, std::string* err = nullptr);
std::optional<AppDefinition> loadPackage(const std::string& path, std::string* err = nullptr);

// Copy runner executable to outPath, append payload, set executable permission.
bool buildStandaloneApp(const AppDefinition& a, const std::string& runnerExePath,
                        const std::string& outPath, std::string* err = nullptr);

// Read payload appended to an executable (returns nullopt if no trailer).
std::optional<AppDefinition> loadEmbedded(const std::string& exePath, std::string* err = nullptr);

// Sanity checks before saving (no exposed params, invalid ranges, model fails to regenerate...)
std::vector<std::string> lintApp(const AppDefinition& a);

} // namespace pc::app
