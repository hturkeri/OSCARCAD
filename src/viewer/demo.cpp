// pc_viewer_demo [out.png] — renders a box with a hole and fillet via ShapeViewer (smoke test).
#include "pc/ShapeViewer.h"
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <QApplication>
#include <QTimer>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  TopoDS_Shape box = BRepPrimAPI_MakeBox(80, 50, 20).Shape();
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(40, 25, -1), gp_Dir(0, 0, 1)), 8, 30).Shape();
  TopoDS_Shape s = BRepAlgoAPI_Cut(box, cyl).Shape();
  std::map<std::string, TopoDS_Face> faces;
  std::map<std::string, TopoDS_Edge> edges;
  int i = 0;
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) faces["F" + std::to_string(i++)] = TopoDS::Face(ex.Current());
  i = 0;
  for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) edges["E" + std::to_string(i++)] = TopoDS::Edge(ex.Current());
  pc::viewer::ShapeViewer v;
  v.resize(1000, 700);
  v.setModel(s, faces, edges, false);
  v.setHighlightedFaces({"F1"});
  if (argc > 2) {  // v2 smoke: datum planes, face colours, hidden face
    std::vector<pc::viewer::DatumPlaneDisplay> pl(2);
    pl[0].name = "Plane1"; pl[0].origin[0] = 40; pl[0].origin[1] = 25; pl[0].origin[2] = 10;
    pl[0].u[0] = 0; pl[0].u[1] = 1; pl[0].v[0] = 0; pl[0].v[1] = 0; pl[0].v[2] = 1; pl[0].halfSize = 30;
    pl[1].name = "Plane2"; pl[1].origin[0] = 40; pl[1].origin[1] = 25; pl[1].origin[2] = 35; pl[1].halfSize = 38; pl[1].highlighted = true;
    v.setDatumPlanes(pl);
    std::map<std::string, QColor> col;
    for (int k = 0; k < 4; ++k) col["F" + std::to_string(k)] = QColor(230, 170, 90);
    v.setFaceColors(col);
    v.setHiddenFaces({"F5"});
  }
  v.show();
  QApplication::processEvents();
  QImage img = v.renderToImage(1000, 700);
  img.save(argc > 1 ? argv[1] : "viewer_demo.png");
  return 0;
}
