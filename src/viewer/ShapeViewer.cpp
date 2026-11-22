// PersistCAD viewer — OpenGL 3.3 core renderer for OCCT shapes.
#include "pc/ShapeViewer.h"
#include "Tessellation.h"
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QVector3D>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>
#include <deque>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>
#include <functional>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace pc::viewer {

namespace {
const char* kMeshVS = R"(#version 330 core
layout(location=0) in vec3 pos;
layout(location=1) in vec3 nrm;
uniform mat4 mvp;
uniform mat3 nmat;
uniform vec4 clip0;
uniform vec4 clip1;
out vec3 vN;
void main(){
  vN = nmat*nrm; gl_Position = mvp*vec4(pos,1.0);
  gl_ClipDistance[0] = dot(vec4(pos,1.0), clip0);
  gl_ClipDistance[1] = dot(vec4(pos,1.0), clip1);
}
)";
const char* kMeshFS = R"(#version 330 core
in vec3 vN;
uniform vec4 color;
out vec4 frag;
void main(){
  vec3 n = normalize(vN);
  if(n.z < 0.0) n = -n;
  vec3 L1 = normalize(vec3(0.35,0.55,0.75));
  vec3 L2 = normalize(vec3(-0.6,-0.25,0.45));
  float d1 = max(dot(n,L1),0.0);
  float d2 = max(dot(n,L2),0.0);
  float hemi = 0.5+0.5*n.y;
  vec3 H = normalize(L1+vec3(0,0,1));
  float sp = pow(max(dot(n,H),0.0), 48.0);
  vec3 col = color.rgb*(0.50+0.14*hemi+0.42*d1+0.14*d2) + vec3(0.32)*sp;
  frag = vec4(min(col,vec3(1.0)), color.a);
}
)";
const char* kFlatVS = R"(#version 330 core
layout(location=0) in vec3 pos;
uniform mat4 mvp;
uniform vec2 ofs;
uniform float zbias;
uniform vec4 clip0;
uniform vec4 clip1;
void main(){
  gl_Position = mvp*vec4(pos,1.0);
  gl_Position.xy += ofs*gl_Position.w;
  gl_Position.z -= zbias*gl_Position.w;
  gl_ClipDistance[0] = dot(vec4(pos,1.0), clip0);
  gl_ClipDistance[1] = dot(vec4(pos,1.0), clip1);
}
)";
// Section cap: flat body tone with world-space hatching (anti-aliased, fades out when too dense).
const char* kCapVS = R"(#version 330 core
layout(location=0) in vec3 pos;
uniform mat4 mvp;
uniform vec4 clip0;
uniform vec4 clip1;
out vec3 wp;
void main(){
  wp = pos;
  gl_Position = mvp*vec4(pos,1.0);
  gl_ClipDistance[0] = dot(vec4(pos,1.0), clip0);
  gl_ClipDistance[1] = dot(vec4(pos,1.0), clip1);
}
)";
const char* kCapFS = R"(#version 330 core
in vec3 wp;
uniform vec4 color;
uniform vec3 hdir;
uniform float spacing;
uniform float hatch;
out vec4 frag;
void main(){
  float t = dot(wp, hdir)/spacing;
  float w = max(fwidth(t), 1e-5);
  float d = abs(fract(t) - 0.5);
  float hw = 0.55*w;                                  // ~1.1 px wide lines
  float line = 1.0 - smoothstep(hw - 0.5*w, hw + 0.5*w, d);
  line *= clamp((0.40 - w)/0.2, 0.0, 1.0);
  frag = vec4(color.rgb*(1.0 - hatch*line), 1.0);
}
)";
const char* kFlatFS = R"(#version 330 core
uniform vec4 color;
out vec4 frag;
void main(){ frag = color; }
)";
const char* kBgVS = R"(#version 330 core
uniform vec3 top;
uniform vec3 bottom;
out vec3 c;
void main(){
  vec2 p = vec2((gl_VertexID & 1)==0 ? -1.0 : 1.0, (gl_VertexID & 2)==0 ? -1.0 : 1.0);
  c = (gl_VertexID & 2)==0 ? bottom : top;
  gl_Position = vec4(p, 0.999, 1.0);
}
)";
const char* kBgFS = R"(#version 330 core
in vec3 c; out vec4 frag;
void main(){ frag = vec4(c,1.0); }
)";

const char* kLabelVS = R"(#version 330 core
uniform vec2 ctr;   // NDC of the lower-left corner
uniform vec2 sz;    // NDC size
out vec2 uv;
void main(){
  vec2 p = vec2((gl_VertexID & 1)==0 ? 0.0 : 1.0, (gl_VertexID & 2)==0 ? 0.0 : 1.0);
  uv = vec2(p.x, 1.0 - p.y);
  gl_Position = vec4(ctr + p*sz, 0.0, 1.0);
}
)";
const char* kLabelFS = R"(#version 330 core
in vec2 uv; uniform sampler2D tex; out vec4 frag;
void main(){ frag = texture(tex, uv); }
)";

constexpr double kDeg = M_PI / 180.0;
#ifndef GL_CLIP_DISTANCE0
#define GL_CLIP_DISTANCE0 0x3000
#endif
#ifndef GL_CLIP_DISTANCE1
#define GL_CLIP_DISTANCE1 0x3001
#endif
const QVector4D kNoClip(0, 0, 0, 1);

struct GpuMesh {
  GLuint vaoT = 0, vboT = 0, vaoL = 0, vboL = 0;
  int nT = 0, nL = 0;
};

// Event filter used to detect mouse leave without changing the frozen header.
class LeaveFilter : public QObject {
public:
  std::function<void()> onLeave;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::Leave && onLeave) onLeave();
    return QObject::eventFilter(o, e);
  }
};
} // namespace

struct ShapeViewer::Impl {
  ShapeViewer* q = nullptr;
  QOpenGLFunctions_3_3_Core* gl = nullptr;
  bool glOk = false;

  MeshData mesh, ghostMesh;
  bool meshDirty = false, ghostDirty = false;
  GpuMesh gm, gg;
  GLuint vaoDyn = 0, vboDyn = 0;
  GLuint vaoEmpty = 0;
  std::unique_ptr<QOpenGLShaderProgram> pMesh, pFlat, pBg, pLabel, pCap;

  // v4: section view. Kept region = union of the half-spaces dot(pl[k], (p,1)) >= 0, k < n.
  struct Section {
    bool on = false;
    int n = 0;
    QVector4D pl[2];
    ShapeViewer::SectionPreset preset = ShapeViewer::SectionPreset::None;
    double offset = 0;
    bool flip = false;
    double pos = 0;
  } sec;
  QVector4D clipCur[2] = {kNoClip, kNoClip};
  std::vector<float> secLines;       // cut contour segments (world), rebuilt lazily
  bool secLinesDirty = true;
  struct SolidRec {
    std::vector<std::pair<int, int>> runs;   // (first vertex, vertex count) in the triangle buffer
    int firstFace = -1;
    bool allHidden = true;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
  };
  std::vector<SolidRec> solids;
  QSet<QString> transFaces;
  double transAlpha = 0.28;
  std::vector<char> faceTrans;
  bool anyTrans = false;

  // v2: datum planes, per-face colours, hidden faces
  std::vector<DatumPlaneDisplay> planes;
  std::vector<std::pair<QVector3D, QString>> labels3d;   // v6: text labels at world points
  std::map<std::string, QColor> faceColors;
  QSet<QString> hiddenFaces;
  std::vector<QColor> faceCol;      // per mesh face
  std::vector<char> faceHid, edgeHid;
  // SC-2D-IMPORT: per-edge styles (edgeStyled = mesh edge index -> style index + 1), dash geometry built lazily
  std::map<std::string, EdgeStyle> edgeStyles;
  std::vector<int> edgeStyled;
  std::vector<std::pair<QColor, std::vector<float>>> styledGeom;
  bool styledDirty = true;
  double styledScale = 1;   // view half-height the dash lengths were built for (patterns ~ constant on screen)
  int hoverDatum = -1;
  struct LabelTex { GLuint tex = 0; QSize size; };
  std::map<QString, LabelTex> labelTex;

  // camera. SC-UX: the orientation is a quaternion (right = rot*X, up = rot*Y, eye = rot*Z) so that any view
  // direction (plan view of an oblique datum plane, free spin) is possible; the legacy turntable keeps az/el and
  // rebuilds the quaternion from them.
  double az = -45, el = 35.264;
  QQuaternion rot;
  QVector3D target{0, 0, 0};
  double halfH = 100;
  bool needFit = true;
  bool autoFit = true;    // view still as fitted (no zoom/pan by the user): re-fit when the widget is resized

  // display options
  bool showEdges = true, showAxes = true;
  QSet<QString> hlFaces, hlEdges;
  QColor hlFaceColor{230, 60, 40}, hlEdgeColor{255, 140, 0};
  bool planeVisible = false;
  QVector3D pOrigin, pU{1, 0, 0}, pV{0, 1, 0};

  // interaction
  QPoint lastPos, pressPos;
  bool dragging = false;
  Qt::MouseButton pressBtn = Qt::NoButton;
  Qt::KeyboardModifiers pressMods;
  int hoverId = 0;  // 1-based face index
  std::unique_ptr<QOpenGLFramebufferObject> pickFbo;
  QImage pickImage;
  bool pickValid = false;
  LeaveFilter leaveFilter;
  QMetaObject::Connection ctxConn;

  // ---------------------------------------------------------------- camera helpers
  QVector3D eyeDir() const { return rot.rotatedVector(QVector3D(0, 0, 1)).normalized(); }
  QVector3D rightV() const { return rot.rotatedVector(QVector3D(1, 0, 0)).normalized(); }
  QVector3D upV() const { return rot.rotatedVector(QVector3D(0, 1, 0)).normalized(); }
  static QQuaternion fromAzEl(double azDeg, double elDeg) {
    const double a = azDeg * kDeg, e = elDeg * kDeg;
    const QVector3D eye(float(std::cos(e) * std::cos(a)), float(std::cos(e) * std::sin(a)), float(std::sin(e)));
    const QVector3D r(float(-std::sin(a)), float(std::cos(a)), 0);
    const QVector3D u = QVector3D::crossProduct(eye, r).normalized();
    return QQuaternion::fromAxes(r, u, eye).normalized();
  }
  void setAzEl(double a, double e) { az = a; el = e; rot = fromAzEl(a, e); }
  // after a free rotation: keep az/el in step (for the legacy turntable) WITHOUT snapping the quaternion
  void syncAzElFromRotKeep() {
    const QVector3D e = eyeDir();
    el = std::asin(std::clamp(double(e.z()), -1.0, 1.0)) / kDeg;
    if (std::hypot(e.x(), e.y()) > 1e-6) az = std::atan2(e.y(), e.x()) / kDeg;
  }
  // legacy turntable after a free rotation: recover az/el from the eye direction
  void syncAzElFromRot() {
    const QVector3D e = eyeDir();
    el = std::asin(std::clamp(double(e.z()), -1.0, 1.0)) / kDeg;
    if (std::hypot(e.x(), e.y()) > 1e-6) az = std::atan2(e.y(), e.x()) / kDeg;
    rot = fromAzEl(az, el);
  }

  // ---- SC-UX state
  ShapeViewer::NavigationScheme scheme = ShapeViewer::NavigationScheme::Legacy;
  ShapeViewer::DragMode leftMode = ShapeViewer::DragMode::None;
  QString hoverEdge;                      // pre-highlighted edge (SpaceClaim scheme / setPreselection)
  bool camDirty = false;
  struct Cam { QQuaternion r; QVector3D t; double h; double az, el; };
  std::deque<Cam> back, fwd;
  Cam cam() const { return Cam{rot, target, halfH, az, el}; }
  void setCam(const Cam& c) { rot = c.r; target = c.t; halfH = c.h; az = c.az; el = c.el; needFit = false; autoFit = false; }
  void pushHistory() {
    back.push_back(cam());
    while (back.size() > 20) back.pop_front();
    fwd.clear();
  }
  int clickCount = 1;
  bool dblPending = false;
  QElapsedTimer lastDbl;
  QPoint lastDblPos;

