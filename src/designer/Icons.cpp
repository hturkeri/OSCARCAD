#include "Icons.h"
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QPixmap>
#include <QPolygonF>

namespace pcd {

namespace {
// SC-UX (WP-A): SpaceClaim-style ribbon icons. 20x20 logical canvas (scaled), same palette as the classic set.
bool drawShellIcon(QPainter& p, const QString& kind) {
  const QColor ink(40, 60, 90), blue(70, 130, 200), light(170, 200, 235), red(210, 50, 50), orange(235, 150, 30),
      green(50, 150, 70), yellow(245, 196, 40), face(120, 170, 230);
  auto pen = [&](const QColor& c, double w, Qt::PenStyle st = Qt::SolidLine) {
    QPen q(c, w, st);
    q.setJoinStyle(Qt::RoundJoin);
    q.setCapStyle(Qt::RoundCap);
    p.setPen(q);
  };
  auto arrow = [&](QPointF a, QPointF b, const QColor& c, double w, double head = 3.2) {
    pen(c, w);
    p.drawLine(a, b);
    const QPointF d = b - a;
    const double L = std::max(1e-6, std::hypot(d.x(), d.y()));
    const QPointF u = d / L, n(-u.y(), u.x());
    p.setBrush(c);
    pen(c, 0.8);
    p.drawPolygon(QPolygonF({b + u * 1.2, b - u * head + n * head * 0.6, b - u * head - n * head * 0.6}));
  };
  auto slab = [&](double y, const QColor& top) {   // flat oblique plate (a selected face)
    pen(ink, 1.1);
    p.setBrush(top);
    p.drawPolygon(QPolygonF({QPointF(1.5, y + 4), QPointF(6, y), QPointF(18.5, y), QPointF(14, y + 4)}));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(1.5, y + 4), QPointF(14, y + 4), QPointF(14, y + 6.5), QPointF(1.5, y + 6.5)}));
  };
  if (kind == "pull") {
    slab(11, face);
    arrow(QPointF(10, 13), QPointF(10, 2.2), yellow.darker(115), 2.6, 4.0);
    pen(QColor(150, 105, 0), 0.6);
    return true;
  }
  if (kind == "move") {
    arrow(QPointF(9, 11), QPointF(18, 11), red, 1.8);
    arrow(QPointF(9, 11), QPointF(9, 2), blue, 1.8);
    arrow(QPointF(9, 11), QPointF(3, 17), green, 1.8);
    p.setBrush(QColor(250, 250, 250));
    pen(ink, 1.1);
    p.drawEllipse(QPointF(9, 11), 2.2, 2.2);
    return true;
  }
  if (kind == "fill") {
    slab(8, light);
    p.setBrush(QColor(40, 55, 80));
    pen(ink, 0.8);
    p.drawEllipse(QRectF(6.5, 8.6, 6, 2.6));
    arrow(QPointF(15, 1.5), QPointF(10.5, 8.2), orange, 1.8);
    return true;
  }
  if (kind == "home") {
    pen(ink, 1.3);
    p.setBrush(light);
    p.drawPolygon(QPolygonF({QPointF(4, 10), QPointF(4, 17.5), QPointF(16, 17.5), QPointF(16, 10)}));
    p.setBrush(red);
    p.drawPolygon(QPolygonF({QPointF(2, 10.5), QPointF(10, 3), QPointF(18, 10.5)}));
    p.setBrush(QColor(110, 80, 50));
    p.drawRect(QRectF(8.5, 12.5, 3, 5));
    return true;
  }
  if (kind == "planview") {
    pen(QColor(35, 85, 190), 1.3);
    p.setBrush(QColor(110, 165, 240, 150));
    p.drawRect(QRectF(3, 3, 14, 14));
    pen(QColor(35, 85, 190), 0.6, Qt::DotLine);
    for (double t : {6.5, 10.0, 13.5}) { p.drawLine(QPointF(3, t), QPointF(17, t)); p.drawLine(QPointF(t, 3), QPointF(t, 17)); }
    pen(orange, 1.6);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(10, 10), 3, 3);
    p.setBrush(orange);
    p.drawEllipse(QPointF(10, 10), 1.1, 1.1);
    return true;
  }
  if (kind == "spin") {
    p.setBrush(Qt::NoBrush);
    pen(blue, 1.9);
    p.drawArc(QRectF(2.5, 5, 15, 10), 200 * 16, 300 * 16);
    arrow(QPointF(6, 14.3), QPointF(3.2, 12.2), blue, 1.4, 3.0);
    pen(ink, 1.3);
    p.drawLine(QPointF(10, 2), QPointF(10, 18));
    return true;
  }
  if (kind == "pan") {
    for (const QPointF& d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)})
      arrow(QPointF(10, 10), QPointF(10, 10) + d * 8.2, ink, 1.6, 2.8);
    return true;
  }
  if (kind == "zoom" || kind == "zoomin" || kind == "zoomout") {
    p.setBrush(QColor(225, 238, 252));
    pen(ink, 1.6);
    p.drawEllipse(QRectF(2.5, 2.5, 11, 11));
    pen(ink, 2.6);
    p.drawLine(QPointF(12.3, 12.3), QPointF(17.5, 17.5));
    pen(blue, 1.6);
    if (kind != "zoom") p.drawLine(QPointF(5.5, 8), QPointF(10.5, 8));
    if (kind == "zoomin") p.drawLine(QPointF(8, 5.5), QPointF(8, 10.5));
    return true;
  }
  if (kind == "prevview" || kind == "nextview") {
    const bool prev = kind == "prevview";
    p.setBrush(Qt::NoBrush);
    pen(ink, 1.1);
    p.drawRect(QRectF(5, 5, 10, 10));
    arrow(QPointF(prev ? 16 : 4, 10), QPointF(prev ? 3 : 17, 10), blue, 1.8, 3.4);
    return true;
  }
  if (kind == "views") {
    pen(ink, 1.1);
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(10, 2), QPointF(17, 5.5), QPointF(10, 9), QPointF(3, 5.5)}));
    p.setBrush(light);
    p.drawPolygon(QPolygonF({QPointF(3, 5.5), QPointF(10, 9), QPointF(10, 18), QPointF(3, 14.5)}));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(17, 5.5), QPointF(10, 9), QPointF(10, 18), QPointF(17, 14.5)}));
    return true;
  }
  if (kind == "modesketch") {
    pen(QColor(70, 110, 170), 0.7);
    for (double t = 3; t <= 17.1; t += 3.5) { p.drawLine(QPointF(2, t), QPointF(18, t)); p.drawLine(QPointF(t, 2), QPointF(t, 18)); }
    pen(blue, 1.8);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(4.5, 8, 8, 6.5));
    pen(QColor(150, 90, 20), 1.0);
    p.setBrush(orange);
    p.drawPolygon(QPolygonF({QPointF(11, 9), QPointF(17.5, 2.5), QPointF(19, 4), QPointF(12.5, 10.5), QPointF(10.6, 10.9)}));
    return true;
  }
  if (kind == "mode3d") {
    pen(ink, 1.2);
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(10, 1.5), QPointF(18, 5.5), QPointF(10, 9.5), QPointF(2, 5.5)}));
    p.setBrush(face);
    p.drawPolygon(QPolygonF({QPointF(2, 5.5), QPointF(10, 9.5), QPointF(10, 18.5), QPointF(2, 14.5)}));
    p.setBrush(QColor(80, 125, 185));
    p.drawPolygon(QPolygonF({QPointF(18, 5.5), QPointF(10, 9.5), QPointF(10, 18.5), QPointF(18, 14.5)}));
    return true;
  }
  if (kind == "trim") {
    pen(ink, 1.8);
    p.drawLine(QPointF(2, 15), QPointF(7, 10));
    pen(red, 1.8, Qt::DashLine);
    p.drawLine(QPointF(7, 10), QPointF(13, 4));
    pen(ink, 1.4);
    p.drawLine(QPointF(4, 3), QPointF(16, 15));
    pen(red, 1.6);
    p.drawLine(QPointF(12, 14.5), QPointF(17, 19));
    p.drawLine(QPointF(17, 14.5), QPointF(12, 19));
    return true;
  }
  if (kind == "group") {   // driving dimension saved as a group (= parameter)
    pen(ink, 1.1);
    p.setBrush(QColor(250, 226, 150));
    p.drawPolygon(QPolygonF({QPointF(2, 6), QPointF(13, 6), QPointF(18, 10.5), QPointF(13, 15), QPointF(2, 15)}));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(13.6, 10.5), 1.3, 1.3);
    QFont f;
    f.setPixelSize(8);
    f.setBold(true);
    p.setFont(f);
    pen(QColor(110, 70, 0), 1);
    p.drawText(QRectF(2, 6, 10, 9), Qt::AlignCenter, "P");
    return true;
  }
  if (kind.startsWith("namedsel")) {   // named selection: a tag on a (selected) face
    const QString sub = kind.mid(8);
    if (sub == ".folder") {
      pen(ink, 1.0);
      p.setBrush(QColor(246, 214, 120));
      p.drawPolygon(QPolygonF({QPointF(1.5, 5), QPointF(7, 5), QPointF(8.5, 7), QPointF(18.5, 7), QPointF(18.5, 17), QPointF(1.5, 17)}));
    } else {
      slab(10, face);
    }
    pen(QColor(120, 70, 0), 1.0);
    p.setBrush(QColor(255, 196, 60));
    const double y = sub == ".folder" ? 9.5 : 3.5;
    p.drawPolygon(QPolygonF({QPointF(4, y), QPointF(12, y), QPointF(16, y + 3.5), QPointF(12, y + 7), QPointF(4, y + 7)}));
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(12.3, y + 3.5), 1.1, 1.1);
    if (sub == ".add" || sub == ".remove") {
      pen(sub == ".add" ? green : red, 2.0);
      p.drawLine(QPointF(13, 16.5), QPointF(19, 16.5));
      if (sub == ".add") p.drawLine(QPointF(16, 13.5), QPointF(16, 19.5));
    } else if (sub == ".replace") {
      arrow(QPointF(12, 18), QPointF(18.5, 18), blue, 1.4, 2.6);
      arrow(QPointF(18.5, 14.5), QPointF(12, 14.5), blue, 1.4, 2.6);
    } else if (sub == ".rebind") {
      pen(orange, 1.8);
      p.setBrush(Qt::NoBrush);
      p.drawArc(QRectF(11.5, 12, 7, 7), 30 * 16, 300 * 16);
    } else if (sub == ".labels") {
      pen(ink, 1.1);
      QFont f;
      f.setPixelSize(7);
      f.setBold(true);
      p.setFont(f);
      p.drawText(QRectF(4, 3.5, 9, 7), Qt::AlignCenter, "Ad");
    }
    return true;
  }
  if (kind == "props" || kind == "options") {
    pen(ink, 1.1);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(3, 2.5, 14, 15));
    for (int i = 0; i < 4; ++i) {
      const double y = 5.5 + i * 3.3;
      pen(blue, 1.4);
      p.drawLine(QPointF(5.5, y), QPointF(kind == "props" ? 9 : 14.5, y));
      if (kind == "props") { pen(ink, 1.0); p.drawLine(QPointF(10.5, y), QPointF(14.5, y)); }
      else { p.setBrush(orange); pen(ink, 0.7); p.drawEllipse(QPointF(7 + (i * 5 % 7), y), 1.3, 1.3); }
    }
    return true;
  }
  if (kind == "measure") {
    pen(ink, 1.1);
    p.setBrush(QColor(250, 226, 150));
    p.save();
    p.translate(10, 10);
    p.rotate(-35);
    p.drawRect(QRectF(-9, -3, 18, 6));
    for (int i = -7; i <= 7; i += 2) p.drawLine(QPointF(i, -3), QPointF(i, i % 4 == 1 ? 0.5 : -1));
    p.restore();
    return true;
  }
  if (kind == "mass") {
    pen(ink, 1.2);
    p.setBrush(QColor(150, 160, 175));
    p.drawPolygon(QPolygonF({QPointF(4, 17.5), QPointF(6, 7), QPointF(14, 7), QPointF(16, 17.5)}));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(7.5, 2, 5, 5));
    pen(Qt::white, 1.6);
    p.drawText(QRectF(4, 8, 12, 9), Qt::AlignCenter, "kg");
    return true;
  }
  if (kind == "hide" || kind == "show" || kind == "showall" || kind == "hideothers") {
    pen(ink, 1.3);
    p.setBrush(Qt::white);
    QPainterPath eye;
    eye.moveTo(2, 10);
    eye.quadTo(10, 2, 18, 10);
    eye.quadTo(10, 18, 2, 10);
    p.drawPath(eye);
    p.setBrush(blue);
    p.drawEllipse(QPointF(10, 10), 2.8, 2.8);
    if (kind == "hide" || kind == "hideothers") { pen(red, 2); p.drawLine(QPointF(3, 17), QPointF(17, 3)); }
    if (kind == "showall") { pen(green, 1.8); p.drawLine(QPointF(13, 16.5), QPointF(15, 18.5)); p.drawLine(QPointF(15, 18.5), QPointF(19, 14)); }
    return true;
  }
  if (kind == "transparent") {   // two overlapping see-through squares
    pen(ink, 1.3);
    p.setBrush(QColor(110, 160, 230, 90));
    p.drawRect(QRectF(2.5, 2.5, 10, 10));
    p.setBrush(QColor(240, 170, 100, 90));
    p.drawRect(QRectF(7.5, 7.5, 10, 10));
    return true;
  }
  if (kind == "edges") {
    pen(ink, 1.6);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF({QPointF(3, 7), QPointF(8, 3), QPointF(17, 3), QPointF(17, 13), QPointF(12, 17), QPointF(3, 17)}));
    p.drawLine(QPointF(3, 7), QPointF(12, 7)); p.drawLine(QPointF(12, 7), QPointF(17, 3)); p.drawLine(QPointF(12, 7), QPointF(12, 17));
    return true;
  }
  if (kind == "axes") {
    arrow(QPointF(6, 14), QPointF(18, 14), red, 1.6, 2.8);
    arrow(QPointF(6, 14), QPointF(6, 2), blue, 1.6, 2.8);
    arrow(QPointF(6, 14), QPointF(1.5, 18.5), green, 1.6, 2.4);
    return true;
  }
  if (kind == "layout") {
    pen(ink, 1.1);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(2, 3, 16, 14));
    p.setBrush(light);
    p.drawRect(QRectF(2, 3, 16, 3.5));
    p.drawRect(QRectF(2, 6.5, 5, 10.5));
    return true;
  }
  if (kind == "delete") {
    pen(red, 2.4);
    p.drawLine(QPointF(5, 5), QPointF(15, 15));
    p.drawLine(QPointF(15, 5), QPointF(5, 15));
    return true;
  }
  if (kind == "export") {
    p.setBrush(Qt::white);
    pen(ink, 1.2);
    p.drawPolygon(QPolygonF({QPointF(3, 2), QPointF(11, 2), QPointF(14, 5), QPointF(14, 18), QPointF(3, 18)}));
    arrow(QPointF(8, 11), QPointF(18.5, 11), green, 2.0, 3.2);
    return true;
  }
  // ---- SC23 P0 stubs (owned by WP-S afterwards): structure kinds, Trimetric view, document tab, HUD error
  if (kind == "surface") {   // thin sheet (sketch region / surface body)
    pen(QColor(35, 85, 190), 1.2);
    p.setBrush(QColor(110, 165, 240, 160));
    p.drawPolygon(QPolygonF({QPointF(2, 14), QPointF(6, 6), QPointF(18, 6), QPointF(14, 14)}));
    return true;
  }
  if (kind == "curve") {     // open curve with end points
    p.setBrush(Qt::NoBrush);
    pen(blue, 1.8);
    QPainterPath c;
    c.moveTo(3, 16);
    c.cubicTo(6, 2, 13, 18, 17, 4);
    p.drawPath(c);
    p.setBrush(orange);
    pen(QColor(150, 90, 20), 0.8);
    p.drawEllipse(QPointF(3, 16), 1.7, 1.7);
    p.drawEllipse(QPointF(17, 4), 1.7, 1.7);
    return true;
  }
  if (kind == "solid") {     // solid block
    pen(ink, 1.1);
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(3, 7), QPointF(7, 3), QPointF(17, 3), QPointF(13, 7)}));
    p.setBrush(face);
    p.drawRect(QRectF(3, 7, 10, 10));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(13, 7), QPointF(17, 3), QPointF(17, 13), QPointF(13, 17)}));
    return true;
  }
  if (kind == "trimetric") { // skewed cube (unequal foreshortening)
    pen(ink, 1.1);
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(9, 2), QPointF(18, 4.5), QPointF(12, 8), QPointF(3, 5)}));
    p.setBrush(light);
    p.drawPolygon(QPolygonF({QPointF(3, 5), QPointF(12, 8), QPointF(12, 18), QPointF(3, 14.5)}));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(18, 4.5), QPointF(12, 8), QPointF(12, 18), QPointF(18, 13.5)}));
    return true;
  }
  if (kind == "document") {  // page with folded corner
    pen(ink, 1.1);
    p.setBrush(Qt::white);
    p.drawPolygon(QPolygonF({QPointF(4, 2), QPointF(12, 2), QPointF(16, 6), QPointF(16, 18), QPointF(4, 18)}));
    p.setBrush(light);
    p.drawPolygon(QPolygonF({QPointF(12, 2), QPointF(12, 6), QPointF(16, 6)}));
    pen(blue, 1.0);
    for (double y : {9.0, 12.0, 15.0}) p.drawLine(QPointF(6.5, y), QPointF(13.5, y));
    return true;
  }
  if (kind == "hud.error") { // red disc with "!"
    pen(red.darker(120), 1.0);
    p.setBrush(red);
    p.drawEllipse(QRectF(2, 2, 16, 16));
    pen(Qt::white, 2.2);
    p.drawLine(QPointF(10, 5.5), QPointF(10, 11.5));
    p.drawPoint(QPointF(10, 14.6));
    return true;
  }
  if (kind == "panel") {
    pen(ink, 1.1);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(2, 3, 16, 14));
    p.setBrush(QColor(255, 225, 210));
    p.drawRect(QRectF(2, 12, 16, 5));
    return true;
  }
  // ---- sketch constraints ("con.*") and dimensions ("dim.*"), drawn small and crisp
  if (kind.startsWith("con.")) {
    const QString k = kind.mid(4);
    auto dot = [&](QPointF c, const QColor& col) { p.setBrush(col); pen(col, 0.8); p.drawEllipse(c, 1.8, 1.8); };
    p.setBrush(Qt::NoBrush);
    if (k == "coincident") { pen(blue, 1.6); p.drawLine(QPointF(2, 16), QPointF(10, 10)); p.drawLine(QPointF(10, 10), QPointF(18, 15)); dot(QPointF(10, 10), orange); }
    else if (k == "horizontal") { pen(blue, 2); p.drawLine(QPointF(2, 12), QPointF(18, 12)); pen(ink, 1.2); p.drawText(QRectF(4, 1, 12, 9), Qt::AlignCenter, "H"); }
    else if (k == "vertical") { pen(blue, 2); p.drawLine(QPointF(12, 2), QPointF(12, 18)); pen(ink, 1.2); p.drawText(QRectF(1, 5, 9, 10), Qt::AlignCenter, "V"); }
    else if (k == "parallel") { pen(blue, 2); p.drawLine(QPointF(3, 15), QPointF(11, 3)); p.drawLine(QPointF(9, 17), QPointF(17, 5)); }
    else if (k == "perpendicular") { pen(blue, 2); p.drawLine(QPointF(3, 17), QPointF(17, 17)); p.drawLine(QPointF(10, 17), QPointF(10, 3)); pen(ink, 1); p.drawRect(QRectF(10, 13, 4, 4)); }
    else if (k == "tangent") { pen(blue, 1.8); p.drawEllipse(QRectF(4, 6, 10, 10)); pen(ink, 1.8); p.drawLine(QPointF(1, 6), QPointF(19, 6)); dot(QPointF(9, 6), orange); }
    else if (k == "equal") { pen(blue, 2); p.drawLine(QPointF(3, 5), QPointF(17, 5)); p.drawLine(QPointF(3, 15), QPointF(17, 15)); pen(ink, 1.6); p.drawLine(QPointF(7, 8.5), QPointF(13, 8.5)); p.drawLine(QPointF(7, 11.5), QPointF(13, 11.5)); }
    else if (k == "fixed") { pen(ink, 1.4); p.setBrush(QColor(150, 160, 175)); p.drawPolygon(QPolygonF({QPointF(4, 18), QPointF(16, 18), QPointF(10, 10)})); dot(QPointF(10, 6), red); pen(ink, 1); for (double x : {5.0, 9.0, 13.0}) p.drawLine(QPointF(x, 18), QPointF(x - 2, 20)); }
    else if (k == "midpoint") { pen(blue, 2); p.drawLine(QPointF(2, 15), QPointF(18, 5)); dot(QPointF(10, 10), orange); }
    else if (k == "symmetric") { pen(ink, 1.2, Qt::DashLine); p.drawLine(QPointF(10, 1), QPointF(10, 19)); dot(QPointF(4, 10), blue); dot(QPointF(16, 10), blue); }
    else if (k == "concentric") { pen(blue, 1.6); p.drawEllipse(QPointF(10, 10), 8, 8); p.drawEllipse(QPointF(10, 10), 4, 4); dot(QPointF(10, 10), orange); }
    else return false;
    return true;
  }
  if (kind.startsWith("dim.")) {
    const QString k = kind.mid(4);
    QFont f;
    f.setPixelSize(8);
    f.setBold(true);
    p.setFont(f);
    p.setBrush(Qt::NoBrush);
    auto dimLine = [&](QPointF a, QPointF b) {
      pen(ink, 1.0);
      p.drawLine(a, b);
      const QPointF d = b - a;
      const double L = std::max(1e-6, std::hypot(d.x(), d.y()));
      const QPointF u = d / L, n(-u.y(), u.x());
      p.setBrush(ink);
      p.drawPolygon(QPolygonF({a, a + u * 3 + n * 1.4, a + u * 3 - n * 1.4}));
      p.drawPolygon(QPolygonF({b, b - u * 3 + n * 1.4, b - u * 3 - n * 1.4}));
      p.setBrush(Qt::NoBrush);
    };
    if (k == "distance") { pen(blue, 1.6); p.drawEllipse(QPointF(3, 15), 1.6, 1.6); p.drawEllipse(QPointF(17, 7), 1.6, 1.6); dimLine(QPointF(3.5, 11), QPointF(15.5, 4)); }
    else if (k == "hdistance") { pen(blue, 1.6); p.drawLine(QPointF(3, 9), QPointF(3, 19)); p.drawLine(QPointF(17, 9), QPointF(17, 19)); dimLine(QPointF(3, 6), QPointF(17, 6)); }
    else if (k == "vdistance") { pen(blue, 1.6); p.drawLine(QPointF(9, 3), QPointF(19, 3)); p.drawLine(QPointF(9, 17), QPointF(19, 17)); dimLine(QPointF(6, 3), QPointF(6, 17)); }
    else if (k == "length") { pen(blue, 2); p.drawLine(QPointF(2, 15), QPointF(18, 15)); dimLine(QPointF(2, 8), QPointF(18, 8)); pen(ink, 0.8); p.drawLine(QPointF(2, 6), QPointF(2, 15)); p.drawLine(QPointF(18, 6), QPointF(18, 15)); }
    else if (k == "radius") { pen(blue, 1.6); p.drawArc(QRectF(2, 2, 16, 16), 0, 270 * 16); dimLine(QPointF(10, 10), QPointF(15.5, 4.5)); pen(ink, 1); p.drawText(QRectF(3, 9, 8, 9), Qt::AlignCenter, "R"); }
    else if (k == "diameter") { pen(blue, 1.6); p.drawEllipse(QPointF(10, 10), 8, 8); dimLine(QPointF(4.4, 15.6), QPointF(15.6, 4.4)); }
    else if (k == "angle") { pen(blue, 1.8); p.drawLine(QPointF(2, 17), QPointF(18, 17)); p.drawLine(QPointF(2, 17), QPointF(15, 4)); pen(ink, 1.1); p.drawArc(QRectF(-7, 8, 18, 18), 0, 45 * 16); }
    else return false;
    return true;
  }
  return false;
}
} // namespace

