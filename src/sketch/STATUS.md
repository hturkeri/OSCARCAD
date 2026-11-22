READY
# pc_sketch — status (Agent A)

Implements `pc/SketchSolver.h` (interface unchanged). Files: `SketchSolver.cpp` (solve/DOF entry
points), `SolverSystem.{h,cpp}` (internal equation system, AD Jacobians, LM solver, rank analysis),
`Profile.cpp` (buildProfile, standardFrame). Tests: `tests/test_sketch.cpp`.

## solve()
* Unknowns: all point coords except origin id 0, plus Circle radii (arc radius implied by
  |start-center|; implicit arc equation |end-center| = |start-center| always added).
* All 22 constraint types per SketchData.h. Exact Jacobians (forward-mode dual numbers).
  Parallel/Perpendicular = normalized cross/dot, Angle = atan2 (wrapped), Tangent line-circle =
  signed distance = ±r (sign from current config), circle-circle internal/external chosen from the
  current config; endpoint tangency (shared point id or Coincident-linked endpoints) uses
  radius ⟂ line / centers-joint collinear (well conditioned).
* Damped Gauss-Newton/LM with minimum-norm steps (dx = -Jᵀ(JJᵀ+μI)⁻¹r, sparse LDLT): under-constrained
  geometry moves minimally. Step limiting; if the direct solve fails or flips a branch (line reversed,
  arc sweep jump > 180°, radius sign), continuation ramps dimensional targets from measured values
  (8, then 32 sub-steps). Tolerance 1e-9 × sketch scale, then polished. Deterministic.
* DOF = unknowns − rank(J) (Gram-Schmidt, rel. tol 1e-8). Status: Ok (dof 0, "Tam tanımlı"),
  UnderConstrained ("N serbestlik derecesi kaldı"), OverConstrained (converged, redundant
  constraints listed in `conflicting` — the LATER constraint of a dependent set is reported; geometry
  IS updated, `converged==true` but `ok()==false` per contract), Inconsistent (geometry left
  untouched; `conflicting` = constraints whose removal makes it solvable, "Çelişen kısıtlar: 13, 15"),
  Failed (bad refs / expression errors / non-positive Distance/Length/Radius/Diameter; geometry untouched).
* Fixed points keep their exact stored coordinates. Arc `radius` field = |start-center| after solve.
* degreesOfFreedom(): solves a copy; if expressions fail, uses currently measured values.
* Performance: 60-entity fully dimensioned sketch ≈ 1-2 ms.

## buildProfile()
* Non-construction entities; connectivity by shared point ids and coordinates within 1e-6.
  Lines, full circles, CCW arcs on the frame (shared TopoDS_Vertex per node, wires built with
  BRep_Builder, faces with BRepBuilderAPI_MakeFace on gp_Pln(frame)).
* Nesting by exact winding numbers (arcs handled analytically): even depth = face, odd = hole of its
  parent; islands inside holes become new faces. Face normal == frame.Direction() (left-handed frames
  handled too). `edges` holds the exact TopoDS_Edge objects found in the faces (Prism::Generated works).
* Errors (ok=false, Turkish message, valid faces still returned): "Açık kontur: nokta 7 (bağlantısız
  kenarlar: ...)", "Dallanan kontur (3 kenar): nokta N", "Kesişen eğriler: varlık A ve B" (analytic
  line/arc intersection incl. overlaps and touching), degenerate lines/arcs, unsolved arcs.
* freePoints / freePoints2d: points not referenced by ANY entity (construction included), origin
  excluded, ordered by (u rounded to 1e-6, then v). Sketches with only free points: ok, zero faces.

## Limits
* Regions split by a shared edge (e.g. a rectangle with a middle line, degree-3 nodes) are reported
  as "Dallanan kontur" and not turned into faces.
* Line-line Tangent and line/arc Equal are rejected (Failed).
* Pitfall for callers: `Sketch::addCircle(0, 0, 5)` with int literals resolves to
  `addCircle(int center, double r, bool)`; use doubles (`addCircle(0.0, 0.0, 5.0)`).

## Verification
* `tests/test_sketch.cpp`: 32 tests, all passing (every constraint type, W 80->200->30 with orientation
  kept, rounded rectangle with endpoint tangencies under big W/H/R changes, bracket.pcad sketches DOF 0,
  under/over-constrained, conflicts, profiles with holes/islands/slots/left-handed frames, errors,
  prism Generated(edge), 60-entity performance).
* Re-built against current pc_model: test_model 21/21 passing.
