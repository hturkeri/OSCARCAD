# =====================================================================================================================
# Drawing import ("Teknik resimden içe aktar") — P0 bootstrap. FROZEN: work packages never edit this file.
# Sources are collected by glob (CONFIGURE_DEPENDS) from module directories; ownership: docs/drawing_import/OWNERSHIP.md
#
# Targets
#   pc_drawing          static lib, NO Qt       src/drawing/{core,ir,sniff,pdf,dxf,normalize,eval,raster,ocr,text,interp,synth}
#   pc_drawing_cli      exe (if cli/*.cpp)      src/drawing/cli
#   pc_drawing_qt       static lib, Qt6::Gui    src/drawing_qt (+ pdf/ with Qt6::Pdf)              [PC_BUILD_GUI]
#   pc_drawing_ui       static lib, Widgets     src/drawing_ui/{overlay,wizard}                     [PC_BUILD_GUI]
#   pc_drawing_corpus   exe (if sources)        tools/drawing_corpus                                [PC_BUILD_GUI]
#   pc_drawing_tests    gtest exe               tests/drawing/*.cpp   (ctest names "drawing.*")     [PC_BUILD_TESTS]
#
# Optional dependencies -> feature flags (always defined 0/1, PUBLIC on pc_drawing):
#   PC_DRAWING_HAVE_QPDF  PC_DRAWING_HAVE_OPENCV  PC_DRAWING_HAVE_TESSERACT  PC_DRAWING_HAVE_QTPDF
# A module directory whose dependencies are missing is replaced by its `nodeps/` subdirectory (fallback stubs that
# return Status::Unavailable). -DPC_DRAWING_DISABLE_OPTIONAL=ON builds without any of them (Windows w/o packages).
# =====================================================================================================================
option(PC_BUILD_DRAWING "Drawing import (pc_drawing)" ON)
if(NOT PC_BUILD_DRAWING)
  return()
endif()
option(PC_DRAWING_DISABLE_OPTIONAL "Build drawing import without qpdf/OpenCV/Tesseract/QtPdf" OFF)
option(PC_DRAWING_WITH_QPDF      "Use libqpdf (vector PDF)" ON)
option(PC_DRAWING_WITH_OPENCV    "Use OpenCV (raster)" ON)
option(PC_DRAWING_WITH_TESSERACT "Use Tesseract + Leptonica (OCR)" ON)
option(PC_DRAWING_WITH_QTPDF     "Use Qt6::Pdf (PDF rendering)" ON)

set(PC_DRW_ROOT ${CMAKE_SOURCE_DIR}/src/drawing)
set(PC_DRW_HAVE_QPDF 0)
set(PC_DRW_HAVE_OPENCV 0)
set(PC_DRW_HAVE_TESSERACT 0)
set(PC_DRW_HAVE_QTPDF 0)
set(PC_DRW_QPDF_VERSION "")
set(PC_DRW_OPENCV_VERSION "")
set(PC_DRW_TESSERACT_VERSION "")
set(PC_DRW_QT_VERSION "")

# All found optional libraries; linked PRIVATE by pc_drawing and directly by tests/tools that need them.
add_library(pc_drawing_optdeps INTERFACE)

