// Copyright © 2020-2022 Dmitriy Lukovenko. All rights reserved.

#include <precompiled.h>
#include <renderer/renderer.h>
#include <scene/scene.h>
#include <scene/scene_visitor.h>
#include <scene/mesh_node.h>
#include <scene/transform_node.h>
#include <scene/camera_node.h>

using namespace ionengine;
using namespace ionengine::renderer;

class MeshVisitor : public scene::SceneVisitor {
public:

    MeshVisitor(RenderQueue& render_queue) : _render_queue(&render_queue) { }

    void operator()(scene::MeshNode& other) {

        if(other.mesh().is_ok()) {
            for(size_t i = 0; i < other.mesh()->surfaces().size(); ++i) {
                
                auto instance = SurfaceInstance {
                    .model = other.transform_global()
                };

                _render_queue->push(other.mesh(), static_cast<uint32_t>(i), instance, other.material(other.mesh()->surfaces()[i].material_index));
            }
        }
    }

    // Default
    void operator()(scene::TransformNode& other) { }
    void operator()(scene::CameraNode& other) { }

private:

    RenderQueue* _render_queue;
};

Renderer::Renderer(platform::Window& window, asset::AssetManager& asset_manager) : 
    _device(0, backend::SwapchainDesc { .window = &window, .sample_count = 1, .buffer_count = 2 }),
    _asset_manager(&asset_manager) {

    auto [mesh_sender, mesh_receiver] = lib::make_channel<asset::AssetEvent<asset::Mesh>>();
    asset_manager.mesh_pool().event_dispatcher().add(std::move(mesh_sender));
    _mesh_event_receiver.emplace(std::move(mesh_receiver));

    auto [technique_sender, technique_receiver] = lib::make_channel<asset::AssetEvent<asset::Technique>>();
    asset_manager.technique_pool().event_dispatcher().add(std::move(technique_sender));
    _technique_event_receiver.emplace(std::move(technique_receiver));

    auto [texture_sender, texture_receiver] = lib::make_channel<asset::AssetEvent<asset::Texture>>();
    asset_manager.texture_pool().event_dispatcher().add(std::move(texture_sender));
    _texture_event_receiver.emplace(std::move(texture_receiver));

    _frame_graph.emplace(_device);
    _upload_context.emplace(_device);

    _shader_cache.emplace(_device);
    _geometry_cache.emplace(_device);
    _pipeline_cache.emplace(_device);
    _texture_cache.emplace(_device);

    _width = window.client_width();
    _height = window.client_height();

    _gbuffer_albedos.resize(2);
    _depth_stencils.resize(2);

    _material_buffers.resize(2);

    for(uint32_t i = 0; i < 2; ++i) {
        _gbuffer_albedos[i] = GPUTexture::render_target(_device, backend::Format::RGBA8, _width, _height);
        _depth_stencils[i] = GPUTexture::depth_stencil(_device, backend::Format::D32, _width, _height);
        _world_cbuffer_pools.emplace_back(_device, 256, 64);
        _material_cbuffer_pools.emplace_back(_device, 512, 32);
        _material_buffers[i].resize(512);
    }
}