  // Bounding box of model + ghost (a larger candidate must not be clipped by the near/far planes).
  struct Box { double bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0}; bool hasBox = false; };
  Box sceneBox() const {
    Box b;
    for (const MeshData* m : {&mesh, &ghostMesh}) {
      if (!m->hasBox) continue;
      if (!b.hasBox) { b.hasBox = true; for (int i = 0; i < 3; ++i) { b.bmin[i] = m->bmin[i]; b.bmax[i] = m->bmax[i]; } continue; }
      for (int i = 0; i < 3; ++i) { b.bmin[i] = std::min(b.bmin[i], m->bmin[i]); b.bmax[i] = std::max(b.bmax[i], m->bmax[i]); }
    }
    // datum planes (v3): included so fitAll / clipping cover them
    for (const auto& pl : planes) {
      QVector3D c[4];
      planeCorners(pl, c);
      for (const auto& q : c) {
        const double a[3] = {q.x(), q.y(), q.z()};
        if (!b.hasBox) { b.hasBox = true; for (int i = 0; i < 3; ++i) b.bmin[i] = b.bmax[i] = a[i]; continue; }
        for (int i = 0; i < 3; ++i) { b.bmin[i] = std::min(b.bmin[i], a[i]); b.bmax[i] = std::max(b.bmax[i], a[i]); }
      }
    }
    return b;
  }
  QVector3D sceneCenter() const {
    const Box m = sceneBox();
    if (!m.hasBox) return QVector3D(0, 0, 0);
    return QVector3D(float((m.bmin[0] + m.bmax[0]) / 2), float((m.bmin[1] + m.bmax[1]) / 2), float((m.bmin[2] + m.bmax[2]) / 2));
  }
  double sceneRadius() const {
    const Box m = sceneBox();
    if (!m.hasBox) return 100;
    const double dx = m.bmax[0] - m.bmin[0], dy = m.bmax[1] - m.bmin[1], dz = m.bmax[2] - m.bmin[2];
    return std::max(0.5 * std::sqrt(dx * dx + dy * dy + dz * dz), 1e-3);
  }

  // Tight fit: project the actual mesh vertices (not the 8 bounding-box corners, which over-estimate slender
  // parts seen obliquely by 30-60 %) on the view's right/up axes, centre the view on that extent.
  void doFit(double aspect) {
    const Box m = sceneBox();
    const QVector3D c = sceneCenter();
    target = c;
    if (!m.hasBox) { halfH = 100; return; }
    const QVector3D r = rightV(), u = upV();
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    auto acc = [&](const QVector3D& p) {
      const QVector3D d = p - c;
      const double a = QVector3D::dotProduct(d, r), b = QVector3D::dotProduct(d, u);
      u0 = std::min(u0, a); u1 = std::max(u1, a);
      v0 = std::min(v0, b); v1 = std::max(v1, b);
    };
    size_t nv = 0;
    // model: visible faces only (hidden bodies must not shrink what is shown)
    for (const FaceRec& f : mesh.faces) {
      if (!hiddenFaces.isEmpty() && hiddenFaces.contains(f.name)) continue;
      for (int k = f.first; k < f.first + f.count; ++k, ++nv) {
        const size_t o = size_t(k) * 6;
        if (o + 2 < mesh.tris.size()) acc(QVector3D(mesh.tris[o], mesh.tris[o + 1], mesh.tris[o + 2]));
      }
    }
    for (size_t k = 0; k + 5 < ghostMesh.tris.size(); k += 6, ++nv)
      acc(QVector3D(ghostMesh.tris[k], ghostMesh.tris[k + 1], ghostMesh.tris[k + 2]));
    for (const auto& pl : planes) {
      QVector3D q[4];
      planeCorners(pl, q);
      for (const auto& x : q) { acc(x); ++nv; }
    }
    if (nv == 0)   // no triangles (wire-only shape): fall back to the box corners
      for (int i = 0; i < 8; ++i)
        acc(QVector3D((i & 1) ? m.bmax[0] : m.bmin[0], (i & 2) ? m.bmax[1] : m.bmin[1], (i & 4) ? m.bmax[2] : m.bmin[2]));
    target = c + r * float(0.5 * (u0 + u1)) + u * float(0.5 * (v0 + v1));
    const double hu = std::max(0.5 * (u1 - u0), 1e-6), hv = std::max(0.5 * (v1 - v0), 1e-6);
    halfH = std::max(hv, hu / aspect) * 1.15;
    autoFit = true;
  }

  void camChanged() { emit q->cameraChanged(); }
  // Resolve a pending fit with the widget's aspect so that projections are valid before the first paint.
  void ensureFit() {
    if (!needFit || q->width() <= 0 || q->height() <= 0) return;
    doFit(double(q->width()) / std::max(1, q->height()));
    needFit = false;
    QMetaObject::invokeMethod(q, [this] { camChanged(); }, Qt::QueuedConnection);
  }
  QMatrix4x4 widgetVP() {
    ensureFit();
    QMatrix4x4 view, proj;
    matrices(double(q->width()) / std::max(1, q->height()), view, proj);
    return proj * view;
  }

  void matrices(double aspect, QMatrix4x4& view, QMatrix4x4& proj) const {
    const double reach = double((target - sceneCenter()).length()) + sceneRadius() * 1.2;
    const double D = reach * 3.0;
    view.setToIdentity();
    view.lookAt(target + eyeDir() * float(D), target, upV());
    proj.setToIdentity();
    proj.ortho(float(-halfH * aspect), float(halfH * aspect), float(-halfH), float(halfH), float(D - reach * 1.3), float(D + reach * 1.3));
  }

  // ---------------------------------------------------------------- GL objects
  // Releases every GL object. Must be called with the owning context current.
  void releaseGL() {
    if (!glOk || !gl) return;
    freeMesh(gm);
    freeMesh(gg);
    if (vboDyn) gl->glDeleteBuffers(1, &vboDyn);
    if (vaoDyn) gl->glDeleteVertexArrays(1, &vaoDyn);
    if (vaoEmpty) gl->glDeleteVertexArrays(1, &vaoEmpty);
    vboDyn = vaoDyn = vaoEmpty = 0;
    pickFbo.reset();
    for (auto& [k, lt] : labelTex) if (lt.tex) gl->glDeleteTextures(1, &lt.tex);
    labelTex.clear();
    pMesh.reset(); pFlat.reset(); pBg.reset(); pLabel.reset(); pCap.reset();
    glOk = false;
    meshDirty = ghostDirty = true;
  }

  void initGL() {
    // A re-created context (e.g. widget re-parented) invalidates all names of the old one:
    // forget them instead of deleting names that may now belong to new objects.
    gm = GpuMesh(); gg = GpuMesh();
    labelTex.clear();
    vboDyn = vaoDyn = vaoEmpty = 0;
    pickFbo.release();
    pickValid = false;
    glOk = false;
    gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(q->context());
    if (!gl || !gl->initializeOpenGLFunctions()) { glOk = false; return; }
    QObject::disconnect(ctxConn);
    ctxConn = QObject::connect(q->context(), &QOpenGLContext::aboutToBeDestroyed, q, [this] {
      q->makeCurrent();
      releaseGL();
      q->doneCurrent();
    });
    auto build = [&](const char* vs, const char* fs) {
      auto p = std::make_unique<QOpenGLShaderProgram>();
      p->addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
      p->addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
      if (!p->link()) qWarning("ShapeViewer shader link failed: %s", qPrintable(p->log()));
      return p;
    };
    pMesh = build(kMeshVS, kMeshFS);
    pFlat = build(kFlatVS, kFlatFS);
    pBg = build(kBgVS, kBgFS);
    pLabel = build(kLabelVS, kLabelFS);
    pCap = build(kCapVS, kCapFS);
    gl->glGenVertexArrays(1, &vaoEmpty);
    gl->glGenVertexArrays(1, &vaoDyn);
    gl->glGenBuffers(1, &vboDyn);
    gl->glBindVertexArray(vaoDyn);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vboDyn);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    gl->glBindVertexArray(0);
    glOk = true;
    meshDirty = ghostDirty = true;
  }

  void freeMesh(GpuMesh& g) {
    if (!gl) return;
    if (g.vboT) gl->glDeleteBuffers(1, &g.vboT);
    if (g.vboL) gl->glDeleteBuffers(1, &g.vboL);
    if (g.vaoT) gl->glDeleteVertexArrays(1, &g.vaoT);
    if (g.vaoL) gl->glDeleteVertexArrays(1, &g.vaoL);
    g = GpuMesh();
  }

  void upload(const MeshData& m, GpuMesh& g) {
    freeMesh(g);
    g.nT = m.vertexCount();
    g.nL = int(m.lines.size() / 3);
    gl->glGenVertexArrays(1, &g.vaoT);
    gl->glGenBuffers(1, &g.vboT);
    gl->glBindVertexArray(g.vaoT);
    gl->glBindBuffer(GL_ARRAY_BUFFER, g.vboT);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.tris.size() * sizeof(float)), m.tris.data(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(12));
    gl->glGenVertexArrays(1, &g.vaoL);
    gl->glGenBuffers(1, &g.vboL);
    gl->glBindVertexArray(g.vaoL);
    gl->glBindBuffer(GL_ARRAY_BUFFER, g.vboL);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.lines.size() * sizeof(float)), m.lines.data(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    gl->glBindVertexArray(0);
  }

  void syncGpu() {
    if (meshDirty) { upload(mesh, gm); meshDirty = false; }
    if (ghostDirty) { upload(ghostMesh, gg); ghostDirty = false; }
  }

  // ---------------------------------------------------------------- draw helpers
  void setFlat(const QMatrix4x4& mvp, const QColor& c, float alpha, float ox = 0, float oy = 0, float zbias = 0) {
    pFlat->bind();
    pFlat->setUniformValue("mvp", mvp);
    pFlat->setUniformValue("color", QVector4D(float(c.redF()), float(c.greenF()), float(c.blueF()), alpha));
    pFlat->setUniformValue("ofs", QVector2D(ox, oy));
    pFlat->setUniformValue("zbias", zbias);
    pFlat->setUniformValue("clip0", clipCur[0]);
    pFlat->setUniformValue("clip1", clipCur[1]);
  }
  void setMesh(const QMatrix4x4& mvp, const QMatrix3x3& nm, const QColor& c, float alpha) {
    pMesh->bind();
    pMesh->setUniformValue("mvp", mvp);
    pMesh->setUniformValue("nmat", nm);
    pMesh->setUniformValue("color", QVector4D(float(c.redF()), float(c.greenF()), float(c.blueF()), alpha));
    pMesh->setUniformValue("clip0", clipCur[0]);
    pMesh->setUniformValue("clip1", clipCur[1]);
  }

  // ---------------------------------------------------------------- v4 section helpers
  // The kept region is a union of half-spaces; it is drawn as disjoint convex pieces, one per pass:
  // pass 0 = {d0 >= 0}, pass 1 = {d1 >= 0 and d0 <= eps} (GL clip distances are intersected).
  int clipPasses() const { return sec.on ? std::max(1, sec.n) : 1; }
  void clipSet(const QVector4D& a, bool useB = false, const QVector4D& b = kNoClip) {
    clipCur[0] = a;
    clipCur[1] = useB ? b : kNoClip;
    gl->glEnable(GL_CLIP_DISTANCE0);
    if (useB) gl->glEnable(GL_CLIP_DISTANCE1); else gl->glDisable(GL_CLIP_DISTANCE1);
  }
  void beginClip(int pass) {
    if (!sec.on) return;
    if (pass == 0) { clipSet(sec.pl[0]); return; }
    QVector4D b = -sec.pl[0];
    b.setW(b.w() + float(2e-5 * sceneRadius()));
    clipSet(sec.pl[1], true, b);
  }
  void endClip() {
    clipCur[0] = clipCur[1] = kNoClip;
    gl->glDisable(GL_CLIP_DISTANCE0);
    gl->glDisable(GL_CLIP_DISTANCE1);
  }
  static float planeDist(const QVector4D& pl, const float* p) { return pl.x() * p[0] + pl.y() * p[1] + pl.z() * p[2] + pl.w(); }
  // true if the point lies in the removed region
  bool sectionRemoves(const float* p) const {
    if (!sec.on) return false;
    for (int k = 0; k < sec.n; ++k) if (planeDist(sec.pl[k], p) >= 0) return false;
    return true;
  }

  // Re-evaluates the preset planes against the current model bbox.
  void updateSectionPreset() {
    using P = ShapeViewer::SectionPreset;
    if (sec.preset == P::None) return;
    secLinesDirty = true;
    if (!mesh.hasBox) { sec.on = false; sec.n = 0; return; }
    double c[3], h[3];
    for (int i = 0; i < 3; ++i) { c[i] = 0.5 * (mesh.bmin[i] + mesh.bmax[i]); h[i] = 0.5 * (mesh.bmax[i] - mesh.bmin[i]); }
    const double t = std::clamp(sec.offset, -1.0, 1.0);
    const float s = sec.flip ? -1.0f : 1.0f;
    auto plane = [&](int axis, float sign, double at) {
      QVector4D p(0, 0, 0, 0);
      p[axis] = sign;
      p.setW(float(-sign * at));
      return p;
    };
    sec.on = true;
    sec.n = 1;
    switch (sec.preset) {
      case P::XZ: sec.pos = c[1] + t * h[1]; sec.pl[0] = plane(1, s, sec.pos); break;
      case P::YZ: sec.pos = c[0] + t * h[0]; sec.pl[0] = plane(0, -s, sec.pos); break;
      case P::XY: sec.pos = c[2] + t * h[2]; sec.pl[0] = plane(2, -s, sec.pos); break;
      case P::Quarter: {
        // positive offsets enlarge the removed quadrant
        const double py = c[1] + s * t * h[1], px = c[0] - s * t * h[0];
        sec.pl[0] = plane(1, s, py);
        sec.pl[1] = plane(0, -s, px);
        sec.pos = px;
        sec.n = 2;
        break;
      }
      case P::None: break;
    }
  }

  // Cut contour: every triangle of a visible face crossing a section plane contributes one segment.
  void rebuildSectionLines() {
    secLines.clear();
    secLinesDirty = false;
    if (!sec.on) return;
    const float eps = float(1e-6 * sceneRadius());
    for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
      if (fi < faceHid.size() && faceHid[fi]) continue;
      const FaceRec& f = mesh.faces[fi];
      for (int t = 0; t + 2 < f.count; t += 3) {
        const float* v[3] = {&mesh.tris[size_t(f.first + t) * 6], &mesh.tris[size_t(f.first + t + 1) * 6], &mesh.tris[size_t(f.first + t + 2) * 6]};
        for (int k = 0; k < sec.n; ++k) {
          const float d[3] = {planeDist(sec.pl[k], v[0]), planeDist(sec.pl[k], v[1]), planeDist(sec.pl[k], v[2])};
          const bool pos[3] = {d[0] >= 0, d[1] >= 0, d[2] >= 0};
          if (pos[0] == pos[1] && pos[1] == pos[2]) continue;
          QVector3D pts[2];
          int np = 0;
          for (int e = 0; e < 3 && np < 2; ++e) {
            const int a = e, b = (e + 1) % 3;
            if (pos[a] == pos[b]) continue;
            const float u = d[a] / (d[a] - d[b]);
            pts[np++] = QVector3D(v[a][0] + (v[b][0] - v[a][0]) * u, v[a][1] + (v[b][1] - v[a][1]) * u, v[a][2] + (v[b][2] - v[a][2]) * u);
          }
          if (np < 2) continue;
          if (sec.n == 2) {
            // the cut exists only where the other plane removes material too: clip the segment to d_other <= eps
            const QVector4D& o = sec.pl[1 - k];
            const float a[3] = {pts[0].x(), pts[0].y(), pts[0].z()}, b[3] = {pts[1].x(), pts[1].y(), pts[1].z()};
            const float da = planeDist(o, a) - eps, db = planeDist(o, b) - eps;
            if (da > 0 && db > 0) continue;
            if (da > 0) pts[0] = pts[0] + (pts[1] - pts[0]) * (da / (da - db));
            else if (db > 0) pts[1] = pts[1] + (pts[0] - pts[1]) * (db / (db - da));
          }
          seg(secLines, pts[0], pts[1]);
        }
      }
    }
  }

  // Capped cut faces, one body (solid) at a time: the stencil parity of the solid clipped by the plane is
  // odd exactly where the plane lies inside the solid; a quad on the plane is then drawn there
  // (depth-tested). pick=true writes id 0 so that caps occlude what is behind without being pickable.
  void drawCaps(const QMatrix4x4& vp, const QMatrix3x3& nm, const QColor& def, bool pick, int viewH = 1, float px = 0, float py = 0) {
    if (!sec.on || solids.empty() || gm.nT <= 0) return;
    const double R = sceneRadius() * 1.05;
    const QVector3D ctr = sceneCenter();
    // hatch pitch: world-anchored, but snapped to a power of two that keeps it at ~6.5..13 px on screen
    const double wpp = 2.0 * halfH / std::max(1, viewH);
    const float spacing = float(std::pow(2.0, std::round(std::log2(std::max(9.0 * wpp, 1e-9)))));
    gl->glEnable(GL_STENCIL_TEST);
    gl->glStencilMask(0xFF);
    for (int k = 0; k < sec.n; ++k) {
      const QVector4D pl = sec.pl[k];
      const QVector3D n = pl.toVector3D();
      const QVector3D o = ctr - n * (QVector3D::dotProduct(n, ctr) + pl.w());
      const QVector3D ref = std::fabs(n.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
      const QVector3D hu = QVector3D::crossProduct(ref, n).normalized(), hv = QVector3D::crossProduct(n, hu).normalized();
      const QVector3D q0 = o - hu * float(R) - hv * float(R), q1 = o + hu * float(R) - hv * float(R),
                      q2 = o + hu * float(R) + hv * float(R), q3 = o - hu * float(R) + hv * float(R);
      std::vector<float> quad;
      for (const QVector3D& qv : {q0, q1, q2, q0, q2, q3}) quad.insert(quad.end(), {qv.x(), qv.y(), qv.z()});
      // shading factor of the cut face (same light model as the mesh shader, two-sided, no specular)
      const float* m = nm.constData();   // column-major 3x3
      QVector3D vn(m[0] * n.x() + m[3] * n.y() + m[6] * n.z(), m[1] * n.x() + m[4] * n.y() + m[7] * n.z(), m[2] * n.x() + m[5] * n.y() + m[8] * n.z());
      vn.normalize();
      if (vn.z() < 0) vn = -vn;
      const float d1 = std::max(0.0f, QVector3D::dotProduct(vn, QVector3D(0.35f, 0.55f, 0.75f).normalized()));
      const float d2 = std::max(0.0f, QVector3D::dotProduct(vn, QVector3D(-0.6f, -0.25f, 0.45f).normalized()));
      const float lit = 0.50f + 0.14f * (0.5f + 0.5f * vn.y()) + 0.42f * d1 + 0.14f * d2;
      for (size_t si = 0; si < solids.size(); ++si) {
        const SolidRec& sr = solids[si];
        if (sr.allHidden || sr.runs.empty()) continue;
        // skip bodies whose bounding box does not cross the plane
        float dmin = 1e30f, dmax = -1e30f;
        for (int c = 0; c < 8; ++c) {
          const float p[3] = {(c & 1) ? sr.bmax[0] : sr.bmin[0], (c & 2) ? sr.bmax[1] : sr.bmin[1], (c & 4) ? sr.bmax[2] : sr.bmin[2]};
          const float dd = planeDist(pl, p);
          dmin = std::min(dmin, dd); dmax = std::max(dmax, dd);
        }
        if (dmin > 0 || dmax < 0) continue;
        // 1) parity of the clipped solid into the stencil buffer
        gl->glClear(GL_STENCIL_BUFFER_BIT);
        gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl->glDepthMask(GL_FALSE);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glStencilFunc(GL_ALWAYS, 0, 0xFF);
        gl->glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        clipSet(pl);
        setFlat(vp, QColor(0, 0, 0), 1.0f);
        gl->glBindVertexArray(gm.vaoT);
        for (const auto& r : sr.runs) gl->glDrawArrays(GL_TRIANGLES, r.first, r.second);
        gl->glBindVertexArray(0);
        // 2) the cap where the parity is odd
        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->glDepthMask(GL_TRUE);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        gl->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        if (sec.n == 2) clipSet(-sec.pl[1 - k]); else endClip();   // only where it borders the removed region
        if (pick) {
          setFlat(vp, QColor(0, 0, 0), 1.0f);
        } else {
          QColor base = def;
          if (sr.firstFace >= 0 && faceCol[size_t(sr.firstFace)].isValid()) base = faceCol[size_t(sr.firstFace)];
          const float f = 0.60f + 0.26f * lit;   // mostly flat: a cut face must read the same from any direction
          const float a = float((si % 2 == 0 ? 45.0 : 135.0) * kDeg);
          pCap->bind();
          pCap->setUniformValue("mvp", vp);
          pCap->setUniformValue("clip0", clipCur[0]);
          pCap->setUniformValue("clip1", clipCur[1]);
          pCap->setUniformValue("color", QVector4D(std::min(1.0f, float(base.redF()) * f), std::min(1.0f, float(base.greenF()) * f),
                                                   std::min(1.0f, float(base.blueF()) * f), 1.0f));
          pCap->setUniformValue("hdir", hu * std::cos(a) + hv * std::sin(a));
          pCap->setUniformValue("spacing", spacing);
          pCap->setUniformValue("hatch", 0.38f);
        }
        drawDyn(quad, GL_TRIANGLES);
        if (!pick && sec.n == 2 && k == 0) {
          // quarter cut: the corner line where the two cut faces meet (inside this body only)
          const QVector3D n1 = sec.pl[1].toVector3D();
          const QVector3D dir = QVector3D::crossProduct(n, n1);
          const float l2 = dir.lengthSquared();
          if (l2 > 1e-10f) {
            QVector3D p0 = (QVector3D::crossProduct(n1, dir) * (-pl.w()) + QVector3D::crossProduct(dir, n) * (-sec.pl[1].w())) / l2;
            p0 += dir * (QVector3D::dotProduct(ctr - p0, dir) / l2);
            const QVector3D dn = dir / std::sqrt(l2);
            std::vector<float> ln;
            seg(ln, p0 - dn * float(R), p0 + dn * float(R));
            endClip();
            for (int j = 0; j < 3; ++j) {
              setFlat(vp, QColor(20, 26, 38), 1.0f, (j == 1 ? px * 0.6f : 0.0f), (j == 2 ? py * 0.6f : 0.0f), 0.0006f);
              drawDyn(ln, GL_LINES);
            }
          }
        }
      }
    }
    endClip();
    gl->glStencilFunc(GL_ALWAYS, 0, 0xFF);
    gl->glDisable(GL_STENCIL_TEST);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->glDepthMask(GL_TRUE);
    gl->glEnable(GL_DEPTH_TEST);
  }
  void drawDyn(const std::vector<float>& v, GLenum mode) {
    if (v.empty()) return;
    gl->glBindVertexArray(vaoDyn);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vboDyn);
    gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    gl->glDrawArrays(mode, 0, GLsizei(v.size() / 3));
    gl->glBindVertexArray(0);
  }
  static void seg(std::vector<float>& v, const QVector3D& a, const QVector3D& b) {
    v.insert(v.end(), {a.x(), a.y(), a.z(), b.x(), b.y(), b.z()});
  }

  // SC-COLOR: `hl` unless it is too close to the face's own colour; then the most distant of a few alternatives.
  static QColor contrastHighlight(const QColor& hl, const QColor& face) {
    auto dist = [](const QColor& a, const QColor& b) {
      const int dr = a.red() - b.red(), dg = a.green() - b.green(), db = a.blue() - b.blue();
      return std::sqrt(double(dr * dr + dg * dg + db * db));
    };
    if (dist(hl, face) >= 120) return hl;
    const QColor alt[] = {QColor(255, 235, 40), QColor(40, 120, 255), QColor(255, 120, 20), QColor(30, 30, 35), QColor(245, 245, 250)};
    QColor best = hl;
    double bd = dist(hl, face);
    for (const QColor& a : alt) {
      const double d = dist(a, face);
      if (d >= 140) return a;
      if (d > bd) { bd = d; best = a; }
    }
    return best;
  }
  void drawFaceRange(const GpuMesh& g, const MeshData& m, const QSet<QString>& names, int single, const QColor& c,
                     const QMatrix4x4& mvp, const QMatrix3x3& nm) {
    setMesh(mvp, nm, c, 1.0f);
    QColor cur = c;
    gl->glBindVertexArray(g.vaoT);
    for (size_t i = 0; i < m.faces.size(); ++i) {
      if (i < faceHid.size() && faceHid[i]) continue;
      const bool hit = (single == int(i) + 1) || (!names.isEmpty() && !m.faces[i].name.isEmpty() && names.contains(m.faces[i].name));
      if (!hit) continue;
      // SC-COLOR: selection / hover colour must stay visible on user-coloured faces
      const QColor want = (i < faceCol.size() && faceCol[i].isValid()) ? contrastHighlight(c, faceCol[i]) : c;
      if (want != cur) { setMesh(mvp, nm, want, 1.0f); cur = want; }
      gl->glDrawArrays(GL_TRIANGLES, m.faces[i].first, m.faces[i].count);
    }
    gl->glBindVertexArray(0);
  }

  // ---------------------------------------------------------------- v2 state helpers
  static void parseEdgeFaces(const QString& en, QStringList& out) {
    out.clear();
    if (!en.startsWith("edge(")) return;
    const int close = en.lastIndexOf(')');
    if (close < 5) return;
    out = en.mid(5, close - 5).split('|');
  }
  void rebuildFaceState() {
    faceCol.assign(mesh.faces.size(), QColor());
    faceHid.assign(mesh.faces.size(), 0);
    faceTrans.assign(mesh.faces.size(), 0);
    anyTrans = false;
    for (size_t i = 0; i < mesh.faces.size(); ++i) {
      const QString& n = mesh.faces[i].name;
      if (n.isEmpty()) continue;
      auto it = faceColors.find(n.toStdString());
      if (it != faceColors.end()) faceCol[i] = it->second;
      if (hiddenFaces.contains(n)) faceHid[i] = 1;
      if (transFaces.contains(n)) { faceTrans[i] = 1; anyTrans = true; }
      // SC-UX: sketch regions ("sketch:<sid>:r<k>", display-only faces) are light blue and see-through by default
      if (n.startsWith("sketch:") && n.contains(":r")) {
        if (!faceCol[i].isValid()) faceCol[i] = QColor(110, 170, 245);
        faceTrans[i] = 1;
        anyTrans = true;
      }
    }
    // v4: per-solid triangle runs (section caps) + cut contour invalidation
    solids.assign(size_t(std::max(0, mesh.solidCount)), SolidRec());
    for (size_t i = 0; i < mesh.faces.size(); ++i) {
      const FaceRec& f = mesh.faces[i];
      if (f.solid < 0 || f.solid >= int(solids.size()) || f.count <= 0) continue;
      SolidRec& sr = solids[size_t(f.solid)];
      const bool first = sr.runs.empty();
      if (!first && sr.runs.back().first + sr.runs.back().second == f.first) sr.runs.back().second += f.count;
      else sr.runs.emplace_back(f.first, f.count);
      if (sr.firstFace < 0 || (!faceCol[size_t(sr.firstFace)].isValid() && faceCol[i].isValid())) sr.firstFace = int(i);
      if (!faceHid[i]) sr.allHidden = false;
      for (int v = 0; v < f.count; ++v) {
        const float* p = &mesh.tris[size_t(f.first + v) * 6];
        for (int a = 0; a < 3; ++a) {
          if (first && v == 0) { sr.bmin[a] = sr.bmax[a] = p[a]; continue; }
          sr.bmin[a] = std::min(sr.bmin[a], p[a]); sr.bmax[a] = std::max(sr.bmax[a], p[a]);
        }
      }
    }
    secLinesDirty = true;
    styledDirty = true;   // SC-2D-IMPORT
    edgeHid.assign(mesh.edges.size(), 0);
    if (!hiddenFaces.isEmpty()) {
      QStringList fs;
      for (size_t i = 0; i < mesh.edges.size(); ++i) {
        parseEdgeFaces(mesh.edges[i].name, fs);
        if (fs.isEmpty()) continue;
        bool all = true;
        for (const QString& f : fs) if (!hiddenFaces.contains(f)) { all = false; break; }
        edgeHid[i] = all ? 1 : 0;
      }
    }
  }
  bool anyHidden() const { return !hiddenFaces.isEmpty(); }
  bool anyColored() const { return !faceColors.empty(); }

  // Draw all (visible) faces, each with its own base colour.
  // trans=false: opaque faces; trans=true: the faces marked translucent (drawn with `alpha`).
  void drawBodyFaces(const QColor& def, const QMatrix4x4& vp, const QMatrix3x3& nm, bool trans = false, float alpha = 1.0f) {
    gl->glBindVertexArray(gm.vaoT);
    if (!trans && !anyHidden() && !anyColored() && !anyTrans) {
      setMesh(vp, nm, def, 1.0f);
      gl->glDrawArrays(GL_TRIANGLES, 0, gm.nT);
      gl->glBindVertexArray(0);
      return;
    }
    auto skip = [&](size_t k) { return faceHid[k] || (faceTrans[k] != 0) != trans; };
    size_t i = 0;
    while (i < mesh.faces.size()) {
      if (skip(i)) { ++i; continue; }
      const QColor c = faceCol[i].isValid() ? faceCol[i] : def;
      size_t j = i + 1;
      int cnt = mesh.faces[i].count;
      while (j < mesh.faces.size() && !skip(j) && (faceCol[j].isValid() ? faceCol[j] : def) == c &&
             mesh.faces[j].first == mesh.faces[j - 1].first + mesh.faces[j - 1].count) {
        cnt += mesh.faces[j].count; ++j;
      }
      setMesh(vp, nm, c, alpha);
      gl->glDrawArrays(GL_TRIANGLES, mesh.faces[i].first, cnt);
      i = j;
    }
    gl->glBindVertexArray(0);
  }
  // Draw all visible edge segments (vaoL must be bound).
  void drawEdgeRuns() {
    if (!anyHidden() && edgeStyles.empty()) { gl->glDrawArrays(GL_LINES, 0, gm.nL); return; }
    for (size_t i = 0; i < mesh.edges.size(); ++i)
      if (!edgeHid[i] && !(i < edgeStyled.size() && edgeStyled[i])) gl->glDrawArrays(GL_LINES, mesh.edges[i].first, mesh.edges[i].count);
  }
  // SC-2D-IMPORT: styled edges as CPU line lists (one per colour); patterns in world units scaled to the scene
  void rebuildStyled() {
    styledDirty = false;
    styledGeom.clear();
    edgeStyled.assign(mesh.edges.size(), 0);
    if (edgeStyles.empty()) return;
    std::map<QRgb, size_t> byColor;
    styledScale = halfH;
    const double R = std::max(1e-6, halfH);
    for (size_t i = 0; i < mesh.edges.size(); ++i) {
      const auto& e = mesh.edges[i];
      auto it = edgeStyles.find(e.name.toStdString());
      if (it == edgeStyles.end()) continue;
      edgeStyled[i] = 1;
      const QRgb key = it->second.color.rgb();
      auto bc = byColor.find(key);
      if (bc == byColor.end()) { byColor[key] = styledGeom.size(); styledGeom.push_back({it->second.color, {}}); bc = byColor.find(key); }
      std::vector<float>& out = styledGeom[bc->second].second;
      // pattern: alternating on/off lengths (fractions of the scene radius)
      std::vector<double> pat;
      switch (it->second.pattern) {
        case 1: pat = {0.022, 0.012}; break;
        case 2: pat = {0.050, 0.012, 0.010, 0.012}; break;
        case 3: pat = {0.004, 0.010}; break;
        default: break;
      }
      for (double& x : pat) x *= R;
      double pos = 0;   // arc length along the edge
      size_t pi = 0;
      double left = pat.empty() ? 0 : pat[0];
      for (int k = 0; k + 1 < e.count; k += 2) {
        const float* a = &mesh.lines[size_t(e.first + k) * 3];
        const float* b = &mesh.lines[size_t(e.first + k + 1) * 3];
        const QVector3D A(a[0], a[1], a[2]), B(b[0], b[1], b[2]);
        if (pat.empty()) { seg(out, A, B); continue; }
        const double L = double((B - A).length());
        double t = 0;
        while (t < L - 1e-12) {
          const double step = std::min(left, L - t);
          if (pi % 2 == 0) seg(out, A + (B - A) * float(t / L), A + (B - A) * float((t + step) / L));
          t += step;
          left -= step;
          if (left <= 1e-12) { pi = (pi + 1) % pat.size(); left = pat[pi]; }
        }
        pos += L;
      }
      (void)pos;
    }
  }

  QSize labelSize(const QImage& im) const { return im.size(); }
  const LabelTex& labelFor(const QString& text) {
    auto it = labelTex.find(text);
    if (it != labelTex.end()) return it->second;
    QFont f("Sans", 9);
    f.setBold(true);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(text) + 14, h = fm.height() + 6;
    QImage im(w, h, QImage::Format_RGBA8888);
    im.fill(Qt::transparent);
    {
      QPainter p(&im);
      p.setRenderHint(QPainter::Antialiasing);
      p.setPen(QPen(QColor(50, 100, 190), 1.2));
      p.setBrush(QColor(245, 249, 255, 225));
      p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 4, 4);
      p.setFont(f);
      p.setPen(QColor(25, 60, 140));
      p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, text);
    }
    LabelTex lt;
    lt.size = im.size();
    gl->glGenTextures(1, &lt.tex);
    gl->glBindTexture(GL_TEXTURE_2D, lt.tex);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.constBits());
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return labelTex.emplace(text, lt).first->second;
  }

  static QVector3D v3(const double* a) { return QVector3D(float(a[0]), float(a[1]), float(a[2])); }
  void planeCorners(const DatumPlaneDisplay& p, QVector3D c[4]) const {
    const QVector3D o = v3(p.origin), u = v3(p.u).normalized(), v = v3(p.v).normalized();
    const float h = float(p.halfSize);
    c[0] = o - u * h - v * h; c[1] = o + u * h - v * h; c[2] = o + u * h + v * h; c[3] = o - u * h + v * h;
  }

  void drawDatumPlanes(const QMatrix4x4& vp, int W, int H) {
    if (planes.empty()) return;
    const float px = 2.0f / float(W), py = 2.0f / float(H);
    gl->glEnable(GL_BLEND);
    gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDepthMask(GL_FALSE);
    for (size_t k = 0; k < planes.size(); ++k) {
      const auto& pl = planes[k];
      QVector3D c[4];
      planeCorners(pl, c);
      const bool hot = pl.highlighted || int(k) == hoverDatum;
      std::vector<float> fill, outline, arrow;
      for (int i : {0, 1, 2, 0, 2, 3}) fill.insert(fill.end(), {c[i].x(), c[i].y(), c[i].z()});
      for (int i = 0; i < 4; ++i) seg(outline, c[i], c[(i + 1) % 4]);
      // inner cross-hair ticks and normal arrow
      const QVector3D o = v3(pl.origin), u = v3(pl.u).normalized(), v = v3(pl.v).normalized();
      const QVector3D n = QVector3D::crossProduct(u, v).normalized();
      const float L = float(pl.halfSize) * 0.45f, hd = L * 0.2f;
      const QVector3D tip = o + n * L;
      seg(arrow, o, tip);
      seg(arrow, tip, tip - n * hd + u * (hd * 0.5f));
      seg(arrow, tip, tip - n * hd - u * (hd * 0.5f));
      seg(arrow, tip, tip - n * hd + v * (hd * 0.5f));
      seg(arrow, tip, tip - n * hd - v * (hd * 0.5f));
      const QColor fillC = hot ? QColor(255, 190, 70) : QColor(70, 140, 235);
      const QColor lineC = hot ? QColor(230, 120, 20) : QColor(35, 85, 190);
      // occluded part of the outline (behind the opaque model): faint
      gl->glDepthFunc(GL_GREATER);
      setFlat(vp, lineC, 0.18f, 0, 0, 0.0005f);
      drawDyn(outline, GL_LINES);
      gl->glDepthFunc(GL_LEQUAL);
      setFlat(vp, fillC, hot ? 0.22f : 0.12f, 0, 0, 0.0003f);
      drawDyn(fill, GL_TRIANGLES);
      // thin outline (single pass) and a lighter normal arrow
      setFlat(vp, lineC, hot ? 0.85f : 0.60f, 0, 0, 0.0008f);
      drawDyn(outline, GL_LINES);
      setFlat(vp, lineC, 0.60f, 0, 0, 0.0008f);
      drawDyn(arrow, GL_LINES);
    }
    // labels (screen space, no depth test)
    gl->glDisable(GL_DEPTH_TEST);
    for (const auto& pl : planes) {
      if (pl.name.isEmpty()) continue;
      const QVector3D o = v3(pl.origin), u = v3(pl.u).normalized(), v = v3(pl.v).normalized();
      const QVector3D a = o + u * float(pl.halfSize) + v * float(pl.halfSize);
      const QVector4D cl = vp * QVector4D(a, 1.0f);
      const LabelTex& lt = labelFor(pl.name);
      pLabel->bind();
      // anchor: label sits just outside the plane's corner (offset away from the square)
      pLabel->setUniformValue("ctr", QVector2D(cl.x() / cl.w() + 8 * px, cl.y() / cl.w() + 8 * py));
      pLabel->setUniformValue("sz", QVector2D(lt.size.width() * px, lt.size.height() * py));
      gl->glActiveTexture(GL_TEXTURE0);
      gl->glBindTexture(GL_TEXTURE_2D, lt.tex);
      pLabel->setUniformValue("tex", 0);
      gl->glBindVertexArray(vaoEmpty);
      gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      gl->glBindVertexArray(0);
    }
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
  }

  void drawLabels3d(const QMatrix4x4& vp, int W, int H) {
    if (labels3d.empty()) return;
    const float px = 2.0f / float(W), py = 2.0f / float(H);
    gl->glEnable(GL_BLEND);
    gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDisable(GL_DEPTH_TEST);
    for (const auto& [pos, text] : labels3d) {
      if (text.isEmpty()) continue;
      const QVector4D cl = vp * QVector4D(pos, 1.0f);
      if (cl.w() <= 0) continue;
      const LabelTex& lt = labelFor(text);
      pLabel->bind();
      pLabel->setUniformValue("ctr", QVector2D(cl.x() / cl.w(), cl.y() / cl.w()));
      pLabel->setUniformValue("sz", QVector2D(lt.size.width() * px, lt.size.height() * py));
      gl->glActiveTexture(GL_TEXTURE0);
      gl->glBindTexture(GL_TEXTURE_2D, lt.tex);
      pLabel->setUniformValue("tex", 0);
      gl->glBindVertexArray(vaoEmpty);
      gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      gl->glBindVertexArray(0);
    }
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDisable(GL_BLEND);
  }

  void drawGrid(const QMatrix4x4& vp, int H, double aspect) {
    if (!planeVisible) return;
    // SC-UX: the step follows the zoom (minor lines >= ~28 px apart, majors every 5) and the grid covers the view
    const double wpp = 2.0 * halfH / std::max(1, H);
    const double raw = 28.0 * wpp;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    const double step = (n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10) * mag;
    const QVector3D pu = pU.normalized(), pv = pV.normalized();
    // view centre projected on the plane, in plane coordinates, snapped to a major line
    const QVector3D rel = target - pOrigin;
    const double cu = std::round(QVector3D::dotProduct(rel, pu) / (5 * step)) * 5 * step;
    const double cv = std::round(QVector3D::dotProduct(rel, pv) / (5 * step)) * 5 * step;
    const double reach = std::max(halfH * aspect, halfH) * 2.2;   // oblique views see a larger part of the plane
    const int cnt = std::min(400, int(std::ceil(reach / step)));
    const double half = cnt * step;
    std::vector<float> fill, grid, major, axU, axV;
    auto P = [&](double u, double v) { return pOrigin + pu * float(u) + pv * float(v); };
    const QVector3D a = P(cu - half, cv - half), b = P(cu + half, cv - half), c = P(cu + half, cv + half), d = P(cu - half, cv + half);
    fill.insert(fill.end(), {a.x(), a.y(), a.z(), b.x(), b.y(), b.z(), c.x(), c.y(), c.z(), a.x(), a.y(), a.z(), c.x(), c.y(), c.z(), d.x(), d.y(), d.z()});
    for (int i = -cnt; i <= cnt; ++i) {
      const double u = cu + i * step, v = cv + i * step;
      const bool mu = std::fabs(std::remainder(u, 5 * step)) < 1e-6 * step, mv = std::fabs(std::remainder(v, 5 * step)) < 1e-6 * step;
      if (std::fabs(u) > 1e-9 * step) seg(mu ? major : grid, P(u, cv - half), P(u, cv + half));
      if (std::fabs(v) > 1e-9 * step) seg(mv ? major : grid, P(cu - half, v), P(cu + half, v));
    }
    seg(axU, P(cu - half, 0), P(cu + half, 0));
    seg(axV, P(0, cv - half), P(0, cv + half));
    gl->glEnable(GL_BLEND);
    // Separate alpha factors keep the framebuffer alpha at 1 (otherwise the widget compositor /
    // grabFramebuffer treats the pixels as translucent and produces saturated colours).
    gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDepthMask(GL_FALSE);
    setFlat(vp, QColor(90, 130, 200), 0.08f);
    drawDyn(fill, GL_TRIANGLES);
    setFlat(vp, QColor(70, 100, 160), 0.16f);
    drawDyn(grid, GL_LINES);
    setFlat(vp, QColor(70, 100, 160), 0.36f);
    drawDyn(major, GL_LINES);
    setFlat(vp, QColor(220, 60, 60), 0.85f);
    drawDyn(axU, GL_LINES);
    setFlat(vp, QColor(60, 170, 70), 0.85f);
    drawDyn(axV, GL_LINES);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
  }

  void drawTriad(const QMatrix4x4& view, int W, int H) {
    const int sz = std::max(90, std::min(W, H) / 6);
    gl->glViewport(8, 8, sz, sz);
    gl->glDisable(GL_DEPTH_TEST);
    QMatrix4x4 rot = view;
    rot.setColumn(3, QVector4D(0, 0, 0, 1));
    QMatrix4x4 proj;
    proj.ortho(-1.7f, 1.7f, -1.7f, 1.7f, -10, 10);
    const QMatrix4x4 mvp = proj * rot;
    const QVector3D axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const QColor cols[3] = {QColor(215, 55, 55), QColor(50, 160, 70), QColor(50, 90, 210)};
    const float px = 2.0f / float(sz);
    for (int i = 0; i < 3; ++i) {
      std::vector<float> v;
      seg(v, QVector3D(0, 0, 0), axes[i]);
      const QVector3D t = axes[i];
      const QVector3D o1 = QVector3D(t.y() == 1 ? 1 : 0, t.z() == 1 ? 1 : 0, t.x() == 1 ? 1 : 0);
      seg(v, t, t * 0.8f + o1 * 0.08f);
      seg(v, t, t * 0.8f - o1 * 0.08f);
      for (int k = -1; k <= 1; ++k) {
        setFlat(mvp, cols[i], 1.0f, k * px * 0.5f, 0);
        drawDyn(v, GL_LINES);
        setFlat(mvp, cols[i], 1.0f, 0, k * px * 0.5f);
        drawDyn(v, GL_LINES);
      }
      // letter in screen space
      const QVector3D c = (rot * QVector4D(axes[i] * 1.32f, 1.0f)).toVector3D();
      std::vector<float> L;
      const float s = 0.15f;
      auto S = [&](float x0, float y0, float x1, float y1) { seg(L, QVector3D(c.x() + x0 * s, c.y() + y0 * s, 0), QVector3D(c.x() + x1 * s, c.y() + y1 * s, 0)); };
      if (i == 0) { S(-1, 1, 1, -1); S(-1, -1, 1, 1); }
      if (i == 1) { S(-1, 1, 0, 0); S(1, 1, 0, 0); S(0, 0, 0, -1); }
      if (i == 2) { S(-1, 1, 1, 1); S(1, 1, -1, -1); S(-1, -1, 1, -1); }
      for (int k = 0; k < 2; ++k) {
        setFlat(proj, cols[i], 1.0f, k * px * 0.6f, 0);
        drawDyn(L, GL_LINES);
      }
    }
    gl->glEnable(GL_DEPTH_TEST);
  }

  // ---------------------------------------------------------------- main render
  enum class Mode { Normal, Pick };
  void render(int W, int H, GLuint targetFbo, Mode mode) {
    if (!glOk) return;
    syncGpu();
    const double aspect = double(W) / std::max(1, H);
    if (needFit) {
      doFit(aspect);
      needFit = false;
      QMetaObject::invokeMethod(q, [this] { camChanged(); }, Qt::QueuedConnection);
    }
    QMatrix4x4 view, proj;
    matrices(aspect, view, proj);
    const QMatrix4x4 vp = proj * view;
    const QMatrix3x3 nm = view.normalMatrix();

    gl->glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    gl->glViewport(0, 0, W, H);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);
    gl->glDepthMask(GL_TRUE);

    if (mode == Mode::Pick) {
      gl->glDisable(GL_DITHER);
      gl->glDisable(GL_BLEND);
      gl->glDisable(GL_MULTISAMPLE);
      gl->glClearColor(0, 0, 0, 1);
      gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      gl->glEnable(GL_CULL_FACE);
      gl->glDisable(GL_CULL_FACE);
      for (int cp = 0; cp < clipPasses(); ++cp) {
        beginClip(cp);
        gl->glBindVertexArray(gm.vaoT);
        // SC-VIDEO2D: sketch regions are shown 0.02 mm above their plane (SketchRegions.cpp); a region lying on a body
        // face (top view of an imported drawing under the pulled solid, sketch on a face) seen from the face's side lost
        // the pick to that face. In the pick pass regions are moved 0.1 mm toward the eye: they win against a coplanar
        // face from either side (as in SpaceClaim, where the curves imprint the face), real geometry in front still wins.
        QMatrix4x4 vpRegion = vp;
        vpRegion.translate((view.inverted() * QVector4D(0, 0, 1, 0)).toVector3D().normalized() * 0.1f);
        for (size_t i = 0; i < mesh.faces.size(); ++i) {
          if (faceHid[i]) continue;
          const int id = int(i) + 1;
          const bool region = mesh.faces[i].name.startsWith("sketch:") && mesh.faces[i].name.contains(":r");
          setFlat(region ? vpRegion : vp, QColor(id & 255, (id >> 8) & 255, (id >> 16) & 255), 1.0f);
          gl->glDrawArrays(GL_TRIANGLES, mesh.faces[i].first, mesh.faces[i].count);
        }
        gl->glBindVertexArray(0);
      }
      endClip();
      // section caps: id 0 (not pickable) but they occlude the faces behind them
      drawCaps(vp, nm, QColor(0, 0, 0), true);
      // datum planes: ids after the faces; depth-tested so a plane only wins where no face is closer
      for (size_t k = 0; k < planes.size(); ++k) {
        QVector3D c[4];
        planeCorners(planes[k], c);
        std::vector<float> fill;
        for (int i : {0, 1, 2, 0, 2, 3}) fill.insert(fill.end(), {c[i].x(), c[i].y(), c[i].z()});
        const int id = int(mesh.faces.size() + k) + 1;
        setFlat(vp, QColor(id & 255, (id >> 8) & 255, (id >> 16) & 255), 1.0f);
        drawDyn(fill, GL_TRIANGLES);
      }
      gl->glEnable(GL_MULTISAMPLE);
      return;
    }

    // background gradient
    gl->glClear(GL_DEPTH_BUFFER_BIT);
    gl->glDisable(GL_DEPTH_TEST);
    pBg->bind();
    pBg->setUniformValue("top", QVector3D(0.80f, 0.86f, 0.94f));
    pBg->setUniformValue("bottom", QVector3D(0.97f, 0.98f, 1.0f));
    gl->glBindVertexArray(vaoEmpty);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    gl->glEnable(GL_DEPTH_TEST);

    const QColor bodyColor(158, 182, 212);
    gl->glEnable(GL_POLYGON_OFFSET_FILL);
    gl->glPolygonOffset(1.0f, 1.0f);
    if (gm.nT > 0) {
      for (int cp = 0; cp < clipPasses(); ++cp) {
        beginClip(cp);
        drawBodyFaces(bodyColor, vp, nm);
        if (!hlFaces.isEmpty()) drawFaceRange(gm, mesh, hlFaces, 0, hlFaceColor, vp, nm);
        // pre-highlight (hover); a selected face keeps its selection colour under the cursor
        if (hoverId > 0 && hoverId <= int(mesh.faces.size()) && !faceHid[hoverId - 1] && !hlFaces.contains(mesh.faces[size_t(hoverId - 1)].name))
          drawFaceRange(gm, mesh, QSet<QString>(), hoverId, QColor(255, 205, 90), vp, nm);
      }
      endClip();
    }
    gl->glDisable(GL_POLYGON_OFFSET_FILL);
    drawCaps(vp, nm, bodyColor, false, H, 2.0f / float(W), 2.0f / float(H));

    // edges
    const float px = 2.0f / float(W), py = 2.0f / float(H);
    if (sec.on && gm.nT > 0) {
      // cut contour (outline of the capped faces)
      if (secLinesDirty) rebuildSectionLines();
      const QColor cc(20, 26, 38);
      for (int k = 0; k < 3; ++k) {
        setFlat(vp, cc, 1.0f, (k == 1 ? px * 0.6f : 0.0f), (k == 2 ? py * 0.6f : 0.0f), 0.0006f);
        drawDyn(secLines, GL_LINES);
      }
    }
    if (!edgeStyles.empty() && (styledDirty || edgeStyled.size() != mesh.edges.size() ||
                                std::fabs(halfH / std::max(1e-9, styledScale) - 1) > 0.25)) rebuildStyled();   // SC-2D-IMPORT
    if (showEdges && gm.nL > 0)
    for (int cp = 0; cp < clipPasses(); ++cp) {
      beginClip(cp);
      gl->glBindVertexArray(gm.vaoL);
      const QColor ec(28, 36, 50);
      for (int k = 0; k < 2; ++k) {
        setFlat(vp, ec, 1.0f, k * px * 0.5f, k * py * 0.5f, 0.0004f);
        drawEdgeRuns();
      }
      if (!edgeStyles.empty()) {   // SC-2D-IMPORT: layer colours / line styles
        for (const auto& [c, v] : styledGeom)
          for (int k = 0; k < 2; ++k) {
            setFlat(vp, c, 1.0f, k * px * 0.5f, k * py * 0.5f, 0.0012f);   // over coincident region outlines
            drawDyn(v, GL_LINES);
          }
        gl->glBindVertexArray(gm.vaoL);
      }
      if (!hlEdges.isEmpty()) {
        for (const auto& e : mesh.edges) {
          if (e.name.isEmpty() || !hlEdges.contains(e.name)) continue;
          if (anyHidden()) { QStringList fs; parseEdgeFaces(e.name, fs); bool all = !fs.isEmpty(); for (auto& f : fs) if (!hiddenFaces.contains(f)) all = false; if (all) continue; }
          for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
              if (dx && dy) continue;
              setFlat(vp, hlEdgeColor, 1.0f, dx * px * 0.8f, dy * py * 0.8f, 0.002f);
              gl->glDrawArrays(GL_LINES, e.first, e.count);
            }
        }
      }
      if (!hoverEdge.isEmpty()) {   // SC-UX pre-highlight of the edge under the cursor
        for (const auto& e : mesh.edges) {
          if (e.name != hoverEdge) continue;
          for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
              if (dx && dy) continue;
              setFlat(vp, QColor(255, 190, 60), 1.0f, dx * px * 0.9f, dy * py * 0.9f, 0.0025f);
              gl->glDrawArrays(GL_LINES, e.first, e.count);
            }
        }
      }
      gl->glBindVertexArray(0);
    }
    endClip();

    // v4: translucent faces - nearest layer only (depth pre-pass), blended over the opaque scene.
    if (anyTrans && gm.nT > 0) {
      gl->glEnable(GL_POLYGON_OFFSET_FILL);
      for (int cp = 0; cp < clipPasses(); ++cp) {
        beginClip(cp);
        gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        drawBodyFaces(bodyColor, vp, nm, true, 1.0f);
        gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl->glDepthMask(GL_FALSE);
        gl->glEnable(GL_BLEND);
        gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        drawBodyFaces(bodyColor, vp, nm, true, float(transAlpha));
        gl->glDisable(GL_BLEND);
        gl->glDepthMask(GL_TRUE);
      }
      endClip();
      gl->glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // ghost: clean translucent overlay drawn after the opaque model.
    //  1) depth-only pre-pass (back faces culled) -> only the nearest ghost layer survives, so the
    //     translucency is uniform (no double blending of overlapping layers, no ordering issues);
    //  2) colour pass with depth func LEQUAL, depth writes off, uniform warm red, alpha 0.25;
    //  3) faint visible edges. The opaque model is drawn with polygon offset, so coincident
    //     model/ghost faces resolve deterministically in favour of the ghost (no z-fighting).
    if (gg.nT > 0)
    for (int cp = 0; cp < clipPasses(); ++cp) {
      beginClip(cp);
      const QColor ghostColor(225, 70, 50);
      gl->glEnable(GL_BLEND);
      gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      gl->glDepthMask(GL_FALSE);
      if (gg.nL > 0) {
        // 0) ghost edges hidden by the opaque model (a smaller candidate would be invisible otherwise)
        gl->glDepthFunc(GL_GREATER);
        setFlat(vp, QColor(200, 50, 30), 0.30f, 0, 0, 0.0006f);  // bias toward the eye: edges on visible surfaces are not "hidden"
        gl->glBindVertexArray(gg.vaoL);
        gl->glDrawArrays(GL_LINES, 0, gg.nL);
        gl->glBindVertexArray(0);
        gl->glDepthFunc(GL_LEQUAL);
      }
      gl->glDisable(GL_BLEND);
      gl->glEnable(GL_CULL_FACE);
      gl->glCullFace(GL_BACK);
      gl->glFrontFace(GL_CCW);
      gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      gl->glDepthMask(GL_TRUE);
      setFlat(vp, ghostColor, 1.0f);
      gl->glBindVertexArray(gg.vaoT);
      gl->glDrawArrays(GL_TRIANGLES, 0, gg.nT);
      gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      gl->glDepthMask(GL_FALSE);
      gl->glEnable(GL_BLEND);
      gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      setMesh(vp, nm, ghostColor, 0.25f);
      gl->glDrawArrays(GL_TRIANGLES, 0, gg.nT);
      gl->glBindVertexArray(0);
      gl->glDisable(GL_CULL_FACE);
      if (gg.nL > 0) {
        setFlat(vp, QColor(180, 40, 25), 0.45f, 0, 0, 0.0006f);
        gl->glBindVertexArray(gg.vaoL);
        gl->glDrawArrays(GL_LINES, 0, gg.nL);
        gl->glBindVertexArray(0);
      }
      gl->glDepthMask(GL_TRUE);
      gl->glDisable(GL_BLEND);
    }
    endClip();

    drawDatumPlanes(vp, W, H);
    drawLabels3d(vp, W, H);
    drawGrid(vp, H, aspect);
    if (showAxes) drawTriad(view, W, H);
    gl->glViewport(0, 0, W, H);
  }

  // ---------------------------------------------------------------- picking
  QSize pixelSize() const {
    const qreal r = q->devicePixelRatioF();
    return QSize(std::max(1, int(q->width() * r)), std::max(1, int(q->height() * r)));
  }
  int pickAt(const QPoint& p) {
    if (!glOk || (mesh.faces.empty() && planes.empty())) return 0;
    const QSize sz = pixelSize();
    if (!pickValid || pickImage.size() != sz) {
      q->makeCurrent();
      if (!pickFbo || pickFbo->size() != sz) {
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        pickFbo = std::make_unique<QOpenGLFramebufferObject>(sz, f);
      }
      render(sz.width(), sz.height(), pickFbo->handle(), Mode::Pick);
      pickImage = pickFbo->toImage();
      gl->glBindFramebuffer(GL_FRAMEBUFFER, q->defaultFramebufferObject());
      q->doneCurrent();
      pickValid = true;
    }
    const qreal r = q->devicePixelRatioF();
    const int x = int(p.x() * r), y = int(p.y() * r);
    if (x < 0 || y < 0 || x >= pickImage.width() || y >= pickImage.height()) return 0;
    const QRgb c = pickImage.pixel(x, y);
    const int id = qRed(c) | (qGreen(c) << 8) | (qBlue(c) << 16);
    return id <= int(mesh.faces.size() + planes.size()) ? id : 0;
  }
  int datumIndex(int id) const { return id > int(mesh.faces.size()) ? id - int(mesh.faces.size()) - 1 : -1; }
  QString faceName(int id) const { return (id > 0 && id <= int(mesh.faces.size())) ? mesh.faces[id - 1].name : QString(); }

  QString pickEdge(const QPoint& p) { return pickEdge(p, 9.0, false); }
  // SC-UX: threshold in px; occlusion=true rejects edges hidden behind other faces (the face under the closest
  // point of the edge must be one of the edge's own faces, or nothing).
  QString pickEdge(const QPoint& p, double threshold, bool occlusion) {
    if (mesh.edges.empty()) return QString();
    if (occlusion) {
      std::vector<std::pair<double, QString>> cands;
      collectEdges(p, threshold, cands);
      std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      const QString under = faceName(pickAt(p));
      for (const auto& [dist, name] : cands) {
        QStringList fs;
        parseEdgeFaces(name, fs);
        if (fs.isEmpty() || under.isEmpty() || fs.contains(under)) return name;
        // the edge's own faces may be named with split suffixes; accept a prefix match
        bool ok = false;
        for (const QString& f : fs) if (under.startsWith(f + ":") || f.startsWith(under + ":")) ok = true;
        if (ok) return name;
      }
      return QString();
    }
    std::vector<std::pair<double, QString>> cands;
    collectEdges(p, threshold, cands);
    double best = 1e300;
    QString bestName;
    for (const auto& [dd, n] : cands) if (dd < best) { best = dd; bestName = n; }
    return bestName;
  }
  // closest distance per edge (only edges closer than `threshold` px)
  void collectEdges(const QPoint& p, double threshold, std::vector<std::pair<double, QString>>& out) {
    out.clear();
    if (mesh.edges.empty()) return;
    const QSize sz = pixelSize();
    const double aspect = double(sz.width()) / sz.height();
    QMatrix4x4 view, proj;
    matrices(aspect, view, proj);
    const QMatrix4x4 vp = proj * view;
    const double W = q->width(), H = q->height();
    auto toScreen = [&](float x, float y, float z) {
      const QVector4D c = vp * QVector4D(x, y, z, 1.0f);
      return QPointF((c.x() / c.w() * 0.5 + 0.5) * W, (1.0 - (c.y() / c.w() * 0.5 + 0.5)) * H);
    };
    for (size_t ei = 0; ei < mesh.edges.size(); ++ei) {
      const auto& e = mesh.edges[ei];
      if (e.name.isEmpty()) continue;
      double best = threshold;
      bool hit = false;
      if (ei < edgeHid.size() && edgeHid[ei]) continue;   // edges of hidden bodies are not pickable
      for (int i = 0; i + 1 < e.count; i += 2) {
        const float* a = &mesh.lines[size_t(e.first + i) * 3];
        const float* b = &mesh.lines[size_t(e.first + i + 1) * 3];
        if (sec.on) {   // v4: segments in the removed region of the section are not pickable
          const float mid[3] = {0.5f * (a[0] + b[0]), 0.5f * (a[1] + b[1]), 0.5f * (a[2] + b[2])};
          if (sectionRemoves(mid)) continue;
        }
        const QPointF A = toScreen(a[0], a[1], a[2]), B = toScreen(b[0], b[1], b[2]);
        const QPointF ab = B - A, ap = QPointF(p) - A;
        const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
        double t = l2 > 1e-12 ? (ap.x() * ab.x() + ap.y() * ab.y()) / l2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        const QPointF c = A + ab * t;
        const double d = std::hypot(c.x() - p.x(), c.y() - p.y());
        if (d < best) { best = d; hit = true; }
      }
      if (hit) out.emplace_back(best, e.name);
    }
  }
  // SC-UX combined pick: edge within 6 px first (if requested), then face / datum.
  QString pickCombined(const QPoint& p, int kinds, int* kindOut) {
    if (kindOut) *kindOut = 0;
    if (kinds & ShapeViewer::PickEdge) {
      const QString e = pickEdge(p, 6.0, true);
      if (!e.isEmpty()) { if (kindOut) *kindOut = ShapeViewer::PickEdge; return e; }
    }
    const int raw = pickAt(p);
    const int dk = datumIndex(raw);
    if (dk >= 0 && dk < int(planes.size())) {
      if (!(kinds & ShapeViewer::PickDatum)) return QString();
      if (kindOut) *kindOut = ShapeViewer::PickDatum;
      return planes[size_t(dk)].name;
    }
    if (!(kinds & ShapeViewer::PickFace)) return QString();
    const QString f = faceName(raw);
    if (!f.isEmpty() && kindOut) *kindOut = ShapeViewer::PickFace;
    return f;
  }
  int faceIndex(const QString& name) const {
    for (size_t i = 0; i < mesh.faces.size(); ++i) if (mesh.faces[i].name == name) return int(i) + 1;
    return 0;
  }

  void invalidate() { pickValid = false; q->update(); }
};

