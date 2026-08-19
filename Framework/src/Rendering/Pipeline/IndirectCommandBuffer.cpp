//Modify Begin:2026-08-19 by Hui
#include <Framework/Rendering/Pipeline/IndirectCommandBuffer.h>

#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/ResourceUploader.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <d3dx12/d3dx12.h>

#include <memory>

IndirectCommandBuffer::IndirectCommandBuffer(
    FrameworkDeviceContext& deviceContext,
    const uint64_t capacityInBytes,
    std::wstring name)
    : m_CapacityInBytes(Math::AlignUp(capacityInBytes, 4u))
{
    Assert(m_CapacityInBytes > 0u, "Indirect command buffer capacity must be positive.");

    const D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(
        m_CapacityInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_Buffer = std::make_unique<ByteAddressBuffer>(
        description,
        1u,
        static_cast<size_t>(m_CapacityInBytes),
        name,
        deviceContext.GetD3D12DeviceContext());
    m_Buffer->SetAutoBarriersEnabled(false);
}

void IndirectCommandBuffer::Upload(
    CommandList& commandList,
    const std::span<const std::byte> commandData)
{
    Assert(!commandData.empty(), "Indirect command data must not be empty.");
    Assert(commandData.size_bytes() <= m_CapacityInBytes, "Indirect command data exceeds buffer capacity.");

    ResourceUploader(commandList.GetDeviceContext()).UploadByteAddressBuffer(
        commandList,
        *m_Buffer,
        commandData.size_bytes(),
        commandData.data());
}

Resource& IndirectCommandBuffer::GetResource()
{
    return *m_Buffer;
}

const Resource& IndirectCommandBuffer::GetResource() const
{
    return *m_Buffer;
}
//Modify End
