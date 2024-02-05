// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "renderer/render_pipeline.hpp"

namespace ionengine::renderer
{
    enum class MyRenderMask : uint8_t
    {
        Opaque = 1 << 0
    };

    class MyRenderPipeline : public RenderPipeline
    {
      public:
        MyRenderPipeline() = default;

        auto setup(RenderGraphBuilder& builder, core::ref_ptr<Camera> camera, core::ref_ptr<rhi::Shader> test_shader)
            -> void override;
    };
} // namespace ionengine::renderer