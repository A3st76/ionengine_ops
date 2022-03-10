// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#include <precompiled.h>
#include <renderer/backend.h>
#include <renderer/d3d12/d3d12_shared.h>
#include <renderer/d3d12/memory_allocator.h>
#include <renderer/d3d12/descriptor_allocator.h>
#include <platform/window.h>
#include <lib/exception.h>
#include <lib/handle_allocator.h>

using ionengine::Handle;
using namespace ionengine::renderer;

namespace ionengine::renderer {

struct Texture {
    ComPtr<ID3D12Resource> resource;
    d3d12::MemoryAllocInfo memory_alloc_info;
    std::array<d3d12::DescriptorAllocInfo, 2> descriptor_alloc_infos;
};

struct Buffer {
    ComPtr<ID3D12Resource> resource;
    d3d12::MemoryAllocInfo memory_alloc_info;
    d3d12::DescriptorAllocInfo descriptor_alloc_info;
};

struct Sampler {
    d3d12::DescriptorAllocInfo descriptor_alloc_info;
};

struct DescriptorLayout {
    ComPtr<ID3D12RootSignature> root_signature;
    std::vector<D3D12_DESCRIPTOR_RANGE> descriptor_ranges;
};

struct DescriptorSet {
    DescriptorLayout descriptor_layout;
    std::vector<d3d12::DescriptorAllocInfo> descriptor_alloc_infos;
};

struct Pipeline {
    ComPtr<ID3D12PipelineState> pipeline_state;
    std::array<uint32_t, 16> vertex_strides;
};

struct Shader {
    D3D12_SHADER_BYTECODE shader_bytecode;
    D3D12_SHADER_VISIBILITY shader_visibility;
    std::vector<char8_t> shader_data;
};

struct RenderPass {
    std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_target_descs;
    uint32_t render_target_count;
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_stencil_desc;
    bool has_depth_stencil;
};

}

struct Backend::Impl {

    struct CommandList {
        ComPtr<ID3D12CommandAllocator> command_allocator;
        ComPtr<ID3D12GraphicsCommandList4> command_list;
        Pipeline* pipeline;
        DescriptorSet* descriptor_set;
    };

    using Fence = std::pair<ComPtr<ID3D12Fence>, uint64_t>;

    using UploadBuffer = std::pair<Handle<Buffer>, uint64_t>;

    d3d12::MemoryAllocator memory_allocator;
    d3d12::DescriptorAllocator descriptor_allocator;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Debug> debug;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device4> device;
    ComPtr<IDXGISwapChain3> swapchain;

    ComPtr<ID3D12CommandQueue> direct_queue;
    ComPtr<ID3D12CommandQueue> copy_queue;
    ComPtr<ID3D12CommandQueue> compute_queue;

    HANDLE fence_event;

    std::vector<Impl::CommandList> direct_lists;
    std::vector<Impl::CommandList> copy_lists;
    std::vector<Impl::CommandList> compute_lists;

    std::vector<Impl::Fence> direct_fences;
    std::vector<Impl::Fence> copy_fences;
    std::vector<Impl::Fence> compute_fences;

    std::vector<Impl::UploadBuffer> upload_buffers;

    std::vector<Handle<Texture>> swap_buffers;
    uint32_t swap_index;

    d3d12::DescriptorAllocInfo null_srv_descriptor_alloc_info;
    d3d12::DescriptorAllocInfo null_sampler_descriptor_alloc_info;

    HandleAllocator<Texture> texture_allocator;
    HandleAllocator<Buffer> buffer_allocator;
    HandleAllocator<Sampler> sampler_allocator;
    HandleAllocator<Pipeline> pipeline_allocator;
    HandleAllocator<Shader> shader_allocator;
    HandleAllocator<RenderPass> render_pass_allocator;
    HandleAllocator<DescriptorLayout> descriptor_layout_allocator;
    HandleAllocator<DescriptorSet> descriptor_set_allocator;

    CommandList* current_list;

    void create_swapchain_resources(uint32_t const swap_buffers_count);
    Handle<Texture> create_texture(Dimension const dimension, uint32_t const width, uint32_t const height, uint16_t const mip_levels, uint16_t const array_layers, Format const format, TextureFlags const flags);
    Handle<Buffer> create_buffer(size_t const size, BufferFlags const flags);
    Handle<RenderPass> create_render_pass(std::span<Handle<Texture>> const& colors, std::span<RenderPassColorDesc> const& color_descs, Handle<Texture> const& depth_stencil, RenderPassDepthStencilDesc const& depth_stencil_desc);
    Handle<Sampler> create_sampler(Filter const filter, AddressMode const address_u, AddressMode const address_v, AddressMode const address_w, uint16_t const anisotropic, CompareOp const compare_op);
    Handle<Shader> create_shader(std::span<char8_t const> const data, ShaderFlags const flags);
    Handle<DescriptorLayout> create_descriptor_layout(std::span<DescriptorRangeDesc> const ranges);
    Handle<Pipeline> create_pipeline(Handle<DescriptorLayout> const& descriptor_layout, std::span<VertexInputDesc> const vertex_descs, std::span<Handle<Shader>> const shaders, RasterizerDesc const& rasterizer_desc, DepthStencilDesc const& depth_stencil_desc, BlendDesc const& blend_desc, Handle<RenderPass> const& render_pass);
    Handle<DescriptorSet> create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout);
    void write_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc> const write_descs);
    void bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set);
    void set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height);
    void set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom);
    void barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after);
    void begin_render_pass(Handle<RenderPass> const& render_pass, std::span<Color> const clear_colors, float const clear_depth = 0.0f, uint8_t const clear_stencil = 0x0);
    void end_render_pass();
    void bind_pipeline(Handle<Pipeline> const& pipeline);
    void bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset);
    void bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset);
    void draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset);
    void draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset);
    void _swap_buffers();
    void resize_buffers(uint32_t const width, uint32_t const height, uint32_t const buffer_count);
    void copy_buffer_region(Handle<Buffer> const& dest, uint64_t const dest_offset, Handle<Buffer> const& source, uint64_t const source_offset, size_t const size);
    void begin_context(ContextType const context_type);
    void end_context();
    void wait_for_context(ContextType const context_type);
    bool is_finished_context(ContextType const context_type);
    void execute_context(ContextType const context_type);
    Handle<Texture> swap_buffer() const;
};

