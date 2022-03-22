// Copyright © 2020-2022 Dmitriy Lukovenko. All rights reserved.

#include <precompiled.h>
#include <renderer/backend.h>
#include <renderer/d3d12/d3d12x.h>
#include <renderer/d3d12/memory_allocator.h>
#include <renderer/d3d12/d3d12_cpu_desc_pool.h>
#include <renderer/d3d12/d3d12_gpu_desc_pool.h>
#include <platform/window.h>
#include <lib/exception.h>
#include <lib/algorithm.h>

using namespace ionengine::renderer;
using ionengine::Handle;

namespace ionengine::renderer {

using DescriptorAllocInfo2 = std::pair<d3d12::DescriptorAllocInfo, uint32_t>;

struct Texture {
    ComPtr<ID3D12Resource> resource;
    d3d12::MemoryAllocInfo memory_alloc_info;
    std::unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, DescriptorAllocInfo2> descriptor_alloc_infos;
};

struct Buffer {
    ComPtr<ID3D12Resource> resource;
    d3d12::MemoryAllocInfo memory_alloc_info;
    DescriptorAllocInfo2 descriptor_alloc_info;
};

struct Sampler {
    DescriptorAllocInfo2 alloc_info;
};

struct DescriptorLayout {
    ComPtr<ID3D12RootSignature> root_signature;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    bool is_compute;
};

struct DescriptorSet {
    ComPtr<ID3D12RootSignature> root_signature;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<d3d12::DescriptorAllocInfo> bindings;
    bool is_compute;
};

struct Pipeline {
    ComPtr<ID3D12PipelineState> pipeline_state;
    std::array<uint32_t, 16> vertex_strides;
    bool is_compute;
};

struct Shader {
    std::vector<char8_t> data;
    ShaderFlags flags;
};

struct RenderPass {
    std::array<D3D12_RENDER_PASS_RENDER_TARGET_DESC, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT> render_target_descs;
    uint32_t render_target_count;
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_stencil_desc;
    bool has_depth_stencil;
};

}

template<class Type, size_t Size>
class ResourceSet {
public:

    ResourceSet() {

       fill_data();
    }

    uint32_t push(Type const& element) {
        
        assert(!_ids.empty() && "The data set is full");

        uint32_t const index = _ids.front();
        _ids.pop_front();
        _data[index] = element;
        return index;
    }

    uint32_t push(Type&& element) {

        assert(!_ids.empty() && "The data set is full");

        uint32_t const index = _ids.front();
        _ids.pop_front();
        _data[index] = std::move(element);
        return index;
    }

    void erase(uint32_t const index) {

        _ids.emplace_back(index);
        _data[index] = Type {};
    }

    void clear() {

        _data.clear();
        _ids.clear();

        fill_data();
    }

    Type const& operator[](uint32_t const index) const {

        return _data[index];
    }

    Type& operator[](uint32_t const index) {

        return _data[index];
    }

private:

    std::vector<Type> _data;
    std::list<uint32_t> _ids;

    void fill_data() {

        _data.resize(Size);

        for(uint32_t i = 0; i < Size; ++i) {

            _ids.emplace_back(i);
        }
    }
};

struct Backend::Impl {

    d3d12::MemoryAllocator memory_allocator;

    std::vector<d3d12::CPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024>> cpu_cbv_srv_uav_pools;
    d3d12::CPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 128> cpu_rtv_pool;
    d3d12::CPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2> cpu_dsv_pool;
    d3d12::CPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024> cpu_sampler_pool;

    d3d12::GPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096> gpu_cbv_srv_uav_pool;
    d3d12::GPUDescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 512> gpu_sampler_pool;

    std::vector<d3d12::GPUDescriptorRange*> gpu_cbv_srv_uav_ranges;
    std::vector<d3d12::GPUDescriptorRange*> gpu_sampler_ranges;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<ID3D12Debug> debug;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device4> device;
    ComPtr<IDXGISwapChain3> swapchain;
    std::vector<ComPtr<ID3D12CommandQueue>> queues;
    std::vector<ComPtr<ID3D12Fence>> fences;
    std::vector<ID3D12CommandList*> batches;

    std::vector<uint64_t> fence_values;

    HANDLE fence_event;

    ResourceSet<Texture, std::to_underlying(BackendLimits::TextureCount)> textures;
    ResourceSet<Buffer, std::to_underlying(BackendLimits::BufferCount)> buffers;
    ResourceSet<Sampler, std::to_underlying(BackendLimits::SamplerCount)> samplers;
    ResourceSet<DescriptorLayout, std::to_underlying(BackendLimits::DescriptorLayoutCount)> descriptor_layouts;
    ResourceSet<DescriptorSet, std::to_underlying(BackendLimits::DescriptorSetCount)> descriptor_sets;
    ResourceSet<Pipeline, std::to_underlying(BackendLimits::PipelineCount)> pipelines;
    ResourceSet<Shader, std::to_underlying(BackendLimits::ShaderCount)> shaders;
    ResourceSet<RenderPass, std::to_underlying(BackendLimits::RenderPassCount)> render_passes;

    Texture default_texture;
    Buffer default_cbv_buffer;
    Sampler default_sampler;
    Buffer default_uav_buffer;

    std::vector<Handle<Texture>> swapchain_textures;
    uint32_t swapchain_index{0};

    std::filesystem::path shader_cache_path = "";
    uint32_t const SHADER_CACHE_MAGIC = ((uint32_t)(uint8_t)'D') | ((uint32_t)(uint8_t)'3' << 8) | ((uint32_t)(uint8_t)'D' << 16) | ((uint32_t)(uint8_t)'1' << 24);

    std::unordered_map<PipelineCacheId, ComPtr<ID3DBlob>> shader_cache_data;

    size_t serialize_shader_cache(std::vector<char8_t>& data);

    bool deserialize_shader_cache(std::vector<char8_t> const& data);

