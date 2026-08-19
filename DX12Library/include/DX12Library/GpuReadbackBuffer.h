//Modify Begin:2026-08-19 by Hui
#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class CommandList;
class CommandQueue;
class Resource;

class GpuReadbackBuffer final
{
public:
    static constexpr uint32_t DefaultSlotCount = 4u;

    void Initialize(
        Microsoft::WRL::ComPtr<ID3D12Device2> device,
        uint64_t sizeInBytes,
        uint32_t slotCount = DefaultSlotCount);
    void Reset();

    bool BeginCopy();
    bool RecordCopy(CommandList& commandList, const Resource& source, uint64_t sourceOffset = 0u);
    void EndCopy(uint64_t submittedFenceValue);
    void CancelCopy();

    bool CollectLatestCompleted(CommandQueue& commandQueue, std::span<std::byte> destination);

    bool IsInitialized() const { return !m_Slots.empty(); }
    uint64_t GetSizeInBytes() const { return m_SizeInBytes; }

private:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> Buffer;
        uint64_t SubmittedFenceValue = 0u;
        bool Pending = false;
    };

    static constexpr uint32_t InvalidSlotIndex = UINT32_MAX;

    std::vector<Slot> m_Slots;
    uint64_t m_SizeInBytes = 0u;
    uint32_t m_ActiveSlotIndex = InvalidSlotIndex;
    bool m_ActiveCopyRecorded = false;
};
//Modify End
