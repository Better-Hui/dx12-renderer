#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <vector>

class CommandList;
class CommandQueue;

//Modify Begin:2026-07-29 by BestHui
struct GpuTimestampSample
{
    std::string Name;
    double MillisecondsFromFrameStart = 0.0;
    double MillisecondsFromPrevious = 0.0;
};

class GpuTimestampProfiler final
{
public:
    static constexpr uint32_t FrameSlotCount = 3;

    bool Initialize(uint32_t maxTimestampCount = 128);
    void Shutdown();

    void BeginFrame(uint64_t frameNumber);
    void WriteTimestamp(CommandList& commandList, const char* name);
    void ResolveFrame(CommandList& commandList);
    void EndFrame(uint64_t submittedFenceValue);

    bool CollectCompletedFrame(CommandQueue& commandQueue, std::vector<GpuTimestampSample>& samples);

    bool IsAvailable() const { return m_Initialized; }
    uint32_t GetCurrentTimestampCount() const;
    double GetLastFrameGpuMilliseconds() const { return m_LastFrameGpuMilliseconds; }

private:
    struct FrameSlot
    {
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> QueryHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource> ReadbackBuffer;
        std::vector<std::string> Names;
        uint32_t TimestampCount = 0;
        uint64_t FrameNumber = 0;
        uint64_t SubmittedFenceValue = 0;
        bool PendingReadback = false;
    };

    FrameSlot& GetCurrentSlot();
    const FrameSlot& GetCurrentSlot() const;

    std::vector<FrameSlot> m_FrameSlots;
    uint32_t m_MaxTimestampCount = 0;
    uint32_t m_CurrentSlotIndex = 0;
    uint64_t m_CurrentFrameNumber = 0;
    uint64_t m_TimestampFrequency = 0;
    bool m_Initialized = false;
    double m_LastFrameGpuMilliseconds = 0.0;
};
//Modify End
