#pragma once
// PersistCAD model — NAMED SELECTIONS: resolution of user-named face/edge/body sets against a regenerated
// model, validated editing, change notifications and STEP export with names.
// Data model: pc/NamedSelection.h (core). Design, guarantees, limitations: docs/named_selections/MODEL.md.
//
// GUARANTEE: a selection's name never changes by itself and never moves to an unrelated face. Every member is
// resolved through the persistent-name LINEAGE of the regeneration (ModelResult::faceLineage):
//   * face split by a later feature  -> the name covers ALL pieces            (status Split)
//   * face trimmed / extended / holed / moved -> same face, same name         (status Ok)
//   * face merged with other faces    -> the merged face keeps the name       (status Merged)
//   * face consumed / deleted         -> member "kayıp" with a diagnostic     (status Lost; never re-bound)
//   * creating feature suppressed / failed / after the rollback position -> Suppressed / Failed / NotYet, and the
//     member comes back automatically when the feature is active again.
// The signature (surface type, normal/axis, area, centroid, neighbours) is used ONLY to pick among positional
// split pieces (":k" / ".u<k>" names, whose numbering can change), to flag Suspect results and to propose
// (never apply) re-binding suggestions for lost members.
#include "pc/Document.h"
#include "pc/Model.h"

#include <TopoDS_Face.hxx>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pc::model {

enum class NsStatus {
  Ok,          // resolved to the original entity (possibly trimmed, extended, holed, moved)
  Split,       // split by later features: the name covers every piece
  Merged,      // merged with other faces (or a split piece re-joined its siblings): name kept on the merged face
  Suspect,     // resolved, but the surface type changed or the choice among split pieces is ambiguous: check it
  Partial,     // (selection only) some members resolved, some not
  Lost,        // "kayıp": the entity no longer exists; never re-bound automatically
  Suppressed,  // creating feature is suppressed
  Failed,      // creating feature failed to regenerate
  NotYet,      // rollback: creating feature lies after the rollback position
  Empty        // (selection only) no members
};
const char* toString(NsStatus s);     // "ok" "split" "merged" "suspect" "partial" "lost" "suppressed" "failed" "notyet" "empty"
const char* statusLabel(NsStatus s);  // Turkish UI label: "Tamam" "Bölündü" "Birleşti" "Şüpheli" "Kısmi" "Kayıp" ...
bool isResolved(NsStatus s);          // Ok | Split | Merged | Suspect (entities are available)

struct NsMemberResult {
  size_t index = 0;                   // index in NamedSelection::members
  NsMemberKind kind = NsMemberKind::Face;
  std::string ref;                    // the stored persistent reference
  NsStatus status = NsStatus::Lost;
  std::vector<std::string> faces;     // current face names (Face members; Body members: all faces of the bodies)
  std::vector<std::string> edges;     // current edge names (Edge members)
  std::vector<std::string> bodies;    // bodies owning the result
  std::string diagnostic;             // Turkish, "" when Ok
  std::vector<std::string> suggestions;  // Lost/Suspect faces: look-alike current faces (best first), NEVER applied
};

struct NsResolution {
  std::string id, name;
  NsStatus status = NsStatus::Empty;  // aggregate: Ok if all Ok; Split/Merged/Suspect if any; Partial if mixed;
                                      // Lost/Suppressed/Failed/NotYet if no member resolved
  std::vector<NsMemberResult> members;
  std::vector<std::string> faces;     // union over members, sorted unique (current persistent names)
  std::vector<std::string> edges;
  std::vector<std::string> bodies;
  std::string diagnostic;             // members' diagnostics joined with "; "
  bool rolledBack = false;            // resolved against a rollback (stopAfter >= 0) result
};

