# Named Selections (Adlandırılmış Seçimler) — model, persistence, robustness

Status: implemented (model + persistence + STEP/runner + designer UI). UI: SpaceClaim "Groups / Named Selections"
— Ctrl+G on 3D faces / edges / bodies, Gruplar > Adlandırılmış Seçimler, viewport / Properties "Ad", labels
(`src/designer/MainWindowNs.cpp`, `NamedSelectionPanel.*`; manual: KULLANIM_KILAVUZU §3.6). Tests:
`tests/test_named_selection.cpp` (25 tests, see §8), GUI `tests/gui/gui_harness_named_sel.cpp` (shots in `shots/`).

| What | Where |
|---|---|
| Data (pure, JSON) | `src/core/pc/NamedSelection.h`, `Document::namedSelections`, `src/core/Document.cpp` |
| Resolution, editing, notifications, STEP | `src/model/pc/NamedSelections.h`, `src/model/NamedSelections.cpp` |
| Face lineage produced by regeneration | `ModelResult::faceLineage`, `ModelResult::removedBy` (`src/model/pc/Model.h`, `Regen.cpp: commit()`) |
| Runner / app | `src/runner/RunnerCore.h` (`namedSelectionsReport`, `exportStepWithNames`), CLI `--named-selections`, `--export-step`, `--body` |
| Transform feature (CR-3) used by "Move" | `pc/Feature.h` (`FeatureType::Transform`), `Regen.cpp: doTransform()` |

## 1. Guarantee

After the user names a face, **no later operation changes the name or silently moves it to another face**:
extrude/pull new bodies from sketches on its edges or faces, cuts, holes, fillets, chamfers, shells, patterns,
booleans (Add/Subtract/Separate/Combine), body splits, Move/Transform, revolve, edits of any earlier parameter,
inserting features earlier, suppress/unsuppress, deleting features, rollback, undo/redo, save/reopen,
Save-as-App and runner `--set`.

| What happens to the named face | Result | Status |
|---|---|---|
| untouched, trimmed, extended, holed, moved/rotated | the same face | `Ok` (Tamam) |
| split by a later (or inserted earlier) feature | **all** pieces that came from it | `Split` (Bölündü) |
| merged with other faces / named piece re-joined its siblings | the merged face | `Merged` (Birleşti) |
| consumed / deleted (cut away, fillet ate an edge, intersect, ...) | nothing; diagnostic names the feature | `Lost` (Kayıp) |
| creating feature suppressed / failed / after the rollback bar | nothing, comes back by itself | `Suppressed` / `Failed` / `NotYet` |
| surface type changed (e.g. sketch line edited into an arc) or positional piece ambiguous | resolved, flagged | `Suspect` (Şüpheli) |

A lost member is **never** re-bound automatically. `NsMemberResult::suggestions` lists look-alike faces
(signature score) for the UI to offer; only an explicit `ns::rebindMember` (user action) changes the reference.

## 2. Data model (`pc/NamedSelection.h`)

```cpp
struct NamedSelection {
  std::string id;          // "ns<k>", stable, never reused in a document (tree keys, notifications)
  std::string name;        // unique, case-insensitive ("Inlet" == "inlet": meshers fold case); UTF-8 ok
  std::vector<NamedSelectionMember> members;   // kind Face | Edge | Body
  std::string color;       // "#RRGGBB" (palette default), also written as STEP face colour
  std::string description;
  std::string createdAfter; int createdAt;     // last regenerated feature when created (informational)
  bool exportStep;         // write into STEP
};
struct NamedSelectionMember {
  NsMemberKind kind; std::string ref;   // persistent name at capture: "Extrude1.end", "Extrude1.end:2",
                                        // "edge(Extrude1.end|Extrude1.side.e5)", body "Braket"
  NsSignature sig;                      // surface type, normal/axis, centroid, area/length/volume, radius,
                                        // neighbour face names, owning body
  std::vector<std::string> anchors;     // Body members: face names of the body (merge tracking)
};
```

`.pcad`: top-level `"namedSelections": [...]`. The key is written only when non-empty, so documents without
selections serialise byte-identically to before, and old files load unchanged. Missing `id` / signature are
tolerated (hand-written files); duplicate or invalid names are load errors.

## 3. How resolution works

