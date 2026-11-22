// persistcad_runner — runs a PersistCAD "app" (.pcapp or embedded payload).
//   persistcad_runner [app.pcapp] [--set NAME=VALUE ...] [--screenshot out.png] [--validate-only] [--text]
#include "RunnerCore.h"
#include "RunnerWindow.h"
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
#include <cstring>
#include <iostream>

static void usage() {
  std::puts("Kullanım: persistcad_runner [app.pcapp] [--set AD=DEĞER ...] [--screenshot çıktı.png]\n"
            "                            [--section xz|yz|xy|quarter [--section-offset YÜZDE] [--section-flip]]\n"
            "                            [--validate-only [--text]]\n"
            "  --set AD=DEĞER     parametre değerini ayarla (birden fazla kullanılabilir)\n"
            "  --screenshot DOSYA pencerenin görüntüsünü kaydedip çık\n"
            "  --show-planes      yardımcı düzlemleri başlangıçta göster\n"
            "  --show-results     parametre panelini 'Hesaplanan değerler' kutusuna kaydır (ekran görüntüsü için)\n"
            "  --section DÜZLEM   kesit görünümü: xz | yz | xy | quarter (çeyrek kesit); yalnızca görünüm\n"
            "  --section-offset N kesit düzleminin konumu, -100..100 (sınır kutusu yarı genişliğinin yüzdesi)\n"
            "  --section-flip     kesitin diğer tarafını göster\n"
            "  --validate-only    imalat raporunu stdout'a (JSON; --text ile düz metin) yazıp çık;\n"
            "                     çıkış kodu: 0 uygun, 2 uygun değil, 1 yükleme/kullanım hatası\n"
            "  --export-step DOSYA  --set değerleriyle modeli üretip STEP yaz (yüz adları = adlandırılmış\n"
            "                     seçimler; CFD/FEA sınır bölgeleri) ve çık. --body AD ile tek gövde\n"
            "  --named-selections adlandırılmış seçimlerin durumunu JSON olarak yazıp çık");
}

