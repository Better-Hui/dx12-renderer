#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <chrono>
#include <cstdint>
//Modify Begin:2026-07-30 by Hui
#include <memory>
//Modify End
#include <string>
#include <vector>

class CommandList;
class CommandQueue;

//Modify Begin:2026-07-29 by Hui
struct GpuTimestampSample
{
    std::string Name;
    double MillisecondsFromFrameStart = 0.0;
    double MillisecondsFromPrevious = 0.0;
//Modify Begin:2026-08-02 by Hui
    double CpuMillisecondsFromFrameStart = 0.0;
    double CpuMillisecondsFromPrevious = 0.0;
//Modify End
};

class GpuTimestampProfiler final
{
public:
//Modify Begin:2026-08-03 by Hui
    static constexpr uint32_t FrameSlotCount = 64;
//Modify End

    bool Initialize(
        Microsoft::WRL::ComPtr<ID3D12Device2> device,
        std::shared_ptr<CommandQueue> commandQueue,
        uint32_t maxTimestampCount = 128,
        D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT);
    void Shutdown();

    bool BeginFrame(uint64_t frameNumber);
    void WriteTimestamp(CommandList& commandList, const char* name);
    void ResolveFrame(CommandList& commandList);
    void EndFrame(uint64_t submittedFenceValue);

    bool CollectCompletedFrame(CommandQueue& commandQueue, std::vector<GpuTimestampSample>& samples);

    bool IsAvailable() const { return m_Initialized; }
    uint32_t GetCurrentTimestampCount() const;
    double GetLastFrameGpuMilliseconds() const { return m_LastFrameGpuMilliseconds; }
//Modify Begin:2026-08-03 by Hui
    uint64_t GetLastCollectedFrameNumber() const { return m_LastCollectedFrameNumber; }
//Modify End

private:
    struct FrameSlot
    {
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> QueryHeap;
        Microsoft::WRL::ComPtr<ID3D12Resource> ReadbackBuffer;
        std::vector<std::string> Names;
//Modify Begin:2026-08-02 by Hui
        std::vector<double> CpuMilliseconds;
//Modify End
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
//Modify Begin:2026-08-03 by Hui
    D3D12_COMMAND_LIST_TYPE m_CommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
//Modify End
//Modify Begin:2026-08-02 by Hui
    std::chrono::steady_clock::time_point m_CpuFrameStart = {};
//Modify End
    bool m_Initialized = false;
//Modify Begin:2026-08-03 by Hui
    bool m_FrameActive = false;
//Modify End
    double m_LastFrameGpuMilliseconds = 0.0;
//Modify Begin:2026-08-03 by Hui
    uint64_t m_LastCollectedFrameNumber = 0;
//Modify End
};
//Modify End
