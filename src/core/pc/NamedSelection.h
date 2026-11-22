#pragma once
// PersistCAD core — NAMED SELECTIONS (SpaceClaim "Named Selections" / "Groups" of geometry), pure data.
// Design, guarantees and API: docs/named_selections/MODEL.md.  Resolution against a regenerated model
// (face lineage, statuses, signals, STEP export) lives in pc_model: src/model/pc/NamedSelections.h.
//
// A named selection is a user-given, unique, stable NAME for a set of B-rep entities (faces; edges and
// bodies are supported for future UI). Each member is stored as a persistent reference = the persistent
// topological name of the entity at the time it was captured (pc/Feature.h naming contract) PLUS a
// geometric/topological signature used only to (a) disambiguate positional names (split pieces ":k",
// fallback faces ".u<k>") and (b) diagnose/suggest when a member is lost. A member is NEVER silently
// re-bound to an unrelated face: lost members stay lost (status "kayıp") until the user re-binds them.
//
// Stored in the .pcad document under "namedSelections" (key omitted when empty -> old files and old readers
// are unaffected).
#include <string>
#include <vector>

namespace pc {

enum class NsMemberKind { Face, Edge, Body };

const char* toString(NsMemberKind k);                    // "face" | "edge" | "body"
bool fromString(const std::string& s, NsMemberKind& out);

// Geometric/topological fingerprint of a member at capture time (refreshed by
// pc::model::refreshSignatures after unambiguous resolutions). Units: mm, mm^2, mm^3.
struct NsSignature {
  bool valid = false;
  std::string surface;                 // face: plane|cylinder|cone|sphere|torus|bspline|revolution|extrusion|other
                                       // edge: line|circle|ellipse|bspline|other ; body: solid
  double nx = 0, ny = 0, nz = 0;       // plane: OUTWARD normal; cylinder/cone/torus: axis; line edge: direction
  double cx = 0, cy = 0, cz = 0;       // face: area centroid; edge: midpoint; body: volume centroid
  double size = 0;                     // face: area; edge: length; body: volume
  double radius = 0;                   // cylinder/cone(ref)/sphere/torus(major)/circle radius
  std::vector<std::string> neighbours; // face: persistent names of adjacent faces (sorted, <= 16)
  std::string body;                    // owning body name at capture time
};

struct NamedSelectionMember {
  NsMemberKind kind = NsMemberKind::Face;
  std::string ref;                     // persistent name at capture: "Extrude1.end", "Extrude1.end:2",
                                       // "edge(Extrude1.end|Extrude1.side.e5)", body name "Braket"
  NsSignature sig;
  std::vector<std::string> anchors;    // Body members: face names of the body at capture (merge tracking)
};

struct NamedSelection {
  std::string id;                      // stable internal id ("ns1", "ns2", ...) — never reused in a document,
                                       // survives renames (UI tree keys, change notifications)
  std::string name;                    // user name: unique in the document (case-insensitive), editable
  std::vector<NamedSelectionMember> members;
  std::string color = "#E8A33D";       // "#RRGGBB" display colour (also written as STEP face colour)
  std::string description;
  std::string createdAfter;            // id of the last regenerated feature when created ("" = before history)
  int createdAt = -1;                  // its index at creation (informational; createdAfter is authoritative)
  bool exportStep = true;              // write member faces' names into STEP export
};

// Name rules: 1..128 bytes after trimming, no control characters, no leading/trailing white space.
// Any other UTF-8 text is allowed ("Giriş", "wall hot side", "inlet-1").
bool isValidSelectionName(const std::string& name, std::string* why = nullptr);
// Case-insensitive (ASCII) name equality: CFD/FEA meshers often fold zone names to lower case.
bool sameSelectionName(const std::string& a, const std::string& b);
bool isValidColor(const std::string& c);   // "#RRGGBB"

} // namespace pc
