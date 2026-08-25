//Modify Begin:2026-08-25 by Hui
#include "DX12LibPCH.h"

#include "GpuReadbackTexture.h"

#include "CommandList.h"
#include "CommandQueue.h"
#include "Helpers.h"
#include "Texture.h"

#include <d3dx12/d3dx12.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <ranges>

void GpuReadbackTexture::Initialize(
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    const Texture& source,
    const uint32_t slotCount)
{
    Reset();
    Assert(device != nullptr, "GPU readback texture requires a D3D12 device.");
    Assert(source.IsValid(), "GPU readback texture source is invalid.");
    Assert(slotCount != 0u, "GPU readback texture requires at least one slot.");

    const D3D12_RESOURCE_DESC sourceDesc = source.GetD3D12ResourceDesc();
    Assert(sourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        "GPU readback texture only supports 2D textures.");
    Assert(sourceDesc.DepthOrArraySize == 1u && sourceDesc.MipLevels == 1u,
        "GPU readback texture only supports one 2D subresource.");
    Assert(sourceDesc.SampleDesc.Count == 1u,
        "GPU readback texture does not support multisampled textures.");
    Assert(sourceDesc.Format != DXGI_FORMAT_UNKNOWN,
        "GPU readback texture source format is invalid.");

    D3D12_RESOURCE_DESC copyableDesc = sourceDesc;
    UINT64 totalBytes = 0u;
    device->GetCopyableFootprints(
        &copyableDesc,
        0u,
        1u,
        0u,
        &m_Footprint,
        &m_NumRows,
        &m_RowSizeInBytes,
        &totalBytes);
    Assert(totalBytes != 0u && m_NumRows != 0u && m_Footprint.Footprint.RowPitch != 0u,
        "GPU readback texture footprint is invalid.");

    const CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
    const CD3DX12_RESOURCE_DESC bufferDescription = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
    m_Slots.resize(slotCount);
    for (uint32_t index = 0u; index < slotCount; ++index)
    {
        Slot& slot = m_Slots[index];
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&slot.Buffer)));
        const std::wstring name = L"GPU Readback Texture Slot " + std::to_wstring(index);
        ThrowIfFailed(slot.Buffer->SetName(name.c_str()));
    }
    m_SizeInBytes = totalBytes;
}

void GpuReadbackTexture::Reset()
{
    m_Slots.clear();
    m_Footprint = {};
    m_NumRows = 0u;
    m_RowSizeInBytes = 0u;
    m_SizeInBytes = 0u;
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

bool GpuReadbackTexture::BeginCopy()
{
    Assert(IsInitialized(), "GPU readback texture is not initialized.");
    Assert(m_ActiveSlotIndex == InvalidSlotIndex, "GPU readback texture copy is already active.");

    const auto availableSlot = std::ranges::find_if(
        m_Slots,
        [](const Slot& slot) { return !slot.Pending; });
    if (availableSlot == m_Slots.end())
    {
        return false;
    }

    m_ActiveSlotIndex = static_cast<uint32_t>(std::distance(m_Slots.begin(), availableSlot));
    m_ActiveCopyRecorded = false;
    return true;
}

bool GpuReadbackTexture::RecordCopy(CommandList& commandList, const Texture& source)
{
    if (m_ActiveSlotIndex == InvalidSlotIndex)
    {
        return false;
    }

    Assert(!m_ActiveCopyRecorded, "GPU readback texture copy was recorded more than once.");
    Assert(source.IsValid(), "GPU readback texture source is invalid.");
    const D3D12_RESOURCE_DESC sourceDesc = source.GetD3D12ResourceDesc();
    Assert(
        sourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        sourceDesc.Width == m_Footprint.Footprint.Width &&
        sourceDesc.Height == m_NumRows &&
        sourceDesc.Format == m_Footprint.Footprint.Format &&
        sourceDesc.SampleDesc.Count == 1u,
        "GPU readback texture source no longer matches its initialized footprint.");

    const Microsoft::WRL::ComPtr<ID3D12Resource> sourceResource = source.GetD3D12Resource();
    const Microsoft::WRL::ComPtr<ID3D12Resource> destinationResource = m_Slots[m_ActiveSlotIndex].Buffer;
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = m_Footprint;
    commandList.ExecuteExternalCommandRecording(
        [sourceResource, destinationResource, footprint](ID3D12GraphicsCommandList2& nativeCommandList)
        {
            D3D12_TEXTURE_COPY_LOCATION destination = {};
            destination.pResource = destinationResource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION source = {};
            source.pResource = sourceResource.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0u;
            nativeCommandList.CopyTextureRegion(&destination, 0u, 0u, 0u, &source, nullptr);
        });
    m_ActiveCopyRecorded = true;
    return true;
}

void GpuReadbackTexture::EndCopy(const uint64_t submittedFenceValue)
{
    if (m_ActiveSlotIndex == InvalidSlotIndex)
    {
        return;
    }

    Slot& slot = m_Slots[m_ActiveSlotIndex];
    if (m_ActiveCopyRecorded)
    {
        Assert(submittedFenceValue != 0u, "GPU readback texture copy requires a submitted queue fence.");
        slot.SubmittedFenceValue = submittedFenceValue;
        slot.Pending = true;
    }
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

void GpuReadbackTexture::CancelCopy()
{
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

bool GpuReadbackTexture::CollectLatestCompleted(
    CommandQueue& commandQueue,
    const std::span<std::byte> destination)
{
    Assert(destination.size_bytes() >= m_SizeInBytes, "GPU readback texture destination is too small.");

    Slot* latestCompletedSlot = nullptr;
    for (Slot& slot : m_Slots)
    {
        if (!slot.Pending || !commandQueue.IsFenceComplete(slot.SubmittedFenceValue))
        {
            continue;
        }

        if (latestCompletedSlot == nullptr || slot.SubmittedFenceValue > latestCompletedSlot->SubmittedFenceValue)
        {
            latestCompletedSlot = &slot;
        }
    }

    if (latestCompletedSlot == nullptr)
    {
        return false;
    }

    void* mappedData = nullptr;
    const D3D12_RANGE readRange = { 0u, m_SizeInBytes };
    ThrowIfFailed(latestCompletedSlot->Buffer->Map(0u, &readRange, &mappedData));
    std::memcpy(destination.data(), mappedData, static_cast<size_t>(m_SizeInBytes));
    const D3D12_RANGE writeRange = { 0u, 0u };
    latestCompletedSlot->Buffer->Unmap(0u, &writeRange);

    for (Slot& slot : m_Slots)
    {
        if (slot.Pending && commandQueue.IsFenceComplete(slot.SubmittedFenceValue))
        {
            slot.SubmittedFenceValue = 0u;
            slot.Pending = false;
        }
    }
    return true;
}
//Modify End