Backend::Backend(uint32_t const adapter_index, platform::Window* const window, uint16_t const samples_count, uint32_t const swap_buffers_count) : _impl(std::make_unique<Impl>()) {

    THROW_IF_FAILED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), reinterpret_cast<void**>(_impl->debug.GetAddressOf())));
    _impl->debug->EnableDebugLayer();

    THROW_IF_FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory4), reinterpret_cast<void**>(_impl->factory.GetAddressOf())));

    THROW_IF_FAILED(_impl->factory->EnumAdapters1(adapter_index, _impl->adapter.GetAddressOf()));

    THROW_IF_FAILED(D3D12CreateDevice(_impl->adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device4), reinterpret_cast<void**>(_impl->device.GetAddressOf())));

    auto queue_desc = D3D12_COMMAND_QUEUE_DESC {};
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    THROW_IF_FAILED(_impl->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(_impl->direct_queue.GetAddressOf())));

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    THROW_IF_FAILED(_impl->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(_impl->copy_queue.GetAddressOf())));

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    THROW_IF_FAILED(_impl->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(_impl->compute_queue.GetAddressOf())));

    // Unbounded shader resource
    {
        auto view_desc = D3D12_SHADER_RESOURCE_VIEW_DESC {};

        _impl->null_srv_descriptor_alloc_info = _impl->descriptor_allocator.allocate(_impl->device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
        
        const uint32_t srv_size = _impl->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
            _impl->null_srv_descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            srv_size * // Device descriptor size
            _impl->null_srv_descriptor_alloc_info.offset() // Allocation offset
        };
        // _impl->device->CreateShaderResourceView(, &view_desc, cpu_handle);
    }

    // Unbounded sampler
    {
        _impl->null_sampler_descriptor_alloc_info = _impl->descriptor_allocator.allocate(_impl->device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);
        // _impl->device->CreateSampler(view_desc, cpu_handle);
    }

    platform::Size window_size = window->get_size();

    auto swapchain_desc = DXGI_SWAP_CHAIN_DESC1 {};
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.Width = window_size.width;
    swapchain_desc.Height = window_size.height;
    swapchain_desc.BufferCount = swap_buffers_count;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    THROW_IF_FAILED(_impl->factory->CreateSwapChainForHwnd(
        _impl->direct_queue.Get(), 
        reinterpret_cast<HWND>(window->get_handle()), 
        &swapchain_desc, 
        nullptr, 
        nullptr, 
        reinterpret_cast<IDXGISwapChain1**>(_impl->swapchain.GetAddressOf()))
    );

    _impl->fence_event = CreateEvent(nullptr, false, false, nullptr);

    if(!_impl->fence_event) {
        THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }

    _impl->swap_index = 0;

    _impl->create_swapchain_resources(swap_buffers_count);
}

Backend::~Backend() = default;

void Backend::Impl::create_swapchain_resources(uint32_t const swap_buffers_count) {

    swap_buffers.resize(swap_buffers_count);
    direct_lists.resize(swap_buffers_count);
    copy_lists.resize(swap_buffers_count);
    compute_lists.resize(swap_buffers_count);
    direct_fences.resize(swap_buffers_count);
    copy_fences.resize(swap_buffers_count);
    compute_fences.resize(swap_buffers_count);
    upload_buffers.resize(swap_buffers_count);

    for(uint32_t i = 0; i < swap_buffers_count; ++i) {

        // Render Target from swapchain resource
        {
            ComPtr<ID3D12Resource> resource;
            THROW_IF_FAILED(swapchain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(resource.GetAddressOf())));

            auto swapchain_desc = DXGI_SWAP_CHAIN_DESC1 {};
            swapchain->GetDesc1(&swapchain_desc);

            auto view_desc = D3D12_RENDER_TARGET_VIEW_DESC {};
            view_desc.Format = swapchain_desc.Format;
            view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            d3d12::DescriptorAllocInfo descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

            const uint32_t rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 
                descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                rtv_size * // Device descriptor size
                descriptor_alloc_info.offset() // Allocation offset
            };
            device->CreateRenderTargetView(resource.Get(), &view_desc, cpu_handle);

            auto texture = Texture {};
            texture.resource = resource;
            texture.descriptor_alloc_infos[0] = descriptor_alloc_info;

            swap_buffers[i] = texture_allocator.allocate(texture);
        }

        ++direct_fences[i].second;
        ++copy_fences[i].second;
        ++compute_fences[i].second;

        THROW_IF_FAILED(device->CreateFence(
            0, 
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence), 
            reinterpret_cast<void**>(direct_fences[i].first.GetAddressOf()))
        );

        THROW_IF_FAILED(device->CreateFence(
            0, 
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence), 
            reinterpret_cast<void**>(copy_fences[i].first.GetAddressOf()))
        );

        THROW_IF_FAILED(device->CreateFence(
            0, 
            D3D12_FENCE_FLAG_NONE, 
            __uuidof(ID3D12Fence), 
            reinterpret_cast<void**>(compute_fences[i].first.GetAddressOf()))
        );

        THROW_IF_FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, 
            __uuidof(ID3D12CommandAllocator), 
            reinterpret_cast<void**>(direct_lists[i].command_allocator.GetAddressOf()))
        );
        
        THROW_IF_FAILED(device->CreateCommandList1(
            0, 
            D3D12_COMMAND_LIST_TYPE_DIRECT, 
            D3D12_COMMAND_LIST_FLAG_NONE,
            __uuidof(ID3D12GraphicsCommandList4),
            reinterpret_cast<void**>(direct_lists[i].command_list.GetAddressOf()))
        );

        THROW_IF_FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COPY, 
            __uuidof(ID3D12CommandAllocator), 
            reinterpret_cast<void**>(copy_lists[i].command_allocator.GetAddressOf()))
        );
        
        THROW_IF_FAILED(device->CreateCommandList1(
            0, 
            D3D12_COMMAND_LIST_TYPE_COPY, 
            D3D12_COMMAND_LIST_FLAG_NONE,
            __uuidof(ID3D12GraphicsCommandList4),
            reinterpret_cast<void**>(copy_lists[i].command_list.GetAddressOf()))
        );

        THROW_IF_FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COMPUTE, 
            __uuidof(ID3D12CommandAllocator), 
            reinterpret_cast<void**>(compute_lists[i].command_allocator.GetAddressOf()))
        );
        
        THROW_IF_FAILED(device->CreateCommandList1(
            0, 
            D3D12_COMMAND_LIST_TYPE_COMPUTE, 
            D3D12_COMMAND_LIST_FLAG_NONE,
            __uuidof(ID3D12GraphicsCommandList4),
            reinterpret_cast<void**>(compute_lists[i].command_list.GetAddressOf()))
        );

        upload_buffers[i].first = create_buffer(64 * 1024 * 1024, BufferFlags::HostWrite);\
        upload_buffers[i].second = 0;
    }
}

