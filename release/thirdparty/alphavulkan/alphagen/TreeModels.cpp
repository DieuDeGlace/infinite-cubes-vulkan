#include "alphagen/TreeModels.h"
#include <fstream>

namespace alphagen {
namespace {
bool loadPalm(const std::filesystem::path &path, PalmModel &model) {
    std::ifstream file(path, std::ios::binary);
    int32_t size[3];
    if (!file.read(reinterpret_cast<char *>(size), sizeof(size)))
        return false;
    for (const auto dim : size)
        if (dim <= 0 || dim > 1024)
            return false;
    const auto count = std::streamoff(size[0]) * size[1] * size[2] * 3;
    file.seekg(0, std::ios::end);
    if (file.tellg() != std::streamoff(sizeof(size)) + count)
        return false;
    file.seekg(sizeof(size));
    model.rgb.resize(size_t(count));
    if (!file.read(reinterpret_cast<char *>(model.rgb.data()), count))
        return false;
    model.sx = size[0];
    model.sy = size[1];
    model.sz = size[2];
    return true;
}
} // namespace

std::shared_ptr<const TreeModels> loadTreeModels(const std::filesystem::path &sprites, std::string &error) {
    error.clear();
    auto models = std::make_shared<TreeModels>();
    if (!loadPalm(sprites / "palm-leaf.cub", models->cardinal) || !loadPalm(sprites / "palm-leaf-diagonal.cub", models->diagonal)) {
        error = "Alpha terrain requires valid palm-leaf.cub and palm-leaf-diagonal.cub in sprites/";
        return {};
    }
    return models;
}
} // namespace alphagen
