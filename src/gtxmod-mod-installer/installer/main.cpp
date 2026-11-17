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
#include <vector>
#include <string>
#include "graphics.h"
#include "png.h"
#include "http_client.hpp"
#include "mod_catalog.hpp"
#include "mod_installer.hpp"

std::stringstream debugLogStream;
Scene2D::~Scene2D() {}
static const int W=1920,H=1080;
static const Color BG={3,10,25}, PANEL={8,22,49}, ROW={12,31,65};
static const Color ACTIVE={18,102,230}, RED={0,137,255}, WHITE={242,247,255};
static const Color MUTED={157,181,220}, GREEN={73,226,157}, BAD={255,105,124}, BLUE={36,145,255};
static const Color DEEP={2,16,42}, NAV={7,27,61}, GLOW={22,85,181}, GOLD={255,198,45};
struct ArabicForm { uint32_t cp, iso, fin, ini, med; bool joinNext, joinPrev; };
static const ArabicForm A[] = {
 {0x0621,0xFE80,0,0,0,0,0},{0x0622,0xFE81,0xFE82,0,0,0,1},{0x0623,0xFE83,0xFE84,0,0,0,1},{0x0625,0xFE87,0xFE88,0,0,0,1},{0x0627,0xFE8D,0xFE8E,0,0,0,1},{0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,1,1},{0x0629,0xFE93,0xFE94,0,0,0,1},{0x062A,0xFE95,0xFE96,0xFE97,0xFE98,1,1},{0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,1,1},{0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,1,1},{0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,1,1},{0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,1,1},{0x062F,0xFEA9,0xFEAA,0,0,0,1},{0x0630,0xFEAB,0xFEAC,0,0,0,1},{0x0631,0xFEAD,0xFEAE,0,0,0,1},{0x0632,0xFEAF,0xFEB0,0,0,0,1},{0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,1,1},{0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,1,1},{0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,1,1},{0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,1,1},{0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,1,1},{0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,1,1},{0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,1,1},{0x063A,0xFECD,0xFECE,0xFECF,0xFED0,1,1},{0x0641,0xFED1,0xFED2,0xFED3,0xFED4,1,1},{0x0642,0xFED5,0xFED6,0xFED7,0xFED8,1,1},{0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,1,1},{0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,1,1},{0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,1,1},{0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,1,1},{0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,1,1},{0x0648,0xFEED,0xFEEE,0,0,0,1},{0x0649,0xFEEF,0xFEF0,0,0,0,1},{0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,1,1}
};
static const ArabicForm *form(uint32_t cp){for(size_t i=0;i<sizeof(A)/sizeof(A[0]);++i)if(A[i].cp==cp)return &A[i];return 0;}
static std::vector<uint32_t> utf8(const char*s){std::vector<uint32_t>v;const unsigned char*p=(const unsigned char*)s;while(*p){uint32_t c;int n;if(*p<128){c=*p;n=1;}else if((*p&224)==192){c=*p&31;n=2;}else if((*p&240)==224){c=*p&15;n=3;}else{c=*p&7;n=4;}for(int i=1;i<n;++i)c=(c<<6)|(p[i]&63);v.push_back(c);p+=n;}return v;}
static uint32_t shaped(const std::vector<uint32_t>&v,size_t i){const ArabicForm*c=form(v[i]);if(!c)return v[i];const ArabicForm*p=i?form(v[i-1]):0,*n=i+1<v.size()?form(v[i+1]):0;bool jp=p&&p->joinNext&&c->joinPrev,jn=n&&c->joinNext&&n->joinPrev;if(jp&&jn&&c->med)return c->med;if(jp&&c->fin)return c->fin;if(jn&&c->ini)return c->ini;return c->iso;}
static void arabic(Scene2D*s,FT_Face face,const std::string&str,int right,int base,Color fg){std::vector<uint32_t>v=utf8(str.c_str());int pen=right;for(size_t i=0;i<v.size();++i){uint32_t cp=shaped(v,i);FT_UInt gi=FT_Get_Char_Index(face,cp);if(FT_Load_Glyph(face,gi,FT_LOAD_DEFAULT)||FT_Render_Glyph(face->glyph,ft_render_mode_normal))continue;FT_GlyphSlot g=face->glyph;pen-=g->advance.x>>6;for(int y=0;y<g->bitmap.rows;++y)for(int x=0;x<g->bitmap.width;++x){int dx=pen+x+g->bitmap_left,dy=base+y-g->bitmap_top;if(dx<0||dy<0||dx>=W||dy>=H)continue;uint8_t a=g->bitmap.buffer[y*g->bitmap.pitch+x];if(a){Color c={(uint8_t)(fg.r*a/255),(uint8_t)(fg.g*a/255),(uint8_t)(fg.b*a/255)};s->DrawPixel(dx,dy,c);}}}}
static void text(Scene2D*s,FT_Face f,const char*t,int x,int y,Color c){s->DrawText((char*)t,f,x,y,BG,c);}
static Scene2D *gScene=nullptr;static FT_Face gEn=nullptr,gAr=nullptr;static PNG *gLeft=nullptr,*gRight=nullptr;static int gFrame=0;
static std::vector<gtxcatalog::Mod> mods;static std::vector<size_t> shown;static std::string appMessage,listError;static int screen=0,category=0,sel=0;static uint64_t progNow=0,progTotal=0;static std::string progTitle="";static std::vector<gtxinstall::Result> installResults;static std::string installError;
static FILE *bootLog=nullptr;
static void trace(const char *stage){if(!bootLog){mkdir("/data/GTXMOD",0777);bootLog=fopen("/data/GTXMOD/boot.log","w");}if(bootLog){fprintf(bootLog,"%s\n",stage);fflush(bootLog);}}
static std::string crop(const std::string&s,size_t max){if(s.size()<=max)return s;size_t n=max;while(n>0&&(((unsigned char)s[n]&0xc0)==0x80))--n;return s.substr(0,n)+"…";}
static void box(int x,int y,int w,int h,Color fill,Color edge={0,0,0}){
    gScene->DrawRectangle(x,y,w,h,fill);
    if(edge.r||edge.g||edge.b){gScene->DrawRectangle(x,y,w,3,edge);gScene->DrawRectangle(x,y,3,h,edge);}
}
static void neon(int x,int y,int w,int h,bool active){
    Color edge=active?RED:Color{25,78,153};
    box(x,y,w,h,active?Color{10,55,125}:Color{7,25,58},edge);
    if(active){gScene->DrawRectangle(x+7,y+7,w-14,3,{36,173,255});}
}
static void logo(){
    text(gScene,gEn,"GTX",845,67,WHITE);
    text(gScene,gEn,"MOD",960,67,RED);
    text(gScene,gEn,"PS4",1105,67,WHITE);
    text(gScene,gEn,"PLAYSTATION",1255,67,MUTED);
}
static void header(){
    gScene->DrawRectangle(0,0,W,94,DEEP);
    gScene->DrawRectangle(0,91,W,4,RED);
    box(42,20,84,58,NAV,{29,84,160});
    box(145,20,84,58,NAV,{29,84,160});
    text(gScene,gEn,"*",72,61,WHITE);
    text(gScene,gEn,"!",177,61,WHITE);
    logo();
    box(1572,15,292,68,NAV,{29,84,160});
    text(gScene,gEn,"USER",1605,57,WHITE);
    text(gScene,gEn,"PS4",1780,57,RED);
}
static void sidebar(){
    int x=1570;
    box(x,112,300,760,DEEP,{28,91,181});
    const char *items[]={"الرئيسية","الإدارة","الرفع","الإعدادات","حول التطبيق"};
    int ys[]={175,270,365,505,590};
    for(int i=0;i<5;i++){
        bool active=i==0;
        if(active)box(x+10,ys[i]-43,280,72,ACTIVE,{44,158,255});
        arabic(gScene,gAr,items[i],x+238,ys[i],active?WHITE:MUTED);
        text(gScene,gEn,i==0?"HOME":i==1?"SAFE":i==2?"CLOUD":i==3?"SET":"INFO",x+35,ys[i],active?WHITE:MUTED);
    }
    gScene->DrawRectangle(x+35,430,230,2,{28,91,181});
    arabic(gScene,gAr,"GTXMOD PS4",x+235,820,RED);
}
static void hero(){
    int x=36,y=118,w=1500,h=292;
    box(x,y,w,h,DEEP,{22,104,220});
    gScene->DrawRectangle(x+8,y+8,w-16,4,RED);
    // An abstract GTA/GTXMOD hero treatment that remains readable without extra runtime assets.
    box(x+22,y+28,555,232,{13,35,75},{24,101,204});
    text(gScene,gEn,"GTA",x+70,y+132,WHITE);
    text(gScene,gEn,"V",x+262,y+132,RED);
    text(gScene,gEn,"V",x+320,y+132,RED);
    text(gScene,gEn,"PLAYSTATION 4 MODS",x+56,y+207,MUTED);
    arabic(gScene,gAr,"طوّر تجربتك في",x+w-56,y+92,WHITE);
    text(gScene,gEn,"GTA V",x+w-380,y+165,RED);
    arabic(gScene,gAr,"مودات مختارة لجهاز PS4 المعدّل",x+w-56,y+222,MUTED);
    gScene->DrawRectangle(x+620,y+38,5,215,RED);
    for(int i=0;i<5;i++)gScene->DrawRectangle(x+700+i*125,y+246-i*18,92,3,{15,(uint8_t)(95+i*22),255});
}
static void categories(){
    int y=430;
    for(size_t i=0;i<gtxcatalog::categoryCount;i++){
        int x=36+(int)i*185; bool active=(int)i==category;
        neon(x,y,168,66,active);
        arabic(gScene,gAr,gtxcatalog::categories[i],x+145,y+42,active?WHITE:MUTED);
    }
}
static void card(const gtxcatalog::Mod&m,int x,int y,int w,int h,bool active){
    box(x,y,w,h,Color{7,25,58},active?RED:Color{28,76,145});
    gScene->DrawRectangle(x+4,y+4,w-8,112,Color{13,48,97});
    gScene->DrawRectangle(x+4,y+112,w-8,3,active?RED:Color{20,68,137});
    arabic(gScene,gAr,crop(m.category,16),x+w-22,y+37,active?WHITE:BLUE);
    arabic(gScene,gAr,crop(m.name,25),x+w-22,y+158,WHITE);
    arabic(gScene,gAr,crop(m.description.empty()?"مود GTA V لجهاز PS4":m.description,31),x+w-22,y+198,MUTED);
    arabic(gScene,gAr,m.size.empty()?"الحجم غير محدد":m.size,x+w-22,y+239,WHITE);
    box(x+18,y+h-52,w-36,36,active?ACTIVE:ROW,active?RED:Color{27,76,145});
    arabic(gScene,gAr,"X  تثبيت المود",x+w-35,y+h-27,WHITE);
}
static void drawList(){
    gScene->FrameBufferFill(BG); header(); hero(); categories(); sidebar();
    arabic(gScene,gAr,"🔥 أحدث المنشورات",1518,533,WHITE);
    char countText[48];snprintf(countText,sizeof(countText),"%d MODS",(int)shown.size());
    text(gScene,gEn,countText,45,535,BLUE);
    size_t pageStart=shown.empty()?0:((size_t)sel/3)*3;
    for(size_t i=0;i<3&&pageStart+i<shown.size();++i){
        const gtxcatalog::Mod&m=mods[shown[pageStart+i]];
        card(m,36+(int)i*500,565,468,310,(int)(pageStart+i)==sel);
    }
    if(shown.empty()){
        box(36,565,1498,310,DEEP,{28,91,181});
        arabic(gScene,gAr,listError.empty()?"ماكو منشورات بهذا التصنيف حالياً":crop(listError,70),1480,700,listError.empty()?MUTED:BAD);
        arabic(gScene,gAr,"اضغط △ لتحديث المنشورات من الموقع",1480,765,WHITE);
    }
    if(shown.size()>3){char pageText[64];snprintf(pageText,sizeof(pageText),"%d / %d",(int)(pageStart/3)+1,(int)((shown.size()+2)/3));text(gScene,gEn,pageText,1450,900,MUTED);}
    gScene->DrawRectangle(0,H-70,W,4,RED);
    text(gScene,gEn,"GTXMOD",38,H-25,WHITE);text(gScene,gEn,"PS4",205,H-25,RED);
    text(gScene,gEn,"Mods for a better gaming experience",715,H-25,MUTED);
    arabic(gScene,gAr,"↑ ↓ اختيار   |   ← → تصنيف   |   X تثبيت   |   △ تحديث   |   O خروج",1830,H-25,WHITE);
}
static void drawStatus(const std::string &title,const std::string &body,bool showProgress){
    gScene->FrameBufferFill(BG); header(); sidebar();
    box(55,145,1465,720,DEEP,{29,99,208});
    arabic(gScene,gAr,title,1450,235,WHITE);
    arabic(gScene,gAr,body,1400,370,WHITE);
    if(showProgress){
        gScene->DrawRectangle(190,540,1200,34,ROW);int percent=progTotal?(int)((progNow*100)/progTotal):0;if(percent>100)percent=100;
        gScene->DrawRectangle(190,540,(1200*percent)/100,34,RED);char pc[16];snprintf(pc,sizeof(pc),"%d%%",percent);text(gScene,gEn,pc,1390,520,WHITE);
    }
    if(!appMessage.empty())arabic(gScene,gAr,crop(appMessage,72),1400,690,BAD);
    arabic(gScene,gAr,"O رجوع",1450,900,WHITE);
}
static void drawResult(){
    gScene->FrameBufferFill(BG); header(); sidebar();
    box(55,145,1465,720,DEEP,{29,99,208});
    arabic(gScene,gAr,installError.empty()?"اكتمل التثبيت":"تعذر إكمال التثبيت",1450,235,WHITE);
    int y=330;for(const auto&r:installResults){box(190,y-38,1200,62,ROW,r.ok?GREEN:BAD);arabic(gScene,gAr,r.text,1360,y,r.ok?GREEN:BAD);y+=78;if(y>710)break;}
    if(!installError.empty())arabic(gScene,gAr,crop(installError,75),1400,770,BAD);else arabic(gScene,gAr,"تم حفظ النسخ الاحتياطية في /data/GTXMOD/backups",1400,770,BLUE);
    arabic(gScene,gAr,"O رجوع إلى قائمة المودات",1400,900,WHITE);
}
static void present(){if(screen==0)drawList();else if(screen==1)drawStatus("تأكيد تثبيت المود","سيتم تنزيل الملف وفحص قواعد JSON قبل نسخ ملفات RPF. لن يتم حذف الملفات الموجودة؛ سيتم نسخها احتياطيًا.",false);else if(screen==2)drawStatus(progTitle.c_str(),"يرجى الانتظار، لا تغلق التطبيق",progTotal>0);else drawResult();gScene->SubmitFlip(gFrame);gScene->FrameWait(gFrame);gScene->FrameBufferSwap();++gFrame;}
static void rebuildShown(){shown.clear();for(size_t i=0;i<mods.size();i++)if(category==0||mods[i].category==gtxcatalog::categories[category])shown.push_back(i);if(sel>=(int)shown.size())sel=shown.empty()?0:(int)shown.size()-1;}
static bool ensureDir(const char*p){if(mkdir(p,0777)==0||errno==EEXIST)return true;return false;}
static bool progress(uint64_t got,uint64_t total,void*){progNow=got;progTotal=total;present();return true;}
static void installStatus(const char*message,void*){appMessage=message?message:"";progTitle="تثبيت المود";present();}
static void refresh(){screen=2;progTitle="الاتصال بموقع GTXMOD";progNow=progTotal=0;appMessage="جاري تحميل قائمة المودات";present();std::string err;trace("refresh: before HTTPS init");if(!gtxhttp::initialize(&err)){listError=err;mods.clear();trace("refresh: HTTPS init failed");}else{trace("refresh: HTTPS init ok");if(!gtxcatalog::fetchMods(mods,&err)){listError=err;mods.clear();trace("refresh: catalog request failed");}else{listError.clear();trace("refresh: catalog loaded");}}rebuildShown();screen=0;appMessage.clear();}
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
    trace("images loaded");if(scePadInit()<0){trace("scePadInit failed");return 4;}trace("scePadInit ok");OrbisUserServiceInitializeParams up;memset(&up,0,sizeof(up));up.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;if(sceUserServiceInitialize(&up)<0){trace("user service init failed");return 5;}int uid=-1;if(sceUserServiceGetInitialUser(&uid)<0){trace("get initial user failed");return 6;}int pad=scePadOpen(uid,ORBIS_PAD_PORT_TYPE_STANDARD,0,0);if(pad<0){trace("scePadOpen failed");return 7;}trace("pad opened");
    listError="جاري تحميل موقع GTXMOD";uint32_t prev=0;bool run=true;refresh();present();trace("first screen presented");while(run){OrbisPadData d;memset(&d,0,sizeof(d));if(scePadReadState(pad,&d)>=0){uint32_t cur=d.buttons,pressed=cur&~prev;prev=cur;
        if(screen==0){if(pressed&ORBIS_PAD_BUTTON_UP){if(sel>0)--sel;present();}else if(pressed&ORBIS_PAD_BUTTON_DOWN){if(sel+1<(int)shown.size())++sel;present();}else if(pressed&ORBIS_PAD_BUTTON_LEFT){category=(category+(int)gtxcatalog::categoryCount-1)%(int)gtxcatalog::categoryCount;sel=0;rebuildShown();present();}else if(pressed&ORBIS_PAD_BUTTON_RIGHT){category=(category+1)%(int)gtxcatalog::categoryCount;sel=0;rebuildShown();present();}else if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){refresh();present();}else if(pressed&ORBIS_PAD_BUTTON_CROSS){if(!shown.empty()){screen=1;appMessage.clear();present();}}else if(pressed&ORBIS_PAD_BUTTON_CIRCLE)run=false;}
        else if(screen==1){if(pressed&ORBIS_PAD_BUTTON_CIRCLE){screen=0;present();}else if(pressed&ORBIS_PAD_BUTTON_CROSS)beginInstall();}
        else if(screen==3&&(pressed&ORBIS_PAD_BUTTON_CIRCLE)){screen=0;present();}
    }usleep(16000);}
    scePadClose(pad);gtxhttp::shutdown();trace("normal shutdown");if(bootLog){fclose(bootLog);bootLog=nullptr;}return 0;
}
