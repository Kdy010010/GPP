/***********************************************************************
 * gpp_transpiler.cpp  (v1.1 – MSVC raw-string safe)
 * ---------------------------------------------------------------
 * Build:
 *   g++  -std=c++14  gpp_transpiler.cpp  -o gpp
 *   cl   /std:c++14  gpp_transpiler.cpp  /Fe:gpp.exe
 **********************************************************************/
#include <iostream>
#include <fstream>
#include <regex>
#include <map>
#include <set>
#include <sstream>

/*───────────── primitive → stdint mapping ─────────────*/
static const std::map<std::string,std::string> kType = {
    {"i8","int8_t"},{"i16","int16_t"},{"i32","int32_t"},{"i64","int64_t"},
    {"u8","uint8_t"},{"u16","uint16_t"},{"u32","uint32_t"},{"u64","uint64_t"}
};

/*───────────── helpers ─────────────*/
static std::string trim(std::string s){
    size_t a=s.find_first_not_of(" \t\r\n"),
           b=s.find_last_not_of(" \t\r\n");
    return (a==std::string::npos)? "" : s.substr(a,b-a+1);
}
static std::string map_tok(const std::string& t){
    std::map<std::string,std::string>::const_iterator it=kType.find(t);
    return (it==kType.end())?t:it->second;
}
static std::string map_types(std::string s){
    for(std::map<std::string,std::string>::const_iterator it=kType.begin();
        it!=kType.end(); ++it){
        std::regex r("\\b"+it->first+"(?=\\b|\\s|\\)|\\(|\\&|\\*)");
        s = std::regex_replace(s,r,it->second);
    }
    return s;
}
static std::string escape_c(const std::string& s){
    std::string r; for(char c:s){ if(c=='\\'||c=='\"') r.push_back('\\'); r.push_back(c);} return r;
}

/* forward */
static void transpile_file(const std::string& path,
                           std::ostream&      out,
                           std::set<std::string>& visited);

