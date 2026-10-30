#define GRAPHICS_USES_FONT
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include <ft2build.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <vector>
#include "graphics.h"
#include "png.h"

extern "C" int installer_153_ready(void);
extern "C" void installer_run_153(int results[3]);
std::stringstream debugLogStream;
Scene2D::~Scene2D() {}

static const int W = 1920, H = 1080;
static const Color BG = {7, 9, 13};
static const Color PANEL = {16, 19, 25};
static const Color ROW = {25, 29, 36};
static const Color ROW_SELECTED = {50, 14, 18};
static const Color RED = {225, 32, 48};
static const Color WHITE = {239, 242, 246};
static const Color MUTED = {144, 151, 160};
static const Color GREEN = {81, 218, 137};
static const Color BAD = {255, 83, 83};

struct ArabicForm { uint32_t cp, isolated, finalForm, initialForm, medialForm; bool joinNext, joinPrev; };
static const ArabicForm ARABIC[] = {
 {0x0621,0xFE80,0,0,0,false,false}, {0x0622,0xFE81,0xFE82,0,0,false,true},
 {0x0623,0xFE83,0xFE84,0,0,false,true}, {0x0625,0xFE87,0xFE88,0,0,false,true},
 {0x0627,0xFE8D,0xFE8E,0,0,false,true}, {0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,true,true},
 {0x0629,0xFE93,0xFE94,0,0,false,true}, {0x062A,0xFE95,0xFE96,0xFE97,0xFE98,true,true},
 {0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,true,true}, {0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,true,true},
 {0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,true,true}, {0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,true,true},
 {0x062F,0xFEA9,0xFEAA,0,0,false,true}, {0x0630,0xFEAB,0xFEAC,0,0,false,true},
 {0x0631,0xFEAD,0xFEAE,0,0,false,true}, {0x0632,0xFEAF,0xFEB0,0,0,false,true},
 {0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,true,true}, {0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,true,true},
 {0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,true,true}, {0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,true,true},
 {0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,true,true}, {0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,true,true},
 {0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,true,true}, {0x063A,0xFECD,0xFECE,0xFECF,0xFED0,true,true},
 {0x0641,0xFED1,0xFED2,0xFED3,0xFED4,true,true}, {0x0642,0xFED5,0xFED6,0xFED7,0xFED8,true,true},
 {0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,true,true}, {0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,true,true},
 {0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,true,true}, {0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,true,true},
 {0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,true,true}, {0x0648,0xFEED,0xFEEE,0,0,false,true},
 {0x0649,0xFEEF,0xFEF0,0,0,false,true}, {0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,true,true}
};
static const ArabicForm *arabicForm(uint32_t cp) {
    for (size_t i=0;i<sizeof(ARABIC)/sizeof(ARABIC[0]);++i) if (ARABIC[i].cp==cp) return &ARABIC[i];
    return NULL;
}
static std::vector<uint32_t> decodeUtf8(const char *s) {
    std::vector<uint32_t> out;
    const unsigned char *p=(const unsigned char*)s;
    while (*p) {
        uint32_t cp; int n;
        if (*p<0x80) { cp=*p; n=1; }
        else if ((*p&0xE0)==0xC0) { cp=*p&0x1F; n=2; }
        else if ((*p&0xF0)==0xE0) { cp=*p&0x0F; n=3; }
        else if ((*p&0xF8)==0xF0) { cp=*p&0x07; n=4; }
        else { ++p; continue; }
        for (int i=1;i<n && p[i];++i) cp=(cp<<6)|(p[i]&0x3F);
        out.push_back(cp); p+=n;
    }
    return out;
}
static uint32_t shapeArabic(const std::vector<uint32_t>& v, size_t i) {
    const ArabicForm *cur=arabicForm(v[i]);
    if (!cur) return v[i];
    const ArabicForm *prev=i ? arabicForm(v[i-1]) : NULL;
    const ArabicForm *next=i+1<v.size() ? arabicForm(v[i+1]) : NULL;
    bool joinsPrev=prev && prev->joinNext && cur->joinPrev;
    bool joinsNext=next && cur->joinNext && next->joinPrev;
    if (joinsPrev && joinsNext && cur->medialForm) return cur->medialForm;
    if (joinsPrev && cur->finalForm) return cur->finalForm;
    if (joinsNext && cur->initialForm) return cur->initialForm;
    return cur->isolated;
}
static void drawArabic(Scene2D *scene, FT_Face face, const char *text, int rightEdge, int baseline, Color fg) {
    std::vector<uint32_t> cps=decodeUtf8(text);
    std::vector<uint32_t> shaped(cps.size());
    for (size_t i=0;i<cps.size();++i) shaped[i]=shapeArabic(cps,i);
    int pen=rightEdge;
    for (int i=(int)shaped.size()-1;i>=0;--i) {
        FT_UInt gi=FT_Get_Char_Index(face,shaped[i]);
        if (FT_Load_Glyph(face,gi,FT_LOAD_DEFAULT)!=0 || FT_Render_Glyph(face->glyph,ft_render_mode_normal)!=0) continue;
        FT_GlyphSlot slot=face->glyph;
        pen-=slot->advance.x>>6;
        for (int y=0;y<slot->bitmap.rows;++y) for (int x=0;x<slot->bitmap.width;++x) {
            int dx=pen+x+slot->bitmap_left, dy=baseline+y-slot->bitmap_top;
            if (dx<0 || dy<0 || dx>=W || dy>=H) continue;
            uint8_t alpha=slot->bitmap.buffer[y*slot->bitmap.pitch+x];
            if (!alpha) continue;
            Color c={(uint8_t)(fg.r*alpha/255),(uint8_t)(fg.g*alpha/255),(uint8_t)(fg.b*alpha/255)};
            scene->DrawPixel(dx,dy,c);
        }
    }
}
static void text(Scene2D *s, FT_Face f, const char *t, int x, int y, Color c) {
    s->DrawText((char*)t,f,x,y,BG,c);
}
static void renderBase(Scene2D *s, PNG *left, PNG *right, FT_Face latin) {
    s->FrameBufferFill(BG);
    s->DrawRectangle(0,0,W,5,RED);
    s->DrawRectangle(0,H-6,W,3,{70,18,24});
    left->Draw(s,48,300); right->Draw(s,1488,300);
    s->DrawRectangle(504,210,912,682,PANEL);
    s->DrawRectangle(504,210,912,4,RED);
    s->DrawRectangle(504,210,4,682,{78,22,28});
    s->DrawRectangle(1412,210,4,682,{78,22,28});
    text(s,latin,"GTXMOD MENU",730,135,WHITE);
    text(s,latin,"PS4 FILE INSTALLER",790,185,MUTED);
}
static void renderMenu(Scene2D *s, PNG *left, PNG *right, FT_Face latin, FT_Face arabic, int selected) {
    renderBase(s,left,right,latin);
    drawArabic(s,arabic,"اختر إصدارك",1355,300,WHITE);
    const char *versions[]={"1.53","1.56","1.57"};
    const char *state[]={"FILES READY","NO FILES","NO FILES"};
    for (int i=0;i<3;++i) {
        int y=350+i*158;
        s->DrawRectangle(570,y,780,126,i==selected?ROW_SELECTED:ROW);
        s->DrawRectangle(570,y,7,126,i==selected?RED:Color{63,69,78});
        if (i==selected) s->DrawRectangle(570,y+122,780,4,RED);
        text(s,latin,versions[i],625,y+85,i==selected?WHITE:MUTED);
        text(s,latin,state[i],925,y+77,i==0?GREEN:MUTED);
        if (i>0) drawArabic(s,arabic,"لا توجد ملفات",1330,y+76,MUTED);
    }
    text(s,latin,"UP / DOWN  MOVE      X  SELECT      O  BACK",650,940,WHITE);
}
static void renderSubscreen(Scene2D *s, PNG *left, PNG *right, FT_Face latin, FT_Face arabic, int screen, const int results[3]) {
    renderBase(s,left,right,latin);
    if (screen==1) {
        drawArabic(s,arabic,"الملفات جاهزة",1350,350,WHITE);
        text(s,latin,"VERSION 1.53",720,430,WHITE);
        text(s,latin,"GTXMOD153.PRX  +  PLUGINS.INI  +  GTX",620,510,MUTED);
        s->DrawRectangle(680,590,560,112,ROW_SELECTED);
        s->DrawRectangle(680,590,6,112,RED);
        text(s,latin,"X  INSTALL",835,663,WHITE);
        text(s,latin,"O  BACK",895,785,MUTED);
    } else if (screen==2) {
        drawArabic(s,arabic,"لا توجد ملفات",1350,390,WHITE);
        text(s,latin,"THE SELECTED VERSION HAS NO FILES YET",595,500,MUTED);
        text(s,latin,"X / O  BACK",845,680,WHITE);
    } else if (screen==3) {
        drawArabic(s,arabic,"جاري التثبيت",1350,410,WHITE);
        text(s,latin,"PLEASE WAIT WHILE FILES ARE COPIED",630,520,MUTED);
        s->DrawRectangle(680,620,560,20,ROW);
        s->DrawRectangle(680,620,390,20,RED);
    } else {
        drawArabic(s,arabic,"نتيجة التثبيت",1350,340,WHITE);
        const char *names[]={"STEP 1  GTXMOD153.PRX","STEP 2  PLUGINS.INI","STEP 3  GTX FOLDER"};
        for (int i=0;i<3;++i) {
            int y=420+i*100;
            s->DrawRectangle(620,y,680,72,ROW);
            text(s,latin,names[i],650,y+49,WHITE);
            text(s,latin,results[i]==0?"OK":"FAILED",1110,y+49,results[i]==0?GREEN:BAD);
        }
        text(s,latin,"O  BACK TO VERSIONS",785,810,WHITE);
    }
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    Scene2D scene(W,H,4);
    if (!scene.Init(0xC000000,2)) return 1;
    // OpenOrbis' Scene2D helper does not initialize this field.
    // Force the first valid framebuffer before the first draw.
    scene.SetActiveFrameBuffer(0);
    FT_Face latin=NULL, arabic=NULL;
    if (!scene.InitFont(&latin,"/app0/assets/fonts/Gontserrat-Regular.ttf",58)) return 2;
    if (!scene.InitFont(&arabic,"/app0/assets/fonts/NotoSansArabic-Regular.ttf",58)) return 3;
    PNG left("/app0/assets/images/GTX-side.png");
    PNG right("/app0/assets/images/GTX-side.png");
    if (scePadInit()<0) return 4;
    OrbisUserServiceInitializeParams userParams; memset(&userParams,0,sizeof(userParams));
    userParams.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    if (sceUserServiceInitialize(&userParams)<0) return 5;
    int userId=-1; if (sceUserServiceGetInitialUser(&userId)<0) return 6;
    int pad=scePadOpen(userId,ORBIS_PAD_PORT_TYPE_STANDARD,0,NULL); if (pad<0) return 7;
    int selected=0, screen=0, results[3]={-1,-1,-1};
    uint32_t previous=0; int frame=0; bool running=true, redraw=true;
    while (running) {
        OrbisPadData data; memset(&data,0,sizeof(data));
        if (scePadReadState(pad,&data)>=0) {
            uint32_t current=data.buttons;
            uint32_t pressed=current & ~previous;
            previous=current;
            if (screen==0) {
                if (pressed & ORBIS_PAD_BUTTON_UP) { selected=(selected+2)%3; redraw=true; }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) { selected=(selected+1)%3; redraw=true; }
                // The target console reports the raw constants opposite to the
                // physical labels. Keep physical X as select and O as back.
                if (pressed & ORBIS_PAD_BUTTON_CROSS) running=false;
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) {
                    if (selected==0) screen=installer_153_ready()?1:2;
                    else screen=2;
                    redraw=true;
                }
            } else if (screen==1) {
                if (pressed & ORBIS_PAD_BUTTON_CROSS) { screen=0; redraw=true; }
                else if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { screen=3; redraw=true; }
            } else if (screen==2 || screen==4) {
                if (pressed & (ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE)) { screen=0; redraw=true; }
            }
        }
        if (!running) break;
        if (redraw) {
            if (screen==0) renderMenu(&scene,&left,&right,latin,arabic,selected);
            else renderSubscreen(&scene,&left,&right,latin,arabic,screen,results);
            scene.SubmitFlip(frame); scene.FrameWait(frame); scene.FrameBufferSwap(); ++frame;
            redraw=false;
            if (screen==3) { installer_run_153(results); screen=4; redraw=true; }
        }
        usleep(16000);
    }
    scePadClose(pad);
    return 0;
}
