// Feasibility spike: minimal PDF content-stream vector + text extractor on top of libqpdf (Apache-2.0).
// Usage: pdfvec file.pdf [page(1-based)] [out.json]
// Emits: page box, stroked/filled segments (line / cubic) in PAGE points after CTM, stroke width, dash,
// RGB; text runs (raw bytes + ToUnicode-less fallback), origin, rotation, size, font. Recurses into Form XObjects.
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct M { double a=1,b=0,c=0,d=1,e=0,f=0;
  M mul(const M& o) const { // this * o  (row-vector convention, PDF: p' = p * M)
    return {a*o.a+b*o.c, a*o.b+b*o.d, c*o.a+d*o.c, c*o.b+d*o.d, e*o.a+f*o.c+o.e, e*o.b+f*o.d+o.f}; }
  void ap(double x,double y,double& X,double& Y) const { X=a*x+c*y+e; Y=b*x+d*y+f; }
  double scale() const { return std::sqrt(std::fabs(a*d-b*c)); } };
struct GS { M ctm; double lw=1; std::vector<double> dash; double rgbS[3]={0,0,0}, rgbF[3]={0,0,0};
  // text state
  M tm, tlm; double fs=0, tl=0, tc=0, tw=0, th=1, rise=0; std::string font; };
struct Seg { char kind; double p[8]; double lw; int dashN; double dashOn; bool stroke, fill; float r,g,b; };
struct Txt { std::string raw, font; double x,y,size,rotDeg; };

