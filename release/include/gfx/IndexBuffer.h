// IndexBuffer is the API-neutral index-buffer handle created by Graphics. VulkanIndexBuffer is its implementation.
#pragma once

namespace gfx {

class IndexBuffer {
public:
    virtual ~IndexBuffer() {}

    // VulkanIndexBuffer binds the buffer with its format.
    virtual bool bind() { return true; }

    virtual void prepare() {}
};

}
