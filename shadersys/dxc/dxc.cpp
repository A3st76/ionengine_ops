// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#include "dxc.hpp"
#include "precompiled.h"
#include "shadersys/lexer.hpp"
#include "shadersys/parser.hpp"

namespace ionengine::shadersys
{
    auto throwIfFailed(HRESULT const hr) -> void
    {
        if (FAILED(hr))
        {
            throw core::runtime_error(std::format("The program closed with an error {:04x}", hr));
        }
    }

    DXCCompiler::DXCCompiler(fx::ShaderAPIType const apiType)
    {
        throwIfFailed(::DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), compiler.put_void()));
        throwIfFailed(::DxcCreateInstance(CLSID_DxcUtils, __uuidof(IDxcUtils), utils.put_void()));
        throwIfFailed(utils->CreateDefaultIncludeHandler(includeHandler.put()));
    }

    auto DXCCompiler::compileFromBytes(std::span<uint8_t const> const dataBytes) -> std::optional<fx::ShaderEffectFile>
    {
        return this->compileBufferData(dataBytes);
    }

    auto DXCCompiler::compileFromFile(std::filesystem::path const& filePath) -> std::optional<fx::ShaderEffectFile>
    {
        std::basic_ifstream<uint8_t> stream(filePath);
        if (!stream.is_open())
        {
            return std::nullopt;
        }

        std::basic_string<uint8_t> buffer = {std::istreambuf_iterator<uint8_t>(stream.rdbuf()), {}};
        return this->compileBufferData(buffer);
    }

    auto DXCCompiler::compileBufferData(std::span<uint8_t const> const buffer) -> std::optional<fx::ShaderEffectFile>
    {
        Lexer lexer(std::string_view(reinterpret_cast<char const*>(buffer.data()), buffer.size()));
        Parser parser;

        fx::ShaderHeaderData headerData;
        fx::ShaderOutputData outputData;
        std::unordered_map<shadersys::fx::ShaderStageType, std::string> stageData;

        parser.parse(lexer, headerData, outputData, stageData);

        for (auto const& [stageType, shaderCode] : stageData)
        {
            DxcBuffer dxcBuffer{.Ptr = shaderCode.data(), .Size = shaderCode.size(), .Encoding = DXC_CP_UTF8};

            winrt::com_ptr<IDxcResult> result;
            throwIfFailed(compiler->Compile(&dxcBuffer, nullptr, 0, includeHandler.get(), __uuidof(IDxcResult),
                                            result.put_void()));

            winrt::com_ptr<IDxcBlobUtf8> errorsBlob;
            throwIfFailed(result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), errorsBlob.put_void(), nullptr));

            if (errorsBlob && errorsBlob->GetStringLength() > 0)
            {
                std::cerr << "\n" + std::string(reinterpret_cast<char*>(errorsBlob->GetBufferPointer()),
                                                errorsBlob->GetBufferSize())
                          << std::endl;
                return std::nullopt;
            }
        }

        return std::nullopt;
    }
} // namespace ionengine::shadersys