1. **Lineage.** During regeneration every `commit()` records, for each resulting face, the union of the lineages
   of **every** candidate that proposed a name for it (OCCT `Modified()` images, unchanged faces, tool faces) — not
   only the winning name. So a split piece `Extrude1.end:2` carries `{Extrude1.end, Extrude1.end:2}`, a face formed
   from body and tool faces carries both names, and a face that vanished is recorded in `removedBy` with the
   feature that removed it. `ModelResult::faceLineage` = final face -> sorted lineage.
   **Pull of a face** (designer Çek on a planar face -> `Sketch` on the face + `Extrude` with `pullFace=<face>`):
   when the Add / Cut consumes the pulled face, the new `<fid>.end` face is that face moved, so it inherits the
   pulled face's whole lineage (`removedBy` entry dropped). A selection on the pulled face therefore resolves to the
   new outer face (Add) or the new bottom (Cut); with "Birleştirme yok" the original face is untouched and keeps it.
   See `docs/spaceclaim_ux/PULL_FACE_FIX.md`.
2. **Non-positional refs** (`Extrude1.end`, `Hole1.h0.wall`, `Revolve1.rev.e6`, ...): the member resolves to every
   final face whose lineage contains the ref. This is exact, history-based, and independent of geometry.
3. **Positional refs** (`<P>:k` split pieces, `<fid>.u<k>` fallback faces — their numbering is a centroid order
   and can change): faces descending from `P` are grouped by their child token `P:j`; the group closest to the
   stored signature wins, and is refined into its own sub-pieces when one matches clearly better (a piece that an
   inserted earlier feature cut). Near ties -> `Suspect`. If the pieces re-joined, the parent face -> `Merged`.
4. **Edges** `edge(A|B)[:k]`: A and B are resolved as faces (lineage), the member = current edges shared by a face
   of A and a face of B (`:k` picks the nearest midpoint). Fillet/chamfer on that edge -> `Lost`.
5. **Bodies**: exact name, else split pieces `name:k` (`Split`), else the bodies now holding its anchor faces
   (`Merged`, e.g. consumed by Combine/join), else `Lost`.
6. **Diagnostics** for missing refs: creating feature deleted / suppressed / failed / after rollback, else
   `removedBy`, else "geometry no longer produced".

Aggregate status of a selection: `Empty`; all members resolved -> worst of Ok < Split < Merged < Suspect; mixed ->
`Partial`; none resolved -> worst of NotYet < Suppressed < Failed < Lost.

## 4. API for the UI (`src/model/pc/NamedSelections.h`)

```cpp
// editing — validate, change `doc` only on success, Turkish *err; apply to a COPY and commit it as one undo step
ns::create(doc, name, {ns::face("Extrude1.end"), ns::edge(..), ns::body(..)}, r, stopAfter, &err, &newId, color, desc);
ns::rename(doc, name, newName, &err);           ns::remove(doc, name, &err);
ns::addMembers(doc, name, refs, r, stopAfter, &err);     // refs already covered are skipped
ns::removeMembers(doc, name, refs, r, stopAfter, &err);  // removing one piece of a split face keeps the others
ns::rebindMember(doc, name, memberIndex, ref, r, &err);  // explicit re-bind of a lost member
ns::setColor / setDescription / setExportStep / move(doc, name, index) / suggestName(doc, "Grup")
ns::renameBodyRefs(doc, from, to);              // call when a body is renamed (designer does it)

// resolution (pure, never throws)
NsResolution resolve(doc, sel, r, stopAfter);   std::vector<NsResolution> resolveAll(doc, r, stopAfter);
selectionsByFace(all) -> map<face name, selection names>
refreshSignatures(doc, r, all);   // optional; better piece tracking across long edit chains (no undo step needed)
captureMember / faceSignature / edgeSignature / bodySignature

// observer with change notifications (keyed by id: rename = Renamed, not Removed+Added)
NamedSelectionManager m;  int tok = m.subscribe([](const NsChange& c){ ... });
m.update(doc, r, stopAfter);   // after every regeneration AND every document change (edit, undo, redo, open)
m.find(nameOrId); m.names(); m.selectionsOfFace("Extrude1.end"); m.selectionsOfFace(r, topoFace);
// NsChange::Kind: Added, Removed, Renamed, MembersChanged, PropertiesChanged, StatusChanged, FacesChanged

// STEP with names (designer File > Export and runner export already use it)
exportSTEPNamed(doc, r, path, StepExportOptions{...}, &err);   readSTEPNames(path, out, &err);
```

`NsResolution`: `id, name, status, members[] {ref, status, faces, edges, bodies, diagnostic, suggestions},
faces/edges/bodies (union, current persistent names), diagnostic, rolledBack`. `statusLabel()` gives the Turkish
UI text, `isResolved()` tells whether entities are available for highlighting.

UI notes:
* Highlight: `res.faces` are keys of `ModelResult::faces` (viewer face ids). Use the selection colour.
* Properties panel of a face: `manager.selectionsOfFace(faceName)`.
* "Create from selection": pass the current face names exactly as the viewer reports them (pieces included).
* Rollback: pass the same `stopAfter` used for regeneration; members created after the bar show `NotYet`.
* Undo/redo/save: selections live in `Document`, so the existing snapshot undo stack and `.pcad` handle them;
  call `manager.update` after any document replacement.

