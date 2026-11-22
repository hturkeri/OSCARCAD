// pc_kesit — verification helper: dumps the planar section of every body of a .pcad model as polylines (JSON).
// Used by docs/enjektor/dogrulama/bindirme.py to overlay the model section on the source drawing.
//   pc_kesit model.pcad out.json [--plane xz|yz|xy] [--offset d] [--edges] [--set NAME=EXPR ...]
// --edges additionally writes every body edge as a 3D polyline ("edges3d") for projected (outside) views.
// Output: {"plane":"xz","offset":0,"bodies":[{"name":..,"volume":..,"solids":n,"polylines":[[[u,v],...],...]}]}
// xz -> (u,v) = (x,z); yz -> (y,z); xy -> (x,y).
#include "pc/Document.h"
#include "pc/Model.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <map>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "kullanim: pc_kesit model.pcad out.json [--plane xz|yz|xy] [--offset d] [--set N=V ...]\n");
    return 64;
  }
  std::string plane = "xz";
  double offset = 0;
  bool edges3d = false;
  std::map<std::string, std::string> ov;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--plane" && i + 1 < argc) plane = argv[++i];
    else if (a == "--edges") edges3d = true;
    else if (a == "--offset" && i + 1 < argc) offset = std::stod(argv[++i]);
    else if (a == "--set" && i + 1 < argc) {
      std::string kv = argv[++i];
      auto eq = kv.find('=');
      if (eq != std::string::npos) ov[kv.substr(0, eq)] = kv.substr(eq + 1);
    }
  }
  pc::Document doc;
  std::string err;
  if (!pc::loadDocument(doc, argv[1], &err)) {
    std::fprintf(stderr, "okunamadi: %s\n", err.c_str());
    return 1;
  }
  pc::model::ModelResult r = pc::model::regenerate(doc, ov);
  for (auto& st : r.status)
    if (!st.ok) std::fprintf(stderr, "HATA %s: %s\n", st.featureId.c_str(), st.message.c_str());
  gp_Pln pln = plane == "yz"   ? gp_Pln(gp_Pnt(offset, 0, 0), gp_Dir(1, 0, 0))
               : plane == "xy" ? gp_Pln(gp_Pnt(0, 0, offset), gp_Dir(0, 0, 1))
                               : gp_Pln(gp_Pnt(0, offset, 0), gp_Dir(0, 1, 0));
  nlohmann::json out;
  out["plane"] = plane;
  out["offset"] = offset;
  out["ok"] = r.ok;
  out["bodies"] = nlohmann::json::array();
  for (auto& b : r.bodies) {
    nlohmann::json jb;
    jb["name"] = b.name;
    jb["volume"] = pc::model::massProperties(b.shape).volume;
    int ns = 0;
    for (TopExp_Explorer ex(b.shape, TopAbs_SOLID); ex.More(); ex.Next()) ++ns;
    jb["solids"] = ns;
    jb["polylines"] = nlohmann::json::array();
    BRepAlgoAPI_Section sec(b.shape, pln, Standard_False);
    sec.Approximation(Standard_False);
    sec.Build();
    if (sec.IsDone())
      for (TopExp_Explorer ex(sec.Shape(), TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        GCPnts_QuasiUniformDeflection disc(c, 0.002);
        if (!disc.IsDone()) continue;
        nlohmann::json pl = nlohmann::json::array();
        for (int k = 1; k <= disc.NbPoints(); ++k) {
          gp_Pnt p = disc.Value(k);
          if (plane == "yz") pl.push_back({p.Y(), p.Z()});
          else if (plane == "xy") pl.push_back({p.X(), p.Y()});
          else pl.push_back({p.X(), p.Z()});
        }
        jb["polylines"].push_back(pl);
      }
    if (edges3d) {
      jb["edges3d"] = nlohmann::json::array();
      for (TopExp_Explorer ex(b.shape, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        GCPnts_QuasiUniformDeflection disc(c, 0.002);
        if (!disc.IsDone()) continue;
        nlohmann::json pl = nlohmann::json::array();
        for (int k = 1; k <= disc.NbPoints(); ++k) {
          gp_Pnt p = disc.Value(k);
          pl.push_back({p.X(), p.Y(), p.Z()});
        }
        jb["edges3d"].push_back(pl);
      }
    }
    out["bodies"].push_back(jb);
  }
  std::ofstream f(argv[2]);
  f << out.dump();
  return r.ok ? 0 : 2;
}
