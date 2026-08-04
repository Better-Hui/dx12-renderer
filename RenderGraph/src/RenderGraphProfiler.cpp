//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphProfiler.h"

#include <DX12Library/GpuTimestampProfiler.h>

void RenderGraph::RenderGraphProfiler::SetQueueProfiler(
    const RenderPassQueue queue,
    GpuTimestampProfiler* profiler)
{
    GetQueueState(queue).Profiler = profiler;
}

bool RenderGraph::RenderGraphProfiler::BeginQueueFrame(
    const RenderPassQueue queue,
    const uint64_t frameIndex,
    CommandList& commandList)
{
    QueueProfilerState& state = GetQueueState(queue);
    state.FrameActive = state.Profiler != nullptr &&
        state.Profiler->IsAvailable() &&
        state.Profiler->BeginFrame(frameIndex);
    if (state.FrameActive)
    {
        state.Profiler->WriteTimestamp(commandList, "RenderGraph.Begin");
    }
    return state.FrameActive;
}

bool RenderGraph::RenderGraphProfiler::IsQueueFrameActive(const RenderPassQueue queue) const
{
    return GetQueueState(queue).FrameActive;
}

void RenderGraph::RenderGraphProfiler::WritePassTimestamp(
    const RenderPassQueue queue,
    CommandList& commandList,
    const std::wstring& passName) const
{
    WriteMarker(queue, commandList, NarrowPassName(passName));
}

void RenderGraph::RenderGraphProfiler::WriteMarker(
    const RenderPassQueue queue,
    CommandList& commandList,
    const std::string& markerName) const
{
    const QueueProfilerState& state = GetQueueState(queue);
    if (state.FrameActive)
    {
        state.Profiler->WriteTimestamp(commandList, markerName.c_str());
    }
}

void RenderGraph::RenderGraphProfiler::ResolveQueueFrame(
    const RenderPassQueue queue,
    CommandList& commandList) const
{
    const QueueProfilerState& state = GetQueueState(queue);
    if (!state.FrameActive)
    {
        return;
    }

    state.Profiler->WriteTimestamp(commandList, "RenderGraph.End");
    state.Profiler->ResolveFrame(commandList);
}

void RenderGraph::RenderGraphProfiler::EndQueueFrame(
    const RenderPassQueue queue,
    const uint64_t fenceValue)
{
    QueueProfilerState& state = GetQueueState(queue);
    if (state.FrameActive)
    {
        state.Profiler->EndFrame(fenceValue);
        state.FrameActive = false;
    }
}

RenderGraph::RenderGraphProfiler::QueueProfilerState&
RenderGraph::RenderGraphProfiler::GetQueueState(const RenderPassQueue queue)
{
    return queue == RenderPassQueue::AsyncCompute ? m_AsyncCompute : m_Direct;
}

const RenderGraph::RenderGraphProfiler::QueueProfilerState&
RenderGraph::RenderGraphProfiler::GetQueueState(const RenderPassQueue queue) const
{
    return queue == RenderPassQueue::AsyncCompute ? m_AsyncCompute : m_Direct;
}

std::string RenderGraph::RenderGraphProfiler::NarrowPassName(const std::wstring& passName)
{
    std::string result;
    result.reserve(passName.size());
    for (const wchar_t character : passName)
    {
        result.push_back(character >= 0 && character < 128 ? static_cast<char>(character) : '?');
    }
    return result;
}
//Modify End
