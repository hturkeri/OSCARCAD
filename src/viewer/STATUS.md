READY v3
ShapeViewer (GL 3.3 core, MSAA, Mesa llvmpipe OK): BRepMesh tessellation, per-face ID picking (facePicked/faceHovered),
ctrl+click edge picking (screen-space), highlights, ghost, sketch-plane grid, axis triad, orbit/pan/zoom-to-cursor,
standard views, renderToImage via FBO (forces context creation if widget not yet shown).
Smoke test: build-gui/bin/pc_viewer_demo out.png (under xvfb-run).
Ghost (QA fix): drawn after the opaque model as a clean overlay — depth-only pre-pass with back-face culling (nearest layer
only), uniform warm red alpha 0.25 with depth writes off, faint visible edges, plus faint model-occluded edges so a smaller
candidate stays visible. Blending uses glBlendFuncSeparate so framebuffer alpha stays 1 (the old GL_SRC_ALPHA blend wrote
alpha < 1, which grab()/compositing turned into saturated blue/cyan). GL objects are released on context destruction.

v2 (D2): setDatumPlanes (translucent bluish square, thick outline, normal arrow, screen-space label texture; depth-tested,
depth writes off, separate-alpha blending so framebuffer alpha stays 1; occluded outline drawn faint), datumPicked on left
click (planes are rendered into the ID pick buffer after the faces, so a plane wins only where no face is closer; hovering
a plane highlights it orange), setFaceColors (per-face base colour, consecutive equal colours drawn in one call),
setHiddenFaces (faces + edges whose adjacent faces are all hidden are skipped in colour AND pick passes). renderToImage
includes all of it. Smoke: `pc_viewer_demo out.png x` (extra arg = v2 scene). Planes do not influence fit-to-scene.
Review fix: edges of hidden bodies are no longer returned by edgeAt()/Ctrl+click picking.

v3: datum planes sized from local context (pc/DatumSizing.h: clamp(max(0.6*refEdgeLen|refFaceDiag, 0.15*diag), 0.35*diag)), subtler
rendering (alpha ~0.12, thin outline, label offset outside the corner); sceneBox/fitAll include planes and the ghost.

v4: SECTION VIEW (view-only, model untouched). API (additive): setSectionPlane(enabled, origin, normal) [keeps (p-o).n >= 0],
setSectionPreset(None|XZ|YZ|XY|Quarter, offset -1..1 of bbox half-extent, flip), sectionPreset/sectionEnabled/sectionPosition,
sectionPresetFromString, signal sectionChanged, setTransparentFaces(names, opacity). Technique: GPU clip planes (gl_ClipDistance in
the mesh/flat shaders; quarter cut = union of two half-spaces drawn as two disjoint clip passes) + per-solid stencil-parity capping
(Tessellation records FaceRec::solid; for each solid crossing a plane: INVERT stencil of the clipped solid, then a plane quad where
stencil != 0) + CPU cut contour (triangle/plane slicing -> GL_LINES) + corner line for the quarter cut. Caps: body colour, mostly
flat darker tone, world-anchored 45/135 deg hatch (pitch snapped to 6.5..13 px). Clipped: faces, edges, highlights, hover, ghost,
translucent faces; NOT clipped: datum planes, sketch grid. Picking: clipped faces + caps rendered as id 0 (a cap is not pickable and
occludes what is behind); edge picking skips removed segments. Presets are re-evaluated on setModel. Hidden bodies get no cap.
pc/SectionControls.h: shared widget (checkable "Kesit" action + preset combo + offset slider + Ortala + Ters çevir), overlay mode
(designer, floats top-right in the viewer) or inline mode (runner toolbar); applySpec("xz|yz|xy|quarter|none") for CLIs.
Limitations: caps need closed solids with a watertight tessellation (open shells / faces whose meshing failed are clipped but not
capped); translucent bodies show only their nearest layer and their caps stay opaque; hatch pitch jumps by x2 while zooming.
Test: tests/gui/gui_harness_v4.cpp (ctest gui_harness_v4).

## Review Nov 2026 (independent reviewer)
- Fit uses the visible mesh vertices (not bbox corners) and re-fits on resize until the user zooms/pans; display tessellation
  0.05 % of the diagonal / 0.12 rad; tessellation clears dirty AVX state first (see core `pc/SimdState.h`).

## SC-UX (WP-B, Nov 2026) — additive API, runner unaffected (default NavigationScheme::Legacy)
- Camera orientation is a quaternion (right/up/eye = rot*X/Y/Z); `setView` and the legacy turntable drag go through az/el,
  so legacy behaviour is unchanged. Free rotation for the SpaceClaim scheme; `setPlanView(o,u,v,fit)` works for any plane.
- `setNavigationScheme(SpaceClaim)`: middle drag = spin, Shift+middle = pan, Ctrl+middle = zoom, wheel = zoom to cursor,
  left drag = `setLeftDragMode` (None by default), plain click picks the edge within 6 px (occlusion-checked) before the face,
  `picked(name, kind, modifiers, clickCount, pos)` for every click (+ `doubleClicked` / `tripleClicked`), Shift+middle double
  click = `setViewToFace`, hover pre-highlights faces AND edges (`hovered`).
- Projection helpers: `worldToScreen`, `screenRay`, `planeToScreen` (exact affine), `pixelsPerMm`; camera: `setHomeView`,
  `fitToBox`, `previousView`/`nextView` (20-step history), `cameraChanged` signal; picking: `pickAt(px, kinds, &kind)`,
  `setPreselection`; `faceCentroid`, `displayedFaceNames`, test properties `cameraAz`/`cameraEl`/`halfH`/`target`/`ghostVisible`.
- Faces named `sketch:<sid>:r<k>` (sketch regions) default to light blue + translucent. The sketch grid step follows the zoom
  (~28 px minor, majors every 5) and covers the view (also oblique).
- Tests: tests/gui/gui_harness_sc_tools.cpp (navigation, plan view, oblique sketching, picking).
