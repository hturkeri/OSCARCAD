// PersistCAD model — named selections: resolution, editing, notifications, STEP export with names.
// See pc/NamedSelections.h and docs/named_selections/MODEL.md.
#include "pc/NamedSelections.h"
#include "pc/ModelNaming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include "pc/ViewColors.h"   // SC-COLOR
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <algorithm>
#include <cctype>
#include <functional>
#include <cmath>
#include <set>

namespace pc::model {

// ================================================================== status strings
const char* toString(NsStatus s) {
  switch (s) {
    case NsStatus::Ok: return "ok";
    case NsStatus::Split: return "split";
    case NsStatus::Merged: return "merged";
    case NsStatus::Suspect: return "suspect";
    case NsStatus::Partial: return "partial";
    case NsStatus::Lost: return "lost";
    case NsStatus::Suppressed: return "suppressed";
    case NsStatus::Failed: return "failed";
    case NsStatus::NotYet: return "notyet";
    case NsStatus::Empty: return "empty";
  }
  return "?";
}
const char* statusLabel(NsStatus s) {
  switch (s) {
    case NsStatus::Ok: return "Tamam";
    case NsStatus::Split: return "Bölündü";
    case NsStatus::Merged: return "Birleşti";
    case NsStatus::Suspect: return "Şüpheli";
    case NsStatus::Partial: return "Kısmi";
    case NsStatus::Lost: return "Kayıp";
    case NsStatus::Suppressed: return "Bastırıldı";
    case NsStatus::Failed: return "Hatalı";
    case NsStatus::NotYet: return "Henüz yok";
    case NsStatus::Empty: return "Boş";
  }
  return "?";
}
bool isResolved(NsStatus s) {
  return s == NsStatus::Ok || s == NsStatus::Split || s == NsStatus::Merged || s == NsStatus::Suspect;
}
const char* toString(NsChange::Kind k) {
  switch (k) {
    case NsChange::Kind::Added: return "added";
    case NsChange::Kind::Removed: return "removed";
    case NsChange::Kind::Renamed: return "renamed";
    case NsChange::Kind::MembersChanged: return "members";
    case NsChange::Kind::PropertiesChanged: return "properties";
    case NsChange::Kind::StatusChanged: return "status";
    case NsChange::Kind::FacesChanged: return "faces";
  }
  return "?";
}

namespace {

// ================================================================== name parsing helpers
bool allDigits(const std::string& s, size_t from) {
  if (from >= s.size()) return false;
  for (size_t i = from; i < s.size(); ++i)
    if (!std::isdigit((unsigned char)s[i])) return false;
  return true;
}

// Positional ("piece") references: "<P>:k", "<P>:dk" (split pieces) and "<fid>.u<k>" (fallback faces).
struct Piece {
  bool piece = false;
  bool fallback = false;   // ".u<k>" family: P = "<fid>.u"
  std::string parent;
};
Piece pieceOf(const std::string& ref) {
  Piece p;
  size_t c = ref.rfind(':');
  if (c != std::string::npos && ref.find_first_of("()|", c) == std::string::npos) {
    size_t j = c + 1;
    if (j < ref.size() && ref[j] == 'd') ++j;
    if (allDigits(ref, j)) {
      p.piece = true;
      p.parent = ref.substr(0, c);
      return p;
    }
  }
  size_t d = ref.rfind(".u");
  if (d != std::string::npos && allDigits(ref, d + 2)) {
    p.piece = p.fallback = true;
    p.parent = ref.substr(0, d + 2);
  }
  return p;
}
bool isChildToken(const std::string& P, bool fallback, const std::string& t) {
  if (t.size() <= P.size() || t.compare(0, P.size(), P) != 0) return false;
  if (fallback) return allDigits(t, P.size());
  if (t[P.size()] != ':') return false;
  size_t j = P.size() + 1;
  if (j < t.size() && t[j] == 'd') ++j;
  return allDigits(t, j);
}
// Strip every split suffix (":k", ":dk") — same rule as the regeneration's resolveRef.
std::string baseName(const std::string& n) {
  std::string out;
  for (size_t i = 0; i < n.size();) {
    if (n[i] == ':') {
      size_t j = i + 1;
      if (j < n.size() && n[j] == 'd') ++j;
      size_t k = j;
      while (k < n.size() && std::isdigit((unsigned char)n[k])) ++k;
      if (k > j && (k == n.size() || n[k] == '|' || n[k] == ')' || n[k] == ':')) {
        i = k;
        continue;
      }
    }
    out += n[i++];
  }
  return out;
}
// Index-based names of the naming contract whose index is an ORDER, not an identity: Hole "h<k>" (sketch point
// order), Fillet "f<k>" / Chamfer "c<k>" (edge-name order). Family key = the name with such segments -> "*".
std::string familyKey(const std::string& n, bool& indexed) {
  indexed = false;
  std::string out, seg;
  size_t i = 0, k = 0;
  while (i <= n.size()) {
    if (i == n.size() || n[i] == '.') {
      bool idx = k > 0 && seg.size() >= 2 && (seg[0] == 'h' || seg[0] == 'f' || seg[0] == 'c') && allDigits(seg, 1);
      indexed |= idx;
      out += (k ? "." : "") + (idx ? std::string("*") : seg);
      seg.clear();
      ++k;
    } else {
      seg += n[i];
    }
    ++i;
  }
  return out;
}

// Feature that created a face name: the prefix up to the first '.'.
std::string creatorOf(const std::string& faceRef) {
  size_t d = faceRef.find('.');
  return d == std::string::npos ? faceRef : faceRef.substr(0, d);
}
// "edge(<a>|<b>)[:k]" -> {a, b}, suffix ":k" (parentheses inside face names, e.g. Shell rim faces, are honoured).
bool parseEdge(const std::string& e, std::vector<std::string>& faces, std::string* suffix = nullptr) {
  faces.clear();
  if (e.compare(0, 5, "edge(") != 0) return false;
  int depth = 1;
  size_t i = 5, start = 5;
  for (; i < e.size(); ++i) {
    char ch = e[i];
    if (ch == '(') ++depth;
    else if (ch == ')') {
      if (--depth == 0) break;
    } else if (ch == '|' && depth == 1) {
      faces.push_back(e.substr(start, i - start));
      start = i + 1;
    }
  }
  if (i >= e.size()) return false;
  faces.push_back(e.substr(start, i - start));
  if (suffix) *suffix = e.substr(i + 1);
  return !faces.empty();
}

// ================================================================== geometry signatures
void surfaceInfo(const TopoDS_Face& F, NsSignature& s) {
  BRepAdaptor_Surface a(F, Standard_False);
  auto setDir = [&](const gp_Dir& d) { s.nx = d.X(); s.ny = d.Y(); s.nz = d.Z(); };
  switch (a.GetType()) {
    case GeomAbs_Plane: {
      s.surface = "plane";
      gp_Dir n = a.Plane().Axis().Direction();
      if (F.Orientation() == TopAbs_REVERSED) n.Reverse();
      setDir(n);
      break;
    }
    case GeomAbs_Cylinder: s.surface = "cylinder"; setDir(a.Cylinder().Axis().Direction()); s.radius = a.Cylinder().Radius(); break;
    case GeomAbs_Cone: s.surface = "cone"; setDir(a.Cone().Axis().Direction()); s.radius = a.Cone().RefRadius(); break;
    case GeomAbs_Sphere: s.surface = "sphere"; s.radius = a.Sphere().Radius(); break;
    case GeomAbs_Torus: s.surface = "torus"; setDir(a.Torus().Axis().Direction()); s.radius = a.Torus().MajorRadius(); break;
    case GeomAbs_BezierSurface:
    case GeomAbs_BSplineSurface: s.surface = "bspline"; break;
    case GeomAbs_SurfaceOfRevolution: s.surface = "revolution"; break;
    case GeomAbs_SurfaceOfExtrusion: s.surface = "extrusion"; break;
    default: s.surface = "other"; break;
  }
}

NsSignature computeFaceSig(const ModelResult& r, const std::string& name, bool withNeighbours) {
  NsSignature s;
  auto it = r.faces.find(name);
  if (it == r.faces.end()) return s;
  try {
    surfaceInfo(it->second, s);
    GProp_GProps g;
    BRepGProp::SurfaceProperties(it->second, g);
    s.size = g.Mass();
    gp_Pnt c = s.size > 1e-12 ? g.CentreOfMass() : naming::faceCentroid(it->second);
    s.cx = c.X();
    s.cy = c.Y();
    s.cz = c.Z();
  } catch (...) {
    return s;
  }
  if (withNeighbours) {
    std::set<std::string> nb;
    std::vector<std::string> fs;
    for (auto& [en, e] : r.edges)
      if (parseEdge(en, fs) && fs.size() == 2) {
        if (fs[0] == name) nb.insert(fs[1]);
        else if (fs[1] == name) nb.insert(fs[0]);
      }
    for (auto& n : nb) {
      if (s.neighbours.size() >= 16) break;
      s.neighbours.push_back(n);
    }
  }
  s.body = r.bodyOfFace(name);
  s.valid = true;
  return s;
}

double clamp1(double v) { return std::max(-1.0, std::min(1.0, v)); }

// Aggregate geometry of a group of current faces (union of split pieces).
struct Agg {
  double area = 0;
  gp_XYZ c{0, 0, 0};
  std::string type;
  gp_XYZ dir{0, 0, 0};
  bool hasDir = false;
};

// Similarity score of a stored signature against a face group (0 = identical; ~1 = clearly different).
double score(const NsSignature& s, const Agg& a) {
  const double L = std::max(std::sqrt(std::max(s.size, 0.0)), 1e-3);
  gp_XYZ d = a.c - gp_XYZ(s.cx, s.cy, s.cz);
  double sc = d.Modulus() / L;
  if (s.size > 1e-12 && a.area > 1e-12) sc += 0.5 * std::abs(std::log(a.area / s.size));
  else sc += 0.5;
  if (s.surface != a.type) sc += 1.0;
  else if (a.hasDir) {
    gp_XYZ sd(s.nx, s.ny, s.nz);
    if (sd.Modulus() > 1e-9 && a.dir.Modulus() > 1e-9) {
      double dot = clamp1(sd.Normalized().Dot(a.dir.Normalized()));
      sc += s.surface == "plane" ? std::acos(dot) / M_PI : std::acos(std::abs(dot)) / (M_PI / 2);
    }
  }
  return sc;
}

// ================================================================== resolution context
struct Ctx {
  const Document& doc;
  const ModelResult& r;
  int stopAfter;
  std::map<std::string, std::vector<std::string>> byToken;   // lineage token -> current faces
  std::map<std::string, NsSignature> sigs;                   // face sig cache (no neighbours)
  std::map<std::string, std::string> statusMsg;              // feature id -> failure message
  std::set<std::string> failed;
  std::map<std::string, std::vector<std::string>> own;       // lineage of faces missing from r.faceLineage

