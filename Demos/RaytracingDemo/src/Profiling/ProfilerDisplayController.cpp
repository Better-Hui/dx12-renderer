//Modify Begin:2026-08-18 by Hui
#include <Profiling/ProfilerDisplayController.h>

#include <algorithm>

DemoProfiling::ProfilerDisplayController::ProfilerDisplayController(const double refreshIntervalSeconds)
{
    SetRefreshIntervalSeconds(refreshIntervalSeconds);
}

void DemoProfiling::ProfilerDisplayController::SetRefreshIntervalSeconds(const double refreshIntervalSeconds)
{
    m_RefreshIntervalSeconds = std::clamp(refreshIntervalSeconds, 0.1, 10.0);
    Reset();
}

void DemoProfiling::ProfilerDisplayController::Reset()
{
    m_WindowStartTimeSeconds = -1.0;
    m_DirectQueueAverager.Clear();
    m_AsyncComputeQueueAverager.Clear();
    m_CopyQueueAverager.Clear();
    m_DirectQueueDisplaySamples.clear();
    m_AsyncComputeQueueDisplaySamples.clear();
    m_CopyQueueDisplaySamples.clear();
    m_RenderGraphCpuAccumulatedMilliseconds = 0.0;
    m_RenderGraphCpuDisplayMilliseconds = 0.0;
    m_RenderGraphCpuFrameCount = 0;
    m_CudaTimingDisplay = {};
    m_LastCudaTimingFrameIndex = 0;
    ResetCudaAccumulator();
}

void DemoProfiling::ProfilerDisplayController::AccumulateDirectQueueSamples(
    const std::vector<GpuTimestampSample>& samples)
{
    m_DirectQueueAverager.Accumulate(samples);
}

void DemoProfiling::ProfilerDisplayController::AccumulateAsyncComputeQueueSamples(
    const std::vector<GpuTimestampSample>& samples)
{
    m_AsyncComputeQueueAverager.Accumulate(samples);
}

void DemoProfiling::ProfilerDisplayController::AccumulateCopyQueueSamples(
    const std::vector<GpuTimestampSample>& samples)
{
    m_CopyQueueAverager.Accumulate(samples);
}

void DemoProfiling::ProfilerDisplayController::AccumulateRenderGraphCpuMilliseconds(const double milliseconds)
{
    m_RenderGraphCpuAccumulatedMilliseconds += milliseconds;
    ++m_RenderGraphCpuFrameCount;
}

void DemoProfiling::ProfilerDisplayController::AccumulateCudaTiming(const CudaTimingSample& sample)
{
    if (!sample.Valid || sample.FrameIndex == m_LastCudaTimingFrameIndex)
    {
        return;
    }
    if (m_LastCudaTimingFrameIndex != 0 && sample.FrameIndex < m_LastCudaTimingFrameIndex)
    {
        ResetCudaAccumulator();
    }

    m_LastCudaTimingFrameIndex = sample.FrameIndex;
    m_CudaTimingAccumulated.D3DToCudaWaitMilliseconds += sample.D3DToCudaWaitMilliseconds;
    m_CudaTimingAccumulated.KernelsMilliseconds += sample.KernelsMilliseconds;
    m_CudaTimingAccumulated.CudaSignalMilliseconds += sample.CudaSignalMilliseconds;
    m_CudaTimingAccumulated.TotalCudaStreamMilliseconds += sample.TotalCudaStreamMilliseconds;
    m_CudaTimingAccumulated.FrameIndex = sample.FrameIndex;
    m_CudaTimingAccumulated.Valid = true;
    ++m_CudaTimingFrameCount;
}