if(NOT PC_DRAWING_DISABLE_OPTIONAL)
  find_package(PkgConfig QUIET)
  # MSYS2: TesseractTargets.cmake (also pulled in by OpenCV 5's config) links OpenMP::OpenMP_CXX
  # without finding it; find it first and make it visible to every directory that links pc_drawing.
  find_package(OpenMP QUIET)
  if(TARGET OpenMP::OpenMP_CXX)
    set_target_properties(OpenMP::OpenMP_CXX PROPERTIES IMPORTED_GLOBAL TRUE)
  endif()
  if(PC_DRAWING_WITH_QPDF)
    find_package(qpdf CONFIG QUIET)
    if(TARGET qpdf::libqpdf)
      set(PC_DRW_HAVE_QPDF 1)
      set(PC_DRW_QPDF_VERSION "${qpdf_VERSION}")
      target_link_libraries(pc_drawing_optdeps INTERFACE qpdf::libqpdf)
    elseif(PKG_CONFIG_FOUND)
      pkg_check_modules(PC_QPDF QUIET IMPORTED_TARGET libqpdf)
      if(PC_QPDF_FOUND)
        set(PC_DRW_HAVE_QPDF 1)
        set(PC_DRW_QPDF_VERSION "${PC_QPDF_VERSION}")
        target_link_libraries(pc_drawing_optdeps INTERFACE PkgConfig::PC_QPDF)
      endif()
    endif()
  endif()
  if(PC_DRAWING_WITH_OPENCV)
    find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
    if(OpenCV_FOUND)
      set(PC_DRW_HAVE_OPENCV 1)
      set(PC_DRW_OPENCV_VERSION "${OpenCV_VERSION}")
      target_include_directories(pc_drawing_optdeps SYSTEM INTERFACE ${OpenCV_INCLUDE_DIRS})
      target_link_libraries(pc_drawing_optdeps INTERFACE opencv_core opencv_imgproc opencv_imgcodecs)
      # OpenCV 5 moved computational geometry (contourArea, approxPolyDP, getRotationMatrix2D,
      # getPerspectiveTransform, DIST_L2, ...) from imgproc into the new 'geometry' module.
      if(OpenCV_VERSION VERSION_GREATER_EQUAL 5 AND TARGET opencv_geometry)
        target_link_libraries(pc_drawing_optdeps INTERFACE opencv_geometry)
      endif()
    endif()
  endif()
  if(PC_DRAWING_WITH_TESSERACT)
    # Prefer pkg-config (Ubuntu and MSYS2 both ship tesseract.pc / lept.pc). MSYS2's TesseractTargets.cmake
    # references targets it never finds (OpenMP::OpenMP_CXX, CURL::libcurl_shared), so its CMake config is
    # only a fallback.
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PC_TESS QUIET IMPORTED_TARGET tesseract)
      pkg_check_modules(PC_LEPT QUIET IMPORTED_TARGET lept)
      if(PC_TESS_FOUND AND PC_LEPT_FOUND)
        set(PC_DRW_HAVE_TESSERACT 1)
        set(PC_DRW_TESSERACT_VERSION "${PC_TESS_VERSION}")
        target_link_libraries(pc_drawing_optdeps INTERFACE PkgConfig::PC_TESS PkgConfig::PC_LEPT)
      endif()
    endif()
    if(NOT PC_DRW_HAVE_TESSERACT)
      find_package(CURL QUIET)
      find_package(Tesseract CONFIG QUIET)
      find_package(Leptonica CONFIG QUIET)
      if(TARGET Tesseract::libtesseract AND Leptonica_FOUND)
        set(PC_DRW_HAVE_TESSERACT 1)
        set(PC_DRW_TESSERACT_VERSION "${Tesseract_VERSION}")
        target_link_libraries(pc_drawing_optdeps INTERFACE Tesseract::libtesseract ${Leptonica_LIBRARIES})
        if(Leptonica_INCLUDE_DIRS)
          target_include_directories(pc_drawing_optdeps SYSTEM INTERFACE ${Leptonica_INCLUDE_DIRS})
        endif()
      endif()
    endif()
  endif()
  if(PC_DRAWING_WITH_QTPDF AND PC_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Pdf)
    if(TARGET Qt6::Pdf)
      set(PC_DRW_HAVE_QTPDF 1)
      set(PC_DRW_QT_VERSION "${Qt6_VERSION}")
    endif()
  endif()
endif()

set(PC_DRAWING_FEATURE_DEFS
  PC_DRAWING_HAVE_QPDF=${PC_DRW_HAVE_QPDF}
  PC_DRAWING_HAVE_OPENCV=${PC_DRW_HAVE_OPENCV}
  PC_DRAWING_HAVE_TESSERACT=${PC_DRW_HAVE_TESSERACT}
  PC_DRAWING_HAVE_QTPDF=${PC_DRW_HAVE_QTPDF})
