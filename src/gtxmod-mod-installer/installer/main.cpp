#ifndef GRAPHICS_USES_FONT
#define GRAPHICS_USES_FONT 1
#endif
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <sstream>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <vector>
#include <string>
#include "graphics.h"
#include "png.h"
#include "http_client.hpp"
#include "mod_catalog.hpp"
#include "mod_installer.hpp"
#include "preview.hpp"

std::stringstream debugLogStream;
Scene2D::~Scene2D() {}
static const int W=1920,H=1080;
static const Color BG={3,10,25}, PANEL={8,22,49}, ROW={12,31,65};
static const Color ACTIVE={18,102,230}, RED={255,55,65}, WHITE={242,247,255};
static const Color MUTED={157,181,220}, GREEN={73,226,157}, BAD={255,105,124}, BLUE={36,145,255};
static const Color DEEP={2,16,42}, NAV={7,27,61}, GLOW={22,85,181}, GOLD={255,198,45};
struct ArabicForm { uint32_t cp, iso, fin, ini, med; bool joinNext, joinPrev; };
static const ArabicForm A[] = {
 {0x0621,0xFE80,0,0,0,0,0},{0x0622,0xFE81,0xFE82,0,0,0,1},{0x0623,0xFE83,0xFE84,0,0,0,1},{0x0625,0xFE87,0xFE88,0,0,0,1},{0x0627,0xFE8D,0xFE8E,0,0,0,1},{0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,1,1},{0x0629,0xFE93,0xFE94,0,0,0,1},{0x062A,0xFE95,0xFE96,0xFE97,0xFE98,1,1},{0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,1,1},{0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,1,1},{0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,1,1},{0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,1,1},{0x062F,0xFEA9,0xFEAA,0,0,0,1},{0x0630,0xFEAB,0xFEAC,0,0,0,1},{0x0631,0xFEAD,0xFEAE,0,0,0,1},{0x0632,0xFEAF,0xFEB0,0,0,0,1},{0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,1,1},{0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,1,1},{0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,1,1},{0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,1,1},{0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,1,1},{0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,1,1},{0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,1,1},{0x063A,0xFECD,0xFECE,0xFECF,0xFED0,1,1},{0x0641,0xFED1,0xFED2,0xFED3,0xFED4,1,1},{0x0642,0xFED5,0xFED6,0xFED7,0xFED8,1,1},{0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,1,1},{0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,1,1},{0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,1,1},{0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,1,1},{0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,1,1},{0x0648,0xFEED,0xFEEE,0,0,0,1},{0x0649,0xFEEF,0xFEF0,0,0,0,1},{0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,1,1}
};
static const ArabicForm *form(uint32_t cp){for(size_t i=0;i<sizeof(A)/sizeof(A[0]);++i)if(A[i].cp==cp)return &A[i];return 0;}
static std::vector<uint32_t> utf8(const char*s){std::vector<uint32_t>v;const unsigned char*p=(const unsigned char*)s;while(*p){uint32_t c;int n;if(*p<128){c=*p;n=1;}else if((*p&224)==192){c=*p&31;n=2;}else if((*p&240)==224){c=*p&15;n=3;}else{c=*p&7;n=4;}for(int i=1;i<n;++i)c=(c<<6)|(p[i]&63);v.push_back(c);p+=n;}return v;}
static uint32_t shaped(const std::vector<uint32_t>&v,size_t i){const ArabicForm*c=form(v[i]);if(!c)return v[i];const ArabicForm*p=i?form(v[i-1]):0,*n=i+1<v.size()?form(v[i+1]):0;bool jp=p&&p->joinNext&&c->joinPrev,jn=n&&c->joinNext&&n->joinPrev;if(jp&&jn&&c->med)return c->med;if(jp&&c->fin)return c->fin;if(jn&&c->ini)return c->ini;return c->iso;}
static FT_Face gEn=nullptr,gAr=nullptr;
static void arabic(Scene2D*s,FT_Face face,const std::string&str,int right,int base,Color fg){
    std::vector<uint32_t>raw=utf8(str.c_str()),v;
    for(size_t i=0;i<raw.size();){
        bool latin=(raw[i]<128)&&((raw[i]>='A'&&raw[i]<='Z')||(raw[i]>='a'&&raw[i]<='z')||(raw[i]>='0'&&raw[i]<='9'));
        if(latin){size_t j=i;while(j<raw.size()&&raw[j]<128&&((raw[j]>='A'&&raw[j]<='Z')||(raw[j]>='a'&&raw[j]<='z')||(raw[j]>='0'&&raw[j]<='9')))++j;for(size_t k=j;k>i;--k)v.push_back(raw[k-1]);i=j;}
        else{v.push_back(raw[i]);++i;}
    }
    int pen=right;
    for(size_t i=0;i<v.size();++i){
        uint32_t original=v[i],cp=shaped(v,i);const ArabicForm*af=form(original);
        FT_Face use=af?face:gEn;FT_UInt gi=FT_Get_Char_Index(use,cp);
        if(!gi&&af){cp=original;gi=FT_Get_Char_Index(use,cp);} if(!gi)continue;
        if(FT_Load_Glyph(use,gi,FT_LOAD_DEFAULT)||FT_Render_Glyph(use->glyph,ft_render_mode_normal))continue;
        FT_GlyphSlot g=use->glyph;pen-=g->advance.x>>6;
        for(int y=0;y<g->bitmap.rows;++y)for(int x=0;x<g->bitmap.width;++x){int dx=pen+x+g->bitmap_left,dy=base+y-g->bitmap_top;if(dx<0||dy<0||dx>=W||dy>=H)continue;uint8_t a=g->bitmap.buffer[y*g->bitmap.pitch+x];if(a){Color c={(uint8_t)(fg.r*a/255),(uint8_t)(fg.g*a/255),(uint8_t)(fg.b*a/255)};s->DrawPixel(dx,dy,c);}}
    }
}
static bool hasArabic(const std::string&s){for(size_t i=0;i<s.size();++i)if((unsigned char)s[i]>=0xD8&&(unsigned char)s[i]<=0xDF)return true;return false;}
static void smart(Scene2D*s,const std::string&str,int right,int base,Color c){if(str.empty())return;if(hasArabic(str))arabic(s,gAr,str,right,base,c);else{std::string t=str;s->DrawText((char*)t.c_str(),gEn,right-(int)t.size()*23,base,BG,c);}}
static void text(Scene2D*s,FT_Face f,const char*t,int x,int y,Color c){s->DrawText((char*)t,f,x,y,BG,c);}
static Scene2D *gScene=nullptr;static PNG *gLeft=nullptr,*gRight=nullptr,*gHero=nullptr;static PreviewImage *gPreview=nullptr;static int gFrame=0;static int media=0,focus=0;static std::string previewPath;
static std::vector<gtxcatalog::Mod> mods;static std::vector<size_t> shown;static std::vector<PreviewImage*> previewCache;static std::string appMessage,listError,searchQuery;static int screen=0,category=0,sel=0,searchCursor=0;static bool searchArabic=false,httpReady=false,offline=false;static uint64_t progNow=0,progTotal=0;static std::string progTitle="";static std::vector<gtxinstall::Result> installResults;static std::string installError;
static FILE *bootLog=nullptr;
static void trace(const char *stage){if(!bootLog){mkdir("/data/GTXMOD",0777);bootLog=fopen("/data/GTXMOD/boot.log","w");}if(bootLog){fprintf(bootLog,"%s\n",stage);fflush(bootLog);}}
static std::string crop(const std::string&s,size_t max){if(s.size()<=max)return s;size_t n=max;while(n>0&&(((unsigned char)s[n]&0xc0)==0x80))--n;return s.substr(0,n)+"…";}
static void box(int x,int y,int w,int h,Color fill,Color edge={0,0,0}){
    gScene->DrawRectangle(x,y,w,h,fill);
    if(edge.r||edge.g||edge.b){gScene->DrawRectangle(x,y,w,3,edge);gScene->DrawRectangle(x,y,3,h,edge);}
}
static void frame(int x,int y,int w,int h,Color c){
    gScene->DrawRectangle(x,y,w,4,c);gScene->DrawRectangle(x,y+h-4,w,4,c);
    gScene->DrawRectangle(x,y,4,h,c);gScene->DrawRectangle(x+w-4,y,4,h,c);
}
static void neon(int x,int y,int w,int h,bool active){
    Color edge=active?RED:Color{25,78,153};
    box(x,y,w,h,active?Color{10,55,125}:Color{7,25,58},edge);
    if(active){frame(x,y,w,h,RED);gScene->DrawRectangle(x+7,y+7,w-14,3,{36,173,255});}
}
static void logo(){text(gScene,gEn,"GTXMOD",800,67,WHITE);text(gScene,gEn,"PS4",1035,67,RED);}
static void header(){gScene->DrawRectangle(0,0,W,94,DEEP);gScene->DrawRectangle(0,91,W,4,RED);logo();}
static void hero(){
    int x=36,y=118,w=1820,h=270;box(x,y,w,h,DEEP,{22,104,220});
    gScene->DrawRectangle(x+8,y+8,w-16,4,RED);
    if(gHero)gHero->Draw(gScene,1540,125);
    smart(gScene,"طوّر تجربتك في",1360,175,WHITE);
    text(gScene,gEn,"GTA V",1030,225,RED);
    smart(gScene,"مودات مختارة لجهاز PS4 المعدّل",1360,300,MUTED);
}
static void categories(){int y=410;for(size_t i=0;i<gtxcatalog::categoryCount;i++){int x=36+(int)i*203;bool active=(int)i==category&&focus==0;neon(x,y,190,62,active);smart(gScene,gtxcatalog::categories[i],x+170,y+40,active?WHITE:MUTED);}smart(gScene,focus==0?"التصنيفات":"المنشورات",1850,507,focus==0?RED:WHITE);}
static void drawPreview(const gtxcatalog::Mod&m,int x,int y,int w,int h){box(x,y,w,h,Color{8,30,70},{29,99,208});if(media==0&&gPreview&&gPreview->valid()){gPreview->drawFit(gScene,x+5,y+5,w-10,h-10);}else if(media==1&& !m.video.empty()){text(gScene,gEn,"PLAY",x+w/2-50,y+h/2,RED);smart(gScene,"معاينة الفيديو",x+w-35,y+h/2+70,WHITE);}else{smart(gScene,media==1?"لا يوجد فيديو لهذا المنشور":"لا توجد صورة لهذا المنشور",x+w-25,y+h/2, MUTED);}}
static void card(const gtxcatalog::Mod&m,int x,int y,int w,int h,bool active){
    box(x,y,w,h,Color{7,25,58},active?RED:Color{28,76,145});
    if(active)frame(x,y,w,h,RED);
    size_t pageStart=shown.empty()?0:((size_t)sel/3)*3;
    size_t slot=(size_t)((x-36)/610);
    size_t modIndex=(pageStart+slot<shown.size())?shown[pageStart+slot]:(size_t)-1;
    PreviewImage *cached=(modIndex<previewCache.size())?previewCache[modIndex]:nullptr;
    if(cached&&cached->valid()) cached->drawFit(gScene,x+5,y+5,w-10,145);
    else box(x+5,y+5,w-10,145,Color{13,48,97});
    std::string tag="#"+m.category;
    smart(gScene,tag,x+w-22,y+194,active?WHITE:BLUE);
    smart(gScene,crop(m.name,32),x+w-22,y+244,WHITE);
    box(x+18,y+h-58,w-36,38,active?ACTIVE:ROW,active?RED:Color{27,76,145});
    smart(gScene,"اضغط X لعرض معلومات",x+w-35,y+h-32,WHITE);
}
static std::string lowerAscii(const std::string&s){std::string o=s;for(size_t i=0;i<o.size();++i)if(o[i]>='A'&&o[i]<='Z')o[i]=(char)(o[i]-'A'+'a');return o;}
static int editDistance(const std::string&a,const std::string&b){std::vector<int> d(b.size()+1);for(size_t j=0;j<=b.size();++j)d[j]=(int)j;for(size_t i=1;i<=a.size();++i){int prev=d[0];d[0]=(int)i;for(size_t j=1;j<=b.size();++j){int old=d[j];d[j]=std::min(std::min(d[j]+1,d[j-1]+1),prev+(a[i-1]==b[j-1]?0:1));prev=old;}}return d[b.size()];}
static bool fuzzyMatch(const gtxcatalog::Mod&m){
    std::string q=lowerAscii(searchQuery);if(q.empty())return true;
    std::string hay=lowerAscii(m.name+" "+m.category+" "+m.description);
    if(hay.find(q)!=std::string::npos)return true;
    if((q=="iraq"||q=="iraqi")&&(hay.find("عراق")!=std::string::npos||hay.find("العراق")!=std::string::npos))return true;
    if(q.size()>=2){for(size_t i=0;i<hay.size();){while(i<hay.size()&&hay[i]==' ')++i;size_t j=i;while(j<hay.size()&&hay[j]!=' ')++j;if(j>i){std::string word=hay.substr(i,j-i);if(word.find(q)==0||editDistance(q,word)<=2)return true;}i=j;}}
    return false;
}
static const char *AR_KEYS[]={"ا","ب","ت","ث","ج","ح","خ","د","ذ","ر","ز","س","ش","ص","ض","ط","ظ","ع","غ","ف","ق","ك","ل","م","ن","ه","و","ي"};
static int searchKeyCount(){return searchArabic?28:36;}
static std::string searchKey(int i){if(searchArabic)return AR_KEYS[i];const char keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";return std::string(1,keys[i]);}
static void drawSearch(){
    gScene->FrameBufferFill(Color{0,0,0});header();
    box(100,135,1720,790,DEEP,{29,99,208});
    smart(gScene,"البحث عن مودات",1640,215,WHITE);
    box(180,255,1560,70,Color{4,12,25},RED);
    smart(gScene,searchQuery.empty()?"اكتب اسم المود أو كلمة قريبة":searchQuery,1680,300,searchQuery.empty()?MUTED:WHITE);
    int cols=10;
    for(int i=0;i<searchKeyCount();++i){int x=250+(i%cols)*135,y=390+(i/cols)*85;bool active=i==searchCursor;box(x,y,100,58,active?ACTIVE:ROW,active?RED:Color{27,76,145});smart(gScene,searchKey(i),x+70,y+42,WHITE);if(active)frame(x,y,100,58,RED);}
    smart(gScene,"X إضافة الحرف   R2 عربي/إنكليزي   مربع بحث   مثلث حذف   O رجوع",1660,840,WHITE);
}
static void drawList(){
    gScene->FrameBufferFill(BG);header();hero();categories();
    box(36,490,620,48,Color{4,12,25},searchQuery.empty()?Color{27,76,145}:RED);
    smart(gScene,searchQuery.empty()?"اضغط مربع للبحث":("بحث: "+searchQuery),610,522,searchQuery.empty()?MUTED:WHITE);
    smart(gScene,category==1?"أحدث المنشورات":(searchQuery.empty()?"أحدث المنشورات":"نتائج البحث"),1850,535,WHITE);
    char countText[48];snprintf(countText,sizeof(countText),"%d مود",(int)shown.size());smart(gScene,countText,1490,535,BLUE);
    size_t pageStart=shown.empty()?0:((size_t)sel/3)*3;
    for(size_t i=0;i<3&&pageStart+i<shown.size();++i){const gtxcatalog::Mod&m=mods[shown[pageStart+i]];card(m,36+(int)i*610,565,578,345,(int)(pageStart+i)==sel);}
    if(shown.empty()){box(36,565,1820,345,DEEP,{28,91,181});smart(gScene,listError.empty()?"ماكو منشورات بهذا التصنيف حالياً":crop(listError,70),1780,735,listError.empty()?MUTED:BAD);smart(gScene,"اضغط مثلث لتحديث المنشورات من الموقع",1780,800,WHITE);}
    gScene->DrawRectangle(0,H-70,W,4,RED);text(gScene,gEn,"GTXMOD PS4",38,H-25,WHITE);smart(gScene,"L1/R1 التصنيف   ↓ المنشورات   ←/→ اختيار   مربع بحث   X معلومات   O خروج",1850,H-25,WHITE);
}
static void drawDetails(){
    gScene->FrameBufferFill(BG);header();
    if(shown.empty()||sel<0||sel>=(int)shown.size()){smart(gScene,"لا يوجد منشور محدد",1760,300,BAD);smart(gScene,"O رجوع",1760,900,WHITE);return;}
    const gtxcatalog::Mod&m=mods[shown[(size_t)sel]];
    box(45,130,1830,805,DEEP,{29,99,208});
    frame(45,130,1830,805,Color{18,102,230});
    box(85,205,760,540,Color{8,30,70},{29,99,208});
    PreviewImage *cached=(shown[(size_t)sel]<previewCache.size())?previewCache[shown[(size_t)sel]]:nullptr;
    if(cached&&cached->valid())cached->drawFit(gScene,95,215,740,520);
    else smart(gScene,"لا توجد صورة لهذا المنشور",790,485,MUTED);
    smart(gScene,"معلومات المنشور",1780,215,RED);
    smart(gScene,crop(m.name,42),1780,295,WHITE);
    smart(gScene,"التصنيف",1780,365,BLUE);
    std::string tag="#"+m.category;smart(gScene,tag,1780,415,WHITE);
    smart(gScene,"السيرة الذاتية",1780,490,BLUE);
    std::string description=m.description.empty()?"لا توجد سيرة ذاتية لهذا المنشور":m.description;
    for(int i=0;i<3&&!description.empty();++i){std::string line=crop(description,42);smart(gScene,line,1780,545+i*48,WHITE);if(line.size()>=description.size())break;description=description.substr(line.size());}
    smart(gScene,"الحجم",1780,715,BLUE);
    smart(gScene,m.size.empty()?"غير محدد":m.size,1780,760,WHITE);
    box(1010,825,780,58,ACTIVE,RED);smart(gScene,"اضغط X للتثبيت",1740,862,WHITE);
    smart(gScene,"O رجوع",810,900,WHITE);
}
static void drawOffline(){
    gScene->FrameBufferFill(Color{0,0,0});
    smart(gScene,"لا يوجد اتصال بالإنترنت",1510,430,RED);
    smart(gScene,"يجب عليك الاتصال بالإنترنت",1510,520,RED);
    smart(gScene,"اضغط مثلث لإعادة التحميل",1510,650,GREEN);
    smart(gScene,"O خروج",1510,820,MUTED);
}
static void drawStatus(const std::string &title,const std::string &body,bool showProgress){
    gScene->FrameBufferFill(BG); header();
    box(55,145,1465,720,DEEP,{29,99,208});
    smart(gScene,title,1800,235,WHITE);
    smart(gScene,body,1750,370,WHITE);
    if(showProgress){
        gScene->DrawRectangle(190,540,1200,34,ROW);int percent=progTotal?(int)((progNow*100)/progTotal):0;if(percent>100)percent=100;
        gScene->DrawRectangle(190,540,(1200*percent)/100,34,RED);char pc[16];snprintf(pc,sizeof(pc),"%d%%",percent);text(gScene,gEn,pc,1390,520,WHITE);
    }
    if(!appMessage.empty())smart(gScene,crop(appMessage,72),1750,690,BAD);
    smart(gScene,"O رجوع",1750,900,WHITE);
}
static void drawResult(){
    gScene->FrameBufferFill(BG); header();
    box(55,145,1465,720,DEEP,{29,99,208});
    smart(gScene,installError.empty()?"اكتمل التثبيت":"تعذر إكمال التثبيت",1800,235,WHITE);
    int y=330;for(const auto&r:installResults){box(190,y-38,1200,62,ROW,r.ok?GREEN:BAD);smart(gScene,r.text,1700,y,r.ok?GREEN:BAD);y+=78;if(y>710)break;}
    if(!installError.empty())smart(gScene,crop(installError,75),1750,770,BAD);else smart(gScene,"تم حفظ النسخ الاحتياطية في /data/GTXMOD/backups",1750,770,BLUE);
    smart(gScene,"O رجوع إلى قائمة المودات",1750,900,WHITE);
}
static void present(){if(screen==0)drawList();else if(screen==1)drawDetails();else if(screen==2)drawStatus(progTitle.c_str(),"يرجى الانتظار، لا تغلق التطبيق",progTotal>0);else if(screen==4)drawOffline();else if(screen==5)drawSearch();else drawResult();gScene->SubmitFlip(gFrame);gScene->FrameWait(gFrame);gScene->FrameBufferSwap();++gFrame;}
static void rebuildShown(){
    shown.clear();
    for(size_t i=0;i<mods.size();++i){
        bool include=searchQuery.empty()?(category<=1||mods[i].category==gtxcatalog::categories[category]):fuzzyMatch(mods[i]);
        if(include)shown.push_back(i);
    }
    if(sel>=(int)shown.size())sel=shown.empty()?0:(int)shown.size()-1;
}

static bool ensureDir(const char*p){if(mkdir(p,0777)==0||errno==EEXIST)return true;return false;}
static bool progress(uint64_t got,uint64_t total,void*){progNow=got;progTotal=total;present();return true;}
static void installStatus(const char*message,void*){appMessage=message?message:"";progTitle="تثبيت المود";present();}
static void clearPreviewCache(){for(size_t i=0;i<previewCache.size();++i)delete previewCache[i];previewCache.clear();gPreview=nullptr;}
static std::string previewFile(const gtxcatalog::Mod&m){ensureDir("/data/GTXMOD");ensureDir("/data/GTXMOD/previews");return std::string("/data/GTXMOD/previews/")+m.id+".media";}
static bool preparePreview(size_t modIndex,bool allowDownload){
    if(modIndex>=mods.size())return false;
    if(previewCache.size()<mods.size())previewCache.resize(mods.size(),nullptr);
    if(previewCache[modIndex]&&previewCache[modIndex]->valid())return true;
    const gtxcatalog::Mod&m=mods[modIndex];if(m.image.empty())return false;
    std::string path=previewFile(m);
    if(access(path.c_str(),F_OK)!=0){if(!allowDownload)return false;std::string u=gtxcatalog::resolveUrl(m.image),e;if(u.empty()||!gtxhttp::download(u,path,nullptr,nullptr,&e))return false;}
    PreviewImage *loaded=new PreviewImage(path.c_str());if(!loaded->valid()){delete loaded;return false;}previewCache[modIndex]=loaded;return true;
}
static void loadPreview(bool allowDownload){gPreview=nullptr;if(shown.empty()||sel<0||sel>=(int)shown.size()||media!=0)return;size_t modIndex=shown[(size_t)sel];if(preparePreview(modIndex,allowDownload))gPreview=previewCache[modIndex];}
static void preloadVisiblePreviews(){size_t limit=shown.size()<12?shown.size():12;for(size_t i=0;i<limit;++i)preparePreview(shown[i],true);loadPreview(false);}
static bool hasAnimatedPreview(){size_t limit=shown.size()<12?shown.size():12;for(size_t i=0;i<limit;++i){size_t idx=shown[i];if(idx<previewCache.size()&&previewCache[idx]&&previewCache[idx]->animated())return true;}return false;}
static void refresh(){
    clearPreviewCache();searchQuery.clear();screen=2;progTitle="الاتصال بموقع GTXMOD";progNow=progTotal=0;appMessage="جاري تحميل قائمة المودات";present();
    if(httpReady){gtxhttp::shutdown();httpReady=false;}
    std::string err;trace("refresh: before HTTPS init");offline=false;
    if(!gtxhttp::initialize(&err)){listError=err;mods.clear();offline=true;trace("refresh: HTTPS init failed");}
    else{httpReady=true;trace("refresh: HTTPS init ok");if(!gtxcatalog::fetchMods(mods,&err)){listError=err;mods.clear();offline=true;trace("refresh: catalog request failed");}else{listError.clear();trace("refresh: catalog loaded");}}
    if(offline){screen=4;appMessage.clear();present();return;}
    rebuildShown();media=0;preloadVisiblePreviews();screen=0;appMessage.clear();present();
}
static void beginInstall(){
    if(shown.empty()||sel<0||sel>=(int)shown.size()){screen=3;installError="لا يوجد مود محدد";return;}
    const gtxcatalog::Mod &m=mods[shown[sel]];screen=2;progTitle="تنزيل المود";appMessage="الاتصال بموقع GTXMOD";progNow=progTotal=0;present();
    ensureDir("/data/GTXMOD");ensureDir("/data/GTXMOD/downloads");std::string zip="/data/GTXMOD/downloads/"+m.id+".zip";std::string err;std::string url=gtxcatalog::resolveUrl(m.download);
    bool ok=!url.empty()&&gtxhttp::download(url,zip,progress,nullptr,&err);
    installResults.clear();installError.clear();if(!ok){installError=err.empty()?"رابط التنزيل غير صالح":err;installResults.push_back({"تنزيل ملف المود",false});screen=3;present();return;}
    appMessage="اكتمل التنزيل؛ جاري فحص المود";progTitle="فحص وتثبيت المود";progTotal=progNow=0;present();
    if(!gtxinstall::installZip(zip,m.id.c_str(),installStatus,nullptr,installResults,&err))installError=err;
    screen=3;appMessage.clear();present();
}
int main(){
    setvbuf(stdout,0,_IONBF,0);trace("main entered");Scene2D scene(W,H,4);if(!scene.Init(0xC000000,2)){trace("Scene2D init failed");return 1;}scene.SetActiveFrameBuffer(0);gScene=&scene;trace("Scene2D init ok");
    if(!scene.InitFont(&gEn,"/app0/assets/fonts/Gontserrat-Regular.ttf",52)){trace("English font init failed");return 2;}trace("English font init ok");if(!scene.InitFont(&gAr,"/app0/assets/fonts/NotoSansArabic-Regular.ttf",43)){trace("Arabic font init failed");return 3;}trace("Arabic font init ok");
    PNG left("/app0/assets/images/GTX-side.png"),right("/app0/assets/images/GTX-side.png");gLeft=&left;gRight=&right;
    PNG heroImage("/app0/assets/images/GTXMOD-header.png");gHero=&heroImage;trace("images loaded");if(scePadInit()<0){trace("scePadInit failed");return 4;}trace("scePadInit ok");OrbisUserServiceInitializeParams up;memset(&up,0,sizeof(up));up.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;if(sceUserServiceInitialize(&up)<0){trace("user service init failed");return 5;}int uid=-1;if(sceUserServiceGetInitialUser(&uid)<0){trace("get initial user failed");return 6;}int pad=scePadOpen(uid,ORBIS_PAD_PORT_TYPE_STANDARD,0,0);if(pad<0){trace("scePadOpen failed");return 7;}trace("pad opened");
    listError="جاري تحميل موقع GTXMOD";uint32_t prev=0;bool run=true;refresh();trace("first screen presented");while(run){OrbisPadData d;memset(&d,0,sizeof(d));if(scePadReadState(pad,&d)>=0){uint32_t cur=d.buttons,pressed=cur&~prev;prev=cur;
        if(screen==0){
            if(pressed&ORBIS_PAD_BUTTON_L1){searchQuery.clear();category=(category+(int)gtxcatalog::categoryCount-1)%(int)gtxcatalog::categoryCount;sel=0;focus=0;media=0;rebuildShown();preloadVisiblePreviews();present();}
            else if(pressed&ORBIS_PAD_BUTTON_R1){searchQuery.clear();category=(category+1)%(int)gtxcatalog::categoryCount;sel=0;focus=0;media=0;rebuildShown();preloadVisiblePreviews();present();}
            else if(pressed&ORBIS_PAD_BUTTON_DOWN){focus=1;present();}
            else if(pressed&ORBIS_PAD_BUTTON_UP){focus=0;present();}
            else if(focus==1&&(pressed&ORBIS_PAD_BUTTON_LEFT)){if(sel>0)--sel;media=0;loadPreview(false);present();}
            else if(focus==1&&(pressed&ORBIS_PAD_BUTTON_RIGHT)){if(sel+1<(int)shown.size())++sel;media=0;loadPreview(false);present();}
            else if(focus==1&&(pressed&ORBIS_PAD_BUTTON_L2)){media=0;loadPreview(false);present();}
            else if(focus==1&&(pressed&ORBIS_PAD_BUTTON_R2)){media=1;present();}
            else if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){refresh();}
            else if(pressed&ORBIS_PAD_BUTTON_SQUARE){screen=5;searchCursor=0;present();}
            else if(focus==1&&(pressed&ORBIS_PAD_BUTTON_CROSS)){if(!shown.empty()){screen=1;appMessage.clear();present();}}
            else if(pressed&ORBIS_PAD_BUTTON_CIRCLE)run=false;
        }
        else if(screen==1){if(pressed&ORBIS_PAD_BUTTON_CIRCLE){screen=0;present();}else if(pressed&ORBIS_PAD_BUTTON_CROSS)beginInstall();}
        else if(screen==3&&(pressed&ORBIS_PAD_BUTTON_CIRCLE)){screen=0;present();}
        else if(screen==4){if(pressed&ORBIS_PAD_BUTTON_TRIANGLE)refresh();else if(pressed&ORBIS_PAD_BUTTON_CIRCLE)run=false;}
        else if(screen==5){
            if(pressed&ORBIS_PAD_BUTTON_R2){searchArabic=!searchArabic;searchCursor%=searchKeyCount();present();}
            else if(pressed&ORBIS_PAD_BUTTON_LEFT){searchCursor=(searchCursor+searchKeyCount()-1)%searchKeyCount();present();}
            else if(pressed&ORBIS_PAD_BUTTON_RIGHT){searchCursor=(searchCursor+1)%searchKeyCount();present();}
            else if(pressed&ORBIS_PAD_BUTTON_UP){searchCursor=(searchCursor+searchKeyCount()-10)%searchKeyCount();present();}
            else if(pressed&ORBIS_PAD_BUTTON_DOWN){searchCursor=(searchCursor+10)%searchKeyCount();present();}
            else if(pressed&ORBIS_PAD_BUTTON_CROSS){searchQuery+=searchKey(searchCursor);present();}
            else if(pressed&ORBIS_PAD_BUTTON_L2){searchQuery+=" ";present();}
            else if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){if(!searchQuery.empty())searchQuery.erase(searchQuery.size()-1);present();}
            else if(pressed&ORBIS_PAD_BUTTON_SQUARE){sel=0;focus=1;category=0;rebuildShown();preloadVisiblePreviews();screen=0;present();}
            else if(pressed&ORBIS_PAD_BUTTON_CIRCLE){searchQuery.clear();rebuildShown();screen=0;present();}
        }
    }if(screen==0&&hasAnimatedPreview())present();usleep(16000);}
    scePadClose(pad);if(httpReady)gtxhttp::shutdown();trace("normal shutdown");if(bootLog){fclose(bootLog);bootLog=nullptr;}return 0;
}
