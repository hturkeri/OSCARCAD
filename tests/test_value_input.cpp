// SC23 (WP-O): ValueInput / operation state tests (docs/sc2023/ANALYSIS.md §4.2, §4.5, O-P0-3).
#include "OperationState.h"
#include "ValueInput.h"
#include "pc/Expression.h"
#include <QStringList>
#include <gtest/gtest.h>
#include <cmath>
#include <map>

using pcd::LargeValueGate;
using pcd::ValueInput;
using S = ValueInput::Status;
using Q = ValueInput::Quantity;

namespace {
// The designer evaluates through IDesignHost::evaluateExpression (pc::Expression + the document's parameters);
// the same evaluator with a fixed parameter table here.
ValueInput::Evaluator evaluator(std::map<std::string, double> params = {{"L", 20.0}, {"T", 5.0}, {"Pull_d", 12.5}}) {
  return [params](const QString& e, double* v, QString* err) {
    try {
      const double r = pc::evaluateExpression(e.toStdString(), [&](const std::string& n) -> std::optional<double> {
        auto it = params.find(n);
        if (it == params.end()) return std::nullopt;
        return it->second;
      });
      if (v) *v = r;
      return true;
    } catch (const std::exception& ex) {
      if (err) *err = QString::fromUtf8(ex.what());
      return false;
    }
  };
}
ValueInput::Result len(const QString& t, double diag = 0) {
  ValueInput::Options o;
  o.quantity = Q::Length;
  o.modelDiagonal = diag;
  return ValueInput::evaluate(t, evaluator(), o);
}
ValueInput::Result ang(const QString& t) {
  ValueInput::Options o;
  o.quantity = Q::Angle;
  return ValueInput::evaluate(t, evaluator(), o);
}
} // namespace

// ---------------------------------------------------------------- OperationState (P0 frozen header)
TEST(OperationState, StateTextsAreNonEmpty) {
  for (int s = int(pcd::OpState::Idle); s <= int(pcd::OpState::Cancelled); ++s)
    EXPECT_FALSE(pcd::opStateText(pcd::OpState(s)).isEmpty()) << s;
}

TEST(OperationState, StatusEquality) {
  pcd::OpStatus a, b;
  EXPECT_EQ(a.state, pcd::OpState::Idle);
  EXPECT_TRUE(a == b);
  b.reason = "Mesafe sıfır olamaz";
  EXPECT_TRUE(a != b);
  b = a;
  b.unit = "mm";
  EXPECT_TRUE(a != b);
}

// ---------------------------------------------------------------- plain numbers, expressions, parameters
TEST(ValueInput, PlainNumber) {
  const auto r = len("15");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 15.0);
  EXPECT_EQ(r.expr, "15");
  EXPECT_EQ(r.displayUnit, "mm");
  EXPECT_TRUE(r.unitTyped.isEmpty());
  EXPECT_FALSE(r.reversed);
  EXPECT_TRUE(r.ok());
}

TEST(ValueInput, WhitespaceAndDecimalComma) {
  EXPECT_DOUBLE_EQ(len("  12.5 ").value, 12.5);
  const auto r = len("1,5");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 1.5);
  const auto c = len("1,5 cm");
  EXPECT_EQ(c.status, S::Ok);
  EXPECT_DOUBLE_EQ(c.value, 15.0);
  // function arguments keep their commas
  const auto m = len("max(3,7)");
  EXPECT_EQ(m.status, S::Ok);
  EXPECT_DOUBLE_EQ(m.value, 7.0);
}

TEST(ValueInput, ExpressionsAndParametersKeepWorking) {
  auto r = len("L");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 20.0);
  EXPECT_EQ(r.expr, "L");   // the parameter name reaches the model unchanged (relation kept)
  r = len("L/2 + T");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 15.0);
  EXPECT_EQ(r.expr, "L/2 + T");
  r = len("2*Pull_d");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 25.0);
  r = len("1e2");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 100.0);
}

// ---------------------------------------------------------------- units
TEST(ValueInput, LengthUnits) {
  struct C { const char* t; double mm; };
  const C cases[] = {{"15 mm", 15}, {"15mm", 15}, {"1.5 cm", 15}, {"0.5 in", 12.7}, {"0.5in", 12.7},
                     {"2 m", 2000}, {"1\"", 25.4}, {"0.5 \"", 12.7}, {"(L) cm", 200}, {"L*2 mm", 40}};
  for (const C& c : cases) {
    const auto r = len(c.t);
    EXPECT_EQ(r.status, S::Ok) << c.t << " " << r.reason.toStdString();
    EXPECT_NEAR(r.value, c.mm, 1e-9) << c.t;
  }
}