void Renderer::update(float const delta_time) {

    // Mesh Event Update
    {
        asset::AssetEvent<asset::Mesh> mesh_event;
        while(_mesh_event_receiver.value().try_receive(mesh_event)) {
            auto event_visitor = make_visitor(
                [&](asset::AssetEventData<asset::Mesh, asset::AssetEventType::Loaded>& event) {
                    std::cout << std::format("[Debug] Renderer created GeometryBuffers from '{}'", event.asset.path().string()) << std::endl;

                    for(size_t i = 0; i < event.asset->surfaces().size(); ++i) {
                        _geometry_cache.value().get(_upload_context.value(), *event.asset, static_cast<uint32_t>(i));
                    }
                },
                // Default
                [&](asset::AssetEventData<asset::Mesh, asset::AssetEventType::Unloaded>& event) { },
                [&](asset::AssetEventData<asset::Mesh, asset::AssetEventType::Added>& event) { },
                [&](asset::AssetEventData<asset::Mesh, asset::AssetEventType::Removed>& event) { }
            );

            std::visit(event_visitor, mesh_event.data);
        }
    }

    // Technique Event Update
    {
        asset::AssetEvent<asset::Technique> technique_event;
        while(_technique_event_receiver.value().try_receive(technique_event)) {
            auto event_visitor = make_visitor(
                [&](asset::AssetEventData<asset::Technique, asset::AssetEventType::Loaded>& event) {
                    std::cout << std::format("[Debug] Renderer created ShaderProgram from '{}'", event.asset.path().string()) << std::endl;
                    _shader_cache.value().get(*event.asset);
                },
                // Default
                [&](asset::AssetEventData<asset::Technique, asset::AssetEventType::Unloaded>& event) { },
                [&](asset::AssetEventData<asset::Technique, asset::AssetEventType::Added>& event) { },
                [&](asset::AssetEventData<asset::Technique, asset::AssetEventType::Removed>& event) { }
            );

            std::visit(event_visitor, technique_event.data);
        }
    }

    // Texture Event Update
    {
        asset::AssetEvent<asset::Texture> texture_event;
        while(_texture_event_receiver.value().try_receive(texture_event)) {
            auto event_visitor = make_visitor(
                [&](asset::AssetEventData<asset::Texture, asset::AssetEventType::Loaded>& event) {
                    std::cout << std::format("[Debug] Renderer created GPUTexture from '{}'", event.asset.path().string()) << std::endl;
                    _texture_cache.value().get(_upload_context.value(), *event.asset);
                },
                // Default
                [&](asset::AssetEventData<asset::Texture, asset::AssetEventType::Unloaded>& event) { },
                [&](asset::AssetEventData<asset::Texture, asset::AssetEventType::Added>& event) { },
                [&](asset::AssetEventData<asset::Texture, asset::AssetEventType::Removed>& event) { }
            );

            std::visit(event_visitor, texture_event.data);
        }
    }
}

void Renderer::render(scene::Scene& scene) {

    uint32_t const frame_index = _frame_graph.value().wait();
    scene::CameraNode* camera = scene.graph().find_by_name<scene::CameraNode>("MainCamera");

    _deffered_queue.clear();

    _world_cbuffer_pools.at(frame_index).reset();
    _material_cbuffer_pools.at(frame_index).reset();

    MeshVisitor mesh_visitor(_deffered_queue);
    scene.graph().visit(scene.graph().begin(), scene.graph().end(), mesh_visitor);

    camera->calculate_matrices();

    build_frame_graph(_width, _height, frame_index, camera);
    
    _frame_graph.value().execute();
}

void Renderer::resize(uint32_t const width, uint32_t const height) {

}

