// WP-3 end-to-end: hand-made perfect-S2 IR fixtures -> layout -> roles/dims -> views -> sketches -> 3D,
// volume / bbox checked against the known models, merge into a document and regenerate.
#include "wp3_helpers.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace pc::drawing;

namespace {

void endToEnd(const std::string& name) {
  const auto ex = wp3::expect(name);
  ASSERT_FALSE(ex.empty()) << name;
  DrawingIR ir;
  StageResult r = wp3::run(name, ir);
  ASSERT_TRUE(r.ok()) << name << ": " << r.message;
  // layout + scale
  EXPECT_EQ((int)ir.layout.views.size(), ex.value("views", 0)) << name;
  EXPECT_DOUBLE_EQ(ir.layout.title.scale.num, ex["scale"][0].get<double>()) << name;
  EXPECT_DOUBLE_EQ(ir.layout.title.scale.den, ex["scale"][1].get<double>()) << name;
  EXPECT_NEAR(ir.layout.scaleFromDims.factor(), ir.layout.title.scale.factor(), 1e-9) << name;
  ProjectionMethod pm;
  ASSERT_TRUE(fromString(ex.value("projection", std::string("FirstAngle")), pm));
  EXPECT_EQ(ir.views.projection, pm) << name;
  // dimensions: all recognised and bound, none out of band
  EXPECT_EQ((int)ir.annot.dims.size(), ex.value("dims", 0)) << name;
  for (auto& d : ir.annot.dims) {
    EXPECT_FALSE(d.geomPrimIds.empty()) << name << " " << d.text.raw;
    EXPECT_FALSE(d.outOfBand) << name << " " << d.text.raw << " residual " << d.residual;
  }
  // sketches: closed, fully defined
  ASSERT_FALSE(ir.sketches.empty()) << name;
  for (auto& s : ir.sketches) {
    EXPECT_EQ(s.dof, 0) << name << " sketch " << s.purpose;
    EXPECT_TRUE(s.conflicting.empty()) << name;
    EXPECT_TRUE(s.profileClosed || s.purpose == "hole-pattern") << name;
  }
  // parameters by role-derived names
  for (auto it = ex["params"].begin(); it != ex["params"].end(); ++it) {
    const ParamProposal* p = wp3::param(ir, it.key());
    ASSERT_NE(p, nullptr) << name << " missing param " << it.key();
    EXPECT_NEAR(p->nominalValue, it->get<double>(), 1e-9) << name << " " << it.key();
    EXPECT_TRUE(pc::ParamTable::isValidName(p->param.name));
  }
  // 3D: expected strategy wins; volume / bbox vs the known model; re-projection agrees with the drawing
  const SynthesisCandidate* best = wp3::chosen(ir);
  ASSERT_NE(best, nullptr);
  Strategy want;
  ASSERT_TRUE(fromString(ex.value("strategy", std::string()), want));
  EXPECT_EQ(best->strategy, want) << name << ": " << best->explanation;
  EXPECT_TRUE(best->regenerated) << name;
  const double vol = ex.value("volume", 0.0), volTol = ex.value("volTolRel", 0.005);
  EXPECT_NEAR(best->volume, vol, vol * volTol) << name;
  ASSERT_GE(best->reprojectionError, 0) << name;
  EXPECT_LT(best->reprojectionError, name == "injector_body" ? 0.15 : 0.05) << name;
  EXPECT_GT(best->score, 0.6f) << name;
  EXPECT_EQ(ir.synthesis.back().strategy, Strategy::SketchOnly) << name;   // fallback always present, ranked last
  // merge into a document that already has content -> one more body, same volume added
  pc::Document doc;
  pc::Parameter W;
  W.name = "W"; W.expression = "5";   // clashes with prismatic W
  doc.params.add(W);
  std::string err;
  std::map<std::string, std::string> ren;
  auto ids = mergeIntoDocument(*best, doc, &err, &ren);
  ASSERT_FALSE(ids.empty()) << name << ": " << err;
  if (best->fragment.params.find("W")) EXPECT_EQ(ren["W"], "W_2") << name;
  auto mr = pc::model::regenerate(doc);
  ASSERT_TRUE(mr.ok) << name;
  auto mp = pc::model::massProperties(mr.shape);
  EXPECT_NEAR(mp.volume, vol, vol * volTol) << name;
  auto bb = ex["bbox"];
  std::vector<double> got{mp.xmax - mp.xmin, mp.ymax - mp.ymin, mp.zmax - mp.zmin}, want3;
  for (auto& b : bb) want3.push_back(b.get<double>());
  std::sort(got.begin(), got.end());
  std::sort(want3.begin(), want3.end());
  for (int i = 0; i < 3; ++i) EXPECT_NEAR(got[i], want3[i], std::max(0.05, want3[i] * volTol)) << name << " bbox " << i;
  std::printf("[wp3] %-14s %-16s score %.2f vol %.1f (ref %.1f, %.3f%%) reproj %.4f mm, %zu sketches, %zu params\n",
              name.c_str(), toString(best->strategy), best->score, best->volume, vol, 100 * (best->volume - vol) / vol,
              best->reprojectionError, ir.sketches.size(), best->fragment.params.all().size());
}

}  // namespace

