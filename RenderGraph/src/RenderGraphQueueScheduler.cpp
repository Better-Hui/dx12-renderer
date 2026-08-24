//Modify Begin:2026-08-24 by Hui
#include "RenderGraphQueueScheduler.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/DiagnosticTelemetry.h>
#include <DX12Library/Helpers.h>

namespace RenderGraph
{
    namespace
    {
        const char* GetQueueName(const RenderPassQueue queue)
        {
            switch (queue)
            {
            case RenderPassQueue::Direct: return "Direct";
            case RenderPassQueue::AsyncCompute: return "AsyncCompute";
            case RenderPassQueue::Copy: return "Copy";
            default: return "Unknown";
            }
        }
    }

    RenderGraphQueueScheduler::RenderGraphQueueScheduler(
        std::shared_ptr<CommandQueue> directCommandQueue,
        std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
        std::shared_ptr<CommandQueue> copyCommandQueue)
        : m_DirectCommandQueue(std::move(directCommandQueue))
        , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
        , m_CopyCommandQueue(std::move(copyCommandQueue))
    {
        Assert(m_DirectCommandQueue != nullptr, "Render graph requires a direct command queue.");
        Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph requires an async compute command queue.");
        Assert(m_CopyCommandQueue != nullptr, "Render graph requires a copy command queue.");
    }

    void RenderGraphQueueScheduler::SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept
    {
        m_DiagnosticTelemetrySink = sink;
    }

    void RenderGraphQueueScheduler::EmitTelemetry(DiagnosticTelemetryEvent event) const noexcept
    {
        if (m_DiagnosticTelemetrySink != nullptr)
        {
            m_DiagnosticTelemetrySink->RecordTelemetry(std::move(event));
        }
    }

    void RenderGraphQueueScheduler::BeginFrame()
    {
        if (m_LastAsyncComputeFenceValue != 0)
        {
            m_DirectCommandQueue->Wait(*m_AsyncComputeCommandQueue, m_LastAsyncComputeFenceValue);
        }
        if (m_LastCopyFenceValue != 0)
        {
            m_DirectCommandQueue->Wait(*m_CopyCommandQueue, m_LastCopyFenceValue);
        }

        m_LastWriterQueues.clear();
        m_LastWriterFenceValues.clear();
        m_ExternalResourceUsages.clear();
        m_ResourceRetirements.clear();
        m_FrameSubmissionFences = {};
        m_PendingDirectResources.clear();
        m_LastAsyncComputeFenceValue = 0;
        m_LastCopyFenceValue = 0;
    }

    uint64_t RenderGraphQueueScheduler::SubmitDirect(std::shared_ptr<CommandList>& commandList)
    {
        if (commandList == nullptr)
        {
            return 0;
        }

        const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(commandList);
        FinalizeDirectSubmission(fenceValue);
        if (HasDiagnosticTelemetrySink())
        {
            EmitTelemetry({
                .Category = "render_graph.queue.submission",
                .Name = "submit",
                .CorrelationId = MakeDiagnosticQueueFenceCorrelationId("Direct", fenceValue),
                .Fields = {
                    { "queue", std::string("Direct") },
                    { "fence", fenceValue },
                    { "command_list_count", uint64_t{ 1 } },
                },
            });
        }
        commandList.reset();
        return fenceValue;
    }

    uint64_t RenderGraphQueueScheduler::SubmitDirect(
        std::vector<std::shared_ptr<CommandList>>& commandLists)
    {
        if (commandLists.empty())
        {
            return 0;
        }

        const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandLists(commandLists);
        FinalizeDirectSubmission(fenceValue);
        if (HasDiagnosticTelemetrySink())
        {
            EmitTelemetry({
                .Category = "render_graph.queue.submission",
                .Name = "submit",
                .CorrelationId = MakeDiagnosticQueueFenceCorrelationId("Direct", fenceValue),
                .Fields = {
                    { "queue", std::string("Direct") },
                    { "fence", fenceValue },
                    { "command_list_count", static_cast<uint64_t>(commandLists.size()) },
                },
            });
        }
        commandLists.clear();
        return fenceValue;
    }

    uint64_t RenderGraphQueueScheduler::SubmitAsyncCompute(
        std::shared_ptr<CommandList>& commandList,
        const bool waitForCompletion)
    {
        return SubmitNonDirect(RenderPassQueue::AsyncCompute, commandList, waitForCompletion);
    }

