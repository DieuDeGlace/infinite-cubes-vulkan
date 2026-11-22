#include "gfx/VulkanGraphics.h"

#include "gfx/VulkanContext.h"

namespace gfx {
namespace {

class VulkanVertexBuffer final : public VertexBuffer {
public:
    VulkanVertexBuffer(VulkanContext &context, BufferPtr buffer)
        : m_context(context),
          m_buffer(std::move(buffer))
    {}
    bool bind() override
    {
        m_context.bindVertex(m_buffer);
        return bool(m_buffer);
    }

private:
    VulkanContext &m_context;
    BufferPtr m_buffer;
};

class VulkanIndexBuffer final : public IndexBuffer {
public:
    VulkanIndexBuffer(VulkanContext &context, BufferPtr buffer, VkIndexType type)
        : m_context(context),
          m_buffer(std::move(buffer)),
          m_type(type)
    {}
    bool bind() override
    {
        m_context.bindIndex(m_buffer, m_type);
        return bool(m_buffer);
    }

private:
    VulkanContext &m_context;
    BufferPtr m_buffer;
    VkIndexType m_type;
};

}

static_assert(sizeof(Vertex) == 8 && offsetof(Vertex, color) == 4);

VulkanGraphics::VulkanGraphics(VulkanContext &context)
    : m_context(context)
{}

VertexBuffer *VulkanGraphics::createVertexBuffer(const std::vector<Vertex> &vertices)
{
    return new VulkanVertexBuffer(m_context, m_context.createBuffer(vertices.data(), vertices.size() * sizeof(Vertex),
                                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
}

IndexBuffer *VulkanGraphics::createIndexBuffer32(const std::vector<unsigned int> &indices)
{
    return new VulkanIndexBuffer(
        m_context,
        m_context.createBuffer(indices.data(), indices.size() * sizeof(unsigned int), VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
        VK_INDEX_TYPE_UINT32);
}

IndexBuffer *VulkanGraphics::createIndexBuffer16(const std::vector<unsigned short> &indices)
{
    return new VulkanIndexBuffer(m_context,
                                 m_context.createBuffer(indices.data(), indices.size() * sizeof(unsigned short),
                                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                                 VK_INDEX_TYPE_UINT16);
}

void VulkanGraphics::drawIndexed(int, int triangleCount)
{
    m_context.drawIndexed(static_cast<uint32_t>(triangleCount) * 3);
}

}
