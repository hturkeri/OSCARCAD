# SC-UX P0 (SPEC §7.5): every designer source except main.cpp, picked up automatically.
# New files in src/designer/ or src/designer/tools/ need NO CMake edit (CONFIGURE_DEPENDS re-globs at build time).
# Used by src/designer/CMakeLists.txt (library pc_designer) and, through that library, by tests/gui.
file(GLOB PC_DESIGNER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp ${CMAKE_CURRENT_LIST_DIR}/*.h
  ${CMAKE_CURRENT_LIST_DIR}/tools/*.cpp ${CMAKE_CURRENT_LIST_DIR}/tools/*.h)
list(FILTER PC_DESIGNER_SOURCES EXCLUDE REGEX "/main\\.cpp$")
set(PC_DESIGNER_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/tools)
