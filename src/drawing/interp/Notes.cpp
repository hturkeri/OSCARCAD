// R3 — notes / caption parser (see Notes.h). Regex over an OCR-tolerant canonical form; numbers via DimGrammar.
#include "interp/Notes.h"
#include "interp/InterpUtil.h"
#include <algorithm>
#include <cstring>
#include <regex>

namespace pc::drawing::interp {

namespace {

bool isD(char c) { return c >= '0' && c <= '9'; }
bool isA(char c) { return c >= 'A' && c <= 'Z'; }

// keyword with OCR confusions: I~L~1~|, O~0~Q, S~5, B~8, Z~2
std::string kw(const std::string& w) {
  std::string r;
  for (char c : w) {
    switch (c) {
      case 'I': r += "[IL1|]"; break;
      case 'L': r += "[LI1|]"; break;
      case 'O': r += "[O0Q]"; break;
      case 'S': r += "[S5]"; break;
      case 'B': r += "[B8]"; break;
      default: r += c;
    }
  }
  return r;
}

double num(const std::string& s) {
  std::string v = s;
  std::replace(v.begin(), v.end(), ',', '.');
  try { return std::stod(v); } catch (...) { return kNaN; }
}

// Number token through the WP-2 grammar (OCR repairs); appends corrections.
double gnum(const std::string& tok, std::vector<std::string>& corr, bool angular = false) {
  DimTextContext ctx;
  ctx.fromOcr = true;
  ctx.angular = angular;
  DimText d = parseDim(tok, ctx);
  if (d.valid && d.nominal > 0) {
    for (auto& c : d.corrections) corr.push_back(c);
    return d.nominal;
  }
  return num(tok);
}

const char* kNum = R"((\d+(?:\.\d+)?))";

std::string kindOf(const std::string& w) {
  std::string u = w;
  if (u.find('G') == 0) return "groove";
  if (u.find('H') == 0) return "hole";
  return "slot";   // SLOT(S), PASSAGE(S) (flow passages are the slots)
}

}  // namespace

std::string canonNote(const std::string& in, std::vector<std::string>* corr) {
  static const std::pair<const char*, const char*> sym[] = {
      {"\xC3\x98", "@"}, {"\xC3\xB8", "@"}, {"\xE2\x8C\x80", "@"}, {"\xC2\xB0", "~"}, {"\xC2\xBA", "~"},
      {"\xC3\x97", "X"}, {"\xE2\x80\xA6", ".."}, {"\xE2\x80\x93", "-"}, {"\xE2\x80\x94", "-"}, {"\xC2\xB1", "+-"}};
  std::string s;
  for (size_t i = 0; i < in.size();) {
    bool done = false;
    if ((unsigned char)in[i] >= 0x80) {
      for (auto& [k, v] : sym)
        if (in.compare(i, std::strlen(k), k) == 0) { s += v; i += std::strlen(k); done = true; break; }
      if (!done) {   // Turkish fold for the rest, else drop
        size_t j = i + 1;
        while (j < in.size() && ((unsigned char)in[j] & 0xC0) == 0x80) ++j;
        std::string f = foldUpper(in.substr(i, j - i));
        s += (f.size() == 1 && (unsigned char)f[0] < 0x80) ? f : " ";
        i = j;
      }
      continue;
    }
    s += (char)std::toupper((unsigned char)in[i]);
    ++i;
  }
  auto note = [&](const std::string& c) { if (corr) corr->push_back(c); };
  // OCR repairs inside numbers: O->0, I/L/|->1, S->5, B->8 when glued to digits and not inside a word
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    char p = i ? s[i - 1] : ' ', n = i + 1 < s.size() ? s[i + 1] : ' ';
    char map = c == 'O' || c == 'Q' ? '0' : (c == 'I' || c == 'L' || c == '|') ? '1' : c == 'S' ? '5' : c == 'B' ? '8' : 0;
    if (!map) continue;
    bool digitNeighbour = isD(p) || isD(n);
    bool letterNeighbour = isA(p) || isA(n);
    // "45O -" (degree read as O) is left for the EQ.SP. rule; "10O" / "1O.5" / "l0" are numbers
    if (digitNeighbour && !letterNeighbour && !(c == 'O' && isD(p) && !isD(n) && n != '.')) {
      note(std::string(1, c) + "->" + map + " (sayı içinde)");
      s[i] = map;
    }
  }
  // digits inside words: SL0TS -> SLOTS, SW1RL -> SWIRL (letter on both sides)
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (!isA(s[i - 1]) || !isA(s[i + 1])) continue;
    char map = s[i] == '0' ? 'O' : s[i] == '1' ? 'I' : s[i] == '5' ? 'S' : s[i] == '8' ? 'B' : 0;
    if (map) { note(std::string(1, s[i]) + "->" + map + " (kelime içinde)"); s[i] = map; }
  }
  // decimal comma, split range dots, spaced ':' kept
  s = std::regex_replace(s, std::regex(R"((\d),(\d))"), "$1.$2");
  s = std::regex_replace(s, std::regex(R"(\.\s+\.)"), "..");
  s = std::regex_replace(s, std::regex(R"(\.{4,})"), "..");
  return s;
}

