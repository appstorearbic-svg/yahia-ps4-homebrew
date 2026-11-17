#include <stb/stb_image.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <vector>
#include "preview.hpp"

static unsigned long long nowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
}

static bool isGif(const std::vector<unsigned char> &data) {
    return data.size() >= 6 &&
           (!memcmp(data.data(), "GIF87a", 6) || !memcmp(data.data(), "GIF89a", 6));
}

PreviewImage::PreviewImage(const char *path)
    : width(0), height(0), frameCount(1), delays(nullptr), pixels(nullptr), startedMs(nowMs()) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (length <= 0 || length > 32 * 1024 * 1024) { fclose(fp); return; }
    std::vector<unsigned char> data((size_t)length);
    if (fread(data.data(), 1, data.size(), fp) != data.size()) { fclose(fp); return; }
    fclose(fp);

    int channels = 0;
    if (isGif(data)) {
        int frames = 0;
        pixels = stbi_load_gif_from_memory(data.data(), (int)data.size(), &delays,
                                           &width, &height, &frames, &channels, 4);
        if (pixels && frames > 0) frameCount = frames;
    } else {
        pixels = stbi_load_from_memory(data.data(), (int)data.size(), &width, &height,
                                       &channels, 4);
    }
    if (!pixels) { width = height = 0; frameCount = 1; }
}

PreviewImage::~PreviewImage() {
    if (pixels) stbi_image_free(pixels);
    if (delays) stbi_image_free(delays);
}

void PreviewImage::draw(Scene2D *scene, int x, int y, int w, int h) const {
    if (!valid() || w <= 0 || h <= 0) return;
    int frame = 0;
    if (frameCount > 1 && delays) {
        unsigned long long elapsed = nowMs() - startedMs;
        unsigned long long total = 0;
        for (int i = 0; i < frameCount; ++i) total += (unsigned long long)(delays[i] > 0 ? delays[i] : 10) * 10ULL;
        if (total) {
            unsigned long long cursor = elapsed % total;
            for (int i = 0; i < frameCount; ++i) {
                unsigned long long duration = (unsigned long long)(delays[i] > 0 ? delays[i] : 10) * 10ULL;
                if (cursor < duration) { frame = i; break; }
                cursor -= duration;
            }
        }
    }
    const unsigned char *framePixels = pixels + ((size_t)frame * (size_t)width * (size_t)height * 4U);
    for (int dy = 0; dy < h; ++dy) {
        int sy = (dy * height) / h;
        for (int dx = 0; dx < w; ++dx) {
            int sx = (dx * width) / w;
            const unsigned char *p = framePixels + ((size_t)sy * (size_t)width + (size_t)sx) * 4U;
            scene->DrawPixel(x + dx, y + dy, Color{p[0], p[1], p[2]});
        }
    }
}

void PreviewImage::drawFit(Scene2D *scene, int x, int y, int w, int h) const {
    if (!valid() || w <= 0 || h <= 0) return;
    int dw = w;
    int dh = (width > 0) ? (w * height) / width : h;
    if (dh > h) {
        dh = h;
        dw = (height > 0) ? (h * width) / height : w;
    }
    int ox = x + (w - dw) / 2;
    int oy = y + (h - dh) / 2;
    draw(scene, ox, oy, dw, dh);
}
