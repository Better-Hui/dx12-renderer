//Modify Begin:2026-08-18 by Hui
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "RenderGraphQueueFence.h"
#include "RenderPass.h"

class CommandList;
class CommandQueue;
class DiagnosticTelemetrySink;
struct DiagnosticTelemetryEvent;

namespace RenderGraph
{
    class RenderGraphQueueScheduler final
    {
    public:
        RenderGraphQueueScheduler(
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
            std::shared_ptr<CommandQueue> copyCommandQueue);

        void SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept;

        void BeginFrame();
        uint64_t SubmitDirect(std::shared_ptr<CommandList>& commandList);
        uint64_t SubmitDirect(std::vector<std::shared_ptr<CommandList>>& commandLists);
        uint64_t SubmitAsyncCompute(std::shared_ptr<CommandList>& commandList, bool waitForCompletion);
        uint64_t SubmitCopy(std::shared_ptr<CommandList>& commandList, bool waitForCompletion);

        RenderGraphQueueFenceValues GetCrossQueueProducerFences(
            const RenderPass& pass,
            RenderPassQueue waitingQueue) const;
        void WaitForDependencies(RenderPassQueue waitingQueue, const RenderGraphQueueFenceValues& dependencies);
        void WaitForDirectSubmission(RenderPassQueue waitingQueue, uint64_t fenceValue);

        void TrackPassResources(const RenderPass& pass, uint64_t fenceValue);
        void TrackExternalResource(ResourceId resourceId, RenderPassQueue queue);
        const std::map<ResourceId, RenderGraphQueueFenceValues>& GetResourceRetirements() const;
        RenderGraphQueueFenceValues GetResourceRetirement(ResourceId resourceId) const;
        const RenderGraphQueueFenceValues& GetFrameSubmissionFences() const;
        const RenderGraphQueueSynchronizationStats& GetFrameSynchronizationStats() const;

    private:
        static size_t QueueIndex(RenderPassQueue queue);
        static uint64_t& QueueFence(RenderGraphQueueFenceValues& fences, RenderPassQueue queue);
        static uint64_t QueueFence(const RenderGraphQueueFenceValues& fences, RenderPassQueue queue);

        struct ExternalResourceUsage
        {
            RenderPassQueue LastWriterQueue = RenderPassQueue::Direct;
            uint64_t LastWriterFenceValue = 0;
            bool HasWriter = false;
            std::array<uint64_t, 3> ReaderFenceValues = {};
            std::array<bool, 3> HasReader = {};
        };

        CommandQueue& GetCommandQueue(RenderPassQueue queue) const;
        uint64_t SubmitNonDirect(
            RenderPassQueue queue,
            std::shared_ptr<CommandList>& commandList,
            bool waitForCompletion);
        void FinalizeDirectSubmission(uint64_t fenceValue);
        void TrackExternalResourceAccess(
            const ExternalResourceAccess& access,
            RenderPassQueue queue,
            uint64_t fenceValue);
        void EmitTelemetry(DiagnosticTelemetryEvent event) const noexcept;
        [[nodiscard]] bool HasDiagnosticTelemetrySink() const noexcept { return m_DiagnosticTelemetrySink != nullptr; }

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<CommandQueue> m_CopyCommandQueue;
        DiagnosticTelemetrySink* m_DiagnosticTelemetrySink = nullptr;
        std::map<ResourceId, RenderPassQueue> m_LastWriterQueues;
        std::map<ResourceId, uint64_t> m_LastWriterFenceValues;
        std::map<const Resource*, ExternalResourceUsage> m_ExternalResourceUsages;
        std::map<ResourceId, RenderGraphQueueFenceValues> m_ResourceRetirements;
        RenderGraphQueueFenceValues m_FrameSubmissionFences;
        RenderGraphQueueSynchronizationStats m_FrameSynchronizationStats;
        std::set<ResourceId> m_PendingDirectResources;
        uint64_t m_LastAsyncComputeFenceValue = 0;
        uint64_t m_LastCopyFenceValue = 0;
    };
}
//Modify End
