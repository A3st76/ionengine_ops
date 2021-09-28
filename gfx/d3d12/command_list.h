// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

namespace ionengine::gfx {

class D3DCommandList : public CommandList {
public:

    D3DCommandList(ID3D12Device4* d3d12_device, const D3D12_COMMAND_LIST_TYPE command_list_type) {
        
        THROW_IF_FAILED(d3d12_device->CreateCommandAllocator(
                command_list_type, 
                __uuidof(ID3D12CommandAllocator), m_d3d12_command_allocator.put_void()
            )
        );

        THROW_IF_FAILED(d3d12_device->CreateCommandList(
                0, 
                command_list_type, 
                m_d3d12_command_allocator.get(), 
                nullptr, 
                __uuidof(ID3D12GraphicsCommandList), m_d3d12_command_list.put_void()
            )
        );
        THROW_IF_FAILED(m_d3d12_command_list->Close());
    }

    /*void bind_pipeline(Pipeline& pipeline) override {

        m_binded_pipeline = static_cast<D3DPipeline&>(pipeline);

        if(m_pipeline.value().get().get_type() == PipelineType::Graphics) {

            m_d3d12_command_list->SetGraphicsRootSignature(m_pipeline.value().get().get_d3d12_root_signature().get());
            m_d3d12_command_list->SetPipelineState(m_pipeline.value().get().get_d3d12_pipeline_state().get());
            m_d3d12_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        } else {

            
        }
    }*/
    
    void set_viewport(const int32 x, const int32 y, const uint32 width, const uint32 height) override {

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = static_cast<float>(x);
        viewport.TopLeftY = static_cast<float>(y);
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        m_d3d12_command_list->RSSetViewports(1, &viewport);
    }

    void set_scissor_rect(int32 left, int32 top, int32 right, int32 bottom) override {

        D3D12_RECT rect{};
        rect.top = static_cast<int32>(top);
        rect.bottom = static_cast<int32>(bottom);
        rect.left = static_cast<int32>(left);
        rect.right = static_cast<int32>(right);

        m_d3d12_command_list->RSSetScissorRects(1, &rect);
    }

    /*void resource_barriers(const std::vector<ResourceBarrierDesc>& barrier_descs) override {
        
        std::vector<D3D12_RESOURCE_BARRIER> d3d12_barriers;
        for(auto& barrier_desc : barrier_descs) {

            D3D12_RESOURCE_BARRIER resource_barrier{};
            resource_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            resource_barrier.Transition.pResource = static_cast<D3DResource&>(barrier_desc.resource.get()).get_d3d12_resource().get();
            resource_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            resource_barrier.Transition.StateAfter = convert_resource_state(barrier_desc.state_after);
            resource_barrier.Transition.StateBefore = convert_resource_state(barrier_desc.state_before);

            d3d12_barriers.emplace_back(resource_barrier);
        }

        if(!d3d12_barriers.empty()) {
            m_d3d12_command_list->ResourceBarrier(static_cast<uint32>(d3d12_barriers.size()), d3d12_barriers.data());
        }
    }*/

