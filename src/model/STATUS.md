READY v2
# pc_model — status (Agent B / B2)

Implements `pc/Model.h` (interface unchanged). Files: `Regen.cpp` (regeneration engine, all
features, bodies, datum planes), `Naming.cpp` + `pc/ModelNaming.h` (persistent-naming toolkit, internal / not frozen),
`Export.cpp` (mass properties, STEP, STL). Tests: `tests/test_model.cpp` (v1, 23 tests) and
`tests/test_model_v2.cpp` (v2, 24 tests). Samples: `samples/model_flange.pcad`, `samples/v2_multibody.pcad`.
`regenerate(bracket.pcad)` takes about 40-60 ms (target < 300 ms).

## v2 summary (datum planes + multi-body)
* `ModelResult::bodies`: every body in creation order. Each `BodyInfo.shape` is ONE TopoDS_Solid and is a
  sub-shape of `ModelResult::shape`. `faces` = sorted persistent face names of that body.
  `ModelResult::shape` is the solid itself when there is one body (exactly as v1), otherwise a compound of
  the body solids (same order as `bodies`). `body(name)` and `bodyOfFace(face)` are implemented.
* `ModelResult::datumPlanes`: one entry per successfully evaluated Plane feature. `frame`: Location = origin,
  XDirection = u, YDirection = v, Direction = normal (right-handed, u x v = normal). `displaySize` =
  max(10, 0.6 * largest bbox extent of the final shape) (50 when there is no body).
* Face names are globally unique across all bodies (the contract is unchanged). Edge names are computed on
  the whole result, so an edge name also identifies its body.
* v1 documents behave as before (one body named after the first solid feature, e.g. "Extrude1").

## Plane feature (FeatureType::Plane) — exactly as in pc/Feature.h
* `edgeNormal`: refs[0] edge; `t` (default 0) = normalised arc length along the curve parameter direction
  (BRepAdaptor_Curve, orientation ignored). origin = point at t, normal = tangent, u = OnFace rule.
  t must be in [0,1]. Works on lines, arcs, circles, splines.
* `edgeAngle`: refs[0] straight edge; refs[1] optional planar reference face (default: the adjacent planar
  face whose name sorts first). `angle` (deg, default 0), `t` (default 0.5). n0 = reference outward normal
  (component perpendicular to the edge), rotated by +angle about the edge curve direction (right-hand rule).
  u = edge curve direction. The plane contains the edge.
* `twoEdges`: refs[0], refs[1] straight, coplanar edges. origin = midpoint of refs[0], u = its direction,
  normal = normalize(d0 x (m1 - m0)), fallback d0 x d1. Errors for collinear and skew edges.
* `faceOffset`: refs[0] planar face; frame = OnFace frame rule of that face.
* Modifiers, applied in this order: `rotate` (deg; u,v rotated about the normal, right-hand rule), then
  `flip` (normal and v reversed, u kept), then `offset` (along the FINAL normal).
* Refs resolve by persistent name in the current result (all bodies). A split edge/face uses its first piece
  (":1", sorted by name) and the status gets a warning "'<ref>' bölünmüş, kullanılan: <piece>".
* FeatureInfo: `sketchFrame` = the datum frame; values `t` (edgeNormal/edgeAngle), `angle` (edgeAngle),
  `offset`, `rotate`, `flip`. createdFaces empty, toolShape null. Planes follow their edges when parameters
  change (they are re-evaluated on every regeneration).
* Errors (status ok=false, "<fid>: ..."): "kenar bulunamadı '<ref>'", "yüz bulunamadı '<ref>'",
  "kenar düz değil '<ref>'", "yüz düzlemsel değil '<ref>'", "t 0 ile 1 arasında olmalı (..)",
  "kenarlar aynı doğru üzerinde, düzlem tanımsız", "kenarlar aynı düzlemde değil (aykırı kenarlar)",
  "referans eksik", "iki kenar referansı gerekli", "kenara bitişik düzlemsel yüz yok '<ref>'",
  "referans yüz normali kenara paralel", "geçersiz düzlem türü '<kind>'", "referans için gövde yok".

## OnPlane sketches
* frame = datum frame of `planeRef`, translated by the sketch offset along the datum normal.
  Works in `regenerate` and in the public `sketchFrame()` (uses `partial.datumPlanes`).
* Errors: "düzlem referansı eksik (planeRef)", "düzlem bulunamadı '<id>'", "'<id>' bir düzlem özelliği değil",
  "düzlem '<id>' çizimden sonra tanımlı", "düzlem '<id>' kullanılamıyor (bastırılmış veya hatalı)".

## Bodies (BODIES contract)
* op=new (Extrude/Revolve/Pattern): new body named prop `bodyName` or the feature id. The first solid
  feature always creates a body. Errors: "gövde adı '<n>' zaten kullanılıyor", "gövde adı virgül içeremez".
* join/cut/intersect act on `targets` (comma-separated body names; a name that was split selects all its
  pieces, with a warning). Unknown target: "gövde bulunamadı '<n>'".