// ======================================================================= public API
ShapeViewer::ShapeViewer(QWidget* parent) : QOpenGLWidget(parent), d(std::make_unique<Impl>()) {
  d->q = this;
  d->rot = Impl::fromAzEl(d->az, d->el);
  QSurfaceFormat f;
  f.setVersion(3, 3);
  f.setProfile(QSurfaceFormat::CoreProfile);
  f.setDepthBufferSize(24);
  f.setStencilBufferSize(8);
  f.setSamples(4);
  setFormat(f);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(200, 150);
  d->leaveFilter.onLeave = [this] {
    if (d->hoverId != 0 || d->hoverDatum >= 0) { d->hoverId = 0; d->hoverDatum = -1; emit faceHovered(QString()); update(); }
  };
  installEventFilter(&d->leaveFilter);
}

ShapeViewer::~ShapeViewer() {
  QObject::disconnect(d->ctxConn);  // Impl dies before ~QOpenGLWidget destroys the context
  if (d->glOk && context()) {
    makeCurrent();
    d->releaseGL();
    doneCurrent();
  }
}

void ShapeViewer::setModel(const TopoDS_Shape& shape, const std::map<std::string, TopoDS_Face>& faceNames,
                           const std::map<std::string, TopoDS_Edge>& edgeNames, bool keepCamera) {
  const bool hadModel = d->mesh.hasBox;
  d->mesh = tessellate(shape, faceNames, edgeNames);
  d->rebuildFaceState();
  d->meshDirty = true;
  d->hoverId = 0;
  if (!keepCamera || !hadModel) d->needFit = true;
  const bool preset = d->sec.preset != SectionPreset::None;
  if (preset) d->updateSectionPreset();
  d->invalidate();
  if (preset) emit sectionChanged();
}