    DescriptorAllocInfo2 allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE const heap_type);

    void initialize(uint32_t const adapter_index, SwapchainDesc const& _swapchain_desc, std::filesystem::path const& shader_cache_path = "");

    void deinitialize();
    
    Handle<Texture> create_texture(
        Dimension const dimension, 
        uint32_t const width, 
        uint32_t const height, 
        uint16_t const mip_levels, 
        uint16_t const array_layers, 
        Format const format, 
        TextureFlags const flags
    );
    
    void delete_texture(Handle<Texture> const& texture);
    
    Handle<Buffer> create_buffer(size_t const size, BufferFlags const flags);
    
    void delete_buffer(Handle<Buffer> const& buffer);
    
    Handle<RenderPass> create_render_pass(
        std::span<Handle<Texture>> const& colors, 
        std::span<RenderPassColorDesc const> const& color_descs, 
        Handle<Texture> const& depth_stencil, 
        RenderPassDepthStencilDesc const& depth_stencil_desc
    );
    
    void delete_render_pass(Handle<RenderPass> const& render_pass);
    
    Handle<Sampler> create_sampler(
        Filter const filter, 
        AddressMode const address_u, 
        AddressMode const address_v, 
        AddressMode const address_w, 
        uint16_t const anisotropic, 
        CompareOp const compare_op
    );
    
    void delete_sampler(Handle<Sampler> const& sampler);
    
    Handle<Shader> create_shader(std::span<char8_t const> const data, ShaderFlags const flags);
    
    void delete_shader(Handle<Shader> const& shader);
    
    Handle<DescriptorLayout> create_descriptor_layout(std::span<DescriptorRangeDesc const> const ranges);
    
    void delete_descriptor_layout(Handle<DescriptorLayout> const& descriptor_layout);
    
    Handle<Pipeline> create_pipeline(
        Handle<DescriptorLayout> const& descriptor_layout, 
        std::span<VertexInputDesc const> const vertex_descs, 
        std::span<Handle<Shader>> const shaders, 
        RasterizerDesc const& rasterizer_desc, 
        DepthStencilDesc const& depth_stencil_desc, 
        BlendDesc const& blend_desc, 
        Handle<RenderPass> const& render_pass,
        std::optional<PipelineCacheId> pipeline_cache = std::nullopt
    );
    
    void delete_pipeline(Handle<Pipeline> const& pipeline);
    
    Handle<DescriptorSet> create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout);
    
    void delete_descriptor_set(Handle<DescriptorSet> const& descriptor_set);
    
    void update_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc const> const write_descs);

    void upload_buffer_data(Handle<Buffer> const& buffer, uint64_t const offset, std::span<char8_t> const data);
    
    void present();

    Handle<Texture> acquire_next_texture();

    void recreate_swapchain(uint32_t const width, uint32_t const height, SwapchainDesc const& swapchain_desc = {});

    FenceResultInfo submit(std::span<Encoder const> const encoders, EncoderFlags const flags);

    FenceResultInfo submit_after(std::span<Encoder const> const encoders, FenceResultInfo const& result_info_after, EncoderFlags const flags);

    void wait(FenceResultInfo const& result_info);

    bool is_completed(FenceResultInfo const& result_info) const;

    void wait_for_idle(EncoderFlags const flags);
};

void Backend::impl_deleter::operator()(Impl* ptr) const {

    delete ptr;
}

struct Encoder::Impl {

    Backend::Impl* backend;
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12GraphicsCommandList4> command_list;
    Pipeline* binded_pipeline;

    bool is_reset{false};

    void initialize(Backend& backend, EncoderFlags const flags);

    void reset();

    void bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set);

    void set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height);

    void set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom);

    void barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after);

    void begin_render_pass(
        Handle<RenderPass> const& render_pass, 
        std::span<Color> const clear_colors, 
        float const clear_depth = 0.0f, 
        uint8_t const clear_stencil = 0x0
    );

    void end_render_pass();

    void bind_pipeline(Handle<Pipeline> const& pipeline);

    void bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset);

    void bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset);

    void draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset);

    void draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset);

    void copy_buffer_region(
        Handle<Buffer> const& dest, 
        uint64_t const dest_offset, 
        Handle<Buffer> const& source, 
        uint64_t const source_offset, 
        size_t const size
    );
    
    void copy_texture_region();
};

void Encoder::impl_deleter::operator()(Impl* ptr) const {

    delete ptr;
}

//===========================================================
//
//
//          Backend Implementation
//
//
//===========================================================

size_t Backend::Impl::serialize_shader_cache(std::vector<char8_t>& data) {

    size_t total_bytes = 
        sizeof(uint32_t) + // Magic size
        sizeof(uint32_t) // Count size
    ;

    for(auto& [id, blob] : shader_cache_data) {

        total_bytes =
            total_bytes +
            sizeof(uint32_t) + // PipelineCache id
            sizeof(size_t) + // ShaderCache size
            sizeof(char8_t) * blob->GetBufferSize() // ShaderCache bytes size
        ;
    }

    data.resize(total_bytes);

    uint64_t offset = 0;

    std::memcpy(data.data(), &SHADER_CACHE_MAGIC, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    uint32_t const cache_count = static_cast<uint32_t>(shader_cache_data.size());
    std::memcpy(data.data() + offset, &cache_count, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    for(auto& [id, blob] : shader_cache_data) {

        std::memcpy(data.data() + offset, &id, sizeof(uint32_t));
        offset += sizeof(uint32_t);
        size_t const blob_size = blob->GetBufferSize();
        std::memcpy(data.data() + offset, &blob_size, sizeof(size_t));
        offset += sizeof(size_t);
        std::memcpy(data.data() + offset, blob->GetBufferPointer(), sizeof(char8_t) * blob->GetBufferSize());
        offset += sizeof(char8_t) * blob->GetBufferSize();
    }

    return total_bytes;
}
    
bool Backend::Impl::deserialize_shader_cache(std::vector<char8_t> const& data) {

    return true;
}

DescriptorAllocInfo2 Backend::Impl::allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE const heap_type) {

    auto alloc_info = d3d12::DescriptorAllocInfo {};
    uint32_t pool_index = 0;

    switch(heap_type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: {
            for(size_t i = 0; i < cpu_cbv_srv_uav_pools.size(); ++i) {
                if(cpu_cbv_srv_uav_pools[i].space() > 0) {
                    alloc_info = cpu_cbv_srv_uav_pools[i].allocate(device.Get());
                    pool_index = static_cast<uint32_t>(i);
                    break;
                }
            }
        } break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: {
            alloc_info = cpu_rtv_pool.allocate(device.Get());
        } break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: {
            alloc_info = cpu_dsv_pool.allocate(device.Get());
        } break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: {
            alloc_info = cpu_sampler_pool.allocate(device.Get());
        } break;
    }

    if(!alloc_info.heap) {
        throw ionengine::Exception(u8"Backend: Out of memory");
    }
    return { alloc_info, pool_index };
}

void Backend::Impl::initialize(uint32_t const adapter_index, SwapchainDesc const& _swapchain_desc, std::filesystem::path const& shader_cache_path) {

    this->shader_cache_path = shader_cache_path;

    THROW_IF_FAILED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), reinterpret_cast<void**>(debug.GetAddressOf())));
    debug->EnableDebugLayer();

    THROW_IF_FAILED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory4), reinterpret_cast<void**>(factory.GetAddressOf())));

    THROW_IF_FAILED(factory->EnumAdapters1(adapter_index, adapter.GetAddressOf()));
    
    THROW_IF_FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device4), reinterpret_cast<void**>(device.GetAddressOf())));
    
    cpu_cbv_srv_uav_pools.resize(4);

    queues.resize(3);
    fences.resize(3);

    auto queue_desc = D3D12_COMMAND_QUEUE_DESC {};
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    THROW_IF_FAILED(device->CreateCommandQueue(
        &queue_desc,
        __uuidof(ID3D12CommandQueue), 
        reinterpret_cast<void**>(queues[0].GetAddressOf()))
    );

    THROW_IF_FAILED(device->CreateFence(
        0, 
        D3D12_FENCE_FLAG_NONE, 
        __uuidof(ID3D12Fence), 
        reinterpret_cast<void**>(fences[0].GetAddressOf()))
    );

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

    THROW_IF_FAILED(device->CreateCommandQueue(
        &queue_desc,
        __uuidof(ID3D12CommandQueue), 
        reinterpret_cast<void**>(queues[1].GetAddressOf()))
    );

    THROW_IF_FAILED(device->CreateFence(
        0, 
        D3D12_FENCE_FLAG_NONE, 
        __uuidof(ID3D12Fence), 
        reinterpret_cast<void**>(fences[1].GetAddressOf()))
    );

    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    THROW_IF_FAILED(device->CreateCommandQueue(
        &queue_desc,
        __uuidof(ID3D12CommandQueue), 
        reinterpret_cast<void**>(queues[2].GetAddressOf()))
    );

    THROW_IF_FAILED(device->CreateFence(
        0, 
        D3D12_FENCE_FLAG_NONE, 
        __uuidof(ID3D12Fence), 
        reinterpret_cast<void**>(fences[2].GetAddressOf()))
    );

    fence_values.resize(3);

    fence_event = CreateEvent(nullptr, false, false, nullptr);

    if(!fence_event) {
        THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
    }

    platform::Size window_size = _swapchain_desc.window->client_size();

    auto swapchain_desc = DXGI_SWAP_CHAIN_DESC1 {};
    swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.Width = window_size.width;
    swapchain_desc.Height = window_size.height;
    swapchain_desc.BufferCount = _swapchain_desc.buffer_count;
    swapchain_desc.SampleDesc.Count = _swapchain_desc.sample_count;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    THROW_IF_FAILED(factory->CreateSwapChainForHwnd(
        queues[0].Get(), 
        reinterpret_cast<HWND>(_swapchain_desc.window->native_handle()), 
        &swapchain_desc, 
        nullptr, 
        nullptr, 
        reinterpret_cast<IDXGISwapChain1**>(swapchain.GetAddressOf()))
    );

    for(uint32_t i = 0; i < _swapchain_desc.buffer_count; ++i) {

        gpu_cbv_srv_uav_ranges.push_back(gpu_cbv_srv_uav_pool.allocate(device.Get(), 1024));
        gpu_sampler_ranges.push_back(gpu_sampler_pool.allocate(device.Get(), 256));

        // Render Target from swapchain resource
        {
            ComPtr<ID3D12Resource> resource;
            THROW_IF_FAILED(swapchain->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(resource.GetAddressOf())));

            auto view_desc = D3D12_RENDER_TARGET_VIEW_DESC {};
            view_desc.Format = swapchain_desc.Format;
            view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            DescriptorAllocInfo2 alloc_info = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(resource.Get(), &view_desc, alloc_info.first.cpu_handle());

            auto texture = Texture {};
            texture.resource = resource;
            texture.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = alloc_info;

            uint32_t id = textures.push(std::move(texture));
            swapchain_textures.emplace_back(id);
        }
    }
}

