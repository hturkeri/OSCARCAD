# OWNER: Agent E (app runner). Add source files freely.
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
add_executable(persistcad_runner main.cpp RunnerCore.cpp RunnerCore.h RunnerWindow.cpp RunnerWindow.h)
target_include_directories(persistcad_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(persistcad_runner PRIVATE pc_apppkg pc_viewer pc_mfg pc_model pc_sketch pc_core
                      Qt6::Widgets Qt6::Concurrent)
