// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "renderer/base_renderer.h"
#include "renderer/api.h"
#include "platform/window.h"

namespace ionengine {

class RenderSystem final {
public:

    RenderSystem(const platform::Window& window) : 
        m_adapter(m_instance), 
        m_device(m_adapter), 
        m_window(window),
        m_swapchain(m_instance, m_device, window.get_handle(), renderer::ImageFormat::R8G8B8A8, 2) {
        
        std::wcout << 
            "device_id: " << m_adapter.get_device_id() <<
            "\ndevice_name: " << m_adapter.get_device_name() << 
            "\nmemory_device: " << m_adapter.get_video_memory() << 
            "\nmemory_system: " << m_adapter.get_system_memory() << 
        std::endl;
    }

    void resize(const uint32 width, const uint32 height) {
        // std::wcout << "RenderSystem::resize triggered" << std::endl;
        m_swapchain.resize(width, height);
    }

    void tick() {
        
    }

private:

    renderer::Instance m_instance;
    renderer::Adapter m_adapter;
    renderer::Device m_device;
    renderer::Swapchain m_swapchain;

    struct {
        renderer::DescriptorPool<renderer::DesctiptorType::UnorderedAccess> ua;
        renderer::DescriptorPool<renderer::DesctiptorType::Sampler> sampler;
        renderer::DescriptorPool<renderer::DesctiptorType::Resource> resource;
        renderer::DescriptorPool<renderer::DesctiptorType::Buffer> buffer;
    } m_descriptors;

    const platform::Window& m_window;
};

}