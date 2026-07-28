#pragma once

#include <cstdint>
#include <vector>

class CommandQueue;

//Modify Begin:2026-07-28 by BestHui
class FrameResourceRing
{
public:
    struct Slot
    {
        uint64_t FenceValue = 0;
        uint64_t FrameNumber = 0;
    };

    void Reset(uint32_t slotCount);
    void SetCurrentIndex(uint32_t slotIndex);

    uint32_t GetCurrentIndex() const;
    const Slot& GetSlot(uint32_t slotIndex) const;

    void MarkSubmitted(uint32_t slotIndex, uint64_t fenceValue, uint64_t frameNumber);
    uint64_t WaitForSlot(CommandQueue& commandQueue, uint32_t slotIndex) const;

private:
    std::vector<Slot> m_Slots;
    uint32_t m_CurrentIndex = 0;
};
//Modify End
