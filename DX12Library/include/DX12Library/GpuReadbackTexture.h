//Modify Begin:2026-08-28 by Hui
#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class CommandList;
class CommandQueue;
class Texture;

class GpuReadbackTexture final
{
public:
    static constexpr uint32_t DefaultSlotCount = 3u;

    void Initialize(
        Microsoft::WRL::ComPtr<ID3D12Device2> device,
        const Texture& source,
        uint32_t slotCount = DefaultSlotCount);
    void Reset();

    bool BeginCopy();
    bool RecordCopy(CommandList& commandList, const Texture& source);
    void EndCopy(uint64_t submittedFenceValue);
    void CancelCopy();

    bool CollectLatestCompleted(CommandQueue& commandQueue, std::span<std::byte> destination);

    bool IsInitialized() const { return !m_Slots.empty(); }
    uint64_t GetSizeInBytes() const { return m_SizeInBytes; }
    uint32_t GetWidth() const { return m_Footprint.Footprint.Width; }
    uint32_t GetHeight() const { return m_NumRows; }
    uint32_t GetRowPitch() const { return m_Footprint.Footprint.RowPitch; }
    uint64_t GetRowSizeInBytes() const { return m_RowSizeInBytes; }
    DXGI_FORMAT GetFormat() const { return m_Footprint.Footprint.Format; }

private:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> Buffer;
        uint64_t SubmittedFenceValue = 0u;
        bool Pending = false;
    };

    static constexpr uint32_t InvalidSlotIndex = UINT32_MAX;

    std::vector<Slot> m_Slots;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_Footprint = {};
    uint32_t m_NumRows = 0u;
    uint64_t m_RowSizeInBytes = 0u;
    uint64_t m_SizeInBytes = 0u;
    uint32_t m_ActiveSlotIndex = InvalidSlotIndex;
    bool m_ActiveCopyRecorded = false;
};
//Modify End
