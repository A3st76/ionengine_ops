// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "core/serializable.hpp"

namespace ionengine::tools::editor
{
    std::array<uint8_t, 8> constexpr ShaderGraphFileType{'S', 'G', 'F', 'I', 'L', 'E', '1', '0'};

    struct NodeData
    {
        uint64_t nodeID;
        int32_t posX;
        int32_t posY;
        uint32_t componentID;
        std::unordered_map<std::string, std::string> options;
    };

    struct ConnectionData
    {
        uint64_t connectionID;
        uint64_t source;
        uint32_t out;
        uint64_t dest;
        uint32_t in;
    };

    struct ShaderGraphData
    {
        std::vector<NodeData> nodes;
        std::vector<ConnectionData> connections;
    };
} // namespace ionengine::tools::editor

namespace ionengine::core
{
    template <>
    class Serializable<tools::editor::ShaderGraphData>
    {
      public:
        static auto loadFromMemory(std::span<uint8_t const> const data)
            -> std::optional<tools::editor::ShaderGraphData>;

        static auto loadFromFile(std::filesystem::path const& filePath)
            -> std::optional<tools::editor::ShaderGraphData>;

        static auto saveToFile(tools::editor::ShaderGraphData const& object,
                               std::filesystem::path const& filePath) -> bool;

        static auto saveToMemory(tools::editor::ShaderGraphData const& object,
                                 std::basic_stringstream<uint8_t>& stream) -> bool;
    };
} // namespace ionengine::core