void mergeHint(std::vector<FeatureHint>& out, const FeatureHint& in) {
  FeatureHint* t = nullptr;
  for (auto& f : out)
    if (f.kind == in.kind && f.role == in.role) { t = &f; break; }
  if (!t) {   // role-less into the only family of that kind (or a role-less family into a role)
    std::vector<FeatureHint*> same;
    for (auto& f : out) if (f.kind == in.kind && (f.role.empty() || in.role.empty())) same.push_back(&f);
    int ofKind = 0;
    for (auto& f : out) ofKind += f.kind == in.kind;
    if (same.size() == 1 && ofKind == 1) t = same[0];
  }
  if (!t) { out.push_back(in); return; }
  if (t->role.empty()) t->role = in.role;
  if (t->viewLetter.empty()) t->viewLetter = in.viewLetter;
  auto take = [](double& a, double b) { if (!has(a) && has(b)) a = b; };
  if (!t->count) t->count = in.count;
  take(t->width, in.width); take(t->height, in.height); take(t->pitchDeg, in.pitchDeg);
  take(t->offset, in.offset); take(t->x0, in.x0); take(t->x1, in.x1);
  if (!t->sense) t->sense = in.sense;
  t->tangential |= in.tangential;
  t->cut |= in.cut;
  t->conf = std::max(t->conf, in.conf);
  t->textIds.insert(t->textIds.end(), in.textIds.begin(), in.textIds.end());
  t->corrections.insert(t->corrections.end(), in.corrections.begin(), in.corrections.end());
}

