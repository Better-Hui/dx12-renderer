#pragma once

//Modify Begin:2026-08-18 by Hui
#include <DX12Library/GpuTimestampProfiler.h>

#include <cstdint>
#include <vector>

namespace DemoProfiling
{
    class ProfilerDisplayController final
    {
    public:
        struct CudaTimingSample
        {
            double D3DToCudaWaitMilliseconds = 0.0;
            double KernelsMilliseconds = 0.0;
            double CudaSignalMilliseconds = 0.0;
            double TotalCudaStreamMilliseconds = 0.0;
            uint64_t FrameIndex = 0;
            bool Valid = false;
        };

        explicit ProfilerDisplayController(double refreshIntervalSeconds = 1.0);

        void SetRefreshIntervalSeconds(double refreshIntervalSeconds);
        double GetRefreshIntervalSeconds() const { return m_RefreshIntervalSeconds; }
        void Reset();

        void AccumulateDirectQueueSamples(const std::vector<GpuTimestampSample>& samples);
        void AccumulateAsyncComputeQueueSamples(const std::vector<GpuTimestampSample>& samples);
        void AccumulateCopyQueueSamples(const std::vector<GpuTimestampSample>& samples);
        void AccumulateRenderGraphCpuMilliseconds(double milliseconds);
        void AccumulateCudaTiming(const CudaTimingSample& sample);
        bool Update(double totalTimeSeconds);

        const std::vector<GpuTimestampSample>& GetDirectQueueSamples() const { return m_DirectQueueDisplaySamples; }
        const std::vector<GpuTimestampSample>& GetAsyncComputeQueueSamples() const { return m_AsyncComputeQueueDisplaySamples; }
        const std::vector<GpuTimestampSample>& GetCopyQueueSamples() const { return m_CopyQueueDisplaySamples; }
        double GetRenderGraphCpuMilliseconds() const { return m_RenderGraphCpuDisplayMilliseconds; }
        const CudaTimingSample& GetCudaTiming() const { return m_CudaTimingDisplay; }

    private:
        class TimestampSampleAverager final
        {
        public:
            void Clear();
            void Accumulate(const std::vector<GpuTimestampSample>& samples);
            std::vector<GpuTimestampSample> ConsumeAverage();
            bool HasSamples() const { return m_FrameCount > 0; }

        private:
            bool IsCompatible(const std::vector<GpuTimestampSample>& samples) const;

            std::vector<GpuTimestampSample> m_AccumulatedSamples;
            uint64_t m_FrameCount = 0;
        };

        static void PublishQueueAverage(
            TimestampSampleAverager& averager,
            std::vector<GpuTimestampSample>& displaySamples);
        void ResetCudaAccumulator();

        double m_RefreshIntervalSeconds = 1.0;
        double m_WindowStartTimeSeconds = -1.0;

        TimestampSampleAverager m_DirectQueueAverager;
        TimestampSampleAverager m_AsyncComputeQueueAverager;
        TimestampSampleAverager m_CopyQueueAverager;
        std::vector<GpuTimestampSample> m_DirectQueueDisplaySamples;
        std::vector<GpuTimestampSample> m_AsyncComputeQueueDisplaySamples;
        std::vector<GpuTimestampSample> m_CopyQueueDisplaySamples;

        double m_RenderGraphCpuAccumulatedMilliseconds = 0.0;
        double m_RenderGraphCpuDisplayMilliseconds = 0.0;
        uint64_t m_RenderGraphCpuFrameCount = 0;

        CudaTimingSample m_CudaTimingAccumulated;
        CudaTimingSample m_CudaTimingDisplay;
        uint64_t m_CudaTimingFrameCount = 0;
        uint64_t m_LastCudaTimingFrameIndex = 0;
    };
}
//Modify End
