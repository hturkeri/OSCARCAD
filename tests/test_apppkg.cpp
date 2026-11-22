#include <filesystem>
#include "pc/AppLint.h"
#include "pc/AppPackage.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace pc;
using namespace pc::app;

namespace {
std::string tmp(const std::string& n);
// On Windows buildStandaloneApp always writes "*.exe"; give outputs that extension up front.
std::string tmpExe(const std::string& n) {
#ifdef _WIN32
  return tmp(n) + ".exe";
#else
  return tmp(n);
#endif
}
std::string tmp(const std::string& n) { return (std::filesystem::temp_directory_path().u8string() + "/test_apppkg_") + std::to_string(getpid()) + "_" + n; }

AppDefinition bracketApp() {
  AppDefinition a;
  std::string err;
  EXPECT_TRUE(loadDocument(a.document, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  a.appName = "Braket Konfigüratör";
  a.version = "2.3.1";
  a.author = "Test";
  a.description = "Açıklama ğüşiöç";
  a.allowExportSTEP = false;
  a.liveUpdate = false;
  a.blockOnErrors = false;
  a.showConventionalChecks = false;
  return a;
}
void writeBytes(const std::string& p, const std::string& b) { std::ofstream f(p, std::ios::binary); f << b; }
std::string readBytes(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream s; s << f.rdbuf(); return s.str(); }
bool hasMsg(const std::vector<std::string>& v, const std::string& needle, bool error) {
  for (auto& s : v) if (s.find(needle) != std::string::npos && lintIsError(s) == error) return true;
  return false;
}
} // namespace

TEST(AppPkg, JsonRoundTrip) {
  auto a = bracketApp();
  auto j = toJson(a);
  EXPECT_EQ(j["schema"], "persistcad-app/1");
  auto b = appFromJson(j);
  EXPECT_EQ(b.appName, a.appName);
  EXPECT_EQ(b.version, "2.3.1");
  EXPECT_EQ(b.description, a.description);
  EXPECT_FALSE(b.allowExportSTEP);
  EXPECT_TRUE(b.allowExportSTL);
  EXPECT_FALSE(b.liveUpdate);
  EXPECT_FALSE(b.blockOnErrors);
  EXPECT_FALSE(b.showConventionalChecks);
  EXPECT_TRUE(b.showAdditiveChecks);
  EXPECT_EQ(toJson(b), j);   // stable
  EXPECT_EQ(b.document.params.all().size(), a.document.params.all().size());
  EXPECT_TRUE(b.document.params.find("W")->exposed);
}

TEST(AppPkg, BadSchemaThrows) {
  auto j = toJson(bracketApp());
  j["schema"] = "other/9";
  EXPECT_THROW(appFromJson(j), std::runtime_error);
  EXPECT_THROW(appFromJson(nlohmann::json::array()), std::runtime_error);
  nlohmann::json k = {{"schema", "persistcad-app/1"}};
  EXPECT_THROW(appFromJson(k), std::runtime_error);
}

TEST(AppPkg, PackageSaveLoad) {
  auto a = bracketApp();
  std::string p = tmp("x.pcapp"), err;
  ASSERT_TRUE(savePackage(a, p, &err)) << err;
  auto b = loadPackage(p, &err);
  ASSERT_TRUE(b.has_value()) << err;
  EXPECT_EQ(toJson(*b), toJson(a));
  EXPECT_FALSE(loadPackage("/nonexistent/zzz.pcapp", &err).has_value());
  EXPECT_FALSE(err.empty());
  writeBytes(p, "not json");
  EXPECT_FALSE(loadPackage(p, &err).has_value());
  std::remove(p.c_str());
  EXPECT_FALSE(savePackage(a, "/nonexistent_dir/a.pcapp", &err));
}

TEST(AppPkg, StandaloneRoundTrip) {
  auto a = bracketApp();
  std::string runner = tmp("runner.bin"), out = tmpExe("app.bin"), err;
  std::string fake = std::string("\x7f" "ELF-dummy-binary", 16) + std::string(1000, '\x01');
  writeBytes(runner, fake);
  EXPECT_FALSE(loadEmbedded(runner, &err).has_value());   // no payload
  ASSERT_TRUE(buildStandaloneApp(a, runner, out, &err)) << err;
  struct stat st{};
  ASSERT_EQ(stat(out.c_str(), &st), 0);
#ifndef _WIN32
  EXPECT_TRUE(st.st_mode & S_IXUSR);
#endif
  std::string bytes = readBytes(out);
  EXPECT_EQ(bytes.substr(0, fake.size()), fake);
  EXPECT_EQ(bytes.substr(bytes.size() - 16), "PCAPP_PAYLOAD_V1");
  auto b = loadEmbedded(out, &err);
  ASSERT_TRUE(b.has_value()) << err;
  EXPECT_EQ(toJson(*b), toJson(a));
  std::remove(runner.c_str());
  std::remove(out.c_str());
}

TEST(AppPkg, RebuildFromEmbeddedKeepsSinglePayload) {
  auto a = bracketApp();
  std::string runner = tmp("r2.bin"), o1 = tmpExe("a1.bin"), o2 = tmpExe("a2.bin"), err;
  std::string fake(500, 'Q');
  writeBytes(runner, fake);
  ASSERT_TRUE(buildStandaloneApp(a, runner, o1, &err)) << err;
  auto a2 = a;
  a2.appName = "İkinci";
  ASSERT_TRUE(buildStandaloneApp(a2, o1, o2, &err)) << err;
  std::string bytes = readBytes(o2);
  EXPECT_EQ(bytes.substr(0, fake.size()), fake);
  // Exactly one trailer: size = runner + payload + 24
  std::string payload = toJson(a2).dump();
  EXPECT_EQ(bytes.size(), fake.size() + payload.size() + 24);
  auto b = loadEmbedded(o2, &err);
  ASSERT_TRUE(b.has_value()) << err;
  EXPECT_EQ(b->appName, "İkinci");
  for (auto& f : {runner, o1, o2}) std::remove(f.c_str());
}

TEST(AppPkg, CorruptTrailer) {
  std::string p = tmp("c.bin"), err;
  writeBytes(p, std::string(10, 'a'));   // too short
  EXPECT_FALSE(loadEmbedded(p, &err).has_value());
  std::string bad = std::string(20, 'a');
  uint64_t huge = 1ull << 40;
  bad.append(reinterpret_cast<char*>(&huge), 8);
  bad += "PCAPP_PAYLOAD_V1";
  writeBytes(p, bad);
  EXPECT_FALSE(loadEmbedded(p, &err).has_value());
  EXPECT_FALSE(err.empty());
  std::remove(p.c_str());
}

TEST(AppPkg, LintNoExposed) {
  auto a = bracketApp();
  for (auto& p : a.document.params.all()) p.exposed = false;
  auto l = lintApp(a);
  EXPECT_TRUE(hasMsg(l, "gösterilen parametre yok", true));
  EXPECT_TRUE(lintHasErrors(l));
}

TEST(AppPkg, LintInfiniteRangeWarn) {
  auto a = bracketApp();
  a.document.params.find("W")->maxValue = std::numeric_limits<double>::infinity();
  auto l = lintApp(a);
  EXPECT_TRUE(hasMsg(l, "'W'", false));
}

TEST(AppPkg, LintDefaultOutOfRange) {
  auto a = bracketApp();
  a.document.params.find("W")->expression = "500";
  auto l = lintApp(a);
  EXPECT_TRUE(hasMsg(l, "'W' varsayılan değeri", true));
}

TEST(AppPkg, LintBadExpression) {
  auto a = bracketApp();
  a.document.params.find("W")->expression = "Q + ";
  auto l = lintApp(a);
  EXPECT_TRUE(lintHasErrors(l));
}

TEST(AppPkg, LintBracketRuns) {
  // Smoke: lint of the sample must complete; any result is a list of prefixed strings.
  auto l = lintApp(bracketApp());
  for (auto& s : l) EXPECT_TRUE(s.rfind("HATA: ", 0) == 0 || s.rfind("UYARI: ", 0) == 0) << s;
}

// Regression (QA review): outputs are written via temp file + rename — an existing app (possibly
// running) is replaced atomically, no temp file is left behind, and a failed write keeps the old file.
TEST(AppPkg, AtomicOverwriteAndFailureKeepsOldFile) {
  auto a = bracketApp();
  std::string runner = tmp("r3.bin"), out = tmpExe("a3.bin"), pkg = tmp("a3.pcapp"), err;
  writeBytes(runner, std::string(300, 'R'));
  ASSERT_TRUE(buildStandaloneApp(a, runner, out, &err)) << err;
  ASSERT_TRUE(savePackage(a, pkg, &err)) << err;
  auto b = a;
  b.appName = "Yeni";
  ASSERT_TRUE(buildStandaloneApp(b, runner, out, &err)) << err;
  ASSERT_TRUE(savePackage(b, pkg, &err)) << err;
  EXPECT_EQ(loadEmbedded(out, &err)->appName, "Yeni");
  EXPECT_EQ(loadPackage(pkg, &err)->appName, "Yeni");
  struct stat st{};
  EXPECT_NE(stat((out + ".tmp." + std::to_string(getpid())).c_str(), &st), 0);
  // serialisation failure (invalid UTF-8) -> error, previous package intact
  auto bad = a;
  bad.appName = std::string("x\xff", 2);
  EXPECT_FALSE(savePackage(bad, pkg, &err));
  EXPECT_EQ(loadPackage(pkg, &err)->appName, "Yeni");
  // empty runner is rejected
  writeBytes(runner, "");
  EXPECT_FALSE(buildStandaloneApp(a, runner, out, &err));
  EXPECT_EQ(loadEmbedded(out, &err)->appName, "Yeni");
  for (auto& f : {runner, out, pkg}) std::remove(f.c_str());
}
