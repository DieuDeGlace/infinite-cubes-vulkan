#pragma once
#include "gfx/QuestMarker.h"
#include "gfx/VulkanContext.h"
#include "gfx/VulkanWorldData.h"
#include <FreeImage.h>
#include <algorithm>
#include <memory>

namespace gfx {
#include "QuestIconPng.h"

constexpr int QuestAtlasWidth = 320;
constexpr int QuestAtlasHeight = 80;

inline ImagePtr loadQuestIcons(VulkanContext &context)
{
    std::unique_ptr<FIMEMORY, decltype(&FreeImage_CloseMemory)> png(
        FreeImage_OpenMemory(const_cast<unsigned char *>(QuestIconPng), sizeof(QuestIconPng)), FreeImage_CloseMemory);
    if (!png)
        return {};
    std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> bitmap(FreeImage_LoadFromMemory(FIF_PNG, png.get(), 0),
                                                                  FreeImage_Unload);
    if (!bitmap)
        return {};
    std::unique_ptr<FIBITMAP, decltype(&FreeImage_Unload)> rgba(FreeImage_ConvertTo32Bits(bitmap.get()),
                                                                FreeImage_Unload);
    if (!rgba || FreeImage_GetWidth(rgba.get()) != QuestAtlasWidth ||
        FreeImage_GetHeight(rgba.get()) != QuestAtlasHeight)
        return {};

    std::vector<plasma::Vector<4, unsigned char>> pixels(QuestAtlasWidth * QuestAtlasHeight);
    for (int y = 0; y < QuestAtlasHeight; ++y) {
        const auto *pixel = FreeImage_GetScanLine(rgba.get(), QuestAtlasHeight - 1 - y);
        for (int x = 0; x < QuestAtlasWidth; ++x, pixel += 4) {
            const auto red = pixel[FI_RGBA_RED];
            const auto green = pixel[FI_RGBA_GREEN];
            const auto blue = pixel[FI_RGBA_BLUE];
            auto &output = pixels[y * QuestAtlasWidth + x];
            if (std::max(red, blue) * 13 > green * 10)
                output = {red, green, blue, pixel[FI_RGBA_ALPHA]};
            else
                output = {255, 255, 255, 0};
        }
    }
    return context.createTexture(QuestAtlasWidth, QuestAtlasHeight, pixels.data());
}

inline void appendQuestIcon(QuestIcon icon, const plasma::Vector<4, float> &clip, int width, int height,
                            std::vector<TexturedVertex> &vertices, std::vector<uint32_t> &indices)
{
    const plasma::Vector<4, unsigned char> white(255, 255, 255, 255);
    const bool giver = icon == QuestIcon::Giver;
    const int iconX = giver ? 188 : icon == QuestIcon::Objective ? 96 : 16;
    constexpr int IconY = 16;
    const int iconWidth = giver ? 24 : 48;
    const int iconHeight = giver ? 48 : 52;
    const float halfWidth = clip[3] * iconWidth * 0.5f / width;
    const float top = clip[1] + clip[3] * iconHeight / height;
    const float u0 = float(iconX) / QuestAtlasWidth;
    const float u1 = float(iconX + iconWidth) / QuestAtlasWidth;
    const float v0 = float(IconY) / QuestAtlasHeight;
    const float v1 = float(IconY + iconHeight) / QuestAtlasHeight;
    const auto base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{clip[0] - halfWidth, top, clip[2], clip[3]}, white, {u0, v0}});
    vertices.push_back({{clip[0] + halfWidth, top, clip[2], clip[3]}, white, {u1, v0}});
    vertices.push_back({{clip[0] + halfWidth, clip[1], clip[2], clip[3]}, white, {u1, v1}});
    vertices.push_back({{clip[0] - halfWidth, clip[1], clip[2], clip[3]}, white, {u0, v1}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}
} // namespace gfx
