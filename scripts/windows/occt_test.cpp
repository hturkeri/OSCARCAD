// OCCT 7.9 classifier smoke test
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Standard_Version.hxx>
#include <gp_Pnt.hxx>
#include <cstdio>
int main() {
  std::printf("OCCT %s\n", OCC_VERSION_COMPLETE); std::fflush(stdout);
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
  std::printf("box ok\n"); std::fflush(stdout);
  BRepClass3d_SolidClassifier c1(box, gp_Pnt(5, 5, 5), 1e-7);
  std::printf("classifier box: %d\n", (int)c1.State()); std::fflush(stdout);
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5,5,-1), gp_Dir(0,0,1)), 2, 12).Shape();
  BRepClass3d_SolidClassifier c2(cyl, gp_Pnt(5, 5, 5), 1e-7);
  std::printf("classifier cyl: %d\n", (int)c2.State()); std::fflush(stdout);
  BRepExtrema_DistShapeShape d(box, cyl);
  std::printf("dist: %g\n", d.Value()); std::fflush(stdout);
  TopoDS_Shape cone = BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(5,5,2), gp_Dir(0,0,-1)), 3, 0, 3).Shape();
  std::printf("cone ok\n"); std::fflush(stdout);
  BRepClass3d_SolidClassifier c3(cone, gp_Pnt(5, 5, 1), 1e-7);
  std::printf("classifier cone: %d\n", (int)c3.State()); std::fflush(stdout);
  BRepExtrema_DistShapeShape d2(box, cone);
  std::printf("dist box-cone: %g\n", d2.Value()); std::fflush(stdout);
  TopoDS_Shape tool = BRepAlgoAPI_Fuse(cyl, cone).Shape();
  BRepExtrema_DistShapeShape d3(box, tool);
  std::printf("dist box-tool: %g\n", d3.Value()); std::fflush(stdout);
  BRepFilletAPI_MakeFillet mf(box);
  for (TopExp_Explorer e(box, TopAbs_EDGE); e.More(); e.Next()) { mf.Add(1.0, TopoDS::Edge(e.Current())); break; }
  TopoDS_Shape fb = mf.Shape();
  BRepExtrema_DistShapeShape d4(fb, tool);
  std::printf("dist filleted-tool: %g\n", d4.Value()); std::fflush(stdout);
  return 0;
}
