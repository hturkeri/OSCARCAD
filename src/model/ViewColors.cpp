// SC-COLOR: see pc/ViewColors.h
#include "pc/ViewColors.h"
#include <algorithm>

namespace pc::model {

std::string normalizeColor(const std::string& c) {
  if (!isValidColor(c)) return {};
  std::string s = c;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
  return s;
}

ViewColorMap resolveViewColors(const Document& doc, const ModelResult& r, int stopAfter) {
  ViewColorMap out;
  if (doc.view.colors.empty() || r.faces.empty()) return out;
  for (int pass = 0; pass < 2; ++pass) {   // pass 0: body members, pass 1: face members (override)
    for (const NamedSelection& g : doc.view.colors) {
      if (g.members.empty()) continue;
      const std::string hex = normalizeColor(g.color);
      if (hex.empty()) continue;
      NsResolution res;
      try { res = resolve(doc, g, r, stopAfter); } catch (...) { continue; }
      for (const auto& m : res.members) {
        if (!isResolved(m.status)) continue;
        if (m.kind == NsMemberKind::Body && pass == 0) {
          if (m.status == NsStatus::Merged) continue;
          for (const auto& b : m.bodies) {
            const auto* bi = r.body(b);
            if (!bi) continue;
            out.body[b] = hex;
            for (const auto& f : bi->faces) out.face[f] = hex;
          }
        } else if (m.kind == NsMemberKind::Face && pass == 1) {
          for (const auto& f : m.faces) {
            if (!r.faces.count(f)) continue;
            out.faceOwn[f] = hex;
            out.face[f] = hex;
          }
        }
      }
    }
  }
  return out;
}

} // namespace pc::model