void Backend::Impl::deinitialize() {

    if(!shader_cache_path.empty() && !shader_cache_data.empty()) {
        std::vector<char8_t> data;
        serialize_shader_cache(data);

        if(!save_bytes_to_file(shader_cache_path, data, std::ios::binary)) {
            std::cerr << "[Error] Backend: Unable to write shader cache to disk" << std::endl;
        }
    } else {
        std::cerr << "[Warning] Backend: Shader caches not found to write to disk" << std::endl;
    }

    wait_for_idle(EncoderFlags::Graphics | EncoderFlags::Copy | EncoderFlags::Compute);

    CloseHandle(fence_event);
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

    auto texture_data = Texture {};

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
    if(!(flags & TextureFlags::ShaderResource)) {
        resource_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }

    D3D12_RESOURCE_ALLOCATION_INFO resource_alloc_info = device->GetResourceAllocationInfo(0, 1, &resource_desc);

    texture_data.memory_alloc_info = memory_allocator.allocate(
        device.Get(), 
        D3D12_HEAP_TYPE_DEFAULT, 
        resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment
    );

    THROW_IF_FAILED(device->CreatePlacedResource(
        texture_data.memory_alloc_info.heap(),
        texture_data.memory_alloc_info.offset(),
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource), 
        reinterpret_cast<void**>(texture_data.resource.GetAddressOf())
    ));

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

        texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        device->CreateDepthStencilView(texture_data.resource.Get(), &view_desc, texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_DSV].first.cpu_handle());
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
            } break;
            case Dimension::_3D: {
                view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
                view_desc.Texture3D.MipSlice = mip_levels;
                view_desc.Texture3D.WSize = array_layers;
            } break;
        }

        texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        device->CreateRenderTargetView(texture_data.resource.Get(), &view_desc, texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].first.cpu_handle());
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

        texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateShaderResourceView(texture_data.resource.Get(), &view_desc, texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].first.cpu_handle());
    }

    uint32_t id = textures.push(std::move(texture_data));
    return Handle<Texture>(id);
}

void Backend::Impl::delete_texture(Handle<Texture> const& texture) {

    auto& texture_data = textures[texture.id];
    
    D3D12_RESOURCE_DESC resource_desc = texture_data.resource->GetDesc();

    if(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        cpu_rtv_pool.deallocate(texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].first);
    }
    if(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        cpu_dsv_pool.deallocate(texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_DSV].first);
    }
    if(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
        auto& alloc_info = texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
        cpu_cbv_srv_uav_pools[alloc_info.second].deallocate(alloc_info.first);
    }
    if(!(resource_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        auto& alloc_info = texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
        cpu_cbv_srv_uav_pools[alloc_info.second].deallocate(alloc_info.first);
    }

    memory_allocator.deallocate(texture_data.memory_alloc_info);

    textures.erase(texture.id);
}

