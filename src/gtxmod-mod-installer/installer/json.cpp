#include "json.hpp"
#include <cstdlib>
#include <stdint.h>

namespace gtxjson {
const Value *Value::get(const std::string &key) const {
    if (type != Object) return nullptr;
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}
std::string Value::string(const std::string &fallback) const {
    return type == String || type == Number ? text : fallback;
}
bool Value::boolean(bool fallback) const {
    return type == Boolean ? text == "true" : fallback;
}

class Parser {
    const std::string &s;
    size_t p = 0;
    std::string why;
    void ws() { while (p < s.size() && (s[p]==' ' || s[p]=='\n' || s[p]=='\r' || s[p]=='\t')) ++p; }
    static void utf8(std::string &out, uint32_t cp) {
        if (cp <= 0x7f) out.push_back((char)cp);
        else if (cp <= 0x7ff) { out.push_back((char)(0xc0 | (cp>>6))); out.push_back((char)(0x80 | (cp&63))); }
        else if (cp <= 0xffff) { out.push_back((char)(0xe0 | (cp>>12))); out.push_back((char)(0x80 | ((cp>>6)&63))); out.push_back((char)(0x80 | (cp&63))); }
        else { out.push_back((char)(0xf0 | (cp>>18))); out.push_back((char)(0x80 | ((cp>>12)&63))); out.push_back((char)(0x80 | ((cp>>6)&63))); out.push_back((char)(0x80 | (cp&63))); }
    }
    bool hex4(uint32_t &v) {
        if (p + 4 > s.size()) return false;
        v=0;
        for (int i=0;i<4;i++) { char c=s[p++]; int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(d<0)return false; v=(v<<4)|(uint32_t)d; }
        return true;
    }
    bool stringValue(std::string &out) {
        if (p >= s.size() || s[p++] != '"') return false;
        while (p < s.size()) {
            unsigned char c=(unsigned char)s[p++];
            if (c=='"') return true;
            if (c<0x20) return false;
            if (c!='\\') { out.push_back((char)c); continue; }
            if (p>=s.size()) return false;
            char e=s[p++];
            switch(e) {
                case '"': case '\\': case '/': out.push_back(e); break;
                case 'b': out.push_back('\b'); break; case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break; case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
                case 'u': { uint32_t cp; if(!hex4(cp))return false; if(cp>=0xd800&&cp<=0xdbff) { if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')return false; uint32_t lo; if(!hex4(lo)||lo<0xdc00||lo>0xdfff)return false; cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00); } else if(cp>=0xdc00&&cp<=0xdfff)return false; utf8(out,cp); break; }
                default:return false;
            }
        }
        return false;
    }
    bool value(Value &v, int depth) {
        if (depth>24) { why="JSON nesting exceeds limit"; return false; }
        ws(); if(p>=s.size()) { why="unexpected end"; return false; }
        char c=s[p];
        if(c=='"') { v.type=Value::String; if(!stringValue(v.text)){why="invalid string";return false;} return true; }
        if(c=='{') {
            ++p; v.type=Value::Object; ws(); if(p<s.size()&&s[p]=='}'){++p;return true;}
            while(p<s.size()) { ws(); std::string key; if(!stringValue(key)){why="expected object key";return false;} ws(); if(p>=s.size()||s[p++]!=':'){why="expected colon";return false;} Value child; if(!value(child,depth+1))return false; v.object[key]=std::move(child); ws(); if(p<s.size()&&s[p]=='}'){++p;return true;} if(p>=s.size()||s[p++]!=','){why="expected comma";return false;} }
            why="unterminated object"; return false;
        }
        if(c=='[') {
            ++p; v.type=Value::Array; ws(); if(p<s.size()&&s[p]==']'){++p;return true;}
            while(p<s.size()) { Value child; if(!value(child,depth+1))return false; v.array.push_back(std::move(child)); if(v.array.size()>20000){why="array too large";return false;} ws(); if(p<s.size()&&s[p]==']'){++p;return true;} if(p>=s.size()||s[p++]!=','){why="expected comma";return false;} }
            why="unterminated array"; return false;
        }
        size_t start=p;
        while(p<s.size()&&s[p]!=','&&s[p]!=']'&&s[p]!='}'&&s[p]!=' '&&s[p]!='\n'&&s[p]!='\r'&&s[p]!='\t')++p;
        if(start==p){why="invalid value";return false;}
        v.text=s.substr(start,p-start);
        if(v.text=="true"||v.text=="false")v.type=Value::Boolean;
        else if(v.text=="null")v.type=Value::Null;
        else { char *end=nullptr; std::strtod(v.text.c_str(),&end); if(!end||*end){why="invalid number";return false;} v.type=Value::Number; }
        return true;
    }
public:
    explicit Parser(const std::string &in):s(in){}
    bool run(Value &out,std::string *error){ bool ok=value(out,0); ws(); if(ok&&p!=s.size()){ok=false;why="trailing data";} if(!ok&&error)*error=why; return ok; }
};
bool parse(const std::string &input, Value &out, std::string *error) {
    if(input.size()>4*1024*1024){if(error)*error="JSON exceeds 4 MiB";return false;}
    Parser p(input); return p.run(out,error);
}
}
