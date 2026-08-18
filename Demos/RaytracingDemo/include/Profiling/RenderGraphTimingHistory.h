#pragma once

//Modify Begin:2026-08-07 by Hui
#include <DX12Library/GpuTimestampProfiler.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace DemoProfiling
{
    class GpuTimestampSampleAverager final
    {
    public:
        void Clear();
        void Accumulate(const std::vector<GpuTimestampSample>& samples);
        std::vector<GpuTimestampSample> ConsumeAverage();

        bool HasSamples() const { return m_FrameCount > 0; }
        uint64_t GetFrameCount() const { return m_FrameCount; }

    private:
        bool IsCompatible(const std::vector<GpuTimestampSample>& samples) const;

        std::vector<GpuTimestampSample> m_AccumulatedSamples;
        uint64_t m_FrameCount = 0;
    };

    class RenderGraphTimingHistory final
    {
    public:
        void Clear();
        void SetCapacity(size_t capacity);
        void Record(uint64_t frameNumber, const char* queueName, const std::vector<GpuTimestampSample>& samples);
        bool DumpCsv();

        size_t GetFrameCount() const { return m_Frames.size(); }
        size_t GetCapacity() const { return m_Capacity; }
        const std::string& GetStatus() const { return m_Status; }

    private:
        struct QueueSamples
        {
            std::string Name;
            std::vector<GpuTimestampSample> Samples;
        };

        struct FrameSamples
        {
            uint64_t Number = 0;
            std::vector<QueueSamples> Queues;
        };

        void TrimToCapacity();

        std::deque<FrameSamples> m_Frames;
        size_t m_Capacity = 300;
        std::string m_Status;
    };
}
//Modify End
