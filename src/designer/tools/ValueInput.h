#pragma once
// SC23 (WP-O, O-P0-3) — numeric value input rules of the on-the-fly fields (docs/sc2023/ANALYSIS.md §4.5). Pure logic:
// no widgets, no document; the expression evaluator is passed in, so the rules are unit-testable
// (tests/test_value_input.cpp).
//
//  * text = expression [unit]. Expressions and parameter names go through the caller's evaluator unchanged
//    (pc::Expression has no unit suffixes, SC23-CR-3); a trailing unit is parsed HERE and converted to the
//    document unit: length mm / cm / m / in / " -> mm, angle ° / deg / rad -> degrees. "1,5 cm" = "1.5 cm".
//  * The document is unit-less (implicit mm, ANALYSIS §1.1); the display unit is kDocumentLengthUnit.
//  * Zero is refused with a reason (unless allowZero). Negative = reverse direction (accepted, flagged).
//  * Very large lengths, |v| > max(1e5 mm, 100 x model diagonal), are accepted only after a second Enter with the
//    same text (LargeValueGate); they never reach a modal dialog by themselves.
#include <QString>
#include <functional>

namespace pcd {

inline constexpr const char* kDocumentLengthUnit = "mm";

class ValueInput {
public:
  enum class Quantity { Length, Angle, Scalar };
  enum class Status { Ok, Empty, BadExpression, UnknownUnit, WrongUnitKind, Zero, Negative, NeedsConfirm };
  // Evaluates an expression without units; false (+ *error) on a parse error / unknown name.
  using Evaluator = std::function<bool(const QString& expr, double* value, QString* error)>;

  struct Result {
    Status status = Status::Empty;
    double value = 0;          // in the document unit (mm / degrees)
    QString expr;              // expression for the model: the text without its unit, converted ("15", "(L)*10")
    QString unitTyped;         // unit as typed ("cm", "\"", "°"); "" = none (document unit)
    QString displayUnit;       // "mm" / "°" / ""
    bool reversed = false;     // value < 0: the operation goes the other way ("← ters")
    QString reason;            // Turkish: why the value is refused / needs confirmation; hint for reversed values
    bool ok() const { return status == Status::Ok; }
    bool acceptable() const { return status == Status::Ok || status == Status::NeedsConfirm; }
  };

  struct Options {
    Quantity quantity = Quantity::Length;
    bool allowZero = false;
    bool allowNegative = true;
    double modelDiagonal = 0;  // mm; the large-value limit is max(kLargeFloor, 100 x diagonal)
    QString what;              // field name for messages ("Mesafe", "Açı", ...); "" = from the quantity
  };

  static constexpr double kLargeFloor = 1e5;   // mm

  static Result evaluate(const QString& text, const Evaluator& eval, const Options& opt);
  static double largeLimit(double modelDiagonal);
  // Unit factor to the document unit; false = not a unit of this quantity (*kind tells which quantity it is,
  // Scalar = no unit at all).
  static bool unitFactor(const QString& unit, Quantity q, double* factor, Quantity* kind = nullptr);
  static QString unitList(Quantity q);           // "mm, cm, m, in, \"" / "°, deg, rad"
  static QString displayUnit(Quantity q);        // "mm" / "°" / ""
  static QString statusText(Status s);           // short label for logs / tests
};

// "Second Enter confirms": remembers the last text that needed confirmation.
class LargeValueGate {
public:
  // true = proceed. A NeedsConfirm result passes only if the same (trimmed) text was presented before.
  bool pass(const ValueInput::Result& r, const QString& text);
  void reset() { pending_.clear(); }
  QString pending() const { return pending_; }

private:
  QString pending_;
};

} // namespace pcd
