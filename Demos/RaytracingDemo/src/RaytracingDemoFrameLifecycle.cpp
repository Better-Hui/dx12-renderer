//Modify Begin:2026-09-28 by Hui
#include <RaytracingDemo.h>

#include <stdexcept>

void RaytracingDemo::UpdateFrameTelemetry(const double totalTime)
{
    const auto directCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const auto asyncComputeCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    const auto copyCommandQueue = m_FrameworkDeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    const auto collectGpuTimingFrames = [this](
        GpuTimestampProfiler& profiler,
        CommandQueue& commandQueue,
        const auto& accumulateSamples,
        const char* queueName)
    {
        std::vector<GpuTimestampSample> completedSamples;
        while (profiler.CollectCompletedFrame(commandQueue, completedSamples))
        {
            if (m_Diagnostics.IsEnabled())
            {
                m_Diagnostics.RecordGpuTimings(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
            accumulateSamples(completedSamples);
            if (m_RenderGraphTimingCaptureEnabled)
            {
                m_RenderGraphTimingHistory.Record(
                    profiler.GetLastCollectedFrameNumber(),
                    queueName,
                    completedSamples);
            }
        }
    };
    if (m_GpuTimingEnabled)
    {
        collectGpuTimingFrames(
            m_GpuTimestampProfiler,
            *directCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateDirectQueueSamples(samples);
            },
            "Direct");
        collectGpuTimingFrames(
            m_AsyncComputeGpuTimestampProfiler,
            *asyncComputeCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateAsyncComputeQueueSamples(samples);
            },
            "AsyncCompute");
        collectGpuTimingFrames(
            m_CopyGpuTimestampProfiler,
            *copyCommandQueue,
            [this](const std::vector<GpuTimestampSample>& samples)
            {
                m_ProfilerDisplay.AccumulateCopyQueueSamples(samples);
            },
            "Copy");
    }

    for (const BloomController::TimingStats& cudaTiming : m_Bloom.ConsumeCompletedTimingStats())
    {
        m_ProfilerDisplay.AccumulateCudaTiming({
            .D3DToCudaWaitMilliseconds = cudaTiming.D3DToCudaWaitMs,
            .KernelsMilliseconds = cudaTiming.KernelsMs,
            .CudaSignalMilliseconds = cudaTiming.CudaSignalMs,
            .TotalCudaStreamMilliseconds = cudaTiming.TotalCudaStreamMs,
            .FrameIndex = cudaTiming.FrameIndex,
            .Valid = cudaTiming.Valid,
        });
    }
    m_ProfilerDisplay.Update(totalTime);

    if (m_ImGui != nullptr)
    {
        m_ImGui->BeginFrame();
        OnImGui();
        m_ImGui->Render();
    }
}

bool RaytracingDemo::ApplyPendingSceneRuntimeChanges()
{
    try
    {
        if (m_SceneRuntime.ApplyPendingChanges(
            m_FrameworkDeviceContext,
            m_SceneResources,
            m_Lights,
            m_RuntimeAutomation))
        {
            m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: rebuild render graph.");
            RebuildRenderGraph();
            m_RenderGraphTimingHistory.Clear();
            ResetAccumulation(AccumulationResetScope::AllHistory);
            m_RuntimeAutomation.AppendDiagnosticLog("Stress transition: complete.");
        }
    }
    catch (const std::exception& exception)
    {
        if (m_RuntimeAutomation.FailNow(
            FrameworkDiagnostics::AutomationExitCode::ControlFailure,
            "Scene runtime update failed: " + std::string(exception.what())))
        {
            return true;
        }
        RecordDiagnosticsFailure("scene_runtime", exception);
        throw;
    }
    return false;
}

void RaytracingDemo::FinalizeRenderedFrame()
{
    ++m_FrameIndex;
    if (m_RenderGraphFrameState->AccumulationEnabled)
    {
        ++m_AccumulationFrameIndex;
    }
    else
    {
        m_AccumulationFrameIndex = 0;
    }
    m_ReSTIRDIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    m_ReSTIRGIHistoryValid =
        m_PathTracingBackend == PathTracingBackend::InlineRayQuery &&
        m_IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI &&
        m_MaxBounces > 1;

    m_PreviousViewProjection = m_RenderGraphFrameState->ViewProjection;
    m_PreviousUnjitteredViewProjection = m_RenderGraphFrameState->UnjitteredViewProjection;
    m_HasPreviousViewProjection = true;
}
//Modify End