void ShapeViewer::clearModel() {
  d->mesh = MeshData();
  d->rebuildFaceState();
  d->meshDirty = true;
  d->hoverId = 0;
  const bool preset = d->sec.preset != SectionPreset::None;
  if (preset) d->updateSectionPreset();
  d->invalidate();
  if (preset) emit sectionChanged();
}

void ShapeViewer::setGhost(const TopoDS_Shape& shape) {
  d->ghostMesh = tessellate(shape, {}, {});
  d->ghostDirty = true;
  d->invalidate();
}
void ShapeViewer::clearGhost() {
  d->ghostMesh = MeshData();
  d->ghostDirty = true;
  d->invalidate();
}

void ShapeViewer::setHighlightedFaces(const QStringList& names, const QColor& color) {
  d->hlFaces = QSet<QString>(names.begin(), names.end());
  d->hlFaceColor = color;
  update();
}
void ShapeViewer::setHighlightedEdges(const QStringList& names, const QColor& color) {
  d->hlEdges = QSet<QString>(names.begin(), names.end());
  d->hlEdgeColor = color;
  update();
}
void ShapeViewer::clearHighlights() {
  d->hlFaces.clear();
  d->hlEdges.clear();
  update();
}

void ShapeViewer::setSketchPlane(bool visible, const double* o, const double* u, const double* v) {
  d->planeVisible = visible;
  if (visible && o && u && v) {
    d->pOrigin = QVector3D(float(o[0]), float(o[1]), float(o[2]));
    d->pU = QVector3D(float(u[0]), float(u[1]), float(u[2]));
    d->pV = QVector3D(float(v[0]), float(v[1]), float(v[2]));
  }
  update();
}

