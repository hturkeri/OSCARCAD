#pragma once
// SC-COLOR: resolution of the view-state face / body colours (pc::ViewState::colors) against a regenerated model.
// A Body member colours every face of the body; a Face member (own group) overrides it. Face members follow face
// lineage exactly like hidden faces / named selections (split -> every piece keeps the colour, consumed -> ignored).
#include "pc/Document.h"
#include "pc/NamedSelections.h"
#include <map>
#include <string>

namespace pc::model {

struct ViewColorMap {
  std::map<std::string, std::string> faceOwn;   // current face name -> "#rrggbb" set on the face itself
  std::map<std::string, std::string> body;      // current body name -> "#rrggbb"
  std::map<std::string, std::string> face;      // effective colour of every coloured face (body colour, then overrides)
  bool empty() const { return face.empty(); }
};
ViewColorMap resolveViewColors(const Document& doc, const ModelResult& r, int stopAfter = -1);
// "#RRGGBB" (any case) -> lower case "#rrggbb"; "" if invalid.
std::string normalizeColor(const std::string& c);

} // namespace pc::model
