#pragma once
// WP-2 private: tessdata lookup shared by ocr/Ocr.cpp and ocr/nodeps/ (no dependency on Tesseract).
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace pc::drawing::ocr::detail {

inline std::vector<std::string> splitLangs(const std::string& langs) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : langs) {
    if (c == '+') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

inline std::filesystem::path exeDir() {
  std::error_code ec;
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf).parent_path();
#else
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path();
#endif
  return {};
}

inline bool hasAll(const std::filesystem::path& dir, const std::vector<std::string>& langs) {
  if (dir.empty()) return false;
  std::error_code ec;
  for (const auto& l : langs)
    if (!std::filesystem::exists(dir / (l + ".traineddata"), ec)) return false;
  return !langs.empty();
}

inline std::string resolveTessdataDirImpl(const std::string& requested, const std::string& languages) {
  const auto langs = splitLangs(languages.empty() ? std::string("eng") : languages);
  std::vector<std::filesystem::path> cands;
  if (!requested.empty()) cands.emplace_back(requested);
  const auto ed = exeDir();
  if (!ed.empty()) {
    cands.push_back(ed / ".." / "share" / "persistcad" / "tessdata");
    cands.push_back(ed / "share" / "persistcad" / "tessdata");
    cands.push_back(ed / ".." / ".." / "share" / "persistcad" / "tessdata");
  }
  if (const char* e = std::getenv("TESSDATA_PREFIX"); e && *e) {
    cands.emplace_back(e);
    cands.push_back(std::filesystem::path(e) / "tessdata");
  }
  for (const char* s : {"/usr/share/tesseract-ocr/5/tessdata", "/usr/share/tesseract-ocr/4.00/tessdata",
                        "/usr/share/tessdata", "/usr/local/share/tessdata", "/mingw64/share/tessdata",
                        "/clang64/share/tessdata", "/ucrt64/share/tessdata"})
    cands.emplace_back(s);
  for (const auto& c : cands) {
    if (hasAll(c, langs)) {
      std::error_code ec;
      auto canon = std::filesystem::weakly_canonical(c, ec);
      return (ec ? c : canon).string();
    }
  }
  return {};
}

}  // namespace pc::drawing::ocr::detail
