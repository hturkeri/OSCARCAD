// SC23 (WP-O, O-P0-3) — value input rules. See ValueInput.h.
#include "ValueInput.h"
#include "pc/Expression.h"
#include <cmath>

namespace pcd {

namespace {
QString num(double v) { return QString::fromStdString(pc::formatNumber(v)); }

QString whatOf(const ValueInput::Options& o) {
  if (!o.what.isEmpty()) return o.what;
  switch (o.quantity) {
    case ValueInput::Quantity::Length: return QStringLiteral("Mesafe");
    case ValueInput::Quantity::Angle: return QStringLiteral("Açı");
    case ValueInput::Quantity::Scalar: break;
  }
  return QStringLiteral("Değer");
}

// "(a+b)" whose first '(' closes at the very end
bool wrapped(const QString& h) {
  if (!h.startsWith('(') || !h.endsWith(')')) return false;
  int depth = 0;
  for (int i = 0; i < h.size(); ++i) {
    if (h[i] == '(') ++depth;
    else if (h[i] == ')' && --depth == 0) return i == h.size() - 1;
  }
  return false;
}

bool isUnitChar(QChar c) { return c.isLetter() || c == '_'; }

// Splits "<head> <unit>" off the end of t. false = no unit-looking suffix.
bool splitUnit(const QString& t, QString* head, QString* unit) {
  if (t.isEmpty()) return false;
  const QChar last = t.back();
  int start = t.size();
  if (last == '"' || last == QChar(0x00B0) /*°*/) start = t.size() - 1;
  else if (isUnitChar(last)) {
    while (start > 0 && isUnitChar(t[start - 1])) --start;
  } else return false;
  const QString h = t.left(start).trimmed();
  if (h.isEmpty()) return false;   // a bare identifier is a parameter name, not a unit
  const QChar e = h.back();
  if (!(e.isDigit() || e == '.' || e == ')')) return false;   // "2*L", "a+b": the letters belong to the expression
  *head = h;
  *unit = t.mid(start).trimmed();
  return true;
}
} // namespace

QString ValueInput::displayUnit(Quantity q) {
  switch (q) {
    case Quantity::Length: return QString::fromLatin1(kDocumentLengthUnit);
    case Quantity::Angle: return QStringLiteral("°");
    case Quantity::Scalar: break;
  }
  return QString();
}

QString ValueInput::unitList(Quantity q) {
  switch (q) {
    case Quantity::Length: return QStringLiteral("mm, cm, m, in, \"");
    case Quantity::Angle: return QStringLiteral("°, deg, rad");
    case Quantity::Scalar: break;
  }
  return QStringLiteral("birimsiz");
}

bool ValueInput::unitFactor(const QString& unit, Quantity q, double* factor, Quantity* kind) {
  struct U { const char* name; Quantity k; double f; };
  static const U table[] = {{"mm", Quantity::Length, 1.0},          {"cm", Quantity::Length, 10.0},
                            {"m", Quantity::Length, 1000.0},        {"in", Quantity::Length, 25.4},
                            {"\"", Quantity::Length, 25.4},         {"deg", Quantity::Angle, 1.0},
                            {"rad", Quantity::Angle, 180.0 / M_PI}};
  const QString u = unit.trimmed();
  for (const U& x : table) {
    if (u != QString::fromUtf8(x.name)) continue;
    if (kind) *kind = x.k;
    if (factor) *factor = x.f;
    return x.k == q;
  }
  if (u == QStringLiteral("°")) {
    if (kind) *kind = Quantity::Angle;
    if (factor) *factor = 1.0;
    return q == Quantity::Angle;
  }
  if (kind) *kind = Quantity::Scalar;
  return false;
}

double ValueInput::largeLimit(double modelDiagonal) {
  return std::max(kLargeFloor, 100.0 * (std::isfinite(modelDiagonal) ? std::max(0.0, modelDiagonal) : 0.0));
}

QString ValueInput::statusText(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Empty: return "empty";
    case Status::BadExpression: return "badExpression";
    case Status::UnknownUnit: return "unknownUnit";
    case Status::WrongUnitKind: return "wrongUnitKind";
    case Status::Zero: return "zero";
    case Status::Negative: return "negative";
    case Status::NeedsConfirm: return "needsConfirm";
  }
  return "?";
}

