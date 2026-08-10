//Modify Begin:2026-07-30 by BestHui
#pragma once

#include "RenderGraphExecutionPlan.h"
#include "RenderPass.h"
#include "RenderGraphTaskScheduler.h"
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
            RenderContext& context,
            const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
//Modify Begin:2026-07-30 by BestHui
        void ExecuteParallelDirectBatch(
            const RenderGraphRecordingBatch& batch,
            const RenderMetadata& renderMetadata,
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
        RenderGraphProfiler& m_Profiler;
//Modify Begin:2026-08-07 by BestHui
        RenderGraphTaskScheduler m_ParallelRecordingTaskScheduler;
//Modify End
    };
}
//Modify End
