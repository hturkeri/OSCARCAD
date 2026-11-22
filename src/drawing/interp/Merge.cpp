// mergeIntoDocument — add a synthesis candidate (params + features) to an existing document with clash resolution.
#include "interp/InterpUtil.h"
#include "pc/Expression.h"
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace pc::drawing {

namespace {

std::string uniqueName(const std::string& base, const std::function<bool(const std::string&)>& taken) {
  if (!taken(base)) return base;
  for (int k = 2;; ++k) {
    std::string n = base + "_" + std::to_string(k);
    if (!taken(n)) return n;
  }
}

std::string renameAll(std::string expr, const std::map<std::string, std::string>& ren) {
  if (expr.empty()) return expr;
  // two-phase rename through placeholders to avoid chains (A->B, B->C)
  std::map<std::string, std::string> tmp;
  int k = 0;
  for (auto& [a, b] : ren) {
    if (a == b) continue;
    std::string ph = "__pcren" + std::to_string(k++);
    try { expr = renameIdentifier(expr, a, ph); } catch (...) { return expr; }
    tmp[ph] = b;
  }
  for (auto& [ph, b] : tmp) expr = renameIdentifier(expr, ph, b);
  return expr;
}

// feature id references inside a face/edge name: "<fid>.xxx", "edge(<fid>.a|<fid2>.b)"
std::string renameFeatureRefs(std::string s, const std::map<std::string, std::string>& fren) {
  for (auto& [a, b] : fren) {
    if (a == b) continue;
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
      bool atStart = i == 0 || s[i - 1] == '(' || s[i - 1] == '|' || s[i - 1] == ',' || s[i - 1] == ':' || s[i - 1] == ' ';
      if (atStart && s.compare(i, a.size(), a) == 0 &&
          (i + a.size() == s.size() || s[i + a.size()] == '.' || s[i + a.size()] == ',' || s[i + a.size()] == '|' || s[i + a.size()] == ')')) {
        out += b;
        i += a.size();
      } else {
        out += s[i++];
      }
    }
    s = out;
  }
  return s;
}

}  // namespace

std::vector<std::string> mergeIntoDocument(const SynthesisCandidate& c, pc::Document& doc, std::string* err,
                                           std::map<std::string, std::string>* renamedParams) {
  std::vector<std::string> added;
  const Document& frag = c.fragment;
  if (frag.features.empty() && frag.params.all().empty()) {
    if (err) *err = "Aday boş";
    return added;
  }
  // ---- parameters
  std::map<std::string, std::string> pren;
  std::set<std::string> newNames;
  for (auto& p : frag.params.all()) {
    std::string n = uniqueName(p.name, [&](const std::string& x) { return doc.params.find(x) != nullptr || newNames.count(x); });
    newNames.insert(n);
    pren[p.name] = n;
  }
  // ---- feature ids
  std::map<std::string, std::string> fren;
  std::set<std::string> newIds;
  for (auto& f : frag.features) {
    std::string n = uniqueName(f.id, [&](const std::string& x) { return doc.feature(x) != nullptr || newIds.count(x); });
    newIds.insert(n);
    fren[f.id] = n;
  }
  // body names of the existing document cannot clash with new feature ids used as body names
  Document out = doc;
  for (auto& p : frag.params.all()) {
    Parameter q = p;
    q.name = pren[p.name];
    q.expression = renameAll(q.expression, pren);
    if (!out.params.add(q)) {
      if (err) *err = "Parametre eklenemedi: " + q.name;
      return {};
    }
  }
  static const std::set<std::string> kFeatureRefProps = {"sketch", "source", "target"};
  static const std::set<std::string> kListRefProps = {"targets", "tools"};
  for (auto& f0 : frag.features) {
    Feature f = f0;
    f.id = fren[f0.id];
    for (auto& [k, v] : f.props) {
      if (kFeatureRefProps.count(k) && fren.count(v)) v = fren[v];
      else if (kListRefProps.count(k)) {
        std::string outv, item;
        std::stringstream ss(v);
        while (std::getline(ss, item, ',')) outv += (outv.empty() ? "" : ",") + (fren.count(item) ? fren[item] : item);
        v = outv;
      } else if (k == "bodyName") {
        // keep
      } else {
        v = renameAll(v, pren);
      }
    }
    for (auto& r : f.refs) r = renameFeatureRefs(r, fren);
    if (f.sketch) {
      for (auto& k : f.sketch->constraints) k.valueExpr = renameAll(k.valueExpr, pren);
      f.sketch->plane.offsetExpr = renameAll(f.sketch->plane.offsetExpr, pren);
      if (fren.count(f.sketch->plane.planeRef)) f.sketch->plane.planeRef = fren[f.sketch->plane.planeRef];
      f.sketch->plane.faceRef = renameFeatureRefs(f.sketch->plane.faceRef, fren);
    }
    out.features.push_back(f);
    added.push_back(f.id);
  }
  std::vector<ParamError> perr;
  if (!out.params.evaluate(&perr)) {
    if (err) *err = "Parametreler değerlendirilemedi: " + (perr.empty() ? std::string() : perr[0].param + ": " + perr[0].message);
    return {};
  }
  doc = std::move(out);
  if (renamedParams) for (auto& [a, b] : pren) if (a != b) (*renamedParams)[a] = b;
  return added;
}

}  // namespace pc::drawing
