#pragma once

#include "gfx/Graphics.h"

namespace gfx {

class VulkanContext;

class VulkanGraphics final : public Graphics {
public:
    explicit VulkanGraphics(VulkanContext &context);
    VertexBuffer *createVertexBuffer(const std::vector<Vertex> &vertices) override;
    IndexBuffer *createIndexBuffer32(const std::vector<unsigned int> &indices) override;
    IndexBuffer *createIndexBuffer16(const std::vector<unsigned short> &indices) override;
    void drawIndexed(int vertexCount, int triangleCount) override;

private:
    VulkanContext &m_context;
};

}
