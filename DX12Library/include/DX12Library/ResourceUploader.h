#pragma once

#include <d3d12.h>
#include <d3dx12/d3dx12.h>
#include <wrl.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Buffer;
class ByteAddressBuffer;
class CommandList;
class D3D12DeviceContext;
class IndexBuffer;
class StructuredBuffer;
class Texture;
class VertexBuffer;
enum class TextureUsageType;

//Modify Begin:2026-08-18 by Hui
class ResourceUploader final
{
public:
    explicit ResourceUploader(std::shared_ptr<D3D12DeviceContext> deviceContext);

    void UploadVertexBuffer(
        CommandList& commandList,
        VertexBuffer& vertexBuffer,
        size_t vertexCount,
        size_t vertexStride,
        const void* vertexData) const;
    void CopyVertexBuffer(
        CommandList& commandList,
        VertexBuffer& vertexBuffer,
        size_t vertexCount,
        size_t vertexStride,
        const void* vertexData) const;

    void CopyStructuredBuffer(
        CommandList& commandList,
        StructuredBuffer& structuredBuffer,
        size_t elementCount,
        size_t elementSize,
        const void* data) const;

    template <typename T>
    void UploadVertexBuffer(
        CommandList& commandList,
        VertexBuffer& vertexBuffer,
        const std::vector<T>& vertices) const
    {
        UploadVertexBuffer(commandList, vertexBuffer, vertices.size(), sizeof(T), vertices.data());
    }

    void UploadIndexBuffer(
        CommandList& commandList,
        IndexBuffer& indexBuffer,
        size_t indexCount,
        DXGI_FORMAT indexFormat,
        const void* indexData) const;

    template <typename T>
    void UploadIndexBuffer(
        CommandList& commandList,
        IndexBuffer& indexBuffer,
        const std::vector<T>& indices) const
    {
        static_assert(sizeof(T) == 2 || sizeof(T) == 4, "Index type must be 16-bit or 32-bit.");
        const DXGI_FORMAT format = sizeof(T) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        UploadIndexBuffer(commandList, indexBuffer, indices.size(), format, indices.data());
    }

    void UploadByteAddressBuffer(
        CommandList& commandList,
        ByteAddressBuffer& byteAddressBuffer,
        size_t sizeInBytes,
        const void* data) const;

    template <typename T>
    void UploadByteAddressBuffer(
        CommandList& commandList,
        ByteAddressBuffer& byteAddressBuffer,
        const T& data) const
    {
        UploadByteAddressBuffer(commandList, byteAddressBuffer, sizeof(T), &data);
    }

    void UploadStructuredBuffer(
        CommandList& commandList,
        StructuredBuffer& structuredBuffer,
        size_t elementCount,
        size_t elementSize,
        const void* data) const;

    template <typename T>
    void UploadStructuredBuffer(
        CommandList& commandList,
        StructuredBuffer& structuredBuffer,
        const std::vector<T>& data) const
    {
        UploadStructuredBuffer(commandList, structuredBuffer, data.size(), sizeof(T), data.data());
    }

    template <typename T>
    void CopyStructuredBuffer(
        CommandList& commandList,
        StructuredBuffer& structuredBuffer,
        const std::vector<T>& data) const
    {
        CopyStructuredBuffer(commandList, structuredBuffer, data.size(), sizeof(T), data.data());
    }

    void UploadTextureSubresources(
        CommandList& commandList,
        Texture& texture,
        uint32_t firstSubresource,
        uint32_t subresourceCount,
        D3D12_SUBRESOURCE_DATA* subresources) const;

    void CopyTextureSubresources(
        CommandList& commandList,
        Texture& texture,
        uint32_t firstSubresource,
        uint32_t subresourceCount,
        const D3D12_SUBRESOURCE_DATA* subresources) const;

    void AssignTextureResource(
        CommandList& commandList,
        Texture& texture,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        TextureUsageType usage,
        const std::wstring& name) const;

private:
    void UploadBuffer(
        CommandList& commandList,
        Buffer& buffer,
        size_t elementCount,
        size_t elementSize,
        const void* data,
        D3D12_RESOURCE_FLAGS flags) const;
    void ValidateCommandList(const CommandList& commandList) const;

    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
};
//Modify End
