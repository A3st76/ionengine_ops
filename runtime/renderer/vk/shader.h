// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

namespace ionengine::renderer
{

class Shader {
public:

    Shader(Device& device, const std::vector<byte>& shader_code) : m_shader_code(shader_code) {

        vk::ShaderModuleCreateInfo shader_info{};

        shader_info
            .setPCode(reinterpret_cast<const uint32*>(m_shader_code.data()))
            .setCodeSize(m_shader_code.size());

        m_shader = device.m_device->createShaderModuleUnique(shader_info);
    }

    Shader(const Shader&) = delete;

    Shader(Shader&& rhs) noexcept {

        m_shader.swap(rhs.m_shader);
        std::swap(m_shader_code, rhs.m_shader_code);
    }

    Shader& operator=(const Shader&) = delete;

    Shader& operator=(Shader&& rhs) {

        m_shader.swap(rhs.m_shader);
        std::swap(m_shader_code, rhs.m_shader_code);
        return *this;
    }

private:

    vk::UniqueShaderModule m_shader;
    std::vector<byte> m_shader_code;
};

}