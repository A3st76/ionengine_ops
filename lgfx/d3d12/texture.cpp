// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#include "../precompiled.h"
#include "texture.h"
#include "conversion.h"
#include "device.h"

using namespace lgfx;

Texture::Texture() {

}

Texture::Texture(Texture&& rhs) noexcept {

    resource_.Swap(rhs.resource_);
    std::swap(resource_desc_, rhs.resource_desc_);
    std::swap(initial_state_, rhs.initial_state_);
    std::swap(ptr_, rhs.ptr_);
    std::swap(pool_, rhs.pool_);
    std::swap(desc_, rhs.desc_);
}

Texture& Texture::operator=(Texture&& rhs) noexcept {

    resource_.Swap(rhs.resource_);
    std::swap(resource_desc_, rhs.resource_desc_);
    std::swap(initial_state_, rhs.initial_state_);
    std::swap(ptr_, rhs.ptr_);
    std::swap(pool_, rhs.pool_);
    std::swap(desc_, rhs.desc_);
    return *this;
}

Texture::Texture(Device* device, const uint32_t buffer_index) : desc_{} {

    THROW_IF_FAILED(device->swapchain_->GetBuffer(buffer_index, __uuidof(ID3D12Resource), reinterpret_cast<void**>(resource_.GetAddressOf())));
    resource_desc_ = resource_->GetDesc();

    desc_.width = static_cast<uint32_t>(resource_desc_.Width);
    desc_.height = resource_desc_.Height;
    desc_.format = DXGIFormatTo(resource_desc_.Format);
    desc_.flags = TextureFlags::kRenderTarget;
    desc_.array_layers = resource_desc_.DepthOrArraySize;
    desc_.mip_levels = resource_desc_.MipLevels;
}

Texture::Texture(Device* device, MemoryPool* pool, const TextureDesc& desc) :
    pool_(pool), desc_(desc), resource_desc_{} {

    resource_desc_.Dimension = ToD3D12ResourceDimension(desc.dimension);
    resource_desc_.Width = desc.width;
    resource_desc_.Height = desc.height;
    resource_desc_.MipLevels = desc.mip_levels;
    resource_desc_.DepthOrArraySize = desc.array_layers;
    resource_desc_.SampleDesc.Count = 1;
    resource_desc_.Format = ToDXGIFormat(desc.format);
    if(desc.flags & TextureFlags::kRenderTarget) {
        resource_desc_.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc_.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        initial_state_ = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if(desc.flags & TextureFlags::kDepthStencil) {
        resource_desc_.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc_.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        initial_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    } else if(desc.flags & TextureFlags::kUnorderedAccess) {
        resource_desc_.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc_.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    } else if(desc.flags & TextureFlags::kShaderResource) {
        resource_desc_.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    }

    D3D12_RESOURCE_ALLOCATION_INFO alloc_info = device->device_->GetResourceAllocationInfo(0, 1, &resource_desc_);
    ptr_ = pool->Allocate(alloc_info.SizeInBytes);

    if(!ptr_) {
        throw std::runtime_error("Texture allocation error!");
    }
    
    THROW_IF_FAILED(device->device_->CreatePlacedResource(ptr_.heap->heap.Get(), ptr_.offset, &resource_desc_, initial_state_, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(resource_.GetAddressOf())));
}