void parseNoteText(const std::string& utf8, int textId, float textConf, NoteHints& out) {
  std::vector<std::string> corr;
  std::string s = canonNote(utf8, &corr);
  std::smatch m;
  const std::string N = kNum;
  // ---- Ø read as 0/O in front of a diameter word: "014 THROAT" -> "@14 THROAT" (grammar, circle context)
  {
    static const std::regex re(R"((^|[\s(,:])[0O](\d+(?:\.\d+)?)(\s+(?:THROAT|HUB|BORE|ORIFICE|CHAMBER|INLET|EXIT|ROOT|OD|O\.D)))");
    std::string r;
    auto b = s.cbegin();
    while (std::regex_search(b, s.cend(), m, re)) {
      DimTextContext ctx;
      ctx.fromOcr = true;
      ctx.nearCircleOrCylinder = true;
      DimText d = parseDim("0" + m[2].str(), ctx);
      r.append(b, m[0].first);
      if ((d.valid && d.kind == DimKind::Diameter) || m[2].str().find('.') == std::string::npos) {   // "014" is no length
        r += m[1].str() + "@" + m[2].str() + m[3].str();
        corr.push_back("0->Ø (çap kelimesi)");
      } else r += m[0].str();
      b = m[0].second;
    }
    r.append(b, s.cend());
    s = r;
  }
  // count token garbled by OCR in front of a feature word: "lO SECONDARY SLOTS" -> "10 SECONDARY SLOTS"
  {
    static const std::regex re("(^|[\\s(:;,])([0-9OQIL|SB]{1,3})(\\s*X?\\s+(?:" + kw("TANGENTIAL") + "\\s+)?(?:PRIMARY\\s+|SECONDARY\\s+)?(?:" +
                               kw("TANGENTIAL") + "\\s+)?(?:" + kw("SLOTS") + "?|" + kw("GROOVES") + "?|PA" + kw("SSAGES") + "?)\\b)");
    std::string r;
    auto b = s.cbegin();
    while (std::regex_search(b, s.cend(), m, re)) {
      std::string tok = m[2], fixed;
      for (char c : tok) fixed += c == 'O' || c == 'Q' ? '0' : (c == 'I' || c == 'L' || c == '|') ? '1' : c == 'S' ? '5' : c == 'B' ? '8' : c;
      r.append(b, m[0].first);
      r += m[1].str() + fixed + m[3].str();
      if (fixed != tok) corr.push_back(tok + "->" + fixed + " (adet)");
      b = m[0].second;
    }
    r.append(b, s.cend());
    s = r;
  }
  float conf = textConf;
  // ---- global sentences
  {
    static const std::regex re(R"(SCALE\s*:?\s*(\d+(?:\.\d+)?)\s*:\s*(\d+(?:\.\d+)?))");
    if (std::regex_search(s, m, re)) { out.scaleNum = num(m[1]); out.scaleDen = num(m[2]); }
  }
  {
    static const std::regex re(R"(FL[O0]W\s+(LEFT\s+T[O0]\s+RIGHT|RIGHT\s+T[O0]\s+LEFT))");
    if (std::regex_search(s, m, re)) out.flowSign = m[1].str()[0] == 'L' ? 1 : -1;
  }
  if (std::regex_search(s, std::regex(R"(VIEWED\s+FR[O0]M\s+D[O0]WNSTREAM)"))) out.viewedFromDownstream = true;
  {
    static const std::regex re(R"(MODELL?ING\s*:?\s*(.*))");
    if (std::regex_search(s, m, re)) {
      std::string body = m[1];
      static const std::regex step(R"(\(([A-H])\)\s*([^;(]*))");
      for (auto it = std::sregex_iterator(body.begin(), body.end(), step); it != std::sregex_iterator(); ++it)
        out.modellingSteps.push_back(trim(std::regex_replace((*it)[2].str(), std::regex(R"([\s.;]+$)"), "")));
    }
  }
  // view captions
  {
    static const std::regex re(R"(\b(SECTI[O0]N|SECT\.|VIEW)\s+([A-Z])(?:\s*-\s*([A-Z]))?\b\s*-?\s*(.*))");
    if (std::regex_search(s, m, re) && (m[3].matched ? m[3].str() == m[2].str() : true) && m.position(0) < 4) {
      ViewCaption c;
      c.letter = m[2];
      c.section = m[1].str()[0] == 'S';
      c.subject = trim(m[4]);
      if (c.subject.find("PRIMARY") != std::string::npos) c.role = "primary";
      if (c.subject.find("SECONDARY") != std::string::npos) c.role = "secondary";
      std::smatch pm;
      if (std::regex_search(c.subject, pm, std::regex(R"(\((\d+)\))"))) c.partNo = std::stoi(pm[1]);
      c.frontFace = c.subject.find("FRONT FACE") != std::string::npos;
      c.rearFace = c.subject.find("REAR FACE") != std::string::npos || c.subject.find("BACK FACE") != std::string::npos;
      c.textId = textId;
      out.captions.push_back(c);
    }
  }
  // named diameters
  {
    static const std::regex fwd(R"(@(\d+(?:\.\d+)?)\s+([A-Z][A-Z.]*(?:\s+[A-Z][A-Z.]*){0,2}))");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), fwd); it != std::sregex_iterator(); ++it) {
      std::string name = trim(std::regex_replace((*it)[2].str(), std::regex(R"(\s+(AT|X|LONG|AND)\b.*$)"), ""));
      name = trim(std::regex_replace(name, std::regex(R"(^(AT|X)\b.*$)"), ""));
      if (!name.empty()) out.namedDiameters.emplace(name, num((*it)[1]));
    }
    static const std::regex rev(R"(([A-Z]{3,}(?:\s+[A-Z]{3,}){0,2})\s*@(\d+(?:\.\d+)?))");
    for (auto it = std::sregex_iterator(s.begin(), s.end(), rev); it != std::sregex_iterator(); ++it) {
      std::string name = trim((*it)[1]);
      name = std::regex_replace(name, std::regex(R"(^(DASHED|VENTURI|AND|THE)\s+)"), "");
      out.namedDiameters.emplace(name, num((*it)[2]));
    }
  }
  // tangency sentence: blanked afterwards (it mentions SLOT)
  {
    static const std::regex re("(" + kw("SLOT") + R"(\s+)?OUTER\s+WALL\s+TANGENT\s+TO\s+([A-Z ]+?)(\s+WALL)?\s*(\.|,|;|$))");
    if (std::regex_search(s, m, re)) {
      out.tangentOuterWallTo = trim(m[2]);
      s.replace(m.position(0), m.length(0), std::string(m.length(0), ' '));
    }
  }
  // offsets: "SLOT CENTRELINE OFFSET FROM AXIS: PRIMARY 8, SECONDARY 11" (or a bare number)
  std::map<std::string, double> offsets;
  {
    static const std::regex re("(" + kw("SLOT") + R"(\s+)?(CENTRE|CENTER)\s*LINE\s+OFFSET[A-Z ]*:?\s*([^.;]*(\.\d+[^.;]*)*))");
    if (std::regex_search(s, m, re)) {
      std::string body = m[3];
      static const std::regex pr(R"((PRIMARY|SECONDARY)\s+(\d+(?:\.\d+)?))");
      for (auto it = std::sregex_iterator(body.begin(), body.end(), pr); it != std::sregex_iterator(); ++it)
        offsets[(*it)[1].str() == "PRIMARY" ? "primary" : "secondary"] = num((*it)[2]);
      std::smatch bm;
      if (offsets.empty() && std::regex_search(body, bm, std::regex(R"(^\s*(\d+(?:\.\d+)?))"))) offsets[""] = num(bm[1]);
      s.replace(m.position(0), m.length(0), std::string(m.length(0), ' '));
    }
  }
  for (auto& [role, v] : offsets) {
    FeatureHint h;
    h.kind = "slot";
    h.role = role;
    h.offset = v;
    h.conf = conf;
    h.textIds = {textId};
    mergeHint(out.features, h);
  }
  // ---- feature phrases
  static const std::regex start("(?:(CUT)\\s+)?(?:(\\d+)\\s*(?:X\\s*)?)?(?:(" + kw("TANGENTIAL") + ")\\s+)?(?:(PRIMARY|SECONDARY)\\s+)?(?:(" +
                                kw("TANGENTIAL") + ")\\s+)?(" + kw("SLOTS") + "?|" + kw("GROOVES") + "?|PA" + kw("SSAGES") + "?|" +
                                kw("HOLES") + "?)\\b");
  std::vector<std::pair<size_t, std::smatch>> starts;
  for (auto it = std::sregex_iterator(s.begin(), s.end(), start); it != std::sregex_iterator(); ++it)
    starts.push_back({(size_t)it->position(0), *it});
  for (size_t k = 0; k < starts.size(); ++k) {
    const std::smatch& sm = starts[k].second;
    size_t a = starts[k].first, b = k + 1 < starts.size() ? starts[k + 1].first : s.size();
    std::string ph = s.substr(a, b - a);
    std::string after = s.substr(a + sm.length(0), b - a - sm.length(0));
    FeatureHint h;
    std::string kwd = sm[6];
    for (auto& c : kwd) c = c == '0' ? 'O' : c == '5' ? 'S' : c == '1' ? 'I' : c;
    h.kind = kindOf(kwd);
    h.cut = sm[1].matched;
    if (sm[2].matched) h.count = std::stoi(sm[2]);
    h.tangential = sm[3].matched || sm[5].matched;
    if (sm[4].matched) h.role = sm[4].str() == "PRIMARY" ? "primary" : "secondary";
    std::vector<std::string> pc = corr;
    std::smatch q;
    static const std::regex size(R"(^[^=]*?(\d+(?:\.\d+)?)\s*X\s*(\d+(?:\.\d+)?))");
    if (std::regex_search(after, q, size) && q.position(0) == 0 && after.find('=') > (size_t)q.position(1)) {
      h.width = gnum(q[1], pc);
      h.height = gnum(q[2], pc);
    }
    static const std::regex pitch(R"(EQ\.?\s*SP\.?\s*(\d+(?:\.\d+)?)\s*(~|O)?)");
    if (std::regex_search(ph, q, pitch)) {
      std::string t = q[1];
      double v = num(t);
      if (!q[2].matched && v >= 360 && t.back() == '0') { v = num(t.substr(0, t.size() - 1)); pc.push_back("0->° (açı)"); }
      if (q[2].matched && q[2].str() == "O") pc.push_back("O->° (açı)");
      h.pitchDeg = v;
    }
    static const std::regex ccw(R"(\b(CCW|C\.C\.W\.?|ANTI-?\s?CLOCKWISE|COUNTER-?\s?CLOCKWISE)\b)");
    static const std::regex cw(R"(\b(CW|C\.W\.?|CLOCKWISE)\b)");
    if (std::regex_search(ph, ccw)) h.sense = 1;
    else if (std::regex_search(ph, cw)) h.sense = -1;
    static const std::regex ax(R"(\bX\s*=\s*(-?\d+(?:\.\d+)?)\s*(?:\.{2,3}|TO|-)\s*(-?\d+(?:\.\d+)?))");
    if (std::regex_search(ph, q, ax)) { h.x0 = num(q[1]); h.x1 = num(q[2]); }
    if (!offsets.empty() && offsets.count(h.role)) h.offset = offsets[h.role];
    h.corrections = pc;
    h.conf = conf;
    for (size_t i = 0; i < pc.size(); ++i) h.conf *= kConfCorrectionPenalty;
    h.textIds = {textId};
    bool informative = h.count || has(h.width) || has(h.pitchDeg) || h.sense || has(h.x0);
    if (informative) mergeHint(out.features, h);
  }
  out.conf = std::max(out.conf, conf);
}