ValueInput::Result ValueInput::evaluate(const QString& text, const Evaluator& eval, const Options& opt) {
  Result r;
  r.displayUnit = displayUnit(opt.quantity);
  QString t = text.trimmed();
  if (t.isEmpty()) {
    r.status = Status::Empty;
    r.reason = QString("%1 girin (%2).").arg(whatOf(opt), unitList(opt.quantity));
    return r;
  }
  if (!t.contains('(')) t.replace(',', '.');   // decimal comma; function calls keep their argument commas
  auto run = [&](const QString& e, double* v, QString* err) { return eval && eval(e, v, err); };

  double v = 0;
  QString err;
  QString head = t, unit;
  double factor = 1.0;
  if (!run(t, &v, &err)) {
    // not a plain expression: maybe "<expression> <unit>"
    if (!splitUnit(t, &head, &unit)) {
      r.status = Status::BadExpression;
      r.reason = QString("‘%1’ bir sayı, parametre adı ya da ifade değil.").arg(t);
      return r;
    }
    Quantity kind = Quantity::Scalar;
    if (!unitFactor(unit, opt.quantity, &factor, &kind)) {
      if (kind == Quantity::Scalar) {
        r.status = Status::UnknownUnit;
        r.reason = QString("‘%1’ bilinmeyen birim; kullanılabilir: %2.").arg(unit, unitList(opt.quantity));
      } else {
        r.status = Status::WrongUnitKind;
        r.reason = QString("‘%1’ bir %2 birimi; bu alan %3 bekliyor (%4).")
                       .arg(unit, kind == Quantity::Angle ? QStringLiteral("açı") : QStringLiteral("uzunluk"),
                            opt.quantity == Quantity::Angle ? QStringLiteral("açı") : opt.quantity == Quantity::Length ? QStringLiteral("uzunluk") : QStringLiteral("birimsiz değer"),
                            unitList(opt.quantity));
      }
      return r;
    }
    err.clear();
    if (!run(head, &v, &err)) {
      r.status = Status::BadExpression;
      r.reason = QString("‘%1’ bir sayı, parametre adı ya da ifade değil.").arg(head);
      return r;
    }
    r.unitTyped = unit;
    v *= factor;
  }
  if (!std::isfinite(v)) {
    r.status = Status::BadExpression;
    r.reason = QString("‘%1’ sonlu bir değer vermiyor.").arg(t);
    return r;
  }
  r.value = v;
  if (factor == 1.0) r.expr = head;
  else {
    bool isNum = false;
    head.toDouble(&isNum);
    r.expr = isNum ? num(v) : (wrapped(head) ? head : "(" + head + ")") + "*" + num(factor);
  }
  if (std::fabs(v) < 1e-12 && !opt.allowZero) {
    r.status = Status::Zero;
    r.reason = QString("%1 sıfır olamaz: sıfırdan farklı bir değer yazın (negatif = ters yön).").arg(whatOf(opt));
    return r;
  }
  if (v < 0) {
    if (!opt.allowNegative) {
      r.status = Status::Negative;
      r.reason = QString("%1 negatif olamaz.").arg(whatOf(opt));
      return r;
    }
    r.reversed = true;
    r.reason = QStringLiteral("← ters yön");
  }
  if (opt.quantity == Quantity::Length) {
    const double lim = largeLimit(opt.modelDiagonal);
    if (std::fabs(v) > lim) {
      r.status = Status::NeedsConfirm;
      r.reason = QString("Çok büyük değer: %1 %2 (sınır %3 %2). Onaylamak için Enter'a yeniden basın, vazgeçmek için Esc.")
                     .arg(num(v), r.displayUnit, num(lim));
      return r;
    }
  }
  r.status = Status::Ok;
  return r;
}

bool LargeValueGate::pass(const ValueInput::Result& r, const QString& text) {
  if (r.status != ValueInput::Status::NeedsConfirm) {
    if (r.ok()) pending_.clear();
    return r.ok();
  }
  const QString t = text.trimmed();
  if (!pending_.isEmpty() && pending_ == t) {
    pending_.clear();
    return true;
  }
  pending_ = t;
  return false;
}

} // namespace pcd
