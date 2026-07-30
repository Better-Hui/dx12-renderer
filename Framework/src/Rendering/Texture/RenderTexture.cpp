#include <Framework/Rendering/Texture/RenderTexture.h>

#include <DX12Library/Texture.h>
#include <DX12Library/TextureUsageType.h>

#include <d3dx12.h>

//Modify Begin:2026-07-23 by BestHui
std::shared_ptr<Texture> RenderTexture::Create2D(
    DXGI_FORMAT format,
    uint32_t width,
    uint32_t height,
    const std::wstring& name,
    D3D12_RESOURCE_FLAGS flags,
    uint16_t arraySize,
    uint16_t mipLevels)
{
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        width,
        height,
        arraySize,
        mipLevels,
        1,
        0,
        flags);

    return std::make_shared<Texture>(desc, nullptr, TextureUsageType::Other, name);
}

std::shared_ptr<Texture> RenderTexture::CreateUav2D(
    DXGI_FORMAT format,
    uint32_t width,
    uint32_t height,
    const std::wstring& name,
    uint16_t arraySize,
    uint16_t mipLevels)
{
    return Create2D(
        format,
        width,
        height,
        name,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        arraySize,
        mipLevels);
}
//Modify End