int main(int argc, char** argv) {
  bool validateOnly = false;
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--validate-only") || !std::strcmp(argv[i], "--export-step") ||
        !std::strcmp(argv[i], "--named-selections"))
      validateOnly = true;   // headless modes (no QApplication / display needed)

  std::unique_ptr<QCoreApplication> app;
  if (validateOnly) app = std::make_unique<QCoreApplication>(argc, argv);
  else app = std::make_unique<QApplication>(argc, argv);

  QStringList args = QCoreApplication::arguments();
  std::string pcapp, shot, exportStep;
  std::vector<std::string> exportBodies;
  bool nsReport = false;
  std::map<std::string, double> sets;
  bool text = false, showPlanes = false, sectionFlip = false, showResults = false;
  QString section;
  int sectionOffset = 0;
  for (int i = 1; i < args.size(); ++i) {
    std::string a = args[i].toStdString();
    if (a == "--set" && i + 1 < args.size()) {
      std::string n, err; double v;
      if (!pc::runner::parseSetArg(args[++i].toStdString(), n, v, &err)) { std::cerr << err << "\n"; return 1; }
      sets[n] = v;
    } else if (a.rfind("--set=", 0) == 0) {
      std::string n, err; double v;
      if (!pc::runner::parseSetArg(a.substr(6), n, v, &err)) { std::cerr << err << "\n"; return 1; }
      sets[n] = v;
    } else if (a == "--screenshot" && i + 1 < args.size()) shot = args[++i].toStdString();
    else if (a == "--validate-only") {}
    else if (a == "--export-step" && i + 1 < args.size()) exportStep = args[++i].toStdString();
    else if (a == "--body" && i + 1 < args.size()) exportBodies.push_back(args[++i].toStdString());
    else if (a == "--named-selections") nsReport = true;
    else if (a == "--text") text = true;
    else if (a == "--show-planes") showPlanes = true;
    else if (a == "--show-results") showResults = true;
    else if (a == "--section" && i + 1 < args.size()) {
      section = args[++i];
      bool ok = false;
      pc::viewer::ShapeViewer::sectionPresetFromString(section, &ok);
      if (!ok) { std::cerr << "--section: bilinmeyen değer '" << section.toStdString() << "' (xz|yz|xy|quarter|none)\n"; return 1; }
    }
    else if (a == "--section-offset" && i + 1 < args.size()) sectionOffset = args[++i].toInt();
    else if (a == "--section-flip") sectionFlip = true;
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else if (!a.empty() && a[0] != '-' && pcapp.empty()) pcapp = a;
    else { std::cerr << "Bilinmeyen argüman: " << a << "\n"; usage(); return 1; }
  }

  // Loading order: 1) CLI .pcapp  2) embedded payload  3) file dialog
  std::optional<pc::app::AppDefinition> def;
  std::string err;
  if (!pcapp.empty()) {
    def = pc::app::loadPackage(pcapp, &err);
    if (!def) { std::cerr << "Uygulama yüklenemedi: " << err << "\n"; if (!validateOnly) QMessageBox::critical(nullptr, "PersistCAD", QString::fromStdString(err)); return 1; }
  } else {
    def = pc::app::loadEmbedded(QCoreApplication::applicationFilePath().toStdString(), &err);
    if (!def && !err.empty()) std::cerr << "Gömülü uygulama okunamadı: " << err << "\n";
  }
  if (!def && !validateOnly) {
    QString f = QFileDialog::getOpenFileName(nullptr, "Uygulama paketi aç", QString(), "PersistCAD uygulaması (*.pcapp)");
    if (f.isEmpty()) return 0;
    def = pc::app::loadPackage(f.toStdString(), &err);
    if (!def) { QMessageBox::critical(nullptr, "PersistCAD", QString::fromStdString(err)); return 1; }
  }
  if (!def) { std::cerr << "Uygulama bulunamadı: .pcapp dosyası verin veya yük gömülü çalıştırılabilir kullanın.\n"; return 1; }

  if (!exportStep.empty() || nsReport) {
    std::map<std::string, std::string> ov = pc::runner::toOverrides(sets);
    for (auto& [k, v] : sets) if (!def->document.params.find(k)) { std::cerr << "Bilinmeyen parametre: " << k << "\n"; return 1; }
    if (!exportStep.empty()) {
      std::string e;
      if (!pc::runner::exportStepWithNames(*def, ov, exportStep, &e, exportBodies)) { std::cerr << "STEP: " << e << "\n"; return 1; }
      std::cerr << "STEP yazıldı: " << exportStep << "\n";
    }
    if (nsReport) std::cout << pc::runner::namedSelectionsReport(*def, ov).dump(2) << "\n";
    return 0;
  }

  if (validateOnly) {
    // Defaults overridden by --set (exposed or not).
    std::map<std::string, std::string> ov = pc::runner::toOverrides(sets);
    for (auto& [k, v] : sets) if (!def->document.params.find(k)) { std::cerr << "Bilinmeyen parametre: " << k << "\n"; return 1; }
    auto r = pc::runner::validateSafe(*def, ov);
    if (text) std::cout << pc::runner::reportToText(*def, r, sets);
    else std::cout << pc::runner::reportToJson(*def, r, sets).dump(2) << "\n";
    return pc::runner::acceptable(*def, r) ? 0 : 2;
  }

  pc::runner::RunnerWindow win(*def);
  QString qerr;
  if (!win.setCliValues(sets, &qerr)) { std::cerr << qerr.toStdString() << "\n"; return 1; }
  if (showPlanes) win.setShowPlanes(true);
  if (!section.isEmpty()) {
    win.sectionControls()->applySpec(section);
    win.sectionControls()->setOffsetPercent(sectionOffset);
    win.sectionControls()->setFlipped(sectionFlip);
  }
  win.show();
  if (!shot.empty()) {
    QObject::connect(&win, &pc::runner::RunnerWindow::settled, &win, [&] {
      // give the GL viewer a moment to tessellate and paint
      QTimer::singleShot(1200, &win, [&] {
        if (showResults) win.showResults();
        QCoreApplication::processEvents();
        QImage img = win.screenshot();
        if (!img.save(QString::fromStdString(shot))) { std::cerr << "Ekran görüntüsü kaydedilemedi\n"; QCoreApplication::exit(1); return; }
        QCoreApplication::exit(0);
      });
    });
    QTimer::singleShot(120000, &win, [] { std::cerr << "Zaman aşımı\n"; QCoreApplication::exit(3); });
  }
  win.startup();
  return QCoreApplication::exec();
}
