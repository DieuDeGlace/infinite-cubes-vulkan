#pragma once

#include "gfx/VulkanContext.h"
#include "plasma/Drawing.h"

namespace plasma {

class VulkanEngine;

class VulkanDrawing final : public Drawing {
public:
    explicit VulkanDrawing(VulkanEngine *engine);
    void update() override;
    void draw(Node *node, const Vector<4, float> &color) override;

private:
    struct Vertex {
        Vector<2, float> position;
        Vector<2, float> textureCoordinate;
        Vector<4, float> color;
    };
    static_assert(sizeof(Vertex) == 32 && offsetof(Vertex, color) == 16);

    VulkanEngine *m_engine;
    gfx::BufferPtr m_vertexBuffer;
    gfx::BufferPtr m_indexBuffer;
    uint32_t m_indexCount = 0;
};

}
