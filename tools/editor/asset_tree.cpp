// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#include "asset_tree.hpp"
#include "precompiled.h"

namespace ionengine::tools::editor
{
    AssetTree::AssetTree(std::filesystem::path const& rootPath) : rootPath(rootPath)
    {
        rootStruct = std::make_unique<AssetStructInfo>();
        rootStruct->name = rootPath.filename().generic_string();
        rootStruct->type = AssetType::Folder;
        rootStruct->path = rootPath;
    }

    auto AssetTree::fetch() -> AssetStructInfo const&
    {
        rootStruct->childrens.clear();

        auto internalFetch = [&](this auto const& internalFetch, AssetStructInfo* curStruct,
                                 std::filesystem::path const& rootPath) -> void {
            for (auto const& dirEntry : std::filesystem::directory_iterator(rootPath))
            {
                if (std::filesystem::is_directory(dirEntry.path()))
                {
                    auto folderStruct = std::make_unique<AssetStructInfo>();
                    folderStruct->name = dirEntry.path().filename().generic_string();
                    folderStruct->path = dirEntry.path();
                    folderStruct->type = AssetType::Folder;

                    curStruct->childrens.emplace_back(std::move(folderStruct));
                    internalFetch(curStruct->childrens.back().get(), dirEntry.path());
                }
                else
                {
                    auto assetStruct = std::make_unique<AssetStructInfo>();
                    assetStruct->name = dirEntry.path().stem().generic_string();
                    assetStruct->path = dirEntry.path();

                    if (dirEntry.path().extension().compare(".asset") == 0)
                    {
                        assetStruct->type = AssetType::Asset;
                    }
                    else
                    {
                        assetStruct->type = AssetType::Unknown;
                    }
                    curStruct->childrens.emplace_back(std::move(assetStruct));
                }
            }
        };

        internalFetch(rootStruct.get(), rootPath);
        return *rootStruct;
    }

    auto AssetTree::createFile(std::filesystem::path const& filePath, std::basic_ofstream<uint8_t>& stream) -> bool
    {
        stream.open(filePath, std::ios::binary);
        return stream.is_open();
    }

    auto AssetTree::deleteFile(std::filesystem::path const& filePath) -> bool
    {
        return std::filesystem::remove(filePath);
    }

    auto AssetTree::renameFile(std::filesystem::path const& oldFilePath,
                               std::filesystem::path const& newFilePath) -> bool
    {
        std::error_code error;
        std::filesystem::rename(oldFilePath, newFilePath, error);
        return !error;
    }
} // namespace ionengine::tools::editor