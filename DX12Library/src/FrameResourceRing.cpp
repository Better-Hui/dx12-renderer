#include "DX12LibPCH.h"

#include "FrameResourceRing.h"

#include "CommandQueue.h"

//Modify Begin:2026-07-28 by Hui
void FrameResourceRing::Reset(const uint32_t slotCount)
{
//Modify Begin:2026-07-29 by Hui
    for (Slot& slot : m_Slots)
    {
        for (auto& retireAction : slot.RetireActions)
        {
            if (retireAction)
            {
                retireAction();
            }
        }
    }
//Modify End
    m_Slots.assign(slotCount, Slot{});
    m_CurrentIndex = 0;
}

void FrameResourceRing::SetCurrentIndex(const uint32_t slotIndex)
{
    Assert(slotIndex < m_Slots.size(), "Frame resource slot index out of range.");
    m_CurrentIndex = slotIndex;
}

uint32_t FrameResourceRing::GetCurrentIndex() const
{
    return m_CurrentIndex;
}

const FrameResourceRing::Slot& FrameResourceRing::GetSlot(const uint32_t slotIndex) const
{
    Assert(slotIndex < m_Slots.size(), "Frame resource slot index out of range.");
    return m_Slots[slotIndex];
}

void FrameResourceRing::MarkSubmitted(const uint32_t slotIndex, const uint64_t fenceValue, const uint64_t frameNumber)
{
    Assert(slotIndex < m_Slots.size(), "Frame resource slot index out of range.");
    m_Slots[slotIndex].FenceValue = fenceValue;
    m_Slots[slotIndex].FrameNumber = frameNumber;
}

//Modify Begin:2026-07-29 by Hui
void FrameResourceRing::RetireCurrentFrameResource(std::function<void()>&& retireAction)
{
    Assert(m_CurrentIndex < m_Slots.size(), "Frame resource slot index out of range.");
    if (retireAction)
    {
        m_Slots[m_CurrentIndex].RetireActions.emplace_back(std::move(retireAction));
    }
}

uint64_t FrameResourceRing::WaitForSlot(CommandQueue& commandQueue, const uint32_t slotIndex)
//Modify End
{
    Assert(slotIndex < m_Slots.size(), "Frame resource slot index out of range.");
    Slot& slot = m_Slots[slotIndex];
    if (slot.FenceValue != 0)
    {
        commandQueue.WaitForFenceValue(slot.FenceValue);
    }
//Modify Begin:2026-07-29 by Hui
    for (auto& retireAction : slot.RetireActions)
    {
        if (retireAction)
        {
            retireAction();
        }
    }
    slot.RetireActions.clear();
//Modify End
    return slot.FrameNumber;
}
//Modify End
