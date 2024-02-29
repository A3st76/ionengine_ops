
#include "model.hpp"
#include "precompiled.h"

namespace ionengine
{
    Model::Model(LinkedDevice& device) : device(&device)
    {
    }

    auto Model::load_from_memory(std::span<uint8_t const> const data, ModelImporter& importer) -> bool
    {
        auto result = importer.load(data);

        if (!result)
        {
            return false;
        }

        device->begin_upload();

        for (auto const& mesh : importer.get().meshes)
        {
            std::vector<Primitive> primitives;

            for (auto const& primitive : mesh.primitives)
            {
                auto vertex_buffer = device->get_device().create_buffer(
                    primitive.vertices.size(), 0, rhi::BufferUsageFlags(rhi::BufferUsage::Vertex));
                auto index_buffer = device->get_device().create_buffer(primitive.indices.size(), 0,
                                                                       rhi::BufferUsageFlags(rhi::BufferUsage::Index));

                device->upload(vertex_buffer, primitive.vertices);
                device->upload(index_buffer, primitive.indices);

                primitives.emplace_back(Primitive{
                    .vertices = vertex_buffer, .indices = index_buffer, .index_count = primitive.index_count});
            }

            meshes.emplace_back(Mesh{.material = nullptr, .primitives = primitives});
        }

        device->end_upload();
        return true;
    }

    auto Model::get_mesh_count() const -> uint32_t
    {
        return static_cast<uint32_t>(meshes.size());
    }

    auto Model::get_mesh(uint32_t const index) -> Mesh&
    {
        return meshes[index];
    }
} // namespace ionengine