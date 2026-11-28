#include "mod_installer.hpp"
#include "../third_party/miniz/miniz.h"
#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <un7zip.h>

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
static std::string trimLine(std::string s){while(!s.empty()&&(s.back()=='\r'||s.back()==' '||s.back()=='\t'))s.pop_back();size_t p=s.find_first_not_of(" \t");return p==std::string::npos?std::string():s.substr(p);}
static bool installOrbxPlugin(const std::string &asset,std::vector<Result> &results,std::string *error){
    const std::string pluginDir="/data/GoldHEN/plugins",pluginPath=pluginDir+"/orbx.prx",iniPath="/data/GoldHEN/plugins.ini",pluginLine="/data/GoldHEN/plugins/orbx.prx",section="[CUSA00419]";
    if(!mkdirs(pluginDir)){if(error)*error="تعذر إنشاء مجلد GoldHEN/plugins";result(results,"إنشاء مجلد GoldHEN/plugins",false);return false;}
    mz_zip_archive zip;memset(&zip,0,sizeof(zip));if(!mz_zip_reader_init_file(&zip,asset.c_str(),0)){if(error)*error="تعذر فتح prx.zip";result(results,"فتح prx.zip",false);return false;}
    int index=mz_zip_reader_locate_file(&zip,"orbx.prx",nullptr,0);mz_zip_archive_file_stat st;bool extracted=false;
    if(index>=0&&mz_zip_reader_file_stat(&zip,(mz_uint)index,&st)&&!st.m_is_directory&&st.m_uncomp_size<=64ULL*1024*1024){std::string temp=pluginPath+".gtxmod.tmp";unlink(temp.c_str());if(mz_zip_reader_extract_to_file(&zip,(mz_uint)index,temp.c_str(),0)&&rename(temp.c_str(),pluginPath.c_str())==0)extracted=true;else unlink(temp.c_str());}
    mz_zip_reader_end(&zip);if(!extracted){if(error)*error="تعذر استخراج orbx.prx";result(results,"تثبيت orbx.prx",false);return false;}result(results,"تثبيت orbx.prx داخل GoldHEN/plugins",true);
    std::string raw;if(!readFile(iniPath,raw,4*1024*1024))raw.clear();const bool crlf=raw.find("\r\n")!=std::string::npos;const std::string nl=crlf?"\r\n":"\n";std::vector<std::string> lines;size_t p=0;while(p<=raw.size()){size_t e=raw.find('\n',p);lines.push_back(trimLine(raw.substr(p,e==std::string::npos?raw.size()-p:e-p)));if(e==std::string::npos)break;p=e+1;}
    size_t sectionAt=lines.size(),sectionEnd=lines.size();for(size_t i=0;i<lines.size();++i){if(lines[i]==section){sectionAt=i;sectionEnd=lines.size();for(size_t j=i+1;j<lines.size();++j)if(!lines[j].empty()&&lines[j][0]=='['){sectionEnd=j;break;}break;}}
    bool hasPlugin=false;if(sectionAt<lines.size())for(size_t i=sectionAt+1;i<sectionEnd;++i)if(lines[i]==pluginLine){hasPlugin=true;break;}
    if(sectionAt==lines.size()){if(!raw.empty()&&raw[raw.size()-1]!='\n')raw+=nl;if(!raw.empty())raw+=nl;raw+=section+nl+pluginLine+nl;}
    else if(!hasPlugin){size_t insertAt=sectionAt+1;while(insertAt<sectionEnd&&lines[insertAt].empty())++insertAt;lines.insert(lines.begin()+insertAt,pluginLine);raw.clear();for(size_t i=0;i<lines.size();++i){raw+=lines[i];raw+=nl;}}
    if(!atomicWrite(iniPath,raw)){if(error)*error="تم تثبيت orbx.prx لكن تعذر تحديث plugins.ini";result(results,"تحديث plugins.ini",false);return false;}result(results,"إضافة orbx.prx إلى CUSA00419",true);return true;
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
    const std::string replacement="/app0/assets/orbx/dlclist.xml";
    const std::string installedXml=std::string(GTX_ORBX_ROOT)+"/gta/dlclist.xml";
    say(status,context,"استبدال ملف dlclist.xml القديم");
    if(!fileExists(replacement)||!copyFile(replacement,installedXml)){if(error)*error="تعذر استبدال ملف dlclist.xml بالنسخة الجديدة";result(results,"استبدال dlclist.xml",false);return false;}
    result(results,"استبدال dlclist.xml",true);
    say(status,context,"تثبيت إضافة ORBX داخل GoldHEN");
    if(!installOrbxPlugin("/app0/assets/orbx/prx.zip",results,error))return false;
    result(results,"تثبيت محرك ORBX",true);return true;
}
static bool removeTree(const std::string&root){DIR*d=opendir(root.c_str());if(!d)return true;struct dirent*e;bool ok=true;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;std::string p=root+"/"+e->d_name;struct stat st;if(stat(p.c_str(),&st)!=0)continue;if(S_ISDIR(st.st_mode)){if(!removeTree(p)||rmdir(p.c_str())!=0)ok=false;}else if(unlink(p.c_str())!=0)ok=false;}closedir(d);return ok&&rmdir(root.c_str())==0;}
static bool knownPayload(const std::string&name){static const char*exts[]={".rpf",".meta",".omt",".dat",".xml",".ytd",".ydr",".yft",".ymap",".ybn",".ymt",".awc",".rel",".bik",".img"};for(const char*e:exts)if(endsWith(name,e))return true;return false;}
static void collectTree(const std::string&root,const std::string&rel,std::vector<std::string>&files){std::string dir=rel.empty()?root:root+"/"+rel;DIR*d=opendir(dir.c_str());if(!d)return;struct dirent*e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;std::string child=rel.empty()?e->d_name:rel+"/"+e->d_name;std::string p=root+"/"+child;struct stat st;if(stat(p.c_str(),&st)!=0)continue;if(S_ISDIR(st.st_mode))collectTree(root,child,files);else if(S_ISREG(st.st_mode))files.push_back(child);}closedir(d);}
static std::string cleanArchiveRelative(std::string p){for(char&c:p)if(c=='\\')c='/';while(!p.empty()&&p[0]=='/')p.erase(0,1);size_t z=p.find("zip/");if(z==0)p=p.substr(4);if(p.compare(0,5,"orbx/")==0)p=p.substr(5);return p;}
static bool genericInstallTree(const std::string&root,const std::string&modId,StatusCallback status,void*context,std::vector<Result>&results,std::string*error){
    std::vector<std::string> files;collectTree(root,"",files);std::vector<Planned> planned;std::vector<std::string> xmlLines;
    for(const std::string&raw:files){if(endsWith(raw,".txt"))continue;std::string rel=cleanArchiveRelative(raw),base=rel.substr(rel.find_last_of('/')==std::string::npos?0:rel.find_last_of('/')+1);if(!knownPayload(base))continue;std::string destination;
        if(base=="dlc.rpf"){size_t slash=rel.find_last_of('/');if(slash==std::string::npos)continue;std::string parent=rel.substr(rel.find_last_of('/',slash-1)==std::string::npos?0:rel.find_last_of('/',slash-1)+1,slash-(rel.find_last_of('/',slash-1)==std::string::npos?0:rel.find_last_of('/',slash-1)+1));if(!safeParent(parent))continue;destination=std::string(GTX_ORBX_ROOT)+"/gta/rpf/dlc_addon/"+parent+"/dlc.rpf";xmlLines.push_back("<Item>/data/orbx/gta/rpf/dlc_addon/"+parent+"/</Item>");}
        else if(base=="vehicles.rpf")destination=std::string(GTX_ORBX_ROOT)+"/gta/rpf/levels/gta5/vehicles.rpf";
        else if(base=="rockstar_logos.bik")destination=std::string(GTX_ORBX_ROOT)+"/gta/rockstar_logos.bik";
        else {size_t gta=rel.find("gta/");std::string inside=gta==std::string::npos?rel:rel.substr(gta);destination=std::string(GTX_ORBX_ROOT)+"/"+inside;}
        bool duplicate=false;for(const Planned&p:planned)if(p.destination==destination)duplicate=true;if(duplicate)continue;Planned p;p.destination=destination;p.existed=fileExists(destination);p.stage=root+"/"+raw;planned.push_back(p);
    }
    if(planned.empty()){if(error)*error="لم يتم العثور على ملفات ORBX معروفة داخل الأرشيف";result(results,"فحص ملفات الأرشيف",false);return false;}
    std::string xmlPath=std::string(GTX_ORBX_ROOT)+"/gta/dlclist.xml",oldXml,newXml;if(!xmlLines.empty()&&!readFile(xmlPath,oldXml,16*1024*1024)){if(error)*error="تعذر قراءة dlclist.xml";return false;}newXml=oldXml;if(!xmlLines.empty()){size_t close=newXml.rfind("</Paths>");if(close==std::string::npos){if(error)*error="لم يتم العثور على </Paths> في dlclist.xml";return false;}std::string add;for(const std::string&l:xmlLines)if(!hasXmlLine(newXml,l))add+="\t\t"+l+"\n";newXml.insert(close,add);}
    for(Planned&p:planned){std::string parent=p.destination.substr(0,p.destination.find_last_of('/'));if(!mkdirs(parent)||!copyFile(p.stage,p.destination)){if(error)*error="فشل نقل ملف ORBX إلى مساره";return false;}std::string message="استبدال "+p.destination;result(results,message.c_str(),true);}
    if(!xmlLines.empty()&&!atomicWrite(xmlPath,newXml)){if(error)*error="تعذر تحديث dlclist.xml";return false;}if(!xmlLines.empty())result(results,"تحديث dlclist.xml",true);if(!appendInstalled(modId,planned,xmlLines)){if(error)*error="تم التثبيت لكن تعذر حفظ سجل الحذف";return false;}result(results,"تثبيت ملفات الأرشيف",true);return true;
}
static bool extractZipTree(mz_zip_archive&zip,const std::string&root){const mz_uint n=mz_zip_reader_get_num_files(&zip);for(mz_uint i=0;i<n;++i){mz_zip_archive_file_stat st;if(!mz_zip_reader_file_stat(&zip,i,&st)||st.m_is_directory||!safeZipName(st.m_filename))continue;std::string out=root+"/"+st.m_filename,parent=out.substr(0,out.find_last_of('/'));if(!mkdirs(parent)||!mz_zip_reader_extract_to_file(&zip,i,out.c_str(),0))return false;}return true;}

