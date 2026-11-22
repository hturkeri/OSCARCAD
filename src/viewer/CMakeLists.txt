# OWNER: Agent D (viewer/designer). Add source files freely.
add_library(pc_viewer STATIC pc/DatumSizing.h DatumSizing.cpp pc/ShapeViewer.h ShapeViewer.cpp Tessellation.cpp Tessellation.h
            pc/SectionControls.h SectionControls.cpp)
target_include_directories(pc_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(pc_viewer PUBLIC Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${PC_OCCT_LIBS})

# Small demo / smoke test: renders OCCT primitives to a PNG (works under Xvfb).
add_executable(pc_viewer_demo demo.cpp)
target_link_libraries(pc_viewer_demo PRIVATE pc_viewer Qt6::Widgets)