Handle<Buffer> Backend::Impl::create_buffer(size_t const size, BufferFlags const flags) {

    auto buffer_data = Buffer {};

    auto resource_desc = D3D12_RESOURCE_DESC {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.MipLevels = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_STATES initial_state;
    D3D12_RESOURCE_ALLOCATION_INFO resource_alloc_info = device->GetResourceAllocationInfo(0, 1, &resource_desc);

    if(flags & BufferFlags::HostWrite) {
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        buffer_data.memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_UPLOAD, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
    } else if(flags & BufferFlags::HostRead) {
        initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        buffer_data.memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_READBACK, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
    } else {
        initial_state = D3D12_RESOURCE_STATE_COMMON;
        buffer_data.memory_alloc_info = memory_allocator.allocate(device.Get(), D3D12_HEAP_TYPE_DEFAULT, resource_alloc_info.SizeInBytes + resource_alloc_info.Alignment);
    }

    THROW_IF_FAILED(device->CreatePlacedResource(
        buffer_data.memory_alloc_info.heap(),
        buffer_data.memory_alloc_info.offset(),
        &resource_desc,
        initial_state,
        nullptr,
        __uuidof(ID3D12Resource), 
        reinterpret_cast<void**>(buffer_data.resource.GetAddressOf())
    ));

    if(flags & BufferFlags::ConstantBuffer) {

        auto view_desc = D3D12_CONSTANT_BUFFER_VIEW_DESC {};
        view_desc.BufferLocation = buffer_data.resource->GetGPUVirtualAddress();
        view_desc.SizeInBytes = static_cast<uint32_t>(size);

        buffer_data.descriptor_alloc_info = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateConstantBufferView(&view_desc, buffer_data.descriptor_alloc_info.first.cpu_handle());
    }
    if(flags & BufferFlags::UnorderedAccess) {

        auto view_desc = D3D12_UNORDERED_ACCESS_VIEW_DESC {};
        view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view_desc.Format = resource_desc.Format;
        // TODO!

        buffer_data.descriptor_alloc_info = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CreateUnorderedAccessView(buffer_data.resource.Get(), nullptr, &view_desc, buffer_data.descriptor_alloc_info.first.cpu_handle());
    }

    uint32_t id = buffers.push(std::move(buffer_data));
    return Handle<Buffer>(id);
}

void Backend::Impl::delete_buffer(Handle<Buffer> const& buffer) {

    auto& buffer_data = buffers[buffer.id];
    
    cpu_cbv_srv_uav_pools[buffer_data.descriptor_alloc_info.second].deallocate(buffer_data.descriptor_alloc_info.first);
    memory_allocator.deallocate(buffer_data.memory_alloc_info);

    buffers.erase(buffer.id);
}

Handle<RenderPass> Backend::Impl::create_render_pass(
    std::span<Handle<Texture>> const& colors, 
    std::span<RenderPassColorDesc const> const& color_descs, 
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

    auto render_pass_data = RenderPass {};
    render_pass_data.render_target_count = static_cast<uint32_t>(colors.size());

    for(uint32_t i = 0; i < render_pass_data.render_target_count; ++i) {

        auto& texture_data = textures[colors[i].id];

        auto begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        begin.Type = get_render_pass_begin_type(color_descs[i].load_op);
        begin.Clear.ClearValue.Format = texture_data.resource->GetDesc().Format;

        auto end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        end.Type = get_render_pass_end_type(color_descs[i].store_op);

        render_pass_data.render_target_descs[i].BeginningAccess = begin;
        render_pass_data.render_target_descs[i].EndingAccess = end;
        render_pass_data.render_target_descs[i].cpuDescriptor = texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].first.cpu_handle();
    }

    if(depth_stencil != INVALID_HANDLE(Texture)) {

        auto& texture_data = textures[depth_stencil.id];

        auto depth_begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        depth_begin.Type = get_render_pass_begin_type(depth_stencil_desc.depth_load_op);
        depth_begin.Clear.ClearValue.Format = texture_data.resource->GetDesc().Format;

        auto stencil_begin = D3D12_RENDER_PASS_BEGINNING_ACCESS {};
        stencil_begin.Type = get_render_pass_begin_type(depth_stencil_desc.stencil_load_op);
        stencil_begin.Clear.ClearValue.Format = texture_data.resource->GetDesc().Format;

        auto depth_end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        depth_end.Type = get_render_pass_end_type(depth_stencil_desc.depth_store_op);

        auto stencil_end = D3D12_RENDER_PASS_ENDING_ACCESS {};
        stencil_end.Type = get_render_pass_end_type(depth_stencil_desc.stencil_store_op);

        render_pass_data.depth_stencil_desc.DepthBeginningAccess = depth_begin;
        render_pass_data.depth_stencil_desc.StencilBeginningAccess = stencil_begin;
        render_pass_data.depth_stencil_desc.DepthEndingAccess = depth_end;
        render_pass_data.depth_stencil_desc.StencilEndingAccess = stencil_end;
        render_pass_data.depth_stencil_desc.cpuDescriptor = texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_DSV].first.cpu_handle();

        render_pass_data.has_depth_stencil = true;
    }

    uint32_t id = render_passes.push(std::move(render_pass_data));
    return Handle<RenderPass>(id);
}

void Backend::Impl::delete_render_pass(Handle<RenderPass> const& render_pass) {

    render_passes.erase(render_pass.id);
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

    auto sampler_data = Sampler {};

    auto sampler_desc = D3D12_SAMPLER_DESC {};
    sampler_desc.Filter = get_filter(filter);
    sampler_desc.AddressU = get_address_mode(address_u);
    sampler_desc.AddressV = get_address_mode(address_v);
    sampler_desc.AddressW = get_address_mode(address_w);
    sampler_desc.ComparisonFunc = get_comparison_func(compare_op);
    sampler_desc.MaxAnisotropy = anisotropic;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    
    sampler_data.alloc_info = allocate_descriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    device->CreateSampler(&sampler_desc, sampler_data.alloc_info.first.cpu_handle());

    uint32_t id = samplers.push(std::move(sampler_data));
    return Handle<Sampler>(id);
}

void Backend::Impl::delete_sampler(Handle<Sampler> const& sampler) {

    auto& sampler_data = samplers[sampler.id];
    cpu_sampler_pool.deallocate(sampler_data.alloc_info.first);
    samplers.erase(sampler.id);
}

Handle<Shader> Backend::Impl::create_shader(std::span<char8_t const> const data, ShaderFlags const flags) {

    auto shader_data = Shader {};
    shader_data.flags = flags;

    shader_data.data.resize(data.size());
    std::memcpy(shader_data.data.data(), data.data(), sizeof(char8_t) * shader_data.data.size());
    
    uint32_t id = this->shaders.push(std::move(shader_data));
    return Handle<Shader>(id);
}

void Backend::Impl::delete_shader(Handle<Shader> const& shader) {

    shaders.erase(shader.id);
}

Handle<DescriptorLayout> Backend::Impl::create_descriptor_layout(std::span<DescriptorRangeDesc const> const ranges) {

    auto get_descriptor_range_type = [&](DescriptorRangeType const range_type) -> D3D12_DESCRIPTOR_RANGE_TYPE {
        switch(range_type) {
            case DescriptorRangeType::ShaderResource: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            case DescriptorRangeType::ConstantBuffer: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            case DescriptorRangeType::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            case DescriptorRangeType::UnorderedAccess: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        }
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    };

    auto get_shader_visibility = [&](ShaderFlags const shader_flags) -> D3D12_SHADER_VISIBILITY {
        switch(shader_flags) {
            case ShaderFlags::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
            case ShaderFlags::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
            case ShaderFlags::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
            case ShaderFlags::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
            case ShaderFlags::Hull: return D3D12_SHADER_VISIBILITY_HULL;
            case ShaderFlags::All: return D3D12_SHADER_VISIBILITY_ALL;
        }
        return D3D12_SHADER_VISIBILITY_ALL;
    };

    auto descriptor_layout_data = DescriptorLayout {};

    descriptor_layout_data.ranges.reserve(ranges.size());

    std::vector<D3D12_ROOT_PARAMETER> parameters;
    parameters.reserve(ranges.size());

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
        descriptor_layout_data.ranges.emplace_back(std::move(_range));

        auto parameter = D3D12_ROOT_PARAMETER {};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable.pDescriptorRanges = &descriptor_layout_data.ranges.back();
        parameter.DescriptorTable.NumDescriptorRanges = 1;
        parameter.ShaderVisibility = get_shader_visibility(range.flags);

        parameters.emplace_back(std::move(parameter));
    }

    auto root_desc = D3D12_ROOT_SIGNATURE_DESC {};
    root_desc.pParameters = parameters.data();
    root_desc.NumParameters = static_cast<uint32_t>(parameters.size());
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized_data;

    THROW_IF_FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, serialized_data.GetAddressOf(), nullptr));

    THROW_IF_FAILED(device->CreateRootSignature(
        0,
        serialized_data->GetBufferPointer(), 
        serialized_data->GetBufferSize(), 
        __uuidof(ID3D12RootSignature), 
        reinterpret_cast<void**>(descriptor_layout_data.root_signature.GetAddressOf())
    ));

    uint32_t id = descriptor_layouts.push(std::move(descriptor_layout_data));
    return Handle<DescriptorLayout>(id);
}