  Ctx(const Document& d, const ModelResult& res, int stop) : doc(d), r(res), stopAfter(stop) {
    for (auto& [f, toks] : r.faceLineage)
      for (auto& t : toks) byToken[t].push_back(f);
    // a result without lineage (built by hand / older code path): every face is its own lineage
    for (auto& [f, face] : r.faces)
      if (!r.faceLineage.count(f)) {
        byToken[f].push_back(f);
        own.emplace(f, std::vector<std::string>{f});
      }
    for (auto& st : r.status)
      if (!st.ok) {
        failed.insert(st.featureId);
        statusMsg[st.featureId] = st.message;
      }
  }
  const NsSignature& sig(const std::string& face) {
    auto it = sigs.find(face);
    if (it != sigs.end()) return it->second;
    return sigs[face] = computeFaceSig(r, face, false);
  }
  const std::vector<std::string>* facesOf(const std::string& token) const {
    auto it = byToken.find(token);
    return it == byToken.end() ? nullptr : &it->second;
  }
  Agg agg(const std::vector<std::string>& faces) {
    Agg a;
    double best = -1;
    for (auto& f : faces) {
      const NsSignature& s = sig(f);
      if (!s.valid) continue;
      a.area += s.size;
      a.c += gp_XYZ(s.cx, s.cy, s.cz) * s.size;
      if (s.size > best) {
        best = s.size;
        a.type = s.surface;
        a.dir = gp_XYZ(s.nx, s.ny, s.nz);
        a.hasDir = a.dir.Modulus() > 1e-9;
      }
    }
    if (a.area > 1e-12) a.c /= a.area;
    return a;
  }
};

// Choose among split pieces: groups of current faces by the child token of `P`; the best-scoring group is refined
// into its own sub-pieces when one of them matches the signature clearly better (a piece that an EARLIER inserted
// feature already cut). Returns the chosen faces (sorted) and its score / ambiguity.
std::vector<std::string> pickPiece(Ctx& c, const std::string& P, bool fallback, const NamedSelectionMember& m, int depth,
                                   double& bestScore, bool& ambiguous, std::string& token) {
  std::map<std::string, std::set<std::string>> groups;
  using Lin = std::map<std::string, std::vector<std::string>>;
  for (const Lin* lin : {static_cast<const Lin*>(&c.r.faceLineage), static_cast<const Lin*>(&c.own)})
    for (auto& [f, toks] : *lin)
      for (auto& t : toks)
        if (isChildToken(P, fallback, t)) groups[t].insert(f);
  if (groups.empty()) return {};
  ambiguous = false;
  if (!m.sig.valid) {  // no signature (hand-written file): positional name as stored
    auto it = groups.find(m.ref);
    if (it == groups.end()) {
      ambiguous = true;
      it = groups.begin();
    }
    token = it->first;
    bestScore = 0;
    return {it->second.begin(), it->second.end()};
  }
  std::vector<std::pair<double, std::string>> sc;
  for (auto& [t, fs] : groups) sc.emplace_back(score(m.sig, c.agg({fs.begin(), fs.end()})), t);
  std::sort(sc.begin(), sc.end());
  bestScore = sc[0].first;
  token = sc[0].second;
  // tie on geometry: prefer the stored positional name
  for (auto& [s, t] : sc)
    if (t == m.ref && s <= bestScore + 1e-9) token = t;
  if (sc.size() > 1 && sc[1].first - bestScore < 0.1) ambiguous = true;
  std::vector<std::string> out(groups[token].begin(), groups[token].end());
  if (depth < 4 && out.size() > 1) {
    double ss = 0;
    bool sa = false;
    std::string st;
    auto sub = pickPiece(c, token, false, m, depth + 1, ss, sa, st);
    if (!sub.empty() && ss < bestScore - 0.05) {
      bestScore = ss;
      ambiguous = sa;
      token = st;
      return sub;
    }
  }
  return out;
}

// Why does a face reference not resolve? Sets status + diagnostic.
void explainMissing(Ctx& c, const std::string& ref, NsMemberResult& mr) {
  const std::string cr = creatorOf(ref);
  int idx = c.doc.indexOf(cr);
  if (idx < 0) {
    mr.status = NsStatus::Lost;
    mr.diagnostic = "'" + ref + "' kayıp: oluşturan özellik '" + cr + "' belgede yok (silinmiş)";
    return;
  }
  if (c.stopAfter >= 0 && idx > c.stopAfter) {
    mr.status = NsStatus::NotYet;
    mr.diagnostic = "'" + ref + "': oluşturan özellik '" + cr + "' geri alma konumundan sonra";
    return;
  }
  if (c.doc.features[idx].suppressed) {
    mr.status = NsStatus::Suppressed;
    mr.diagnostic = "'" + ref + "': oluşturan özellik '" + cr + "' bastırılmış";
    return;
  }
  if (c.failed.count(cr)) {
    mr.status = NsStatus::Failed;
    mr.diagnostic = "'" + ref + "': oluşturan özellik '" + cr + "' hatalı (" + c.statusMsg[cr] + ")";
    return;
  }
  mr.status = NsStatus::Lost;
  std::string by;
  for (std::string t = ref;;) {
    auto it = c.r.removedBy.find(t);
    if (it != c.r.removedBy.end()) {
      by = it->second;
      break;
    }
    Piece p = pieceOf(t);
    if (!p.piece || p.fallback) break;
    t = p.parent;
  }
  if (!by.empty()) mr.diagnostic = "'" + ref + "' kayıp: '" + by + "' tarafından kaldırıldı (tüketildi)";
  else mr.diagnostic = "'" + ref + "' kayıp: yüz artık oluşmuyor (çizim veya özellik değişmiş olabilir)";
}

void suggest(Ctx& c, const NamedSelectionMember& m, NsMemberResult& mr) {
  if (!m.sig.valid) return;
  std::vector<std::pair<double, std::string>> sc;
  for (auto& [f, face] : c.r.faces) {
    double s = score(m.sig, c.agg({f}));
    if (s < 1.5) sc.emplace_back(s, f);
  }
  std::sort(sc.begin(), sc.end());
  for (size_t i = 0; i < sc.size() && i < 3; ++i) mr.suggestions.push_back(sc[i].second);
}

void ownersOf(Ctx& c, NsMemberResult& mr) {
  std::set<std::string> b;
  for (auto& f : mr.faces) {
    std::string n = c.r.bodyOfFace(f);
    if (!n.empty()) b.insert(n);
  }
  for (auto& e : mr.edges) {
    std::vector<std::string> fs;
    if (parseEdge(e, fs))
      for (auto& f : fs) {
        std::string n = c.r.bodyOfFace(f);
        if (!n.empty()) b.insert(n);
      }
  }
  for (auto& n : mr.bodies) b.insert(n);
  mr.bodies.assign(b.begin(), b.end());
}

// Current faces of a face reference (lineage / piece choice); empty if missing.
std::vector<std::string> facesForRef(Ctx& c, const NamedSelectionMember& m, const std::string& ref, bool& ambiguous,
                                     bool& rejoined) {
  ambiguous = rejoined = false;
  Piece p = pieceOf(ref);
  if (!p.piece) {
    const auto* v = c.facesOf(ref);
    return v ? *v : std::vector<std::string>{};
  }
  double s = 0;
  std::string tok;
  NamedSelectionMember mm = m;
  mm.ref = ref;
  auto out = pickPiece(c, p.parent, p.fallback, mm, 0, s, ambiguous, tok);
  if (!out.empty()) return out;
  if (const auto* v = c.facesOf(ref)) return *v;  // exact (defensive)
  // pieces re-joined (the splitting feature was removed / suppressed): the parent face carries the name
  for (Piece q = p; q.piece && !q.fallback;) {
    if (const auto* v = c.facesOf(q.parent)) {
      rejoined = true;
      return *v;
    }
    q = pieceOf(q.parent);
  }
  return {};
}

NsMemberResult resolveFace(Ctx& c, const NamedSelectionMember& m) {
  NsMemberResult mr;
  mr.kind = m.kind;
  mr.ref = m.ref;
  bool amb = false, rejoined = false;
  mr.faces = facesForRef(c, m, m.ref, amb, rejoined);
  std::sort(mr.faces.begin(), mr.faces.end());
  if (mr.faces.empty()) {
    explainMissing(c, m.ref, mr);
    if (mr.status == NsStatus::Lost) suggest(c, m, mr);
    return mr;
  }
  mr.status = mr.faces.size() > 1 ? NsStatus::Split : NsStatus::Ok;
  const std::string rb = baseName(pieceOf(m.ref).fallback ? creatorOf(m.ref) : m.ref);
  for (auto& f : mr.faces) {
    bool related = pieceOf(m.ref).fallback ? creatorOf(f) == rb : baseName(f) == rb;
    if (!related) {
      mr.status = NsStatus::Merged;
      mr.diagnostic = "'" + m.ref + "' başka bir yüzle birleşti: '" + f + "'";
      break;
    }
  }
  if (rejoined) {
    mr.status = NsStatus::Merged;
    mr.diagnostic = "'" + m.ref + "' parçası kardeş parçalarıyla yeniden birleşti";
  }
  if (m.sig.valid) {
    Agg a = c.agg(mr.faces);
    if (!a.type.empty() && !m.sig.surface.empty() && a.type != m.sig.surface) {
      mr.status = NsStatus::Suspect;
      mr.diagnostic = "'" + m.ref + "' yüz tipi değişti (" + m.sig.surface + " -> " + a.type + ")";
    }
  }
  // index-based names: warn (never switch) when a sibling of the family now matches the stored geometry clearly
  // better than the named one — the order behind "h<k>"/"f<k>"/"c<k>" may have changed
  bool indexed = false;
  const std::string fam = familyKey(baseName(m.ref), indexed);
  if (indexed && m.sig.valid && !pieceOf(m.ref).piece) {
    const double own = score(m.sig, c.agg(mr.faces));
    std::map<std::string, std::vector<std::string>> sib;
    bool dummy = false;
    for (auto& [f, face] : c.r.faces) {
      const std::string b = baseName(f);
      if (b != baseName(m.ref) && familyKey(b, dummy) == fam) sib[b].push_back(f);
    }
    std::string best;
    double bs = 1e300;
    for (auto& [b, fs] : sib) {
      double s = score(m.sig, c.agg(fs));
      if (s < bs) { bs = s; best = b; }
    }
    if (!best.empty() && bs < own - 0.5 && bs < 0.5 * own) {
      mr.status = NsStatus::Suspect;
      mr.diagnostic = "'" + m.ref + "' sıra numaralı bir ad; '" + best +
                      "' kayıtlı geometriye daha yakın (sıra değişmiş olabilir), kontrol edin";
      mr.suggestions = sib[best];
    }
  }
  if (amb) {
    mr.status = NsStatus::Suspect;
    mr.diagnostic = "'" + m.ref + "' parça seçimi belirsiz (benzer parçalar var), kontrol edin";
    suggest(c, m, mr);
  }
  return mr;
}

NsMemberResult resolveEdge(Ctx& c, const NamedSelectionMember& m) {
  NsMemberResult mr;
  mr.kind = m.kind;
  mr.ref = m.ref;
  std::vector<std::string> fs;
  std::string suffix;
  if (!parseEdge(m.ref, fs, &suffix) || fs.empty() || fs.size() > 2) {
    mr.status = NsStatus::Lost;
    mr.diagnostic = "'" + m.ref + "' geçersiz kenar adı";
    return mr;
  }
  std::vector<std::set<std::string>> F;
  for (auto& n : fs) {
    bool amb = false, rej = false;
    NamedSelectionMember fm;
    fm.ref = n;
    auto v = facesForRef(c, fm, n, amb, rej);
    if (v.empty()) {
      explainMissing(c, n, mr);
      mr.diagnostic = "kenar '" + m.ref + "': " + mr.diagnostic;
      return mr;
    }
    F.emplace_back(v.begin(), v.end());
  }
  std::vector<std::pair<std::string, gp_Pnt>> cand;
  std::vector<std::string> cf;
  for (auto& [en, e] : c.r.edges) {
    if (!parseEdge(en, cf) || cf.size() != fs.size()) continue;
    bool hit = fs.size() == 1 ? F[0].count(cf[0]) > 0
                              : ((F[0].count(cf[0]) && F[1].count(cf[1])) || (F[0].count(cf[1]) && F[1].count(cf[0])));
    if (!hit) continue;
    BRepAdaptor_Curve cv(e);
    cand.emplace_back(en, cv.Value(0.5 * (cv.FirstParameter() + cv.LastParameter())));
  }
  if (cand.empty()) {
    mr.status = NsStatus::Lost;
    mr.diagnostic = "kenar '" + m.ref + "' kayıp: komşu yüzler artık ortak kenar paylaşmıyor (yuvarlatma/pah olabilir)";
    return mr;
  }
  if (!suffix.empty() && cand.size() > 1 && m.sig.valid) {  // ":k": one of several edges of the same face pair
    gp_Pnt s(m.sig.cx, m.sig.cy, m.sig.cz);
    auto best = std::min_element(cand.begin(), cand.end(),
                                 [&](auto& a, auto& b) { return a.second.Distance(s) < b.second.Distance(s); });
    mr.edges.push_back(best->first);
  } else {
    for (auto& [n, p] : cand) mr.edges.push_back(n);
  }
  std::sort(mr.edges.begin(), mr.edges.end());
  mr.status = mr.edges.size() > 1 ? NsStatus::Split : NsStatus::Ok;
  return mr;
}

NsMemberResult resolveBody(Ctx& c, const NamedSelectionMember& m) {
  NsMemberResult mr;
  mr.kind = m.kind;
  mr.ref = m.ref;
  std::vector<std::string> bs;
  if (c.r.body(m.ref)) bs.push_back(m.ref);
  if (bs.empty())
    for (auto& b : c.r.bodies)
      if (b.name.size() > m.ref.size() + 1 && b.name.compare(0, m.ref.size() + 1, m.ref + ":") == 0) bs.push_back(b.name);
  if (!bs.empty()) mr.status = bs.size() > 1 ? NsStatus::Split : NsStatus::Ok;
  else {
    std::set<std::string> hit;  // merged into another body: follow its faces
    for (auto& a : m.anchors)
      if (const auto* v = c.facesOf(a))
        for (auto& f : *v) {
          std::string b = c.r.bodyOfFace(f);
          if (!b.empty()) hit.insert(b);
        }
    bs.assign(hit.begin(), hit.end());
    if (!bs.empty()) {
      mr.status = NsStatus::Merged;
      mr.diagnostic = "gövde '" + m.ref + "' başka gövdeyle birleşti: " + bs.front();
    }
  }
  if (bs.empty()) {
    mr.status = NsStatus::Lost;
    mr.diagnostic = "gövde '" + m.ref + "' kayıp";
    // creating feature diagnostics through the anchors
    if (!m.anchors.empty()) {
      NsMemberResult tmp;
      explainMissing(c, m.anchors.front(), tmp);
      if (tmp.status != NsStatus::Lost) {
        mr.status = tmp.status;
        mr.diagnostic = "gövde '" + m.ref + "': " + tmp.diagnostic;
      }
    }
    return mr;
  }
  mr.bodies = bs;
  std::set<std::string> fs;
  for (auto& b : bs)
    if (const BodyInfo* bi = c.r.body(b)) fs.insert(bi->faces.begin(), bi->faces.end());
  mr.faces.assign(fs.begin(), fs.end());
  return mr;
}

int rank(NsStatus s) {
  switch (s) {
    case NsStatus::Ok: return 0;
    case NsStatus::Split: return 1;
    case NsStatus::Merged: return 2;
    case NsStatus::Suspect: return 3;
    case NsStatus::NotYet: return 4;
    case NsStatus::Suppressed: return 5;
    case NsStatus::Failed: return 6;
    case NsStatus::Lost: return 7;
    default: return 8;
  }
}

NsResolution resolveIn(Ctx& c, const NamedSelection& sel) {
  NsResolution out;
  out.id = sel.id;
  out.name = sel.name;
  out.rolledBack = c.stopAfter >= 0;
  std::set<std::string> fs, es, bs;
  int nRes = 0, nUn = 0;
  NsStatus worstRes = NsStatus::Ok, worstUn = NsStatus::NotYet;
  for (size_t i = 0; i < sel.members.size(); ++i) {
    const auto& m = sel.members[i];
    NsMemberResult mr;
    try {
      mr = m.kind == NsMemberKind::Face ? resolveFace(c, m) : m.kind == NsMemberKind::Edge ? resolveEdge(c, m) : resolveBody(c, m);
    } catch (...) {
      mr.kind = m.kind;
      mr.ref = m.ref;
      mr.status = NsStatus::Lost;
      mr.diagnostic = "'" + m.ref + "' çözümlenemedi (geometri hatası)";
    }
    mr.index = i;
    ownersOf(c, mr);
    if (isResolved(mr.status)) {
      ++nRes;
      if (rank(mr.status) > rank(worstRes)) worstRes = mr.status;
    } else {
      ++nUn;
      if (rank(mr.status) > rank(worstUn)) worstUn = mr.status;
    }
    fs.insert(mr.faces.begin(), mr.faces.end());
    es.insert(mr.edges.begin(), mr.edges.end());
    bs.insert(mr.bodies.begin(), mr.bodies.end());
    if (!mr.diagnostic.empty()) out.diagnostic += (out.diagnostic.empty() ? "" : "; ") + mr.diagnostic;
    out.members.push_back(std::move(mr));
  }
  if (sel.members.empty()) out.status = NsStatus::Empty;
  else if (nUn == 0) out.status = worstRes;
  else if (nRes == 0) out.status = worstUn;
  else out.status = NsStatus::Partial;
  out.faces.assign(fs.begin(), fs.end());
  out.edges.assign(es.begin(), es.end());
  out.bodies.assign(bs.begin(), bs.end());
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

} // namespace

// ================================================================== public: signatures / capture
NsSignature faceSignature(const ModelResult& r, const std::string& faceName) { return computeFaceSig(r, faceName, true); }

NsSignature edgeSignature(const ModelResult& r, const std::string& edgeName) {
  NsSignature s;
  auto it = r.edges.find(edgeName);
  if (it == r.edges.end()) return s;
  try {
    BRepAdaptor_Curve cv(it->second);
    switch (cv.GetType()) {
      case GeomAbs_Line: {
        s.surface = "line";
        gp_Dir d = cv.Line().Direction();
        s.nx = d.X(); s.ny = d.Y(); s.nz = d.Z();
        break;
      }
      case GeomAbs_Circle: {
        s.surface = "circle";
        gp_Dir d = cv.Circle().Axis().Direction();
        s.nx = d.X(); s.ny = d.Y(); s.nz = d.Z();
        s.radius = cv.Circle().Radius();
        break;
      }
      case GeomAbs_Ellipse: s.surface = "ellipse"; break;
      case GeomAbs_BezierCurve:
      case GeomAbs_BSplineCurve: s.surface = "bspline"; break;
      default: s.surface = "other"; break;
    }
    gp_Pnt m = cv.Value(0.5 * (cv.FirstParameter() + cv.LastParameter()));
    s.cx = m.X(); s.cy = m.Y(); s.cz = m.Z();
    GProp_GProps g;
    BRepGProp::LinearProperties(it->second, g);
    s.size = g.Mass();
  } catch (...) {
    return s;
  }
  std::vector<std::string> fs;
  if (parseEdge(edgeName, fs)) {
    s.neighbours = fs;
    s.body = r.bodyOfFace(fs.front());
  }
  s.valid = true;
  return s;
}

NsSignature bodySignature(const ModelResult& r, const std::string& bodyName) {
  NsSignature s;
  const BodyInfo* b = r.body(bodyName);
  if (!b) return s;
  try {
    GProp_GProps g;
    BRepGProp::VolumeProperties(b->shape, g);
    s.size = std::abs(g.Mass());
    gp_Pnt c = g.CentreOfMass();
    s.cx = c.X(); s.cy = c.Y(); s.cz = c.Z();
  } catch (...) {
    return s;
  }
  s.surface = "solid";
  s.body = bodyName;
  s.valid = true;
  return s;
}

bool captureMember(const ModelResult& r, NsMemberKind kind, const std::string& ref, NamedSelectionMember& out,
                   std::string* err) {
  out = NamedSelectionMember{};
  out.kind = kind;
  out.ref = ref;
  if (kind == NsMemberKind::Face) {
    out.sig = faceSignature(r, ref);
    if (!out.sig.valid) { if (err) *err = "yüz bulunamadı '" + ref + "'"; return false; }
  } else if (kind == NsMemberKind::Edge) {
    out.sig = edgeSignature(r, ref);
    if (!out.sig.valid) { if (err) *err = "kenar bulunamadı '" + ref + "'"; return false; }
  } else {
    out.sig = bodySignature(r, ref);
    if (!out.sig.valid) { if (err) *err = "gövde bulunamadı '" + ref + "'"; return false; }
    const BodyInfo* b = r.body(ref);
    // anchors: non-positional face names first (most stable), at most 32
    for (int pass = 0; pass < 2; ++pass)
      for (auto& f : b->faces) {
        if (out.anchors.size() >= 32) break;
        if ((pieceOf(f).piece ? 1 : 0) == pass) out.anchors.push_back(f);
      }
  }
  return true;
}

// ================================================================== public: resolution
NsResolution resolve(const Document& doc, const NamedSelection& sel, const ModelResult& r, int stopAfter) {
  Ctx c(doc, r, stopAfter);
  return resolveIn(c, sel);
}

std::vector<NsResolution> resolveAll(const Document& doc, const ModelResult& r, int stopAfter) {
  std::vector<NsResolution> out;
  if (doc.namedSelections.empty()) return out;
  Ctx c(doc, r, stopAfter);
  for (auto& s : doc.namedSelections) out.push_back(resolveIn(c, s));
  return out;
}

std::map<std::string, std::vector<std::string>> selectionsByFace(const std::vector<NsResolution>& all) {
  std::map<std::string, std::vector<std::string>> m;
  for (auto& r : all)
    for (auto& f : r.faces) m[f].push_back(r.name);
  return m;
}

bool refreshSignatures(Document& doc, const ModelResult& r, const std::vector<NsResolution>& all) {
  bool changed = false;
  for (auto& res : all) {
    if (res.rolledBack) continue;
    NamedSelection* s = nullptr;
    for (auto& x : doc.namedSelections)
      if (x.id == res.id) s = &x;
    if (!s) continue;
    for (auto& mr : res.members) {
      if (mr.index >= s->members.size() || mr.status != NsStatus::Ok) continue;
      auto& m = s->members[mr.index];
      if (m.ref != mr.ref || m.kind != mr.kind) continue;
      NsSignature ns;
      if (m.kind == NsMemberKind::Face && mr.faces.size() == 1) {
        ns = faceSignature(r, mr.faces[0]);
        // positional piece refs follow their piece's current number (non-positional refs are never rewritten)
        if (pieceOf(m.ref).piece && pieceOf(mr.faces[0]).piece && m.ref != mr.faces[0]) {
          m.ref = mr.faces[0];
          changed = true;
        }
      } else if (m.kind == NsMemberKind::Edge && mr.edges.size() == 1) {
        ns = edgeSignature(r, mr.edges[0]);
      } else if (m.kind == NsMemberKind::Body && mr.bodies.size() == 1) {
        ns = bodySignature(r, mr.bodies[0]);
      }
      if (!ns.valid) continue;
      const auto& o = m.sig;
      bool same = o.valid && o.surface == ns.surface && o.nx == ns.nx && o.ny == ns.ny && o.nz == ns.nz && o.cx == ns.cx &&
                  o.cy == ns.cy && o.cz == ns.cz && o.size == ns.size && o.radius == ns.radius &&
                  o.neighbours == ns.neighbours && o.body == ns.body;
      if (!same) {
        m.sig = ns;
        changed = true;
      }
    }
  }
  return changed;
}

// ================================================================== editing
namespace ns {
namespace {
const char* kPalette[] = {"#E8A33D", "#3D8FE8", "#4CB050", "#D9534F", "#8E6AC8", "#2BB3B3", "#E86AA6", "#8C7A5B"};

NamedSelection* findSel(Document& doc, const std::string& name, std::string* err) {
  NamedSelection* s = doc.namedSelection(name);
  if (!s && err) *err = "adlandırılmış seçim bulunamadı '" + name + "'";
  return s;
}
bool captureAll(const ModelResult& r, const std::vector<Ref>& refs, std::vector<NamedSelectionMember>& out,
                std::string* err) {
  for (auto& rf : refs) {
    NamedSelectionMember m;
    if (!captureMember(r, rf.kind, rf.name, m, err)) return false;
    bool dup = false;
    for (auto& x : out) dup |= x.kind == m.kind && x.ref == m.ref;
    if (!dup) out.push_back(std::move(m));
  }
  return true;
}
} // namespace

bool create(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r, int stopAfter,
            std::string* err, std::string* newId, const std::string& color, const std::string& description) {
  const std::string nm = trim(name);
  std::string why;
  if (!isValidSelectionName(nm, &why)) { if (err) *err = "geçersiz ad: " + why; return false; }
  if (doc.namedSelection(nm)) { if (err) *err = "'" + nm + "' adlı seçim zaten var"; return false; }
  if (!color.empty() && !isValidColor(color)) { if (err) *err = "geçersiz renk '" + color + "' (#RRGGBB)"; return false; }
  NamedSelection s;
  s.id = doc.uniqueNamedSelectionId();
  s.name = nm;
  s.color = color.empty() ? kPalette[doc.namedSelections.size() % 8] : color;
  s.description = description;
  if (!captureAll(r, refs, s.members, err)) return false;
  int n = (int)doc.features.size();
  int idx = stopAfter >= 0 ? std::min(stopAfter, n - 1) : n - 1;
  if (idx >= 0) {
    s.createdAt = idx;
    s.createdAfter = doc.features[idx].id;
  }
  if (newId) *newId = s.id;
  doc.namedSelections.push_back(std::move(s));
  return true;
}

bool rename(Document& doc, const std::string& name, const std::string& newName, std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  const std::string nm = trim(newName);
  std::string why;
  if (!isValidSelectionName(nm, &why)) { if (err) *err = "geçersiz ad: " + why; return false; }
  const NamedSelection* o = doc.namedSelection(nm);
  if (o && o != s) { if (err) *err = "'" + nm + "' adlı seçim zaten var"; return false; }
  s->name = nm;
  return true;
}

bool remove(Document& doc, const std::string& name, std::string* err) {
  for (auto it = doc.namedSelections.begin(); it != doc.namedSelections.end(); ++it)
    if (sameSelectionName(it->name, name)) {
      doc.namedSelections.erase(it);
      return true;
    }
  if (err) *err = "adlandırılmış seçim bulunamadı '" + name + "'";
  return false;
}

bool addMembers(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r, int stopAfter,
                std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  NsResolution cur = resolve(doc, *s, r, stopAfter);
  std::set<std::string> fs(cur.faces.begin(), cur.faces.end()), es(cur.edges.begin(), cur.edges.end());
  std::set<std::string> bs;
  for (auto& m : cur.members)
    if (m.kind == NsMemberKind::Body) bs.insert(m.bodies.begin(), m.bodies.end());
  std::vector<Ref> todo;
  for (auto& rf : refs) {
    bool covered = rf.kind == NsMemberKind::Face ? fs.count(rf.name) > 0
                 : rf.kind == NsMemberKind::Edge ? es.count(rf.name) > 0
                                                 : bs.count(rf.name) > 0;
    if (!covered) todo.push_back(rf);
  }
  std::vector<NamedSelectionMember> add = s->members;
  if (!captureAll(r, todo, add, err)) return false;
  s->members = std::move(add);
  return true;
}

bool removeMembers(Document& doc, const std::string& name, const std::vector<Ref>& refs, const ModelResult& r,
                   int stopAfter, std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  NsResolution cur = resolve(doc, *s, r, stopAfter);
  std::set<std::string> rmF, rmE, rmB, rmRef;
  for (auto& rf : refs) {
    (rf.kind == NsMemberKind::Face ? rmF : rf.kind == NsMemberKind::Edge ? rmE : rmB).insert(rf.name);
    rmRef.insert(rf.name);
  }
  std::vector<NamedSelectionMember> out;
  for (size_t i = 0; i < s->members.size(); ++i) {
    const auto& m = s->members[i];
    const NsMemberResult& mr = cur.members[i];
    if (rmRef.count(m.ref)) continue;  // removed by its stored reference (also lost members)
    const std::vector<std::string>& got = m.kind == NsMemberKind::Edge ? mr.edges
                                        : m.kind == NsMemberKind::Body ? mr.bodies : mr.faces;
    const std::set<std::string>& rm = m.kind == NsMemberKind::Edge ? rmE : m.kind == NsMemberKind::Body ? rmB : rmF;
    std::vector<std::string> keep;
    for (auto& g : got)
      if (!rm.count(g)) keep.push_back(g);
    if (keep.size() == got.size()) { out.push_back(m); continue; }
    if (keep.empty()) continue;
    std::vector<Ref> kr;
    for (auto& k : keep) kr.push_back({m.kind, k});
    if (!captureAll(r, kr, out, err)) return false;
  }
  s->members = std::move(out);
  return true;
}

bool rebindMember(Document& doc, const std::string& name, size_t index, const Ref& ref, const ModelResult& r,
                  std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  if (index >= s->members.size()) { if (err) *err = "üye indeksi geçersiz"; return false; }
  NamedSelectionMember m;
  if (!captureMember(r, ref.kind, ref.name, m, err)) return false;
  s->members[index] = std::move(m);
  return true;
}

bool setColor(Document& doc, const std::string& name, const std::string& color, std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  if (!isValidColor(color)) { if (err) *err = "geçersiz renk '" + color + "' (#RRGGBB)"; return false; }
  s->color = color;
  return true;
}
bool setDescription(Document& doc, const std::string& name, const std::string& text, std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  s->description = text;
  return true;
}
bool setExportStep(Document& doc, const std::string& name, bool on, std::string* err) {
  NamedSelection* s = findSel(doc, name, err);
  if (!s) return false;
  s->exportStep = on;
  return true;
}
bool move(Document& doc, const std::string& name, size_t index, std::string* err) {
  auto& v = doc.namedSelections;
  for (size_t i = 0; i < v.size(); ++i)
    if (sameSelectionName(v[i].name, name)) {
      NamedSelection s = v[i];
      v.erase(v.begin() + i);
      v.insert(v.begin() + std::min(index, v.size()), std::move(s));
      return true;
    }
  if (err) *err = "adlandırılmış seçim bulunamadı '" + name + "'";
  return false;
}
int renameBodyRefs(Document& doc, const std::string& from, const std::string& to) {
  auto map = [&](std::string& n) {
    if (n == from) { n = to; return true; }
    if (n.size() > from.size() && n.compare(0, from.size() + 1, from + ":") == 0) { n = to + n.substr(from.size()); return true; }
    return false;
  };
  int cnt = 0;
  for (auto& s : doc.namedSelections)
    for (auto& m : s.members) {
      if (m.kind == NsMemberKind::Body && map(m.ref)) ++cnt;
      map(m.sig.body);
    }
  return cnt;
}

std::string suggestName(const Document& doc, const std::string& base) {
  for (int k = 1;; ++k) {
    std::string n = base + std::to_string(k);
    if (!doc.namedSelection(n)) return n;
  }
}
} // namespace ns

// ================================================================== manager
int NamedSelectionManager::subscribe(Listener l) {
  int t = next_++;
  listeners_[t] = std::move(l);
  return t;
}
void NamedSelectionManager::unsubscribe(int token) { listeners_.erase(token); }
void NamedSelectionManager::clear() {
  res_.clear();
  prev_.clear();
  order_.clear();
  byFace_.clear();
  primed_ = false;
}

const std::vector<NsResolution>& NamedSelectionManager::update(const Document& doc, const ModelResult& r, int stopAfter) {
  res_ = resolveAll(doc, r, stopAfter);
  byFace_ = selectionsByFace(res_);
  std::vector<NsChange> ev;
  std::map<std::string, Snap> now;
  order_.clear();
  for (size_t i = 0; i < doc.namedSelections.size(); ++i) {
    const auto& s = doc.namedSelections[i];
    const auto& rs = res_[i];
    Snap sn;
    sn.name = s.name;
    for (auto& m : s.members) sn.members += std::string(toString(m.kind)) + ":" + m.ref + "\n";
    sn.props = s.color + "\n" + s.description + "\n" + (s.exportStep ? "1" : "0");
    sn.status = rs.status;
    sn.faces = rs.faces;
    order_.push_back(s.id);
    auto it = prev_.find(s.id);
    if (it == prev_.end()) {
      NsChange c;
      c.kind = NsChange::Kind::Added;
      c.id = s.id;
      c.name = s.name;
      c.newStatus = rs.status;
      c.newFaces = rs.faces;
      c.diagnostic = rs.diagnostic;
      ev.push_back(c);
    } else {
      const Snap& p = it->second;
      auto mk = [&](NsChange::Kind k) {
        NsChange c;
        c.kind = k;
        c.id = s.id;
        c.name = s.name;
        c.oldName = p.name;
        c.oldStatus = p.status;
        c.newStatus = rs.status;
        c.oldFaces = p.faces;
        c.newFaces = rs.faces;
        c.diagnostic = rs.diagnostic;
        ev.push_back(c);
      };
      if (p.name != sn.name) mk(NsChange::Kind::Renamed);
      if (p.members != sn.members) mk(NsChange::Kind::MembersChanged);
      if (p.props != sn.props) mk(NsChange::Kind::PropertiesChanged);
      if (p.status != sn.status) mk(NsChange::Kind::StatusChanged);
      if (p.faces != sn.faces) mk(NsChange::Kind::FacesChanged);
    }
    now[s.id] = std::move(sn);
  }
  for (auto& [id, p] : prev_)
    if (!now.count(id)) {
      NsChange c;
      c.kind = NsChange::Kind::Removed;
      c.id = id;
      c.name = c.oldName = p.name;
      c.oldStatus = p.status;
      c.oldFaces = p.faces;
      ev.push_back(c);
    }
  prev_ = std::move(now);
  primed_ = true;
  auto ls = listeners_;  // listeners may unsubscribe while notified
  for (auto& e : ev)
    for (auto& [t, l] : ls)
      if (l) l(e);
  return res_;
}

const NsResolution* NamedSelectionManager::find(const std::string& nameOrId) const {
  for (auto& r : res_)
    if (r.id == nameOrId) return &r;
  for (auto& r : res_)
    if (sameSelectionName(r.name, nameOrId)) return &r;
  return nullptr;
}
std::vector<std::string> NamedSelectionManager::names() const {
  std::vector<std::string> v;
  for (auto& r : res_) v.push_back(r.name);
  return v;
}
std::vector<std::string> NamedSelectionManager::selectionsOfFace(const std::string& faceName) const {
  auto it = byFace_.find(faceName);
  return it == byFace_.end() ? std::vector<std::string>{} : it->second;
}
std::vector<std::string> NamedSelectionManager::selectionsOfFace(const ModelResult& r, const TopoDS_Face& f) const {
  return selectionsOfFace(r.nameOf(f));
}

// ================================================================== STEP export / import with names
std::string stepName(const std::string& name, bool ascii) {
  if (!ascii) return name;
  static const std::map<std::string, char> tr = {
      {"\xC3\xA7", 'c'}, {"\xC3\x87", 'C'}, {"\xC4\x9F", 'g'}, {"\xC4\x9E", 'G'}, {"\xC4\xB1", 'i'}, {"\xC4\xB0", 'I'},
      {"\xC3\xB6", 'o'}, {"\xC3\x96", 'O'}, {"\xC5\x9F", 's'}, {"\xC5\x9E", 'S'}, {"\xC3\xBC", 'u'}, {"\xC3\x9C", 'U'},
      {"\xC3\xA2", 'a'}, {"\xC3\x82", 'A'}, {"\xC3\xAE", 'i'}, {"\xC3\x8E", 'I'}, {"\xC3\xBB", 'u'}, {"\xC3\x9B", 'U'}};
  std::string out;
  for (size_t i = 0; i < name.size();) {
    unsigned char ch = (unsigned char)name[i];
    if (ch < 0x80) {
      out += (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.') ? (char)ch : '_';
      ++i;
      continue;
    }
    size_t len = (ch >> 5) == 0x6 ? 2 : (ch >> 4) == 0xE ? 3 : (ch >> 3) == 0x1E ? 4 : 1;
    auto it = tr.find(name.substr(i, len));
    out += it != tr.end() ? it->second : '_';
    i += len;
  }
  return out.empty() ? "_" : out;
}

namespace {
TCollection_ExtendedString ext(const std::string& s) { return TCollection_ExtendedString(s.c_str(), Standard_True); }
std::string utf8(const TCollection_ExtendedString& e) {
  std::string s(e.LengthOfCString() + 1, '\0');
  Standard_PCharacter p = s.data();
  int n = e.ToUTF8CString(p);
  s.resize(n);
  return s;
}
bool parseColor(const std::string& c, Quantity_Color& q) {
  if (!isValidColor(c)) return false;
  int v = std::stoi(c.substr(1), nullptr, 16);
  q = Quantity_Color(((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0, Quantity_TOC_sRGB);
  return true;
}
// Silences OCCT's transfer statistics on stdout (the runner prints JSON there) for the scope.
struct QuietOcct {
  std::vector<std::pair<Handle(Message_Printer), Message_Gravity>> saved;
  QuietOcct() {
    const Message_SequenceOfPrinters& ps = Message::DefaultMessenger()->Printers();
    for (int i = 1; i <= ps.Length(); ++i) {
      saved.emplace_back(ps(i), ps(i)->GetTraceLevel());
      ps(i)->SetTraceLevel(Message_Fail);
    }
  }
  ~QuietOcct() {
    for (auto& [p, g] : saved) p->SetTraceLevel(g);
  }
};
std::string labelName(const TDF_Label& L) {
  Handle(TDataStd_Name) n;
  if (L.FindAttribute(TDataStd_Name::GetID(), n)) return utf8(n->Get());
  return {};
}
} // namespace

bool exportSTEPNamed(const Document& doc, const ModelResult& r, const std::string& path, const StepExportOptions& opt,
                     std::string* err) {
  if (r.shape.IsNull() || r.bodies.empty()) {
    if (err) *err = "Boş şekil dışa aktarılamaz";
    return false;
  }
  try {
    // 1) names per face / edge / body
    std::map<std::string, std::string> faceLabel, edgeLabel, bodyLabel, faceColor;
    std::set<std::string> used;
    std::map<std::string, std::string> stepOfSel;  // selection name -> unique STEP name
    auto uniq = [&](const std::string& n) {
      std::string b = stepName(n, opt.asciiNames), s = b;
      for (int k = 2; used.count(s); ++k) s = b + "_" + std::to_string(k);
      used.insert(s);
      return s;
    };
    if (opt.namedSelections) {
      auto all = resolveAll(doc, r, -1);
      for (size_t i = 0; i < all.size(); ++i) {
        const NamedSelection& s = doc.namedSelections[i];
        if (!s.exportStep || (all[i].faces.empty() && all[i].edges.empty() && all[i].bodies.empty())) continue;
        const std::string sn = uniq(s.name);
        for (auto& mr : all[i].members) {
          if (!isResolved(mr.status)) continue;
          if (mr.kind == NsMemberKind::Body) {
            for (auto& b : mr.bodies) bodyLabel.emplace(b, sn);
            continue;
          }
          for (auto& f : mr.faces)
            if (faceLabel.emplace(f, sn).second) faceColor[f] = s.color;
          for (auto& e : mr.edges) edgeLabel.emplace(e, sn);
        }
      }
    }
    const ViewColorMap viewColors = opt.colors ? resolveViewColors(doc, r, -1) : ViewColorMap{};   // SC-COLOR
    if (opt.persistentFaceNames)
      for (auto& [n, f] : r.faces) faceLabel.emplace(n, opt.asciiNames ? stepName(n, true) : n);
    // 2) XCAF document
    Handle(TDocStd_Document) xd;
    XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", xd);
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(xd->Main());
    Handle(XCAFDoc_ColorTool) ct = XCAFDoc_DocumentTool::ColorTool(xd->Main());
    naming::NameMap fname, ename;
    for (auto& [n, f] : r.faces) fname.Bind(f, n);
    for (auto& [n, e] : r.edges) ename.Bind(e, n);
    int nb = 0;
    for (const auto& b : r.bodies) {
      if (!opt.bodies.empty() && std::find(opt.bodies.begin(), opt.bodies.end(), b.name) == opt.bodies.end()) continue;
      ++nb;
      TDF_Label L = st->AddShape(b.shape, Standard_False);
      if (L.IsNull()) continue;
      auto bl = bodyLabel.find(b.name);
      if (bl != bodyLabel.end()) TDataStd_Name::Set(L, ext(bl->second));
      else if (opt.bodyNames) TDataStd_Name::Set(L, ext(stepName(b.name, opt.asciiNames)));
      if (opt.colors) {   // SC-COLOR: body colour on the solid itself as well (importers that ignore face styles)
        auto bc = viewColors.body.find(b.name);
        Quantity_Color bq;
        if (bc != viewColors.body.end() && parseColor(bc->second, bq)) ct->SetColor(L, bq, XCAFDoc_ColorSurf);
      }
      TopTools_IndexedMapOfShape seen;
      for (TopExp_Explorer ex(b.shape, TopAbs_FACE); ex.More(); ex.Next()) {
        if (seen.Contains(ex.Current())) continue;
        seen.Add(ex.Current());
        const std::string* n = fname.Seek(ex.Current());
        if (!n) continue;
        auto it = faceLabel.find(*n);
        // SC-COLOR: the user's view colour (face / body colour, ViewState::colors) wins over the selection colour
        std::string hex;
        if (opt.colors) {
          auto vi = viewColors.face.find(*n);
          if (vi != viewColors.face.end()) hex = vi->second;
          else if (it != faceLabel.end() && faceColor.count(*n)) hex = faceColor[*n];
        }
        if (it == faceLabel.end() && hex.empty()) continue;
        TDF_Label SL = st->AddSubShape(L, ex.Current());
        if (SL.IsNull()) continue;
        if (it != faceLabel.end()) TDataStd_Name::Set(SL, ext(it->second));
        Quantity_Color q;
        if (!hex.empty() && parseColor(hex, q)) ct->SetColor(SL, q, XCAFDoc_ColorSurf);
      }
      if (!edgeLabel.empty())
        for (TopExp_Explorer ex(b.shape, TopAbs_EDGE); ex.More(); ex.Next()) {
          if (seen.Contains(ex.Current())) continue;
          seen.Add(ex.Current());
          const std::string* n = ename.Seek(ex.Current());
          if (!n) continue;
          auto it = edgeLabel.find(*n);
          if (it == edgeLabel.end()) continue;
          TDF_Label SL = st->AddSubShape(L, ex.Current());
          if (!SL.IsNull()) TDataStd_Name::Set(SL, ext(it->second));
        }
    }
    if (nb == 0) {
      if (err) *err = "Dışa aktarılacak gövde yok";
      XCAFApp_Application::GetApplication()->Close(xd);
      return false;
    }
    // 3) write
    QuietOcct quiet;
    STEPCAFControl_Writer w;   // constructing the writer registers the XDE static parameters set below
    Interface_Static::SetCVal("write.step.unit", "MM");
    Interface_Static::SetIVal("write.stepcaf.subshapes.name", 1);
    w.SetNameMode(Standard_True);
    w.SetColorMode(opt.colors ? Standard_True : Standard_False);
    bool ok = w.Transfer(xd, STEPControl_AsIs) == Standard_True;
    if (ok) ok = w.Write(path.c_str()) == IFSelect_RetDone;
    XCAFApp_Application::GetApplication()->Close(xd);
    if (!ok && err) *err = "STEP dosyası yazılamadı: " + path;
    return ok;
  } catch (const Standard_Failure& e) {
    if (err) *err = std::string("STEP hatası: ") + (e.GetMessageString() ? e.GetMessageString() : "");
  } catch (const std::exception& e) {
    if (err) *err = std::string("STEP hatası: ") + e.what();
  } catch (...) {
    if (err) *err = "STEP hatası";
  }
  return false;
}

bool readSTEPNames(const std::string& path, StepNames& out, std::string* err) {
  out = StepNames{};
  try {
    Handle(TDocStd_Document) xd;
    XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", xd);
    QuietOcct quiet;
    STEPCAFControl_Reader rd;
    Interface_Static::SetIVal("read.stepcaf.subshapes.name", 1);
    rd.SetNameMode(Standard_True);
    if (rd.ReadFile(path.c_str()) != IFSelect_RetDone || !rd.Transfer(xd)) {
      if (err) *err = "STEP okunamadı: " + path;
      XCAFApp_Application::GetApplication()->Close(xd);
      return false;
    }
    Handle(XCAFDoc_ShapeTool) st = XCAFDoc_DocumentTool::ShapeTool(xd->Main());
    TDF_LabelSequence fr;
    st->GetFreeShapes(fr);
    BRep_Builder bb;
    TopoDS_Compound comp;
    bb.MakeCompound(comp);
    std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& L) {
      TDF_LabelSequence subs;
      st->GetSubShapes(L, subs);
      for (int i = 1; i <= subs.Length(); ++i) {
        TopoDS_Shape s = XCAFDoc_ShapeTool::GetShape(subs(i));
        std::string n = labelName(subs(i));
        if (!s.IsNull() && s.ShapeType() == TopAbs_FACE && !n.empty()) out.faces.push_back({n, TopoDS::Face(s)});
      }
      if (XCAFDoc_ShapeTool::IsAssembly(L)) {
        TDF_LabelSequence comps;
        XCAFDoc_ShapeTool::GetComponents(L, comps);
        for (int i = 1; i <= comps.Length(); ++i) {
          TDF_Label ref;
          if (XCAFDoc_ShapeTool::GetReferredShape(comps(i), ref)) {
            out.bodies.push_back(labelName(ref).empty() ? labelName(comps(i)) : labelName(ref));
            walk(ref);
          }
        }
      }
    };
    for (int i = 1; i <= fr.Length(); ++i) {
      bb.Add(comp, XCAFDoc_ShapeTool::GetShape(fr(i)));
      if (!XCAFDoc_ShapeTool::IsAssembly(fr(i))) out.bodies.push_back(labelName(fr(i)));
      walk(fr(i));
    }
    out.shape = comp;
    XCAFApp_Application::GetApplication()->Close(xd);
    return true;
  } catch (const Standard_Failure& e) {
    if (err) *err = std::string("STEP hatası: ") + (e.GetMessageString() ? e.GetMessageString() : "");
  } catch (...) {
    if (err) *err = "STEP hatası";
  }
  return false;
}

} // namespace pc::model