struct Interp : QPDFObjectHandle::ParserCallbacks {
  std::vector<QPDFObjectHandle> ops; std::vector<GS> st{GS{}}; QPDFObjectHandle res;
  std::vector<Seg> path; double cx=0,cy=0, sx=0,sy=0; // current point (user space)
  std::vector<Seg>* out; std::vector<Txt>* txt; int depth=0; int images=0;
  std::map<std::string,int> opCount;
  GS& g(){ return st.back(); }
  double n(size_t i){ return ops[i].getNumericValue(); }
  void addSeg(char k, std::initializer_list<double> pts){ Seg s{}; s.kind=k; int i=0;
    for(double v:pts) s.p[i++]=v; path.push_back(s); }
  void flush(bool stroke, bool fill){
    for(auto& s: path){ int np = s.kind=='L'?2:4; for(int i=0;i<np;i++) g().ctm.ap(s.p[2*i],s.p[2*i+1],s.p[2*i],s.p[2*i+1]);
      s.lw=g().lw*g().ctm.scale(); s.dashN=(int)g().dash.size(); s.dashOn=s.dashN? g().dash[0]*g().ctm.scale():0;
      s.stroke=stroke; s.fill=fill; const double* c= stroke? g().rgbS: g().rgbF; s.r=c[0]; s.g=c[1]; s.b=c[2];
      out->push_back(s);} path.clear(); }
  void showText(const std::string& bytes){
    GS& s=g(); M trm = M{s.fs*s.th,0,0,s.fs,0,s.rise}.mul(s.tm).mul(s.ctm);
    double x,y; trm.ap(0,0,x,y); double rot = std::atan2(trm.b,trm.a)*180/M_PI;
    txt->push_back({bytes, s.font, x, y, std::hypot(trm.c,trm.d), rot});
    // advance: crude 0.5em per glyph (spike: real impl uses /Widths)
    double adv = bytes.size()*(0.5*s.fs + s.tc)*s.th; s.tm = M{1,0,0,1,adv,0}.mul(s.tm); }
  void handleObject(QPDFObjectHandle h) override {
    if(!h.isOperator()){ ops.push_back(h); return; }
    std::string op=h.getOperatorValue(); opCount[op]++;
    try { run(op); } catch(std::exception& e){ /* tolerate malformed operands */ }
    ops.clear(); }
  void handleEOF() override {}
  void run(const std::string& op){
    if(op=="q") st.push_back(g()); else if(op=="Q"){ if(st.size()>1) st.pop_back(); }
    else if(op=="cm"){ M m{n(0),n(1),n(2),n(3),n(4),n(5)}; g().ctm=m.mul(g().ctm); }
    else if(op=="w") g().lw=n(0);
    else if(op=="d"){ g().dash.clear(); for(auto& v: ops[0].getArrayAsVector()) g().dash.push_back(v.getNumericValue()); }
    else if(op=="RG"){ for(int i=0;i<3;i++) g().rgbS[i]=n(i);} else if(op=="rg"){ for(int i=0;i<3;i++) g().rgbF[i]=n(i);}
    else if(op=="G"){ for(int i=0;i<3;i++) g().rgbS[i]=n(0);} else if(op=="g"){ for(int i=0;i<3;i++) g().rgbF[i]=n(0);}
    else if(op=="m"){ cx=sx=n(0); cy=sy=n(1);}
    else if(op=="l"){ addSeg('L',{cx,cy,n(0),n(1)}); cx=n(0); cy=n(1);}
    else if(op=="c"){ addSeg('C',{cx,cy,n(0),n(1),n(2),n(3),n(4),n(5)}); cx=n(4); cy=n(5);}
    else if(op=="v"){ addSeg('C',{cx,cy,cx,cy,n(0),n(1),n(2),n(3)}); cx=n(2); cy=n(3);}
    else if(op=="y"){ addSeg('C',{cx,cy,n(0),n(1),n(2),n(3),n(2),n(3)}); cx=n(2); cy=n(3);}
    else if(op=="h"){ if(cx!=sx||cy!=sy) addSeg('L',{cx,cy,sx,sy}); cx=sx; cy=sy; }
    else if(op=="re"){ double x=n(0),y=n(1),w=n(2),hh=n(3); addSeg('L',{x,y,x+w,y}); addSeg('L',{x+w,y,x+w,y+hh});
      addSeg('L',{x+w,y+hh,x,y+hh}); addSeg('L',{x,y+hh,x,y}); cx=sx=x; cy=sy=y; }
    else if(op=="S"||op=="s") { if(op=="s") run("h"); flush(true,false);}
    else if(op=="f"||op=="F"||op=="f*") flush(false,true);
    else if(op=="B"||op=="B*"||op=="b"||op=="b*") flush(true,true);
    else if(op=="n") path.clear();
    else if(op=="BT"){ g().tm=g().tlm=M{}; }
    else if(op=="Tf"){ g().font=ops[0].getName(); g().fs=n(1);}
    else if(op=="Tc") g().tc=n(0); else if(op=="Tw") g().tw=n(0); else if(op=="Tz") g().th=n(0)/100; else if(op=="TL") g().tl=n(0);
    else if(op=="Ts") g().rise=n(0);
    else if(op=="Td"){ g().tlm=M{1,0,0,1,n(0),n(1)}.mul(g().tlm); g().tm=g().tlm; }
    else if(op=="TD"){ g().tl=-n(1); run("Td"); }
    else if(op=="Tm"){ g().tlm=g().tm=M{n(0),n(1),n(2),n(3),n(4),n(5)}; }
    else if(op=="T*"){ g().tlm=M{1,0,0,1,0,-g().tl}.mul(g().tlm); g().tm=g().tlm; }
    else if(op=="Tj") showText(ops[0].getStringValue());
    else if(op=="'"){ run("T*"); showText(ops[0].getStringValue()); }
    else if(op=="\""){ g().tw=n(0); g().tc=n(1); run("T*"); showText(ops[2].getStringValue()); }
    else if(op=="TJ"){ std::string s; for(auto& e: ops[0].getArrayAsVector()) if(e.isString()) s+=e.getStringValue(); showText(s);}
    else if(op=="Do"){
      auto xo = res.getKey("/XObject").getKey(ops[0].getName());
      if(!xo.isStream()) return; auto d=xo.getDict(); std::string st2=d.getKey("/Subtype").getName();
      if(st2=="/Image"){ images++; return; }
      if(st2=="/Form" && depth<8){ Interp sub; sub.out=out; sub.txt=txt; sub.depth=depth+1; sub.st={g()};
        if(d.hasKey("/Matrix")){ auto a=d.getKey("/Matrix").getArrayAsVector();
          M m{a[0].getNumericValue(),a[1].getNumericValue(),a[2].getNumericValue(),a[3].getNumericValue(),a[4].getNumericValue(),a[5].getNumericValue()};
          sub.st[0].ctm=m.mul(g().ctm);}
        sub.res = d.hasKey("/Resources")? d.getKey("/Resources"): res;
        xo.parseAsContents(&sub); images+=sub.images; for(auto& kv: sub.opCount) opCount[kv.first]+=kv.second; }
    }
  }
};