void Backend::Impl::delete_descriptor_layout(Handle<DescriptorLayout> const& descriptor_layout) {

    descriptor_layouts.erase(descriptor_layout.id);
}

Handle<Pipeline> Backend::Impl::create_pipeline(
    Handle<DescriptorLayout> const& descriptor_layout,
    std::span<VertexInputDesc const> const vertex_descs,
    std::span<Handle<Shader>> const shaders,
    RasterizerDesc const& rasterizer_desc,
    DepthStencilDesc const& depth_stencil_desc,
    BlendDesc const& blend_desc,
    Handle<RenderPass> const& render_pass,
    std::optional<PipelineCacheId> pipeline_cache
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

    auto pipeline_data = Pipeline {};

    auto& descriptor_layout_data = descriptor_layouts[descriptor_layout.id];

    auto pipeline_desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {};
    pipeline_desc.pRootSignature = descriptor_layout_data.root_signature.Get();

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_element_descs;
    input_element_descs.reserve(vertex_descs.size());

    pipeline_data.vertex_strides.fill(0);

    for(auto const& input : vertex_descs) {

        auto input_element_desc = D3D12_INPUT_ELEMENT_DESC {};
        input_element_desc.SemanticName = input.semantic.c_str();
        input_element_desc.InputSlot = input.slot;
        input_element_desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        input_element_desc.Format = get_dxgi_format(input.format);
        input_element_desc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
        input_element_descs.emplace_back(input_element_desc);

        pipeline_data.vertex_strides[input.slot] += input.stride;
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

        auto& shader_data = this->shaders[shader.id];
        
        auto shader_bytecode = D3D12_SHADER_BYTECODE {};
        shader_bytecode.pShaderBytecode = shader_data.data.data();
        shader_bytecode.BytecodeLength = shader_data.data.size();

        switch(shader_data.flags) {
            case ShaderFlags::Vertex: pipeline_desc.VS = shader_bytecode; break;
            case ShaderFlags::Pixel: pipeline_desc.PS = shader_bytecode; break;
            case ShaderFlags::Geometry: pipeline_desc.GS = shader_bytecode; break;
            case ShaderFlags::Domain: pipeline_desc.DS = shader_bytecode; break;
            case ShaderFlags::Hull: pipeline_desc.HS = shader_bytecode; break;
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

    auto render_pass_data = render_passes[render_pass.id];

    for(uint32_t i = 0; i < render_pass_data.render_target_count; ++i) {
        _blend_desc.RenderTarget[i] = render_target_blend_desc;
        pipeline_desc.RTVFormats[i] = render_pass_data.render_target_descs[i].BeginningAccess.Clear.ClearValue.Format;
    }

    pipeline_desc.DSVFormat = render_pass_data.has_depth_stencil ? render_pass_data.depth_stencil_desc.DepthBeginningAccess.Clear.ClearValue.Format : DXGI_FORMAT_UNKNOWN;
    pipeline_desc.NumRenderTargets = render_pass_data.render_target_count;
    pipeline_desc.BlendState = _blend_desc;
    pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline_desc.SampleMask = std::numeric_limits<uint32_t>::max();

    // TODO!
    pipeline_desc.SampleDesc.Count = 1;

    bool is_cached = false;

    if(pipeline_cache.has_value()) {
        
        auto it = shader_cache_data.find(pipeline_cache.value());

        if(it != shader_cache_data.end()) {

            auto cached_pso_state = D3D12_CACHED_PIPELINE_STATE {};
            cached_pso_state.pCachedBlob = it->second->GetBufferPointer();
            cached_pso_state.CachedBlobSizeInBytes = it->second->GetBufferSize();

            pipeline_desc.CachedPSO = cached_pso_state;
            is_cached = true;
        }
    }

    THROW_IF_FAILED(device->CreateGraphicsPipelineState(
        &pipeline_desc,
        __uuidof(ID3D12PipelineState), 
        reinterpret_cast<void**>(pipeline_data.pipeline_state.GetAddressOf())
    ));

    if(!is_cached && pipeline_cache.has_value()) {

        ComPtr<ID3DBlob> buffer;
        pipeline_data.pipeline_state->GetCachedBlob(buffer.GetAddressOf());
        
        shader_cache_data[pipeline_cache.value()] = buffer;
        is_cached = true;
    }

    uint32_t id = pipelines.push(std::move(pipeline_data));
    return Handle<Pipeline>(id);
}

void Backend::Impl::delete_pipeline(Handle<Pipeline> const& pipeline) {

    pipelines.erase(pipeline.id);
}

Handle<DescriptorSet> Backend::Impl::create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout) {

    auto descriptor_set_data = DescriptorSet {};
    auto& descriptor_layout_data = descriptor_layouts[descriptor_layout.id];

    descriptor_set_data.root_signature = descriptor_layout_data.root_signature;
    descriptor_set_data.ranges = descriptor_layout_data.ranges;
    descriptor_set_data.is_compute = descriptor_layout_data.is_compute;

    for(size_t i = 0; i < descriptor_layout_data.ranges.size(); ++i) {

        auto& range = descriptor_layout_data.ranges[i];
        for(uint32_t j = 0; j < range.NumDescriptors; ++j) {

            descriptor_set_data.bindings.emplace_back(d3d12::DescriptorAllocInfo {});
        }
    }

    uint32_t id = descriptor_sets.push(std::move(descriptor_set_data));    
    return Handle<DescriptorSet>(id);
}

void Backend::Impl::delete_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    descriptor_sets.erase(descriptor_set.id);
}

void Backend::Impl::update_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc const> const write_descs) {

    auto& descriptor_set_data = descriptor_sets[descriptor_set.id];

    for(auto& write_desc : write_descs) {

        auto alloc_info = d3d12::DescriptorAllocInfo {};

        auto resource_visitor = make_visitor(
            [&](Handle<Texture> const& resource) {
                auto& texture_data = textures[resource.id];
                alloc_info = texture_data.descriptor_alloc_infos[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].first;
            },
            [&](Handle<Buffer> const& resource) {
                auto& buffer_data = buffers[resource.id];
                alloc_info = buffer_data.descriptor_alloc_info.first;
            },
            [&](Handle<Sampler> const& resource) {
                auto& sampler_data = samplers[resource.id];
                alloc_info = sampler_data.alloc_info.first;
            }
        );

        std::visit(resource_visitor, write_desc.data);

        descriptor_set_data.bindings[write_desc.index] = alloc_info;
    }
}

void Backend::Impl::upload_buffer_data(Handle<Buffer> const& buffer, uint64_t const offset, std::span<char8_t> const data) {

    auto& buffer_data = buffers[buffer.id];

    D3D12_HEAP_DESC heap_desc = buffer_data.memory_alloc_info.heap()->GetDesc();

    if(heap_desc.Properties.Type == D3D12_HEAP_TYPE_UPLOAD || heap_desc.Properties.Type == D3D12_HEAP_TYPE_READBACK) {

        char8_t* bytes;
        auto range = D3D12_RANGE {};

        THROW_IF_FAILED(buffer_data.resource->Map(0, &range, reinterpret_cast<void**>(&bytes)));
        std::memcpy(bytes + offset, data.data(), data.size());
        buffer_data.resource->Unmap(0, &range);
    } else {
        assert(false && "invalid buffer");
    }
}