QIcon makeIcon(const QString& kind, int S) {
  QPixmap pm(S, S);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const double s = S / 20.0;
  p.scale(s, s);
  const QColor ink(40, 60, 90), blue(70, 130, 200), light(170, 200, 235), red(210, 50, 50), orange(235, 150, 30), green(50, 150, 70);
  QPen pen(ink, 1.4);
  pen.setJoinStyle(Qt::RoundJoin);
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  p.setBrush(light);

  auto box = [&](double x, double y, double w, double h, double dx, double dy) {
    // simple oblique box: front rect + top + side
    p.setBrush(light);
    p.drawRect(QRectF(x, y + dy, w, h));
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(x, y + dy), QPointF(x + dx, y), QPointF(x + w + dx, y), QPointF(x + w, y + dy)}));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(x + w, y + dy), QPointF(x + w + dx, y), QPointF(x + w + dx, y + h), QPointF(x + w, y + h + dy)}));
  };

  if (drawShellIcon(p, kind)) {
    // drawn by the SC-UX shell set
  } else if (kind == "sketch") {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(blue, 1.6));
    p.drawRect(QRectF(3, 6, 11, 10));
    p.setPen(QPen(orange, 2));
    p.drawLine(QPointF(10, 12), QPointF(17, 3));
    p.setPen(QPen(ink, 1.2));
    p.drawLine(QPointF(17, 3), QPointF(18, 5));
  } else if (kind == "extrude") {
    box(3, 6, 9, 9, 5, -4);
    p.setPen(QPen(orange, 1.8));
    p.drawLine(QPointF(9, 6), QPointF(9, 1.5));
    p.drawLine(QPointF(9, 1.5), QPointF(7.2, 3.5));
    p.drawLine(QPointF(9, 1.5), QPointF(10.8, 3.5));
  } else if (kind == "revolve") {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(blue, 1.8));
    p.drawArc(QRectF(3, 5, 14, 11), 30 * 16, 300 * 16);
    p.setPen(QPen(orange, 1.8));
    p.drawLine(QPointF(10, 2), QPointF(10, 18));
    p.setPen(QPen(ink, 1.2));
    p.drawLine(QPointF(15.5, 6.5), QPointF(16.5, 3.5));
    p.drawLine(QPointF(15.5, 6.5), QPointF(18.5, 7));
  } else if (kind == "hole") {
    box(2, 6, 12, 8, 5, -3);
    p.setBrush(QColor(40, 55, 80));
    p.drawEllipse(QRectF(6.5, 4.5, 7, 4));
  } else if (kind == "fillet") {
    p.setBrush(light);
    QPainterPath pa;
    pa.moveTo(3, 17); pa.lineTo(3, 3); pa.lineTo(8, 3);
    pa.arcTo(QRectF(8, 3, 0.1, 0.1), 0, 0);
    pa.quadTo(9, 11, 17, 12);
    pa.lineTo(17, 17); pa.closeSubpath();
    p.drawPath(pa);
  } else if (kind == "chamfer") {
    p.setBrush(light);
    p.drawPolygon(QPolygonF({QPointF(3, 17), QPointF(3, 3), QPointF(8, 3), QPointF(17, 12), QPointF(17, 17)}));
  } else if (kind == "shell") {
    box(2, 5, 12, 10, 5, -3);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRect(QRectF(5, 9, 6, 4));
  } else if (kind == "pattern") {
    p.setBrush(light);
    for (int i = 0; i < 3; ++i) p.drawRect(QRectF(2 + i * 6, 5 + i * 3, 5, 5));
  } else if (kind == "plane") {
    p.setBrush(QColor(110, 165, 240, 150));
    p.setPen(QPen(QColor(35, 85, 190), 1.4));
    p.drawPolygon(QPolygonF({QPointF(2, 14), QPointF(6, 5), QPointF(18, 5), QPointF(14, 14)}));
    p.setPen(QPen(orange, 1.8));
    p.drawLine(QPointF(10, 10), QPointF(10, 1.5));
    p.drawLine(QPointF(10, 1.5), QPointF(8.3, 3.5));
    p.drawLine(QPointF(10, 1.5), QPointF(11.7, 3.5));
  } else if (kind == "combine") {
    p.setBrush(QColor(150, 190, 235, 190));
    p.drawRect(QRectF(2, 6, 10, 10));
    p.setBrush(QColor(240, 170, 90, 170));
    p.drawRect(QRectF(8, 3, 10, 10));
  } else if (kind == "bodies") {
    p.setBrush(QColor(150, 190, 235));
    p.drawRect(QRectF(2, 9, 9, 8));
    p.setBrush(QColor(240, 170, 90));
    p.drawRect(QRectF(9, 3, 9, 8));
  } else if (kind == "ok") {
    p.setPen(QPen(green, 2.4)); p.drawLine(QPointF(4, 10), QPointF(8, 14)); p.drawLine(QPointF(8, 14), QPointF(16, 5));
  } else if (kind == "error") {
    p.setPen(Qt::NoPen); p.setBrush(red); p.drawEllipse(QRectF(2, 2, 16, 16));
    p.setPen(QPen(Qt::white, 2.2)); p.drawLine(QPointF(7, 7), QPointF(13, 13)); p.drawLine(QPointF(13, 7), QPointF(7, 13));
  } else if (kind == "warn") {
    p.setPen(QPen(QColor(180, 110, 0), 1.2)); p.setBrush(orange);
    p.drawPolygon(QPolygonF({QPointF(10, 2), QPointF(18.5, 17.5), QPointF(1.5, 17.5)}));
    p.setPen(QPen(Qt::white, 2)); p.drawLine(QPointF(10, 7.5), QPointF(10, 12)); p.drawPoint(QPointF(10, 14.8));
  } else if (kind == "info") {
    p.setPen(Qt::NoPen); p.setBrush(blue); p.drawEllipse(QRectF(2, 2, 16, 16));
    p.setPen(QPen(Qt::white, 2.2)); p.drawLine(QPointF(10, 9), QPointF(10, 14.5)); p.drawPoint(QPointF(10, 6));
  } else if (kind == "suppressed") {
    p.setPen(QPen(QColor(140, 140, 140), 1.6)); p.setBrush(Qt::NoBrush); p.drawEllipse(QRectF(3, 3, 14, 14)); p.drawLine(QPointF(5, 15), QPointF(15, 5));
  } else if (kind == "new") {
    p.setBrush(Qt::white); p.drawPolygon(QPolygonF({QPointF(4, 2), QPointF(12, 2), QPointF(16, 6), QPointF(16, 18), QPointF(4, 18)}));
    p.drawLine(QPointF(12, 2), QPointF(12, 6)); p.drawLine(QPointF(12, 6), QPointF(16, 6));
  } else if (kind == "open") {
    p.setBrush(QColor(240, 200, 100)); p.drawPolygon(QPolygonF({QPointF(2, 16), QPointF(2, 5), QPointF(8, 5), QPointF(10, 7), QPointF(17, 7), QPointF(17, 16)}));
  } else if (kind == "save") {
    p.setBrush(blue); p.drawRect(QRectF(3, 3, 14, 14)); p.setBrush(Qt::white); p.drawRect(QRectF(6, 3, 8, 5)); p.drawRect(QRectF(6, 11, 8, 6));
  } else if (kind == "saveapp") {
    p.setBrush(green); p.drawRoundedRect(QRectF(2.5, 2.5, 15, 15), 3, 3);
    p.setPen(QPen(Qt::white, 2)); p.drawLine(QPointF(10, 5), QPointF(10, 13)); p.drawLine(QPointF(10, 13), QPointF(7, 10)); p.drawLine(QPointF(10, 13), QPointF(13, 10));
  } else if (kind == "undo" || kind == "redo") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(blue, 2.2));
    const bool u = kind == "undo";
    p.drawArc(QRectF(4, 5, 12, 10), (u ? 20 : 160) * 16, (u ? 200 : -200) * 16);
    p.setBrush(blue);
    if (u) p.drawPolygon(QPolygonF({QPointF(2, 8), QPointF(8, 7), QPointF(5, 12)}));
    else p.drawPolygon(QPolygonF({QPointF(18, 8), QPointF(12, 7), QPointF(15, 12)}));
  } else if (kind == "check") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(green, 2)); p.drawEllipse(QRectF(2.5, 2.5, 15, 15));
    p.drawLine(QPointF(6, 10), QPointF(9, 13)); p.drawLine(QPointF(9, 13), QPointF(14, 7));
  } else if (kind == "settings") {
    p.setBrush(QColor(150, 160, 175)); p.drawEllipse(QRectF(3, 3, 14, 14)); p.setBrush(Qt::white); p.drawEllipse(QRectF(7.5, 7.5, 5, 5));
  } else if (kind == "iso" || kind == "top" || kind == "front" || kind == "right") {
    if (kind == "iso") box(3, 6, 9, 9, 5, -4);
    else { p.setBrush(light); p.drawRect(QRectF(3, 3, 14, 14)); p.setPen(QPen(red, 2)); if (kind == "top") p.drawLine(QPointF(3, 3), QPointF(17, 3)); if (kind == "front") p.drawLine(QPointF(3, 17), QPointF(17, 17)); if (kind == "right") p.drawLine(QPointF(17, 3), QPointF(17, 17)); }
  } else if (kind == "section") {
    // cut block: hatched cut face on the left, remaining body on the right, cutting plane edge in orange
    p.setBrush(QColor(210, 228, 248));
    p.drawPolygon(QPolygonF({QPointF(8, 6), QPointF(12, 2.5), QPointF(18, 2.5), QPointF(14, 6)}));
    p.setBrush(QColor(130, 165, 205));
    p.drawPolygon(QPolygonF({QPointF(14, 6), QPointF(18, 2.5), QPointF(18, 13.5), QPointF(14, 17)}));
    p.setBrush(light);
    p.drawRect(QRectF(8, 6, 6, 11));
    p.setPen(QPen(ink, 0.8));
    for (double y : {9.0, 12.0, 15.0, 18.0}) p.drawLine(QPointF(8, y), QPointF(std::min(14.0, 8 + (y - 6)), std::max(6.0, y - 6)));
    p.setPen(QPen(orange, 1.8));
    p.drawLine(QPointF(5, 1.5), QPointF(5, 18.5));
    p.drawLine(QPointF(2, 18.5), QPointF(8, 18.5));
    p.drawLine(QPointF(2, 1.5), QPointF(8, 1.5));
  } else if (kind == "fit") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 1.6));
    p.drawLine(QPointF(3, 7), QPointF(3, 3)); p.drawLine(QPointF(3, 3), QPointF(7, 3));
    p.drawLine(QPointF(13, 3), QPointF(17, 3)); p.drawLine(QPointF(17, 3), QPointF(17, 7));
    p.drawLine(QPointF(3, 13), QPointF(3, 17)); p.drawLine(QPointF(3, 17), QPointF(7, 17));
    p.drawLine(QPointF(17, 13), QPointF(17, 17)); p.drawLine(QPointF(17, 17), QPointF(13, 17));
    p.setBrush(light); p.drawRect(QRectF(7, 7, 6, 6));
  } else if (kind == "point") {
    p.setPen(Qt::NoPen); p.setBrush(ink); p.drawEllipse(QRectF(7, 7, 6, 6));
  } else if (kind == "line") {
    p.setPen(QPen(ink, 2)); p.drawLine(QPointF(3, 17), QPointF(17, 3)); p.setBrush(Qt::white); p.drawEllipse(QRectF(1.5, 15.5, 3.5, 3.5)); p.drawEllipse(QRectF(15, 1.5, 3.5, 3.5));
  } else if (kind == "rect") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 2)); p.drawRect(QRectF(3, 5, 14, 10));
  } else if (kind == "circle") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 2)); p.drawEllipse(QRectF(3, 3, 14, 14));
  } else if (kind == "arc") {
    p.setBrush(Qt::NoBrush); p.setPen(QPen(ink, 2)); p.drawArc(QRectF(3, 3, 14, 14), 20 * 16, 200 * 16);
  } else if (kind == "construction") {
    QPen dp(ink, 1.6, Qt::DashLine); p.setPen(dp); p.drawLine(QPointF(3, 17), QPointF(17, 3));
  } else if (kind == "select") {
    p.setBrush(ink); p.setPen(Qt::NoPen); p.drawPolygon(QPolygonF({QPointF(5, 2), QPointF(5, 16), QPointF(8.5, 12.5), QPointF(11, 18), QPointF(13, 17), QPointF(10.5, 11.5), QPointF(15, 11)}));
  } else {
    p.drawRect(QRectF(4, 4, 12, 12));
  }
  p.end();
  return QIcon(pm);
}

} // namespace pcd