## 5. STEP export

`exportSTEPNamed` writes an XDE (XCAF) document with `write.stepcaf.subshapes.name = 1`: every face of a selection
gets the selection name on its `ADVANCED_FACE` entity (read by Gmsh, ANSA, Fluent Meshing, STAR-CCM+, HyperMesh
as boundary/zone names), bodies become named products, selection colours become face colours. Options
(`StepExportOptions`): `namedSelections`, `persistentFaceNames` (name every other face with its persistent name),
`bodyNames`, `colors`, `asciiNames` (default **on**: `ş->s, ı->i, ç->c ...`, other characters outside
`[A-Za-z0-9_.-]` -> `_`, collisions get `_2`; `stepName()` shows the result), `bodies` (filter).
A face in several selections gets the first selection in document order (`ns::move` sets the priority);
`exportStep=false` excludes a selection.

Runner: `persistcad_runner app.pcapp --set W=120 --named-selections --export-step out.step [--body Braket]`
works headless (no display), prints the JSON report on stdout and writes the named STEP. The runner window's
STEP export uses the same names. Windows deployment: the runner additionally needs `TKXCAF, TKLCAF, TKCAF,
TKCDF, TKVCAF, TKV3d, TKService, TKXDESTEP` DLLs next to it.

## 6. Transform (Move) feature — CR-3

`FeatureType::Transform`: `bodies` (comma list, empty = all), rotation `axis` X|Y|Z through `ox,oy,oz` by `angle`
deg, then translation `tx,ty,tz`; `copy=1` adds moved copies (`<body>.<fid>`, faces `<fid>.<face>`). Moved bodies
keep body and face names, so selections follow them. The mfg analytic model ignores Transform (geometric
checks see the moved bodies) — mfg owner: replay it analytically if needed. Designer UI for Move is not built.

## 7. Limitations (by design or known)

* **Index-based names of the naming contract**: `Hole <fid>.h<k>` (ordered by sketch point u,v), `Fillet .f<k>` /
  `Chamfer .c<k>` (ordered by edge name), `Pattern .i<k>`. If an edit reorders holes or changes which edges a
  fillet uses, `h0` can denote another hole. Resolution flags this as `Suspect` when a sibling of the same family
  matches the stored signature much better than the named one (it never switches by itself).
* A face removed by **Shell** is turned into the rim face in its plane by OCCT; the name follows that rim
  (`Ok`), it is not reported lost.
* Editing a sketch so that the entity behind `side.e<id>` is deleted and redrawn creates a new face: the old name
  is `Lost` (correct — new geometry), re-bind it explicitly.
* Positional piece refs are chosen by geometry. After extreme jumps (piece moved further than its siblings) the
  choice can be wrong; ties are flagged `Suspect`. `refreshSignatures` after each accepted edit makes tracking
  incremental and robust.
* Renaming a **feature id** changes persistent names (not supported by the designer today).
* STEP: one name per face (first selection wins); edge names are written but few meshers read them.
* Split Body by a plane (SpaceClaim "Split Body") is not a model feature; body splits come from cuts.

## 8. Tests (`tests/test_named_selection.cpp`)

Data: name rules/editing/ids, save-reopen round trip, backward compatibility, malformed files, result without
lineage degrading to exact names.
Ops matrix (each asserts name, geometric identity of the face(s), status): new body pulled from a sketch on a
datum plane at the named face's edge; pull-join on the named face; sketch-on-named-face cut (split) and through
cut (body split); fillet and chamfer on its edge (edge member lost, other edge kept); shell removing another face;
hole through it with diameter edits; linear/circular pattern with count edits; Add/Subtract/Separate (keepTool)/
Combine join and cut; body split and body members; Move/Transform and copy; revolve angle/radius edits and a cut
splitting the revolved wall; inserting an earlier feature, suppress/unsuppress, rollback, delete; consumed face
lost + explicit re-bind; positional piece survives renumbering by an inserted earlier split and re-joins;
remove one piece / add members; index-based hole name reordered by a parameter -> `Suspect` + suggestion, never
switched; body rename (designer path) and body found through anchors; undo/redo snapshots + notifications; signature refresh over a 10-step edit chain;
STEP names/colours/body filter/Turkish names read back with geometry; Save-as-App package + real runner CLI
`--set` + named STEP. Randomized: bracket sample (40 random parameter sets: plate top, 4 hole walls, boss top,
bore, bottom checked geometrically), injector sample (random exposed parameters: orifice wall radius = d0/2,
swirler slot walls, body member), random history edit sequences (insert/suppress/param changes) on the plate.
