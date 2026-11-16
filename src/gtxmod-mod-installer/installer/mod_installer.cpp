#include "mod_installer.hpp"
#include "json.hpp"
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
using gtxjson::Value;
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
static std::string tokenString(const Value *o,const char *key){const Value *v=o?o->get(key):nullptr;return v?v->string():std::string();}
static bool readRules(mz_zip_archive *zip,std::vector<Rule> &rules,std::string &xmlFile,std::string &insertBefore,std::string *err){
    bool found=false;Value chosen;
    mz_uint count=mz_zip_reader_get_num_files(zip);if(count>20000){if(err)*err="الأرشيف يحتوي عددًا غير مسموح من الملفات";return false;}
    for(mz_uint i=0;i<count;i++){
        mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(zip,i,&st))continue;
        std::string name=st.m_filename;if(st.m_is_directory||!endsWith(name,".json"))continue;
        if(st.m_uncomp_size>1024*1024){if(err)*err="ملف JSON داخل ZIP أكبر من الحد المسموح";return false;}
        size_t size=0;void *data=mz_zip_reader_extract_to_heap(zip,i,&size,0);if(!data)continue;
        std::string raw((const char*)data,size);mz_free(data);Value parsed;std::string parseErr;
        if(!gtxjson::parse(raw,parsed,&parseErr))continue;
        const Value *r=parsed.get("rules");if(!r||r->type!=Value::Array)continue;
        if(found){if(err)*err="يوجد أكثر من JSON صالح يحتوي قواعد؛ لا يمكن اختيار قاعدة بأمان";return false;}
        found=true;chosen=std::move(parsed);
    }
    if(!found){if(err)*err="لم يتم العثور على ملف JSON صالح يحتوي rules داخل ZIP";return false;}
    const Value *arr=chosen.get("rules");
    for(const Value &v:arr->array){
        if(v.type!=Value::Object)continue;
        Rule r;r.name=tokenString(&v,"file_name");r.source=tokenString(&v,"source_folder");r.destination=tokenString(&v,"destination");r.mode=tokenString(&v,"copy_mode");r.xmlLine=tokenString(&v,"xml_line");
        if(r.name=="dlc.rpf"||r.name=="vehicles.rpf")rules.push_back(r);
    }
    const Value *xml=chosen.get("xml");xmlFile=tokenString(xml,"file");insertBefore=tokenString(xml,"insert_before");
    if(rules.empty()||xmlFile!="data/orbx/gta/dlclist.xml"||insertBefore!="</Paths>"){if(err)*err="بنية قواعد المثبت لا تطابق المسارات المعتمدة";return false;}
    bool dlc=false,veh=false;
    for(const Rule &r:rules){
        if(r.name=="dlc.rpf"){
            if(r.source!="parent"||r.mode!="with_parent_folder"||r.destination!="data/orbx/gta/rpf/dlc_addon/{parent}/dlc.rpf"||r.xmlLine!="<Item>/data/orbx/gta/rpf/dlc_addon/{parent}/</Item>"){if(err)*err="قاعدة dlc.rpf غير مطابقة للمسار الآمن المتفق عليه";return false;}dlc=true;
        } else if(r.name=="vehicles.rpf"){
            if(r.source!="root"||r.mode!="file_only"||r.destination!="data/orbx/gta/rpf/levels/gta5/vehicles.rpf"){if(err)*err="قاعدة vehicles.rpf غير مطابقة للمسار الآمن المتفق عليه";return false;}veh=true;
        }
    }
    if(!dlc&&!veh){if(err)*err="JSON لا يحتوي قاعدة DLC أو vehicles مدعومة";return false;}
    const Value *doNot=xml?xml->get("do_not_replace"):nullptr;const Value *skip=xml?xml->get("skip_existing_line"):nullptr;
    if(!doNot||!doNot->boolean()||!skip||!skip->boolean()){if(err)*err="إعداد XML يجب أن يمنع الاستبدال والتكرار";return false;}
    return true;
}
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
bool orbxReady(std::string *reason){
    std::string root=GTX_ORBX_ROOT;std::string gta=root+"/gta";std::string dlc=gta+"/dlclist.xml";std::string rpf=gta+"/rpf";
    if(!dirExists(root.c_str())||!dirExists(gta.c_str())||!dirExists(rpf.c_str())||!fileExists(dlc)){if(reason)*reason="لم يتم العثور على ORBX أو مسارات GTA المطلوبة: /data/orbx/gta";return false;}return true;
}
bool installZip(const std::string &zipPath,const std::string &modId,StatusCallback status,void *context,std::vector<Result> &results,std::string *error){
    results.clear();if(error)error->clear();std::string why;
    say(status,context,"فحص مسارات ORBX");if(!orbxReady(&why)){if(error)*error=why;result(results,"العثور على ORBX",false);return false;}result(results,"العثور على ORBX",true);
    say(status,context,"فتح ZIP وفحص محتوياته");mz_zip_archive zip;memset(&zip,0,sizeof(zip));if(!mz_zip_reader_init_file(&zip,zipPath.c_str(),0)){if(error)*error="تعذر فتح ZIP أو أن الملف تالف";result(results,"فتح ملف ZIP",false);return false;}result(results,"فتح ملف ZIP",true);
    std::vector<Rule> rules;std::string xmlRel,insertBefore;
    if(!readRules(&zip,rules,xmlRel,insertBefore,error)){mz_zip_reader_end(&zip);result(results,"قراءة JSON",false);return false;}result(results,"قراءة JSON",true);
    std::vector<Planned> planned;std::vector<std::string> dlcLines;const mz_uint count=mz_zip_reader_get_num_files(&zip);
    for(mz_uint i=0;i<count;i++){
        mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st)||st.m_is_directory)continue;
        std::string name=st.m_filename;if(!safeZipName(name))continue;
        std::string destinationRel;
        if(name=="vehicles.rpf"){
            for(const Rule &r:rules)if(r.name=="vehicles.rpf"){destinationRel=r.destination;break;}
        } else if(endsWith(name,"/dlc.rpf")){
            size_t slash=name.find('/');if(slash==std::string::npos||name.find('/',slash+1)!=std::string::npos)continue;
            std::string parent=name.substr(0,slash);if(!safeParent(parent))continue;
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
    std::string backupRoot;
    std::string staging=std::string(GTX_DATA_ROOT)+"/staging/"+((modId.empty()||modId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" )!=std::string::npos)?"mod":modId);
    if(!uniqueBackupDirectory(backupRoot)||!mkdirs(staging)){mz_zip_reader_end(&zip);if(error)*error="تعذر إنشاء مجلد النسخ الاحتياطية أو التجهيز";return false;}
    for(size_t k=0;k<planned.size();k++){
        Planned &p=planned[k];p.stage=staging+"/file_"+std::to_string(k)+".part";p.backup=backupRoot+"/file_"+std::to_string(k)+".bak";unlink(p.stage.c_str());
        say(status,context,"استخراج الملف المطابق من ZIP");
        if(!mz_zip_reader_extract_to_file(&zip,p.index,p.stage.c_str(),0)){for(const Planned &x:planned)unlink(x.stage.c_str());mz_zip_reader_end(&zip);if(error)*error="فشل استخراج ملف RPF من ZIP";return false;}
        if(p.existed&&!copyFile(p.destination,p.backup)){for(const Planned &x:planned)unlink(x.stage.c_str());mz_zip_reader_end(&zip);if(error)*error="فشل حفظ نسخة احتياطية من ملف موجود";return false;}
    }
    mz_zip_reader_end(&zip);
    std::string xmlBackup=backupRoot+"/dlclist.xml.bak",xmlStage=xmlPath+".gtxmod.tmp";
    std::string newXml=oldXml;
    if(xmlNeedsWrite){
        if(fileExists(xmlPath)&&!copyFile(xmlPath,xmlBackup)){if(error)*error="فشل النسخ الاحتياطي لـdlclist.xml";return false;}
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
        for(size_t i=0;i<committed;i++){Planned &p=planned[i];if(p.existed)copyFile(p.backup,p.destination);else unlink(p.destination.c_str());}
        if(xmlNeedsWrite)unlink(xmlStage.c_str());if(error)*error="فشل إتمام النقل؛ تمت محاولة استعادة الملفات السابقة من النسخ الاحتياطية";result(results,"نقل ملفات المود",false);return false;
    }
    for(const Planned &p:planned)result(results,p.destination.find("dlc_addon")!=std::string::npos?"نقل dlc.rpf إلى مجلده":"نقل vehicles.rpf إلى gta5",true);
    result(results,"تحديث dlclist.xml",!xmlNeedsWrite||fileExists(xmlPath));
    if(xmlNeedsWrite)result(results,"إنشاء نسخة احتياطية من dlclist.xml",fileExists(xmlBackup));
    result(results,"إنشاء نسخ احتياطية للوجهات المستبدلة",true);
    result(results,"اكتمل التثبيت",true);return true;
}
}
