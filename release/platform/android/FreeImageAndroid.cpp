// FreeImage's bitmap layout (bottom-up rows of BGR or BGRA bytes) over stb_image's decoders.
#include <FreeImage.h>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "AndroidGameData.h"
#include "AndroidPaths.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_PNM
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include <stb_image.h>

struct FIBITMAP {
    int width = 0;
    int height = 0;
    int channels = 0; // 3 or 4
    std::vector<unsigned char> pixels;
};

struct FIMEMORY {
    const unsigned char *data;
    unsigned size;
};

namespace {

// stb_image returns top-down RGB(A); FreeImage stores bottom-up BGR(A).
FIBITMAP *decode(const unsigned char *data, size_t size)
{
    int width = 0, height = 0, sourceChannels = 0;
    if (size > 0x7fffffff || !stbi_info_from_memory(data, int(size), &width, &height, &sourceChannels))
        return nullptr;
    const int channels = sourceChannels == 2 || sourceChannels == 4 ? 4 : 3;
    stbi_uc *decoded = stbi_load_from_memory(data, int(size), &width, &height, &sourceChannels, channels);
    if (!decoded)
        return nullptr;

    auto *bitmap = new FIBITMAP;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->channels = channels;
    bitmap->pixels.resize(size_t(width) * height * channels);
    const size_t line = size_t(width) * channels;
    for (int y = 0; y < height; ++y) {
        const stbi_uc *source = decoded + line * y;
        unsigned char *destination = bitmap->pixels.data() + line * (height - 1 - y);
        for (int x = 0; x < width; ++x, source += channels, destination += channels) {
            destination[FI_RGBA_RED] = source[0];
            destination[FI_RGBA_GREEN] = source[1];
            destination[FI_RGBA_BLUE] = source[2];
            if (channels == 4)
                destination[FI_RGBA_ALPHA] = source[3];
        }
    }
    stbi_image_free(decoded);
    return bitmap;
}

}

extern "C" {

FIBITMAP *FreeImage_LoadU(int, const wchar_t *path, int)
{
    const auto data = android::readGameData(android::hostPath(path));
    return data.empty() ? nullptr : decode(data.data(), data.size());
}

FIMEMORY *FreeImage_OpenMemory(unsigned char *data, unsigned size)
{
    return new FIMEMORY {data, size};
}

void FreeImage_CloseMemory(FIMEMORY *stream)
{
    delete stream;
}

FIBITMAP *FreeImage_LoadFromMemory(int, FIMEMORY *stream, int)
{
    return stream ? decode(stream->data, stream->size) : nullptr;
}

FIBITMAP *FreeImage_ConvertTo32Bits(FIBITMAP *bitmap)
{
    if (!bitmap)
        return nullptr;
    auto *converted = new FIBITMAP;
    converted->width = bitmap->width;
    converted->height = bitmap->height;
    converted->channels = 4;
    if (bitmap->channels == 4) {
        converted->pixels = bitmap->pixels;
        return converted;
    }
    converted->pixels.reserve(bitmap->pixels.size() / 3 * 4);
    for (size_t i = 0; i < bitmap->pixels.size(); i += 3) {
        converted->pixels.insert(converted->pixels.end(),
                                 {bitmap->pixels[i], bitmap->pixels[i + 1], bitmap->pixels[i + 2], 255});
    }
    return converted;
}

unsigned FreeImage_GetWidth(FIBITMAP *bitmap)
{
    return bitmap ? bitmap->width : 0;
}

unsigned FreeImage_GetHeight(FIBITMAP *bitmap)
{
    return bitmap ? bitmap->height : 0;
}

unsigned FreeImage_GetLine(FIBITMAP *bitmap)
{
    return bitmap ? bitmap->width * bitmap->channels : 0;
}

unsigned char *FreeImage_GetScanLine(FIBITMAP *bitmap, int row)
{
    if (!bitmap || row < 0 || row >= bitmap->height)
        return nullptr;
    return bitmap->pixels.data() + size_t(row) * bitmap->width * bitmap->channels;
}

void FreeImage_Unload(FIBITMAP *bitmap)
{
    delete bitmap;
}
}
