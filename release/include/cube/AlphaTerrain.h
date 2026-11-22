#pragma once

#include <memory>
#include "cube/World.h"

namespace cube {

class AlphaTerrain {
public:
    // Keep the Alpha save profile and require matching relief rules for co-op.
    static constexpr const char *SaveDirectory = "Save/AlphaTerrain-v1";
    static constexpr int ProtocolVersion = 100265;

    AlphaTerrain();
    ~AlphaTerrain();

    // roadDistance is streetAmount's distance to the nearest road's centre line.
    float heightOffset(World &world, int x, int y, float height, const World::BiomeSample &biomes,
                       const World::AreaInfo &area, float roadDistance) const;

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};

}
