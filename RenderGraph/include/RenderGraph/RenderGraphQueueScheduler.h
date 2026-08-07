//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "RenderGraphQueueFence.h"
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
//Modify Begin:2026-07-30 by BestHui
        uint64_t SubmitDirect(std::vector<std::shared_ptr<CommandList>>& commandLists);
//Modify End
        uint64_t SubmitAsyncCompute(std::shared_ptr<CommandList>& commandList, bool waitForCompletion);
        uint64_t GetCrossQueueProducerFence(const RenderPass& pass) const;
        bool WasLastWrittenBy(ResourceId resourceId, RenderPassQueue queue) const;
        void WaitForDirectSubmissionOnAsyncCompute(uint64_t fenceValue) const;
        void WaitForAsyncComputeSubmissionOnDirect(uint64_t fenceValue);
        void TrackPassResources(const RenderPass& pass, uint64_t fenceValue);
        void TrackExternalResource(ResourceId resourceId, RenderPassQueue queue);
        const std::map<ResourceId, RenderGraphQueueFenceValues>& GetResourceRetirements() const;

    private:
        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::map<ResourceId, RenderPassQueue> m_LastWriterQueues;
        std::map<ResourceId, uint64_t> m_LastWriterFenceValues;
        std::map<ResourceId, RenderGraphQueueFenceValues> m_ResourceRetirements;
        std::set<ResourceId> m_PendingDirectResources;
        bool m_AsyncComputeSubmitted = false;
        uint64_t m_LastAsyncComputeFenceValue = 0;
    };
}
//Modify End
