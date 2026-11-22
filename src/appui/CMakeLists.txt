# OWNER: Agent E (app packaging). Add source files freely.
add_library(pc_appui STATIC pc/SaveAsAppDialog.h SaveAsAppDialog.cpp)
target_include_directories(pc_appui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pc_appui PUBLIC pc_apppkg Qt6::Widgets)
