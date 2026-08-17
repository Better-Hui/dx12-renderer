//Modify Begin:2026-07-30 by Hui
#pragma once

#include "RenderPass.h"

#include <cstdint>
#include <string>

class CommandList;
class GpuTimestampProfiler;

namespace RenderGraph
{
    class RenderGraphProfiler final
    {
    public:
        void SetQueueProfiler(RenderPassQueue queue, GpuTimestampProfiler* profiler);

        bool BeginQueueFrame(RenderPassQueue queue, uint64_t frameIndex, CommandList& commandList);
        bool IsQueueFrameActive(RenderPassQueue queue) const;
        static std::string NarrowPassName(const std::wstring& passName);
        void WritePassTimestamp(RenderPassQueue queue, CommandList& commandList, const std::wstring& passName) const;
        void WriteMarker(RenderPassQueue queue, CommandList& commandList, const std::string& markerName) const;
        void ResolveQueueFrame(RenderPassQueue queue, CommandList& commandList) const;
        void EndQueueFrame(RenderPassQueue queue, uint64_t fenceValue);

    private:
        struct QueueProfilerState
        {
            GpuTimestampProfiler* Profiler = nullptr;
            bool FrameActive = false;
        };

        QueueProfilerState& GetQueueState(RenderPassQueue queue);
        const QueueProfilerState& GetQueueState(RenderPassQueue queue) const;
        QueueProfilerState m_Direct;
        QueueProfilerState m_AsyncCompute;
    };
}
//Modify End
