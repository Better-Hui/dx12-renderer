#include "DX12LibPCH.h"

#include "ResourceUploader.h"

#include "Buffer.h"
#include "ByteAddressBuffer.h"
#include "CommandList.h"
#include "CommandListInternalAccess.h"
#include "D3D12DeviceContext.h"
#include "Helpers.h"
#include "IndexBuffer.h"
#include "StructuredBuffer.h"
#include "Texture.h"
#include "VertexBuffer.h"

#include <cstring>
#include <stdexcept>

//Modify Begin:2026-08-18 by Hui
ResourceUploader::ResourceUploader(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_DeviceContext(std::move(deviceContext))
{
    Assert(m_DeviceContext != nullptr, "Resource uploader requires a D3D12 device context.");
}

void ResourceUploader::UploadVertexBuffer(
    CommandList& commandList,
    VertexBuffer& vertexBuffer,
    const size_t vertexCount,
    const size_t vertexStride,
    const void* vertexData) const
{
    UploadBuffer(commandList, vertexBuffer, vertexCount, vertexStride, vertexData, D3D12_RESOURCE_FLAG_NONE);
}

void ResourceUploader::UploadIndexBuffer(
    CommandList& commandList,
    IndexBuffer& indexBuffer,
    const size_t indexCount,
    const DXGI_FORMAT indexFormat,
    const void* indexData) const
{
    const size_t indexSize = indexFormat == DXGI_FORMAT_R16_UINT ? 2u : 4u;
    UploadBuffer(commandList, indexBuffer, indexCount, indexSize, indexData, D3D12_RESOURCE_FLAG_NONE);
}

void ResourceUploader::UploadByteAddressBuffer(
    CommandList& commandList,
    ByteAddressBuffer& byteAddressBuffer,
    const size_t sizeInBytes,
    const void* data) const
{
    const size_t alignedSize = Math::AlignUp(sizeInBytes, 4u);
    if (alignedSize == sizeInBytes)
    {
        UploadBuffer(
            commandList,
            byteAddressBuffer,
            1u,
            sizeInBytes,
            data,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        return;
    }

    std::vector<uint8_t> paddedData(alignedSize, 0u);
    std::memcpy(paddedData.data(), data, sizeInBytes);
    UploadBuffer(
        commandList,
        byteAddressBuffer,
        1u,
        alignedSize,
        paddedData.data(),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

void ResourceUploader::UploadStructuredBuffer(
    CommandList& commandList,
    StructuredBuffer& structuredBuffer,
    const size_t elementCount,
    const size_t elementSize,
    const void* data) const
{
    UploadBuffer(
        commandList,
        structuredBuffer,
        elementCount,
        elementSize,
        data,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

void ResourceUploader::UploadTextureSubresources(
    CommandList& commandList,
    Texture& texture,
    const uint32_t firstSubresource,
    const uint32_t subresourceCount,
    D3D12_SUBRESOURCE_DATA* subresources) const
{
    ValidateCommandList(commandList);
    texture.AttachDeviceContext(m_DeviceContext);
    Assert(subresourceCount > 0u, "Texture upload requires at least one subresource.");
    Assert(subresources != nullptr, "Texture upload data is null.");

    const Microsoft::WRL::ComPtr<ID3D12Resource> destination = texture.GetD3D12Resource();
    Assert(destination != nullptr, "Texture upload destination has not been created.");

    if (texture.AreAutoBarriersEnabled())
    {
        commandList.TransitionBarrier(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, true);
    }

    const UINT64 requiredSize = GetRequiredIntermediateSize(destination.Get(), firstSubresource, subresourceCount);
    const UploadBuffer::Allocation uploadAllocation = CommandListInternalAccess::AllocateTransientUpload(
        commandList,
        static_cast<size_t>(requiredSize),
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    UpdateSubresources(
        commandList.GetGraphicsCommandList().Get(),
        destination.Get(),
        uploadAllocation.Resource.Get(),
        uploadAllocation.Offset,
        firstSubresource,
        subresourceCount,
        subresources);
    CommandListInternalAccess::TrackObjectLifetime(commandList, uploadAllocation.Resource);
    CommandListInternalAccess::TrackResourceLifetime(commandList, texture);
}

void ResourceUploader::AssignTextureResource(
    CommandList& commandList,
    Texture& texture,
    Microsoft::WRL::ComPtr<ID3D12Resource> resource,
    const TextureUsageType usage,
    const std::wstring& name) const
{
    ValidateCommandList(commandList);
    Assert(resource != nullptr, "Assigned texture resource is null.");
    texture.AttachDeviceContext(m_DeviceContext);
    if (texture.GetD3D12Resource().Get() != resource.Get())
    {
        CommandListInternalAccess::RetireResource(commandList, texture);
        texture.SetD3D12Resource(std::move(resource));
    }
    texture.SetTextureUsage(usage);
    texture.CreateViews();
    texture.SetName(name);
}

void ResourceUploader::UploadBuffer(
    CommandList& commandList,
    Buffer& buffer,
    const size_t elementCount,
    const size_t elementSize,
    const void* data,
    const D3D12_RESOURCE_FLAGS flags) const
{
    ValidateCommandList(commandList);
    buffer.AttachDeviceContext(m_DeviceContext);

    const size_t bufferSize = elementCount * elementSize;
    if (bufferSize == 0u)
    {
        throw std::invalid_argument("GPU buffer upload size must be non-zero.");
    }

    const D3D12_RESOURCE_DESC currentDesc = buffer.GetD3D12ResourceDesc();
    const bool recreate = buffer.GetD3D12Resource() == nullptr ||
        currentDesc.Width < bufferSize || currentDesc.Flags != flags;
    if (recreate)
    {
        Assert(buffer.AreAutoBarriersEnabled(), "Cannot recreate a resource with automatic barriers disabled.");
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags);
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        ThrowIfFailed(m_DeviceContext->GetDevice()->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&resource)));
        resource->SetName(buffer.GetName().c_str());

        CommandListInternalAccess::RetireResource(commandList, buffer);
        buffer.SetD3D12Resource(std::move(resource));
    }

    if (data != nullptr)
    {
        const UploadBuffer::Allocation uploadAllocation = CommandListInternalAccess::AllocateTransientUpload(
            commandList,
            bufferSize,
            16u);
        std::memcpy(uploadAllocation.Cpu, data, bufferSize);
        commandList.CopyBufferRegion(
            buffer,
            0u,
            uploadAllocation.Resource,
            uploadAllocation.Offset,
            bufferSize);
    }
    else
    {
        CommandListInternalAccess::TrackResourceLifetime(commandList, buffer);
    }

    buffer.CreateViews(elementCount, elementSize);
}

void ResourceUploader::ValidateCommandList(const CommandList& commandList) const
{
    Assert(
        commandList.GetDeviceContext().get() == m_DeviceContext.get(),
        "Resource uploader and command list belong to different D3D12 device contexts.");
}
//Modify End
