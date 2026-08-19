//Modify Begin:2026-08-19 by Hui
#include "DX12LibPCH.h"

#include "GpuReadbackBuffer.h"

#include "CommandList.h"
#include "CommandQueue.h"
#include "Helpers.h"
#include "Resource.h"

#include <d3dx12/d3dx12.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <ranges>

void GpuReadbackBuffer::Initialize(
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    const uint64_t sizeInBytes,
    const uint32_t slotCount)
{
    Reset();
    Assert(device != nullptr, "GPU readback buffer requires a D3D12 device.");
    Assert(sizeInBytes != 0u, "GPU readback buffer size must be non-zero.");
    Assert(slotCount != 0u, "GPU readback buffer requires at least one slot.");

    const CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_READBACK);
    const CD3DX12_RESOURCE_DESC resourceDescription = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    m_Slots.resize(slotCount);
    for (Slot& slot : m_Slots)
    {
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&slot.Buffer)));
    }
    m_SizeInBytes = sizeInBytes;
}

void GpuReadbackBuffer::Reset()
{
    m_Slots.clear();
    m_SizeInBytes = 0u;
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

bool GpuReadbackBuffer::BeginCopy()
{
    Assert(IsInitialized(), "GPU readback buffer is not initialized.");
    Assert(m_ActiveSlotIndex == InvalidSlotIndex, "GPU readback buffer copy is already active.");

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

bool GpuReadbackBuffer::RecordCopy(
    CommandList& commandList,
    const Resource& source,
    const uint64_t sourceOffset)
{
    if (m_ActiveSlotIndex == InvalidSlotIndex)
    {
        return false;
    }

    Assert(!m_ActiveCopyRecorded, "GPU readback buffer copy was recorded more than once.");
    Assert(sourceOffset + m_SizeInBytes <= source.GetD3D12ResourceDesc().Width, "GPU readback source range is out of bounds.");
    commandList.CopyBufferToReadback(
        source,
        sourceOffset,
        m_Slots[m_ActiveSlotIndex].Buffer,
        0u,
        m_SizeInBytes);
    m_ActiveCopyRecorded = true;
    return true;
}

void GpuReadbackBuffer::EndCopy(const uint64_t submittedFenceValue)
{
    if (m_ActiveSlotIndex == InvalidSlotIndex)
    {
        return;
    }

    Slot& slot = m_Slots[m_ActiveSlotIndex];
    if (m_ActiveCopyRecorded)
    {
        Assert(submittedFenceValue != 0u, "GPU readback copy requires a submitted direct-queue fence.");
        slot.SubmittedFenceValue = submittedFenceValue;
        slot.Pending = true;
    }
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

void GpuReadbackBuffer::CancelCopy()
{
    m_ActiveSlotIndex = InvalidSlotIndex;
    m_ActiveCopyRecorded = false;
}

bool GpuReadbackBuffer::CollectLatestCompleted(
    CommandQueue& commandQueue,
    const std::span<std::byte> destination)
{
    Assert(destination.size_bytes() >= m_SizeInBytes, "GPU readback destination is too small.");

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
    std::memcpy(destination.data(), mappedData, m_SizeInBytes);
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
