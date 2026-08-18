//Modify Begin:2026-07-30 by Hui
#pragma once

#include "RenderGraphExecutionPlan.h"
#include "RenderPass.h"
#include "RenderGraphTaskScheduler.h"
#include "RenderTargetInfo.h"

#include <map>
#include <memory>
//Modify Begin:2026-08-12 by Hui
#include <span>
//Modify End
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
            std::shared_ptr<CommandQueue> copyCommandQueue,
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
//Modify Begin:2026-07-30 by Hui
        void ExecuteParallelDirectBatch(
            const RenderGraphRecordingBatch& batch,
            const RenderMetadata& renderMetadata,
            std::shared_ptr<CommandList>& directCommandList,
            const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
//Modify End
        void PrepareDirectQueueDependencies(
            std::span<RenderPass* const> passes,
            std::shared_ptr<CommandList>& directCommandList);
//Modify Begin:2026-08-18 by Hui
        void ExecuteNonDirectBatch(
            const RenderGraphRecordingBatch& batch,
            const RenderMetadata& renderMetadata,
            const RenderPass* lastQueuePass,
            bool debugSerializeAsyncCompute,
            std::shared_ptr<CommandList>& directCommandList,
            const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void PrepareNonDirectBatchDependencies(
            const RenderGraphRecordingBatch& batch,
            std::shared_ptr<CommandList>& directCommandList,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void ApplyDirectQueuePreamble(
            const RenderPass& pass,
            CommandList& commandList,
            const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        CommandQueue& GetCommandQueue(RenderPassQueue queue) const;
//Modify End
//Modify Begin:2026-08-13 by Hui
        static void ApplyExternalResourceTransitions(
            CommandList& commandList,
            std::span<const PassExternalResourceTransition> transitions);
//Modify End

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<CommandQueue> m_CopyCommandQueue;
        std::shared_ptr<ResourcePool> m_ResourcePool;
        RenderGraphQueueScheduler& m_QueueScheduler;
        RenderGraphProfiler& m_Profiler;
//Modify Begin:2026-08-07 by Hui
        RenderGraphTaskScheduler m_ParallelRecordingTaskScheduler;
//Modify End
    };
}
//Modify End
