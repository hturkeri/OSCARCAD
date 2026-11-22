#pragma once
// PersistCAD core — manufacturing (DfAM / DfM) settings stored in the document and in apps.
// Limits in mm / degrees. Rule logic lives in src/mfg.
#include <string>

namespace pc {

enum class AmProcess { None, LPBF_Metal, EBM_Metal, DED_Metal, FDM_Polymer, SLA_Resin, SLS_Polymer };
enum class ConvProcess { None, CNC_Milling3Axis, CNC_Milling5Axis, CNC_Turning };

const char* toString(AmProcess p);
const char* toString(ConvProcess p);
bool fromString(const std::string& s, AmProcess& out);
bool fromString(const std::string& s, ConvProcess& out);

struct AmSettings {
  bool enabled = true;
  AmProcess process = AmProcess::LPBF_Metal;
  std::string material = "Ti-6Al-4V";
  double minWallThickness = 0.4;       // min printable wall
  double minFeatureSize = 0.3;         // min pin / rib width
  double minHoleDiameter = 0.5;        // min open hole
  double maxSelfSupportingHoleDia = 8; // horizontal holes larger than this need support
  double minOverhangAngle = 45;        // surfaces flatter than this (from build plate) need support
  double minGap = 0.3;                 // min gap between walls (powder removal / fusion)
  double maxAspectRatio = 8;           // height / thickness for thin free-standing walls/pins
  double minEscapeHoleDia = 2.0;       // powder/resin escape hole for enclosed cavities
  double buildX = 250, buildY = 250, buildZ = 300; // build envelope
  double buildDirX = 0, buildDirY = 0, buildDirZ = 1; // build direction (unit-ish)
  double layerThickness = 0.03;
};

struct ConvSettings {
  bool enabled = true;
  ConvProcess process = ConvProcess::CNC_Milling3Axis;
  std::string material = "Al 7075";
  double minToolDiameter = 3.0;        // smallest end mill -> min internal vertical corner radius = D/2
  double maxToolLengthToDia = 4.0;     // max pocket depth / tool diameter
  double maxHoleDepthToDia = 10.0;     // drilling L/D
  double minWallThickness = 0.8;       // metal; plastics typically 1.5
  double maxWallHeightToThickness = 15.0;
  double minHoleDiameter = 1.0;
  bool preferStandardDrills = true;    // warn when hole diameter is not a standard drill size
  double stockX = 300, stockY = 300, stockZ = 150; // machine / stock envelope
  double toolAxisX = 0, toolAxisY = 0, toolAxisZ = 1; // 3-axis: spindle direction (accessibility)
};

struct MfgSettings {
  AmSettings am;
  ConvSettings conv;
};

} // namespace pc