static bool looksRar(const std::string&path){FILE*f=fopen(path.c_str(),"rb");if(!f)return false;unsigned char sig[7]={0};size_t n=fread(sig,1,7,f);fclose(f);return n>=4&&sig[0]==0x52&&sig[1]==0x61&&sig[2]==0x72&&sig[3]==0x21;}
static bool extractRarTree(const std::string&path,const std::string&root){return unrar_mini_extract(path.c_str(),root.c_str())==0;}
static bool looks7z(const std::string&path){FILE*f=fopen(path.c_str(),"rb");if(!f)return false;unsigned char sig[6]={0};size_t n=fread(sig,1,6,f);fclose(f);return n==6&&sig[0]==0x37&&sig[1]==0x7A&&sig[2]==0xBC&&sig[3]==0xAF&&sig[4]==0x27&&sig[5]==0x1C;}
bool installZip(const std::string &zipPath,const std::string &modId,StatusCallback status,void *context,std::vector<Result> &results,std::string *error){
    results.clear();if(error)error->clear();std::string why;
    say(status,context,"فحص مسارات ORBX");if(!orbxReady(&why)){if(error)*error=why;result(results,"العثور على ORBX",false);return false;}result(results,"العثور على ORBX",true);
    const bool is7z=endsWith(zipPath,".7z")||endsWith(zipPath,".7Z")||looks7z(zipPath);
    const bool isRar=endsWith(zipPath,".rar")||endsWith(zipPath,".RAR")||looksRar(zipPath);
    const std::string expanded=std::string(GTX_DATA_ROOT)+"/staging/archive_"+((modId.empty()||modId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)?"mod":modId);
    removeTree(expanded);if(!mkdirs(expanded)){if(error)*error="تعذر إنشاء مجلد فك الأرشيف";return false;}
    bool unpacked=false;
    if(isRar){say(status,context,"فك ملف RAR");unpacked=extractRarTree(zipPath,expanded);}
    else if(is7z){say(status,context,"فك ملف 7z");unpacked=un7z_ExtractArchive(zipPath.c_str(),expanded.c_str(),nullptr,0x80000)==SZ_OK;}
    else {say(status,context,"فك ملف ZIP وفحص المجلدات الداخلية");mz_zip_archive scan;memset(&scan,0,sizeof(scan));if(mz_zip_reader_init_file(&scan,zipPath.c_str(),0)){unpacked=extractZipTree(scan,expanded);mz_zip_reader_end(&scan);}}
    if(!unpacked){removeTree(expanded);if(error)*error=isRar?"تعذر فك ملف RAR أو أن الملف تالف":(is7z?"تعذر فك ملف 7z أو أن الملف تالف":"تعذر فك ملف ZIP أو أن الملف تالف");result(results,"فك الأرشيف",false);return false;}
    bool genericOk=genericInstallTree(expanded,modId,status,context,results,error);removeTree(expanded);return genericOk;
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
    std::string raw;if(!readFile(path,raw,16*1024*1024))return false;std::string out;size_t p=0;while(p<=raw.size()){size_t e=raw.find('\n',p);std::string line=raw.substr(p,e==std::string::npos?raw.size()-p:e-p);std::string cmp=trimXmlLine(line);bool drop=false;for(const std::string&t:targets){if(cmp==t||(cmp.find("<Item>")!=std::string::npos&&cmp.find("</Item>")!=std::string::npos&&cmp.find(t)!=std::string::npos)){drop=true;break;}}if(!drop){out+=line;if(e!=std::string::npos)out+='\n';}if(e==std::string::npos)break;p=e+1;}return atomicWrite(path,out);
}
static bool xmlLineForDlcPath(const std::string&path,std::string&line){
    const std::string prefix=std::string(GTX_ORBX_ROOT)+"/gta/rpf/dlc_addon/",suffix="/dlc.rpf";
    if(path.compare(0,prefix.size(),prefix)!=0||path.size()<=prefix.size()+suffix.size()||path.compare(path.size()-suffix.size(),suffix.size(),suffix)!=0)return false;
    std::string parent=path.substr(prefix.size(),path.size()-prefix.size()-suffix.size());if(!safeParent(parent))return false;
    line="<Item>/data/orbx/gta/rpf/dlc_addon/"+parent+"/</Item>";return true;
}
bool removeInstalled(const InstalledMod&mod,std::vector<Result>&results,std::string*error){
    results.clear();bool ok=true;std::vector<std::string> xmlLines=mod.xmlLines;
    for(const std::string&p:mod.paths){std::string derived;if(xmlLineForDlcPath(p,derived))xmlLines.push_back(derived);if(p.compare(0,strlen(GTX_ORBX_ROOT),GTX_ORBX_ROOT)!=0){ok=false;result(results,"مسار غير مسموح",false);continue;}if(unlink(p.c_str())==0||errno==ENOENT){std::string parent=p.substr(0,p.find_last_of('/'));rmdir(parent.c_str());result(results,"حذف ملف المود أو أنه محذوف مسبقاً",true);}else{ok=false;result(results,"حذف ملف المود",false);}}
    if(!xmlLines.empty()){std::string xml=std::string(GTX_ORBX_ROOT)+"/gta/dlclist.xml";if(removeXmlLines(xml,xmlLines))result(results,"حذف أسطر DLC",true);else{ok=false;result(results,"حذف أسطر DLC",false);}}
    if(ok){std::vector<InstalledMod> all;listInstalled(all,nullptr);std::string db;for(const auto&m:all){bool samePath=false;for(const auto&p:m.paths)for(const auto&q:mod.paths)if(p==q)samePath=true;if(m.id==mod.id||samePath)continue;db+="MOD\t"+cleanField(m.id)+"\n";for(const auto&p:m.paths)db+="PATH\t"+cleanField(p)+"\n";for(const auto&l:m.xmlLines)db+="XML\t"+cleanField(l)+"\n";db+="END\n";}if(!atomicWrite(registryPath(),db))ok=false;}
    if(!ok&&error)*error="تعذر حذف بعض ملفات المود";return ok;
}
}