Handle<Texture> Backend::Impl::acquire_next_texture() {

    swapchain_index = swapchain->GetCurrentBackBufferIndex();
    return swapchain_textures[swapchain_index];
}

void Backend::Impl::present() {

    swapchain->Present(0, 0);
}

void Backend::Impl::recreate_swapchain(uint32_t const width, uint32_t const height, SwapchainDesc const& swapchain_desc) {


}

FenceResultInfo Backend::Impl::submit(std::span<Encoder const> const encoders, EncoderFlags const flags) {

    auto get_queue_index = [&](EncoderFlags const encoder_flags) {
        switch(encoder_flags) {
            case EncoderFlags::Graphics: return 0;
            case EncoderFlags::Copy: return 1;
            case EncoderFlags::Compute: return 2;
        }
        return 0;
    };

    batches.clear();

    for(auto& encoder : encoders) {
        encoder._impl->command_list->Close();
        encoder._impl->is_reset = false;
        batches.push_back(reinterpret_cast<ID3D12CommandList* const>(encoder._impl->command_list.Get()));
    }

    queues[get_queue_index(flags)]->ExecuteCommandLists(static_cast<uint32_t>(batches.size()), batches.data());

    uint64_t& fence_value = fence_values[get_queue_index(flags)];

    auto result_info = FenceResultInfo {};
    result_info.flags = flags;
    result_info.value = fence_value;

    THROW_IF_FAILED(queues[get_queue_index(flags)]->Signal(fences[get_queue_index(flags)].Get(), fence_value));

    ++fence_value;
    return result_info;
}

FenceResultInfo Backend::Impl::submit_after(std::span<Encoder const> const encoders, FenceResultInfo const& result_info_after, EncoderFlags const flags) {
    
    auto get_queue_index = [&](EncoderFlags const encoder_flags) {
        switch(encoder_flags) {
            case EncoderFlags::Graphics: return 0;
            case EncoderFlags::Copy: return 1;
            case EncoderFlags::Compute: return 2;
        }
        return 0;
    };

    batches.clear();

    for(auto& encoder : encoders) {
        encoder._impl->command_list->Close();
        encoder._impl->is_reset = false;
        batches.push_back(reinterpret_cast<ID3D12CommandList* const>(encoder._impl->command_list.GetAddressOf()));
    }

    THROW_IF_FAILED(queues[get_queue_index(flags)]->Wait(fences[get_queue_index(result_info_after.flags)].Get(), result_info_after.value));
    queues[get_queue_index(flags)]->ExecuteCommandLists(static_cast<uint32_t>(batches.size()), batches.data());

    uint64_t& fence_value = fence_values[get_queue_index(flags)];

    auto result_info = FenceResultInfo {};
    result_info.flags = flags;
    result_info.value = fence_value;

    THROW_IF_FAILED(queues[get_queue_index(flags)]->Signal(fences[get_queue_index(flags)].Get(), fence_value));

    ++fence_value;
    return result_info;
}

void Backend::Impl::wait(FenceResultInfo const& result_info) {
    
    auto get_queue_index = [&](EncoderFlags const encoder_flags) {
        switch(encoder_flags) {
            case EncoderFlags::Graphics: return 0;
            case EncoderFlags::Copy: return 1;
            case EncoderFlags::Compute: return 2;
        }
        return 0;
    };

    if(fences[get_queue_index(result_info.flags)]->GetCompletedValue() < result_info.value) {
        THROW_IF_FAILED(fences[get_queue_index(result_info.flags)]->SetEventOnCompletion(result_info.value, fence_event));
        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
    }
}

bool Backend::Impl::is_completed(FenceResultInfo const& result_info) const {
    
    return fences[static_cast<uint32_t>(result_info.flags)]->GetCompletedValue() >= result_info.value;
}

void Backend::Impl::wait_for_idle(EncoderFlags const flags) {

    if(flags & EncoderFlags::Graphics) {

        uint64_t& fence_value = fence_values[0];

        THROW_IF_FAILED(queues[0]->Signal(fences[0].Get(), fence_value));
        THROW_IF_FAILED(fences[0]->SetEventOnCompletion(fence_value, fence_event));

        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        
        ++fence_value;
    }

    if(flags & EncoderFlags::Copy) {

        uint64_t& fence_value = fence_values[1];

        THROW_IF_FAILED(queues[1]->Signal(fences[1].Get(), fence_value));
        THROW_IF_FAILED(fences[1]->SetEventOnCompletion(fence_value, fence_event));

        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        
        ++fence_value;
    }

    if(flags & EncoderFlags::Compute) {

        uint64_t& fence_value = fence_values[2];

        THROW_IF_FAILED(queues[2]->Signal(fences[2].Get(), fence_value));
        THROW_IF_FAILED(fences[2]->SetEventOnCompletion(fence_value, fence_event));

        WaitForSingleObjectEx(fence_event, INFINITE, FALSE);
        
        ++fence_value;
    }
}

//===========================================================
//
//
//          Backend Pointer to Implementation
//
//
//===========================================================

Backend::Backend(uint32_t const adapter_index, SwapchainDesc const& swapchain_desc, std::filesystem::path const& shader_cache_path) : 
    _impl(std::unique_ptr<Impl, impl_deleter>(new Impl())) {

    _impl->initialize(adapter_index, swapchain_desc, shader_cache_path);
}

