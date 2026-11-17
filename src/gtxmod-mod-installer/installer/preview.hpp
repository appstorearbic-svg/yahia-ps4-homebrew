#pragma once
#include "graphics.h"

class PreviewImage {
    int width;
    int height;
    int frameCount;
    int *delays;
    unsigned char *pixels;
    unsigned long long startedMs;
public:
    explicit PreviewImage(const char *path);
    ~PreviewImage();
    bool valid() const { return pixels != nullptr && width > 0 && height > 0; }
    bool animated() const { return valid() && frameCount > 1; }
    void draw(Scene2D *scene, int x, int y, int w, int h) const;
    void drawFit(Scene2D *scene, int x, int y, int w, int h) const;
};