void ShapeViewer::setDatumPlanes(const std::vector<DatumPlaneDisplay>& planes) {
  d->planes = planes;
  d->hoverDatum = -1;
  d->invalidate();
}
void ShapeViewer::setFaceColors(const std::map<std::string, QColor>& faceColors) {
  d->faceColors = faceColors;
  d->rebuildFaceState();
  d->invalidate();
}
// SC-2D-IMPORT (additive)
void ShapeViewer::setEdgeStyles(const std::map<std::string, EdgeStyle>& styles) {
  if (styles.size() == d->edgeStyles.size()) {
    bool same = true;
    for (const auto& [k, v] : styles) {
      auto it = d->edgeStyles.find(k);
      if (it == d->edgeStyles.end() || it->second.color != v.color || it->second.pattern != v.pattern) { same = false; break; }
    }
    if (same) return;
  }
  d->edgeStyles = styles;
  d->styledDirty = true;
  d->invalidate();
}
int ShapeViewer::styledEdgeCount() const {
  int n = 0;
  for (const auto& e : d->mesh.edges) if (d->edgeStyles.count(e.name.toStdString())) ++n;
  return n;
}
void ShapeViewer::setHiddenFaces(const QStringList& names) {
  d->hiddenFaces = QSet<QString>(names.begin(), names.end());
  d->hoverId = 0;
  d->rebuildFaceState();
  d->invalidate();
}

