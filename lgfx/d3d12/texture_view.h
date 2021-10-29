// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "../types.h"
#include "d3d12.h"
#include "descriptor_pool.h"

namespace lgfx {

class TextureView {

friend class CommandBuffer;

public:

    TextureView(Device* device, DescriptorPool* pool, Texture* texture, const TextureViewDesc& desc);
    ~TextureView();
    TextureView(const TextureView&) = delete;
    TextureView(TextureView&&) = delete;

    TextureView& operator=(const TextureView&) = delete;
    TextureView& operator=(TextureView&&) = delete;

    inline Texture* GetTexture() const { return texture_; }
    inline const TextureViewDesc& GetDesc() const { return desc_; }

private:

    DescriptorAllocInfo CreateRenderTarget(Device* device);
    DescriptorAllocInfo CreateShaderResource(Device* device);

    DescriptorPool* pool_;
    Texture* texture_;
    DescriptorAllocInfo alloc_info_;
    TextureViewDesc desc_;
};

}