Backend::~Backend() {

    _impl->deinitialize();
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

void Backend::delete_texture(Handle<Texture> const& texture) {

    _impl->delete_texture(texture);
}

Handle<Buffer> Backend::create_buffer(size_t const size, BufferFlags const flags) {

    return _impl->create_buffer(size, flags); 
}

void Backend::delete_buffer(Handle<Buffer> const& buffer) {

    _impl->delete_buffer(buffer);
}

Handle<RenderPass> Backend::create_render_pass(
    std::span<Handle<Texture>> const& colors, 
    std::span<RenderPassColorDesc const> const& color_descs, 
    Handle<Texture> const& depth_stencil, 
    RenderPassDepthStencilDesc const& depth_stencil_desc
) {

    return _impl->create_render_pass(colors, color_descs, depth_stencil, depth_stencil_desc);
}

void Backend::delete_render_pass(Handle<RenderPass> const& render_pass) {

    _impl->delete_render_pass(render_pass);
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

void Backend::delete_sampler(Handle<Sampler> const& sampler) {

    _impl->delete_sampler(sampler);
}

Handle<Shader> Backend::create_shader(std::span<char8_t const> const data, ShaderFlags const flags) {

    return _impl->create_shader(data, flags);
}

void Backend::delete_shader(Handle<Shader> const& shader) {

    _impl->delete_shader(shader);
}

Handle<DescriptorLayout> Backend::create_descriptor_layout(std::span<DescriptorRangeDesc const> const ranges) {

    return _impl->create_descriptor_layout(ranges);
}

void Backend::delete_descriptor_layout(Handle<DescriptorLayout> const& descriptor_layout) {

    _impl->delete_descriptor_layout(descriptor_layout);
}

Handle<Pipeline> Backend::create_pipeline(
    Handle<DescriptorLayout> const& descriptor_layout,
    std::span<VertexInputDesc const> const vertex_descs,
    std::span<Handle<Shader>> const shaders,
    RasterizerDesc const& rasterizer_desc,
    DepthStencilDesc const& depth_stencil_desc,
    BlendDesc const& blend_desc,
    Handle<RenderPass> const& render_pass,
    std::optional<PipelineCacheId> pipeline_cache
) {
    
    return _impl->create_pipeline(descriptor_layout, vertex_descs, shaders, rasterizer_desc, depth_stencil_desc, blend_desc, render_pass);
}

void Backend::delete_pipeline(Handle<Pipeline> const& pipeline) {

    _impl->delete_pipeline(pipeline);
}

Handle<DescriptorSet> Backend::create_descriptor_set(Handle<DescriptorLayout> const& descriptor_layout) {

    return _impl->create_descriptor_set(descriptor_layout);
}

void Backend::delete_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    _impl->delete_descriptor_set(descriptor_set);
}

void Backend::update_descriptor_set(Handle<DescriptorSet> const& descriptor_set, std::span<DescriptorWriteDesc const> const write_descs) {

    _impl->update_descriptor_set(descriptor_set, write_descs);
}

void Backend::upload_buffer_data(Handle<Buffer> const& buffer, uint64_t const offset, std::span<char8_t> const data) {

    _impl->upload_buffer_data(buffer, offset, data);
}

void Backend::present() {

    _impl->present();
}

Handle<Texture> Backend::acquire_next_texture() {

    return _impl->acquire_next_texture();
}

void Backend::recreate_swapchain(uint32_t const width, uint32_t const height, SwapchainDesc const& swapchain_desc) {
    
    _impl->recreate_swapchain(width, height, swapchain_desc);
}

FenceResultInfo Backend::submit(std::span<Encoder const> const encoders, EncoderFlags const flags) {

    return _impl->submit(encoders, flags);
}

FenceResultInfo Backend::submit_after(std::span<Encoder const> const encoders, FenceResultInfo const& result_info_after, EncoderFlags const flags) {
    
    return _impl->submit_after(encoders, result_info_after, flags);
}

void Backend::wait(FenceResultInfo const& result_info) {
    
    return _impl->wait(result_info);
}

bool Backend::is_completed(FenceResultInfo const& result_info) const {
    
    return _impl->is_completed(result_info);
}

void Backend::wait_for_idle(EncoderFlags const flags) {
    
    return _impl->wait_for_idle(flags);
}

//===========================================================
//
//
//          Encoder Implementation
//
//
//===========================================================

void Encoder::Impl::initialize(Backend& backend, EncoderFlags const flags) {

    auto get_command_list_type = [&](EncoderFlags const encoder_flags) -> D3D12_COMMAND_LIST_TYPE {
        switch(encoder_flags) {
            case EncoderFlags::Graphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
            case EncoderFlags::Copy: return D3D12_COMMAND_LIST_TYPE_COPY;
            case EncoderFlags::Compute: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        }
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    };

    this->backend = backend._impl.get();
    
    THROW_IF_FAILED(this->backend->device->CreateCommandAllocator(
        get_command_list_type(flags), 
        __uuidof(ID3D12CommandAllocator), 
        reinterpret_cast<void**>(command_allocator.GetAddressOf()))
    );
        
    THROW_IF_FAILED(this->backend->device->CreateCommandList1(
        0, 
        get_command_list_type(flags), 
        D3D12_COMMAND_LIST_FLAG_NONE,
        __uuidof(ID3D12GraphicsCommandList4),
        reinterpret_cast<void**>(command_list.GetAddressOf()))
    );
}

void Encoder::Impl::reset() {

    assert(!is_reset && "Encoder has already been reset");

    THROW_IF_FAILED(command_allocator->Reset());

    THROW_IF_FAILED(command_list->Reset(command_allocator.Get(), nullptr));

    if(command_list->GetType() == D3D12_COMMAND_LIST_TYPE_DIRECT) {

        uint32_t const swap_index = backend->swapchain_index;

        std::array<ID3D12DescriptorHeap*, 2> descriptor_heaps;
        descriptor_heaps[0] = backend->gpu_cbv_srv_uav_ranges[swap_index]->get_heap();
        descriptor_heaps[1] = backend->gpu_sampler_ranges[swap_index]->get_heap();

        command_list->SetDescriptorHeaps(2, descriptor_heaps.data());
    }

    is_reset = true;
}

void Encoder::Impl::bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    if(!is_reset) {

        reset();
    }

    auto& descriptor_set_data = backend->descriptor_sets[descriptor_set.id];

    if(!descriptor_set_data.is_compute) {
        command_list->SetGraphicsRootSignature(descriptor_set_data.root_signature.Get());
    } else {
        command_list->SetComputeRootSignature(descriptor_set_data.root_signature.Get());
    }

    uint32_t binding_count = 0;

    for(size_t i = 0; i < descriptor_set_data.ranges.size(); ++i) {

        auto alloc_info = d3d12::DescriptorAllocInfo {};

        auto& range = descriptor_set_data.ranges[i];
        switch(range.RangeType) {
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: {
                alloc_info = backend->gpu_cbv_srv_uav_ranges[backend->swapchain_index]->allocate(range);
            } break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: {
                alloc_info = backend->gpu_sampler_ranges[backend->swapchain_index]->allocate(range);
            }
        }

        for(uint32_t i = 0; i < range.NumDescriptors; ++i) {

            backend->device->CopyDescriptorsSimple(
                1, 
                alloc_info.cpu_handle(i), 
                descriptor_set_data.bindings[binding_count + i].cpu_handle(), 
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        }

        binding_count += range.NumDescriptors;

        if(!descriptor_set_data.is_compute) {
            command_list->SetGraphicsRootDescriptorTable(static_cast<uint32_t>(i), alloc_info.gpu_handle());
        } else {
            command_list->SetComputeRootDescriptorTable(static_cast<uint32_t>(i), alloc_info.gpu_handle());   
        }
    }
}

void Encoder::Impl::set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height) {

    if(!is_reset) {

        reset();
    }

    auto viewport = D3D12_VIEWPORT {};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = D3D12_MIN_DEPTH;
    viewport.MaxDepth = D3D12_MAX_DEPTH;

    command_list->RSSetViewports(1, &viewport);
}

void Encoder::Impl::set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom) {

    if(!is_reset) {
        
        reset();
    }

    auto rect = D3D12_RECT {};
    rect.top = static_cast<LONG>(top);
    rect.bottom = static_cast<LONG>(bottom);
    rect.left = static_cast<LONG>(left);
    rect.right = static_cast<LONG>(right);

    command_list->RSSetScissorRects(1, &rect);
}

void Encoder::Impl::barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after) {

    if(!is_reset) {
        
        reset();
    }

    auto get_memory_state = [&](MemoryState const state) -> D3D12_RESOURCE_STATES {
        switch(state) {
		    case MemoryState::Common: return D3D12_RESOURCE_STATE_COMMON;
		    case MemoryState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
		    case MemoryState::Present: return D3D12_RESOURCE_STATE_PRESENT;
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
            resource_barrier.Transition.pResource = backend->textures[texture.id].resource.Get();
        },
        [&](Handle<Buffer> const& buffer) {
            resource_barrier.Transition.pResource = backend->buffers[buffer.id].resource.Get();
        }
    );

    std::visit(barrier_visitor, target);

    command_list->ResourceBarrier(1, &resource_barrier);
}