TEST(Wp3Pipeline, PlateFourHoles) { endToEnd("plate4"); }
TEST(Wp3Pipeline, LBracketTwoView) { endToEnd("lbracket"); }
TEST(Wp3Pipeline, FlangeBoltCircle) { endToEnd("flange"); }
TEST(Wp3Pipeline, SteppedShaft) { endToEnd("shaft"); }
TEST(Wp3Pipeline, InjectorHalfProfileSection) { endToEnd("injector_body"); }

TEST(Wp3Pipeline, PlateHolesAreHoleFeatures) {
  DrawingIR ir;
  ASSERT_TRUE(wp3::run("plate4", ir).ok());
  const auto* b = wp3::chosen(ir);
  int holes = 0;
  for (auto& f : b->fragment.features) if (f.type == pc::FeatureType::Hole) { ++holes; EXPECT_EQ(f.prop("through"), "1"); EXPECT_EQ(f.prop("diameter"), "Dh1"); }
  EXPECT_EQ(holes, 1);
  // hole sketch lies on the top face (offset = depth parameter) and carries 4 circles
  for (auto& s : ir.sketches)
    if (s.purpose == "hole-pattern") {
      EXPECT_EQ(s.sketch.plane.offsetExpr, "T");
      int circles = 0;
      for (auto& e : s.sketch.entities) circles += e.type == pc::SkEntityType::Circle;
      EXPECT_EQ(circles, 4);
    }
}

TEST(Wp3Pipeline, FlangeHolePatternAndPitchCircle) {
  DrawingIR ir;
  ASSERT_TRUE(wp3::run("flange", ir).ok());
  const auto* b = wp3::chosen(ir);
  const pc::Feature* pat = nullptr;
  for (auto& f : b->fragment.features) if (f.type == pc::FeatureType::Pattern) pat = &f;
  ASSERT_NE(pat, nullptr);
  EXPECT_EQ(pat->prop("kind"), "circular");
  EXPECT_EQ(pat->prop("count"), "n_h1");
  EXPECT_EQ(pat->prop("axis"), "Z");
  EXPECT_NE(wp3::param(ir, "D_bc1"), nullptr);
  EXPECT_EQ(wp3::param(ir, "D_bc1")->tag, ParamTag::FromDimension);
  // hole sketch offset bound to the flange thickness parameter (L1 = 12)
  for (auto& s : ir.sketches) if (s.purpose == "hole-pattern") EXPECT_EQ(s.sketch.plane.offsetExpr, "L1");
  // parametric: changing the bolt count regenerates with fewer holes (more material)
  pc::Document doc;
  std::string err;
  ASSERT_FALSE(mergeIntoDocument(*b, doc, &err).empty()) << err;
  auto m6 = pc::model::massProperties(pc::model::regenerate(doc).shape);
  auto m4 = pc::model::massProperties(pc::model::regenerate(doc, {{"n_h1", "4"}}).shape);
  EXPECT_NEAR(m4.volume - m6.volume, 2 * M_PI * 4.5 * 4.5 * 12, 1.0);
}

