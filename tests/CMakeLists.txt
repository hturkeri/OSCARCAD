# Every tests/test_<module>.cpp becomes its own gtest target automatically (no edits needed).
include(GoogleTest)
file(GLOB PC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
# test_pull_face tests designer logic (PullMapping): built only with the GUI (links pc_designer), see below.
list(FILTER PC_TEST_SOURCES EXCLUDE REGEX "test_pull_face\\.cpp$")
# SC23: test_value_input tests designer code (OperationState.h, later tools/ValueInput): GUI-only too, see below.
list(FILTER PC_TEST_SOURCES EXCLUDE REGEX "test_value_input\\.cpp$")
foreach(src ${PC_TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE GTest::gtest_main pc_apppkg pc_mfg pc_model pc_sketch pc_core)
  target_compile_definitions(${name} PRIVATE PC_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
  gtest_discover_tests(${name} DISCOVERY_TIMEOUT 120)
endforeach()

# Named selections: the Save-as-App test also drives the real runner CLI (--named-selections / --export-step).
if(PC_BUILD_GUI AND TARGET test_named_selection)
  target_compile_definitions(test_named_selection PRIVATE PC_RUNNER_EXE="${CMAKE_BINARY_DIR}/bin/persistcad_runner")
  add_dependencies(test_named_selection persistcad_runner)
endif()

# Pull on faces (docs/spaceclaim_ux/PULL_FACE_FIX.md): PullMapping decisions + regenerated model (gtest, no display).
if(PC_BUILD_GUI)
  add_executable(test_pull_face ${CMAKE_CURRENT_SOURCE_DIR}/test_pull_face.cpp)
  target_link_libraries(test_pull_face PRIVATE GTest::gtest_main pc_designer pc_model pc_sketch pc_core)
  target_compile_definitions(test_pull_face PRIVATE PC_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
  gtest_discover_tests(test_pull_face DISCOVERY_TIMEOUT 120)
  # SC23 (WP-O): value input / operation state (gtest, no display). P0-SC23 ships a placeholder.
  add_executable(test_value_input ${CMAKE_CURRENT_SOURCE_DIR}/test_value_input.cpp)
  target_link_libraries(test_value_input PRIVATE GTest::gtest_main pc_designer pc_model pc_sketch pc_core)
  target_compile_definitions(test_value_input PRIVATE PC_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
  gtest_discover_tests(test_value_input DISCOVERY_TIMEOUT 120)
endif()

# GUI end-to-end harness (designer review flow, Save as App, runner) — needs Qt + a display (xvfb-run).
if(PC_BUILD_GUI)
  add_subdirectory(gui)
endif()