Handle<Texture> Backend::Impl::create_texture(
    Dimension const dimension,
    uint32_t const width,
    uint32_t const height,
    uint16_t const mip_levels,
    uint16_t const array_layers,
    Format const format,
    TextureFlags const flags
) {

    auto get_dimension_type = [&](Dimension const dimension) -> D3D12_RESOURCE_DIMENSION {
        switch(dimension) {
            case Dimension::_1D: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            case Dimension::_2D: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            case Dimension::_3D: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        }
        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    };

    auto get_format = [&](Format const format) -> DXGI_FORMAT {
        switch(format) {
            case Format::Unknown: return DXGI_FORMAT_UNKNOWN;
            case Format::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::BGRA8: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case Format::BGR8: return DXGI_FORMAT_B8G8R8X8_UNORM;
            case Format::BC1: return DXGI_FORMAT_BC1_UNORM;
            case Format::BC5: return DXGI_FORMAT_BC5_UNORM;
            default: assert(false && "invalid format specified"); break;
        }
        return DXGI_FORMAT_UNKNOWN;
    };

    auto resource_desc = D3D12_RESOURCE_DESC {};
    resource_desc.Dimension = get_dimension_type(dimension);
    resource_desc.Width = width;
    resource_desc.Height = height;
    resource_desc.MipLevels = mip_levels;
    resource_desc.DepthOrArraySize = array_layers;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = get_format(format);
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if(flags & TextureFlags::DepthStencil) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if(flags & TextureFlags::RenderTarget) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if(flags & TextureFlags::UnorderedAccess) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_RESOURCE_ALLOCATION_INFO resource_alloc_info = device->GetResourceAllocationInfo(0, 1, &resource_desc);
    d3d12::MemoryAllocInfo memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_DEFAULT, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);

    ComPtr<ID3D12Resource> resource;

    THROW_IF_FAILED(device->CreatePlacedResource(
        memory_alloc_info.heap(),
        memory_alloc_info.offset(),
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource), 
        reinterpret_cast<void**>(resource.GetAddressOf())
    ));

    std::array<d3d12::DescriptorAllocInfo, 2> descriptor_alloc_infos;
    uint32_t descriptor_alloc_count = 0;

    if(flags & TextureFlags::DepthStencil) {

        auto view_desc = D3D12_DEPTH_STENCIL_VIEW_DESC {};
        view_desc.Format = resource_desc.Format;

        switch(dimension) {
            case Dimension::_1D: {
                view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
                view_desc.Texture1D.MipSlice = mip_levels;
            } break;
            case Dimension::_2D: {
                view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                view_desc.Texture2D.MipSlice = mip_levels - 1;
            } break;
            default: {
                assert(false && "Depth stencil view dimension is unsupported");
            } break;
        }

        descriptor_alloc_infos[descriptor_alloc_count] = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, false);

        const uint32_t dsv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE { 
            descriptor_alloc_infos[descriptor_alloc_count].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            dsv_size * // Device descriptor size
            descriptor_alloc_infos[descriptor_alloc_count].offset() // Allocation offset
        };
        
        device->CreateDepthStencilView(resource.Get(), &view_desc, cpu_handle);
        
        ++descriptor_alloc_count;
    }
    if(flags & TextureFlags::RenderTarget) {

        auto view_desc = D3D12_RENDER_TARGET_VIEW_DESC {};
        view_desc.Format = resource_desc.Format;

        switch(dimension) {
            case Dimension::_1D: {
                view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
                view_desc.Texture1D.MipSlice = mip_levels;
            } break;
            case Dimension::_2D: {
                view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                view_desc.Texture2D.MipSlice = mip_levels - 1;
                // view_desc.Texture2D.PlaneSlice = array_layers;
            } break;
            case Dimension::_3D: {
                view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
                view_desc.Texture3D.MipSlice = mip_levels;
                view_desc.Texture3D.WSize = array_layers;
            } break;
        }

        descriptor_alloc_infos[descriptor_alloc_count] = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);

        const uint32_t rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE { 
            descriptor_alloc_infos[descriptor_alloc_count].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            rtv_size * // Device descriptor size
            descriptor_alloc_infos[descriptor_alloc_count].offset() // Allocation offset
        };
        
        device->CreateRenderTargetView(resource.Get(), &view_desc, cpu_handle);

        ++descriptor_alloc_count;
    }
    if(flags & TextureFlags::UnorderedAccess) {
        // TODO!
    }
    if(flags & TextureFlags::ShaderResource) {

        auto view_desc = D3D12_SHADER_RESOURCE_VIEW_DESC {};
        view_desc.Format = resource_desc.Format;
        view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        switch(dimension) {
            case Dimension::_1D: {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                view_desc.Texture1D.MipLevels = mip_levels;
            } break;
            case Dimension::_2D: {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view_desc.Texture2D.MipLevels = mip_levels;
            } break;
            case Dimension::_3D: {
                view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                view_desc.Texture3D.MipLevels = mip_levels;
            } break;
        }

        descriptor_alloc_infos[descriptor_alloc_count] = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);

        const uint32_t srv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE { 
            descriptor_alloc_infos[descriptor_alloc_count].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            srv_size * // Device descriptor size
            descriptor_alloc_infos[descriptor_alloc_count].offset() // Allocation offset
        };

        device->CreateShaderResourceView(resource.Get(), &view_desc, cpu_handle);

        ++descriptor_alloc_count;
    }

    auto texture = Texture {};
    texture.resource = resource;
    texture.memory_alloc_info = memory_alloc_info;
    texture.descriptor_alloc_infos = descriptor_alloc_infos;

    return texture_allocator.allocate(texture);
}

Handle<Buffer> Backend::Impl::create_buffer(size_t const size, BufferFlags const flags) {

    auto resource_desc = D3D12_RESOURCE_DESC {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.MipLevels = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_ALLOCATION_INFO resource_alloc_info = device->GetResourceAllocationInfo(0, 1, &resource_desc);

    d3d12::MemoryAllocInfo memory_alloc_info;
    D3D12_RESOURCE_STATES initial_state;

    if(flags & BufferFlags::HostWrite) {
        memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_UPLOAD, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if(flags & BufferFlags::HostRead) {
        memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_READBACK, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else {
        memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_DEFAULT, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
        initial_state = D3D12_RESOURCE_STATE_COMMON;
    }

    ComPtr<ID3D12Resource> resource;

    THROW_IF_FAILED(device->CreatePlacedResource(
        memory_alloc_info.heap(),
        memory_alloc_info.offset(),
        &resource_desc,
        initial_state,
        nullptr,
        __uuidof(ID3D12Resource), 
        reinterpret_cast<void**>(resource.GetAddressOf())
    ));

    d3d12::DescriptorAllocInfo descriptor_alloc_info;
    
    if(flags & BufferFlags::ConstantBuffer) {

        auto view_desc = D3D12_CONSTANT_BUFFER_VIEW_DESC {};
        view_desc.BufferLocation = resource->GetGPUVirtualAddress();
        view_desc.SizeInBytes = static_cast<uint32_t>(size);

        descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);

        const uint32_t cbv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE { 
            descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            cbv_size * // Device descriptor size
            descriptor_alloc_info.offset() // Allocation offset
        };

        device->CreateConstantBufferView(&view_desc, cpu_handle);
    }

    if(flags & BufferFlags::UnorderedAccess) {

        auto view_desc = D3D12_UNORDERED_ACCESS_VIEW_DESC {};
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view_desc.Format = resource_desc.Format;
        // TODO!

        descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);

        const uint32_t uav_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE { 
            descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            uav_size * // Device descriptor size
            descriptor_alloc_info.offset() // Allocation offset
        };

        device->CreateUnorderedAccessView(resource.Get(), nullptr, &view_desc, cpu_handle);
    }

    auto buffer = Buffer {};
    buffer.resource = resource;
    buffer.memory_alloc_info = memory_alloc_info;
    buffer.descriptor_alloc_info = descriptor_alloc_info;

    return buffer_allocator.allocate(buffer);
}

Handle<RenderPass> Backend::Impl::create_render_pass(
    std::span<Handle<Texture>> const& colors, 
    std::span<RenderPassColorDesc> const& color_descs, 
    Handle<Texture> const& depth_stencil, 
    RenderPassDepthStencilDesc const& depth_stencil_desc
) {

    auto get_render_pass_begin_type = [&](RenderPassLoadOp const load_op) {
        switch (load_op) {
    	    case RenderPassLoadOp::Load: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    	    case RenderPassLoadOp::Clear: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
		    case RenderPassLoadOp::DontCare: return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
        }
        return D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
    };
    
    auto get_render_pass_end_type = [&](RenderPassStoreOp const store_op) {
        switch (store_op) {
    	    case RenderPassStoreOp::Store: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    	    case RenderPassStoreOp::DontCare: return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
        }
        return D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;
    };

    std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets;
    uint32_t render_target_count = static_cast<uint32_t>(colors.size()); 

    for(uint32_t i = 0; i < render_target_count; ++i) {

        auto& texture = texture_allocator.get(colors[i]);

        const uint32_t rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 
            texture.descriptor_alloc_infos[0].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            rtv_size * // Device descriptor size
            texture.descriptor_alloc_infos[0].offset() // Allocation offset
        };

        auto begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        begin.Type = get_render_pass_begin_type(color_descs[i].load_op);
        begin.Clear.ClearValue.Format = texture.resource->GetDesc().Format;

        auto end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        end.Type = get_render_pass_end_type(color_descs[i].store_op);

        render_targets[i].BeginningAccess = begin;
        render_targets[i].EndingAccess = end;
        render_targets[i].cpuDescriptor = cpu_handle;
    }

    auto _depth_stencil = D3D12_RENDER_PASS_DEPTH_STENCIL_DESC {};
    bool has_depth_stencil = false;

    if(depth_stencil != INVALID_HANDLE(Texture)) {

        auto& texture = texture_allocator.get(depth_stencil);

        const uint32_t dsv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 
            texture.descriptor_alloc_infos[0].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
            dsv_size * // Device descriptor size
            texture.descriptor_alloc_infos[0].offset() // Allocation offset
        };

        auto depth_begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        depth_begin.Type = get_render_pass_begin_type(depth_stencil_desc.depth_load_op);
        depth_begin.Clear.ClearValue.Format = texture.resource->GetDesc().Format;

        auto stencil_begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        stencil_begin.Type = get_render_pass_begin_type(depth_stencil_desc.stencil_load_op);
        stencil_begin.Clear.ClearValue.Format = texture.resource->GetDesc().Format;

        auto depth_end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        depth_end.Type = get_render_pass_end_type(depth_stencil_desc.depth_store_op);

        auto stencil_end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        stencil_end.Type = get_render_pass_end_type(depth_stencil_desc.stencil_store_op);

        _depth_stencil.DepthBeginningAccess = depth_begin;
        _depth_stencil.StencilBeginningAccess = stencil_begin;
        _depth_stencil.DepthEndingAccess = depth_end;
        _depth_stencil.StencilEndingAccess = stencil_end;
        _depth_stencil.cpuDescriptor = cpu_handle;

        has_depth_stencil = true;
    }

    auto render_pass = RenderPass {};
    render_pass.render_target_descs = render_targets;
    render_pass.render_target_count = render_target_count;
    render_pass.depth_stencil_desc = _depth_stencil;
    render_pass.has_depth_stencil = has_depth_stencil;

    return render_pass_allocator.allocate(render_pass);
}

