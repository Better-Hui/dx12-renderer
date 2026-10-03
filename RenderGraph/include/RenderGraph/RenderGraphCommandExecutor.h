//Modify Begin:2026-09-28 by Hui
#pragma once

#include "RenderGraphExecutionPlan.h"
#include "RenderGraphBarrierRecorder.h"
#include "RenderPass.h"
#include "RenderGraphTaskScheduler.h"
#include "RenderTargetInfo.h"

#include <DX12Library/DiagnosticRenderScope.h>

#include <memory>
#include <future>
#include <span>
#include <unordered_map>
#include <vector>

class CommandList;
class CommandQueue;
class DiagnosticTelemetrySink;
struct DiagnosticTelemetryEvent;
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
            bool enableParallelRecording);
        void SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept;

    private:
        //Modify Begin:2026-10-02 by Hui
        struct PendingParallelBatch
        {
            const RenderGraphRecordingBatch* Batch = nullptr;
            const RenderPass* LastQueuePass = nullptr;
            std::shared_ptr<CommandList> ProfilerCommandList;
            std::vector<std::future<std::shared_ptr<CommandList>>> RecordingTasks;
        };

        PendingParallelBatch EnqueueParallelBatch(
            const RenderGraphRecordingBatch& batch,
            const RenderMetadata& renderMetadata,
            const RenderPass* lastQueuePass,
            const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void ExecutePendingParallelBatch(
            PendingParallelBatch& pendingBatch,
            const RenderMetadata& renderMetadata,
            bool debugSerializeAsyncCompute,
            std::shared_ptr<CommandList>& directCommandList,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        std::shared_ptr<CommandList> RecordParallelPass(
            RenderPass& renderPass,
            const RenderMetadata& renderMetadata,
            const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        //Modify End
        void RecordPassBoundaryBarriers(
            RenderPassContext& passContext,
            const RenderPass& renderPass,
            RenderContext& context,
            uint64_t frameIndex,
            const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void PrepareDirectQueueDependencies(
            std::span<RenderPass* const> passes,
            std::shared_ptr<CommandList>& directCommandList,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void ExecuteNonDirectBatch(
            const RenderGraphRecordingBatch& batch,
            const RenderMetadata& renderMetadata,
            const RenderPass* lastQueuePass,
            bool debugSerializeAsyncCompute,
            std::shared_ptr<CommandList>& directCommandList,
            const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void PrepareNonDirectBatchDependencies(
            const RenderGraphRecordingBatch& batch,
            std::shared_ptr<CommandList>& directCommandList,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void ApplyDirectQueuePreamble(
            const RenderPass& pass,
            CommandList& commandList,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        void RecordLocalAliasingBarriers(
            const RenderPass& pass,
            const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans);
        CommandQueue& GetCommandQueue(RenderPassQueue queue) const;
        static void ApplyExternalResourceTransitions(
            RenderPassContext& passContext,
            std::span<const PassExternalResourceTransition> transitions);
        void EmitTelemetry(DiagnosticTelemetryEvent event) const noexcept;
        [[nodiscard]] bool HasDiagnosticTelemetrySink() const noexcept { return m_DiagnosticTelemetrySink != nullptr; }
        std::unique_ptr<DX12Diagnostics::DiagnosticRenderPassScope> CreateDiagnosticRenderPassScope(
            const RenderPass& pass,
            uint64_t frameIndex) const;
        void EmitShaderAccessValidation(
            const DX12Diagnostics::DiagnosticRenderPassScope& scope) const noexcept;
        static uint64_t GetPassCorrelationId(const RenderPass& pass) noexcept;

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<CommandQueue> m_CopyCommandQueue;
        std::shared_ptr<ResourcePool> m_ResourcePool;
        RenderGraphQueueScheduler& m_QueueScheduler;
        RenderGraphProfiler& m_Profiler;
        DiagnosticTelemetrySink* m_DiagnosticTelemetrySink = nullptr;
        RenderGraphTaskScheduler m_ParallelRecordingTaskScheduler;
    };
}
//Modify End
