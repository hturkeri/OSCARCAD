# OWNER: Agent C (mfg). Add source files freely.
add_library(pc_mfg STATIC MfgChecks.cpp Suggest.cpp AnalyticModel.cpp AnalyticChecks.cpp GeometricChecks.cpp AnalyticPhase2.cpp)
target_include_directories(pc_mfg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(pc_mfg PUBLIC pc_model pc_core ${PC_OCCT_LIBS})
