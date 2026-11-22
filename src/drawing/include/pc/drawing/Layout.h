#pragma once
// PersistCAD drawing import — S3 sheet layout. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Primitives.h"

namespace pc::drawing {

struct Scale {                      // drawing : part = num : den  (1:2 -> num 1, den 2; part mm = sheet mm * den/num)
  double num = 1, den = 1;
  float conf = 0;
  std::string evidence;             // "antet: ÖLÇEK 1:2", "ölçüler: 70.87±0.07 px/mm (4 ölçü)", "kullanıcı"
  bool notToScale = false;          // NTS: geometry gives topology only
  double factor() const { return den / num; }   // multiply sheet mm by this to get part mm
};

struct TitleBlock {
  Box2 box; float conf = 0;
  std::map<std::string, std::string> fields;  // normalised keys: "scale","material","drawingNo","title","units",
                                              // "sheet","revision","projection","generalTolerance","date","author"
  std::vector<int> textIds;
  Scale scale;
  ProjectionMethod projection = ProjectionMethod::Unknown;
  int projectionSymbolPrim = -1;    // primitive id of the truncated-cone symbol (or -1)
};

struct ViewRegion {
  int id = 0;
  Box2 box;                         // sheet mm
  ViewRole role = ViewRole::Unknown; float roleConf = 0;
  std::string label;                // "A-A", "B", "DETAY C (5:1)"
  int labelTextId = -1;
  Scale scale;                      // = title block scale unless the view label overrides it
  int sectionOfView = -1;           // Section: view carrying the cutting-plane line
  int cuttingLinePrim = -1;         // in `sectionOfView`
  std::vector<int> primIds, textIds;
  Affine2 sheetToView = kIdentityAffine;   // sheet mm -> part mm (includes 1/scale and view origin), set in S5
  bool userEdited = false;
  Attrs attrs;
};

struct SheetLayout {
  Box2 border; float borderConf = 0;
  TitleBlock title;
  std::vector<Box2> noteBlocks; std::vector<int> noteTextIds;
  std::vector<ViewRegion> views;
  Scale scaleFromDims;              // robust median of dimension values vs measured lengths (SPEC §2.5)
  std::vector<double> scaleResiduals;   // per dimension used, relative
  Attrs attrs;
};

}  // namespace pc::drawing
