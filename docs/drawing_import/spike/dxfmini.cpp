// Spike: minimal ASCII DXF group-code reader (no external lib). Prints ENTITIES of interest incl. DIMENSION measured value (42).
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>
static std::string trim(std::string s){ while(!s.empty()&&(s.back()=='\r'||s.back()==' ')) s.pop_back(); size_t i=s.find_first_not_of(' '); return i==std::string::npos?"":s.substr(i); }
int main(int c,char**v){ std::ifstream f(v[1]); std::string a,b; std::vector<std::pair<int,std::string>> g;
  while(std::getline(f,a)&&std::getline(f,b)) g.push_back({std::stoi(trim(a)),trim(b)});
  bool inEnt=false; std::map<std::string,int> cnt;
  for(size_t i=0;i<g.size();i++){
    if(g[i].first==2&&g[i].second=="ENTITIES"&&i&&g[i-1].second=="SECTION") inEnt=true;
    if(g[i].first==0&&g[i].second=="ENDSEC") inEnt=false;
    if(!inEnt||g[i].first!=0) continue;
    std::string t=g[i].second; cnt[t]++; std::map<int,std::string> kv; std::string layer;
    for(size_t j=i+1;j<g.size()&&g[j].first!=0;j++){ if(!kv.count(g[j].first)) kv[g[j].first]=g[j].second; }
    if(t=="DIMENSION") printf("DIMENSION layer=%s block=%s type=%s measured(42)=%s text(1)='%s' defpt=(%s,%s) p13=(%s,%s) p14=(%s,%s)\n",
      kv[8].c_str(),kv[2].c_str(),kv[70].c_str(),kv[42].c_str(),kv[1].c_str(),kv[10].c_str(),kv[20].c_str(),kv[13].c_str(),kv[23].c_str(),kv[14].c_str(),kv[24].c_str());
    else if(t=="TEXT"||t=="MTEXT") printf("%s '%s' at (%s,%s) h=%s\n",t.c_str(),kv[1].c_str(),kv[10].c_str(),kv[20].c_str(),kv[40].c_str());
    else if(t=="LINE") printf("LINE layer=%s (%s,%s)-(%s,%s) ltype=%s\n",kv[8].c_str(),kv[10].c_str(),kv[20].c_str(),kv[11].c_str(),kv[21].c_str(),kv.count(6)?kv[6].c_str():"BYLAYER");
  }
  for(auto& kv:cnt) printf("  %s x%d\n",kv.first.c_str(),kv.second); }
