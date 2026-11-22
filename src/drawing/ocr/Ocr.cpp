// WP-2: Tesseract 5 (LSTM) wrapper — single-line recognition with per-symbol n-best variants.
// Compiled only with OpenCV + Tesseract (see src/drawing/CMakeLists.txt); fallback in ocr/nodeps/.
#include "ocr/Ocr.h"
#include "ocr/OcrCommon.h"
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#include <leptonica/allheaders.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace pc::drawing::ocr {

namespace {
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\n' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
  return s.substr(i);
}
}  // namespace

struct Engine::Impl {
  tesseract::TessBaseAPI api;
  bool ok = false;
  std::string error, langs;
};

bool available() { return true; }
std::string engineVersion() { return std::string("tesseract ") + tesseract::TessBaseAPI::Version(); }
std::string resolveTessdataDir(const std::string& requested, const std::string& languages) {
  return detail::resolveTessdataDirImpl(requested, languages);
}

Engine::Engine(const std::string& tessdataDir, const std::string& languages) : d_(std::make_unique<Impl>()) {
  d_->langs = languages;
  const std::string dir = resolveTessdataDir(tessdataDir, languages);
  if (dir.empty()) {
    d_->error = "OCR: tessdata bulunamadı (" + languages + ")";
    return;
  }
  // Silence Tesseract's stderr chatter ("Estimating resolution ...").
  if (d_->api.Init(dir.c_str(), languages.c_str(), tesseract::OEM_LSTM_ONLY) != 0) {
    d_->error = "OCR: tesseract başlatılamadı (" + dir + ", " + languages + ")";
    return;
  }
#ifdef _WIN32
  d_->api.SetVariable("debug_file", "NUL");
#else
  d_->api.SetVariable("debug_file", "/dev/null");
#endif
  d_->api.SetVariable("lstm_choice_mode", "2");
  d_->api.SetVariable("user_defined_dpi", "300");
  d_->ok = true;
}

Engine::~Engine() {
  if (d_ && d_->ok) d_->api.End();
}
bool Engine::ok() const { return d_->ok; }
const std::string& Engine::error() const { return d_->error; }
const std::string& Engine::languages() const { return d_->langs; }

Result Engine::recognize(const GrayView& img, int psm, const std::string& whitelist, double dpi, int maxAlternatives) {
  Result r;
  if (!d_->ok) { r.error = d_->error; return r; }
  if (!img.data || img.width <= 0 || img.height <= 0) { r.error = "OCR: boş görüntü"; return r; }
  auto& api = d_->api;
  api.SetPageSegMode(static_cast<tesseract::PageSegMode>(psm));
  api.SetVariable("tessedit_char_whitelist", whitelist.c_str());
  api.SetImage(img.data, img.width, img.height, 1, static_cast<int>(img.stride ? img.stride : img.width));
  api.SetSourceResolution(dpi > 0 ? static_cast<int>(std::lround(dpi)) : 300);
  if (api.Recognize(nullptr) != 0) { r.error = "OCR: tanıma başarısız"; api.Clear(); return r; }

  struct Sym { std::string best; float conf; std::string alt; float altConf; bool wordStart; };
  std::vector<Sym> syms;
  double confSum = 0;
  int words = 0;
  std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
  if (it) {
    do {
      if (it->IsAtBeginningOf(tesseract::RIL_WORD)) {
        const float wc = it->Confidence(tesseract::RIL_WORD);
        if (wc >= 0) { confSum += wc; ++words; }
      }
      std::unique_ptr<char[]> s(it->GetUTF8Text(tesseract::RIL_SYMBOL));
      if (!s) continue;
      Sym sym{s.get(), it->Confidence(tesseract::RIL_SYMBOL) / 100.f, {}, 0, it->IsAtBeginningOf(tesseract::RIL_WORD)};
      tesseract::ChoiceIterator ci(*it);
      do {
        const char* t = ci.GetUTF8Text();
        if (!t) continue;
        if (sym.best != t && sym.alt.empty()) { sym.alt = t; sym.altConf = ci.Confidence() / 100.f; }
      } while (ci.Next());
      syms.push_back(std::move(sym));
    } while (it->Next(tesseract::RIL_SYMBOL));
  }
  api.Clear();
  auto build = [&](const std::vector<int>& subst) {
    std::string out;
    for (std::size_t i = 0; i < syms.size(); ++i) {
      if (syms[i].wordStart && !out.empty()) out.push_back(' ');
      out += std::find(subst.begin(), subst.end(), static_cast<int>(i)) != subst.end() ? syms[i].alt : syms[i].best;
    }
    return trim(out);
  };
  r.text = build({});
  r.conf = words ? static_cast<float>(confSum / words / 100.0) : 0.f;
  r.ok = !r.text.empty();
  // n-best: substitute the most ambiguous symbols (smallest margin best - alt)
  std::vector<int> amb;
  for (std::size_t i = 0; i < syms.size(); ++i)
    if (!syms[i].alt.empty()) amb.push_back(static_cast<int>(i));
  std::sort(amb.begin(), amb.end(), [&](int a, int b) {
    return (syms[a].conf - syms[a].altConf) < (syms[b].conf - syms[b].altConf);
  });
  std::set<std::string> seen{r.text};
  for (std::size_t k = 0; k < amb.size() && static_cast<int>(r.alternatives.size()) < maxAlternatives; ++k) {
    const std::string v = build({amb[k]});
    if (!seen.insert(v).second) continue;
    const float margin = syms[amb[k]].conf - syms[amb[k]].altConf;
    r.alternatives.push_back({v, std::max(0.f, r.conf - std::max(0.f, margin) * 0.5f)});
  }
  if (amb.size() >= 2 && static_cast<int>(r.alternatives.size()) < maxAlternatives) {
    const std::string v = build({amb[0], amb[1]});
    if (seen.insert(v).second) r.alternatives.push_back({v, r.conf * 0.7f});
  }
  return r;
}

}  // namespace pc::drawing::ocr
