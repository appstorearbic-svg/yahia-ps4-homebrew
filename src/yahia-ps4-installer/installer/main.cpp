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

struct Labels {
    PNG *choose;
    PNG *noFiles;
    PNG *ready;
    PNG *installing;
    PNG *result;
};

static void text(Scene2D *s, FT_Face f, const char *t, int x, int y, Color c) {
    s->DrawText((char*)t, f, x, y, BG, c);
}

static void renderBase(Scene2D *s, PNG *left, PNG *right, FT_Face latin, Labels *a) {
    s->FrameBufferFill(BG);
    s->DrawRectangle(0, 0, W, 5, RED);
    s->DrawRectangle(0, H - 6, W, 3, {70, 18, 24});
    left->Draw(s, 48, 300);
    right->Draw(s, 1488, 300);
    s->DrawRectangle(504, 210, 912, 682, PANEL);
    s->DrawRectangle(504, 210, 912, 4, RED);
    s->DrawRectangle(504, 210, 4, 682, {78, 22, 28});
    s->DrawRectangle(1412, 210, 4, 682, {78, 22, 28});
    text(s, latin, "GTXMOD MENU", 730, 135, WHITE);
    text(s, latin, "PS4 FILE INSTALLER", 790, 185, MUTED);
    a->choose->Draw(s, 780, 245);
}

static void renderMenu(Scene2D *s, PNG *left, PNG *right, FT_Face latin, Labels *a, int selected) {
    renderBase(s, left, right, latin, a);
    const char *versions[] = {"1.53", "1.56", "1.57"};
    const char *state[] = {"FILES READY", "NO FILES", "NO FILES"};
    for (int i = 0; i < 3; ++i) {
        int y = 350 + i * 158;
        s->DrawRectangle(570, y, 780, 126, i == selected ? ROW_SELECTED : ROW);
        s->DrawRectangle(570, y, 7, 126, i == selected ? RED : Color{63, 69, 78});
        if (i == selected) s->DrawRectangle(570, y + 122, 780, 4, RED);
        text(s, latin, versions[i], 625, y + 85, i == selected ? WHITE : MUTED);
        text(s, latin, state[i], 925, y + 77, i == 0 ? GREEN : MUTED);
        if (i > 0) a->noFiles->Draw(s, 1010, y + 24);
    }
    text(s, latin, "UP / DOWN  MOVE       X  SELECT       O  BACK", 630, 940, WHITE);
}

static void renderSubscreen(Scene2D *s, PNG *left, PNG *right, FT_Face latin, Labels *a, int screen, const int results[3]) {
    renderBase(s, left, right, latin, a);
    if (screen == 1) {
        a->ready->Draw(s, 780, 330);
        text(s, latin, "VERSION 1.53", 720, 430, WHITE);
        text(s, latin, "GTXMOD153.PRX  +  PLUGINS.INI  +  GTX", 620, 510, MUTED);
        s->DrawRectangle(680, 590, 560, 112, ROW_SELECTED);
        s->DrawRectangle(680, 590, 6, 112, RED);
        text(s, latin, "X  INSTALL", 835, 663, WHITE);
        text(s, latin, "O  BACK", 895, 785, MUTED);
    } else if (screen == 2) {
        a->noFiles->Draw(s, 780, 370);
        text(s, latin, "THE SELECTED VERSION HAS NO FILES YET", 595, 500, MUTED);
        text(s, latin, "X / O  BACK", 845, 680, WHITE);
    } else if (screen == 3) {
        a->installing->Draw(s, 780, 390);
        text(s, latin, "PLEASE WAIT WHILE FILES ARE COPIED", 630, 520, MUTED);
        s->DrawRectangle(680, 620, 560, 20, ROW);
        s->DrawRectangle(680, 620, 390, 20, RED);
    } else {
        a->result->Draw(s, 780, 300);
        const char *names[] = {"STEP 1  GTXMOD153.PRX", "STEP 2  PLUGINS.INI", "STEP 3  GTX FOLDER"};
        for (int i = 0; i < 3; ++i) {
            int y = 420 + i * 100;
            s->DrawRectangle(620, y, 680, 72, ROW);
            text(s, latin, names[i], 650, y + 49, WHITE);
            text(s, latin, results[i] == 0 ? "OK" : "FAILED", 1110, y + 49, results[i] == 0 ? GREEN : BAD);
        }
        text(s, latin, "O  BACK TO VERSIONS", 785, 810, WHITE);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    Scene2D scene(W, H, 4);
    if (!scene.Init(0xC000000, 2)) return 1;
    // The OpenOrbis helper leaves this field undefined; select a valid buffer.
    scene.SetActiveFrameBuffer(0);

    FT_Face latin = NULL;
    if (!scene.InitFont(&latin, "/app0/assets/fonts/Gontserrat-Regular.ttf", 58)) return 2;

    PNG left("/app0/assets/images/GTX-side.png");
    PNG right("/app0/assets/images/GTX-side.png");
    PNG choose("/app0/assets/images/arabic/choose.png");
    PNG noFiles("/app0/assets/images/arabic/nofiles.png");
    PNG ready("/app0/assets/images/arabic/ready.png");
    PNG installing("/app0/assets/images/arabic/installing.png");
    PNG result("/app0/assets/images/arabic/result.png");
    Labels labels = {&choose, &noFiles, &ready, &installing, &result};

    if (scePadInit() < 0) return 4;
    OrbisUserServiceInitializeParams userParams;
    memset(&userParams, 0, sizeof(userParams));
    userParams.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    if (sceUserServiceInitialize(&userParams) < 0) return 5;
    int userId = -1;
    if (sceUserServiceGetInitialUser(&userId) < 0) return 6;
    int pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (pad < 0) return 7;

    int selected = 0, screen = 0, results[3] = {-1, -1, -1};
    uint32_t previous = 0;
    int frame = 0;
    bool running = true, redraw = true;

    while (running) {
        OrbisPadData data;
        memset(&data, 0, sizeof(data));
        if (scePadReadState(pad, &data) >= 0) {
            uint32_t current = data.buttons;
            uint32_t pressed = current & ~previous;
            previous = current;
            if (screen == 0) {
                if (pressed & ORBIS_PAD_BUTTON_UP) { selected = (selected + 2) % 3; redraw = true; }
                if (pressed & ORBIS_PAD_BUTTON_DOWN) { selected = (selected + 1) % 3; redraw = true; }
                // Physical X selects/enters; physical O exits or goes back.
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) running = false;
                if (pressed & ORBIS_PAD_BUTTON_CROSS) {
                    screen = selected == 0 && installer_153_ready() ? 1 : 2;
                    redraw = true;
                }
            } else if (screen == 1) {
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { screen = 0; redraw = true; }
                else if (pressed & ORBIS_PAD_BUTTON_CROSS) { screen = 3; redraw = true; }
            } else if (screen == 2 || screen == 4) {
                if (pressed & (ORBIS_PAD_BUTTON_CROSS | ORBIS_PAD_BUTTON_CIRCLE)) { screen = 0; redraw = true; }
            }
        }
        if (!running) break;
        if (redraw) {
            if (screen == 0) renderMenu(&scene, &left, &right, latin, &labels, selected);
            else renderSubscreen(&scene, &left, &right, latin, &labels, screen, results);
            scene.SubmitFlip(frame);
            scene.FrameWait(frame);
            scene.FrameBufferSwap();
            ++frame;
            redraw = false;
            if (screen == 3) { installer_run_153(results); screen = 4; redraw = true; }
        }
        usleep(16000);
    }
    scePadClose(pad);
    return 0;
}
