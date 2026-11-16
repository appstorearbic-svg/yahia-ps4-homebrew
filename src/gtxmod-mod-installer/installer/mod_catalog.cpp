#include "mod_catalog.hpp"
#include "http_client.hpp"
#include "json.hpp"

namespace gtxcatalog {
const char *const categories[]={"الكل","سيارات","طيارات","موطورات","قوارب","شخصيات","مابات","سكربتات"};
const size_t categoryCount=sizeof(categories)/sizeof(categories[0]);
static const char *BASE="https://gtxmod-ps4.pages.dev/";
std::string resolveUrl(const std::string &path){
    if(path.compare(0,7,"http://")==0||path.compare(0,8,"https://")==0){
        const std::string trusted="https://gtxmod-ps4.pages.dev/";
        if(path.compare(0,trusted.size(),trusted)==0 && path.find_first_of("\r\n") == std::string::npos)return path;
        return std::string();
    }
    if(path.empty()||path[0]=='/'||path.find("..")!=std::string::npos||path.find_first_of("\\\r\n")!=std::string::npos)return std::string();
    return std::string(BASE)+path;
}
bool fetchMods(std::vector<Mod> &mods,std::string *error){
    std::string json;if(!gtxhttp::getText(std::string(BASE)+"mods.json",json,error))return false;
    gtxjson::Value root;std::string parseErr;
    if(!gtxjson::parse(json,root,&parseErr)||root.type!=gtxjson::Value::Array){if(error)*error="تعذر تحليل قائمة المودات من الموقع: "+parseErr;return false;}
    std::vector<Mod> found;
    for(const gtxjson::Value &v:root.array){
        if(v.type!=gtxjson::Value::Object)continue;
        Mod m;
        auto field=[&](const char *k){const gtxjson::Value *x=v.get(k);return x?x->string():std::string();};
        m.id=field("id");m.name=field("name");m.category=field("category");m.description=field("description");
        m.image=field("image");m.download=field("download");m.size=field("size");
        if(m.id.empty()||m.name.empty()||m.download.empty())continue;
        if(m.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)continue;
        if(resolveUrl(m.download).empty())continue;
        found.push_back(m);
        if(found.size()>=1000)break;
    }
    mods.swap(found);
    if(mods.empty()){if(error)*error="لم يعرض الموقع أي مود صالح للتنزيل";return false;}
    return true;
}
}