// ------------------------------------------------------------------ capture / signatures
// Signature of a current face / edge / body of `r` (valid=false if the name is unknown).
NsSignature faceSignature(const ModelResult& r, const std::string& faceName);
NsSignature edgeSignature(const ModelResult& r, const std::string& edgeName);
NsSignature bodySignature(const ModelResult& r, const std::string& bodyName);
// Build a member from a CURRENT persistent name of `r` (fails if the entity does not exist).
bool captureMember(const ModelResult& r, NsMemberKind kind, const std::string& ref, NamedSelectionMember& out,
                   std::string* err = nullptr);

// ------------------------------------------------------------------ resolution (pure, never throws)
// `r` must be the regeneration of `doc` (stopAfter = the rollback index used for it, -1 = full history).
NsResolution resolve(const Document& doc, const NamedSelection& sel, const ModelResult& r, int stopAfter = -1);
std::vector<NsResolution> resolveAll(const Document& doc, const ModelResult& r, int stopAfter = -1);
// current face name -> names of the selections containing it (document order) — UI highlight / properties
std::map<std::string, std::vector<std::string>> selectionsByFace(const std::vector<NsResolution>& all);
// Refresh stored signatures from unambiguous (status Ok, single entity, full-history) member resolutions of
// `all` (= resolveAll(doc, r)), and re-anchor positional piece refs (":k") to their current number. Non-positional
// refs are never rewritten. No-op for rollback resolutions. Returns true if `doc` changed. Optional; improves
// piece tracking across long chains of edits (call it e.g. before saving; it needs no undo step).
bool refreshSignatures(Document& doc, const ModelResult& r, const std::vector<NsResolution>& all);

// ------------------------------------------------------------------ editing (validating)
// All functions edit `doc` in place and return false (+ Turkish *err) without changing it on failure.
// The UI applies them to a copy and commits the copy as ONE undo step (undo/redo/save restore everything).
namespace ns {
struct Ref {
  NsMemberKind kind = NsMemberKind::Face;
  std::string name;                   // CURRENT persistent name in `r` (face / edge / body)
};
inline Ref face(const std::string& n) { return {NsMemberKind::Face, n}; }
inline Ref edge(const std::string& n) { return {NsMemberKind::Edge, n}; }
inline Ref body(const std::string& n) { return {NsMemberKind::Body, n}; }

// New selection (name: isValidSelectionName + unique, case-insensitive). `r` regenerated with `stopAfter`.
// color "" = next colour of the built-in palette. Empty `refs` creates an empty selection (status Empty).
bool create(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r,
            int stopAfter = -1, std::string* err = nullptr, std::string* newId = nullptr,
            const std::string& color = "", const std::string& description = "");
bool rename(Document& doc, const std::string& name, const std::string& newName, std::string* err = nullptr);
bool remove(Document& doc, const std::string& name, std::string* err = nullptr);
// Adds members; refs already covered by the selection's current resolution are skipped (no error).
bool addMembers(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r,
                int stopAfter = -1, std::string* err = nullptr);
// Removes CURRENT entities. A member whose resolution is partly removed (e.g. one piece of a split face) is
// replaced by members for its remaining pieces. A ref equal to a stored member ref (e.g. a lost member)
// removes that member.
bool removeMembers(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r,
                   int stopAfter = -1, std::string* err = nullptr);
// Explicit user re-bind of member `index` (e.g. a lost one) to a current entity.
bool rebindMember(Document& doc, const std::string& name, size_t index, const Ref& ref, const ModelResult& r,
                  std::string* err = nullptr);
bool setColor(Document& doc, const std::string& name, const std::string& color, std::string* err = nullptr);
bool setDescription(Document& doc, const std::string& name, const std::string& text, std::string* err = nullptr);
bool setExportStep(Document& doc, const std::string& name, bool on, std::string* err = nullptr);
// Move a selection to position `index` (order = STEP name priority when a face is in several selections).
bool move(Document& doc, const std::string& name, size_t index, std::string* err = nullptr);
// A body was renamed (creator's bodyName): Body-member refs "<from>" and pieces "<from>:k" become "<to>[:k]".
// Returns the number of rewritten members. (Face/edge refs are feature-based and never change.)
int renameBodyRefs(Document& doc, const std::string& from, const std::string& to);
// First free "<base><k>" name (k = 1, 2, ...).
std::string suggestName(const Document& doc, const std::string& base = "Grup");
} // namespace ns