Handle<Sampler> Backend::Impl::create_sampler(
    Filter const filter, 
    AddressMode const address_u, 
    AddressMode const address_v, 
    AddressMode const address_w, 
    uint16_t const anisotropic, 
    CompareOp const compare_op
) {

    auto get_filter = [&](Filter const filter) -> D3D12_FILTER {
        switch(filter) {
            case Filter::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
            case Filter::MinMagMipLinear: return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            case Filter::ComparisonMinMagMipLinear: return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        }
        return D3D12_FILTER_ANISOTROPIC;
    };

    auto get_address_mode = [&](AddressMode const address_mode) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch(address_mode) {
            case AddressMode::Wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            case AddressMode::Clamp: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            case AddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        }
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    };

    auto get_comparison_func = [&](CompareOp const compare_op) {
        switch(compare_op) {
            case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
            case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
            case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
            case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
            case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
            case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        }
        return D3D12_COMPARISON_FUNC_ALWAYS;
    };

    auto sampler_desc = D3D12_SAMPLER_DESC {};
    sampler_desc.Filter = get_filter(filter);
    sampler_desc.AddressU = get_address_mode(address_u);
    sampler_desc.AddressV = get_address_mode(address_v);
    sampler_desc.AddressW = get_address_mode(address_w);
    sampler_desc.ComparisonFunc = get_comparison_func(compare_op);
    sampler_desc.MaxAnisotropy = anisotropic;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    
    d3d12::DescriptorAllocInfo descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, false);

    const uint32_t sampler_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    auto cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
        descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
        sampler_size * // Device descriptor size
        descriptor_alloc_info.offset() // Allocation offset
    };

    device->CreateSampler(&sampler_desc, cpu_handle);

    auto sampler = Sampler {};
    sampler.descriptor_alloc_info = descriptor_alloc_info;

    return sampler_allocator.allocate(sampler);
}

Handle<Shader> Backend::Impl::create_shader(std::span<char8_t const> const data, ShaderFlags const flags) {

    auto get_shader_visibility = [&](ShaderFlags const flags) -> D3D12_SHADER_VISIBILITY {
        switch(flags) {
            case ShaderFlags::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
            case ShaderFlags::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
            case ShaderFlags::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
            case ShaderFlags::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
            case ShaderFlags::Hull: return D3D12_SHADER_VISIBILITY_HULL;
            case ShaderFlags::Compute: return D3D12_SHADER_VISIBILITY_ALL;
            case ShaderFlags::All: return D3D12_SHADER_VISIBILITY_ALL;
        }
        return D3D12_SHADER_VISIBILITY_ALL;
    };

    std::vector<char8_t> shader_data;
    shader_data.resize(data.size());
    std::memcpy(shader_data.data(), data.data(), sizeof(char8_t) * shader_data.size());
    
    auto shader = Shader {};
    shader.shader_data = shader_data;
    shader.shader_visibility = get_shader_visibility(flags);
    shader.shader_bytecode = D3D12_SHADER_BYTECODE { shader.shader_data.data(), shader.shader_data.size() };

    return shader_allocator.allocate(shader);
}

Handle<DescriptorLayout> Backend::Impl::create_descriptor_layout(std::span<DescriptorRangeDesc> const ranges) {

    std::vector<D3D12_DESCRIPTOR_RANGE> _ranges;
    _ranges.reserve(ranges.size());

    std::vector<D3D12_ROOT_PARAMETER> parameters;
    parameters.reserve(ranges.size());

    auto get_descriptor_range_type = [&](DescriptorRangeType const range_type) -> D3D12_DESCRIPTOR_RANGE_TYPE {
        switch(range_type) {
            case DescriptorRangeType::ShaderResource: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            case DescriptorRangeType::ConstantBuffer: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            case DescriptorRangeType::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            case DescriptorRangeType::UnorderedAccess: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        }
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    };

    auto get_shader_visibility = [&](ShaderFlags const flags) -> D3D12_SHADER_VISIBILITY {
        switch(flags) {
            case ShaderFlags::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
            case ShaderFlags::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
            case ShaderFlags::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
            case ShaderFlags::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
            case ShaderFlags::Hull: return D3D12_SHADER_VISIBILITY_HULL;
            case ShaderFlags::All: return D3D12_SHADER_VISIBILITY_ALL;
        }
        return D3D12_SHADER_VISIBILITY_ALL;
    };

    std::map<D3D12_DESCRIPTOR_RANGE_TYPE, uint32_t> index_ranges;

    index_ranges[D3D12_DESCRIPTOR_RANGE_TYPE_CBV] = 0;
    index_ranges[D3D12_DESCRIPTOR_RANGE_TYPE_SRV] = 0;
    index_ranges[D3D12_DESCRIPTOR_RANGE_TYPE_UAV] = 0;
    index_ranges[D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER] = 0;

    for(auto& range : ranges) {
        auto _range = D3D12_DESCRIPTOR_RANGE {};
        _range.RangeType = get_descriptor_range_type(range.range_type);
        _range.NumDescriptors = range.count;
        _range.BaseShaderRegister = index_ranges[_range.RangeType];
        _range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        index_ranges[_range.RangeType] += _range.NumDescriptors;
        _ranges.emplace_back(_range);

        auto parameter = D3D12_ROOT_PARAMETER {};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.pDescriptorRanges = &_ranges.back();
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.ShaderVisibility = get_shader_visibility(range.flags);

        parameters.emplace_back(parameter);
    }

    auto root_desc = D3D12_ROOT_SIGNATURE_DESC {};
    root_desc.pParameters = parameters.data();
    root_desc.NumParameters = static_cast<uint32_t>(parameters.size());
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized_data;

    THROW_IF_FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, serialized_data.GetAddressOf(), nullptr));

    ComPtr<ID3D12RootSignature> signature;

    THROW_IF_FAILED(device->CreateRootSignature(
        0,
        serialized_data->GetBufferPointer(), 
        serialized_data->GetBufferSize(), 
        __uuidof(ID3D12RootSignature), 
        reinterpret_cast<void**>(signature.GetAddressOf())
    ));

    auto descriptor_layout = DescriptorLayout {};
    descriptor_layout.root_signature = signature;
    descriptor_layout.descriptor_ranges = _ranges;

    return descriptor_layout_allocator.allocate(descriptor_layout);
}

