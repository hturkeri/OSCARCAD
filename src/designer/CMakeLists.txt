# OWNER: WP-A (SC-UX P0). Do not edit: sources come from sources.cmake (glob, CONFIGURE_DEPENDS).
include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)
# All designer code (MainWindow, dialogs, sketch editor, ToolApi, tools/) is compiled ONCE into this library;
# the persistcad executable and every GUI harness in tests/gui link it.
add_library(pc_designer STATIC ${PC_DESIGNER_SOURCES})
target_include_directories(pc_designer PUBLIC ${PC_DESIGNER_INCLUDE_DIRS})
target_link_libraries(pc_designer PUBLIC pc_appui pc_viewer pc_mfg pc_model pc_sketch pc_core Qt6::Widgets)

add_executable(persistcad main.cpp)
target_link_libraries(persistcad PRIVATE pc_designer)
add_dependencies(persistcad persistcad_runner)
