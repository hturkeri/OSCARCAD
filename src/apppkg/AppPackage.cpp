#include "pc/AppPackage.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using nlohmann::json;

namespace pc::app {

namespace {
constexpr char kMagic[17] = "PCAPP_PAYLOAD_V1";   // 16 bytes + NUL
constexpr size_t kTrailer = 8 + 16;
constexpr const char* kSchema = "persistcad-app/1";

void setErr(std::string* e, const std::string& m) { if (e) *e = m; }

namespace fs = std::filesystem;
// Paths are UTF-8 everywhere in PersistCAD (Qt/JSON); on Windows they must be widened explicitly.
fs::path u8p(const std::string& s) { return fs::u8path(s); }
long processId() {
#ifdef _WIN32
  return static_cast<long>(::_getpid());
#else
  return static_cast<long>(::getpid());
#endif
}

bool readFile(const std::string& path, std::string& out, std::string* err) {
  std::ifstream f(u8p(path), std::ios::binary);
  if (!f) { setErr(err, "Dosya açılamadı: " + path); return false; }
  std::ostringstream ss; ss << f.rdbuf();
  if (f.bad()) { setErr(err, "Dosya okunamadı: " + path); return false; }
  out = ss.str();
  return true;
}

// Writes `chunks` to `<path>.tmp.<pid>` and renames it over `path`, so an existing file is never
// left truncated/half-written on failure, and a running executable can be replaced (no ETXTBSY).
bool writeFileAtomic(const std::string& path, std::initializer_list<std::pair<const char*, size_t>> chunks,
                     unsigned mode, std::string* err) {
  const std::string tmp = path + ".tmp." + std::to_string(processId());
  {
    std::ofstream f(u8p(tmp), std::ios::binary | std::ios::trunc);
    if (!f) { setErr(err, "Yazılamadı: " + path); return false; }
    for (auto& c : chunks) f.write(c.first, static_cast<std::streamsize>(c.second));
    f.flush();
    if (!f) { f.close(); std::error_code ec; fs::remove(u8p(tmp), ec); setErr(err, "Yazma hatası: " + path); return false; }
  }
  std::error_code ec;
#ifndef _WIN32
  if (::chmod(tmp.c_str(), static_cast<mode_t>(mode)) != 0) { fs::remove(u8p(tmp), ec); setErr(err, "chmod başarısız: " + path); return false; }
#else
  (void)mode;
#endif
  // fs::rename replaces an existing destination on every platform (MoveFileExW REPLACE_EXISTING on Windows).
  fs::rename(u8p(tmp), u8p(path), ec);
  if (ec) { std::error_code ec2; fs::remove(u8p(tmp), ec2); setErr(err, "Yazılamadı (yeniden adlandırma): " + path + " — " + ec.message()); return false; }
  return true;
}

// If `bytes` ends with a valid trailer returns true and the payload range.
bool findPayload(const std::string& bytes, size_t& payloadStart, size_t& payloadSize) {
  if (bytes.size() < kTrailer) return false;
  if (std::memcmp(bytes.data() + bytes.size() - 16, kMagic, 16) != 0) return false;
  uint64_t sz = 0;
  for (int i = 7; i >= 0; --i) sz = (sz << 8) | static_cast<unsigned char>(bytes[bytes.size() - 16 - 8 + i]);
  if (sz > bytes.size() - kTrailer) return false;
  payloadSize = static_cast<size_t>(sz);
  payloadStart = bytes.size() - kTrailer - payloadSize;
  return true;
}
} // namespace

json toJson(const AppDefinition& a) {
  json j;
  j["schema"] = kSchema;
  j["appName"] = a.appName;
  j["version"] = a.version;
  j["author"] = a.author;
  j["description"] = a.description;
  j["options"] = {{"allowExportSTL", a.allowExportSTL},   {"allowExportSTEP", a.allowExportSTEP},
                  {"blockOnErrors", a.blockOnErrors},     {"liveUpdate", a.liveUpdate},
                  {"showAdditiveChecks", a.showAdditiveChecks},
                  {"showConventionalChecks", a.showConventionalChecks}};
  j["document"] = pc::toJson(a.document);
  return j;
}

AppDefinition appFromJson(const json& j) {
  try {
    if (!j.is_object()) throw std::runtime_error("kök nesne değil");
    std::string schema = j.value("schema", std::string());
    if (schema != kSchema) throw std::runtime_error("desteklenmeyen şema: '" + schema + "'");
    AppDefinition a;
    a.appName = j.value("appName", a.appName);
    a.version = j.value("version", a.version);
    a.author = j.value("author", a.author);
    a.description = j.value("description", a.description);
    json o = j.value("options", json::object());
    a.allowExportSTL = o.value("allowExportSTL", a.allowExportSTL);
    a.allowExportSTEP = o.value("allowExportSTEP", a.allowExportSTEP);
    a.blockOnErrors = o.value("blockOnErrors", a.blockOnErrors);
    a.liveUpdate = o.value("liveUpdate", a.liveUpdate);
    a.showAdditiveChecks = o.value("showAdditiveChecks", a.showAdditiveChecks);
    a.showConventionalChecks = o.value("showConventionalChecks", a.showConventionalChecks);
    a.document = documentFromJson(j.at("document"));
    return a;
  } catch (const std::runtime_error&) {
    throw;
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Uygulama tanımı okunamadı: ") + e.what());
  }
}

bool savePackage(const AppDefinition& a, const std::string& path, std::string* err) {
  try {
    const std::string txt = toJson(a).dump(2);   // may throw (invalid UTF-8) before anything is touched
    return writeFileAtomic(path, {{txt.data(), txt.size()}}, 0644, err);
  } catch (const std::exception& e) { setErr(err, e.what()); return false; }
}

std::optional<AppDefinition> loadPackage(const std::string& path, std::string* err) {
  std::string txt;
  if (!readFile(path, txt, err)) return std::nullopt;
  try {
    return appFromJson(json::parse(txt));
  } catch (const std::exception& e) {
    setErr(err, std::string("Paket okunamadı (") + path + "): " + e.what());
    return std::nullopt;
  }
}

bool buildStandaloneApp(const AppDefinition& a, const std::string& runnerExePath,
                        const std::string& outPath, std::string* err) {
  try {
    std::string bytes;
    if (!readFile(runnerExePath, bytes, err)) return false;
    if (bytes.empty()) { setErr(err, "Çalıştırıcı dosyası boş: " + runnerExePath); return false; }
    size_t ps, pz;
    while (findPayload(bytes, ps, pz)) bytes.resize(ps);   // keep original runner bytes only (strip nested payloads)
    std::string payload = toJson(a).dump();
    uint64_t sz = payload.size();
    char le[8];
    for (int i = 0; i < 8; ++i) le[i] = static_cast<char>((sz >> (8 * i)) & 0xFF);
    std::string out = outPath;
#ifdef _WIN32
    if (u8p(out).extension().u8string() != ".exe" && u8p(out).extension().u8string() != ".EXE") out += ".exe";
#endif
    if (!writeFileAtomic(out, {{bytes.data(), bytes.size()}, {payload.data(), payload.size()}, {le, 8}, {kMagic, 16}},
                         0755, err))
      return false;
#ifdef _WIN32
    // Windows has no rpath: the app needs the runner's DLLs and Qt plugin folders beside it.
    // Copy them from the runner's directory (missing files only; failures are not fatal).
    {
      std::error_code ec;
      fs::path src = u8p(runnerExePath).parent_path(), dst = u8p(out).parent_path();
      if (dst.empty()) dst = fs::current_path(ec);
      if (!src.empty() && fs::equivalent(src, dst, ec) == false) {
        for (auto& e : fs::directory_iterator(src, ec)) {
          if (e.is_regular_file(ec) && e.path().extension() == ".dll" && !fs::exists(dst / e.path().filename(), ec))
            fs::copy_file(e.path(), dst / e.path().filename(), ec);
        }
        for (const char* sub : {"platforms", "styles", "imageformats", "iconengines", "generic"}) {
          if (fs::is_directory(src / sub, ec) && !fs::exists(dst / sub, ec))
            fs::copy(src / sub, dst / sub, fs::copy_options::recursive, ec);
        }
      }
    }
#endif
    return true;
  } catch (const std::exception& e) { setErr(err, e.what()); return false; }
}

std::optional<AppDefinition> loadEmbedded(const std::string& exePath, std::string* err) {
  std::ifstream f(u8p(exePath), std::ios::binary);
  if (!f) { setErr(err, "Dosya açılamadı: " + exePath); return std::nullopt; }
  f.seekg(0, std::ios::end);
  std::streamoff total = f.tellg();
  if (total < static_cast<std::streamoff>(kTrailer)) return std::nullopt;
  char tr[kTrailer];
  f.seekg(total - static_cast<std::streamoff>(kTrailer));
  f.read(tr, kTrailer);
  if (!f || std::memcmp(tr + 8, kMagic, 16) != 0) return std::nullopt;   // no payload (not an error)
  uint64_t sz = 0;
  for (int i = 7; i >= 0; --i) sz = (sz << 8) | static_cast<unsigned char>(tr[i]);
  if (sz > static_cast<uint64_t>(total) - kTrailer) { setErr(err, "Gömülü yük boyutu geçersiz"); return std::nullopt; }
  std::string payload(static_cast<size_t>(sz), '\0');
  f.seekg(total - static_cast<std::streamoff>(kTrailer) - static_cast<std::streamoff>(sz));
  f.read(payload.data(), static_cast<std::streamsize>(sz));
  if (!f) { setErr(err, "Gömülü yük okunamadı"); return std::nullopt; }
  try {
    return appFromJson(json::parse(payload));
  } catch (const std::exception& e) {
    setErr(err, std::string("Gömülü yük çözümlenemedi: ") + e.what());
    return std::nullopt;
  }
}

} // namespace pc::app