void ShapeViewer::setView(View v) {
  d->pushHistory();
  switch (v) {
    case View::Iso: d->setAzEl(-45, 35.264); break;
    case View::Top: d->setAzEl(-90, 90); break;
    case View::Bottom: d->setAzEl(-90, -90); break;
    case View::Front: d->setAzEl(-90, 0); break;
    case View::Back: d->setAzEl(90, 0); break;
    case View::Left: d->setAzEl(180, 0); break;
    case View::Right: d->setAzEl(0, 0); break;
    case View::Trimetric: d->setAzEl(-62, 22); break;   // SC23 (additive): az ≠ ±45°, el ≠ 35.26° -> 3 different scales
  }
  d->needFit = true;
  d->invalidate();
  d->camChanged();
}
void ShapeViewer::fitAll() { d->needFit = true; d->invalidate(); }
void ShapeViewer::setShowEdges(bool on) { d->showEdges = on; update(); }
void ShapeViewer::setShowAxes(bool on) { d->showAxes = on; update(); }

// ----------------------------------------------------------------------- v4: section view
void ShapeViewer::setSectionPlane(bool enabled, const double* origin, const double* normal) {
  d->sec.preset = SectionPreset::None;
  d->sec.offset = 0;
  d->sec.flip = false;
  d->sec.on = false;
  d->sec.n = 0;
  if (enabled && normal) {
    QVector3D n = QVector3D(float(normal[0]), float(normal[1]), float(normal[2]));
    if (n.length() > 1e-9f) {
      n.normalize();
      const QVector3D o = origin ? QVector3D(float(origin[0]), float(origin[1]), float(origin[2])) : QVector3D(0, 0, 0);
      d->sec.pl[0] = QVector4D(n, -QVector3D::dotProduct(n, o));
      d->sec.n = 1;
      d->sec.on = true;
    }
  }
  d->secLinesDirty = true;
  d->hoverId = 0;
  d->invalidate();
  emit sectionChanged();
}
void ShapeViewer::setSectionPreset(SectionPreset preset, double offset, bool flip) {
  d->sec.preset = preset;
  d->sec.offset = std::clamp(offset, -1.0, 1.0);
  d->sec.flip = flip;
  d->sec.on = false;
  d->sec.n = 0;
  d->sec.pos = 0;
  d->secLinesDirty = true;
  d->updateSectionPreset();
  d->hoverId = 0;
  d->invalidate();
  emit sectionChanged();
}
ShapeViewer::SectionPreset ShapeViewer::sectionPreset() const { return d->sec.preset; }
bool ShapeViewer::sectionEnabled() const { return d->sec.on; }
double ShapeViewer::sectionPosition() const { return d->sec.pos; }
ShapeViewer::SectionPreset ShapeViewer::sectionPresetFromString(const QString& text, bool* ok) {
  const QString t = text.trimmed().toLower();
  if (ok) *ok = true;
  if (t == "xz") return SectionPreset::XZ;
  if (t == "yz") return SectionPreset::YZ;
  if (t == "xy") return SectionPreset::XY;
  if (t == "quarter" || t == "ceyrek" || t == QString::fromUtf8("çeyrek")) return SectionPreset::Quarter;
  if (t == "none" || t == "off") return SectionPreset::None;
  if (ok) *ok = false;
  return SectionPreset::None;
}
void ShapeViewer::setTransparentFaces(const QStringList& names, double opacity) {
  d->transFaces = QSet<QString>(names.begin(), names.end());
  d->transAlpha = std::clamp(opacity, 0.05, 1.0);
  d->rebuildFaceState();
  update();
}

