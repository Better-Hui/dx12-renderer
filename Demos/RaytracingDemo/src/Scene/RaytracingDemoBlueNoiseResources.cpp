#include <Scene/RaytracingDemoBlueNoiseResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Texture.h>
#include <Framework/Rendering/Texture/TextureLoader.h>

#include <stdexcept>

namespace
{
    constexpr wchar_t BlueNoiseScalarPath[] =
        L"Assets/Textures/BlueNoise/FastBlueNoise_scalar_128x128x64.dds";
    constexpr wchar_t BlueNoiseVec2Path[] =
        L"Assets/Textures/BlueNoise/FastBlueNoise_vec2_128x128x64.dds";
}

void RaytracingDemoBlueNoiseResources::Clear() noexcept
{
    m_ScalarTexture.reset();
    m_Vec2Texture.reset();
}

void RaytracingDemoBlueNoiseResources::Load(CommandList& commandList)
{
    if (IsLoaded())
    {
        return;
    }

    Clear();
    const std::shared_ptr<D3D12DeviceContext> deviceContext = commandList.GetDeviceContext();
    m_ScalarTexture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"UE FastBlueNoise scalar 128x128x64",
        deviceContext);
    m_Vec2Texture = std::make_shared<Texture>(
        TextureUsageType::Other,
        L"UE FastBlueNoise vec2 128x128x64",
        deviceContext);

    TextureLoader loader(deviceContext);
    if (!loader.Load(commandList, *m_ScalarTexture, BlueNoiseScalarPath, TextureUsageType::Other) ||
        !loader.Load(commandList, *m_Vec2Texture, BlueNoiseVec2Path, TextureUsageType::Other))
    {
        Clear();
        throw std::runtime_error("Failed to load the UE spatiotemporal blue-noise masks.");
    }
}

bool RaytracingDemoBlueNoiseResources::IsLoaded() const noexcept
{
    return m_ScalarTexture != nullptr && m_Vec2Texture != nullptr;
}

const std::shared_ptr<Texture>& RaytracingDemoBlueNoiseResources::GetScalarTexture() const noexcept
{
    return m_ScalarTexture;
}

const std::shared_ptr<Texture>& RaytracingDemoBlueNoiseResources::GetVec2Texture() const noexcept
{
    return m_Vec2Texture;
}