const FeatureHint* findHint(const NoteHints& h, const std::string& kind, const std::string& role) {
  const FeatureHint* any = nullptr;
  int n = 0;
  for (auto& f : h.features) {
    if (f.kind != kind) continue;
    if (f.role == role) return &f;
    if (f.role.empty() || role.empty()) { any = &f; ++n; }
  }
  return n == 1 ? any : nullptr;
}

NoteHints parseNotes(const PrimitiveSheet& sheet) {
  NoteHints out;
  // captions first, so caption sub-lines ("8x TANGENTIAL SLOTS ...") can inherit the caption's role
  struct Item { const TextRun* t; NoteHints h; };
  std::vector<Item> items;
  for (auto& t : sheet.texts) {
    Item it{&t, {}};
    parseNoteText(t.utf8, t.id, t.conf, it.h);
    for (auto& c : it.h.captions) c.pos = textCentre(t);
    items.push_back(std::move(it));
  }
  for (auto& it : items)
    for (auto& c : it.h.captions) out.captions.push_back(c);
  for (auto& it : items) {
    auto& h = it.h;
    if (h.scaleNum > 0 && out.scaleNum == 0) { out.scaleNum = h.scaleNum; out.scaleDen = h.scaleDen; }
    if (h.flowSign && !out.flowSign) out.flowSign = h.flowSign;
    out.viewedFromDownstream |= h.viewedFromDownstream;
    for (auto& [k, v] : h.namedDiameters) out.namedDiameters.emplace(k, v);
    if (out.tangentOuterWallTo.empty()) out.tangentOuterWallTo = h.tangentOuterWallTo;
    for (auto& st : h.modellingSteps) out.modellingSteps.push_back(st);
    out.conf = std::max(out.conf, h.conf);
    // caption sub-line: text right below a caption (within 3 caption heights, horizontally overlapping)
    const ViewCaption* cap = nullptr;
    if (h.captions.empty()) {
      Vec2 c = textCentre(*it.t);
      double best = 1e300;
      for (auto& cp : out.captions) {
        const TextRun* ct = findText(sheet, cp.textId);
        if (!ct) continue;
        Box2 cb = textBox(*ct);
        double dy = cp.pos.y - c.y;
        if (dy <= 0 || dy > 3.5 * std::max(ct->height, it.t->height)) continue;
        if (c.x < cb.lo.x - 0.25 * width(cb) || c.x > cb.hi.x + 0.25 * width(cb)) continue;
        if (dy < best) { best = dy; cap = &cp; }
      }
    }
    for (auto f : h.features) {
      if (cap && f.role.empty()) f.role = cap->role;
      if (cap) f.viewLetter = cap->letter;
      mergeHint(out.features, f);
    }
  }
  return out;
}

}  // namespace pc::drawing::interp