QString ShapeViewer::edgeAt(const QPoint& p) { return d->pickEdge(p); }

void ShapeViewer::setLabels(const std::vector<Label3D>& labels) {
  d->labels3d.clear();
  for (const auto& l : labels) d->labels3d.push_back({QVector3D(float(l.pos[0]), float(l.pos[1]), float(l.pos[2])), l.text});
  update();
}
QString ShapeViewer::datumAt(const QPoint& p) {
  const int dk = d->datumIndex(d->pickAt(p));
  return (dk >= 0 && dk < int(d->planes.size())) ? d->planes[size_t(dk)].name : QString();
}

QImage ShapeViewer::renderToImage(int w, int h) {
  if (!isValid()) {
    // Force context creation (works under Xvfb): show the widget once.
    if (width() < 10) resize(std::max(w, 400), std::max(h, 300));
    show();
    QApplication::processEvents();
  }
  makeCurrent();
  if (!d->glOk) { doneCurrent(); QImage i(w, h, QImage::Format_RGB32); i.fill(Qt::lightGray); return i; }
  QOpenGLFramebufferObjectFormat f;
  f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
  f.setSamples(4);
  auto fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, f);
  if (!fbo->isValid()) {
    QOpenGLFramebufferObjectFormat f2;
    f2.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, f2);
  }
  d->render(w, h, fbo->handle(), Impl::Mode::Normal);
  QImage img = fbo->toImage().convertToFormat(QImage::Format_RGB32);
  d->gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
  fbo.reset();
  doneCurrent();
  d->pickValid = false;
  return img;
}

// ======================================================================= GL callbacks
void ShapeViewer::initializeGL() { d->initGL(); }
void ShapeViewer::resizeGL(int, int) {
  d->pickValid = false;
  if (d->autoFit) d->needFit = true;   // e.g. first paint happens before the docks reach their final size
  QMetaObject::invokeMethod(this, [this] { d->camChanged(); }, Qt::QueuedConnection);   // SC-UX: projection changed
}
void ShapeViewer::paintGL() {
  if (!d->glOk) return;
  const QSize sz = d->pixelSize();
  d->render(sz.width(), sz.height(), defaultFramebufferObject(), Impl::Mode::Normal);
}

// ======================================================================= SC-UX additive API
void ShapeViewer::setNavigationScheme(NavigationScheme sc) {
  if (sc == NavigationScheme::Legacy && d->scheme != sc) d->syncAzElFromRot();
  d->scheme = sc;
  d->hoverEdge.clear();
  update();
}
ShapeViewer::NavigationScheme ShapeViewer::navigationScheme() const { return d->scheme; }
void ShapeViewer::setLeftDragMode(DragMode m) { d->leftMode = m; }
ShapeViewer::DragMode ShapeViewer::leftDragMode() const { return d->leftMode; }

QPointF ShapeViewer::worldToScreen(const double p[3]) const {
  const QMatrix4x4 vp = d->widgetVP();
  const QVector4D c = vp * QVector4D(float(p[0]), float(p[1]), float(p[2]), 1.0f);
  const double w = std::fabs(c.w()) > 1e-12 ? c.w() : 1.0;
  return QPointF((c.x() / w * 0.5 + 0.5) * width(), (1.0 - (c.y() / w * 0.5 + 0.5)) * height());
}

bool ShapeViewer::screenRay(const QPointF& px, double origin[3], double dir[3]) const {
  bool inv = false;
  const QMatrix4x4 ivp = d->widgetVP().inverted(&inv);
  if (!inv || width() <= 0 || height() <= 0) return false;
  const float nx = float(px.x() / width() * 2.0 - 1.0), ny = float(1.0 - px.y() / height() * 2.0);
  const QVector4D a = ivp * QVector4D(nx, ny, -1.0f, 1.0f), b = ivp * QVector4D(nx, ny, 1.0f, 1.0f);
  const QVector3D A = a.toVector3DAffine(), B = b.toVector3DAffine();
  const QVector3D dd = (B - A).normalized();
  for (int i = 0; i < 3; ++i) { origin[i] = A[i]; dir[i] = dd[i]; }
  return true;
}

QTransform ShapeViewer::planeToScreen(const double o[3], const double u[3], const double v[3]) const {
  const double ou[3] = {o[0] + u[0], o[1] + u[1], o[2] + u[2]}, ov[3] = {o[0] + v[0], o[1] + v[1], o[2] + v[2]};
  const QPointF S0 = worldToScreen(o), Su = worldToScreen(ou) - S0, Sv = worldToScreen(ov) - S0;
  return QTransform(Su.x(), Su.y(), Sv.x(), Sv.y(), S0.x(), S0.y());
}

double ShapeViewer::pixelsPerMm() const {
  d->ensureFit();
  return height() / std::max(1e-9, 2.0 * d->halfH);
}

void ShapeViewer::setPlanView(const double o[3], const double u[3], const double v[3], bool fit) {
  QVector3D U{float(u[0]), float(u[1]), float(u[2])}, V{float(v[0]), float(v[1]), float(v[2])};
  if (U.length() < 1e-9f || V.length() < 1e-9f) return;
  U.normalize();
  V = (V - U * QVector3D::dotProduct(U, V)).normalized();
  const QVector3D N = QVector3D::crossProduct(U, V).normalized();
  d->pushHistory();
  d->rot = QQuaternion::fromAxes(U, V, N).normalized();
  d->syncAzElFromRotKeep();
  if (fit) {
    d->needFit = true;
  } else {
    // keep the current target but bring the plane origin into the view centre if it is far outside
    (void)o;
  }
  d->autoFit = fit;
  d->invalidate();
  d->camChanged();
}

void ShapeViewer::setViewToFace(const QString& faceName) {
  double c[3], n[3];
  if (!faceCentroid(faceName, c, n)) return;
  QVector3D N{float(n[0]), float(n[1]), float(n[2])};
  if (N.length() < 1e-6f) return;
  N.normalize();
  // u: world +X projected on the face (else +Y), as the OnFace sketch frame rule
  QVector3D U = QVector3D(1, 0, 0) - N * N.x();
  if (U.length() < 1e-4f) U = QVector3D(0, 1, 0) - N * N.y();
  U.normalize();
  const QVector3D V = QVector3D::crossProduct(N, U).normalized();
  const double u[3] = {U.x(), U.y(), U.z()}, v[3] = {V.x(), V.y(), V.z()};
  setPlanView(c, u, v, false);
}

void ShapeViewer::setHomeView() { setView(View::Iso); }