    uint64_t RenderGraphQueueScheduler::SubmitCopy(
        std::shared_ptr<CommandList>& commandList,
        const bool waitForCompletion)
    {
        return SubmitNonDirect(RenderPassQueue::Copy, commandList, waitForCompletion);
    }

    uint64_t RenderGraphQueueScheduler::SubmitNonDirect(
        const RenderPassQueue queue,
        std::shared_ptr<CommandList>& commandList,
        const bool waitForCompletion)
    {
        Assert(queue != RenderPassQueue::Direct, "SubmitNonDirect cannot submit a direct command list.");
        Assert(commandList != nullptr, "Cannot submit a null non-direct command list.");
        CommandQueue& commandQueue = GetCommandQueue(queue);
        const uint64_t fenceValue = commandQueue.ExecuteCommandList(commandList);
        QueueFence(m_FrameSubmissionFences, queue) = (std::max)(
            QueueFence(m_FrameSubmissionFences, queue),
            fenceValue);
        commandList.reset();

        if (HasDiagnosticTelemetrySink())
        {
            EmitTelemetry({
                .Category = "render_graph.queue.submission",
                .Name = "submit",
                .CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueName(queue), fenceValue),
                .Fields = {
                    { "queue", std::string(GetQueueName(queue)) },
                    { "fence", fenceValue },
                    { "command_list_count", uint64_t{ 1 } },
                    { "cpu_wait_for_completion", waitForCompletion },
                },
            });
        }

        if (waitForCompletion)
        {
            commandQueue.WaitForFenceValue(fenceValue);
        }

