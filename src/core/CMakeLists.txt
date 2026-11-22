add_library(pc_core STATIC Expression.cpp Parameter.cpp SketchData.cpp Document.cpp
            Layers.cpp)   # SC-2D-IMPORT
target_include_directories(pc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pc_core PUBLIC nlohmann_json::nlohmann_json)