TEST(ValueInput, UnitConversionGivesModelExpression) {
  EXPECT_EQ(len("1.5 cm").expr, "15");
  EXPECT_EQ(len("15 mm").expr, "15");
  EXPECT_EQ(len("0.5 in").expr, "12.7");
  EXPECT_EQ(len("1.5 cm").unitTyped, "cm");
  // a parameter expression with a unit stays relational: (expr)*factor
  const auto r = len("(L) cm");
  EXPECT_EQ(r.expr, "(L)*10");
  double back = 0;
  ASSERT_TRUE(evaluator()(r.expr, &back, nullptr));
  EXPECT_DOUBLE_EQ(back, r.value);
  const auto mm = len("L mm");   // "L" is a bare identifier + unit
  EXPECT_EQ(mm.status, S::BadExpression) << "a bare identifier is not split: 'L mm' is not an expression";
}

TEST(ValueInput, AngleUnits) {
  auto r = ang("90");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_EQ(r.displayUnit, "°");
  EXPECT_DOUBLE_EQ(r.value, 90);
  r = ang("90°");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_DOUBLE_EQ(r.value, 90);
  r = ang("45 deg");
  EXPECT_DOUBLE_EQ(r.value, 45);
  r = ang("3.141592653589793 rad");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_NEAR(r.value, 180.0, 1e-9);
  r = ang("0.5rad");
  EXPECT_NEAR(r.value, 28.64788975654116, 1e-9);
}

TEST(ValueInput, UnknownUnitIsRefusedWithReason) {
  const auto r = len("10 xyz");
  EXPECT_EQ(r.status, S::UnknownUnit);
  EXPECT_TRUE(r.reason.contains("xyz")) << r.reason.toStdString();
  EXPECT_TRUE(r.reason.contains("bilinmeyen birim"));
  EXPECT_TRUE(r.reason.contains("mm, cm, m, in"));
  EXPECT_FALSE(r.acceptable());
}

TEST(ValueInput, WrongUnitKind) {
  auto r = len("10 deg");
  EXPECT_EQ(r.status, S::WrongUnitKind);
  EXPECT_TRUE(r.reason.contains("açı")) << r.reason.toStdString();
  r = len("10°");
  EXPECT_EQ(r.status, S::WrongUnitKind);
  r = ang("10 mm");
  EXPECT_EQ(r.status, S::WrongUnitKind);
  EXPECT_TRUE(r.reason.contains("uzunluk"));
  ValueInput::Options o;
  o.quantity = Q::Scalar;
  r = ValueInput::evaluate("3 mm", evaluator(), o);
  EXPECT_EQ(r.status, S::WrongUnitKind);
}

TEST(ValueInput, UnitFactorTable) {
  double f = 0;
  Q k = Q::Scalar;
  EXPECT_TRUE(ValueInput::unitFactor("cm", Q::Length, &f, &k));
  EXPECT_DOUBLE_EQ(f, 10);
  EXPECT_EQ(k, Q::Length);
  EXPECT_FALSE(ValueInput::unitFactor("rad", Q::Length, &f, &k));
  EXPECT_EQ(k, Q::Angle);
  EXPECT_FALSE(ValueInput::unitFactor("furlong", Q::Length, &f, &k));
  EXPECT_EQ(k, Q::Scalar);
  EXPECT_EQ(ValueInput::displayUnit(Q::Length), "mm");
  EXPECT_EQ(QString::fromLatin1(pcd::kDocumentLengthUnit), "mm");
}

// ---------------------------------------------------------------- invalid text
TEST(ValueInput, InvalidTextHasReason) {
  const auto r = len("abc");
  EXPECT_EQ(r.status, S::BadExpression);
  EXPECT_TRUE(r.reason.contains("‘abc’")) << r.reason.toStdString();
  EXPECT_TRUE(r.reason.contains("bir sayı, parametre adı ya da ifade değil"));
  EXPECT_EQ(len("2*").status, S::BadExpression);
  EXPECT_EQ(len("(3").status, S::BadExpression);
  EXPECT_EQ(len("cm").status, S::BadExpression);
  EXPECT_EQ(len("unknown_param").status, S::BadExpression);
}

TEST(ValueInput, EmptyText) {
  const auto r = len("   ");
  EXPECT_EQ(r.status, S::Empty);
  EXPECT_FALSE(r.reason.isEmpty());
}

TEST(ValueInput, NoEvaluatorRefusesEverything) {
  const auto r = ValueInput::evaluate("15", ValueInput::Evaluator(), ValueInput::Options());
  EXPECT_EQ(r.status, S::BadExpression);
}

// ---------------------------------------------------------------- zero / negative
TEST(ValueInput, ZeroIsRefusedWithReason) {
  for (const char* t : {"0", "0 mm", "0.0", "L-20", "0 cm"}) {
    const auto r = len(t);
    EXPECT_EQ(r.status, S::Zero) << t;
    EXPECT_TRUE(r.reason.startsWith("Mesafe sıfır olamaz")) << r.reason.toStdString();
  }
  EXPECT_TRUE(ang("0").reason.startsWith("Açı sıfır olamaz"));
  ValueInput::Options o;
  o.what = "Kalınlık";
  EXPECT_TRUE(ValueInput::evaluate("0", evaluator(), o).reason.startsWith("Kalınlık sıfır olamaz"));
  o.allowZero = true;
  EXPECT_EQ(ValueInput::evaluate("0", evaluator(), o).status, S::Ok);
}

