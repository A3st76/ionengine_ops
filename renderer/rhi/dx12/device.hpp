// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "renderer/rhi/device.hpp"
#include "descriptor_allocator.hpp"
#include "command_allocator.hpp"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxcapi.h>
#include <winrt/base.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace ionengine {

namespace platform {
class Window;
}

namespace renderer {

namespace rhi {

class DX12Device : public Device {
public:

    DX12Device(platform::Window const& window);

	auto create_allocator(size_t const block_size, size_t const shrink_size, BufferUsageFlags const flags) -> core::ref_ptr<MemoryAllocator> override;

	auto allocate_command_buffer(CommandBufferType const buffer_type) -> core::ref_ptr<CommandBuffer> override;

    auto create_texture() -> Future<Texture> override;

    auto create_buffer(
		MemoryAllocator& allocator, 
		size_t const size, 
		BufferUsageFlags const flags, 
		std::span<uint8_t const> const data
	) -> Future<Buffer> override;

    auto write_buffer(core::ref_ptr<Buffer> buffer, uint64_t const offset, std::span<uint8_t const> const data) -> Future<Buffer> override;

private:

#ifdef _DEBUG
    winrt::com_ptr<ID3D12Debug> debug;
#endif
    winrt::com_ptr<IDXGIFactory4> factory;
    winrt::com_ptr<IDXGIAdapter1> adapter;
    winrt::com_ptr<ID3D12Device4> device;

	struct QueueInfo {
		winrt::com_ptr<ID3D12CommandQueue> queue;
		winrt::com_ptr<ID3D12Fence> fence;
		uint64_t fence_value;	
	};
	std::vector<QueueInfo> queue_infos;
	HANDLE fence_event;

	core::ref_ptr<PoolDescriptorAllocator> pool_allocator{nullptr};

	winrt::com_ptr<IDXGISwapChain1> swapchain;

	struct FrameInfo {
		core::ref_ptr<Texture> swapchain_buffer;
		core::ref_ptr<CommandAllocator> command_allocator;
	};
	std::vector<FrameInfo> frame_infos;
	uint32_t frame_index{0};
};

}

}

}