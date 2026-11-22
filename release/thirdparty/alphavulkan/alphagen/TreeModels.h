#pragma once
// Owner-supplied palm leaves. Model bytes never belong to source or release packages.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace alphagen {
struct PalmModel {
    int32_t sx = 0, sy = 0, sz = 0;
    std::vector<uint8_t> rgb;

    const uint8_t *color(int x, int y, int z) const { return &rgb[((size_t(z) * sy + y) * sx + x) * 3]; }
    bool solid(int x, int y, int z) const {
        const auto *c = color(x, y, z);
        return c[0] || c[1] || c[2];
    }
};
struct TreeModels {
    PalmModel cardinal, diagonal;
};
std::shared_ptr<const TreeModels> loadTreeModels(const std::filesystem::path &sprites, std::string &error);
} // namespace alphagen