static std::string jesc(const std::string& s){ std::string o; for(unsigned char c: s){
  if(c=='"'||c=='\\'){o+='\\';o+=c;} else if(c<0x20||c>=0x7f){ char b[8]; snprintf(b,8,"\\u%04x",c); o+=b;} else o+=c;} return o; }

int main(int argc,char** argv){
  if(argc<2){ fprintf(stderr,"usage: pdfvec file.pdf [page] [out.json]\n"); return 2; }
  QPDF pdf; pdf.processFile(argv[1]); int pageNo = argc>2? atoi(argv[2]):1;
  auto pages = QPDFPageDocumentHelper(pdf).getAllPages(); auto page = pages.at(pageNo-1);
  std::vector<Seg> segs; std::vector<Txt> txt; Interp ip; ip.out=&segs; ip.txt=&txt;
  ip.res = page.getAttribute("/Resources", false);
  page.parseContents(&ip);
  // summary
  std::map<double,int> lw; std::map<std::string,int> kinds; int dashed=0;
  for(auto& s: segs){ kinds[std::string(1,s.kind)+(s.stroke?"S":"")+(s.fill?"F":"")]++; if(s.stroke) lw[std::round(s.lw*1000)/1000]++; if(s.dashN) dashed++; }
  printf("page %d: segments=%zu text_runs=%zu image_xobjects=%d dashed=%d\n", pageNo, segs.size(), txt.size(), ip.images, dashed);
  for(auto& k: kinds) printf("  kind %-4s %d\n", k.first.c_str(), k.second);
  printf("  stroke widths (pt): "); int shown=0; for(auto it=lw.rbegin(); it!=lw.rend() && shown<12; ++it,++shown) printf("%g x%d  ", it->first, it->second); printf("\n");
  printf("  top operators: "); for(auto& o: ip.opCount) if(o.second>200) printf("%s=%d ", o.first.c_str(), o.second); printf("\n");
  if(argc>3){ std::ofstream f(argv[3]); f<<"{\"segments\":["; for(size_t i=0;i<segs.size();i++){ auto& s=segs[i]; int np=s.kind=='L'?4:8;
      f<<(i?",":"")<<"{\"k\":\""<<s.kind<<"\",\"p\":["; for(int j=0;j<np;j++) f<<(j?",":"")<<s.p[j];
      f<<"],\"lw\":"<<s.lw<<",\"dash\":"<<s.dashN<<",\"S\":"<<s.stroke<<",\"F\":"<<s.fill<<"}"; }
    f<<"],\"text\":["; for(size_t i=0;i<txt.size();i++){ auto& t=txt[i];
      f<<(i?",":"")<<"{\"s\":\""<<jesc(t.raw)<<"\",\"font\":\""<<jesc(t.font)<<"\",\"x\":"<<t.x<<",\"y\":"<<t.y<<",\"size\":"<<t.size<<",\"rot\":"<<t.rotDeg<<"}"; }
    f<<"]}\n"; }
  return 0; }
