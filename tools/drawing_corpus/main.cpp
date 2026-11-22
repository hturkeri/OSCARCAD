// pc_drawing_corpus --out <dir> [--samples <dir>] [--quick] [--core] [--count N] [--families a,b]
//   --quick : 1 case (nominal parameters) per family;  default: 10 cases per family (SPEC §6.1: >= 10)
//   --core  : small rasters (committed core set tests/drawing/corpus)
// Output: <out>/<family>/<family>_NN/{sheet.pdf, sheet_outline.pdf, sheet_pscript.pdf, sheet.dxf, sheet_exploded.dxf,
//          clean.png, scan_a.jpg[, scan_b.jpg, scan_c.jpg], photo_a.jpg, gt.json}. OWNER WP-1.
#include "Corpus.h"
#include <QGuiApplication>
#include <QImage>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

using namespace corpus;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
  if (!std::getenv("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");  // portable (no POSIX setenv on Windows)
  std::string out, samples, famFilter;
  bool quick = false, core = false; int count = 10;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { if (i + 1 >= argc) { std::cerr << a << ": değer eksik\n"; std::exit(2); } return argv[++i]; };
    if (a == "--out") out = next();
    else if (a == "--samples") samples = next();
    else if (a == "--quick") quick = true;
    else if (a == "--core") core = true;
    else if (a == "--count") count = std::atoi(next().c_str());
    else if (a == "--families") famFilter = "," + next() + ",";
    else { std::cerr << "kullanım: pc_drawing_corpus --out <dizin> [--samples <dizin>] [--quick] [--core] [--count N] [--families a,b]\n"; return 2; }
  }
  if (out.empty()) { std::cerr << "--out gerekli\n"; return 2; }
  if (quick) count = 1;
  QGuiApplication app(argc, argv);
  const double cleanDpi = core ? 150 : 300, scanDpi = core ? 100 : 200, photoDpi = core ? 100 : 150;
  int ok = 0, fail = 0;
  for (auto& fam : families()) {
    if (!famFilter.empty() && famFilter.find("," + fam + ",") == std::string::npos) continue;
    for (int idx = 0; idx < count; ++idx) {
      try {
        std::mt19937 rng(static_cast<unsigned>(std::hash<std::string>{}(fam) % 100000 * 131 + idx));
        Part part = makePart(fam, idx, rng);
        GroundTruth gt = buildSheet(part, idx);
        const fs::path dir = fs::path(out) / fam / part.name;
        fs::create_directories(dir);
        auto P = [&](const char* f) { return (dir / f).string(); };
        bool good = writePdf(gt, P("sheet.pdf"), 0) && writePdf(gt, P("sheet_outline.pdf"), 1) && writePdf(gt, P("sheet_pscript.pdf"), 2) &&
                    writeDxf(gt, P("sheet.dxf"), true) && writeDxf(gt, P("sheet_exploded.dxf"), false);
        QImage clean = renderImage(gt, cleanDpi);
        good = good && clean.save(QString::fromStdString(P("clean.png")));
        gt.attrs["raster.clean.png.dpi"] = std::to_string(static_cast<int>(cleanDpi));
        const QImage base = renderImage(gt, scanDpi);
        const char* scans[] = {"scan_a.jpg", "scan_b.jpg", "scan_c.jpg"};
        const int nScan = quick || core ? 1 : 3;
        std::uniform_real_distribution<double> skew(0.5, 3.0), sig(0.8, 1.5);
        std::uniform_int_distribution<int> q(40, 70);
        for (int k = 0; k < nScan; ++k) {
          const double sk = (k % 2 ? -1 : 1) * skew(rng), sg = sig(rng); const int qq = q(rng);
          good = good && makeScan(base, sk, sg, 12, rng).save(QString::fromStdString(P(scans[k])), "JPG", qq);
          std::ostringstream o; o << "dpi=" << scanDpi << ";skewDeg=" << sk << ";blur=" << sg << ";jpegQ=" << qq;
          gt.attrs[std::string("raster.") + scans[k]] = o.str();
        }
        std::uniform_real_distribution<double> tilt(-15, 15);
        const double tl = tilt(rng);
        good = good && makePhoto(photoDpi == scanDpi ? base : renderImage(gt, photoDpi), tl, rng).save(QString::fromStdString(P("photo_a.jpg")), "JPG", 75);
        gt.attrs["raster.photo_a.jpg"] = "dpi=" + std::to_string(static_cast<int>(photoDpi)) + ";tiltDeg=" + std::to_string(tl);
        std::string err;
        good = good && saveGroundTruth(gt, P("gt.json"), &err);
        if (!good) throw std::runtime_error("yazma hatası " + err);
        std::cout << part.name << ": " << gt.sheet.prims.size() << " ilkel, " << gt.sheet.texts.size() << " metin, " << gt.dims.size()
                  << " ölçü, ölçek " << gt.scale.num << ":" << gt.scale.den << "\n";
        ++ok;
      } catch (const std::exception& e) {
        std::cerr << fam << " #" << idx << ": " << e.what() << "\n"; ++fail;
      }
    }
  }
  std::cout << ok << " örnek üretildi, " << fail << " hata\n";
  return fail == 0 && ok > 0 ? 0 : 1;
}
