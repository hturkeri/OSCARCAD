#pragma once
// Simple procedurally drawn icons (no resource files).
#include <QColor>
#include <QIcon>
#include <QString>

namespace pcd {

// kind: sketch extrude revolve hole fillet chamfer shell pattern ok error warn info suppressed
//       new open save saveapp export undo redo check settings iso top front right fit
//       plane combine bodies
//       point line rect circle arc construction select
// SC-UX shell (WP-A): pull move fill home planview spin pan zoom zoomin zoomout prevview nextview views
//       mode3d modesketch (modesection = "section") trim group props options measure mass hide show showall
//       hideothers edges axes layout delete panel
//       namedsel namedsel.{folder,add,remove,replace,rebind,labels}
//       con.{coincident,horizontal,vertical,parallel,perpendicular,tangent,equal,fixed,midpoint,symmetric,concentric}
//       dim.{distance,hdistance,vdistance,length,radius,diameter,angle}
// SC23 P0 stubs (WP-S afterwards): surface curve solid trimetric document hud.error ("views" exists above)
QIcon makeIcon(const QString& kind, int size = 20);

} // namespace pcd
