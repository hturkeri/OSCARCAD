#pragma once
// PersistCAD mfg — INTERNAL header (not a public contract).
//
// The analytic checks work on a light-weight "analytic CSG" model built WITHOUT OCCT booleans:
//   * every Sketch is solved on a copy (pc::sketch::solve) and placed in 3D (standard planes or
//     OnFace frames resolved analytically from earlier Extrude caps/sides and Hole floors);
//   * every Extrude becomes a Prism (sketch region x [w0,w1] along the sketch normal);
//   * every Hole location becomes a HoleLoc (cylinder + counterbore/countersink + drill tip);
//   * Pattern instances are replicated copies.
// The material classifier replays the history (join/cut/intersect/hole) for a 3D point.
// Fillets/chamfers/shell/draft/revolve are not part of the material classifier (documented limits).
#include "pc/MfgChecks.h"
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pc::mfg::detail {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBig = 1e5;  // "through all" length (mm), clipped to the part extents where used

// ------------------------------------------------------------------ 2D primitives
struct V2 {
  double x = 0, y = 0;
};
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator*(V2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
double norm(V2 a);
V2 normalized(V2 a);

enum class SegKind { Line, Arc, Circle };

// Boundary curve. Line: a->b. Arc/Circle: centre c, radius r, start angle a0 (rad), signed sweep
// (Circle: a0 = 0, sweep = 2pi). Arcs keep a/b = start/end points.
struct Seg {
  SegKind kind = SegKind::Line;
  V2 a, b, c;
  double r = 0, a0 = 0, sweep = 0;
  int entityId = -1;
  V2 point(double t) const;
  V2 tangent(double t) const;   // unit
  V2 normal(double t) const;    // unit, left of the tangent (line) / radial outward (arc, circle)
  double length() const;
  bool angleInSpan(double ang) const;
  // ray o + t d (|d| = 1): all t >= tmin
  void intersectRay(V2 o, V2 d, double tmin, std::vector<double>& out) const;
  // closest distance from point
  double distance(V2 p) const;
  // number of crossings of the +x ray from p (used for even-odd inside test)
  int crossings(V2 p) const;
};

// minimum distance between two boundary curves (0 if they intersect)
double curveDistance(const Seg& s, const Seg& h);

bool insideEvenOdd(const std::vector<Seg>& segs, V2 p);

// minimal "own" thickness of a closed region (2D ray casting along inward normals), facing walls
// only. Returns +inf if no facing pair.
double regionMinThickness(const std::vector<Seg>& segs);

// ------------------------------------------------------------------ analytic model
struct SketchGeo {
  std::string fid;
  int featIndex = -1;
  Sketch solved;
  bool solveOk = false;
  std::string solveMsg;
  bool frameOk = false;
  gp_Ax3 frame;
  std::vector<Seg> segs;                        // non-construction profile curves (sketch uv)
  std::vector<std::pair<int, V2>> holePoints;   // (pointId, uv): circle centres + free points
  bool inside(V2 p) const { return insideEvenOdd(segs, p); }
  // parameter names referenced by dimensional constraints (+ plane offset), in order
  std::vector<std::string> exprs() const;
};

enum class BodyOp { New, Join, Cut, Intersect };

struct Prism {
  std::string fid;          // feature that owns the faces ("Extrude1" or "Pattern1")
  std::string srcFid;       // source extrude id (== fid unless a pattern instance)
  int instance = 0;         // pattern instance index (0 = original)
  int featIndex = -1;       // history index of the creating feature (pattern: pattern index)
  int sketch = -1;          // index into AModel::sketches
  BodyOp op = BodyOp::Join;
  gp_Ax3 frame;             // sketch frame (possibly transformed for pattern instances)
  double w0 = 0, w1 = 0;    // local range along frame normal
  double extSign = 1;       // +1 if the extrusion goes along +normal ("end" cap at w1), -1 otherwise
  bool through = false;
  double draft = 0;
  std::string distanceExpr;
  std::vector<std::string> bodies;  // v2: bodies this op acts on / creates (empty = auto: all touched bodies)
  std::vector<Seg> segs;    // profile curves in sketch uv (copied from the sketch)
  bool contains(const gp_Pnt& p) const;
  bool inside2d(V2 p) const { return insideEvenOdd(segs, p); }
  V2 toLocal(const gp_Pnt& p, double* w = nullptr) const;
  gp_Pnt toWorld(V2 uv, double w) const;
  gp_Dir normal() const { return frame.Direction(); }
  std::string faceName(const std::string& rest) const;  // "<fid>.<rest>" or "<fid>.i<k>.<src>.<rest>"
};

struct HoleLoc {
  std::string fid, srcFid;
  int instance = 0;
  int featIndex = -1;
  int sketch = -1;
  int k = 0;                // location index (ordered by (u,v))
  int pointId = -1;
  V2 uv;
  gp_Pnt top;               // on the sketch plane
  gp_Dir axis;              // drilling direction (= -sketch normal)
  double r = 0;             // drill radius
  double depth = 0;         // cylindrical depth (blind)
  double tipLen = 0;        // cone length of the drill tip (blind, tipAngle > 0)
  bool through = false;
  int kind = 0;             // 0 simple, 1 counterbore, 2 countersink
  double cbR = 0, cbDepth = 0, csR = 0, csLen = 0;
  std::string diameterExpr, depthExpr;
  std::vector<std::string> bodies;  // v2: explicit target bodies (empty = auto)
  double radiusAt(double s) const;   // removal radius at axial position s (<0 = none)
  double totalLen() const { return through ? kBig : depth + tipLen; }
  bool contains(const gp_Pnt& p) const;
  std::string faceName(const std::string& rest) const;
};

struct EdgeTreat {   // Fillet / Chamfer
  std::string fid;
  bool chamfer = false;
  double size = 0;
  std::string expr;
  std::vector<std::string> refs;
};

struct ShellInfo {
  std::string fid;
  double thickness = 0;
  std::string expr;
  std::vector<std::string> refs;
  bool outward = false;
};

struct RevolveInfo {
  std::string fid;
  int sketch = -1;
  double angle = 360;
};

// v3: Combine (body-to-body boolean, pc/Feature.h) in the material history
struct CombineOp {
  std::string fid;
  BodyOp op = BodyOp::Join;           // Join | Cut | Intersect
  std::string target;
  std::vector<std::string> tools;
  bool keepTools = false;
};

struct OpRef {
  bool hole = false;
  int idx = 0;
  bool combine = false;               // v3: idx indexes AModel::combines
};

struct AModel {
  const Document* doc = nullptr;
  ParamValues values;
  std::vector<SketchGeo> sketches;
  std::map<std::string, int> sketchIndex;
  std::vector<Prism> prisms;
  std::vector<HoleLoc> holes;
  std::vector<OpRef> ops;                 // material history
  std::vector<EdgeTreat> edgeTreats;
  std::vector<ShellInfo> shells;
  std::vector<RevolveInfo> revolves;
  std::vector<std::string> unsolvedSketches;
  double scale = 1;                       // characteristic part size (mm)
  std::vector<std::string> bodyNames;     // v2: bodies in creation order (analytic simulation of the BODIES contract)
  std::map<std::string, std::string> featureBody;  // v2: feature id -> body ("" if not determinable)
  std::map<std::string, int> bodyBorn;            // v3: body -> index into `ops` of its creating op
  std::vector<CombineOp> combines;                // v3
  bool multiBody() const { return bodyNames.size() > 1; }
  // material of one body only (ops whose target list is empty or contains `body`, from the body's
  // creation on; Combine ops act on their target / consume their tools)
  bool materialOfBody(const gp_Pnt& p, const std::string& body, int skipHole = -1) const;
  bool materialOfBodyUpTo(const gp_Pnt& p, const std::string& body, int skipHole, int opEnd) const;

  bool material(const gp_Pnt& p, int skipHole = -1) const;
  const SketchGeo* sketch(const std::string& fid) const;
  const Prism* prismByFid(const std::string& fid) const;   // original (instance 0)
  // planar face (point + OUTWARD normal) of a persistent face name, if analytically known
  bool facePlane(const std::string& faceName, gp_Pnt& p, gp_Dir& n) const;
  // part extents along a direction (min, max) from join/new prisms + revolve estimate
  bool extents(const gp_Dir& d, double& lo, double& hi) const;
};

// Build the analytic model (never throws; features whose props fail to evaluate are skipped).
AModel buildModel(const Document& doc, const ParamValues& values);

// v3 "analytic phase 2": frames of sketches the geometry-free model cannot place by itself (OnPlane sketches on
// datum planes; OnFace sketches on faces it does not know). Installed (thread-local) by validateChange after
// regeneration for the whole analytic run, including the re-builds of the suggestion probes; buildModel asks it
// for every sketch whose frame is not analytically known. Without a provider (plain checkAnalytic) those
// sketches stay unplaced and their features are skipped, exactly as before.
struct AModel;
struct FrameProvider {
  virtual ~FrameProvider() = default;
  // `partial` = the analytic model built so far (every feature before the sketch)
  virtual bool frame(const Feature& sketchFeature, const ParamValues& values, const AModel& partial, gp_Ax3& out) const = 0;
};
const FrameProvider* currentFrameProvider();
class FrameScope {
 public:
  explicit FrameScope(const FrameProvider* p);
  ~FrameScope();
  FrameScope(const FrameScope&) = delete;
  FrameScope& operator=(const FrameScope&) = delete;

 private:
  const FrameProvider* prev_;
};
// true if the document has sketches that need a FrameProvider (OnPlane sketches)
bool needsPhase2(const Document& doc);
// analytic checks with frames from a regenerated model (phase 2); falls back to checkAnalyticImpl semantics
Report checkAnalyticWithModel(const Document& doc, const ParamValues& values, const MfgSettings& s,
                              const model::ModelResult& model);

// ------------------------------------------------------------------ helpers
std::string fmt(double v, int prec = 2);
gp_Dir safeDir(double x, double y, double z, const gp_Dir& def);
double evalProp(const Feature& f, const std::string& key, const ParamValues& v, double def, bool* ok = nullptr);
bool isConvMilling(const ConvSettings& c);
bool amUsesSupports(AmProcess p);
bool amIsPowderOrResin(AmProcess p);
const char* amName(AmProcess p);
const char* convName(ConvProcess p);
double amBridgeLimit(AmProcess p);

// Parameter selection + suggestions ----------------------------------------------------------
// Ordered candidates: names referenced by `exprs` in order; exposed ones first; non-exposed ones are
// followed by their (transitive) dependencies.
std::vector<std::string> orderCandidates(const Document& doc, const std::vector<std::string>& exprs);

using Metric = std::function<std::optional<double>(const AModel&)>;
enum class Want { AtLeast, AtMost };   // metric >= limit  |  metric <= limit
struct Suggestion {
  std::vector<std::string> params;      // drivers, params[0] = the one the value is for
  std::optional<double> value;
  int relation = 0;                     // +1: params[0] >= value, -1: params[0] <= value, 0: = value
};
// Probe the parameters (re-building the analytic model) to find the drivers of `metric`, and solve
// the first driver so that metric meets `limit`.
Suggestion suggestByProbe(const Document& doc, const ParamValues& values, const std::vector<std::string>& candidates,
                          const Metric& metric, double limit, Want want);
// Pure-expression version (no rebuild): metric = expr evaluated with doc-consistent values.
Suggestion suggestByExpr(const Document& doc, const ParamValues& values, const std::string& expr, double limit,
                         Want want);
Suggestion suggestExactByExpr(const Document& doc, const ParamValues& values, const std::string& expr, double target);
std::optional<double> solve1DPublic(const std::function<std::optional<double>(double)>& g, double x0, double lo,
                                    double hi);
// Evaluate doc parameters with a single override (dependent parameters re-evaluated).
std::optional<ParamValues> evalWithOverride(const Document& doc, const ParamValues& values, const std::string& param,
                                            double value);
std::map<std::string, std::string> fixedOverrides(const Document& doc, const ParamValues& values);

// Issue merge: same ruleId + featureId -> keep the most specific (analytic params/suggestion,
// geometric faces/edges, worst actual).
void mergeInto(std::vector<Issue>& dst, const std::vector<Issue>& src);
void dedupe(std::vector<Issue>& issues);
void mergeByFaces(const Document& doc, std::vector<Issue>& dst, const std::vector<Issue>& src);  // v3 phase 2

Report checkAnalyticImpl(const Document& doc, const ParamValues& values, const MfgSettings& s);
// v2 multi-body helpers (MfgChecks.cpp)
std::vector<std::string> splitNames(const std::string& csv);
// body list of a model: model.bodies, or the solids of model.shape ("Body1..n") when empty
std::vector<model::BodyInfo> bodiesOf(const Document& doc, const model::ModelResult& model);
Report checkBodiesImpl(const Document& doc, const model::ModelResult& model, const MfgSettings& s);
Report checkGeometricImpl(const Document& doc, const model::ModelResult& model, const MfgSettings& s);

}  // namespace pc::mfg::detail
