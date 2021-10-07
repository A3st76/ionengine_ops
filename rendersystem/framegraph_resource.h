// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "lib/math.h"
#include "texture.h"

namespace ionengine::rendersystem {

class FrameGraphResource;

enum class FrameGraphResourceType {
    Attachment,
    Buffer
};

enum class FrameGraphResourceFlags : uint32 {
    None = 1 << 0,
    Swapchain = 1 << 1
};

enum class FrameGraphResourceOp {
    Load,
    Clear
};

struct WriteFrameGraphResource {

    FrameGraphResource* resource;
    FrameGraphResourceOp op;
    math::Fcolor clear_color;
};

struct ReadFrameGraphResource {

    FrameGraphResource* resource;
};

ENUM_CLASS_BIT_FLAG_DECLARE(FrameGraphResourceFlags)

class FrameGraphResource {
public:

    FrameGraphResource(Texture* texture) : m_texture(texture) {

        m_state = gfx::ResourceState::Present;
    }

    void set_state(const gfx::ResourceState state) { m_state = state; }

    gfx::ResourceState get_state() const { return m_state; }

    Texture* get_texture() const { return m_texture; }

    FrameGraphResourceFlags get_flags() { return m_flags; }

private:

    gfx::Device* m_device;

    Texture* m_texture;

    gfx::ResourceState m_state;

    FrameGraphResourceFlags m_flags;
};

}