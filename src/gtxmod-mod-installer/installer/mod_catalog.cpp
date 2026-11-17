#include "mod_catalog.hpp"
#include "http_client.hpp"
#include "json.hpp"
#include <algorithm>

namespace gtxcatalog {
    const char *const categories[]={"الكل","أحدث المنشورات","سيارات","طيارات","موطورات","قوارب","شخصيات","مابات","سكربتات"};
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
        m.image=field("image");m.video=field("video");m.download=field("download");m.size=field("size");
        m.sortKey=field("createdAt");if(m.sortKey.empty())m.sortKey=field("publishedAt");if(m.sortKey.empty())m.sortKey=field("date");if(m.sortKey.empty())m.sortKey=field("created_at");
        if(m.id.empty()||m.name.empty()||m.download.empty())continue;
        if(m.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)continue;
        if(resolveUrl(m.download).empty())continue;
        found.push_back(m);
        if(found.size()>=1000)break;
    }
    std::stable_sort(found.begin(),found.end(),[](const Mod&a,const Mod&b){
        if(a.sortKey.empty()&&b.sortKey.empty())return false;
        if(a.sortKey.empty())return false;
        if(b.sortKey.empty())return true;
        return a.sortKey>b.sortKey;
    });
    mods.swap(found);
    if(mods.empty()){if(error)*error="لم يعرض الموقع أي مود صالح للتنزيل";return false;}
    return true;
}
}
