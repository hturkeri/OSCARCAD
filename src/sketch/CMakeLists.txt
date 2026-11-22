# OWNER: Agent A (sketch). Add source files freely.
find_package(Eigen3 REQUIRED NO_MODULE)  # 3.3+ (Eigen 5 uses SameMajorVersion)
add_library(pc_sketch STATIC SketchSolver.cpp SolverSystem.cpp Profile.cpp Regions.cpp)  # Regions.cpp: SC-PULLUPTO
target_include_directories(pc_sketch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(pc_sketch PUBLIC pc_core ${PC_OCCT_LIBS} PRIVATE Eigen3::Eigen)