/*───────────── stream transpile core ─────────────*/
static void transpile_stream(std::istream& in,std::ostream& out,
                             std::set<std::string>& visited,
                             const std::string& curdir)
{
#define RX(x) std::regex x
    RX(re_import   (R"gpp(^\s*import\s+"([^"]+)"\s*;?)gpp"));
    RX(re_ns_s     (R"gpp(^\s*namespace\s+([A-Za-z_0-9]+)\s*\{)gpp"));

    RX(re_let      (R"gpp(^\s*let\s+([A-Za-z_0-9]+)\s+([A-Za-z_0-9]+)\s*=\s*(.+);)gpp"));
    RX(re_var      (R"gpp(^\s*var\s+([A-Za-z_0-9]+)\s*=\s*(.+);)gpp"));
    RX(re_fn       (R"gpp(^\s*(?:fn|func)(?:\s+([A-Za-z_0-9]+))?\s+([A-Za-z_0-9]+)\s*\(([^)]*)\)\s*\{)gpp"));
    RX(re_for_let  (R"gpp(^\s*for\s+let\s+([A-Za-z_0-9]+)\s+([A-Za-z_0-9]+)\s*=\s*([^;]+;)(.*))gpp"));

    RX(re_print    (R"gpp(print!\s*\((.*)\);)gpp"));
    RX(re_asm_s    (R"gpp(^\s*asm\s*\{)gpp"));
    RX(re_asm_e    (R"gpp(\}\s*$)gpp"));
    RX(re_ext_s    (R"gpp(^\s*externC\s*\{)gpp"));
    RX(re_ext_e    (R"gpp(\}\s*$)gpp"));

    RX(re_typedef1 (R"gpp(^\s*typedef\s+([A-Za-z_0-9]+)\s+([A-Za-z_0-9]+)\s*;)gpp"));
    RX(re_typedef2 (R"gpp(^\s*typedef\s+([A-Za-z_0-9]+)\s*=\s*([A-Za-z_0-9]+)\s*;)gpp"));
    RX(re_elseif   (R"gpp(\belseif\b)gpp"));
#undef RX

    bool in_asm=false, in_ext=false, in_ns=false;
    std::string ns,line; std::smatch m;

    while(std::getline(in,line)){
        std::string outl=line;

        /* externC 그대로 통과 */
        if(in_ext){
            out<<outl<<'\n';
            if(std::regex_search(line,re_ext_e)) in_ext=false;
            continue;
        }
        /* asm 묶음 */
        if(in_asm){
            if(std::regex_search(line,re_asm_e)){
                in_asm=false;
                std::string body=line; body.erase(body.find('}'));
                outl="  \""+escape_c(trim(body))+"\\n\" );";
            }else{
                outl="  \""+escape_c(trim(line))+"\\n\"";
            }
            out<<outl<<'\n'; continue;
        }

        /* namespace 블럭 */
        if(std::regex_search(line,m,re_ns_s)){ in_ns=true; ns=m[1];
            out<<"/* namespace "<<ns<<" */\n"; continue; }
        if(in_ns && trim(line)=="}"){ in_ns=false;
            out<<"/* end namespace "<<ns<<" */\n"; continue; }

        /* import 처리 */
        if(std::regex_search(line,m,re_import)){
            std::string sub=curdir+m[1].str();
            if(visited.insert(sub).second){
                out<<"/* import \""<<m[1].str()<<"\" */\n";
                transpile_file(sub,out,visited);
            }else{
                out<<"/* import \""<<m[1].str()<<"\" skipped */\n";
            }
            continue;
        }

        /* 규칙 기반 변환 */
        if(std::regex_search(line,m,re_let)){
            outl = map_tok(m[1])+' '+m[2].str()+" = "+m[3].str()+';';
        }else if(std::regex_search(line,m,re_var)){
            outl = "int32_t "+m[1].str()+" = "+m[2].str()+';';
        }else if(std::regex_search(line,m,re_fn)){
            std::string ret=m[1].length()?map_tok(m[1]):"int";
            outl = ret+' '+m[2].str()+'('+map_types(m[3])+"){";
        }else if(std::regex_search(line,m,re_for_let)){
            outl = "for("+map_tok(m[1])+' '+m[2].str()+" = "+m[3].str()+m[4].str();
        }else if(std::regex_search(line,m,re_print)){
            outl = "printf(\"%lld\\n\", (long long)("+m[1].str()+"));";
        }else if(std::regex_search(line,re_asm_s)){
            in_asm=true;
            std::string body=line.substr(line.find('{')+1);
            if(std::regex_search(body,re_asm_e)){ in_asm=false; body.erase(body.find('}'));
                outl="__asm__ __volatile__(\""+escape_c(trim(body))+"\\n\" );";
            }else{
                outl="__asm__ __volatile__(\""+escape_c(trim(body))+"\\n\"";
            }
        }else if(std::regex_search(line,re_ext_s)){
            in_ext=true; outl=line;
            if(std::regex_search(line,re_ext_e)) in_ext=false;
        }else if(std::regex_search(line,m,re_typedef1)){
            outl="typedef "+map_tok(m[1])+' '+m[2].str()+';';
        }else if(std::regex_search(line,m,re_typedef2)){
            outl="typedef "+map_tok(m[2])+' '+m[1].str()+';';
        }else{
            outl=std::regex_replace(outl,re_elseif,"else if");
            outl=map_types(outl);
        }
        out<<outl<<'\n';
    }
}

/*───────────── file wrapper ─────────────*/
static void transpile_file(const std::string& path,
                           std::ostream& out,
                           std::set<std::string>& visited)
{
    std::ifstream fin(path.c_str());
    if(!fin){ out<<"/* cannot open "<<path<<" */\n"; return; }

    if(visited.size()==1)           /* 첫 파일이면 헤더 삽입 */
        out<<"#include <stdint.h>\n#include <stdio.h>\n\n";

    std::string dir="";
    size_t p=path.find_last_of("/\\");
    if(p!=std::string::npos) dir=path.substr(0,p+1);

    transpile_stream(fin,out,visited,dir);
}

/*───────────── main ─────────────*/
int main(int argc,char* argv[]){
    if(argc<2){ std::cerr<<"usage: "<<argv[0]<<" <src.gpp> [-o out.c]\n"; return 1; }
    std::ostream* pout=&std::cout; std::ofstream fout;
    if(argc>=4 && std::string(argv[2])=="-o"){
        fout.open(argv[3]); if(!fout){ std::cerr<<"cannot write "<<argv[3]<<"\n"; return 1; }
        pout=&fout;
    }
    std::set<std::string> visited;  visited.insert(argv[1]);
    transpile_file(argv[1],*pout,visited);
    return 0;
}
