// pc_mkapp — command-line "Save as App": pc_mkapp in.pcad runner_exe out_base [--name N] [--version V]
//            [--author A] [--desc D] [--no-exe] [--no-pcapp]
// Uses the exposure flags stored in the .pcad parameters. Writes out_base (standalone) and out_base.pcapp.
#include "pc/AppLint.h"
#include "pc/AppPackage.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
  // Windows passes argv in the ANSI code page (e.g. cp1254); PersistCAD strings and paths are UTF-8.
  std::vector<std::string> u8args;
  std::vector<char*> u8argv;
  {
    int wc = 0;
    if (LPWSTR* wv = CommandLineToArgvW(GetCommandLineW(), &wc)) {
      for (int i = 0; i < wc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wv[i], -1, s.data(), n, nullptr, nullptr);
        u8args.push_back(std::move(s));
      }
      LocalFree(wv);
      for (auto& s : u8args) u8argv.push_back(s.data());
      u8argv.push_back(nullptr);
      argc = wc;
      argv = u8argv.data();
    }
  }
  SetConsoleOutputCP(CP_UTF8);
#endif
  if (argc < 4) { std::cerr << "kullanım: pc_mkapp in.pcad runner out_base [--name N] [--version V] [--author A] [--desc D] [--no-exe] [--no-pcapp]\n"; return 1; }
  pc::app::AppDefinition a;
  std::string err;
  if (!pc::loadDocument(a.document, argv[1], &err)) { std::cerr << err << "\n"; return 1; }
  a.appName = a.document.name;
  bool exe = true, pkg = true;
  for (int i = 4; i < argc; ++i) {
    std::string k = argv[i];
    auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--name") a.appName = val();
    else if (k == "--version") a.version = val();
    else if (k == "--author") a.author = val();
    else if (k == "--desc") a.description = val();
    else if (k == "--no-exe") exe = false;
    else if (k == "--no-pcapp") pkg = false;
  }
  for (auto& l : pc::app::lintApp(a)) std::cerr << l << "\n";
  std::string base = argv[3];
  if (exe && !pc::app::buildStandaloneApp(a, argv[2], base, &err)) { std::cerr << err << "\n"; return 1; }
  if (pkg && !pc::app::savePackage(a, base + ".pcapp", &err)) { std::cerr << err << "\n"; return 1; }
  return 0;
}