Handle<Pipeline> Backend::Impl::create_pipeline(
    Handle<DescriptorLayout> const& descriptor_layout,
    std::span<VertexInputDesc> const vertex_descs,
    std::span<Handle<Shader>> const shaders,
    RasterizerDesc const& rasterizer_desc,
    DepthStencilDesc const& depth_stencil_desc,
    BlendDesc const& blend_desc,
    Handle<RenderPass> const& render_pass
) {

    auto get_fill_mode = [&](FillMode const fill_mode) {
        switch(fill_mode) {
            case FillMode::Solid: return D3D12_FILL_MODE_SOLID;
            case FillMode::Wireframe: return D3D12_FILL_MODE_WIREFRAME;
        }
        return D3D12_FILL_MODE_SOLID;
    };

    auto get_cull_mode = [&](CullMode const cull_mode) {
        switch(cull_mode) {
            case CullMode::Front: return D3D12_CULL_MODE_FRONT;
            case CullMode::Back: return D3D12_CULL_MODE_BACK;
            case CullMode::None: return D3D12_CULL_MODE_NONE;
        }
        return D3D12_CULL_MODE_NONE;
    };

    auto get_comparison_func = [&](CompareOp const compare_op) {
        switch(compare_op) {
            case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
            case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
            case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
            case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
            case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
            case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        }
        return D3D12_COMPARISON_FUNC_ALWAYS;
    };

    auto get_blend_func = [&](BlendOp const blend_op) {
        switch(blend_op) {
            case BlendOp::Add: return D3D12_BLEND_OP_ADD;
            case BlendOp::Max: return D3D12_BLEND_OP_MAX;
            case BlendOp::Min: return D3D12_BLEND_OP_MIN;
            case BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
            case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        }
        return D3D12_BLEND_OP_ADD;
    };

    auto get_blend = [&](Blend const blend) {
        switch(blend) {
            case Blend::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
            case Blend::One: return D3D12_BLEND_ONE;
            case Blend::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
            case Blend::Zero: return D3D12_BLEND_ZERO; 
        }
        return D3D12_BLEND_ZERO;
    };

    auto get_dxgi_format = [&](Format const format) {
        switch(format) {
            case Format::RGB32: return DXGI_FORMAT_R32G32B32_FLOAT;
            case Format::RGBA32: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case Format::RG32: return DXGI_FORMAT_R32G32_FLOAT;
        }
        return DXGI_FORMAT_R32G32B32_FLOAT;
    };

    auto& signature = descriptor_layout_allocator.get(descriptor_layout);

    auto pipeline_desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {};
    pipeline_desc.pRootSignature = signature.root_signature.Get();

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_element_descs;
    input_element_descs.reserve(vertex_descs.size());

    std::array<uint32_t, 16> vertex_strides;

    for(auto const& input : vertex_descs) {
        auto input_element_desc = D3D12_INPUT_ELEMENT_DESC {};
        input_element_desc.SemanticName = input.semantic.c_str();
        input_element_desc.InputSlot = input.slot;
        input_element_desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        input_element_desc.Format = get_dxgi_format(input.format);
        input_element_desc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        input_element_descs.emplace_back(input_element_desc);

        vertex_strides[input.slot] += input.stride;
    }

    pipeline_desc.InputLayout.pInputElementDescs = input_element_descs.data();
    pipeline_desc.InputLayout.NumElements = static_cast<uint32_t>(input_element_descs.size());

    auto _rasterizer_desc = D3D12_RASTERIZER_DESC {};
    _rasterizer_desc.FillMode = get_fill_mode(rasterizer_desc.fill_mode);
    _rasterizer_desc.CullMode = get_cull_mode(rasterizer_desc.cull_mode);
    _rasterizer_desc.FrontCounterClockwise = false;
    _rasterizer_desc.DepthBias = 0;
    _rasterizer_desc.DepthBiasClamp = 0.0f;
    _rasterizer_desc.SlopeScaledDepthBias = 0.0f;
    _rasterizer_desc.DepthClipEnable = true;
    _rasterizer_desc.MultisampleEnable = false;
    _rasterizer_desc.AntialiasedLineEnable = false;
    _rasterizer_desc.ForcedSampleCount = 0;
    _rasterizer_desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    pipeline_desc.RasterizerState = _rasterizer_desc;

    for(auto& shader : shaders) {
        auto& _shader = shader_allocator.get(shader);
        
        switch(_shader.shader_visibility) {
            case D3D12_SHADER_VISIBILITY_VERTEX: pipeline_desc.VS = _shader.shader_bytecode; break;
            case D3D12_SHADER_VISIBILITY_PIXEL: pipeline_desc.PS = _shader.shader_bytecode; break;
            case D3D12_SHADER_VISIBILITY_GEOMETRY: pipeline_desc.GS = _shader.shader_bytecode; break;
            case D3D12_SHADER_VISIBILITY_DOMAIN: pipeline_desc.DS = _shader.shader_bytecode; break;
            case D3D12_SHADER_VISIBILITY_HULL: pipeline_desc.HS = _shader.shader_bytecode; break;
        }
    }

    auto _depth_stencil_desc = D3D12_DEPTH_STENCIL_DESC {};
    _depth_stencil_desc.DepthFunc = get_comparison_func(depth_stencil_desc.depth_func);
    _depth_stencil_desc.DepthEnable = depth_stencil_desc.write_enable;
    _depth_stencil_desc.StencilEnable = depth_stencil_desc.write_enable;
    _depth_stencil_desc.DepthWriteMask = depth_stencil_desc.write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    _depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    _depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;

    auto depth_stencil_face = D3D12_DEPTH_STENCILOP_DESC {};
    depth_stencil_face.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_face.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_face.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depth_stencil_face.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    _depth_stencil_desc.FrontFace = depth_stencil_face;
    _depth_stencil_desc.BackFace = depth_stencil_face;

    pipeline_desc.DepthStencilState = _depth_stencil_desc;

    auto _blend_desc = D3D12_BLEND_DESC {};
    auto render_target_blend_desc = D3D12_RENDER_TARGET_BLEND_DESC {};
    render_target_blend_desc.BlendEnable = blend_desc.blend_enable;
    render_target_blend_desc.SrcBlend = get_blend(blend_desc.blend_src);
    render_target_blend_desc.DestBlend = get_blend(blend_desc.blend_dst);
    render_target_blend_desc.BlendOp = get_blend_func(blend_desc.blend_op);
    render_target_blend_desc.SrcBlendAlpha = get_blend(blend_desc.blend_src_alpha);
    render_target_blend_desc.DestBlendAlpha = get_blend(blend_desc.blend_dst_alpha);
    render_target_blend_desc.BlendOpAlpha = get_blend_func(blend_desc.blend_op_alpha);
    render_target_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    auto _render_pass = render_pass_allocator.get(render_pass);

    for(uint32_t i = 0; i < _render_pass.render_target_count; ++i) {
        _blend_desc.RenderTarget[i] = render_target_blend_desc;
        pipeline_desc.RTVFormats[i] = _render_pass.render_target_descs[i].BeginningAccess.Clear.ClearValue.Format;
    }

    pipeline_desc.DSVFormat = _render_pass.has_depth_stencil ? _render_pass.depth_stencil_desc.DepthBeginningAccess.Clear.ClearValue.Format : DXGI_FORMAT_UNKNOWN;
    pipeline_desc.NumRenderTargets = _render_pass.render_target_count;
    pipeline_desc.BlendState = _blend_desc;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.SampleMask = std::numeric_limits<uint32_t>::max();

    // TODO!
    pipeline_desc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipeline_state;

    THROW_IF_FAILED(device->CreateGraphicsPipelineState(
        &pipeline_desc,
        __uuidof(ID3D12PipelineState), 
        reinterpret_cast<void**>(pipeline_state.GetAddressOf())
    ));

    auto pipeline = Pipeline {};
    pipeline.pipeline_state = pipeline_state;
    pipeline.vertex_strides = vertex_strides;

    return pipeline_allocator.allocate(pipeline);
}

