// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "core/ref_ptr.hpp"
#include "mdl.hpp"

namespace ionengine::asset
{
    class MDLImporter : public core::ref_counted_object
    {
      public:
        virtual ~MDLImporter() = default;

        virtual auto loadFromFile(std::filesystem::path const& filePath,
                                  std::string& errors) -> std::optional<ModelFile> = 0;

        virtual auto loadFromBytes(std::span<uint8_t const> const dataBytes,
                                   std::string& errors) -> std::optional<ModelFile> = 0;
    };
} // namespace ionengine::mdl