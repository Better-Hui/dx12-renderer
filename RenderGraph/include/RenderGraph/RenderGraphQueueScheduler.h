//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "RenderPass.h"

class CommandList;
class CommandQueue;

namespace RenderGraph
{
    class RenderGraphQueueScheduler final
    {
    public:
        RenderGraphQueueScheduler(
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue);

        void BeginFrame();
        uint64_t SubmitDirect(std::shared_ptr<CommandList>& commandList);
        uint64_t SubmitAsyncCompute(std::shared_ptr<CommandList>& commandList, bool waitForCompletion);
        uint64_t GetCrossQueueProducerFence(const RenderPass& pass) const;
        bool WasLastWrittenBy(ResourceId resourceId, RenderPassQueue queue) const;
        void WaitForDirectSubmissionOnAsyncCompute(uint64_t fenceValue) const;
        void WaitForAsyncComputeSubmissionOnDirect(uint64_t fenceValue);
        void MarkPassOutputs(const RenderPass& pass, uint64_t fenceValue);

    private:
        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::map<ResourceId, RenderPassQueue> m_LastWriterQueues;
        std::map<ResourceId, uint64_t> m_LastWriterFenceValues;
        bool m_AsyncComputeSubmitted = false;
        uint64_t m_LastAsyncComputeFenceValue = 0;
    };
}
//Modify End
