#define GRAPHICS_USES_FONT
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include "graphics.h"
#include "png.h"

std::stringstream debugLogStream;
Scene2D::~Scene2D() {}

static const int W=1920,H=1080;
static const Color BG={7,9,13}, PANEL={16,19,25}, ROW={25,29,36};
static const Color SELECTED={50,14,18}, RED={225,32,48}, WHITE={239,242,246};
static const Color MUTED={144,151,160};

struct ArabicForm { uint32_t cp, iso, fin, ini, med; bool joinNext, joinPrev; };
static const ArabicForm A[] = {
 {0x0621,0xFE80,0,0,0,0,0},{0x0622,0xFE81,0xFE82,0,0,0,1},{0x0623,0xFE83,0xFE84,0,0,0,1},{0x0625,0xFE87,0xFE88,0,0,0,1},{0x0627,0xFE8D,0xFE8E,0,0,0,1},{0x0628,0xFE8F,0xFE90,0xFE91,0xFE92,1,1},{0x0629,0xFE93,0xFE94,0,0,0,1},{0x062A,0xFE95,0xFE96,0xFE97,0xFE98,1,1},{0x062B,0xFE99,0xFE9A,0xFE9B,0xFE9C,1,1},{0x062C,0xFE9D,0xFE9E,0xFE9F,0xFEA0,1,1},{0x062D,0xFEA1,0xFEA2,0xFEA3,0xFEA4,1,1},{0x062E,0xFEA5,0xFEA6,0xFEA7,0xFEA8,1,1},{0x062F,0xFEA9,0xFEAA,0,0,0,1},{0x0630,0xFEAB,0xFEAC,0,0,0,1},{0x0631,0xFEAD,0xFEAE,0,0,0,1},{0x0632,0xFEAF,0xFEB0,0,0,0,1},{0x0633,0xFEB1,0xFEB2,0xFEB3,0xFEB4,1,1},{0x0634,0xFEB5,0xFEB6,0xFEB7,0xFEB8,1,1},{0x0635,0xFEB9,0xFEBA,0xFEBB,0xFEBC,1,1},{0x0636,0xFEBD,0xFEBE,0xFEBF,0xFEC0,1,1},{0x0637,0xFEC1,0xFEC2,0xFEC3,0xFEC4,1,1},{0x0638,0xFEC5,0xFEC6,0xFEC7,0xFEC8,1,1},{0x0639,0xFEC9,0xFECA,0xFECB,0xFECC,1,1},{0x063A,0xFECD,0xFECE,0xFECF,0xFED0,1,1},{0x0641,0xFED1,0xFED2,0xFED3,0xFED4,1,1},{0x0642,0xFED5,0xFED6,0xFED7,0xFED8,1,1},{0x0643,0xFED9,0xFEDA,0xFEDB,0xFEDC,1,1},{0x0644,0xFEDD,0xFEDE,0xFEDF,0xFEE0,1,1},{0x0645,0xFEE1,0xFEE2,0xFEE3,0xFEE4,1,1},{0x0646,0xFEE5,0xFEE6,0xFEE7,0xFEE8,1,1},{0x0647,0xFEE9,0xFEEA,0xFEEB,0xFEEC,1,1},{0x0648,0xFEED,0xFEEE,0,0,0,1},{0x0649,0xFEEF,0xFEF0,0,0,0,1},{0x064A,0xFEF1,0xFEF2,0xFEF3,0xFEF4,1,1}
};
static const ArabicForm *form(uint32_t cp){for(size_t i=0;i<sizeof(A)/sizeof(A[0]);++i)if(A[i].cp==cp)return &A[i];return 0;}
static uint32_t shape(const uint32_t *v,size_t n,size_t i){const ArabicForm*c=form(v[i]);if(!c)return v[i];const ArabicForm*p=i?form(v[i-1]):0,*q=i+1<n?form(v[i+1]):0;bool a=p&&p->joinNext&&c->joinPrev,b=q&&c->joinNext&&q->joinPrev;if(a&&b&&c->med)return c->med;if(a&&c->fin)return c->fin;if(b&&c->ini)return c->ini;return c->iso;}
static size_t utf8(const char*s,uint32_t*out,size_t cap){size_t n=0;const unsigned char*p=(const unsigned char*)s;while(*p&&n<cap){uint32_t c;int k;if(*p<128){c=*p;k=1;}else if((*p&224)==192){c=*p&31;k=2;}else if((*p&240)==224){c=*p&15;k=3;}else{c=*p&7;k=4;}for(int i=1;i<k;++i)c=(c<<6)|(p[i]&63);out[n++]=c;p+=k;}return n;}
static void arabic(Scene2D*s,FT_Face f,const char*str,int right,int base,Color color){uint32_t v[128];size_t n=utf8(str,v,128);int pen=right;for(size_t i=0;i<n;++i){FT_UInt gi=FT_Get_Char_Index(f,shape(v,n,i));if(FT_Load_Glyph(f,gi,FT_LOAD_DEFAULT)||FT_Render_Glyph(f->glyph,ft_render_mode_normal))continue;FT_GlyphSlot g=f->glyph;pen-=g->advance.x>>6;for(int y=0;y<g->bitmap.rows;++y)for(int x=0;x<g->bitmap.width;++x){int dx=pen+x+g->bitmap_left,dy=base+y-g->bitmap_top;if(dx<0||dy<0||dx>=W||dy>=H)continue;uint8_t a=g->bitmap.buffer[y*g->bitmap.pitch+x];if(a)s->DrawPixel(dx,dy,{(uint8_t)(color.r*a/255),(uint8_t)(color.g*a/255),(uint8_t)(color.b*a/255)});}}}
static void text(Scene2D*s,FT_Face f,const char*t,int x,int y,Color c){s->DrawText((char*)t,f,x,y,BG,c);}
static void draw(Scene2D*s,PNG*l,PNG*r,FT_Face en,FT_Face ar,int sel){s->FrameBufferFill(BG);s->DrawRectangle(0,0,W,5,RED);s->DrawRectangle(0,H-7,W,3,{70,18,24});l->Draw(s,48,220);r->Draw(s,1488,220);s->DrawRectangle(504,210,912,682,PANEL);s->DrawRectangle(504,210,912,4,RED);text(s,en,"GTXMOD MOD INSTALLER",665,135,RED);text(s,en,"SAFE BOOT",845,185,WHITE);arabic(s,ar,"مثبت مودات GTA V",1355,300,WHITE);const char*items[]={"START APPLICATION","MOD CATALOG","SETTINGS","EXIT"};for(int i=0;i<4;++i){int y=315+i*115;s->DrawRectangle(610,y,700,82,i==sel?SELECTED:ROW);if(i==sel)s->DrawRectangle(610,y,7,82,RED);text(s,en,items[i],670,y+54,i==sel?WHITE:MUTED);}text(s,en,"UP/DOWN SELECT       X OPEN       O EXIT",720,920,WHITE);arabic(s,ar,"تم تشغيل وضع الإقلاع الآمن",1330,980,MUTED);}

