#pragma once
#include "gfx/AlphaRenderDevice.h"
#include "plasma/Drawing.h"

namespace plasma {
class VulkanEngine;

class VulkanDrawing : public Drawing {
public:
    explicit VulkanDrawing(VulkanEngine *engine);
    virtual ~VulkanDrawing();
    virtual void update();
    virtual void draw(Node *node, const Vector<4, float> &color);
    void releaseDeviceResources();
    void restoreDeviceResources();

private:
    VulkanEngine *m_engine;
    AlphaBuffer *m_vertexBuffer;
    AlphaBuffer *m_indexBuffer;
    unsigned char *m_vertexData; // interleaved vertex bytes, allocated with new[]
    unsigned char *m_indexData;
    int m_vertexCount;
    int m_indexCount;
};
}
