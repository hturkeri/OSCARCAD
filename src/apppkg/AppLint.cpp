#include "pc/AppLint.h"
#include "pc/AppPackage.h"
#include "pc/MfgChecks.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include <cmath>
#include <sstream>

namespace pc::app {

std::vector<std::string> lintApp(const AppDefinition& a) {
  std::vector<std::string> out;
  auto err = [&](const std::string& m) { out.push_back(kLintError + m); };
  auto warn = [&](const std::string& m) { out.push_back(kLintWarning + m); };
  const Document& doc = a.document;

  if (a.appName.empty()) err("Uygulama adı boş.");

  int exposed = 0;
  for (const auto& p : doc.params.all()) {
    if (!p.exposed) continue;
    ++exposed;
    bool fmin = std::isfinite(p.minValue), fmax = std::isfinite(p.maxValue);
    if (!fmin || !fmax)
      warn("'" + p.name + "' parametresi için sonlu min/maks değeri tanımlı değil; son kullanıcı sınırsız değer girebilir.");
    if (fmin && fmax && p.minValue > p.maxValue)
      err("'" + p.name + "' parametresinde min (" + pc::formatNumber(p.minValue) + ") maks değerden (" + pc::formatNumber(p.maxValue) + ") büyük.");
    if (p.step < 0) err("'" + p.name + "' parametresinin adım değeri negatif.");
  }
  if (exposed == 0) err("Uygulamada gösterilen parametre yok; en az bir parametreyi 'Uygulamada göster' olarak işaretleyin.");

  // Default values (evaluate with no overrides).
  ParamValues vals;
  bool evalOk = false;
  try { vals = doc.evaluateParams(); evalOk = true; }
  catch (const std::exception& e) { err(std::string("Parametreler hesaplanamadı: ") + e.what()); }

  if (evalOk) {
    for (const auto& p : doc.params.all()) {
      if (!p.exposed) continue;
      auto it = vals.find(p.name);
      if (it == vals.end()) continue;
      double v = it->second;
      if (v < p.minValue || v > p.maxValue)
        err("'" + p.name + "' varsayılan değeri (" + pc::formatNumber(v) + ") izin verilen aralığın [" +
            pc::formatNumber(p.minValue) + ", " + pc::formatNumber(p.maxValue) + "] dışında.");
    }
    // Exposed parameters whose expression depends on others: user overrides replace the expression.
    for (const auto& p : doc.params.all()) {
      if (!p.exposed) continue;
      bool literal = true;
      try { pc::evaluateExpression(p.expression, [](const std::string&) { return std::optional<double>(); }); }
      catch (...) { literal = false; }
      if (!literal)
        warn("'" + p.name + "' bir ifadeyle tanımlı ('" + p.expression + "'); uygulamada sabit sayı olarak ezilecek.");
    }
  }

  // Model must regenerate.
  if (evalOk) {
    pc::model::ModelResult m = pc::model::regenerate(doc);
    if (!m.ok) {
      std::string msg = "Model varsayılan parametrelerle yeniden oluşturulamadı.";
      for (auto& s : m.status) if (!s.ok) msg += " [" + s.featureId + ": " + s.message + "]";
      err(msg);
    } else {
      // Manufacturing validation of defaults.
      try {
        auto vr = pc::mfg::validateChange(doc, {}, doc.mfg);
        int ne = 0, nw = 0;
        std::string first;
        for (auto& i : vr.report.issues) {
          if (i.severity == pc::mfg::Severity::Error) { ++ne; if (first.empty()) first = i.message; }
          else if (i.severity == pc::mfg::Severity::Warning) ++nw;
        }
        // v2: multi-body defaults — interfering bodies / unintended split bodies (MULTI_BODY at Info level means
        // the separation is deliberate, so it is not flagged).
        for (auto& i : vr.report.issues) {
          if (i.ruleId.find("BODY_INTERFERENCE") != std::string::npos)
            warn("Varsayılan modelde gövdeler birbirine giriyor" + (i.body.empty() ? std::string() : " (" + i.body + ")") +
                 "; son kullanıcıya girişimli gövdeler sunulacak. " + i.message);
          else if (i.ruleId.find("MULTI_BODY") != std::string::npos && i.severity != pc::mfg::Severity::Info)
            warn("Varsayılan model birden fazla ayrık gövdeye ayrılıyor (istenmeyen bölünme olabilir). " + i.message);
        }
        if (ne > 0)
          err("Varsayılan parametreler imalat kurallarını ihlal ediyor (" + std::to_string(ne) + " hata): " + first);
        if (nw > 0)
          warn("Varsayılan parametrelerde " + std::to_string(nw) + " imalat uyarısı var.");
      } catch (const std::exception& e) {
        warn(std::string("İmalat kontrolü çalıştırılamadı: ") + e.what());
      }
    }
  }
  return out;
}

} // namespace pc::app
