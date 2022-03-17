// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include <renderer/backend.h>

namespace ionengine::renderer {

struct MaterialData {

    
};

struct ShaderData {
    std::u8string name;
    ShaderFlags flags;
    std::vector<char8_t> data;
};

struct TextureData {
    Dimension dimension;
    Format format;
    uint32_t width;
    uint32_t height;
    uint16_t mip_levels;
    uint16_t array_layers;
    Filter filter;
    AddressMode address_u;
    AddressMode address_v;
    AddressMode address_w;
    std::vector<char8_t> data;
};

struct MeshData {
    std::vector<float> positions;
    std::vector<float> uv_normals;
    std::vector<uint32_t> indices;

    inline static std::vector<VertexInputDesc> vertex_declaration = {
        VertexInputDesc { "POSITION", 0, Format::RGB32, 0, sizeof(float) * 3 },
        VertexInputDesc { "TEXCOORD", 1, Format::RG32, 1, sizeof(float) * 2 },
        VertexInputDesc { "NORMAL", 2, Format::RGB32, 1, sizeof(float) * 3 }
    };

    
};

}