// ------------------------------------------------------------------ notifications
struct NsChange {
  enum class Kind { Added, Removed, Renamed, MembersChanged, PropertiesChanged, StatusChanged, FacesChanged };
  Kind kind = Kind::StatusChanged;
  std::string id, name, oldName;
  NsStatus oldStatus = NsStatus::Empty, newStatus = NsStatus::Empty;
  std::vector<std::string> oldFaces, newFaces;
  std::string diagnostic;
};
const char* toString(NsChange::Kind k);

// Stateful observer: call update() after every regeneration and after every document change (edit, undo,
// redo, open). It resolves all selections and notifies listeners about every difference to the previous call
// (keyed by NamedSelection::id, so a rename is reported as Renamed, not Removed+Added).
class NamedSelectionManager {
 public:
  using Listener = std::function<void(const NsChange&)>;
  int subscribe(Listener l);
  void unsubscribe(int token);

  const std::vector<NsResolution>& update(const Document& doc, const ModelResult& r, int stopAfter = -1);
  void clear();                                         // forget state: next update reports every selection as Added

  const std::vector<NsResolution>& resolutions() const { return res_; }
  const NsResolution* find(const std::string& nameOrId) const;   // name (case-insensitive) or id
  std::vector<std::string> names() const;                         // document order
  std::vector<std::string> selectionsOfFace(const std::string& faceName) const;
  std::vector<std::string> selectionsOfFace(const ModelResult& r, const TopoDS_Face& f) const;

 private:
  struct Snap {
    std::string name, members, props;
    NsStatus status = NsStatus::Empty;
    std::vector<std::string> faces;
  };
  std::vector<NsResolution> res_;
  std::map<std::string, Snap> prev_;                    // id -> last state
  std::vector<std::string> order_;
  std::map<std::string, std::vector<std::string>> byFace_;
  std::map<int, Listener> listeners_;
  int next_ = 1;
  bool primed_ = false;
};

// ------------------------------------------------------------------ STEP export with names
struct StepExportOptions {
  bool namedSelections = true;        // faces/edges of selections (exportStep=true) get the selection name;
                                      // a face in several selections gets the FIRST one in document order
  bool persistentFaceNames = false;   // faces in no selection get their persistent name ("Extrude1.side.e5")
  bool bodyNames = true;              // solid / product names = body names (Body-member selections override)
  bool colors = true;                 // selection colour written as face colour
  bool asciiNames = true;             // CFD/FEA friendly: Turkish letters transliterated (ş->s, ı->i ...), other
                                      // characters outside [A-Za-z0-9_.-] -> '_' ; colliding names get "_2", "_3"
  std::vector<std::string> bodies;    // export only these bodies (empty = all)
};
// The STEP name actually written for a selection name (asciiNames rule above; collisions not considered).
std::string stepName(const std::string& name, bool ascii = true);
bool exportSTEPNamed(const Document& doc, const ModelResult& r, const std::string& path,
                     const StepExportOptions& opt = {}, std::string* err = nullptr);

// Read back names from a STEP file (XCAF): solids/products and named faces (with their geometry).
struct StepNamedFace {
  std::string name;
  TopoDS_Face face;
};
struct StepNames {
  std::vector<std::string> bodies;    // names of the top-level shapes
  std::vector<StepNamedFace> faces;   // every named face sub-shape
  TopoDS_Shape shape;                 // all transferred geometry (compound)
};
bool readSTEPNames(const std::string& path, StepNames& out, std::string* err = nullptr);

} // namespace pc::model
