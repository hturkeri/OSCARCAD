# GUI end-to-end harness: drives the real designer MainWindow, the Save-as-App dialog and the runner
# window through widgets (QtTest input simulation + findChild). Built only with PC_BUILD_GUI.
#
# SC-UX P0: the designer sources are no longer listed per harness. Every harness links the pc_designer library
# (src/designer/sources.cmake globs src/designer/*.cpp and src/designer/tools/*.cpp), so new designer files need
# no edit here. A WP that adds a NEW harness appends ONE pc_add_gui_harness(...) call at the end of this file.
find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)
set(PC_R ${CMAKE_SOURCE_DIR}/src/runner)
set(PC_RUNNER_SOURCES ${PC_R}/RunnerCore.cpp ${PC_R}/RunnerWindow.cpp ${PC_R}/RunnerWindow.h ${PC_R}/RunnerCore.h)
find_program(PC_XVFB_RUN xvfb-run)
if(NOT PC_XVFB_RUN AND NOT WIN32)
  message(STATUS "xvfb-run not found: GUI harnesses are built but not registered with ctest")
endif()

# pc_add_gui_harness(<test name> SOURCES <files...> [RUNNER] [TIMEOUT <s>] [ARGS <argv...>] [DEFINES <defs...>])
#   target = pc_<test name>; ctest runs it under xvfb-run (1600x1000x24) or directly on Windows.
#   RUNNER: also compiles the runner window sources and builds bin/persistcad_runner first (Save as App).
function(pc_add_gui_harness name)
  cmake_parse_arguments(H "RUNNER" "TIMEOUT" "SOURCES;ARGS;DEFINES" ${ARGN})
  if(NOT H_TIMEOUT)
    set(H_TIMEOUT 300)
  endif()
  set(tgt pc_${name})
  set(srcs ${H_SOURCES})
  if(H_RUNNER)
    list(APPEND srcs ${PC_RUNNER_SOURCES})
  endif()
  add_executable(${tgt} ${srcs})
  target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  if(H_RUNNER)
    target_include_directories(${tgt} PRIVATE ${PC_R})
  endif()
  target_link_libraries(${tgt} PRIVATE pc_designer pc_appui pc_viewer pc_apppkg pc_mfg pc_model pc_sketch pc_core
                        Qt6::Widgets Qt6::Concurrent Qt6::Test)
  target_compile_definitions(${tgt} PRIVATE PC_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples" ${H_DEFINES})
  if(H_RUNNER)
    add_dependencies(${tgt} persistcad_runner)
  endif()
  if(PC_XVFB_RUN)
    add_test(NAME ${name} COMMAND ${PC_XVFB_RUN} -a -s "-screen 0 1600x1000x24" $<TARGET_FILE:${tgt}> ${H_ARGS})
  elseif(WIN32)
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${tgt}> ${H_ARGS})
  else()
    return()
  endif()
  set_tests_properties(${name} PROPERTIES TIMEOUT ${H_TIMEOUT})
  if(WIN32)
    # Windows'ta GUI testleri gerçek masaüstünü paylaşır: paralel koşarlarsa
    # birbirinin klavye odağını çalar. ctest -jN altında bile sırayla koşsunlar.
    set_tests_properties(${name} PROPERTIES RESOURCE_LOCK pc_gui_desktop)
  endif()
endfunction()

