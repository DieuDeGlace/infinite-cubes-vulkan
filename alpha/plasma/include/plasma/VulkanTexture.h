#pragma once
#include "gfx/AlphaRenderDevice.h"
#include "plasma/Texture.h"

namespace plasma {
class VulkanEngine;

class VulkanTexture : public Texture {
public:
    VulkanTexture(VulkanEngine *engine, int width, int height, const unsigned char *pixels, const TextureFormat &format,
                const std::wstring &name, bool transient);
    ~VulkanTexture();
    virtual void bind(int stage);
    virtual void upload(const unsigned char *pixels);

private:
    AlphaTexture *m_texture;
    VulkanEngine *m_vulkanEngine;
};
}
