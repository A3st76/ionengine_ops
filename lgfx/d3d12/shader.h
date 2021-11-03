// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "../types.h"
#include "d3d12.h"

namespace lgfx {

class Shader {

friend class Pipeline;

public:

    Shader(Device* device, const ShaderType type, const std::filesystem::path& path);
    Shader(const Shader&) = delete;
    Shader(Shader&&) = delete;

    Shader& operator=(const Shader&) = delete;
    Shader& operator=(Shader&&) = delete;

    inline ShaderType GetType() const { return type_; }

private:

    ShaderType type_;

    D3D12_SHADER_BYTECODE shader_;

    std::vector<std::byte> data_;
};

}