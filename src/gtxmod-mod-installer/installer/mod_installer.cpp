#include "mod_installer.hpp"
#include "../third_party/miniz/miniz.h"
#include <algorithm>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef GTX_ORBX_ROOT
#define GTX_ORBX_ROOT "/data/orbx"
#endif
#ifndef GTX_DATA_ROOT
#define GTX_DATA_ROOT "/data/GTXMOD"
#endif

namespace gtxinstall {
struct Rule { std::string name, source, destination, mode, xmlLine; };
struct Planned { mz_uint index; std::string destination, stage, backup; bool existed; };
static void say(StatusCallback cb, void *ctx, const char *s) { if(cb)cb(s,ctx); }
static void result(std::vector<Result> &r,const char *s,bool ok) { r.push_back({s,ok}); }
static bool dirExists(const char *p){struct stat st;return stat(p,&st)==0&&S_ISDIR(st.st_mode);}
static bool fileExists(const std::string &p){struct stat st;return stat(p.c_str(),&st)==0&&S_ISREG(st.st_mode);}
static bool mkdirs(const std::string &path){
    if(path.empty()||path.size()>1024||path.find("..")!=std::string::npos)return false;
    std::string p; p.reserve(path.size());
    for(size_t i=0;i<path.size();++i){p.push_back(path[i]);if(path[i]=='/'&&p.size()>1){if(mkdir(p.c_str(),0777)<0&&errno!=EEXIST)return false;}}
    if(mkdir(p.c_str(),0777)<0&&errno!=EEXIST)return false;return true;
}
static bool copyFile(const std::string &src,const std::string &dst){
    FILE *in=fopen(src.c_str(),"rb");if(!in)return false;
    std::string tmp=dst+".gtxmod-copy.tmp";unlink(tmp.c_str());FILE *out=fopen(tmp.c_str(),"wb");if(!out){fclose(in);return false;}
    char b[64*1024];bool ok=true;for(;;){size_t n=fread(b,1,sizeof(b),in);if(n&&fwrite(b,1,n,out)!=n){ok=false;break;}if(n<sizeof(b)){if(ferror(in))ok=false;break;}}
    if(ok&&fflush(out)!=0)ok=false;if(ok&&fsync(fileno(out))<0)ok=false;
    if(fclose(in)!=0)ok=false;if(fclose(out)!=0)ok=false;
    if(ok&&rename(tmp.c_str(),dst.c_str())<0)ok=false;if(!ok)unlink(tmp.c_str());return ok;
}
static bool readFile(const std::string &path,std::string &out,size_t max){
    FILE *f=fopen(path.c_str(),"rb");if(!f)return false;out.clear();char b[8192];bool ok=true;
    for(;;){size_t n=fread(b,1,sizeof(b),f);if(n){if(out.size()+n>max){ok=false;break;}out.append(b,n);}if(n<sizeof(b)){if(ferror(f))ok=false;break;}}
    fclose(f);return ok;
}
static bool safeParent(const std::string &s){
    if(s.empty()||s.size()>80||s=="."||s=="..")return false;
    for(unsigned char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
static std::string actualPath(const std::string &relative){
    const std::string prefix="data/orbx";
    if(relative.compare(0,prefix.size(),prefix)!=0)return std::string();
    return std::string(GTX_ORBX_ROOT)+relative.substr(prefix.size());
}
static bool safeZipName(const std::string &n){
    if(n.empty()||n.size()>512||n[0]=='/'||n.find('\\')!=std::string::npos||n.find(':')!=std::string::npos)return false;
    size_t pos=0;while(pos<n.size()){size_t slash=n.find('/',pos);std::string part=n.substr(pos,slash==std::string::npos?n.size()-pos:slash-pos);if(part==".."||part==".")return false;if(slash==std::string::npos)break;pos=slash+1;}return true;
}
static bool endsWith(const std::string &s,const char *suffix){size_t n=strlen(suffix);return s.size()>=n&&s.compare(s.size()-n,n,suffix)==0;}
static std::string stamp(){time_t now=time(nullptr);struct tm t;localtime_r(&now,&t);char b[40];strftime(b,sizeof(b),"%Y%m%d_%H%M%S",&t);return b;}
static bool uniqueBackupDirectory(std::string &out){
    std::string root=std::string(GTX_DATA_ROOT)+"/backups";if(!mkdirs(root))return false;
    for(int i=0;i<100;i++){
        std::string candidate=root+"/"+stamp()+"_"+std::to_string((long)getpid())+"_"+std::to_string(i);
        if(mkdir(candidate.c_str(),0777)==0){out=candidate;return true;}
        if(errno!=EEXIST)return false;
    }
    return false;
}
static bool hasXmlLine(const std::string &xml,const std::string &line){
    size_t p=0;while(p<xml.size()){size_t e=xml.find('\n',p);std::string s=xml.substr(p,e==std::string::npos?xml.size()-p:e-p);if(!s.empty()&&s[s.size()-1]=='\r')s.pop_back();size_t a=s.find_first_not_of(" \t");if(a!=std::string::npos&&s.substr(a)==line)return true;if(e==std::string::npos)break;p=e+1;}return false;
}
static bool atomicWrite(const std::string &path,const std::string &data){
    std::string tmp=path+".gtxmod.tmp";unlink(tmp.c_str());int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0666);if(fd<0)return false;size_t off=0;bool ok=true;
    while(off<data.size()){ssize_t n=write(fd,data.data()+off,data.size()-off);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}off+=(size_t)n;}
    if(ok&&fsync(fd)<0)ok=false;if(close(fd)<0)ok=false;if(ok&&rename(tmp.c_str(),path.c_str())<0)ok=false;if(!ok)unlink(tmp.c_str());return ok;
}
static std::string registryPath(){return std::string(GTX_DATA_ROOT)+"/installed.db";}
static std::string cleanField(std::string s){for(char &c:s)if(c=='\t'||c=='\r'||c=='\n')c=' ';return s;}
static bool appendInstalled(const std::string&id,const std::vector<Planned>&planned,const std::vector<std::string>&xmlLines){
    if(!mkdirs(GTX_DATA_ROOT))return false;FILE*f=fopen(registryPath().c_str(),"ab");if(!f)return false;
    fprintf(f,"MOD\t%s\n",cleanField(id).c_str());for(const Planned&p:planned)fprintf(f,"PATH\t%s\n",cleanField(p.destination).c_str());for(const std::string&l:xmlLines)fprintf(f,"XML\t%s\n",cleanField(l).c_str());fprintf(f,"END\n");bool ok=fflush(f)==0;fclose(f);return ok;
}
bool orbxReady(std::string *reason){
    std::string root=GTX_ORBX_ROOT;std::string gta=root+"/gta";std::string dlc=gta+"/dlclist.xml";std::string rpf=gta+"/rpf";
    if(!dirExists(root.c_str())||!dirExists(gta.c_str())||!dirExists(rpf.c_str())||!fileExists(dlc)){if(reason)*reason="لم يتم العثور على ORBX أو مسارات GTA المطلوبة: /data/orbx/gta";return false;}return true;
}
bool installOrbx(const std::string &zipPath,StatusCallback status,ProgressCallback progress,void *context,std::vector<Result> &results,std::string *error){
    results.clear();if(error)error->clear();say(status,context,"فتح ملف محرك ORBX");
    mz_zip_archive zip;memset(&zip,0,sizeof(zip));if(!mz_zip_reader_init_file(&zip,zipPath.c_str(),0)){if(error)*error="تعذر فتح ملف ORBX أو أن الملف تالف";result(results,"فتح ملف ORBX",false);return false;}
    const mz_uint count=mz_zip_reader_get_num_files(&zip);mz_uint extracted=0;uint64_t totalBytes=0,currentBytes=0;
    for(mz_uint i=0;i<count;i++){mz_zip_archive_file_stat st;std::string n;if(mz_zip_reader_file_stat(&zip,i,&st)){n=st.m_filename;if(!st.m_is_directory&&safeZipName(n)&&n.compare(0,5,"orbx/")==0)totalBytes+=(uint64_t)st.m_uncomp_size;}}
    if(progress&&!progress(0,totalBytes,context)){mz_zip_reader_end(&zip);if(error)*error="تم إلغاء تثبيت ORBX";return false;}
    for(mz_uint i=0;i<count;i++){
        mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st)||st.m_is_directory)continue;
        std::string name=st.m_filename;if(!safeZipName(name)||name.compare(0,5,"orbx/")!=0)continue;
        std::string relative=name.substr(5);if(relative.empty())continue;
        std::string dest=std::string(GTX_ORBX_ROOT)+"/"+relative;
        if(dest.find("/../")!=std::string::npos||dest.compare(dest.size()>3?dest.size()-3:0,3,"/..") == 0){mz_zip_reader_end(&zip);if(error)*error="مسار غير آمن داخل ملف ORBX";result(results,"فحص مسارات ORBX",false);return false;}
        std::string parent=dest.substr(0,dest.find_last_of('/'));if(!mkdirs(parent)){mz_zip_reader_end(&zip);if(error)*error="تعذر إنشاء مجلدات ORBX";result(results,"إنشاء مجلدات ORBX",false);return false;}
        std::string temp=dest+".gtxorbx.tmp";unlink(temp.c_str());
        if(st.m_uncomp_size>1024ULL*1024*1024||!mz_zip_reader_extract_to_file(&zip,i,temp.c_str(),0)){unlink(temp.c_str());mz_zip_reader_end(&zip);if(error)*error="فشل استخراج ملف من ORBX";result(results,"استخراج ملفات ORBX",false);return false;}
        unlink(dest.c_str());if(rename(temp.c_str(),dest.c_str())<0){unlink(temp.c_str());mz_zip_reader_end(&zip);if(error)*error="فشل نقل ملف ORBX إلى وجهته";result(results,"نقل ملفات ORBX",false);return false;}
        currentBytes+=(uint64_t)st.m_uncomp_size;++extracted;if((extracted%100)==0)say(status,context,"جاري استخراج ملفات محرك ORBX");
        if(progress&&!progress(currentBytes,totalBytes,context)){mz_zip_reader_end(&zip);if(error)*error="تم إلغاء تثبيت ORBX";return false;}
    }
    mz_zip_reader_end(&zip);if(!extracted){if(error)*error="ملف ORBX لا يحتوي مجلد orbx صالحاً";result(results,"العثور على ملفات ORBX",false);return false;}
    result(results,"تثبيت محرك ORBX",true);return true;
}
bool installZip(const std::string &zipPath,const std::string &modId,StatusCallback status,void *context,std::vector<Result> &results,std::string *error){
    results.clear();if(error)error->clear();std::string why;
    say(status,context,"فحص مسارات ORBX");if(!orbxReady(&why)){if(error)*error=why;result(results,"العثور على ORBX",false);return false;}result(results,"العثور على ORBX",true);
    say(status,context,"فتح ZIP وفحص محتوياته");mz_zip_archive zip;memset(&zip,0,sizeof(zip));if(!mz_zip_reader_init_file(&zip,zipPath.c_str(),0)){if(error)*error="تعذر فتح ZIP أو أن الملف تالف";result(results,"فتح ملف ZIP",false);return false;}result(results,"فتح ملف ZIP",true);
    std::vector<Rule> rules;
    rules.push_back({"dlc.rpf","parent","data/orbx/gta/rpf/dlc_addon/{parent}/dlc.rpf","with_parent_folder","<Item>/data/orbx/gta/rpf/dlc_addon/{parent}/</Item>"});
    rules.push_back({"vehicles.rpf","root","data/orbx/gta/rpf/levels/gta5/vehicles.rpf","file_only",std::string()});
    const std::string xmlRel="data/orbx/gta/dlclist.xml";
    say(status,context,"تحليل ملفات ZIP تلقائياً بدون JSON");
    result(results,"قواعد التثبيت الداخلية",true);
    std::vector<Planned> planned;std::vector<std::string> dlcLines;const mz_uint count=mz_zip_reader_get_num_files(&zip);
    for(mz_uint i=0;i<count;i++){
        mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st)||st.m_is_directory)continue;
        std::string name=st.m_filename;if(!safeZipName(name))continue;
        std::string destinationRel;
        size_t lastSlash=name.find_last_of('/');
        std::string base=lastSlash==std::string::npos?name:name.substr(lastSlash+1);
        if(base=="vehicles.rpf"){
            for(const Rule &r:rules)if(r.name=="vehicles.rpf"){destinationRel=r.destination;break;}
        } else if(base=="dlc.rpf"&&lastSlash!=std::string::npos){
            size_t parentSlash=name.find_last_of('/',lastSlash-1);std::string parent=name.substr(parentSlash==std::string::npos?0:parentSlash+1,lastSlash-(parentSlash==std::string::npos?0:parentSlash+1));if(!safeParent(parent))continue;
            for(const Rule &r:rules)if(r.name=="dlc.rpf"){destinationRel=r.destination;size_t p=destinationRel.find("{parent}");if(p!=std::string::npos)destinationRel.replace(p,8,parent);std::string line=r.xmlLine;size_t lp=line.find("{parent}");if(lp!=std::string::npos)line.replace(lp,8,parent);dlcLines.push_back(line);break;}
        }
        if(destinationRel.empty())continue;
        std::string dest=actualPath(destinationRel);if(dest.empty()){mz_zip_reader_end(&zip);if(error)*error="وجهة ملف غير مسموحة";return false;}
        if(st.m_uncomp_size==0||st.m_uncomp_size>1024ULL*1024*1024){mz_zip_reader_end(&zip);if(error)*error="حجم ملف المود غير مقبول";return false;}
        for(const Planned &p:planned)if(p.destination==dest){mz_zip_reader_end(&zip);if(error)*error="أكثر من ملف يطابق وجهة واحدة؛ أوقف التثبيت لحماية الملفات";return false;}
        Planned p;p.index=i;p.destination=dest;p.existed=fileExists(dest);planned.push_back(p);
    }
    if(planned.empty()){mz_zip_reader_end(&zip);if(error)*error="لم يتم العثور على dlc.rpf أو vehicles.rpf مطابق للقواعد";result(results,"العثور على ملفات مدعومة",false);return false;}
    result(results,"مطابقة ملفات المود",true);
    std::string xmlPath=actualPath(xmlRel),oldXml;bool xmlNeedsWrite=false;
    for(const std::string &line:dlcLines)if(!hasXmlLine(oldXml,line))xmlNeedsWrite=true;
    if(!dlcLines.empty()){
        if(!readFile(xmlPath,oldXml,16*1024*1024)){mz_zip_reader_end(&zip);if(error)*error="تعذر قراءة dlclist.xml";return false;}
        xmlNeedsWrite=false;for(const std::string &line:dlcLines)if(!hasXmlLine(oldXml,line))xmlNeedsWrite=true;
        if(xmlNeedsWrite&&oldXml.rfind("</Paths>")==std::string::npos){mz_zip_reader_end(&zip);if(error)*error="لم يتم العثور على وسم </Paths> في dlclist.xml";return false;}
    }
    std::string staging=std::string(GTX_DATA_ROOT)+"/staging/"+((modId.empty()||modId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" )!=std::string::npos)?"mod":modId);
    if(!mkdirs(staging)){mz_zip_reader_end(&zip);if(error)*error="تعذر إنشاء مجلد التجهيز";return false;}
    for(size_t k=0;k<planned.size();k++){
        Planned &p=planned[k];p.stage=staging+"/file_"+std::to_string(k)+".part";unlink(p.stage.c_str());
        say(status,context,"استخراج الملف المطابق من ZIP");
        if(!mz_zip_reader_extract_to_file(&zip,p.index,p.stage.c_str(),0)){for(const Planned &x:planned)unlink(x.stage.c_str());mz_zip_reader_end(&zip);if(error)*error="فشل استخراج ملف RPF من ZIP";return false;}
        if(p.existed&&p.destination!=std::string(GTX_ORBX_ROOT)+"/gta/rpf/levels/gta5/vehicles.rpf"){for(const Planned &x:planned)unlink(x.stage.c_str());mz_zip_reader_end(&zip);if(error)*error="الوجهة موجودة مسبقاً؛ مود DLC موجود ولم يتم استبداله بدون نسخة احتياطية";return false;}
    }
    mz_zip_reader_end(&zip);
    std::string xmlStage=xmlPath+".gtxmod.tmp";
    std::string newXml=oldXml;
    if(xmlNeedsWrite){
        const bool crlf=oldXml.find("\r\n")!=std::string::npos;const std::string nl=crlf?"\r\n":"\n";size_t close=oldXml.rfind("</Paths>");
        std::string additions;for(const std::string &line:dlcLines)if(!hasXmlLine(newXml,line)){additions+="\t\t"+line+nl;}
        newXml.insert(close,additions);
        if(!atomicWrite(xmlStage,newXml)){if(error)*error="تعذر تجهيز نسخة dlclist.xml المعدلة";return false;}
    }
    size_t committed=0;bool commitOk=true;
    for(Planned &p:planned){
        std::string parent=p.destination.substr(0,p.destination.find_last_of('/'));
        if(!mkdirs(parent)||rename(p.stage.c_str(),p.destination.c_str())<0){commitOk=false;break;}++committed;
    }
    if(commitOk&&xmlNeedsWrite&&rename(xmlStage.c_str(),xmlPath.c_str())<0)commitOk=false;
    if(!commitOk){
        for(size_t i=0;i<committed;i++){Planned &p=planned[i];unlink(p.destination.c_str());}
        if(xmlNeedsWrite)unlink(xmlStage.c_str());if(error)*error="فشل إتمام النقل؛ أزيلت الملفات التي نُقلت في هذه المحاولة";result(results,"نقل ملفات المود",false);return false;
    }
    for(const Planned &p:planned)result(results,p.destination.find("dlc_addon")!=std::string::npos?"نقل dlc.rpf إلى مجلده":"نقل vehicles.rpf إلى gta5",true);
    result(results,"تحديث dlclist.xml",!xmlNeedsWrite||fileExists(xmlPath));
    bool recorded=appendInstalled(modId,planned,dlcLines);result(results,"حفظ سجل المود للمسح لاحقاً",recorded);
    if(!recorded){if(error)*error="تم التثبيت لكن تعذر حفظ سجل الحذف في /data/GTXMOD/installed.db";return false;}
    result(results,"اكتمل التثبيت",true);return true;
}
bool listInstalled(std::vector<InstalledMod>&out,std::string*){
    out.clear();FILE*f=fopen(registryPath().c_str(),"rb");char line[2048];InstalledMod cur;bool open=false;
    if(f){
    while(fgets(line,sizeof(line),f)){size_t n=strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;std::string s(line);if(s.compare(0,4,"MOD\t")==0){if(open)out.push_back(cur);cur=InstalledMod();cur.id=s.substr(4);open=true;}else if(open&&s.compare(0,5,"PATH\t")==0)cur.paths.push_back(s.substr(5));else if(open&&s.compare(0,4,"XML\t")==0)cur.xmlLines.push_back(s.substr(4));else if(open&&s=="END"){out.push_back(cur);cur=InstalledMod();open=false;}}
    if(open)out.push_back(cur);fclose(f);}
    std::string xml;const std::string xmlPath=std::string(GTX_ORBX_ROOT)+"/gta/dlclist.xml";const std::string prefix="<Item>/data/orbx/gta/rpf/dlc_addon/";
    if(readFile(xmlPath,xml,16*1024*1024)){
        size_t p=0;while((p=xml.find(prefix,p))!=std::string::npos){size_t a=p+prefix.size(),b=xml.find("/</Item>",a);if(b==std::string::npos)break;std::string parent=xml.substr(a,b-a);if(safeParent(parent)){std::string path=std::string(GTX_ORBX_ROOT)+"/gta/rpf/dlc_addon/"+parent+"/dlc.rpf";bool known=false;for(const auto&m:out)for(const auto&x:m.paths)if(x==path)known=true;if(!known){InstalledMod m;m.id=parent;m.name=parent;m.paths.push_back(path);m.xmlLines.push_back(xml.substr(p,b+8-p));out.push_back(m);}}p=b+8;}
    }
    std::vector<InstalledMod> newest;std::vector<std::string> seenPaths;for(size_t ri=out.size();ri>0;--ri){const InstalledMod&m=out[ri-1];bool duplicate=false;for(const std::string&p:m.paths)for(const std::string&s:seenPaths)if(p==s)duplicate=true;if(!duplicate){newest.push_back(m);for(const std::string&p:m.paths)seenPaths.push_back(p);}}std::reverse(newest.begin(),newest.end());out.swap(newest);return true;
}
static std::string trimXmlLine(std::string s){while(!s.empty()&&(s.back()=='\r'||s.back()==' '||s.back()=='\t'))s.pop_back();size_t p=s.find_first_not_of(" \t");return p==std::string::npos?std::string():s.substr(p);}
static bool removeXmlLines(const std::string&path,const std::vector<std::string>&lines){
    std::vector<std::string> targets;for(const std::string&l:lines){std::string t=trimXmlLine(l);if(!t.empty())targets.push_back(t);}if(targets.empty())return false;
    std::string raw;if(!readFile(path,raw,16*1024*1024))return false;std::string out;size_t p=0;while(p<=raw.size()){size_t e=raw.find('\n',p);std::string line=raw.substr(p,e==std::string::npos?raw.size()-p:e-p);std::string cmp=trimXmlLine(line);bool drop=false;for(const std::string&t:targets)if(cmp==t){drop=true;break;}if(!drop){out+=line;if(e!=std::string::npos)out+='\n';}if(e==std::string::npos)break;p=e+1;}return atomicWrite(path,out);
}
bool removeInstalled(const InstalledMod&mod,std::vector<Result>&results,std::string*error){
    results.clear();bool ok=true;for(const std::string&p:mod.paths){if(p.compare(0,strlen(GTX_ORBX_ROOT),GTX_ORBX_ROOT)!=0){ok=false;result(results,"مسار غير مسموح",false);continue;}if(unlink(p.c_str())==0||errno==ENOENT){std::string parent=p.substr(0,p.find_last_of('/'));rmdir(parent.c_str());result(results,"حذف ملف المود أو أنه محذوف مسبقاً",true);}else{ok=false;result(results,"حذف ملف المود",false);}}
    if(!mod.xmlLines.empty()){std::string xml=std::string(GTX_ORBX_ROOT)+"/gta/dlclist.xml";if(removeXmlLines(xml,mod.xmlLines))result(results,"حذف أسطر DLC",true);else{ok=false;result(results,"حذف أسطر DLC",false);}}
    if(ok){std::vector<InstalledMod> all;listInstalled(all,nullptr);std::string db;for(const auto&m:all){bool samePath=false;for(const auto&p:m.paths)for(const auto&q:mod.paths)if(p==q)samePath=true;if(m.id==mod.id||samePath)continue;db+="MOD\t"+cleanField(m.id)+"\n";for(const auto&p:m.paths)db+="PATH\t"+cleanField(p)+"\n";for(const auto&l:m.xmlLines)db+="XML\t"+cleanField(l)+"\n";db+="END\n";}if(!atomicWrite(registryPath(),db))ok=false;}
    if(!ok&&error)*error="تعذر حذف بعض ملفات المود";return ok;
}
}
