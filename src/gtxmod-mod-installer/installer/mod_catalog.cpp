#include "mod_catalog.hpp"
#include "http_client.hpp"
#include "json.hpp"
#include <algorithm>

namespace gtxcatalog {
    const char *const categories[]={"الكل","الأحدث","سيارات","طيارات","موطورات","قوارب","شخصيات","مابات","سكربتات"};
const size_t categoryCount=sizeof(categories)/sizeof(categories[0]);
static const char *BASE="https://gtxmod-ps4.pages.dev/";
std::string resolveUrl(const std::string &path){
    if(path.compare(0,7,"http://")==0||path.compare(0,8,"https://")==0){
        const std::string trusted="https://gtxmod-ps4.pages.dev/";
        if(path.compare(0,trusted.size(),trusted)==0 && path.find_first_of("\r\n") == std::string::npos)return path;
        const std::string mediafire="https://www.mediafire.com/file/";
        if(path.compare(0,mediafire.size(),mediafire)==0 && path.find_first_of("\r\n") == std::string::npos)return path;
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
    auto addMod=[&](const gtxjson::Value &v){
        if(v.type!=gtxjson::Value::Object)return;
        Mod m;
        auto field=[&](const char *k){const gtxjson::Value *x=v.get(k);return x?x->string():std::string();};
        m.id=field("id");m.name=field("name");m.category=field("category");m.description=field("description");
        m.image=field("image");m.video=field("video");m.download=field("download");m.size=field("size");
        const gtxjson::Value *imgs=v.get("images");if(!imgs)imgs=v.get("gallery");if(imgs&&imgs->type==gtxjson::Value::Array)for(const gtxjson::Value &iv:imgs->array){std::string u=iv.string();if(!u.empty())m.images.push_back(u);}if(m.images.empty()&&!m.image.empty())m.images.push_back(m.image);if(m.image.empty()&&!m.images.empty())m.image=m.images[0];
        m.sortKey=field("createdAt");if(m.sortKey.empty())m.sortKey=field("publishedAt");if(m.sortKey.empty())m.sortKey=field("date");if(m.sortKey.empty())m.sortKey=field("created_at");
        if(m.id.empty()||m.name.empty()||m.download.empty())return;
        if(m.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)return;
        if(resolveUrl(m.download).empty())return;
        found.push_back(m);
    };
    for(const gtxjson::Value &v:root.array){if(found.size()>=1000)break;addMod(v);}
    // الموقع يحمّل منشورات المستخدمين من API منفصل بعد mods.json.
    // يجب دمجها هنا حتى تظهر أكواد codes.community داخل تطبيق PS4 أيضاً.
    std::string communityJson, communityErr;
    if(gtxhttp::getText(std::string(BASE)+"api/community",communityJson,&communityErr)){
        gtxjson::Value communityRoot;std::string communityParseErr;
        if(gtxjson::parse(communityJson,communityRoot,&communityParseErr)){
            const gtxjson::Value *community=communityRoot.get("mods");
            if(community&&community->type==gtxjson::Value::Array)for(const gtxjson::Value &v:community->array){if(found.size()>=1000)break;addMod(v);}
        }
    }
    std::stable_sort(found.begin(),found.end(),[](const Mod&a,const Mod&b){
        if(a.sortKey.empty()&&b.sortKey.empty())return false;
        if(a.sortKey.empty())return false;
        if(b.sortKey.empty())return true;
        return a.sortKey>b.sortKey;
    });
    // أكواد المنشورات لا تأتي داخل mods.json؛ الموقع يوفرها من API مستقل.
    // نطبق نفس أولوية الموقع: static ثم community عند غياب static.
    std::string codesJson, codesErr;
    if(gtxhttp::getText(std::string(BASE)+"api/mod-codes",codesJson,&codesErr)){
        gtxjson::Value codeRoot;std::string codeParseErr;
        if(gtxjson::parse(codesJson,codeRoot,&codeParseErr)){
            const gtxjson::Value *all=codeRoot.get("codes");
            if(all&&all->type==gtxjson::Value::Object){
                std::map<std::string,std::string> codeById;
                const gtxjson::Value *st=all->get("static");
                if(st&&st->type==gtxjson::Value::Object)for(const auto &p:st->object){std::string c=p.second.string();if(!p.first.empty()&&!c.empty())codeById[p.first]=c;}
                const gtxjson::Value *cm=all->get("community");
                if(cm&&cm->type==gtxjson::Value::Object)for(const auto &p:cm->object){std::string c=p.second.string();if(!p.first.empty()&&!c.empty()&&!codeById.count(p.first))codeById[p.first]=c;}
                for(Mod &m:found){auto it=codeById.find(m.id);if(it!=codeById.end())m.code=it->second;}
            }
        }
    }
    mods.swap(found);
    if(mods.empty()){if(error)*error="لم يعرض الموقع أي مود صالح للتنزيل";return false;}
    return true;
}
}
