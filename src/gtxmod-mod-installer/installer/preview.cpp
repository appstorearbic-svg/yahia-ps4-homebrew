#include <stb/stb_image.h>
#include "preview.hpp"

PreviewImage::PreviewImage(const char *path) : width(0), height(0), pixels(nullptr) {
    int channels = 0;
    pixels = stbi_load(path, &width, &height, &channels, 4);
}

PreviewImage::~PreviewImage() {
    if (pixels) stbi_image_free(pixels);
}

void PreviewImage::draw(Scene2D *scene, int x, int y, int w, int h) const {
    if (!valid() || w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; ++dy) {
        int sy = (dy * height) / h;
        for (int dx = 0; dx < w; ++dx) {
            int sx = (dx * width) / w;
            const unsigned char *p = pixels + ((sy * width + sx) * 4);
            scene->DrawPixel(x + dx, y + dy, Color{p[0], p[1], p[2]});
        }
    }
}