int main(){setvbuf(stdout,0,_IONBF,0);Scene2D scene(W,H,4);if(!scene.Init(0xC000000,2))return 1;scene.SetActiveFrameBuffer(0);FT_Face en=0,ar=0;if(!scene.InitFont(&en,"/app0/assets/fonts/Gontserrat-Regular.ttf",52))return 2;if(!scene.InitFont(&ar,"/app0/assets/fonts/NotoSansArabic-Regular.ttf",43))return 3;PNG left("/app0/assets/images/GTX-side.png"),right("/app0/assets/images/GTX-side.png");if(scePadInit()<0)return 4;OrbisUserServiceInitializeParams up;memset(&up,0,sizeof(up));up.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;if(sceUserServiceInitialize(&up)<0)return 5;int uid=-1;if(sceUserServiceGetInitialUser(&uid)<0)return 6;int pad=scePadOpen(uid,ORBIS_PAD_PORT_TYPE_STANDARD,0,0);if(pad<0)return 7;uint32_t prev=0;int sel=0,frame=0;bool run=true;draw(&scene,&left,&right,en,ar,sel);scene.SubmitFlip(frame);scene.FrameWait(frame);scene.FrameBufferSwap();++frame;while(run){OrbisPadData d;memset(&d,0,sizeof(d));if(scePadReadState(pad,&d)>=0){uint32_t pressed=d.buttons&~prev;prev=d.buttons;if(pressed&ORBIS_PAD_BUTTON_UP){if(sel>0)--sel;draw(&scene,&left,&right,en,ar,sel);scene.SubmitFlip(frame);scene.FrameWait(frame);scene.FrameBufferSwap();++frame;}else if(pressed&ORBIS_PAD_BUTTON_DOWN){if(sel<3)++sel;draw(&scene,&left,&right,en,ar,sel);scene.SubmitFlip(frame);scene.FrameWait(frame);scene.FrameBufferSwap();++frame;}else if(pressed&ORBIS_PAD_BUTTON_CIRCLE)run=false;}usleep(16000);}scePadClose(pad);return 0;}