* AUTO (empty `targets`), "touch" = distance <= 1e-5 or one inside the other:
  - join: all touched bodies + tool are fused into ONE body named after the OLDEST touched body (keeps its
    position and createdBy). Touching nothing -> new body (`bodyName` if given, else the feature id).
  - cut: every touched body is cut separately. Touching nothing -> warning "araç hiçbir gövdeye değmiyor
    (değişiklik yok)", ok=true.
  - intersect: touched bodies intersected; untouched bodies are removed (warning).
  - A body that becomes empty is removed (warning "gövde '<n>' tamamen kaldırıldı"); error only if no body
    remains ("işlem sonucu boş gövde (<op>)", as in v1).
* keepTool=1 (cut/intersect, also Hole and Pattern): a COPY of the tool is kept as a new body named
  `bodyName` or "<fid>.tool"; its faces are named "<fid>.tool.<rest>" where <rest> is the tool face name
  without the "<fid>." prefix (e.g. "Extrude5.tool.side.e2", "Extrude5.tool.start").
* Hole and Pattern honour `targets` (Hole: through length and materialDepth are measured on the targets,
  or on all bodies in AUTO mode). Pattern: own prop `op` if given (op=new -> new bodies), else the
  source's op with new treated as join (v1 behaviour).
* Fillet/Chamfer/Shell act on the bodies owning the referenced edges/faces (other bodies untouched).
  Fillet/Chamfer `f<k>`/`c<k>` numbering is over all bodies of the feature (sorted by edge name).
  Shell without refs acts on `targets` (or every body). New FeatureInfo value for Fillet/Chamfer: `bodies`.
* Combine: `target` (one body), `tools` (comma list), op join|cut|intersect (default join),
  keepTools=0|1. Result keeps the target's name; consumed tool bodies disappear (their face names live on
  in the target). With keepTools=1 the boolean uses copies of the tools, and faces the copies contribute to
  the target are named "<CombineId>.<toolFaceName>" (e.g. "Combine1.Extrude1.side.e6").
  FeatureInfo: values `tools` (count), `keepTools`; toolShape = compound of the tool solids.
  Errors: "hedef gövde belirtilmemiş", "araç gövdesi belirtilmemiş", "gövde bulunamadı '<n>'",
  "hedef gövde '<n>' araç olarak kullanılamaz", "hedef gövde '<n>' bölünmüş, parçalardan birini seçin".
* Splits: a body that falls apart into disjoint solids becomes "<body>:1", "<body>:2" ... ordered by
  volume centroid (x, then y, then z); all pieces keep createdBy.

## Sample samples/v2_multibody.pcad (bracket + v2)
bracket.pcad unchanged (Extrude1 has bodyName "Braket"), then:
* Plane1 edgeNormal on `edge(Extrude1.end|Extrude1.side.e5)` (front top edge) t=0.5 -> origin (0,-D/2,T),
  normal +X, u=+Y, v=+Z. Sketch5 (OnPlane Plane1): flat-topped rib trapezoid, base from
  min(Rm, D/2-Db/2-rt-1) to the middle of the boss wall (D/2-Db/2+(Db-Dbore)/4), height min(Hr, Hb-1),
  top width rt outside the boss. Extrude3: symmetric, distance tr, op=join (AUTO) -> merged into "Braket".
  Fillet2: radius min(Rf, h/2) on the rib/plate and rib/boss concave edges (Fillet2.f0..f3).
* Plane2 edgeAngle on `edge(Extrude1.end|Extrude1.side.e6)` (right top edge), angle=Atab (30), t=0.5.
  Sketch6 (OnPlane Plane2): rectangle Wtab x Ltab outward from the edge. Extrude4: reverse, Ttab,
  op=new, bodyName "Tab".
* Sketch7 (OnPlane Plane2): circle Dp at (0,-Ltab/2). Extrude5: cut, reverse, Ttab, targets "Tab",
  keepTool=1, bodyName "Tapa" -> hole in Tab, plug kept as body "Tapa".
* Combine1: op=cut, target "Tab", tools "Braket", keepTools=1 -> trims the part of the tab that overlaps
  the bracket (new tab face "Combine1.Extrude1.side.e6").
* Final bodies: Braket (Extrude1), Tab (Extrude4), Tapa (Extrude5). Exposed parameters: group "Nervür"
  (Hr 3..8, tr 2..8, rt 2..8, Rf 1.5..2.5), group "Kulak" (Atab 0..60, Ltab 15..60, Wtab 16..40,
  Ttab 1..10, Dp 2..12) plus the bracket ones. Every v2 input min/max regenerates ok (tested).
* mfg (validateChange, default parameters): 0 errors / 4 inherent warnings (30° tab + plug overhang,
  zero gap of the deliberately touching bodies) ONCE the mfg analytic model handles OnPlane sketches.
  Currently `src/mfg/AnalyticModel.cpp` treats OnPlane sketches as XY (no OnPlane branch -> standardFrame),
  which yields 2 phantom CNC.SHARP_INTERNAL_CORNER errors; fix described in docs/CONTRACT_REQUESTS.md.

