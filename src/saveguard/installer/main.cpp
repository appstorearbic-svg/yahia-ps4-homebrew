#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "graphics.h"
#include <sstream>
#include "graphics.h"
#include "png.h"

std::stringstream debugLogStream;
Scene2D::~Scene2D() {}

#define W 1920
#define H 1080

const Color BG={15,18,25}, PANEL={25,30,40}, ROW={35,40,50}, SEL={50,60,80}, RED={220,50,60}, WHITE={255,255,255}, MUTED={150,155,165}, BLUE={0,150,255};

struct GI { const char* cusa; const char* name; } KG[] = {
    {"CUSA00411", "GTA V"}, {"CUSA00419", "GTA V"},
    {"CUSA13197", "RDR 2"}, {"CUSA34180", "FC 24"}, {0, 0}
};
struct IG { char cusa[16], name[64]; long mb; } IGs[32];
int IC = 0;

void scan() {
    IC = 0;
    DIR* d = opendir("/user/home/00000001/savedata");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && IC < 32) {
        if (e->d_type != DT_DIR || strncmp(e->d_name, "CUSA", 4)) continue;
        for (int i = 0; KG[i].cusa; i++) {
            if (!strcmp(e->d_name, KG[i].cusa)) {
                strncpy(IGs[IC].cusa, e->d_name, 15);
                strncpy(IGs[IC].name, KG[i].name, 63);
                IGs[IC].mb = 45;
                IC++;
                break;
            }
        }
    }
    closedir(d);
}

static void txt(Scene2D* s, FT_Face f, const char* t, int x, int y, Color c) {
    s->DrawText((char*)t, f, x, y, BG, c);
}

static void main_menu(Scene2D* s, FT_Face en, int sel) {
    s->FrameBufferFill(BG);
    s->DrawRectangle(0, 0, W, 5, RED);
    s->DrawRectangle(504, 210, 912, 682, PANEL);
    s->DrawRectangle(504, 210, 912, 4, RED);
    txt(s, en, "SAVEGUARD", 780, 135, RED);
    txt(s, en, "PS4 SAVE MANAGER", 720, 185, WHITE);
    const char* o[] = {"Scan Games", "Settings", "About"};
    for (int i = 0; i < 3; i++) {
        int y = 300 + i * 158;
        s->DrawRectangle(570, y, 780, 126, i == sel ? SEL : ROW);
        s->DrawRectangle(570, y, 7, 126, i == sel ? RED : Color{63, 69, 78});
        txt(s, en, o[i], 625, y + 85, i == 0 ? BLUE : (i == sel ? WHITE : MUTED));
    }
    txt(s, en, "X  SELECT       O  EXIT", 780, 940, WHITE);
}

static void scan_screen(Scene2D* s, FT_Face en, int p) {
    s->FrameBufferFill(BG);
    s->DrawRectangle(504, 210, 912, 682, PANEL);
    txt(s, en, "SCANNING...", 820, 350, BLUE);
    s->DrawRectangle(620, 500, 680, 40, ROW);
    s->DrawRectangle(620, 500, (680 * p) / 100, 40, BLUE);
    char t[16];
    snprintf(t, 16, "%d%%", p);
    txt(s, en, t, 930, 580, WHITE);
}

static void games_list(Scene2D* s, FT_Face en, int sel) {
    s->FrameBufferFill(BG);
    s->DrawRectangle(504, 210, 912, 682, PANEL);
    txt(s, en, "INSTALLED GAMES", 700, 280, BLUE);
    int v = IC < 3 ? IC : 3;
    for (int i = 0; i < v; i++) {
        int y = 350 + i * 140;
        s->DrawRectangle(570, y, 780, 110, i == sel ? SEL : ROW);
        s->DrawRectangle(570, y, 7, 110, i == sel ? RED : Color{63, 69, 78});
        txt(s, en, IGs[i].name, 700, y + 45, i == sel ? WHITE : MUTED);
        char t[64];
        snprintf(t, 64, "%s | %ld MB", IGs[i].cusa, IGs[i].mb);
        txt(s, en, t, 700, y + 85, MUTED);
    }
    txt(s, en, "X  SELECT       O  BACK", 780, 940, WHITE);
}

int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    Scene2D sc(W, H, 4);
    if (!sc.Init(0xC000000, 2)) return 1;
    sc.SetActiveFrameBuffer(0);
    FT_Face en = 0;
    if (!sc.InitFont(&en, "/app0/assets/fonts/Gontserrat-Regular.ttf", 58)) return 2;
    if (scePadInit() < 0) return 4;
    OrbisUserServiceInitializeParams up = {};
    up.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    if (sceUserServiceInitialize(&up) < 0) return 5;
    int uid = -1;
    sceUserServiceGetInitialUser(&uid);
    int pad = scePadOpen(uid, ORBIS_PAD_PORT_TYPE_STANDARD, 0, 0);
    if (pad < 0) return 7;
    
    int scr = 0, sel = 0, prog = 0;
    uint32_t prev = 0;
    bool run = true, redraw = true;
    int frame = 0;
    
    while (run) {
        OrbisPadData d = {};
        if (scePadReadState(pad, &d) >= 0) {
            uint32_t cur = d.buttons, pr = cur & ~prev;
            prev = cur;
            if (scr == 0) {
                if (pr & ORBIS_PAD_BUTTON_UP) sel = (sel + 2) % 3, redraw = true;
                if (pr & ORBIS_PAD_BUTTON_DOWN) sel = (sel + 1) % 3, redraw = true;
                if (pr & ORBIS_PAD_BUTTON_CIRCLE) run = false;
                if (pr & ORBIS_PAD_BUTTON_CROSS && sel == 0) { scr = 1; prog = 0; redraw = true; }
            } else if (scr == 1) {
                prog += 10;
                if (prog >= 100) { scan(); scr = 2; sel = 0; }
                redraw = true;
            } else if (scr == 2) {
                if (pr & ORBIS_PAD_BUTTON_UP) sel = (sel + IC - 1) % IC, redraw = true;
                if (pr & ORBIS_PAD_BUTTON_DOWN) sel = (sel + 1) % IC, redraw = true;
                if (pr & ORBIS_PAD_BUTTON_CIRCLE) { scr = 0; redraw = true; }
            }
        }
        if (!run) break;
        if (redraw) {
            if (scr == 0) main_menu(&sc, en, sel);
            else if (scr == 1) scan_screen(&sc, en, prog);
            else games_list(&sc, en, sel);
            sc.SubmitFlip(frame);
            sc.FrameWait(frame);
            sc.FrameBufferSwap();
            ++frame;
            redraw = false;
            if (scr == 1) usleep(100000);
            else usleep(16000);
        }
    }
    scePadClose(pad);
    return 0;
}
