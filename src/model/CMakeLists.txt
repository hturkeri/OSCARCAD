# OWNER: Agent B (model). Add source files freely.
add_library(pc_model STATIC Regen.cpp Naming.cpp Export.cpp NamedSelections.cpp UpTo.cpp ViewColors.cpp)  # UpTo.cpp: SC-PULLUPTO, ViewColors.cpp: SC-COLOR
target_include_directories(pc_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCASCADE_INCLUDE_DIR})
# Named selections -> STEP face names need the XDE (XCAF/OCAF) toolkits. OCCT >= 7.8 merged TKXDESTEP into TKDESTEP.
set(PC_XDE_LIBS TKXCAF TKLCAF TKCAF TKCDF TKVCAF TKService TKV3d)
if(TARGET TKXDESTEP)
  list(APPEND PC_XDE_LIBS TKXDESTEP)
endif()
target_link_libraries(pc_model PUBLIC pc_sketch pc_core ${PC_OCCT_LIBS} ${PC_XDE_LIBS})