## v1 features (unchanged)
* Sketch: frame = standardFrame(kind, offset), OnFace (planar face, outward normal) or OnPlane.
* Extrude (normal/reverse/symmetric, draft, through), Revolve, Hole (simple/counterbore/countersink),
  Fillet/Chamfer (edge names or `face:<name>`), Shell, Pattern (linear/circular) as documented before.
* Errors: feature skipped, status.ok=false, Turkish message "<fid>: ...". Nothing throws. At the end each
  body is checked with BRepCheck_Analyzer and repaired with ShapeFix_Shape when invalid (names remapped).

## Persistent naming
* Tool faces come from the creation history (`.side.e<id>`, `.rev.e<id>`, `.start`, `.end`, hole segment
  names). Names are carried through booleans, fillets, chamfers, drafts by Modified/IsDeleted; unchanged
  faces keep their name. Body (earlier) names win over tool names, then the lexicographically smallest.
  A name carried by several faces (also across bodies) becomes `name:1..n` ordered by centroid (x, y, z).
  Unnamed faces get `<fid>.u<k>`.
* Edges follow the contract (`edge(a|b)`, `edge(a)`, `:k` by midpoint). Degenerated edges are skipped.

## FeatureInfo.values keys
* Sketch: offset. Extrude: distance, draft, through, depth (cut). Revolve: angle.
* Hole: diameter, through, depth, tipAngle, cbDiameter, cbDepth, csDiameter, csAngle, csDepth, locations,
  materialDepth. Fillet: radius, edges, concaveEdges, bodies. Chamfer: distance, edges, concaveEdges, bodies.
* Shell: thickness, removedFaces. Pattern: count, spacing / angle, stepAngle.
* Plane: t, angle, offset, rotate, flip. Combine: tools, keepTools.
* createdFaces = sorted names (over all bodies) that start with "<fid>.". Extrude/Revolve/Hole/Plane also
  get sketchFrame (Plane: the datum frame).

## Limits
* Pattern instances are transformed copies of the source tool.
* Draft on a symmetric extrude is one-sided. Revolve axis U/V passes through the sketch origin.
* A touch test uses BRepExtrema (bbox pre-filter); a tool that only touches a body face counts as touching
  (join merges it; a cut is then a no-op on that body).
* Review fix (v2): a cut/intersect with keepTool=1 whose targets all vanish no longer fails ("boş gövde"): the
  kept tool body remains (tests/test_review_v2.cpp).

## Enjektör extensions (Nov 2026, samples/enjektor_09DB01*.pcad, tests/test_enjektor.cpp)
* Plane `kind=axisAngle` (no refs, works without a body): plane containing a world axis (`axis` = X|Y|Z, default Z),
  rotated by `angle` about it; origin (0,0,0), v = axis, u = base u rotated, normal = u x v (Z, angle 0 = standard XZ).
  Then the common modifiers (rotate, flip, offset). Error: "geçersiz eksen '<a>'". FeatureInfo value `angle`.
  Used for the tangential swirler slot (meridian plane at the slot azimuth, sketch offset = slot offset) and the
  filter windows (partial revolve starting at an azimuth).
* Pattern: `count` = 1 is accepted and adds nothing (a count parameter can go down to 1); count < 1 is an error
  ("adet en az 1 olmalı").
* Combine `largestOnly=1`: only the largest solid of the result is kept; FeatureInfo value `droppedPieces`.
* Feature prop `mfg=0` on a body-creating feature is ignored by the model (consumed by pc_mfg: reference body).
* Regeneration of the 30-feature injector assembly (6 bodies, 2 multi-tool Combines): about 0.6 s.

## Review Nov 2026 (independent reviewer)
- `regenerate()` / `massProperties()` call `pc::resetSimdUpperState()` (core `pc/SimdState.h`): GUI code left dirty AVX state that
  made OCCT 2.5-3x slower inside the designer/runner. `PC_MODEL_PROFILE=1` prints per-feature timings on stderr.
- `Plane show=0` (construction plane) documented in `pc/Feature.h`; geometry unaffected.

## Named selections + Transform (Nov 2026, docs/named_selections/MODEL.md)
* `ModelResult::faceLineage` (final face -> every persistent name it carried or inherited, incl. losing candidates of
  merges and parents of split pieces) and `ModelResult::removedBy` (vanished name -> removing feature), recorded in
  `commit()`. Consumed by `pc/NamedSelections.h` (resolve/edit/notify/STEP with names, `NamedSelections.cpp`).
* `exportSTEPNamed()` (XDE: face names on ADVANCED_FACE, body products, colours); pc_model now links the XDE
  toolkits (TKXCAF, TKLCAF, TKCAF, TKXDESTEP ...). The old `exportSTEP()` is unchanged.
* `FeatureType::Transform` (CR-3): bodies, axis/angle about (ox,oy,oz), then tx,ty,tz; copy=1 adds copies
  "<body>.<fid>" with faces "<fid>.<face>". Moved faces keep their names. Tests: tests/test_named_selection.cpp.