TEST(Wp3Pipeline, ShaftRedundantTotalBecomesReference) {
  DrawingIR ir;
  ASSERT_TRUE(wp3::run("shaft", ir).ok());
  const ParamProposal* ref = wp3::param(ir, "L_ref1");
  ASSERT_NE(ref, nullptr);
  EXPECT_EQ(ref->tag, ParamTag::Derived);
  EXPECT_DOUBLE_EQ(ref->nominalValue, 80);
  EXPECT_NE(ir.sketches[0].attrs.at("interp.redundant"), "");
  // diameters are driven through D/2 expressions, numbered along the axis
  int half = 0;
  for (auto& c : ir.sketches[0].sketch.constraints) half += c.valueExpr.find("/2") != std::string::npos;
  EXPECT_EQ(half, 3);
  EXPECT_DOUBLE_EQ(wp3::param(ir, "D1")->nominalValue, 20);
  EXPECT_DOUBLE_EQ(wp3::param(ir, "D3")->nominalValue, 25);
}

TEST(Wp3Pipeline, InjectorToleranceMidpointOption) {
  DrawingIR a, b;
  PipelineOptions po;
  ASSERT_TRUE(wp3::run("injector_body", a, po).ok());
  po.interp.preferTolMid = true;
  ASSERT_TRUE(wp3::run("injector_body", b, po).ok());
  const ParamProposal* d = wp3::param(b, "D1");
  ASSERT_NE(d, nullptr);
  EXPECT_DOUBLE_EQ(d->nominalValue, 16);
  EXPECT_EQ(d->param.expression, "15.75");                     // tolerance midpoint (drawn at 15.70)
  ASSERT_TRUE(d->tolMinus.has_value());
  EXPECT_DOUBLE_EQ(*d->tolMinus, -0.5);                        // stacked "+0 / -0.5" bound to "Ø16"
  EXPECT_EQ(wp3::param(a, "D1")->param.expression, "16");
  EXPECT_LT(b.sketches[0].maxGeomDeviation, a.sketches[0].maxGeomDeviation);   // closer to the drawn geometry
  EXPECT_EQ(wp3::chosen(b)->strategy, Strategy::SectionRevolve);
  // measured parameters close the remaining DOF, tagged [Ö]
  int measured = 0;
  for (auto& p : a.sketches[0].params) measured += p.tag == ParamTag::Measured;
  EXPECT_GT(measured, 10);
  EXPECT_EQ(a.sketches[0].dof, 0);
}

TEST(Wp3Pipeline, ThirdAngleProjectionSwapsAdjacentViewRole) {
  DrawingIR a, b;
  ASSERT_TRUE(wp3::run("plate4", a).ok());
  b.edits.push_back({Stage::Layout, edit_op::setProjection, R"({"method":"ThirdAngle"})"});
  ASSERT_TRUE(wp3::run("plate4", b).ok());
  EXPECT_EQ(a.views.projection, ProjectionMethod::FirstAngle);
  EXPECT_EQ(b.views.projection, ProjectionMethod::ThirdAngle);
  auto otherRole = [](const DrawingIR& ir) {
    for (auto& v : ir.layout.views) if (v.id != ir.views.primaryView) return v.role;
    return ViewRole::Unknown;
  };
  const std::map<ViewRole, ViewRole> mirror{{ViewRole::Top, ViewRole::Bottom}, {ViewRole::Bottom, ViewRole::Top},
                                            {ViewRole::Left, ViewRole::Right}, {ViewRole::Right, ViewRole::Left}};
  ASSERT_TRUE(mirror.count(otherRole(a))) << toString(otherRole(a));
  EXPECT_EQ(otherRole(b), mirror.at(otherRole(a)));
  // the 3D result does not depend on the naming of the views
  EXPECT_NEAR(wp3::chosen(b)->volume, wp3::chosen(a)->volume, 1e-6 * wp3::chosen(a)->volume);
}

TEST(Wp3Pipeline, StaleEditIsSkippedWithWarning) {
  DrawingIR ir;
  ir.edits.push_back({Stage::Annotate, edit_op::setDriving, R"({"dimId":999999,"driving":false})"});
  ASSERT_TRUE(wp3::run("lbracket", ir).ok());
  bool stale = false;
  for (auto& d : ir.diagnostics) stale = stale || d.code == "edit.staleRef";
  EXPECT_TRUE(stale);
}