TEST(ValueInput, NegativeMeansReverse) {
  const auto r = len("-8");
  EXPECT_EQ(r.status, S::Ok);
  EXPECT_TRUE(r.reversed);
  EXPECT_DOUBLE_EQ(r.value, -8);
  EXPECT_EQ(r.expr, "-8");
  EXPECT_TRUE(r.reason.contains("ters"));
  const auto c = len("-1.5 cm");
  EXPECT_EQ(c.status, S::Ok);
  EXPECT_TRUE(c.reversed);
  EXPECT_DOUBLE_EQ(c.value, -15);
  EXPECT_EQ(c.expr, "-15");
  ValueInput::Options o;
  o.allowNegative = false;
  const auto n = ValueInput::evaluate("-8", evaluator(), o);
  EXPECT_EQ(n.status, S::Negative);
  EXPECT_FALSE(n.reason.isEmpty());
}

// ---------------------------------------------------------------- very large values
TEST(ValueInput, LargeLimit) {
  EXPECT_DOUBLE_EQ(ValueInput::largeLimit(0), 1e5);
  EXPECT_DOUBLE_EQ(ValueInput::largeLimit(72.1), 1e5);
  EXPECT_DOUBLE_EQ(ValueInput::largeLimit(5000), 5e5);
  EXPECT_DOUBLE_EQ(ValueInput::largeLimit(-3), 1e5);
  EXPECT_DOUBLE_EQ(ValueInput::largeLimit(std::nan("")), 1e5);
}

TEST(ValueInput, VeryLargeNeedsConfirmation) {
  auto r = len("1e7");
  EXPECT_EQ(r.status, S::NeedsConfirm);
  EXPECT_TRUE(r.acceptable());
  EXPECT_FALSE(r.ok());
  EXPECT_DOUBLE_EQ(r.value, 1e7);
  EXPECT_TRUE(r.reason.contains("Enter")) << r.reason.toStdString();
  EXPECT_TRUE(r.reason.contains("100000"));
  EXPECT_EQ(len("-1e7").status, S::NeedsConfirm);
  EXPECT_EQ(len("1e5").status, S::Ok) << "the limit itself is fine";
  EXPECT_EQ(len("100.001 m").status, S::NeedsConfirm);
  // the limit grows with the model: 100 x diagonal
  EXPECT_EQ(len("2e5", 5000).status, S::Ok);
  EXPECT_EQ(len("6e5", 5000).status, S::NeedsConfirm);
  // angles have no length limit
  EXPECT_EQ(ang("1e7").status, S::Ok);
}

TEST(LargeValueGate, SecondEnterConfirms) {
  LargeValueGate g;
  const auto big = len("1e7");
  EXPECT_FALSE(g.pass(big, "1e7"));
  EXPECT_EQ(g.pending(), "1e7");
  EXPECT_TRUE(g.pass(big, " 1e7 ")) << "same text again = confirmed";
  EXPECT_TRUE(g.pending().isEmpty());
  EXPECT_FALSE(g.pass(big, "1e7")) << "a confirmation is used once";
}

TEST(LargeValueGate, ChangedTextNeedsNewConfirmation) {
  LargeValueGate g;
  EXPECT_FALSE(g.pass(len("1e7"), "1e7"));
  EXPECT_FALSE(g.pass(len("2e7"), "2e7"));
  EXPECT_TRUE(g.pass(len("2e7"), "2e7"));
}

TEST(LargeValueGate, NormalValuesPassAndClear) {
  LargeValueGate g;
  EXPECT_TRUE(g.pass(len("10"), "10"));
  EXPECT_FALSE(g.pass(len("1e7"), "1e7"));
  EXPECT_TRUE(g.pass(len("10"), "10"));
  EXPECT_TRUE(g.pending().isEmpty());
  EXPECT_FALSE(g.pass(len("1e7"), "1e7")) << "after an ordinary value the big one needs confirming again";
  EXPECT_FALSE(g.pass(len("abc"), "abc"));
  EXPECT_FALSE(g.pass(len("0"), "0"));
  g.reset();
  EXPECT_TRUE(g.pending().isEmpty());
}

TEST(ValueInput, StatusTextsAreDistinct) {
  QStringList seen;
  for (S s : {S::Ok, S::Empty, S::BadExpression, S::UnknownUnit, S::WrongUnitKind, S::Zero, S::Negative, S::NeedsConfirm}) {
    const QString t = ValueInput::statusText(s);
    EXPECT_FALSE(t.isEmpty());
    EXPECT_FALSE(seen.contains(t));
    seen << t;
  }
}
