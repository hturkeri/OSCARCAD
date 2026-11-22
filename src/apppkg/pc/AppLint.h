#pragma once
// PersistCAD apppkg — helpers around lintApp() message severity (lintApp returns plain strings,
// each prefixed with "HATA: " (blocking) or "UYARI: " (confirm)).
#include <string>
#include <vector>

namespace pc::app {
inline constexpr const char* kLintError = "HATA: ";
inline constexpr const char* kLintWarning = "UYARI: ";
inline bool lintIsError(const std::string& s) { return s.rfind(kLintError, 0) == 0; }
inline bool lintHasErrors(const std::vector<std::string>& v) {
  for (auto& s : v) if (lintIsError(s)) return true;
  return false;
}
} // namespace pc::app