Handle<DescriptorSet> Backend::Impl::create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout) {

    auto& _descriptor_layout = descriptor_layout_allocator.get(descriptor_layout);

    std::vector<d3d12::DescriptorAllocInfo> descriptor_alloc_infos;
    
    for(auto& range : _descriptor_layout.descriptor_ranges) {

        for(uint32_t i = 0; i < range.NumDescriptors; ++i) {

            d3d12::DescriptorAllocInfo descriptor_alloc_info;

            switch(range.RangeType) {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: {
                    descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
                } break;
                case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: {
                    descriptor_alloc_info = descriptor_allocator.allocate(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true);
                }break;
            }

            descriptor_alloc_infos.emplace_back(descriptor_alloc_info);
        }
    }

    auto descriptor_set = DescriptorSet {};
    descriptor_set.descriptor_layout = _descriptor_layout;
    descriptor_set.descriptor_alloc_infos = descriptor_alloc_infos;
    
    return descriptor_set_allocator.allocate(descriptor_set);
}

void Backend::Impl::write_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc> const write_descs) {

    auto& _descriptor_set = descriptor_set_allocator.get(descriptor_set);

    for(auto& write : write_descs) {

        auto write_visitor = make_visitor(
            [&](Handle<Texture>& texture) {

                auto& _texture = texture_allocator.get(texture);

                const uint32_t srv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                auto src_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _texture.descriptor_alloc_infos[0].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    srv_size * // Device descriptor size
                    _texture.descriptor_alloc_infos[0].offset() // Allocation offset
                };

                auto dst_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _descriptor_set.descriptor_alloc_infos[write.index].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    srv_size * // Device descriptor size
                    _descriptor_set.descriptor_alloc_infos[write.index].offset() // Allocation offset
                };

                device->CopyDescriptorsSimple(1, dst_cpu_handle, src_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            },
            [&](Handle<Buffer>& buffer) {

                auto& _buffer = buffer_allocator.get(buffer);

                const uint32_t srv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                auto src_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _buffer.descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    srv_size * // Device descriptor size
                    _buffer.descriptor_alloc_info.offset() // Allocation offset
                };
                auto dst_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _descriptor_set.descriptor_alloc_infos[write.index].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    srv_size * // Device descriptor size
                    _descriptor_set.descriptor_alloc_infos[write.index].offset() // Allocation offset
                };

                device->CopyDescriptorsSimple(1, dst_cpu_handle, src_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            },
            [&](Handle<Sampler>& sampler) {

                auto& _sampler = sampler_allocator.get(sampler);

                const uint32_t sampler_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

                auto src_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _sampler.descriptor_alloc_info.heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    sampler_size * // Device descriptor size
                    _sampler.descriptor_alloc_info.offset() // Allocation offset
                };

                auto dst_cpu_handle = D3D12_CPU_DESCRIPTOR_HANDLE {
                    _descriptor_set.descriptor_alloc_infos[write.index].heap()->GetCPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    sampler_size * // Device descriptor size
                    _descriptor_set.descriptor_alloc_infos[write.index].offset() // Allocation offset
                };

                device->CopyDescriptorsSimple(1, dst_cpu_handle, src_cpu_handle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            }
        );

        std::visit(write_visitor, write.data); 
    }
}

void Backend::Impl::bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    auto& _descriptor_set = descriptor_set_allocator.get(descriptor_set);
    auto& _descriptor_layout = _descriptor_set.descriptor_layout;

    current_list->command_list->SetGraphicsRootSignature(_descriptor_layout.root_signature.Get());

    size_t binding = 0;

    for(uint32_t i = 0; i < _descriptor_layout.descriptor_ranges.size(); ++i) {

        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
        
        switch(_descriptor_layout.descriptor_ranges[i].RangeType) {
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: {
                const uint32_t srv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE {
                    _descriptor_set.descriptor_alloc_infos[binding].heap()->GetGPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    srv_size * // Device descriptor size
                    _descriptor_set.descriptor_alloc_infos[binding].offset() // Allocation offset
                };
            } break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: {
                const uint32_t sampler_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
                gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE {
                    _descriptor_set.descriptor_alloc_infos[binding].heap()->GetGPUDescriptorHandleForHeapStart().ptr + // Base descriptor pointer
                    sampler_size * // Device descriptor size
                    _descriptor_set.descriptor_alloc_infos[binding].offset() // Allocation offset
                };
            } break;
        }

        binding += _descriptor_layout.descriptor_ranges[i].NumDescriptors;

        current_list->command_list->SetGraphicsRootDescriptorTable(i, gpu_handle);     
    }
}

void Backend::Impl::set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height) {

    auto viewport = D3D12_VIEWPORT {};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = D3D12_MIN_DEPTH;
    viewport.MaxDepth = D3D12_MAX_DEPTH;

    current_list->command_list->RSSetViewports(1, &viewport);
}

void Backend::Impl::set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom) {

    auto rect = D3D12_RECT {};
    rect.top = static_cast<LONG>(top);
    rect.bottom = static_cast<LONG>(bottom);
    rect.left = static_cast<LONG>(left);
    rect.right = static_cast<LONG>(right);

    current_list->command_list->RSSetScissorRects(1, &rect);
}

