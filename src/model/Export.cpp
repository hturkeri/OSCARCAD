// PersistCAD model — mass properties and STEP/STL export.
#include "pc/Model.h"
#include "pc/SimdState.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>

namespace pc::model {

MassProps massProperties(const TopoDS_Shape& s) {
  MassProps m;
  if (s.IsNull()) return m;
  resetSimdUpperState();
  try {
    GProp_GProps v;
    BRepGProp::VolumeProperties(s, v);
    m.volume = v.Mass();
    if (std::abs(m.volume) > 1e-12) {
      gp_Pnt c = v.CentreOfMass();
      m.cx = c.X();
      m.cy = c.Y();
      m.cz = c.Z();
    }
    GProp_GProps a;
    BRepGProp::SurfaceProperties(s, a);
    m.area = a.Mass();
    Bnd_Box b;
    BRepBndLib::AddOptimal(s, b, Standard_False, Standard_False);
    if (!b.IsVoid()) b.Get(m.xmin, m.ymin, m.zmin, m.xmax, m.ymax, m.zmax);
  } catch (...) {
  }
  return m;
}

bool exportSTEP(const TopoDS_Shape& s, const std::string& path, std::string* err) {
  if (s.IsNull()) {
    if (err) *err = "Boş şekil dışa aktarılamaz";
    return false;
  }
  try {
    STEPControl_Writer w;
    Interface_Static::SetCVal("write.step.unit", "MM");
    if (w.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
      if (err) *err = "STEP aktarımı başarısız";
      return false;
    }
    if (w.Write(path.c_str()) != IFSelect_RetDone) {
      if (err) *err = "STEP dosyası yazılamadı: " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (err) *err = std::string("STEP hatası: ") + (e.GetMessageString() ? e.GetMessageString() : "");
  } catch (...) {
    if (err) *err = "STEP hatası";
  }
  return false;
}

bool exportSTL(const TopoDS_Shape& s, const std::string& path, double linDefl, std::string* err) {
  if (s.IsNull()) {
    if (err) *err = "Boş şekil dışa aktarılamaz";
    return false;
  }
  try {
    BRepMesh_IncrementalMesh mesh(s, linDefl > 0 ? linDefl : 0.05, Standard_False, 0.35, Standard_True);
    StlAPI_Writer w;
    w.ASCIIMode() = Standard_False;
    if (!w.Write(s, path.c_str())) {
      if (err) *err = "STL dosyası yazılamadı: " + path;
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    if (err) *err = std::string("STL hatası: ") + (e.GetMessageString() ? e.GetMessageString() : "");
  } catch (...) {
    if (err) *err = "STL hatası";
  }
  return false;
}

} // namespace pc::model
