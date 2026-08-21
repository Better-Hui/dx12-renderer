//Modify Begin:2026-08-21 by Hui
#pragma once

#include <DX12Library/MipGenerator.h>
#include <DX12Library/ResourceUploader.h>
#include <DX12Library/TextureUsageType.h>

#include <filesystem>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

class CommandList;
class D3D12DeviceContext;
class Texture;

struct TextureMemorySource
{
    std::span<const uint8_t> Data;
    std::wstring CacheKey;
    std::string FormatHint;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool IsBgra8 = false;
};

class TextureLoader final
{
public:
    explicit TextureLoader(std::shared_ptr<D3D12DeviceContext> deviceContext);

    bool Load(
        CommandList& commandList,
        Texture& texture,
        const std::filesystem::path& path,
        TextureUsageType usage = TextureUsageType::Albedo,
        bool throwOnNotFound = true);
    bool Load(
        CommandList& commandList,
        Texture& texture,
        const TextureMemorySource& source,
        TextureUsageType usage = TextureUsageType::Albedo);

private:
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
    ResourceUploader m_Uploader;
    MipGenerator m_MipGenerator;
};
//Modify End