void Backend::Impl::barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after) {

    auto get_memory_state = [&](MemoryState const state) -> D3D12_RESOURCE_STATES {
        switch(state) {
		    case MemoryState::Common: return D3D12_RESOURCE_STATE_COMMON;
		    case MemoryState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
		    case MemoryState::Present: return D3D12_RESOURCE_STATE_PRESENT;
		    //case MemoryState::GenericRead: return D3D12_RESOURCE_STATE_GENERIC_READ;
		    case MemoryState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
		    case MemoryState::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
            case MemoryState::ShaderRead: return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            case MemoryState::VertexConstantBufferRead: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            case MemoryState::IndexBufferRead: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    };

    auto resource_barrier = D3D12_RESOURCE_BARRIER {};
    resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    resource_barrier.Transition.StateBefore = get_memory_state(before);
    resource_barrier.Transition.StateAfter = get_memory_state(after);

    auto barrier_visitor = make_visitor(
        [&](Handle<Texture> const& texture) {
            resource_barrier.Transition.pResource = texture_allocator.get(texture).resource.Get();
        },
        [&](Handle<Buffer> const& buffer) {
            resource_barrier.Transition.pResource = buffer_allocator.get(buffer).resource.Get();
        }
    );

    std::visit(barrier_visitor, target);

    current_list->command_list->ResourceBarrier(1, &resource_barrier);
}

void Backend::Impl::begin_render_pass(
    Handle<RenderPass> const& render_pass, 
    std::span<Color> const clear_colors, 
    float const clear_depth, 
    uint8_t const clear_stencil
) {

    auto& _render_pass = render_pass_allocator.get(render_pass);

    for(uint32_t i = 0; i < _render_pass.render_target_count; ++i) {
        std::memcpy(_render_pass.render_target_descs[i].BeginningAccess.Clear.ClearValue.Color, &clear_colors[i], sizeof(Color));
    }

    if(_render_pass.has_depth_stencil) {

        _render_pass.depth_stencil_desc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = clear_depth;
        _render_pass.depth_stencil_desc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = clear_stencil;

        current_list->command_list->BeginRenderPass(
            _render_pass.render_target_count, 
            _render_pass.render_target_descs.data(), 
            &_render_pass.depth_stencil_desc, 
            D3D12_RENDER_PASS_FLAG_NONE
        );
    } else {
        current_list->command_list->BeginRenderPass(
            _render_pass.render_target_count, 
            _render_pass.render_target_descs.data(), 
            nullptr, 
            D3D12_RENDER_PASS_FLAG_NONE
        );
    }
}

void Backend::Impl::end_render_pass() {

    current_list->command_list->EndRenderPass();
}

void Backend::Impl::bind_pipeline(Handle<Pipeline> const& pipeline) {

    auto& _pipeline = pipeline_allocator.get(pipeline);

    current_list->pipeline = &_pipeline;

    current_list->command_list->SetPipelineState(_pipeline.pipeline_state.Get());
    current_list->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Backend::Impl::bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset) {

    auto& _buffer = buffer_allocator.get(buffer);
    auto& pipeline = *current_list->pipeline;

    auto vertex_view = D3D12_VERTEX_BUFFER_VIEW {};
    vertex_view.BufferLocation = _buffer.resource->GetGPUVirtualAddress() + offset;
    vertex_view.SizeInBytes = static_cast<uint32_t>(_buffer.resource->GetDesc().Width);
    vertex_view.StrideInBytes = pipeline.vertex_strides[index];

    current_list->command_list->IASetVertexBuffers(index, 1, &vertex_view);
}

void Backend::Impl::bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset) {

    auto& _buffer = buffer_allocator.get(buffer);

    auto index_view = D3D12_INDEX_BUFFER_VIEW {};
    index_view.BufferLocation = _buffer.resource->GetGPUVirtualAddress() + offset;
    index_view.SizeInBytes = static_cast<uint32_t>(_buffer.resource->GetDesc().Width);
    index_view.Format = DXGI_FORMAT_R32_UINT;

    current_list->command_list->IASetIndexBuffer(&index_view);
}

void Backend::Impl::draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset) {

    current_list->command_list->DrawInstanced(vertex_count, instance_count, vertex_offset, 0);
}

void Backend::Impl::draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset) {

    current_list->command_list->DrawIndexedInstanced(index_count, instance_count, 0, 0, instance_offset);
}

void Backend::Impl::_swap_buffers() {

    const uint64_t value = _impl->frames[_impl->frame_index].direct_fence.fence_value;
    THROW_IF_FAILED(_impl->direct_queue->Signal(_impl->frames[_impl->frame_index].direct_fence.fence.Get(), value));

    if(_impl->frames[_impl->frame_index].direct_fence.fence->GetCompletedValue() < value) {
        THROW_IF_FAILED(_impl->frames[_impl->frame_index].direct_fence.fence->SetEventOnCompletion(value, _impl->wait_event));
        WaitForSingleObjectEx(_impl->wait_event, INFINITE, false);
    }
    ++_impl->frames[_impl->frame_index].direct_fence.fence_value;

    swapchain->Present(0, 0);

    _impl->frame_index = (_impl->frame_index + 1) % _impl->frames.size();
}

void Backend::Impl::resize_buffers(uint32_t const width, uint32_t const height, uint32_t const buffer_count) {

}

void Backend::Impl::copy_buffer_region(
    Handle<Buffer> const& dest, 
    uint64_t const dest_offset, 
    Handle<Buffer> const& source, 
    uint64_t const source_offset,
    size_t const size
) {

    auto& dest_buffer = buffer_allocator.get(dest);
    auto& source_buffer = buffer_allocator.get(source);

    current_list->command_list->CopyBufferRegion(
        dest_buffer.resource.Get(), 
        dest_offset,
        source_buffer.resource.Get(),
        source_offset,
        size
    );
}

void Backend::Impl::begin_context(ContextType const context_type) {

    switch(context_type) {
        case ContextType::Graphics: {
            current_list = &direct_lists[swap_index];

            std::array<ID3D12DescriptorHeap*, 2> heaps;
            heaps[0] = null_srv_descriptor_alloc_info.heap();
            heaps[1] = null_sampler_descriptor_alloc_info.heap();

            current_list->command_list->SetDescriptorHeaps(2, heaps.data());
        } break;
        case ContextType::Copy: {
            current_list = &copy_lists[swap_index];
        } break;
        case ContextType::Compute: {
            current_list = &compute_lists[swap_index];
            // TODO!
        } break;
    }

    THROW_IF_FAILED(current_list->command_allocator->Reset());
    THROW_IF_FAILED(current_list->command_list->Reset(current_list->command_allocator.Get(), nullptr));
}

void Backend::Impl::end_context() {

    THROW_IF_FAILED(current_list->command_list->Close());
    current_list = nullptr;
}

void Backend::Impl::wait_for_context(ContextType const context_type) {


}

bool Backend::Impl::is_finished_context(ContextType const context_type) {


}

void Backend::Impl::execute_context(ContextType const context_type) {

    ID3D12CommandList* const* batch_command_list;

    switch(context_type) {
        case ContextType::Graphics: {
            batch_command_list = reinterpret_cast<ID3D12CommandList* const*>(direct_lists[swap_index].command_list.GetAddressOf());
            direct_queue->ExecuteCommandLists(1, batch_command_list);
        } break;
        case ContextType::Copy: {
            batch_command_list = reinterpret_cast<ID3D12CommandList* const*>(copy_lists[swap_index].command_list.GetAddressOf());
            copy_queue->ExecuteCommandLists(1, batch_command_list);
        } break;
        case ContextType::Compute: {
            batch_command_list = reinterpret_cast<ID3D12CommandList* const*>(compute_lists[swap_index].command_list.GetAddressOf());
            compute_queue->ExecuteCommandLists(1, batch_command_list);
        } break;
    }
}

Handle<Texture> Backend::Impl::swap_buffer() const {

    return swap_buffers[swap_index];
}

Handle<Texture> Backend::create_texture(
    Dimension const dimension,
    uint32_t const width,
    uint32_t const height,
    uint16_t const mip_levels,
    uint16_t const array_layers,
    Format const format,
    TextureFlags const flags
) {
    
    return _impl->create_texture(dimension, width, height, mip_levels, array_layers, format, flags);
}

Handle<Buffer> Backend::create_buffer(size_t const size, BufferFlags const flags) {

    return _impl->create_buffer(size, flags); 
}

Handle<RenderPass> Backend::create_render_pass(
    std::span<Handle<Texture>> const& colors, 
    std::span<RenderPassColorDesc> const& color_descs, 
    Handle<Texture> const& depth_stencil, 
    RenderPassDepthStencilDesc const& depth_stencil_desc
) {

    return _impl->create_render_pass(colors, color_descs, depth_stencil, depth_stencil_desc);
}

Handle<Sampler> Backend::create_sampler(
    Filter const filter, 
    AddressMode const address_u, 
    AddressMode const address_v, 
    AddressMode const address_w, 
    uint16_t const anisotropic, 
    CompareOp const compare_op
) {

    return _impl->create_sampler(filter, address_u, address_v, address_w, anisotropic, compare_op);
}

