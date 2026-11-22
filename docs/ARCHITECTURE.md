# PersistCAD — Architecture & Team Contract

SpaceClaim-like parametric part modeler (C++17, Qt 6.4, OpenCASCADE 7.6) whose main deliverable is
**"Save as App"**: a model + exposed parameters + manufacturing rules saved as a standalone
configurator app. In the app (and in the designer) every parameter change is first validated by
**additive-manufacturing (DfAM) and conventional-manufacturing (DfM) analytic checks**; offending
parameters get warnings with suggested corrected values.

## Modules (dependency order)

| Dir | Library / exe | Owner | Depends on |
|---|---|---|---|
| src/core | pc_core | Orchestrator (done) | nlohmann_json |
| src/sketch | pc_sketch | Agent A | core, OCCT |
| src/model | pc_model | Agent B | sketch, core, OCCT |
| src/mfg | pc_mfg | Agent C | model, core |
| src/apppkg | pc_apppkg | Agent E | mfg, model, core |
| src/viewer | pc_viewer | Agent D | Qt, OCCT |
| src/appui | pc_appui | Agent E | apppkg, Qt |
| src/designer | persistcad (exe) | Agent D | all |
| src/runner | persistcad_runner (exe) | Agent E | apppkg, viewer, mfg, model |
| tests | test_<module> (auto-globbed) | each owner | |

Public headers live in `src/<module>/pc/*.h` and are **frozen contracts**. Do not change another
module's header. If a contract is genuinely insufficient, add a NEW header in your own module, or
write the request into `docs/CONTRACT_REQUESTS.md` and work around it.

Core data model: `pc/Parameter.h`, `pc/SketchData.h`, `pc/Feature.h` (feature property schema +
**persistent naming contract**), `pc/MfgSettings.h`, `pc/Document.h` (JSON `.pcad`).
Sample model: `samples/bracket.pcad` (plate + fillets + 4 through holes + boss + blind bore).

## Rules for agents
* Only edit files inside your own module directories, your own `tests/test_<module>.cpp`, and
  (if needed) new files under `samples/` prefixed with your module name.
* Each agent builds in its OWN build dir to avoid clashes:
  `cmake -S . -B build-<module> -G Ninja && ninja -C build-<module> -j1 <targets>`
  (machine has 2 cores shared by several agents — always `-j1`).
  Agents without GUI work may configure with `-DPC_BUILD_GUI=OFF`.
* Other modules may still be stubs while you work (files starting with `// STUB`). When your module
  is functional, write `src/<module>/STATUS.md` with the first line `READY` plus a short summary.
  Re-build against others' latest code before finishing.
* GUI screenshots: `xvfb-run -a -s "-screen 0 1600x1000x24" <exe> ...` (Mesa llvmpipe, GL 4.5).
* UI strings are Turkish. Code/comments English.
* Units: mm, degrees.

## v2 — Datum planes & multi-body (SpaceClaim parity, Nov 2026)

Contract additions (orchestrator-owned, already compiled):
* `pc/Feature.h`: `FeatureType::Plane` (datum plane: edgeNormal | edgeAngle | twoEdges | faceOffset) and
  `FeatureType::Combine` (body-to-body boolean); BODIES contract (op=new creates a body, `targets`,
  `keepTool`, `bodyName`, merge/split naming).
* `pc/SketchData.h`: `SketchPlaneKind::OnPlane` + `SketchPlane::planeRef` (sketch on a datum plane).
* `pc/Model.h`: `BodyInfo`, `DatumPlane`, `ModelResult::bodies`, `datumPlanes`, `body()`, `bodyOfFace()`.
* `pc/MfgChecks.h`: `Issue::body`.
* `pc/ShapeViewer.h`: `DatumPlaneDisplay`, `setDatumPlanes`, `setFaceColors`, `setHiddenFaces`, `datumPicked`.

v2 owners: B2 model (Opus), C2 mfg (Sonnet), D2 viewer+designer (Sonnet), E2 runner (Sonnet).
New tests go in NEW files `tests/test_<module>_v2.cpp` (auto-globbed). Existing tests must stay green.
A module is ready for others when `src/<module>/STATUS.md` first line is `READY v2`.
