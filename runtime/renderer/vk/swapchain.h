// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

namespace ionengine::renderer {

class Swapchain {
public:

    Swapchain(const Instance& instance, const Device& device, void* hwnd, const uint32 buffer_count) : 
        m_instance(instance), m_device(device), m_buffer_count(buffer_count) {

#ifdef VK_USE_PLATFORM_WIN32_KHR
        VkWin32SurfaceCreateInfoKHR surface_info = {};
        surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_info.hwnd = reinterpret_cast<HWND>(hwnd);
        surface_info.hinstance = GetModuleHandle(nullptr);
        throw_if_failed(vkCreateWin32SurfaceKHR(m_instance.m_handle, &surface_info, nullptr, &m_surface_handle));
#endif

        VkSurfaceCapabilitiesKHR surface_capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(, m_surface_handle, &surface_capabilities);


        VkSwapchainCreateInfoKHR swapchain_info = {};
        swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_info.surface = m_surface_handle;
        swapchain_info.minImageCount = m_buffer_count;
        swapchain_info.imageFormat = static_cast<VkFormat>();
        swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapchain_info.imageExtent = VkExtent2D { 1, 1 };
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        throw_if_failed(vkCreateSwapchainKHR(m_device.m_handle, &swapchain_info, nullptr, &m_swapchain_handle));
    }

    ~Swapchain() {
        vkDestroySwapchainKHR(m_device.m_handle, m_swapchain_handle, nullptr);
        vkDestroySurfaceKHR(m_instance.m_handle, m_surface_handle, nullptr);
    }

    void resize(const uint32 width, const uint32 height) {

    }

private:

    const Device& m_device;
    const Instance& m_instance;

    VkSurfaceKHR m_surface_handle;
    VkSwapchainKHR m_swapchain_handle;
    uint32 m_buffer_count;

};

}