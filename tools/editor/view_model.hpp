// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "engine/engine.hpp"
#include "graph/registry.hpp"
#include "graph/scene.hpp"
#include <webview.hpp>
#include "asset_tree.hpp"
#include "shader_graph/shader_graph.hpp"

namespace ionengine::tools::editor
{
    class ViewModel : public Engine
    {
      public:
        ViewModel(libwebview::App* app);

        auto requestViewportTexture() -> std::string;

        auto getShaderGraphComponents() -> std::string;

        auto shaderGraphAssetSave(std::string fileData, std::string sceneData) -> std::string;

        auto shaderGraphAssetCompile(std::string fileData) -> std::string;

        auto compileShader(std::string sceneData) -> std::string;

        auto getAssetTree() -> std::string;

        auto assetBrowserCreateFile(std::string fileData) -> std::string;

        auto assetBrowserDeleteFile(std::string fileData) -> std::string;

        auto assetBrowserRenameFile(std::string fileData) -> std::string;

        auto assetBrowserOpenFile(std::string fileData) -> std::string;

      protected:
        auto init() -> void override;

        auto update(float const dt) -> void override;

        auto render() -> void override;

      private:
        libwebview::App* app;

        AssetTree assetTree;
        ShaderGraphEditor shaderGraphEditor;

        core::ref_ptr<TextureAsset> viewportTexture;
        core::ref_ptr<ShaderAsset> outputShader;
        std::vector<RenderableData> renderableObjects;
    };
} // namespace ionengine::tools::editor