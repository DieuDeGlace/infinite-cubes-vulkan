// The FreeImage functions the game calls. FreeImageAndroid.cpp implements them with stb_image.
#pragma once

struct FIBITMAP;
struct FIMEMORY;

enum FREE_IMAGE_FORMAT {
    FIF_UNKNOWN = -1,
    FIF_BMP = 0,
    FIF_JPEG = 2,
    FIF_PNG = 13,
    FIF_PPM = 14,
    FIF_TARGA = 17,
    FIF_TIFF = 18
};

// Byte positions within a little-endian FreeImage pixel.
#define FI_RGBA_BLUE 0
#define FI_RGBA_GREEN 1
#define FI_RGBA_RED 2
#define FI_RGBA_ALPHA 3

extern "C" {
FIBITMAP *FreeImage_LoadU(int format, const wchar_t *path, int flags);
FIMEMORY *FreeImage_OpenMemory(unsigned char *data, unsigned size);
void FreeImage_CloseMemory(FIMEMORY *stream);
FIBITMAP *FreeImage_LoadFromMemory(int format, FIMEMORY *stream, int flags);
FIBITMAP *FreeImage_ConvertTo32Bits(FIBITMAP *bitmap);
unsigned FreeImage_GetWidth(FIBITMAP *bitmap);
unsigned FreeImage_GetHeight(FIBITMAP *bitmap);
// The bytes of pixel data in one scanline.
unsigned FreeImage_GetLine(FIBITMAP *bitmap);
// Row 0 is the bottom of the image.
unsigned char *FreeImage_GetScanLine(FIBITMAP *bitmap, int row);
void FreeImage_Unload(FIBITMAP *bitmap);
}
