#include "DX12LibPCH.h"

#include "MipGenerator.h"

#include "CommandList.h"
#include "CommandListInternalAccess.h"
#include "D3D12DeviceContext.h"
#include "GenerateMipsPso.h"
#include "Texture.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

//Modify Begin:2026-08-18 by Hui
MipGenerator::MipGenerator(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_DeviceContext(std::move(deviceContext))
{
    Assert(m_DeviceContext != nullptr, "Mip generator requires a D3D12 device context.");
}

MipGenerator::~MipGenerator() = default;

void MipGenerator::Generate(CommandList& commandList, Texture& texture)
{
    Assert(
        commandList.GetDeviceContext().get() == m_DeviceContext.get(),
        "Mip generator and command list belong to different D3D12 device contexts.");
    if (commandList.GetCommandListType() == D3D12_COMMAND_LIST_TYPE_COPY)
    {
        throw std::invalid_argument("Mip generation requires a direct or compute command list.");
    }

    const Microsoft::WRL::ComPtr<ID3D12Resource> resource = texture.GetD3D12Resource();
    Assert(resource != nullptr, "Cannot generate mipmaps for an uninitialized texture.");
    const D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();
    if (resourceDesc.MipLevels == 1u)
    {
        return;
    }
    if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        resourceDesc.DepthOrArraySize != 1u || resourceDesc.SampleDesc.Count > 1u)
    {
        throw std::invalid_argument("Mip generation supports only non-multisampled 2D textures.");
    }

    if (texture.CheckUavSupport() &&
        (resourceDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0u)
    {
        GenerateUnorderedAccessMips(commandList, texture, resourceDesc.Format);
        return;
    }

    D3D12_RESOURCE_DESC aliasDesc = resourceDesc;
    aliasDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    aliasDesc.Flags &= ~(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_RESOURCE_DESC uavDesc = aliasDesc;
    uavDesc.Format = Texture::GetUavCompatibleFormat(resourceDesc.Format);
    const D3D12_RESOURCE_DESC allocationDescs[] = { aliasDesc, uavDesc };
    const auto device = m_DeviceContext->GetDevice();
    const D3D12_RESOURCE_ALLOCATION_INFO allocationInfo =
        device->GetResourceAllocationInfo(0u, _countof(allocationDescs), allocationDescs);

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = allocationInfo.SizeInBytes;
    heapDesc.Alignment = allocationInfo.Alignment;
    heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    Microsoft::WRL::ComPtr<ID3D12Heap> heap;
    ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)));
    CommandListInternalAccess::TrackObjectLifetime(commandList, heap);

    Microsoft::WRL::ComPtr<ID3D12Resource> aliasResource;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0u,
        &aliasDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&aliasResource)));
    Texture aliasTexture(aliasResource, texture.GetTextureUsage(), L"Mip Alias", m_DeviceContext);
    CommandListInternalAccess::TrackResourceLifetime(commandList, aliasTexture);

    Microsoft::WRL::ComPtr<ID3D12Resource> unorderedAccessResource;
    ThrowIfFailed(device->CreatePlacedResource(
        heap.Get(),
        0u,
        &uavDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&unorderedAccessResource)));
    Texture unorderedAccessTexture(
        unorderedAccessResource,
        texture.GetTextureUsage(),
        L"Mip UAV Alias",
        m_DeviceContext);
    CommandListInternalAccess::TrackResourceLifetime(commandList, unorderedAccessTexture);

    commandList.AliasingBarrier(nullptr, aliasResource);
    commandList.CopyResource(aliasTexture, texture);
    commandList.AliasingBarrier(aliasResource, unorderedAccessResource);
    GenerateUnorderedAccessMips(commandList, unorderedAccessTexture, resourceDesc.Format);
    commandList.AliasingBarrier(unorderedAccessResource, aliasResource);
    commandList.CopyResource(texture, aliasTexture);
}

void MipGenerator::GenerateUnorderedAccessMips(
    CommandList& commandList,
    Texture& texture,
    const DXGI_FORMAT format)
{
    if (m_Pipeline == nullptr)
    {
        m_Pipeline = std::make_unique<GenerateMipsPso>(m_DeviceContext->GetDevice());
    }

    commandList.SetPipelineState(m_Pipeline->GetPipelineState());
    commandList.SetComputeRootSignature(m_Pipeline->GetRootSignature());

    GenerateMipsCb constants = {};
    constants.IsSRgb = Texture::IsSRgbFormat(format);
    const D3D12_RESOURCE_DESC resourceDesc = texture.GetD3D12ResourceDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;

    for (uint32_t sourceMip = 0u; sourceMip < resourceDesc.MipLevels - 1u;)
    {
        const uint64_t sourceWidth = resourceDesc.Width >> sourceMip;
        const uint32_t sourceHeight = resourceDesc.Height >> sourceMip;
        uint32_t destinationWidth = static_cast<uint32_t>(sourceWidth >> 1u);
        uint32_t destinationHeight = sourceHeight >> 1u;
        constants.SrcDimension = ((sourceHeight & 1u) << 1u) | (sourceWidth & 1u);

        DWORD mipCount = 0u;
        _BitScanForward(
            &mipCount,
            (destinationWidth == 1u ? destinationHeight : destinationWidth) |
            (destinationHeight == 1u ? destinationWidth : destinationHeight));
        mipCount = std::min<DWORD>(GenerateMipsPso::MAX_MIP_LEVELS_AT_ONCE, mipCount + 1u);
        mipCount = sourceMip + mipCount >= resourceDesc.MipLevels
            ? resourceDesc.MipLevels - sourceMip - 1u
            : mipCount;

        destinationWidth = std::max(1u, destinationWidth);
        destinationHeight = std::max(1u, destinationHeight);
        constants.SrcMipLevel = sourceMip;
        constants.NumMipLevels = mipCount;
        constants.TexelSize.x = 1.0f / static_cast<float>(destinationWidth);
        constants.TexelSize.y = 1.0f / static_cast<float>(destinationHeight);
        commandList.SetCompute32BitConstants(GenerateMips::GenerateMipsCb, constants);
        commandList.SetShaderResourceView(
            GenerateMips::SrcMip,
            0u,
            texture,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            sourceMip,
            1u,
            &srvDesc);

        for (uint32_t mip = 0u; mip < mipCount; ++mip)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};
            viewDesc.Format = resourceDesc.Format;
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = sourceMip + mip + 1u;
            commandList.SetUnorderedAccessView(
                GenerateMips::OutMip,
                mip,
                texture,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                viewDesc.Texture2D.MipSlice,
                1u,
                &viewDesc);
        }

        if (mipCount < GenerateMipsPso::MAX_MIP_LEVELS_AT_ONCE)
        {
            commandList.StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                GenerateMips::OutMip,
                mipCount,
                GenerateMipsPso::MAX_MIP_LEVELS_AT_ONCE - mipCount,
                m_Pipeline->GetDefaultUav());
        }

        commandList.Dispatch(
            Math::DivideByMultiple(destinationWidth, 8u),
            Math::DivideByMultiple(destinationHeight, 8u));
        commandList.UavBarrier(texture);
        sourceMip += mipCount;
    }
}
//Modify End