# Review flow, Save as App, runner.
pc_add_gui_harness(gui_harness SOURCES gui_harness.cpp RUNNER)
# v2: datum planes / bodies workflow through real widgets and input events.
pc_add_gui_harness(gui_harness_v2 SOURCES gui_harness_v2.cpp)
# v3: parameter edited while a feature dialog is open survives the dialog's OK.
pc_add_gui_harness(gui_harness_v3 SOURCES gui_harness_v3.cpp)
# v4: section view ("Kesit") in the viewer, the designer and the runner.
pc_add_gui_harness(gui_harness_v4 SOURCES gui_harness_v4.cpp RUNNER)
# v5: 09-DB01 atomizer sample in the designer (axisAngle plane dialog, hardware-only bodies, GUI regen speed).
pc_add_gui_harness(gui_harness_v5 SOURCES gui_harness_v5.cpp)
# QA: customer scenario (sketch dims -> parametric extrude/fillet -> Save as App -> generated app), UI-driven.
pc_add_gui_harness(gui_harness_qa SOURCES gui_harness_qa.cpp RUNNER TIMEOUT 600 ARGS --no-shots
                   DEFINES PC_QA_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/qa")
# v6: sketch dimensions/parameters survive closing the sketch; feature fields bound to existing / new parameters;
# exposed parameters reach Save-as-App and the generated app.
pc_add_gui_harness(gui_harness_v6 SOURCES gui_harness_v6.cpp RUNNER TIMEOUT 400)

# SC-UX (docs/spaceclaim_ux/ACCEPTANCE.md). Registered in P0 with placeholder mains; the WPs only replace the
# file contents (SH = WP-A shell, TL = WP-B tools). Shots dir for --shots: docs/spaceclaim_ux/shots.
pc_add_gui_harness(gui_harness_sc_shell SOURCES gui_harness_sc_shell.cpp sc_stub_host.h RUNNER TIMEOUT 400
                   DEFINES PC_SC_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/spaceclaim_ux/shots")
pc_add_gui_harness(gui_harness_sc_tools SOURCES gui_harness_sc_tools.cpp sc_stub_host.h RUNNER TIMEOUT 400
                   DEFINES PC_SC_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/spaceclaim_ux/shots")
# SC-UX integration: the real MainWindow (WP-A shell + WP-B tools) through the ACCEPTANCE first-10-minutes flow,
# Save as App and the generated app (--validate-only). --shots writes docs/spaceclaim_ux/shots/final_*.png.
pc_add_gui_harness(gui_harness_sc_e2e SOURCES gui_harness_sc_e2e.cpp RUNNER TIMEOUT 500
                   DEFINES PC_SC_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/spaceclaim_ux/shots")
# Seçimden parametre: select a line / circle / arc / dimension label / 3D face or edge -> badge + P -> inline popup ->
# parameter bound to the dimension (sketch and 3D), undo as one step, save / reopen, Save as App. --shots writes
# docs/param_select/*.png.
pc_add_gui_harness(gui_harness_param_select SOURCES gui_harness_param_select.cpp RUNNER TIMEOUT 500
                   DEFINES PC_PS_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/param_select")
# Teknik resimden içe aktar (DRW: designer hook): Dosya menu / ribbon -> wizard on the corpus flange PDF -> ONE undo
# step, parameters in Gruplar, save / reopen, Save as App. --shots writes docs/drawing_import/shots/*.png.
pc_add_gui_harness(gui_harness_drawing_import SOURCES gui_harness_drawing_import.cpp RUNNER TIMEOUT 500
                   DEFINES PC_DRAWING_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/drawing/corpus"
                           PC_DRW_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/drawing_import/shots")
# Real drawings (R1-R4 integration): wizard on radrad_swirler_SWC-RR-001.pdf + simplex_atomizer_ATM-PS-001.pdf through
# the Dosya menu -> bodies / volume vs R4 ground truth / slot patterns / Gruplar. --shots: docs/drawing_import/shots/real_*.png
pc_add_gui_harness(gui_harness_drawing_real SOURCES gui_harness_drawing_real.cpp TIMEOUT 600
                   DEFINES PC_DRAWING_REAL_DIR="${CMAKE_SOURCE_DIR}/tests/drawing/real"
                           PC_DRW_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/drawing_import/shots")
# Named selections (Adlandırılmış seçimler): Ctrl+G on faces -> inline name in Gruplar, names survive boss / hole / split /
# fillet / parameter change / undo / save-reopen; F2, Del, context menu, Properties "Ad", labels, Save-as-App section.
# --shots writes docs/named_selections/shots/*.png. Also run with QT_FONT_DPI=72.
pc_add_gui_harness(gui_harness_named_sel SOURCES gui_harness_named_sel.cpp RUNNER TIMEOUT 500
                   DEFINES PC_NS_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/named_selections/shots")
# Gizle / Göster (faces + bodies, view state): Ctrl+H / Alt+H / Ctrl+Shift+H, click-through, Yapı "Gizli yüzler", context
# menus, named selections, split face stays hidden, undo, save / reopen, STEP has all faces. --shots -> docs/visibility/shots.
pc_add_gui_harness(gui_harness_visibility SOURCES gui_harness_visibility.cpp TIMEOUT 400
                   DEFINES PC_VIS_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/visibility/shots")
# SC-2D-IMPORT: 2D drawing as curves (DXF / PDF), Katmanlar tab, Görünüş seç / Görünüşü yerleştir, Fill + Pull on the
# imported curves (XY + placed vertical plane), save / reopen, undo, drag & drop. --shots -> docs/import2d/shots.
pc_add_gui_harness(gui_harness_import2d SOURCES gui_harness_import2d.cpp TIMEOUT 400
                   DEFINES PC_I2D_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/import2d/shots"
                           PC_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/drawing/corpus")
# SC-PULLUPTO: Fill (tolerant loops) -> Pull Up To a vertex of another sketch -> associative update, Ruler, multi-region.
pc_add_gui_harness(gui_harness_pullupto SOURCES gui_harness_pullupto.cpp TIMEOUT 300)

# SC-COLOR: face / body colours (Görüntü > Stil, context menu, Özellikler, undo, split, save/reopen, STEP, Save-as-App).
# --shots -> docs/colors/shots.
pc_add_gui_harness(gui_harness_colors SOURCES gui_harness_colors.cpp RUNNER TIMEOUT 500
                   DEFINES PC_COL_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/colors/shots")

# SC-VIDEO2D: the SpaceClaim video "Convert 2D Drawings to 3D Models" end to end on plate + lbracket (import as curves ->
# hide layers -> Görünüş seç / yerleştir -> Fill -> Pull Up To the other view -> Renk -> undo/redo -> save/reopen).
# --shots -> docs/import2d/shots/video_*.png. Registered twice: default fonts and QT_FONT_DPI=72.
pc_add_gui_harness(gui_harness_video2d SOURCES gui_harness_video2d.cpp TIMEOUT 400
                   DEFINES PC_V2D_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/import2d/shots"
                           PC_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tests/drawing/corpus")
if(PC_XVFB_RUN)
  add_test(NAME gui_harness_video2d_dpi72 COMMAND ${PC_XVFB_RUN} -a -s "-screen 0 1600x1000x24" $<TARGET_FILE:pc_gui_harness_video2d>)
  set_tests_properties(gui_harness_video2d_dpi72 PROPERTIES TIMEOUT 400 ENVIRONMENT "QT_FONT_DPI=72")
endif()

# SC23 (docs/sc2023/ANALYSIS.md §5.2, P0_CONTRACT.md): the real MainWindow at 1240x800; checks split into
# sc2023_shell_checks.inc (WP-S) and sc2023_op_checks.inc (WP-O). --shots -> docs/sc2023/shots.
pc_add_gui_harness(gui_harness_sc2023 SOURCES gui_harness_sc2023.cpp sc2023_shell_checks.inc sc2023_op_checks.inc TIMEOUT 300
                   DEFINES PC_SC23_SHOTS_DIR="${CMAKE_SOURCE_DIR}/docs/sc2023/shots")