void Renderer::build_frame_graph(uint32_t const width, uint32_t const height, uint32_t const frame_index, scene::CameraNode* camera) {

    auto gbuffer_albedo_info = CreateColorInfo {
        .attachment = _gbuffer_albedos.at(frame_index),
        .load_op = backend::RenderPassLoadOp::Clear,
        .clear_color = lib::math::Color(0.1f, 0.5f, 0.2f, 1.0f)
    };

    auto depth_stencil_info = CreateDepthStencilInfo {
        .attachment = _depth_stencils.at(frame_index),
        .load_op = backend::RenderPassLoadOp::Clear,
        .clear_depth = 1.0f,
        .clear_stencil = 0x0
    };

    CreateColorInfo swapchain_info = {
        .attachment = nullptr,
        .load_op = backend::RenderPassLoadOp::Clear,
        .clear_color = lib::math::Color(0.5f, 0.5f, 0.5f, 1.0f)
    };

    _frame_graph.value().add_pass(
        "gbuffer", 
        width,
        height,
        std::span<CreateColorInfo const>(&swapchain_info, 1),
        std::nullopt,
        depth_stencil_info,
        [&](RenderPassContext const& context) {
            
            for(auto const& batch : _deffered_queue) {
                
                auto geometry_buffer = _geometry_cache.value().get(_upload_context.value(), *batch.mesh, batch.surface_index);
                auto pipeline = _pipeline_cache.value().get(_shader_cache.value(), *batch.material, "gbuffer", context.render_pass());

                pipeline->bind(context.command_list());

                auto shader_program = pipeline->shader_program();

                uint32_t const world_location = shader_program->location_by_uniform_name("world");

                ShaderUniformBinder binder(_device, *shader_program);
                std::vector<std::shared_ptr<GPUTexture>> samplers;

                for(auto& [parameter_name, parameter] : batch.material->parameters()) {

                    if(parameter.is_sampler2D()) {

                        auto it = shader_program->uniforms().find(parameter_name);
                        if(it == shader_program->uniforms().end()) {
                            break;
                        }

                        auto gpu_texture = _texture_cache.value().get(_upload_context.value(), *parameter.as_sampler2D().asset);
                        samplers.emplace_back(gpu_texture);

                        auto uniform_visitor = make_visitor(
                            [&](ShaderUniformData<ShaderUniformType::Sampler2D> const& data) {
                                binder.bind_texture(data.index, *gpu_texture);
                            },
                            // Default
                            [&](ShaderUniformData<ShaderUniformType::CBuffer> const& data) { }
                        );

                        std::visit(uniform_visitor, it->second.data);

                    } else {
                        
                        auto it = shader_program->uniforms().find("material");
                        if(it == shader_program->uniforms().end()) {
                            break;
                        }

                        auto uniform_visitor = make_visitor(
                            [&](ShaderUniformData<ShaderUniformType::CBuffer> const& uniform_data) {

                                auto parameter_visitor = make_visitor(
                                    [&](asset::MaterialParameterData<asset::MaterialParameterType::F32> const& parameter_data) {
                                        std::memcpy(_material_buffers.at(frame_index).data() + uniform_data.offsets.at(parameter_name), &parameter_data.value, sizeof(float));
                                    },
                                    [&](asset::MaterialParameterData<asset::MaterialParameterType::F32x2> const& parameter_data) {
                                        std::memcpy(_material_buffers.at(frame_index).data() + uniform_data.offsets.at(parameter_name), parameter_data.value.data(), sizeof(lib::math::Vector2f));
                                    },
                                    [&](asset::MaterialParameterData<asset::MaterialParameterType::F32x3> const& parameter_data) {
                                        std::memcpy(_material_buffers.at(frame_index).data() + uniform_data.offsets.at(parameter_name), parameter_data.value.data(), sizeof(lib::math::Vector3f));
                                    },
                                    [&](asset::MaterialParameterData<asset::MaterialParameterType::F32x4> const& parameter_data) {
                                        std::memcpy(_material_buffers.at(frame_index).data() + uniform_data.offsets.at(parameter_name), parameter_data.value.data(), sizeof(lib::math::Vector4f));
                                    },
                                    // Default
                                    [&](asset::MaterialParameterData<asset::MaterialParameterType::Sampler2D> const& parameter_data) { }
                                );

                                std::visit(parameter_visitor, parameter.data);
                            },
                            // Default
                            [&](ShaderUniformData<ShaderUniformType::Sampler2D> const& uniform_data) { }
                        );

                        std::visit(uniform_visitor, it->second.data);
                    }
                }

                {
                    std::shared_ptr<GPUBuffer> buffer = _material_cbuffer_pools.at(frame_index).allocate();
                    buffer->copy_data(_upload_context.value(), std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(_material_buffers.at(frame_index).data()), _material_buffers.at(frame_index).size()));

                    uint32_t const material_location = shader_program->location_by_uniform_name("material");
                    binder.bind_cbuffer(material_location, *buffer);
                }

                for(auto& sampler : samplers) {
                    sampler->barrier(context.command_list(), backend::MemoryState::ShaderRead);
                }

                for(auto const& instance : batch.instances) {

                    auto world = WorldCBuffer {
                        .world = instance.model,
                        .view = camera->transform_view(),
                        .proj = camera->transform_projection()
                    };

                    std::shared_ptr<GPUBuffer> buffer = _world_cbuffer_pools.at(frame_index).allocate();
                    buffer->copy_data(_upload_context.value(), std::span<uint8_t const>(reinterpret_cast<uint8_t const*>(&world), sizeof(WorldCBuffer)));
                    
                    binder.bind_cbuffer(world_location, *buffer);

                    binder.update(context.command_list());
                    geometry_buffer->bind(context.command_list());
                }

                for(auto& sampler : samplers) {
                    sampler->barrier(context.command_list(), backend::MemoryState::Common);
                }
            }
        }
    );

    /*CreateColorInfo swapchain_info = {
        .attachment = nullptr,
        .load_op = backend::RenderPassLoadOp::Clear,
        .clear_color = lib::math::Color(0.1f, 0.5f, 0.2f, 1.0f)
    };

    _frame_graph.value().add_pass(
        "present",
        width,
        height,
        std::span<CreateColorInfo const>(&swapchain_info, 1),
        std::nullopt,
        std::nullopt,
        RenderPassDefaultFunc
    );*/
}