bool DemoProfiling::ProfilerDisplayController::Update(const double totalTimeSeconds)
{
    if (m_WindowStartTimeSeconds < 0.0)
    {
        m_WindowStartTimeSeconds = totalTimeSeconds;
        return false;
    }
    if (totalTimeSeconds - m_WindowStartTimeSeconds < m_RefreshIntervalSeconds)
    {
        return false;
    }

    PublishQueueAverage(m_DirectQueueAverager, m_DirectQueueDisplaySamples);
    PublishQueueAverage(m_AsyncComputeQueueAverager, m_AsyncComputeQueueDisplaySamples);
    PublishQueueAverage(m_CopyQueueAverager, m_CopyQueueDisplaySamples);

    if (m_RenderGraphCpuFrameCount > 0)
    {
        m_RenderGraphCpuDisplayMilliseconds = m_RenderGraphCpuAccumulatedMilliseconds /
            static_cast<double>(m_RenderGraphCpuFrameCount);
        m_RenderGraphCpuAccumulatedMilliseconds = 0.0;
        m_RenderGraphCpuFrameCount = 0;
    }

    if (m_CudaTimingFrameCount > 0)
    {
        const double reciprocalFrameCount = 1.0 / static_cast<double>(m_CudaTimingFrameCount);
        m_CudaTimingDisplay = m_CudaTimingAccumulated;
        m_CudaTimingDisplay.D3DToCudaWaitMilliseconds *= reciprocalFrameCount;
        m_CudaTimingDisplay.KernelsMilliseconds *= reciprocalFrameCount;
        m_CudaTimingDisplay.CudaSignalMilliseconds *= reciprocalFrameCount;
        m_CudaTimingDisplay.TotalCudaStreamMilliseconds *= reciprocalFrameCount;
        ResetCudaAccumulator();
    }

    m_WindowStartTimeSeconds = totalTimeSeconds;
    return true;
}

void DemoProfiling::ProfilerDisplayController::TimestampSampleAverager::Clear()
{
    m_AccumulatedSamples.clear();
    m_FrameCount = 0;
}

void DemoProfiling::ProfilerDisplayController::TimestampSampleAverager::Accumulate(
    const std::vector<GpuTimestampSample>& samples)
{
    if (samples.empty())
    {
        return;
    }
    if (!IsCompatible(samples))
    {
        Clear();
    }
    if (m_AccumulatedSamples.empty())
    {
        m_AccumulatedSamples = samples;
        m_FrameCount = 1;
        return;
    }

    for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
    {
        GpuTimestampSample& accumulated = m_AccumulatedSamples[sampleIndex];
        const GpuTimestampSample& sample = samples[sampleIndex];
        accumulated.MillisecondsFromFrameStart += sample.MillisecondsFromFrameStart;
        accumulated.MillisecondsFromPrevious += sample.MillisecondsFromPrevious;
        accumulated.CpuMillisecondsFromFrameStart += sample.CpuMillisecondsFromFrameStart;
        accumulated.CpuMillisecondsFromPrevious += sample.CpuMillisecondsFromPrevious;
    }
    ++m_FrameCount;
}

std::vector<GpuTimestampSample>
DemoProfiling::ProfilerDisplayController::TimestampSampleAverager::ConsumeAverage()
{
    if (m_FrameCount == 0)
    {
        return {};
    }

    std::vector<GpuTimestampSample> average = m_AccumulatedSamples;
    const double reciprocalFrameCount = 1.0 / static_cast<double>(m_FrameCount);
    for (GpuTimestampSample& sample : average)
    {
        sample.MillisecondsFromFrameStart *= reciprocalFrameCount;
        sample.MillisecondsFromPrevious *= reciprocalFrameCount;
        sample.CpuMillisecondsFromFrameStart *= reciprocalFrameCount;
        sample.CpuMillisecondsFromPrevious *= reciprocalFrameCount;
    }
    Clear();
    return average;
}

bool DemoProfiling::ProfilerDisplayController::TimestampSampleAverager::IsCompatible(
    const std::vector<GpuTimestampSample>& samples) const
{
    if (m_AccumulatedSamples.empty())
    {
        return true;
    }
    if (m_AccumulatedSamples.size() != samples.size())
    {
        return false;
    }
    for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex)
    {
        if (m_AccumulatedSamples[sampleIndex].Name != samples[sampleIndex].Name)
        {
            return false;
        }
    }
    return true;
}

void DemoProfiling::ProfilerDisplayController::PublishQueueAverage(
    TimestampSampleAverager& averager,
    std::vector<GpuTimestampSample>& displaySamples)
{
    if (averager.HasSamples())
    {
        displaySamples = averager.ConsumeAverage();
    }
}

void DemoProfiling::ProfilerDisplayController::ResetCudaAccumulator()
{
    m_CudaTimingAccumulated = {};
    m_CudaTimingFrameCount = 0;
}
//Modify End
