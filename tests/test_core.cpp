#include <filesystem>
#include "pc/Document.h"
#include "pc/Expression.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <unistd.h>

using namespace pc;

static VarLookup none() { return [](const std::string&) { return std::optional<double>(); }; }

TEST(Expression, Arithmetic) {
  EXPECT_DOUBLE_EQ(evaluateExpression("1+2*3", none()), 7);
  EXPECT_DOUBLE_EQ(evaluateExpression("(1+2)*3", none()), 9);
  EXPECT_DOUBLE_EQ(evaluateExpression("-2^2", none()), -4);
  EXPECT_DOUBLE_EQ(evaluateExpression("2^3^2", none()), 512);
  EXPECT_NEAR(evaluateExpression("sin(30)", none()), 0.5, 1e-12);
  EXPECT_NEAR(evaluateExpression("atan2(1,1)", none()), 45, 1e-12);
  EXPECT_DOUBLE_EQ(evaluateExpression("max(1, 5, 3)", none()), 5);
  EXPECT_DOUBLE_EQ(evaluateExpression("1.5e2", none()), 150);
  EXPECT_THROW(evaluateExpression("1+", none()), ExprError);
  EXPECT_THROW(evaluateExpression("foo(1)", none()), ExprError);
  EXPECT_THROW(evaluateExpression("X", none()), ExprError);
}

TEST(Expression, Names) {
  auto n = referencedNames("W/2 + max(T, pi) - W");
  ASSERT_EQ(n.size(), 2u);
  EXPECT_EQ(n[0], "W");
  EXPECT_EQ(n[1], "T");
  EXPECT_EQ(singleIdentifier(" T ").value(), "T");
  EXPECT_FALSE(singleIdentifier("T*2"));
  EXPECT_EQ(renameIdentifier("W/2+max(W,Wx)", "W", "Width"), "Width/2+max(Width,Wx)");
  EXPECT_EQ(formatNumber(12.5), "12.5");
  EXPECT_EQ(formatNumber(40), "40");
}

TEST(Params, EvaluateOrderAndCycles) {
  ParamTable t;
  ASSERT_TRUE(t.add({"H", "W/2"}));
  ASSERT_TRUE(t.add({"W", "40"}));
  auto v = t.evaluate();
  ASSERT_TRUE(v);
  EXPECT_DOUBLE_EQ((*v)["H"], 20);
  auto v2 = t.evaluate(nullptr, {{"W", "10"}});
  EXPECT_DOUBLE_EQ((*v2)["H"], 5);
  t.setExpression("W", "H*2");
  std::vector<ParamError> errs;
  EXPECT_FALSE(t.evaluate(&errs));
  EXPECT_FALSE(errs.empty());
  EXPECT_FALSE(t.add({"sin", "1"}));
}

TEST(Params, Ranges) {
  ParamTable t;
  Parameter p{"T", "0.2"};
  p.minValue = 0.5;
  t.add(p);
  auto v = t.evaluate();
  EXPECT_EQ(t.checkRanges(*v).size(), 1u);
}

TEST(Document, JsonRoundTrip) {
  Document d;
  d.name = "Bracket";
  Parameter w{"W", "60"};
  w.exposed = true;
  w.label = "Genişlik";
  w.minValue = 10;
  d.params.add(w);
  Feature sk;
  sk.id = "Sketch1";
  sk.type = FeatureType::Sketch;
  sk.sketch = Sketch::create();
  auto lines = sk.sketch->addRectangle(0, 0, 60, 40);
  sk.sketch->addConstraint(SkConstraintType::Length, {lines[0]}, "W");
  d.features.push_back(sk);
  Feature ex;
  ex.id = "Extrude1";
  ex.type = FeatureType::Extrude;
  ex.props = {{"sketch", "Sketch1"}, {"distance", "5"}};
  d.features.push_back(ex);
  d.mfg.am.minWallThickness = 0.7;

  auto j = toJson(d);
  Document e = documentFromJson(nlohmann::json::parse(j.dump()));
  EXPECT_EQ(e.name, "Bracket");
  ASSERT_EQ(e.features.size(), 2u);
  ASSERT_TRUE(e.features[0].sketch);
  EXPECT_EQ(e.features[0].sketch->entities.size(), 4u);
  EXPECT_EQ(e.features[0].sketch->constraints.back().valueExpr, "W");
  EXPECT_EQ(e.features[1].prop("distance"), "5");
  EXPECT_TRUE(e.params.find("W")->exposed);
  EXPECT_DOUBLE_EQ(e.params.find("W")->minValue, 10);
  EXPECT_TRUE(std::isinf(e.params.find("W")->maxValue));
  EXPECT_DOUBLE_EQ(e.mfg.am.minWallThickness, 0.7);
  EXPECT_EQ(toJson(e).dump(), j.dump());
}

TEST(Document, SampleBracketLoads) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  EXPECT_EQ(d.features.size(), 9u);
  auto v = d.evaluateParams();
  EXPECT_DOUBLE_EQ(v["Lbore"], 24);
}

// Regression (QA review): short mfg arrays used const operator[] out of range (undefined behaviour).
TEST(Document, ShortMfgArraysAreRejectedNotUB) {
  for (const char* sect : {"additive", "conventional"}) {
    nlohmann::json j = toJson(Document());
    const char* key = std::string(sect) == "additive" ? "build" : "stock";
    j["mfg"][sect][key] = nlohmann::json::array({250});
    EXPECT_THROW(documentFromJson(j), std::runtime_error) << sect;
  }
}

// Regression (QA review): a failing serialisation (invalid UTF-8) must not truncate an existing file.
TEST(Document, SaveDoesNotTruncateOnSerialisationError) {
  const std::string p = (std::filesystem::temp_directory_path().u8string() + "/test_core_save_") + std::to_string(::getpid()) + ".pcad";
  Document good;
  good.name = "Good";
  ASSERT_TRUE(saveDocument(good, p));
  Document bad;
  bad.name = std::string("bad\xff\xfe", 5);
  std::string err;
  EXPECT_FALSE(saveDocument(bad, p, &err));
  EXPECT_FALSE(err.empty());
  Document back;
  ASSERT_TRUE(loadDocument(back, p, &err)) << err;
  EXPECT_EQ(back.name, "Good");
  std::remove(p.c_str());
}