        if (queue == RenderPassQueue::AsyncCompute)
        {
            m_LastAsyncComputeFenceValue = fenceValue;
        }
        else
        {
            m_LastCopyFenceValue = fenceValue;
        }
        return fenceValue;
    }

    RenderGraphQueueFenceValues RenderGraphQueueScheduler::GetCrossQueueProducerFences(
        const RenderPass& pass,
        const RenderPassQueue waitingQueue) const
    {
        RenderGraphQueueFenceValues dependencies = {};
        const auto inspectFence = [waitingQueue, &dependencies](
            const RenderPassQueue producerQueue,
            const uint64_t fenceValue)
        {
            if (producerQueue == waitingQueue || fenceValue == 0)
            {
                return;
            }
            QueueFence(dependencies, producerQueue) = (std::max)(
                QueueFence(dependencies, producerQueue),
                fenceValue);
        };
        const auto inspectResource = [this, &inspectFence](const ResourceId resourceId)
        {
            const auto writer = m_LastWriterQueues.find(resourceId);
            if (writer == m_LastWriterQueues.end())
            {
                return;
            }
            const auto fence = m_LastWriterFenceValues.find(resourceId);
            Assert(fence != m_LastWriterFenceValues.end(), "Render pass writer fence was not recorded.");
            inspectFence(writer->second, fence->second);
        };

        for (const Input& input : pass.GetInputs())
        {
            if (input.m_Type != InputType::ExternalAccess)
            {
                inspectResource(input.m_Id);
            }
        }
        for (const Output& output : pass.GetOutputs())
        {
            if (output.m_Type != OutputType::ExternalAccess)
            {
                inspectResource(output.m_Id);
            }
        }

        for (const ExternalResourceAccess& access : pass.GetExternalResourceAccesses())
        {
            access.Resolve().ForEachResourceRecursive(
                [this, &access, &inspectFence](const Resource& resource)
                {
                    const auto usageIt = m_ExternalResourceUsages.find(&resource);
                    if (usageIt == m_ExternalResourceUsages.end())
                    {
                        return;
                    }

                    const ExternalResourceUsage& usage = usageIt->second;
                    if (usage.HasWriter)
                    {
                        inspectFence(usage.LastWriterQueue, usage.LastWriterFenceValue);
                    }
                    if (access.Mode == ExternalResourceAccessMode::Write)
                    {
                        for (const RenderPassQueue readerQueue : {
                            RenderPassQueue::Direct,
                            RenderPassQueue::AsyncCompute,
                            RenderPassQueue::Copy })
                        {
                            const size_t readerIndex = QueueIndex(readerQueue);
                            if (usage.HasReader[readerIndex])
                            {
                                inspectFence(readerQueue, usage.ReaderFenceValues[readerIndex]);
                            }
                        }
                    }
                });
        }
        return dependencies;
    }

    void RenderGraphQueueScheduler::WaitForDependencies(
        const RenderPassQueue waitingQueue,
        const RenderGraphQueueFenceValues& dependencies)
    {
        CommandQueue& consumer = GetCommandQueue(waitingQueue);
        for (const RenderPassQueue producerQueue : {
            RenderPassQueue::Direct,
            RenderPassQueue::AsyncCompute,
            RenderPassQueue::Copy })
        {
            if (producerQueue == waitingQueue)
            {
                continue;
            }
            const uint64_t fenceValue = QueueFence(dependencies, producerQueue);
            if (fenceValue != 0)
            {
                consumer.Wait(GetCommandQueue(producerQueue), fenceValue);
                if (HasDiagnosticTelemetrySink())
                {
                    EmitTelemetry({
                        .Category = "render_graph.queue.wait",
                        .Name = "dependency",
                        .CorrelationId = MakeDiagnosticQueueFenceCorrelationId(GetQueueName(producerQueue), fenceValue),
                        .Fields = {
                            { "consumer_queue", std::string(GetQueueName(waitingQueue)) },
                            { "producer_queue", std::string(GetQueueName(producerQueue)) },
                            { "producer_fence", fenceValue },
                        },
                    });
                }
            }
        }

        if (waitingQueue == RenderPassQueue::Direct)
        {
            if (dependencies.AsyncCompute >= m_LastAsyncComputeFenceValue)
            {
                m_LastAsyncComputeFenceValue = 0;
            }
            if (dependencies.Copy >= m_LastCopyFenceValue)
            {
                m_LastCopyFenceValue = 0;
            }
        }
    }

    void RenderGraphQueueScheduler::WaitForDirectSubmission(
        const RenderPassQueue waitingQueue,
        const uint64_t fenceValue)
    {
        Assert(waitingQueue != RenderPassQueue::Direct, "Direct queue cannot wait for itself.");
        if (fenceValue != 0)
        {
            GetCommandQueue(waitingQueue).Wait(*m_DirectCommandQueue, fenceValue);
            if (HasDiagnosticTelemetrySink())
            {
                EmitTelemetry({
                    .Category = "render_graph.queue.wait",
                    .Name = "direct_preamble",
                    .CorrelationId = MakeDiagnosticQueueFenceCorrelationId("Direct", fenceValue),
                    .Fields = {
                        { "consumer_queue", std::string(GetQueueName(waitingQueue)) },
                        { "producer_queue", std::string("Direct") },
                        { "producer_fence", fenceValue },
                    },
                });
            }
        }
    }

    void RenderGraphQueueScheduler::TrackPassResources(
        const RenderPass& pass,
        const uint64_t fenceValue)
    {
        const auto trackResource = [this, &pass, fenceValue](const ResourceId resourceId)
        {
            if (pass.GetQueue() == RenderPassQueue::Direct)
            {
                m_PendingDirectResources.insert(resourceId);
                return;
            }

            Assert(fenceValue != 0, "Non-direct resource usage requires a submitted fence value.");
            uint64_t& retirementFence = QueueFence(m_ResourceRetirements[resourceId], pass.GetQueue());
            retirementFence = (std::max)(retirementFence, fenceValue);
        };

        for (const Input& input : pass.GetInputs())
        {
            if (input.m_Type != InputType::ExternalAccess)
            {
                trackResource(input.m_Id);
            }
        }
        for (const Output& output : pass.GetOutputs())
        {
            if (output.m_Type != OutputType::ExternalAccess)
            {
                trackResource(output.m_Id);
                m_LastWriterQueues[output.m_Id] = pass.GetQueue();
                m_LastWriterFenceValues[output.m_Id] = fenceValue;
            }
        }
        for (const ExternalResourceAccess& access : pass.GetExternalResourceAccesses())
        {
            TrackExternalResourceAccess(access, pass.GetQueue(), fenceValue);
        }
    }

    void RenderGraphQueueScheduler::FinalizeDirectSubmission(const uint64_t fenceValue)
    {
        m_FrameSubmissionFences.Direct = (std::max)(m_FrameSubmissionFences.Direct, fenceValue);
        for (const ResourceId resourceId : m_PendingDirectResources)
        {
            m_ResourceRetirements[resourceId].Direct = (std::max)(
                m_ResourceRetirements[resourceId].Direct,
                fenceValue);
        }
        m_PendingDirectResources.clear();

        for (const auto& [resourceId, queue] : m_LastWriterQueues)
        {
            if (queue == RenderPassQueue::Direct && m_LastWriterFenceValues[resourceId] == 0)
            {
                m_LastWriterFenceValues[resourceId] = fenceValue;
            }
        }

        const size_t directIndex = QueueIndex(RenderPassQueue::Direct);
        for (auto& [resource, usage] : m_ExternalResourceUsages)
        {
            (void)resource;
            if (usage.HasWriter &&
                usage.LastWriterQueue == RenderPassQueue::Direct &&
                usage.LastWriterFenceValue == 0)
            {
                usage.LastWriterFenceValue = fenceValue;
            }
            if (usage.HasReader[directIndex] && usage.ReaderFenceValues[directIndex] == 0)
            {
                usage.ReaderFenceValues[directIndex] = fenceValue;
            }
        }
    }

    void RenderGraphQueueScheduler::TrackExternalResourceAccess(
        const ExternalResourceAccess& access,
        const RenderPassQueue queue,
        const uint64_t fenceValue)
    {
        access.Resolve().ForEachResourceRecursive(
            [this, &access, queue, fenceValue](const Resource& resource)
            {
                ExternalResourceUsage& usage = m_ExternalResourceUsages[&resource];
                if (access.Mode == ExternalResourceAccessMode::Write)
                {
                    usage.LastWriterQueue = queue;
                    usage.LastWriterFenceValue = fenceValue;
                    usage.HasWriter = true;
                    usage.ReaderFenceValues = {};
                    usage.HasReader = {};
                    return;
                }

                const size_t queueIndex = QueueIndex(queue);
                usage.ReaderFenceValues[queueIndex] = fenceValue;
                usage.HasReader[queueIndex] = true;
            });
    }

    void RenderGraphQueueScheduler::TrackExternalResource(
        const ResourceId resourceId,
        const RenderPassQueue queue)
    {
        if (queue != RenderPassQueue::Direct)
        {
            throw std::invalid_argument("External graph outputs must be tracked on the direct queue.");
        }
        m_PendingDirectResources.insert(resourceId);
    }

    const std::map<ResourceId, RenderGraphQueueFenceValues>&
    RenderGraphQueueScheduler::GetResourceRetirements() const
    {
        return m_ResourceRetirements;
    }

    const RenderGraphQueueFenceValues& RenderGraphQueueScheduler::GetFrameSubmissionFences() const
    {
        return m_FrameSubmissionFences;
    }

    size_t RenderGraphQueueScheduler::QueueIndex(const RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderPassQueue::Direct:
            return 0u;
        case RenderPassQueue::AsyncCompute:
            return 1u;
        case RenderPassQueue::Copy:
            return 2u;
        default:
            Assert(false, "Unknown render-pass queue.");
            return 0u;
        }
    }

    uint64_t& RenderGraphQueueScheduler::QueueFence(
        RenderGraphQueueFenceValues& fences,
        const RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderPassQueue::Direct:
            return fences.Direct;
        case RenderPassQueue::AsyncCompute:
            return fences.AsyncCompute;
        case RenderPassQueue::Copy:
            return fences.Copy;
        default:
            Assert(false, "Unknown render-pass queue.");
            return fences.Direct;
        }
    }

    uint64_t RenderGraphQueueScheduler::QueueFence(
        const RenderGraphQueueFenceValues& fences,
        const RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderPassQueue::Direct:
            return fences.Direct;
        case RenderPassQueue::AsyncCompute:
            return fences.AsyncCompute;
        case RenderPassQueue::Copy:
            return fences.Copy;
        default:
            Assert(false, "Unknown render-pass queue.");
            return 0;
        }
    }

    CommandQueue& RenderGraphQueueScheduler::GetCommandQueue(const RenderPassQueue queue) const
    {
        switch (queue)
        {
        case RenderPassQueue::Direct:
            return *m_DirectCommandQueue;
        case RenderPassQueue::AsyncCompute:
            return *m_AsyncComputeCommandQueue;
        case RenderPassQueue::Copy:
            return *m_CopyCommandQueue;
        default:
            Assert(false, "Unknown render-pass queue.");
            return *m_DirectCommandQueue;
        }
    }
}
//Modify End
