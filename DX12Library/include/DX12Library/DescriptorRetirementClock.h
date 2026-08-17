//Modify Begin:2026-07-30 by Hui
#pragma once

#include <atomic>
#include <cstdint>

class DescriptorRetirementClock final
{
public:
    void SetCurrentFrame(const uint64_t frameNumber)
    {
        m_CurrentFrame.store(frameNumber, std::memory_order_relaxed);
    }

    uint64_t GetCurrentFrame() const
    {
        return m_CurrentFrame.load(std::memory_order_relaxed);
    }

private:
    std::atomic_uint64_t m_CurrentFrame = 0;
};
//Modify End
