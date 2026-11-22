cmake_minimum_required(VERSION 3.21)
project(PersistCAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(WIN32)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX UNICODE _UNICODE)
  if(MINGW)
    add_compile_options(-Wa,-mbig-obj)   # large OCCT translation units
  endif()
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(PC_BUILD_GUI   "Build Qt GUI (viewer, designer, runner)" ON)
option(PC_BUILD_TESTS "Build unit tests" ON)

find_package(OpenCASCADE REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

# OCCT toolkits used across the project
set(PC_OCCT_LIBS
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo
  TKPrim TKBO TKBool TKFillet TKOffset TKShHealing TKMesh TKXSBase)
# OCCT >= 7.8 merged the STEP/STL toolkits into TKDESTEP / TKDESTL.
if(TARGET TKDESTEP)
  list(APPEND PC_OCCT_LIBS TKDESTEP TKDESTL)
else()
  list(APPEND PC_OCCT_LIBS TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKSTL)
endif()
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION}: ${PC_OCCT_LIBS}")

add_subdirectory(src/core)
add_subdirectory(src/sketch)
add_subdirectory(src/model)
add_subdirectory(src/mfg)
add_subdirectory(src/apppkg)

if(PC_BUILD_GUI)
  set(CMAKE_AUTOMOC ON)
  set(CMAKE_AUTORCC ON)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
  add_subdirectory(src/viewer)
  add_subdirectory(src/appui)
  add_subdirectory(src/designer)
  add_subdirectory(src/runner)
endif()

if(PC_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_subdirectory(tests)
endif()
add_subdirectory(src/drawing)   # drawing import (option PC_BUILD_DRAWING, docs/drawing_import/OWNERSHIP.md)
# DRW: designer hook — the designer gets "Dosya > Teknik resimden içe aktar…" when the drawing UI is built.
if(TARGET pc_drawing_ui AND TARGET pc_designer)
  target_link_libraries(pc_designer PUBLIC pc_drawing_ui)
  target_compile_definitions(pc_designer PUBLIC PC_HAVE_DRAWING_IMPORT=1)
endif()
