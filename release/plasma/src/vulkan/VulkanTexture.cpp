#include "gfx/VulkanContext.h"
#include "plasma/Texture.h"

namespace plasma {

namespace {

class VulkanTexture final : public Texture {
public:
    VulkanTexture(Engine *engine, gfx::VulkanContext &context, int width, int height, const unsigned char *pixels,
                  const TextureFormat &format, const std::wstring &name, bool transient)
        : Texture(engine, name, transient), m_context(context)
    {
        setPixels(width, height, pixels, format);
    }

    void bind(unsigned slot) override
    {
        if (!m_image && !m_pixels.empty()) {
            upload(m_pixels.data());
            if (m_transient)
                m_pixels.clear();
        }
        m_context.bindTexture(slot, m_image);
    }

    void upload(const unsigned char *pixels) override
    {
        m_image = m_context.createTexture(m_width, m_height, pixels);
    }

private:
    gfx::VulkanContext &m_context;
    gfx::ImagePtr m_image;
};

}

Texture *createVulkanTexture(Engine *engine, gfx::VulkanContext &context, int width, int height,
                             const unsigned char *pixels, const TextureFormat &format,
                             const std::wstring &name, bool transient)
{
    return new VulkanTexture(engine, context, width, height, pixels, format, name, transient);
}

}
