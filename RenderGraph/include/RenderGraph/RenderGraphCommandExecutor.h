//Modify Begin:2026-07-30 by BestHui
#pragma once

#include "RenderGraphExecutionPlan.h"
#include "RenderPass.h"
#include "RenderTargetInfo.h"

#include <map>
#include <memory>
#include <vector>

class CommandList;
class CommandQueue;
class Resource;

namespace RenderGraph
{
    class RenderGraphProfiler;
    class RenderGraphQueueScheduler;
    class RenderGraphResourceStateTracker;
    class ResourcePool;
    struct RenderMetadata;

    class RenderGraphCommandExecutor final
    {
    public:
        RenderGraphCommandExecutor(
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
            std::shared_ptr<ResourcePool> resourcePool,
            RenderGraphQueueScheduler& queueScheduler,
            RenderGraphResourceStateTracker& resourceStateTracker,
            RenderGraphProfiler& profiler);

        void Execute(
            const RenderMetadata& renderMetadata,
            const CompiledRenderGraph& compiledGraph,
            bool debugSerializeAsyncCompute,
            bool enableParallelDirectRecording);

    private:
        void PrepareResourcesForRenderPass(
            CommandList& commandList,
            const RenderPass& renderPass,
            uint32_t renderPassIndex,
            RenderContext& context,
            const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
//Modify Begin:2026-07-30 by BestHui
        void ExecuteParallelDirectBatch(
            const RenderGraphExecutionBatch& batch,
            const RenderMetadata& renderMetadata,
            uint32_t firstRenderPassIndex,
            std::shared_ptr<CommandList>& directCommandList,
            const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
//Modify End
        void PrepareQueueDependency(
            const RenderPass& pass,
            std::shared_ptr<CommandList>& directCommandList,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<ResourcePool> m_ResourcePool;
        RenderGraphQueueScheduler& m_QueueScheduler;
        RenderGraphResourceStateTracker& m_ResourceStateTracker;
        RenderGraphProfiler& m_Profiler;
    };
}
//Modify End
