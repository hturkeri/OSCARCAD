#pragma once
// WP-3 private helpers shared by interp/ and synth/ (not part of the contract).
#include "pc/drawing/Confidence.h"
#include "pc/drawing/DimGrammar.h"
#include "pc/drawing/Pipeline.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace pc::drawing::interp {

inline constexpr double kPi = 3.14159265358979323846;
inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }
inline Vec2 unit(Vec2 v) { double l = length(v); return l > 1e-15 ? Vec2{v.x / l, v.y / l} : Vec2{1, 0}; }
inline Vec2 perp(Vec2 v) { return {-v.y, v.x}; }

// ---------------------------------------------------------------- primitive geometry (sheet mm)
bool isCurve(const Primitive& p);                 // Line / Arc / Circle
Box2 primBox(const Primitive& p);
double primLength(const Primitive& p);
Vec2 primMid(const Primitive& p);
double distToPrim(const Primitive& p, Vec2 q);    // to the curve (Line/Arc/Circle/Polyline); others: to p0
std::vector<Vec2> samplePrim(const Primitive& p, double step);
std::vector<Vec2> primEndpoints(const Primitive& p);   // Line: 2, Arc: 2, others: none
bool isDashed(const Primitive& p);
bool isCenterDash(const Primitive& p);            // dash-dot (>= 4 entries, long/short)
double distPointSeg(Vec2 q, Vec2 a, Vec2 b, double* t = nullptr);
bool angleInArc(double angDeg, double a0, double a1);
// Intersections of an infinite line (p + t*d) with a primitive's curve: parameters t
std::vector<double> rayHits(const Primitive& p, Vec2 o, Vec2 d);

// ---------------------------------------------------------------- texts
Box2 textBox(const TextRun& t);
Vec2 textCentre(const TextRun& t);
std::string foldUpper(const std::string& utf8);   // Turkish-aware ASCII fold + upper ("Ölçek" -> "OLCEK")
std::string trim(const std::string& s);

// Dimension text: WP-2 grammar (parseDimText).
DimText parseDim(const std::string& text, const DimTextContext& ctx = {});
bool parseScaleText(const std::string& s, double& num, double& den);   // "1:2", "2 : 1", "ÖLÇEK 1:2"

// ---------------------------------------------------------------- view helpers
// Visible-role primitives of a view (sheet mm bounding box of them)
Box2 visibleBox(const DrawingIR& ir, const ViewRegion& v);
std::vector<const Primitive*> viewPrims(const DrawingIR& ir, const ViewRegion& v, std::initializer_list<LineRole> roles);
double viewScaleFactor(const ViewRegion& v);      // part mm per sheet mm

// ---------------------------------------------------------------- attrs
std::string fmt(double v, int prec = 6);
double attrNum(const Attrs& a, const std::string& k, double def = 0);
int attrInt(const Attrs& a, const std::string& k, int def = -1);

// Diagnostic helper
void diag(StageResult& r, Severity s, const std::string& code, const std::string& msg, Stage st, std::vector<int> ids = {});


// Parameter value lookup for proposals of a candidate
ParamValues proposalValues(const std::vector<ParamProposal>& ps);

}  // namespace pc::drawing::interp
