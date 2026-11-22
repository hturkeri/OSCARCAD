#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <Extrema_ExtCC.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Line.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Precision.hxx>
#include <cstdio>
int main() {
  TopoDS_Shape cone = BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(5,5,2), gp_Dir(0,0,-1)), 3, 0, 3).Shape();
  gp_Lin L(gp_Pnt(5, 5, 1), gp_Dir(0.3, 0.2, 0.93));
  Handle(Geom_Line) gl = new Geom_Line(L);
  for (double lim : {1e100, 1e7}) {
    GeomAdaptor_Curve LC(gl, -lim, lim);
    std::printf("== line range %g\n", lim); std::fflush(stdout);
    int k = 0;
    for (TopExp_Explorer e(cone, TopAbs_EDGE); e.More(); e.Next(), ++k) {
      const TopoDS_Edge& E = TopoDS::Edge(e.Current());
      bool deg = BRep_Tool::Degenerated(E);
      if (deg) { std::printf("edge %d degenerated, skip\n", k); continue; }
      BRepAdaptor_Curve C(E);
      double f, l; BRep_Tool::Range(E, f, l);
      std::printf("edge %d type %d [%g,%g] ...", k, (int)C.GetType(), f, l); std::fflush(stdout);
      Extrema_ExtCC ext(C, LC, f, l, LC.FirstParameter(), LC.LastParameter());
      std::printf(" done=%d n=%d\n", (int)ext.IsDone(), ext.IsDone() && !ext.IsParallel() ? ext.NbExt() : -1); std::fflush(stdout);
    }
  }
  return 0;
}