void Encoder::Impl::begin_render_pass(
    Handle<RenderPass> const& render_pass, 
    std::span<Color> const clear_colors, 
    float const clear_depth, 
    uint8_t const clear_stencil
) {

    if(!is_reset) {
        
        reset();
    }

    auto& render_pass_data = backend->render_passes[render_pass.id];

    for(uint32_t i = 0; i < render_pass_data.render_target_count; ++i) {
        std::memcpy(render_pass_data.render_target_descs[i].BeginningAccess.Clear.ClearValue.Color, &clear_colors[i], sizeof(Color));
    }

    if(render_pass_data.has_depth_stencil) {

        render_pass_data.depth_stencil_desc.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = clear_depth;
        render_pass_data.depth_stencil_desc.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = clear_stencil;

        command_list->BeginRenderPass(
            render_pass_data.render_target_count, 
            render_pass_data.render_target_descs.data(), 
            &render_pass_data.depth_stencil_desc, 
            D3D12_RENDER_PASS_FLAG_NONE
        );
    } else {
        command_list->BeginRenderPass(
            render_pass_data.render_target_count, 
            render_pass_data.render_target_descs.data(), 
            nullptr, 
            D3D12_RENDER_PASS_FLAG_NONE
        );
    }
}

void Encoder::Impl::end_render_pass() {

    if(!is_reset) {
        
        reset();
    }

    command_list->EndRenderPass();
}

void Encoder::Impl::bind_pipeline(Handle<Pipeline> const& pipeline) {

    if(!is_reset) {
        
        reset();
    }

    auto& pipeline_data = backend->pipelines[pipeline.id];

    binded_pipeline = &pipeline_data;

    command_list->SetPipelineState(pipeline_data.pipeline_state.Get());
    command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Encoder::Impl::bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset) {

    if(!is_reset) {
        
        reset();
    }

    auto& buffer_data = backend->buffers[buffer.id];

    auto vertex_view = D3D12_VERTEX_BUFFER_VIEW {};
    vertex_view.BufferLocation = buffer_data.resource->GetGPUVirtualAddress() + offset;
    vertex_view.SizeInBytes = static_cast<uint32_t>(buffer_data.resource->GetDesc().Width);
    vertex_view.StrideInBytes = binded_pipeline->vertex_strides[index];

    command_list->IASetVertexBuffers(index, 1, &vertex_view);
}

void Encoder::Impl::bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset) {

    if(!is_reset) {
        
        reset();
    }

    auto& buffer_data = backend->buffers[buffer.id];

    auto index_view = D3D12_INDEX_BUFFER_VIEW {};
    index_view.BufferLocation = buffer_data.resource->GetGPUVirtualAddress() + offset;
    index_view.SizeInBytes = static_cast<uint32_t>(buffer_data.resource->GetDesc().Width);
    index_view.Format = DXGI_FORMAT_R32_UINT;

    command_list->IASetIndexBuffer(&index_view);
}

void Encoder::Impl::copy_texture_region() {

    if(!is_reset) {
        
        reset();
    }    

    /*auto& texture = _impl->textures.get(dst);
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

    _impl->current_buffer->command_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);*/
}

void Encoder::Impl::copy_buffer_region(
    Handle<Buffer> const& dest, 
    uint64_t const dest_offset, 
    Handle<Buffer> const& source, 
    uint64_t const source_offset,
    size_t const size
) {

    if(!is_reset) {
        
        reset();
    }

    auto& dest_buffer = backend->buffers[dest.id];
    auto& source_buffer = backend->buffers[source.id];

    command_list->CopyBufferRegion(
        dest_buffer.resource.Get(), 
        dest_offset,
        source_buffer.resource.Get(),
        source_offset,
        size
    );
}

void Encoder::Impl::draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset) {

    if(!is_reset) {
        
        reset();
    }

    command_list->DrawInstanced(vertex_count, instance_count, vertex_offset, 0);
}

void Encoder::Impl::draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset) {

    if(!is_reset) {
        
        reset();
    }

    command_list->DrawIndexedInstanced(index_count, instance_count, 0, 0, instance_offset);
}

//===========================================================
//
//
//          Encoder Pointer to Implementation
//
//
//===========================================================

Encoder::Encoder(Backend& backend, EncoderFlags const flags) : 
    _impl(std::unique_ptr<Impl, impl_deleter>(new Impl())) {

    _impl->initialize(backend, flags);
}

Encoder::~Encoder() = default;

Encoder& Encoder::bind_descriptor_set(Handle<DescriptorSet> const& descriptor_set) {

    _impl->bind_descriptor_set(descriptor_set);
    return *this;
}

Encoder& Encoder::set_viewport(uint32_t const x, uint32_t const y, uint32_t const width, uint32_t const height) {

    _impl->set_viewport(x, y, width, height);
    return *this;
}

Encoder& Encoder::set_scissor(uint32_t const left, uint32_t const top, uint32_t const right, uint32_t const bottom) {

    _impl->set_scissor(left, top, right, bottom);
    return *this;
}

Encoder& Encoder::barrier(std::variant<Handle<Texture>, Handle<Buffer>> const& target, MemoryState const before, MemoryState const after) {

    _impl->barrier(target, before, after);
    return *this;
}

Encoder& Encoder::begin_render_pass(
    Handle<RenderPass> const& render_pass, 
    std::span<Color> const clear_colors, 
    float const clear_depth, 
    uint8_t const clear_stencil
) {

    _impl->begin_render_pass(render_pass, clear_colors, clear_depth, clear_stencil);
    return *this;
}

Encoder& Encoder::end_render_pass() {

    _impl->end_render_pass();
    return *this;
}

Encoder& Encoder::bind_pipeline(Handle<Pipeline> const& pipeline) {

    _impl->bind_pipeline(pipeline);
    return *this;
}

Encoder& Encoder::copy_buffer_region(
    Handle<Buffer> const& dest, 
    uint64_t const dest_offset, 
    Handle<Buffer> const& source, 
    uint64_t const source_offset,
    size_t const size
) {

    _impl->copy_buffer_region(dest, dest_offset, source, source_offset, size);
    return *this;
}

Encoder& Encoder::copy_texture_region() {

    return *this;
}

Encoder& Encoder::bind_vertex_buffer(uint32_t const index, Handle<Buffer> const& buffer, uint64_t const offset) {

    _impl->bind_vertex_buffer(index, buffer, offset);
    return *this;
}

Encoder& Encoder::bind_index_buffer(Handle<Buffer> const& buffer, uint64_t const offset) {

    _impl->bind_index_buffer(buffer, offset);
    return *this;
}

Encoder& Encoder::draw(uint32_t const vertex_count, uint32_t const instance_count, uint32_t const vertex_offset) {

    _impl->draw(vertex_count, instance_count, vertex_offset);
    return *this;    
}

Encoder& Encoder::draw_indexed(uint32_t const index_count, uint32_t const instance_count, uint32_t const instance_offset) {

    _impl->draw_indexed(index_count, instance_count, instance_offset);
    return *this;
}