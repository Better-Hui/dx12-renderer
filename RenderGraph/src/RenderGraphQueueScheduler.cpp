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

        std::string NarrowName(const std::wstring& value)
        {
            std::string result;
            result.reserve(value.size());
            for (const wchar_t character : value)
            {
                result.push_back(character >= 0 && character < 128 ? static_cast<char>(character) : '?');
            }
            return result;
        }
    }

    size_t RenderGraphQueueSynchronizationStats::GetQueueIndex(const RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderPassQueue::Direct: return 0u;
        case RenderPassQueue::AsyncCompute: return 1u;
        case RenderPassQueue::Copy: return 2u;
        default:
            Assert(false, "Unknown render-pass queue.");
            return 0u;
        }
    }

    void RenderGraphQueueSynchronizationStats::RecordSubmission(const RenderPassQueue queue)
    {
        ++SubmissionCounts[GetQueueIndex(queue)];
    }

    void RenderGraphQueueSynchronizationStats::RecordWait(
        const RenderPassQueue producerQueue,
        const RenderPassQueue consumerQueue)
    {
        ++WaitCounts[GetQueueIndex(producerQueue)][GetQueueIndex(consumerQueue)];
    }

    uint64_t RenderGraphQueueSynchronizationStats::GetSubmissionCount(
        const RenderPassQueue queue) const
    {
        return SubmissionCounts[GetQueueIndex(queue)];
    }

    uint64_t RenderGraphQueueSynchronizationStats::GetWaitCount(
        const RenderPassQueue producerQueue,
        const RenderPassQueue consumerQueue) const
    {
        return WaitCounts[GetQueueIndex(producerQueue)][GetQueueIndex(consumerQueue)];
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

    void RenderGraphQueueScheduler::BeginFrame(const uint64_t frameIndex)
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
        m_ReferencedGraphResources.clear();
        m_FrameSubmissionFences = {};
        m_FrameSynchronizationStats = {};
        m_WaitedProducerFences = {};
        m_FrameRuntimeValidation = {};
        m_PendingDirectResources.clear();
        m_CurrentFrameIndex = frameIndex;
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
        m_FrameSynchronizationStats.RecordSubmission(RenderPassQueue::Direct);
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
        m_FrameSynchronizationStats.RecordSubmission(RenderPassQueue::Direct);
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
        m_FrameSynchronizationStats.RecordSubmission(queue);
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
                uint64_t& waitedFence = QueueFence(
                    m_WaitedProducerFences[QueueIndex(waitingQueue)],
                    producerQueue);
                waitedFence = (std::max)(waitedFence, fenceValue);
                m_FrameSynchronizationStats.RecordWait(producerQueue, waitingQueue);
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
            uint64_t& waitedFence = QueueFence(
                m_WaitedProducerFences[QueueIndex(waitingQueue)],
                RenderPassQueue::Direct);
            waitedFence = (std::max)(waitedFence, fenceValue);
            m_FrameSynchronizationStats.RecordWait(RenderPassQueue::Direct, waitingQueue);
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
            m_ReferencedGraphResources.insert(resourceId);
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

    void RenderGraphQueueScheduler::ValidateDirectPassDependencies(
        const std::span<RenderPass* const> passes,
        const RenderGraphQueueFenceValues& dependencies)
    {
        for (const RenderPass* pass : passes)
        {
            Assert(pass != nullptr && pass->GetQueue() == RenderPassQueue::Direct,
                "Direct dependency validation received an invalid pass.");
            std::set<ResourceId> resources;
            for (const Input& input : pass->GetInputs())
            {
                if (input.m_Type != InputType::Token && input.m_Type != InputType::ExternalAccess)
                {
                    resources.insert(input.m_Id);
                }
            }
            for (const Output& output : pass->GetOutputs())
            {
                if (output.m_Type != OutputType::Token && output.m_Type != OutputType::ExternalAccess)
                {
                    resources.insert(output.m_Id);
                }
            }
            for (const ResourceId resourceId : resources)
            {
                ValidateResourceDependency(
                    *pass,
                    resourceId,
                    RenderPassQueue::Direct,
                    dependencies,
                    false,
                    0u);
            }
        }
    }

    void RenderGraphQueueScheduler::ValidateNonDirectBatchDependencies(
        const std::span<RenderPass* const> passes,
        const RenderPassQueue queue,
        const RenderGraphQueueFenceValues& producerDependencies,
        const uint64_t directPreambleFence)
    {
        Assert(queue != RenderPassQueue::Direct,
            "Non-direct dependency validation cannot target the direct queue.");
        for (const RenderPass* pass : passes)
        {
            Assert(pass != nullptr && pass->GetQueue() == queue,
                "Non-direct dependency validation received an invalid pass.");
            std::set<ResourceId> resources;
            for (const Input& input : pass->GetInputs())
            {
                if (input.m_Type != InputType::Token && input.m_Type != InputType::ExternalAccess)
                {
                    resources.insert(input.m_Id);
                }
            }
            for (const Output& output : pass->GetOutputs())
            {
                if (output.m_Type != OutputType::Token && output.m_Type != OutputType::ExternalAccess)
                {
                    resources.insert(output.m_Id);
                }
            }
            for (const ResourceId resourceId : resources)
            {
                ValidateResourceDependency(
                    *pass,
                    resourceId,
                    queue,
                    producerDependencies,
                    true,
                    directPreambleFence);
            }
        }
    }

    void RenderGraphQueueScheduler::ValidateFrameResourceRetirements()
    {
        for (const ResourceId resourceId : m_ReferencedGraphResources)
        {
            const RenderGraphQueueFenceValues retirement = GetResourceRetirement(resourceId);
            if (!retirement.IsEmpty())
            {
                continue;
            }

            ++m_FrameRuntimeValidation.MissingRetirementFenceCount;
            RecordRuntimeInvariantFailure(
                "render_graph_resource_retirement",
                "A graph resource was referenced without a retirement fence.",
                {
                    { "resource_id", static_cast<uint64_t>(resourceId) },
                    { "resource_name", NarrowName(ResourceIds::GetResourceName(resourceId)) },
                });
        }

        EmitTelemetry({
            .Category = "assertion",
            .Name = "render_graph_queue_lifetime_runtime",
            .Severity = m_FrameRuntimeValidation.IsValid()
                ? DiagnosticTelemetrySeverity::Info
                : DiagnosticTelemetrySeverity::Error,
            .FrameIndex = m_CurrentFrameIndex,
            .Fields = {
                { "result", std::string(m_FrameRuntimeValidation.IsValid() ? "pass" : "fail") },
                { "cross_queue_transfer_count", m_FrameRuntimeValidation.CrossQueueTransferCount },
                { "missing_producer_signal_count", m_FrameRuntimeValidation.MissingProducerSignalCount },
                { "missing_consumer_wait_count", m_FrameRuntimeValidation.MissingConsumerWaitCount },
                { "missing_retirement_fence_count", m_FrameRuntimeValidation.MissingRetirementFenceCount },
            },
        });
    }

    void RenderGraphQueueScheduler::ValidateResourceDependency(
        const RenderPass& pass,
        const ResourceId resourceId,
        const RenderPassQueue consumerQueue,
        const RenderGraphQueueFenceValues& producerDependencies,
        const bool throughDirectPreamble,
        const uint64_t directPreambleFence)
    {
        const auto producerQueueIt = m_LastWriterQueues.find(resourceId);
        if (producerQueueIt == m_LastWriterQueues.end() || producerQueueIt->second == consumerQueue)
        {
            return;
        }

        ++m_FrameRuntimeValidation.CrossQueueTransferCount;
        const RenderPassQueue producerQueue = producerQueueIt->second;
        const auto producerFenceIt = m_LastWriterFenceValues.find(resourceId);
        if (producerFenceIt == m_LastWriterFenceValues.end() || producerFenceIt->second == 0u)
        {
            ++m_FrameRuntimeValidation.MissingProducerSignalCount;
            RecordRuntimeInvariantFailure(
                "render_graph_cross_queue_producer_signal",
                "A cross-queue resource consumer has no producer fence signal.",
                {
                    { "pass", NarrowName(pass.GetPassName()) },
                    { "resource_id", static_cast<uint64_t>(resourceId) },
                    { "resource_name", NarrowName(ResourceIds::GetResourceName(resourceId)) },
                    { "producer_queue", std::string(GetQueueName(producerQueue)) },
                    { "consumer_queue", std::string(GetQueueName(consumerQueue)) },
                });
            return;
        }

        const uint64_t producerFence = producerFenceIt->second;
        const uint64_t declaredDependencyFence = QueueFence(producerDependencies, producerQueue);
        const uint64_t directWaitedProducerFence = QueueFence(
            m_WaitedProducerFences[QueueIndex(RenderPassQueue::Direct)],
            producerQueue);
        const uint64_t consumerWaitedDirectFence = QueueFence(
            m_WaitedProducerFences[QueueIndex(consumerQueue)],
            RenderPassQueue::Direct);
        const uint64_t consumerWaitedProducerFence = QueueFence(
            m_WaitedProducerFences[QueueIndex(consumerQueue)],
            producerQueue);

        const bool producerWaitCovered = throughDirectPreamble
            ? (producerQueue == RenderPassQueue::Direct
                ? directPreambleFence != 0u
                : declaredDependencyFence >= producerFence && directWaitedProducerFence >= producerFence)
            : declaredDependencyFence >= producerFence && consumerWaitedProducerFence >= producerFence;
        const bool consumerWaitCovered = !throughDirectPreamble ||
            (directPreambleFence != 0u && consumerWaitedDirectFence >= directPreambleFence);
        if (producerWaitCovered && consumerWaitCovered)
        {
            return;
        }

        ++m_FrameRuntimeValidation.MissingConsumerWaitCount;
        RecordRuntimeInvariantFailure(
            "render_graph_cross_queue_consumer_wait",
            "A cross-queue resource transfer is not covered by the required queue wait chain.",
            {
                { "pass", NarrowName(pass.GetPassName()) },
                { "resource_id", static_cast<uint64_t>(resourceId) },
                { "resource_name", NarrowName(ResourceIds::GetResourceName(resourceId)) },
                { "producer_queue", std::string(GetQueueName(producerQueue)) },
                { "consumer_queue", std::string(GetQueueName(consumerQueue)) },
                { "producer_fence", producerFence },
                { "declared_dependency_fence", declaredDependencyFence },
                { "direct_preamble_fence", directPreambleFence },
                { "through_direct_preamble", throughDirectPreamble },
            });
    }

    void RenderGraphQueueScheduler::RecordRuntimeInvariantFailure(
        std::string name,
        std::string message,
        std::vector<DiagnosticTelemetryField> fields)
    {
        fields.insert(fields.begin(), { "message", std::move(message) });
        fields.insert(fields.begin(), { "result", std::string("fail") });
        EmitTelemetry({
            .Category = "assertion",
            .Name = std::move(name),
            .Severity = DiagnosticTelemetrySeverity::Error,
            .FrameIndex = m_CurrentFrameIndex,
            .Fields = std::move(fields),
        });
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

    RenderGraphQueueFenceValues RenderGraphQueueScheduler::GetResourceRetirement(
        const ResourceId resourceId) const
    {
        const auto retirement = m_ResourceRetirements.find(resourceId);
        return retirement != m_ResourceRetirements.end()
            ? retirement->second
            : RenderGraphQueueFenceValues{};
    }

    const RenderGraphQueueFenceValues& RenderGraphQueueScheduler::GetFrameSubmissionFences() const
    {
        return m_FrameSubmissionFences;
    }

    const RenderGraphQueueSynchronizationStats&
    RenderGraphQueueScheduler::GetFrameSynchronizationStats() const
    {
        return m_FrameSynchronizationStats;
    }

    const RenderGraphQueueRuntimeValidation&
    RenderGraphQueueScheduler::GetFrameRuntimeValidation() const
    {
        return m_FrameRuntimeValidation;
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
