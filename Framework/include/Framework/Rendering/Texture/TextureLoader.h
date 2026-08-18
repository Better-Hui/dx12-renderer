#pragma once

#include <DX12Library/MipGenerator.h>
#include <DX12Library/ResourceUploader.h>
#include <DX12Library/TextureUsageType.h>

#include <filesystem>
#include <memory>

class CommandList;
class D3D12DeviceContext;
class Texture;

//Modify Begin:2026-08-18 by Hui
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

private:
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
    ResourceUploader m_Uploader;
    MipGenerator m_MipGenerator;
};
//Modify End