    /*void begin_render_pass(RenderPass& render_pass, FrameBuffer& frame_buffer, const ClearValueDesc& clear_value_desc) override {

        auto& render_pass_desc = static_cast<D3DRenderPass&>(render_pass).get_desc();
        auto& colors = static_cast<D3DFrameBuffer&>(frame_buffer).get_desc().colors;
        auto& depth_stencil = static_cast<D3DFrameBuffer&>(frame_buffer).get_desc().depth_stencil;

        std::vector<D3D12_RENDER_PASS_RENDER_TARGET_DESC> render_pass_rt_descs;
        for(uint32 i = 0; i < colors.size(); ++i) {

            D3D12_RENDER_PASS_BEGINNING_ACCESS begin{};
            begin.Type = convert_render_pass_type(render_pass_desc.colors[i].load_op);

            begin.Clear.ClearValue.Format = static_cast<DXGI_FORMAT>(render_pass_desc.colors[i].format);

            begin.Clear.ClearValue.Color[0] = clear_value_desc.colors[i].r;
            begin.Clear.ClearValue.Color[1] = clear_value_desc.colors[i].g;
            begin.Clear.ClearValue.Color[2] = clear_value_desc.colors[i].b;
            begin.Clear.ClearValue.Color[3] = clear_value_desc.colors[i].a;

            D3D12_RENDER_PASS_ENDING_ACCESS end{};
            end.Type = convert_render_pass_type(render_pass_desc.colors[i].store_op);

            D3D12_RENDER_PASS_RENDER_TARGET_DESC render_pass_rt_desc;
            render_pass_rt_desc.cpuDescriptor = static_cast<D3DView&>(colors[i].get()).get_d3d12_cpu_descriptor();
            render_pass_rt_desc.BeginningAccess = begin;
            render_pass_rt_desc.EndingAccess = end;

            render_pass_rt_descs.emplace_back(render_pass_rt_desc);      
        }

        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC render_pass_ds_desc;
        if(depth_stencil.has_value()) {

            D3D12_RENDER_PASS_BEGINNING_ACCESS depth_begin{};
            depth_begin.Type = convert_render_pass_type(render_pass_desc.depth_stencil.depth_load_op);
            depth_begin.Clear.ClearValue.Format = static_cast<DXGI_FORMAT>(render_pass_desc.depth_stencil.format);
            depth_begin.Clear.ClearValue.DepthStencil.Depth = clear_value_desc.depth;

            D3D12_RENDER_PASS_ENDING_ACCESS depth_end{};
            depth_end.Type = convert_render_pass_type(render_pass_desc.depth_stencil.depth_store_op);

            D3D12_RENDER_PASS_BEGINNING_ACCESS stencil_begin{};
            stencil_begin.Type = convert_render_pass_type(render_pass_desc.depth_stencil.stencil_load_op);
            stencil_begin.Clear.ClearValue.Format = static_cast<DXGI_FORMAT>(render_pass_desc.depth_stencil.format);
            stencil_begin.Clear.ClearValue.DepthStencil.Stencil = clear_value_desc.stencil;

            D3D12_RENDER_PASS_ENDING_ACCESS stencil_end{};
            stencil_end.Type = convert_render_pass_type(render_pass_desc.depth_stencil.stencil_store_op);

            render_pass_ds_desc.cpuDescriptor = static_cast<D3DView&>(depth_stencil.value().get()).get_d3d12_cpu_descriptor();
            render_pass_ds_desc.DepthBeginningAccess = depth_begin;
            render_pass_ds_desc.DepthEndingAccess = depth_end;
            render_pass_ds_desc.StencilBeginningAccess = stencil_begin;
            render_pass_ds_desc.StencilEndingAccess = stencil_end;

            m_d3d12_command_list->BeginRenderPass(static_cast<uint32>(render_pass_rt_descs.size()), render_pass_rt_descs.data(), &render_pass_ds_desc, D3D12_RENDER_PASS_FLAG_NONE);
        } else {
            m_d3d12_command_list->BeginRenderPass(static_cast<uint32>(render_pass_rt_descs.size()), render_pass_rt_descs.data(), nullptr, D3D12_RENDER_PASS_FLAG_NONE);
        }
    }

    void end_render_pass() override {
        m_d3d12_command_list->EndRenderPass();
    }*/

    void draw_indexed(const uint32 index_count, const uint32 instance_count, const uint32 first_index, const int32 vertex_offset, const uint32 first_instance) override {
        m_d3d12_command_list->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
    }

    void close() override {
        m_d3d12_command_list->Close();
    }

    void reset() override {
        m_d3d12_command_list->Reset(m_d3d12_command_allocator.get(), nullptr);
    }

    /*void set_index_buffer(Buffer* buffer, const Format format) override {
        D3D12_INDEX_BUFFER_VIEW index_view{};
        index_view.Format = static_cast<DXGI_FORMAT>(format);
        index_view.BufferLocation = static_cast<D3DResource&>(resource).get_d3d12_resource()->GetGPUVirtualAddress();
        index_view.SizeInBytes = static_cast<uint32>(std::get<D3D12_RESOURCE_DESC>(static_cast<D3DResource&>(resource).get_d3d12_desc()).Width);
        m_d3d12_command_list->IASetIndexBuffer(&index_view);
    }

    void set_vertex_buffer(const uint32 slot, Buffer* buffer, const uint32 stride) override {
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        vertex_view.StrideInBytes = stride;
        vertex_view.BufferLocation = static_cast<D3DResource&>(resource).get_d3d12_resource()->GetGPUVirtualAddress();
        vertex_view.SizeInBytes = static_cast<uint32>(std::get<D3D12_RESOURCE_DESC>(static_cast<D3DResource&>(resource).get_d3d12_desc()).Width);
        m_d3d12_command_list->IASetVertexBuffers(slot, 1, &vertex_view);
    }*/

    winrt::com_ptr<ID3D12CommandAllocator>& get_d3d12_command_allocator() { return m_d3d12_command_allocator; }
    winrt::com_ptr<ID3D12GraphicsCommandList4>& get_d3d12_command_list() { return m_d3d12_command_list; }

private:

    winrt::com_ptr<ID3D12CommandAllocator> m_d3d12_command_allocator;
    winrt::com_ptr<ID3D12GraphicsCommandList4> m_d3d12_command_list;
};

}