void ShapeViewer::fitToBox(const double mn[3], const double mx[3]) {
  d->ensureFit();
  d->pushHistory();
  const QVector3D r = d->rightV(), u = d->upV();
  const QVector3D c(float(0.5 * (mn[0] + mx[0])), float(0.5 * (mn[1] + mx[1])), float(0.5 * (mn[2] + mx[2])));
  double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
  for (int i = 0; i < 8; ++i) {
    const QVector3D p(float((i & 1) ? mx[0] : mn[0]), float((i & 2) ? mx[1] : mn[1]), float((i & 4) ? mx[2] : mn[2]));
    const double a = QVector3D::dotProduct(p - c, r), b = QVector3D::dotProduct(p - c, u);
    u0 = std::min(u0, a); u1 = std::max(u1, a); v0 = std::min(v0, b); v1 = std::max(v1, b);
  }
  const double aspect = double(width()) / std::max(1, height());
  d->target = c + r * float(0.5 * (u0 + u1)) + u * float(0.5 * (v0 + v1));
  d->halfH = std::max({0.5 * (v1 - v0), 0.5 * (u1 - u0) / aspect, 1e-3}) * 1.2;
  d->needFit = false;
  d->autoFit = false;
  d->invalidate();
  d->camChanged();
}

bool ShapeViewer::previousView() {
  if (d->back.empty()) return false;
  d->fwd.push_back(d->cam());
  d->setCam(d->back.back());
  d->back.pop_back();
  d->invalidate();
  d->camChanged();
  return true;
}
bool ShapeViewer::nextView() {
  if (d->fwd.empty()) return false;
  d->back.push_back(d->cam());
  d->setCam(d->fwd.back());
  d->fwd.pop_back();
  d->invalidate();
  d->camChanged();
  return true;
}

double ShapeViewer::cameraAz() const {
  const QVector3D e = d->eyeDir();
  if (std::hypot(e.x(), e.y()) < 1e-6) return d->az;
  return std::atan2(e.y(), e.x()) / kDeg;
}
double ShapeViewer::cameraEl() const { return std::asin(std::clamp(double(d->eyeDir().z()), -1.0, 1.0)) / kDeg; }
double ShapeViewer::viewHalfHeight() const { d->ensureFit(); return d->halfH; }
QVector3D ShapeViewer::cameraTarget() const { d->ensureFit(); return d->target; }
void ShapeViewer::viewDirection(double out[3]) const {
  const QVector3D e = -d->eyeDir();
  out[0] = e.x(); out[1] = e.y(); out[2] = e.z();
}

QString ShapeViewer::pickAt(const QPoint& px, int kinds, int* kindOut) { return d->pickCombined(px, kinds, kindOut); }

void ShapeViewer::setPreselection(const QString& name) {
  int fi = 0;
  QString edge;
  if (!name.isEmpty()) {
    fi = d->faceIndex(name);
    if (!fi) for (const auto& e : d->mesh.edges) if (e.name == name) { edge = name; break; }
  }
  if (fi == d->hoverId && edge == d->hoverEdge) return;
  d->hoverId = fi;
  d->hoverEdge = edge;
  update();
}
QString ShapeViewer::preselection() const { return !d->hoverEdge.isEmpty() ? d->hoverEdge : d->faceName(d->hoverId); }
bool ShapeViewer::ghostVisible() const { return !d->ghostMesh.tris.empty(); }

bool ShapeViewer::faceCentroid(const QString& name, double c[3], double n[3]) const {
  const int id = d->faceIndex(name);
  if (!id) return false;
  const FaceRec& f = d->mesh.faces[size_t(id - 1)];
  double A = 0, C[3] = {0, 0, 0}, N[3] = {0, 0, 0};
  for (int t = 0; t + 2 < f.count; t += 3) {
    const float* a = &d->mesh.tris[size_t(f.first + t) * 6];
    const float* b = &d->mesh.tris[size_t(f.first + t + 1) * 6];
    const float* e = &d->mesh.tris[size_t(f.first + t + 2) * 6];
    const QVector3D A3(a[0], a[1], a[2]), B3(b[0], b[1], b[2]), E3(e[0], e[1], e[2]);
    const QVector3D cr = QVector3D::crossProduct(B3 - A3, E3 - A3);
    const double ar = 0.5 * cr.length();
    if (ar <= 0) continue;
    const QVector3D m = (A3 + B3 + E3) / 3.0f;
    // per-vertex normals stored in the mesh (outward, OCCT orientation applied by the tessellator)
    const QVector3D vn(a[3] + b[3] + e[3], a[4] + b[4] + e[4], a[5] + b[5] + e[5]);
    for (int i = 0; i < 3; ++i) { C[i] += m[i] * ar; N[i] += vn.normalized()[i] * ar; }
    A += ar;
  }
  if (A <= 0) return false;
  const double nl = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
  for (int i = 0; i < 3; ++i) { c[i] = C[i] / A; n[i] = nl > 1e-12 ? N[i] / nl : 0; }
  return true;
}

QStringList ShapeViewer::displayedFaceNames() const {
  QStringList out;
  for (const auto& f : d->mesh.faces) if (!f.name.isEmpty()) out << f.name;
  return out;
}

// ======================================================================= interaction
void ShapeViewer::mousePressEvent(QMouseEvent* e) {
  d->lastPos = d->pressPos = e->pos();
  d->pressBtn = e->button();
  d->pressMods = e->modifiers();
  d->dragging = false;
  if (!d->dblPending) d->clickCount = 1;
  if (d->scheme == NavigationScheme::SpaceClaim && e->button() == Qt::LeftButton && !d->dblPending) {
    // a third click shortly after a double click = triple click
    if (d->lastDbl.isValid() && d->lastDbl.elapsed() < QGuiApplication::styleHints()->mouseDoubleClickInterval() &&
        (e->pos() - d->lastDblPos).manhattanLength() < 6)
      d->clickCount = 3;
  }
  d->dblPending = false;
  setFocus();
}

void ShapeViewer::mouseDoubleClickEvent(QMouseEvent* e) {
  if (d->scheme != NavigationScheme::SpaceClaim) { mousePressEvent(e); return; }
  if (e->button() == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier)) {
    const QString f = d->faceName(d->pickAt(e->pos()));
    if (!f.isEmpty()) setViewToFace(f);
    d->pressBtn = Qt::NoButton;
    return;
  }
  d->dblPending = true;
  mousePressEvent(e);
  d->clickCount = 2;
}

void ShapeViewer::mouseMoveEvent(QMouseEvent* e) {
  const QPoint p = e->pos();
  const bool sc = d->scheme == NavigationScheme::SpaceClaim;
  if (e->buttons() == Qt::NoButton) {
    if (sc) {
      int kind = 0;
      const QString n = d->pickCombined(p, PickFace | PickEdge | PickDatum, &kind);
      const int fid = kind == PickFace ? d->faceIndex(n) : 0;
      const int dk = kind == PickDatum ? [&] { for (size_t k = 0; k < d->planes.size(); ++k) if (d->planes[k].name == n) return int(k); return -1; }() : -1;
      const QString edge = kind == PickEdge ? n : QString();
      if (fid != d->hoverId || dk != d->hoverDatum || edge != d->hoverEdge) {
        const bool faceChanged = fid != d->hoverId;
        d->hoverId = fid;
        d->hoverDatum = dk;
        d->hoverEdge = edge;
        if (faceChanged) emit faceHovered(d->faceName(fid));
        emit hovered(n, kind);
        update();
      }
      return;
    }
    const int raw = d->pickAt(p);
    const int dk = d->datumIndex(raw);
    const int id = dk >= 0 ? 0 : raw;
    if (id != d->hoverId || dk != d->hoverDatum) {
      const bool faceChanged = id != d->hoverId;
      d->hoverId = id;
      d->hoverDatum = dk;
      if (faceChanged) emit faceHovered(d->faceName(id));
      update();
    }
    return;
  }
  const QPoint dp = p - d->lastPos;
  if (!d->dragging && (p - d->pressPos).manhattanLength() > 4) {
    d->dragging = true;
    d->pushHistory();
  }
  if (!d->dragging) return;
  d->lastPos = p;
  if (sc) {
    DragMode m = DragMode::None;
    if (e->buttons() & Qt::MiddleButton) {
      m = (d->pressMods & Qt::ControlModifier) ? DragMode::Zoom : (d->pressMods & Qt::ShiftModifier) ? DragMode::Pan : DragMode::Spin;
    } else if (e->buttons() & Qt::LeftButton) {
      m = d->leftMode;
    }
    if (m == DragMode::None) return;
    if (m == DragMode::Pan) {
      const double s = 2.0 * d->halfH / std::max(1, height());
      d->target -= d->rightV() * float(dp.x() * s);
      d->target += d->upV() * float(dp.y() * s);
    } else if (m == DragMode::Zoom) {
      d->halfH = std::clamp(d->halfH * std::pow(1.01, double(dp.y())), 1e-3, 1e6);
    } else {
      const QVector3D up = d->upV(), right = d->rightV();
      const QQuaternion qu = QQuaternion::fromAxisAndAngle(up, float(-dp.x() * 0.4));
      const QQuaternion qr = QQuaternion::fromAxisAndAngle(right, float(-dp.y() * 0.4));
      d->rot = (qu * qr * d->rot).normalized();
      d->syncAzElFromRotKeep();
    }
    d->autoFit = false;
    d->needFit = false;
    if (d->hoverId || !d->hoverEdge.isEmpty()) { d->hoverId = 0; d->hoverEdge.clear(); emit faceHovered(QString()); }
    d->invalidate();
    d->camChanged();
    return;
  }
  const bool pan = (e->buttons() & Qt::MiddleButton) || ((e->buttons() & Qt::LeftButton) && (d->pressMods & Qt::ShiftModifier));
  if (pan) {
    const double s = 2.0 * d->halfH / std::max(1, height());
    d->target -= d->rightV() * float(dp.x() * s);
    d->target += d->upV() * float(dp.y() * s);
    d->autoFit = false;
  } else if (e->buttons() & Qt::LeftButton) {
    d->setAzEl(d->az - dp.x() * 0.4, std::clamp(d->el + dp.y() * 0.4, -90.0, 90.0));
  } else {
    return;
  }
  if (d->hoverId) { d->hoverId = 0; emit faceHovered(QString()); }
  d->invalidate();
  d->camChanged();
}

void ShapeViewer::mouseReleaseEvent(QMouseEvent* e) {
  if (d->scheme == NavigationScheme::SpaceClaim) {
    if (!d->dragging && e->button() == Qt::LeftButton && d->pressBtn == Qt::LeftButton) {
      int kind = 0;
      const QString n = d->pickCombined(e->pos(), PickFace | PickEdge | PickDatum, &kind);
      const int cc = d->clickCount;
      if (cc == 2) { d->lastDbl.restart(); d->lastDblPos = e->pos(); }
      else if (cc == 3) d->lastDbl.invalidate();
      emit picked(n, kind, d->pressMods, cc, e->pos());
      if (cc == 2) emit doubleClicked(n, kind);
      else if (cc == 3) emit tripleClicked(n, kind);
      else if (kind == PickEdge) emit edgePicked(n);
      else if (kind == PickDatum) emit datumPicked(n);
      else emit facePicked(n);
    }
    d->dragging = false;
    d->pressBtn = Qt::NoButton;
    return;
  }
  if (!d->dragging && e->button() == Qt::LeftButton && d->pressBtn == Qt::LeftButton) {
    if (d->pressMods & Qt::ControlModifier) {
      emit edgePicked(d->pickEdge(e->pos()));
    } else if (!(d->pressMods & Qt::ShiftModifier)) {
      const int raw = d->pickAt(e->pos());
      const int dk = d->datumIndex(raw);
      if (dk >= 0 && dk < int(d->planes.size())) emit datumPicked(d->planes[dk].name);
      else emit facePicked(d->faceName(raw));
    }
  }
  d->dragging = false;
  d->pressBtn = Qt::NoButton;
}

void ShapeViewer::wheelEvent(QWheelEvent* e) {
  const double steps = e->angleDelta().y() / 120.0;
  if (steps == 0) return;
  d->ensureFit();
  const double f = std::pow(0.88, steps);
  const double nh = std::clamp(d->halfH * f, 1e-3, 1e6);
  const double aspect = double(width()) / std::max(1, height());
  const QPointF p = e->position();
  const double nx = p.x() / width() * 2.0 - 1.0, ny = 1.0 - p.y() / height() * 2.0;
  d->target += d->rightV() * float(nx * aspect * (d->halfH - nh)) + d->upV() * float(ny * (d->halfH - nh));
  d->halfH = nh;
  d->needFit = false;
  d->autoFit = false;
  d->invalidate();
  d->camChanged();
  e->accept();
}

} // namespace pc::viewer
