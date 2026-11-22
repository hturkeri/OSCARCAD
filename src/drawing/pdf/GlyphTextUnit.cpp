// R1: compiles src/drawing/glyphtext/ as part of the pdf/ module. src/drawing/CMakeLists.txt (frozen) globs a fixed
// list of module directories; glyphtext/ is only used by vector-PDF ingest (needs qpdf), so it is built through this
// translation unit instead of a CMake change. Remove this file once glyphtext/ is added to pc_drw_collect.
#include "glyphtext/GlyphText.cpp"
