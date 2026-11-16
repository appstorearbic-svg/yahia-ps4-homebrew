#include <unistd.h>
#include "graphics.h"

Scene2D::~Scene2D() {}

int main(){
    Scene2D scene(1920,1080,4);
    if(!scene.Init(0x2000000,2)) return 1;
    scene.SetActiveFrameBuffer(0);
    scene.FrameBufferFill({8,10,16});
    scene.DrawRectangle(260,220,1400,520,{25,29,36});
    scene.DrawRectangle(260,220,1400,5,{225,32,48});
    scene.SubmitFlip(0);
    scene.FrameWait(0);
    scene.FrameBufferSwap();
    for(;;) usleep(16000);
    return 0;
}