Handle<Shader> Backend::create_shader(std::span<char8_t const> const data, ShaderFlags const flags) {

    return _impl->create_shader(data, flags);
}

Handle<DescriptorLayout> Backend::create_descriptor_layout(std::span<DescriptorRangeDesc> const ranges) {

    return _impl->create_descriptor_layout(ranges);
}

Handle<Pipeline> Backend::create_pipeline(
    Handle<DescriptorLayout> const& descriptor_layout,
    std::span<VertexInputDesc> const vertex_descs,
    std::span<Handle<Shader>> const shaders,
    RasterizerDesc const& rasterizer_desc,
    DepthStencilDesc const& depth_stencil_desc,
    BlendDesc const& blend_desc,
    Handle<RenderPass> const& render_pass
) {
    
    return _impl->create_pipeline(descriptor_layout, vertex_descs, shaders, rasterizer_desc, depth_stencil_desc, blend_desc, render_pass);
}

Handle<DescriptorSet> Backend::create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout) {

    return _impl->create_descriptor_set(descriptor_layout);
}

void Backend::write_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc> const write_descs) {

    _impl->write_descriptor_set(descriptor_set, write_descs);
}

Backend& Backend::bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    _impl->bind_descriptor_set(descriptor_set);
    return *this;
}

Backend& Backend::set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height) {

    _impl->set_viewport(x, y, width, height);
    return *this;
}

Backend& Backend::set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom) {

    _impl->set_scissor(left, top, right, bottom);
    return *this;
}

Backend& Backend::barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after) {

    _impl->barrier(target, before, after);
    return *this;
}

Backend& Backend::begin_render_pass(
    Handle<RenderPass> const& render_pass, 
    std::span<Color> const clear_colors, 
    float const clear_depth, 
    uint8_t const clear_stencil
) {

    _impl->begin_render_pass(render_pass, clear_colors, clear_depth, clear_stencil);
    return *this;
}

Backend& Backend::end_render_pass() {

    _impl->end_render_pass();
    return *this;
}

Backend& Backend::bind_pipeline(Handle<Pipeline> const& pipeline) {

    _impl->bind_pipeline(pipeline);
    return *this;
}

Backend& Backend::copy_buffer_region(
    Handle<Buffer> const& dest, 
    uint64_t const dest_offset, 
    Handle<Buffer> const& source, 
    uint64_t const source_offset,
    size_t const size
) {

    _impl->copy_buffer_region(dest, dest_offset, source, source_offset, size);
    return *this;
}

Backend& Backend::bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset) {

    _impl->bind_vertex_buffer(index, buffer, offset);
    return *this;
}

Backend& Backend::bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset) {

    _impl->bind_index_buffer(buffer, offset);
    return *this;
}

Backend& Backend::draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset) {

    _impl->draw(vertex_count, instance_count, vertex_offset);
    return *this;    
}

Backend& Backend::draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset) {

    _impl->draw_indexed(index_count, instance_count, instance_offset);
    return *this;
}

Backend& Backend::begin_context(ContextType const context_type) {

    _impl->begin_context(context_type);
    return *this;
}

void Backend::end_context() {

    _impl->end_context();
}

void Backend::execute_context(ContextType const context_type) {

    _impl->execute_context(context_type);
}

void Backend::wait_for_context(ContextType const context_type) {

    _impl->wait_for_context(context_type);
}

bool Backend::is_finished_context(ContextType const context_type) {

    return _impl->is_finished_context(context_type);
}

void Backend::wait_for_context(ContextType const context_type) {

    switch(context_type) {
        case ContextType::Graphics: {
            const uint64_t value = _impl->frames[_impl->frame_index].direct_fence.fence_value;
            THROW_IF_FAILED(_impl->direct_queue->Signal(_impl->frames[_impl->frame_index].direct_fence.fence.Get(), value));

            ++_impl->frames[_impl->frame_index].direct_fence.fence_value;

            if(_impl->frames[_impl->frame_index].direct_fence.fence->GetCompletedValue() < value) {
                THROW_IF_FAILED(_impl->frames[_impl->frame_index].direct_fence.fence->SetEventOnCompletion(value, _impl->wait_event));
                WaitForSingleObjectEx(_impl->wait_event, INFINITE, false);
            }
        } break;
        case ContextType::Copy: {
            const uint64_t value = _impl->frames[_impl->frame_index].copy_fence.fence_value;
            THROW_IF_FAILED(_impl->copy_queue->Signal(_impl->frames[_impl->frame_index].copy_fence.fence.Get(), value));

            ++_impl->frames[_impl->frame_index].copy_fence.fence_value;

            if(_impl->frames[_impl->frame_index].copy_fence.fence->GetCompletedValue() < value) {
                THROW_IF_FAILED(_impl->frames[_impl->frame_index].copy_fence.fence->SetEventOnCompletion(value, _impl->wait_event));
                WaitForSingleObjectEx(_impl->wait_event, INFINITE, false);
            }
        } break;
    }
}

Handle<Texture> Backend::swap_buffer() const {

    return _impl->swap_buffer();
}

void Backend::swap_buffers() {

    _impl->_swap_buffers();
}


void Backend::resize_buffers(uint32_t const width, uint32_t const height, uint32_t const buffer_count) {

}

void Backend::copy_texture_data(Handle<Texture> const& dst, std::span<char8_t const> const data) {

    auto& texture = _impl->textures.get(dst);
    D3D12_RESOURCE_DESC resource_desc = texture.resource->GetDesc();

    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 16> footprints;
    std::array<uint32_t, 16> num_rows;
    std::array<uint64_t, 16> row_sizes;

    size_t total_bytes = 0;

    _impl->device->GetCopyableFootprints(
        &resource_desc,
        0,
        1,
        0,
        footprints.data(),
        num_rows.data(),
        row_sizes.data(),
        &total_bytes
    );

    auto range = D3D12_RANGE {};
    char8_t* ptr;

    auto& stage_buffer = _impl->frames[_impl->frame_index].stage_buffer;

    // auto& layout = footprints[0];
    // const uint64_t subresource_pitch = layout.Footprint.RowPitch + (~layout.Footprint.RowPitch & D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    THROW_IF_FAILED(stage_buffer.resource->Map(0, &range, reinterpret_cast<void**>(&ptr)));
    std::memcpy(ptr + stage_buffer.offset, data.data(), data.size());
    stage_buffer.resource->Unmap(0, &range);

    auto dst_location = D3D12_TEXTURE_COPY_LOCATION {};
    dst_location.pResource = texture.resource.Get();
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;
 
    auto src_location = D3D12_TEXTURE_COPY_LOCATION {};
    src_location.pResource = stage_buffer.resource.Get();
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_location.PlacedFootprint = footprints[0];

    _impl->current_buffer->command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
}

void Backend::wait_for_idle_device() {

    for(uint32_t i = 0; i < static_cast<uint32_t>(_impl->frames.size()); ++i) {
        const uint64_t value = _impl->frames[i].direct_fence.fence_value;
        THROW_IF_FAILED(_impl->direct_queue->Signal(_impl->frames[i].direct_fence.fence.Get(), value));

        ++_impl->frames[i].direct_fence.fence_value;

        if(_impl->frames[i].direct_fence.fence->GetCompletedValue() < value) {
            THROW_IF_FAILED(_impl->frames[i].direct_fence.fence->SetEventOnCompletion(value, _impl->wait_event));
            WaitForSingleObjectEx(_impl->wait_event, INFINITE, false);
        }
    }
}





void Backend::delete_render_pass(Handle<RenderPass> const& handle) {

}

void Backend::delete_texture(Handle<Texture> const& handle) {
    
}