message(STATUS "pc_drawing: qpdf=${PC_DRW_HAVE_QPDF} ${PC_DRW_QPDF_VERSION} | opencv=${PC_DRW_HAVE_OPENCV} "
               "${PC_DRW_OPENCV_VERSION} | tesseract=${PC_DRW_HAVE_TESSERACT} ${PC_DRW_TESSERACT_VERSION} | "
               "qtpdf=${PC_DRW_HAVE_QTPDF}")

# pc_drw_collect(<out-var> <module dir> [REQUIRES <flag vars...>])
#   all REQUIRES set to 1 -> <dir>/*.cpp + <dir>/*.h ; otherwise <dir>/nodeps/*.cpp + *.h
function(pc_drw_collect out dir)
  cmake_parse_arguments(C "" "" "REQUIRES" ${ARGN})
  set(ok TRUE)
  foreach(flag ${C_REQUIRES})
    if(NOT ${flag})
      set(ok FALSE)
    endif()
  endforeach()
  if(ok)
    file(GLOB srcs CONFIGURE_DEPENDS ${dir}/*.cpp ${dir}/*.h)
  else()
    file(GLOB srcs CONFIGURE_DEPENDS ${dir}/nodeps/*.cpp ${dir}/nodeps/*.h)
  endif()
  set(${out} ${${out}} ${srcs} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------------------------ pc_drawing
set(PC_DRAWING_SOURCES)
file(GLOB PC_DRAWING_HEADERS CONFIGURE_DEPENDS ${PC_DRW_ROOT}/include/pc/drawing/*.h)
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/core)                                   # P0
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/ir)                                     # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/sniff)                                  # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/pdf REQUIRES PC_DRW_HAVE_QPDF)          # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/dxf)                                    # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/normalize)                              # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/eval)                                   # WP-1
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/raster REQUIRES PC_DRW_HAVE_OPENCV)     # WP-2
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/ocr REQUIRES PC_DRW_HAVE_OPENCV PC_DRW_HAVE_TESSERACT)   # WP-2
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/text)                                   # WP-2
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/interp)                                 # WP-3
pc_drw_collect(PC_DRAWING_SOURCES ${PC_DRW_ROOT}/synth)                                  # WP-3

add_library(pc_drawing STATIC ${PC_DRAWING_SOURCES} ${PC_DRAWING_HEADERS})
target_include_directories(pc_drawing PUBLIC ${PC_DRW_ROOT}/include PRIVATE ${PC_DRW_ROOT})
target_compile_definitions(pc_drawing PUBLIC ${PC_DRAWING_FEATURE_DEFS})
set_source_files_properties(${PC_DRW_ROOT}/core/Features.cpp PROPERTIES COMPILE_DEFINITIONS
  "PC_DRAWING_QPDF_VERSION=\"${PC_DRW_QPDF_VERSION}\";PC_DRAWING_OPENCV_VERSION=\"${PC_DRW_OPENCV_VERSION}\";PC_DRAWING_TESSERACT_VERSION=\"${PC_DRW_TESSERACT_VERSION}\";PC_DRAWING_QT_VERSION=\"${PC_DRW_QT_VERSION}\"")
set(PC_DRW_OCCT_EXTRA)
if(TARGET TKHLR)
  list(APPEND PC_DRW_OCCT_EXTRA TKHLR)    # S7 re-projection, corpus views
endif()
target_link_libraries(pc_drawing
  PUBLIC pc_model pc_sketch pc_core nlohmann_json::nlohmann_json
  PRIVATE pc_drawing_optdeps ${PC_DRW_OCCT_EXTRA})

# ------------------------------------------------------------------------------------------------ pc_drawing_cli
file(GLOB PC_DRAWING_CLI_SOURCES CONFIGURE_DEPENDS ${PC_DRW_ROOT}/cli/*.cpp ${PC_DRW_ROOT}/cli/*.h)  # WP-3
if(PC_DRAWING_CLI_SOURCES)
  add_executable(pc_drawing_cli ${PC_DRAWING_CLI_SOURCES})
  target_link_libraries(pc_drawing_cli PRIVATE pc_drawing)
endif()

# ------------------------------------------------------------------------------------------------ Qt layers
if(PC_BUILD_GUI)
  set(PC_DRWQ_ROOT ${CMAKE_SOURCE_DIR}/src/drawing_qt)
  set(PC_DRAWING_QT_SOURCES)
  file(GLOB PC_DRAWING_QT_HEADERS CONFIGURE_DEPENDS ${PC_DRWQ_ROOT}/include/pc/drawing/qt/*.h)
  pc_drw_collect(PC_DRAWING_QT_SOURCES ${PC_DRWQ_ROOT})                                   # WP-2
  pc_drw_collect(PC_DRAWING_QT_SOURCES ${PC_DRWQ_ROOT}/pdf REQUIRES PC_DRW_HAVE_QTPDF)    # WP-2
  add_library(pc_drawing_qt STATIC ${PC_DRAWING_QT_SOURCES} ${PC_DRAWING_QT_HEADERS})
  target_include_directories(pc_drawing_qt PUBLIC ${PC_DRWQ_ROOT}/include PRIVATE ${PC_DRWQ_ROOT})
  target_link_libraries(pc_drawing_qt PUBLIC pc_drawing Qt6::Gui)
  if(PC_DRW_HAVE_QTPDF)
    target_link_libraries(pc_drawing_qt PRIVATE Qt6::Pdf)
  endif()

  set(PC_DRWU_ROOT ${CMAKE_SOURCE_DIR}/src/drawing_ui)
  set(PC_DRAWING_UI_SOURCES)
  file(GLOB PC_DRAWING_UI_HEADERS CONFIGURE_DEPENDS ${PC_DRWU_ROOT}/include/pc/drawing/ui/*.h)
  pc_drw_collect(PC_DRAWING_UI_SOURCES ${PC_DRWU_ROOT}/overlay)                           # WP-2
  pc_drw_collect(PC_DRAWING_UI_SOURCES ${PC_DRWU_ROOT}/wizard)                            # WP-3
  add_library(pc_drawing_ui STATIC ${PC_DRAWING_UI_SOURCES} ${PC_DRAWING_UI_HEADERS})
  target_include_directories(pc_drawing_ui PUBLIC ${PC_DRWU_ROOT}/include PRIVATE ${PC_DRWU_ROOT})
  set_target_properties(pc_drawing_ui PROPERTIES AUTOMOC ON)
  target_link_libraries(pc_drawing_ui PUBLIC pc_drawing_qt pc_drawing pc_viewer Qt6::Widgets)

  file(GLOB PC_DRAWING_CORPUS_SOURCES CONFIGURE_DEPENDS
       ${CMAKE_SOURCE_DIR}/tools/drawing_corpus/*.cpp ${CMAKE_SOURCE_DIR}/tools/drawing_corpus/*.h)   # WP-1
  if(PC_DRAWING_CORPUS_SOURCES)
    add_executable(pc_drawing_corpus ${PC_DRAWING_CORPUS_SOURCES})
    target_include_directories(pc_drawing_corpus PRIVATE ${CMAKE_SOURCE_DIR}/tools/drawing_corpus)
    target_link_libraries(pc_drawing_corpus PRIVATE pc_drawing_qt pc_drawing pc_drawing_optdeps Qt6::Gui
                          ${PC_DRW_OCCT_EXTRA})
  endif()
endif()

# ------------------------------------------------------------------------------------------------ tests
if(PC_BUILD_TESTS)
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests/drawing ${CMAKE_BINARY_DIR}/tests/drawing)
endif()
