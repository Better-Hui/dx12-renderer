//Modify Begin:2026-10-01 by Hui
#include "RenderGraphCommandExecutor.h"

#include "RenderGraphProfiler.h"
#include "RenderGraphQueueScheduler.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/DiagnosticRenderScope.h>
#include <DX12Library/DiagnosticTelemetry.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/PerformanceScope.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Resource.h>
#include <DX12Library/Texture.h>

#include <algorithm>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace
{
    uint64_t MakeBatchCorrelationId(const uint64_t frameIndex, const uint64_t batchIndex) noexcept
    {
        uint64_t hash = 14695981039346656037ull;
        hash ^= frameIndex;
        hash *= 1099511628211ull;
        hash ^= batchIndex;
        return hash * 1099511628211ull;
    }

    const char* GetDiagnosticQueueName(const RenderGraph::RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderGraph::RenderPassQueue::Direct: return "Direct";
        case RenderGraph::RenderPassQueue::AsyncCompute: return "AsyncCompute";
        case RenderGraph::RenderPassQueue::Copy: return "Copy";
        default: return "Unknown";
        }
    }

    std::string NarrowDiagnosticName(const std::wstring& value)
    {
        std::string result;
        result.reserve(value.size());
        for (const wchar_t character : value)
        {
            result.push_back(character >= 0 && character < 128 ? static_cast<char>(character) : '?');
        }
        return result;
    }

    DX12Diagnostics::DiagnosticResourceAccess GetDiagnosticAccess(const RenderGraph::InputType type)
    {
        using namespace RenderGraph;
        switch (type)
        {
        case InputType::ShaderResource:
        case InputType::NonPixelShaderResource:
        case InputType::UnorderedAccess:
        case InputType::CopySource:
        case InputType::IndirectArgument:
            return DX12Diagnostics::DiagnosticResourceAccess::Read;
        default:
            return DX12Diagnostics::DiagnosticResourceAccess::None;
        }
    }

    DX12Diagnostics::DiagnosticResourceAccess GetDiagnosticAccess(const RenderGraph::OutputType type)
    {
        using namespace RenderGraph;
        switch (type)
        {
        case OutputType::DepthRead:
            return DX12Diagnostics::DiagnosticResourceAccess::Read;
        case OutputType::RenderTarget:
        case OutputType::DepthWrite:
        case OutputType::UnorderedAccess:
        case OutputType::CopyDestination:
            return DX12Diagnostics::DiagnosticResourceAccess::Write;
        default:
            return DX12Diagnostics::DiagnosticResourceAccess::None;
        }
    }

    uint64_t GetShaderAccessValidationSignature(
        const DX12Diagnostics::DiagnosticRenderPassScope& scope) noexcept
    {
        uint64_t hash = 14695981039346656037ull;
        const auto combine = [&hash](const uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        const DX12Diagnostics::DiagnosticRenderPassScopeDesc& scopeDesc = scope.GetDesc();
        combine(scopeDesc.CorrelationId);
        for (const DX12Diagnostics::DiagnosticDeclaredResource& resource : scopeDesc.DeclaredResources)
        {
            combine(resource.LogicalResourceId);
            combine(static_cast<uint64_t>(resource.Access));
        }
        combine(scope.GetObservedAccessCount());
        combine(scope.GetMatchedAccessCount());
        return hash;
    }

    bool ShouldEmitShaderAccessValidation(const DX12Diagnostics::DiagnosticRenderPassScope& scope)
    {
        static std::mutex mutex;
        static std::map<uint64_t, std::set<uint64_t>> signatures;
        const DX12Diagnostics::DiagnosticRenderPassScopeDesc& scopeDesc = scope.GetDesc();
        const uint64_t signature = GetShaderAccessValidationSignature(scope);
        std::lock_guard lock(mutex);
        return signatures[scopeDesc.CorrelationId].insert(signature).second;
    }

    bool HasCrossQueueExternalDependencies(const RenderGraph::PassResourceStatePlan& statePlan)
    {
        return std::ranges::any_of(
            statePlan.ExternalResourceTransitions,
            [](const RenderGraph::PassExternalResourceTransition& transition)
            {
                return !transition.DirectOnly;
            });
    }
}

RenderGraph::RenderGraphCommandExecutor::RenderGraphCommandExecutor(
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
    std::shared_ptr<CommandQueue> copyCommandQueue,
    std::shared_ptr<ResourcePool> resourcePool,
    RenderGraphQueueScheduler& queueScheduler,
    RenderGraphProfiler& profiler)
    : m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
    , m_CopyCommandQueue(std::move(copyCommandQueue))
    , m_ResourcePool(std::move(resourcePool))
    , m_QueueScheduler(queueScheduler)
    , m_Profiler(profiler)
{
    Assert(m_DirectCommandQueue != nullptr, "Render graph executor requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph executor requires an async compute command queue.");
    Assert(m_CopyCommandQueue != nullptr, "Render graph executor requires a copy command queue.");
    Assert(m_ResourcePool != nullptr, "Render graph executor requires a resource pool.");
}

void RenderGraph::RenderGraphCommandExecutor::SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept
{
    m_DiagnosticTelemetrySink = sink;
}

void RenderGraph::RenderGraphCommandExecutor::EmitTelemetry(DiagnosticTelemetryEvent event) const noexcept
{
    if (m_DiagnosticTelemetrySink != nullptr)
    {
        m_DiagnosticTelemetrySink->RecordTelemetry(std::move(event));
    }
}

uint64_t RenderGraph::RenderGraphCommandExecutor::GetPassCorrelationId(const RenderPass& pass) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    for (const wchar_t character : pass.GetPassName())
    {
        hash ^= static_cast<uint64_t>(character);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<uint64_t>(pass.GetQueue());
    return hash * 1099511628211ull;
}

std::unique_ptr<DX12Diagnostics::DiagnosticRenderPassScope>
RenderGraph::RenderGraphCommandExecutor::CreateDiagnosticRenderPassScope(
    const RenderPass& pass,
    const uint64_t frameIndex) const
{
    if (!HasDiagnosticTelemetrySink())
    {
        return nullptr;
    }

    DX12Diagnostics::DiagnosticRenderPassScopeDesc desc = {};
    desc.CorrelationId = GetPassCorrelationId(pass);
    desc.FrameIndex = frameIndex;
    desc.PassName = NarrowDiagnosticName(pass.GetPassName());
    desc.QueueName = GetDiagnosticQueueName(pass.GetQueue());
    std::unordered_map<ID3D12Resource*, size_t> declaredResourceIndices;
    declaredResourceIndices.reserve(pass.GetExternalResourceAccesses().size());
    const auto addResource = [&desc, &declaredResourceIndices](
        const Resource& resource,
        const ResourceId resourceId,
        std::string resourceName,
        const DX12Diagnostics::DiagnosticResourceAccess access)
    {
        if (access == DX12Diagnostics::DiagnosticResourceAccess::None)
        {
            return;
        }
        resource.ForEachResourceRecursive([&desc, &declaredResourceIndices, resourceId, &resourceName, access](const Resource& nestedResource)
        {
            ID3D12Resource* resourceIdentity = nestedResource.GetD3D12Resource().Get();
            if (resourceIdentity == nullptr)
            {
                return;
            }
            const auto [existing, inserted] = declaredResourceIndices.try_emplace(
                resourceIdentity, desc.DeclaredResources.size());
            if (!inserted)
            {
                auto& declared = desc.DeclaredResources[existing->second];
                declared.Access = declared.Access | access;
                return;
            }
            desc.DeclaredResources.push_back({
                .ResourceIdentity = resourceIdentity,
                .LogicalResourceId = resourceId,
                .LogicalResourceName = resourceName,
                .Access = access,
            });
        });
    };

#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    const bool measureDiagnosticComponents = DX12Diagnostics::ActiveRecordingScope.Sink != nullptr;
    const auto diagnosticInputsStart = measureDiagnosticComponents ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
#endif
    for (const Input& input : pass.GetInputs())
    {
        const DX12Diagnostics::DiagnosticResourceAccess access = GetDiagnosticAccess(input.m_Type);
        if (access != DX12Diagnostics::DiagnosticResourceAccess::None &&
            m_ResourcePool->IsRegistered(input.m_Id))
        {
            addResource(
                m_ResourcePool->GetResource(input.m_Id),
                input.m_Id,
                NarrowDiagnosticName(ResourceIds::GetResourceName(input.m_Id)),
                access);
        }
    }
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    const auto diagnosticInputsDuration = measureDiagnosticComponents ?
        std::chrono::steady_clock::now() - diagnosticInputsStart : std::chrono::steady_clock::duration{};
    const auto diagnosticOutputsStart = measureDiagnosticComponents ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
#endif
    for (const Output& output : pass.GetOutputs())
    {
        const DX12Diagnostics::DiagnosticResourceAccess access = GetDiagnosticAccess(output.m_Type);
        if (access != DX12Diagnostics::DiagnosticResourceAccess::None &&
            m_ResourcePool->IsRegistered(output.m_Id))
        {
            addResource(
                m_ResourcePool->GetResource(output.m_Id),
                output.m_Id,
                NarrowDiagnosticName(ResourceIds::GetResourceName(output.m_Id)),
                access);
        }
    }
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    const auto diagnosticOutputsDuration = measureDiagnosticComponents ?
        std::chrono::steady_clock::now() - diagnosticOutputsStart : std::chrono::steady_clock::duration{};
    const auto diagnosticExternalStart = measureDiagnosticComponents ? std::chrono::steady_clock::now() :
        std::chrono::steady_clock::time_point{};
#endif
    for (const ExternalResourceAccess& access : pass.GetExternalResourceAccesses())
    {
        addResource(
            access.Resolve(),
            access.Id,
            NarrowDiagnosticName(ResourceIds::GetResourceName(access.Id)),
            access.Mode == ExternalResourceAccessMode::Read
                ? DX12Diagnostics::DiagnosticResourceAccess::Read
                : DX12Diagnostics::DiagnosticResourceAccess::Write);
    }
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    if (measureDiagnosticComponents)
    {
        if (!pass.GetInputs().empty())
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage("rg.diagnostic_inputs", diagnosticInputsDuration);
        }
        if (!pass.GetOutputs().empty())
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage("rg.diagnostic_outputs", diagnosticOutputsDuration);
        }
        if (!pass.GetExternalResourceAccesses().empty())
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage(
                "rg.diagnostic_external", std::chrono::steady_clock::now() - diagnosticExternalStart);
        }
    }
#endif
    return std::make_unique<DX12Diagnostics::DiagnosticRenderPassScope>(std::move(desc));
}

void RenderGraph::RenderGraphCommandExecutor::EmitShaderAccessValidation(
    const DX12Diagnostics::DiagnosticRenderPassScope& scope) const noexcept
{
    if (!HasDiagnosticTelemetrySink())
    {
        return;
    }
    const DX12Diagnostics::DiagnosticRenderPassScopeDesc& scopeDesc = scope.GetDesc();
    const bool valid = scope.GetInvalidAccessCount() == 0u;
    if (valid && !ShouldEmitShaderAccessValidation(scope))
    {
        return;
    }
    EmitTelemetry({
        .Category = "assertion",
        .Name = "render_graph_shader_access_declaration",
        .Severity = valid ? DiagnosticTelemetrySeverity::Info : DiagnosticTelemetrySeverity::Error,
        .FrameIndex = scopeDesc.FrameIndex,
        .CorrelationId = scopeDesc.CorrelationId,
        .Fields = {
            { "result", std::string(valid ? "pass" : "fail") },
            { "message", std::string("All tracked shader descriptor resources are declared by the active RenderGraph pass.") },
            { "pass", scopeDesc.PassName },
            { "queue", scopeDesc.QueueName },
            { "declared_resource_count", static_cast<uint64_t>(scopeDesc.DeclaredResources.size()) },
            { "observed_access_count", scope.GetObservedAccessCount() },
            { "matched_access_count", scope.GetMatchedAccessCount() },
            { "invalid_access_count", scope.GetInvalidAccessCount() },
        },
    });
}

void RenderGraph::RenderGraphCommandExecutor::Execute(
    const RenderMetadata& renderMetadata,
    const CompiledRenderGraph& compiledGraph,
    const bool debugSerializeAsyncCompute,
    const bool enableParallelRecording)
{
    DX12_CPU_PERFORMANCE_SCOPE(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        "RenderGraph.Execute",
        "CPU",
        0u,
        "render_graph_execute");
    m_DirectCommandQueue->SetDiagnosticFrameIndex(renderMetadata.m_FrameIndex);
    m_AsyncComputeCommandQueue->SetDiagnosticFrameIndex(renderMetadata.m_FrameIndex);
    m_CopyCommandQueue->SetDiagnosticFrameIndex(renderMetadata.m_FrameIndex);
    const std::vector<RenderPass*>& renderPasses = compiledGraph.GetRenderPasses();
    const std::vector<RenderGraphRecordingBatch>& recordingBatches = compiledGraph.GetRecordingBatches();
    const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets = compiledGraph.GetRenderTargets();
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans = compiledGraph.GetResourceStatePlans();
    m_QueueScheduler.BeginFrame(renderMetadata.m_FrameIndex);
    if (HasDiagnosticTelemetrySink())
    {
        EmitTelemetry({
            .Category = "render_graph.frame",
            .Name = "begin",
            .FrameIndex = renderMetadata.m_FrameIndex,
            .Fields = {
                { "pass_count", static_cast<uint64_t>(renderPasses.size()) },
                { "batch_count", static_cast<uint64_t>(recordingBatches.size()) },
                { "parallel_recording", enableParallelRecording },
                { "parallel_direct_recording", enableParallelRecording },
            },
        });
    }

    auto directCommandList = m_DirectCommandQueue->GetCommandList();

    const RenderPass* lastAsyncComputePass = nullptr;
    const RenderPass* lastCopyPass = nullptr;
    for (const RenderPass* renderPass : renderPasses)
    {
        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            lastAsyncComputePass = renderPass;
        }
        else if (renderPass->GetQueue() == RenderPassQueue::Copy)
        {
            lastCopyPass = renderPass;
        }
    }

    PIXScopeCPU(L"Render Graph: Execute");
    m_Profiler.BeginQueueFrame(RenderPassQueue::Direct, renderMetadata.m_FrameIndex, *directCommandList);

    FrameContext context(m_ResourcePool, renderMetadata);

    m_ResourcePool->BeginFrame(*directCommandList);

    //Modify Begin:2026-10-02 by Hui
    // Recording is allowed to overlap across queue batches. Submission and
    // resource tracking remain serialized in RenderGraph order below.
    std::vector<PendingParallelBatch> pendingParallelBatches;
    const auto flushPendingParallelBatches = [&]()
    {
        for (PendingParallelBatch& pendingBatch : pendingParallelBatches)
        {
            ExecutePendingParallelBatch(
                pendingBatch,
                renderMetadata,
                debugSerializeAsyncCompute,
                directCommandList,
                resourceStatePlans);
        }
        pendingParallelBatches.clear();
    };
    //Modify End

    uint64_t batchIndex = 0;
    for (const RenderGraphRecordingBatch& recordingBatch : recordingBatches)
    {
        const uint64_t currentBatchIndex = batchIndex++;
        Assert(!recordingBatch.Passes.empty(), "Render-graph recording batch cannot be empty.");
        Assert(recordingBatch.Passes.front()->GetQueue() == recordingBatch.Queue,
            "Render-graph recording batch queue does not match its passes.");
        //Modify Begin:2026-10-02 by Hui
        const bool parallelRecordingRequested =
            enableParallelRecording && recordingBatch.RecordInParallel;
        const bool useParallelDirectRecording =
            parallelRecordingRequested && recordingBatch.Queue == RenderPassQueue::Direct;
        const bool useParallelNonDirectRecording =
            parallelRecordingRequested && recordingBatch.Queue != RenderPassQueue::Direct;
        //Modify End
        if (HasDiagnosticTelemetrySink())
        {
            const uint64_t batchCorrelationId = MakeBatchCorrelationId(
                renderMetadata.m_FrameIndex,
                currentBatchIndex);
            const bool batchQueueValid = std::ranges::all_of(
                recordingBatch.Passes,
                [&recordingBatch](const RenderPass* pass)
                {
                    return pass != nullptr && pass->GetQueue() == recordingBatch.Queue;
                });
            if (!batchQueueValid)
            {
                EmitTelemetry({
                    .Category = "assertion",
                    .Name = "render_graph_batch_queue_homogeneous",
                    .Severity = DiagnosticTelemetrySeverity::Error,
                    .FrameIndex = renderMetadata.m_FrameIndex,
                    .CorrelationId = batchCorrelationId,
                    .Fields = {
                        { "result", std::string("fail") },
                        { "message", std::string("Recording batch contains exactly one queue type.") },
                        { "batch_index", currentBatchIndex },
                        { "pass_count", static_cast<uint64_t>(recordingBatch.Passes.size()) },
                    },
                });
            }
            EmitTelemetry({
                .Category = "render_graph.batch",
                .Name = "record",
                .FrameIndex = renderMetadata.m_FrameIndex,
                .CorrelationId = batchCorrelationId,
                .Fields = {
                    { "batch_index", currentBatchIndex },
                    { "queue", std::string(recordingBatch.Queue == RenderPassQueue::Direct
                        ? "Direct"
                        : recordingBatch.Queue == RenderPassQueue::AsyncCompute ? "AsyncCompute" : "Copy") },
                    { "pass_count", static_cast<uint64_t>(recordingBatch.Passes.size()) },
                    { "parallel_requested", parallelRecordingRequested },
                    { "parallel", useParallelDirectRecording || useParallelNonDirectRecording },
                    { "parallel_task_count", (useParallelDirectRecording || useParallelNonDirectRecording)
                        ? static_cast<uint64_t>(recordingBatch.Passes.size())
                        : uint64_t{ 0 } },
                    { "parallel_worker_count", (useParallelDirectRecording || useParallelNonDirectRecording)
                        ? static_cast<uint64_t>(m_ParallelRecordingTaskScheduler.GetWorkerCount())
                        : uint64_t{ 0 } },
                },
            });
        }

        if (useParallelDirectRecording || useParallelNonDirectRecording)
        {
            pendingParallelBatches.push_back(EnqueueParallelBatch(
                recordingBatch,
                renderMetadata,
                recordingBatch.Queue == RenderPassQueue::AsyncCompute
                    ? lastAsyncComputePass
                    : recordingBatch.Queue == RenderPassQueue::Copy ? lastCopyPass : nullptr,
                renderTargets,
                resourceStatePlans));
            continue;
        }

        // An ineligible or external pass is a recording barrier. Finish all
        // earlier worker recordings before touching its shared execution state.
        flushPendingParallelBatches();

        if (recordingBatch.Queue != RenderPassQueue::Direct)
        {
            ExecuteNonDirectBatch(
                recordingBatch,
                renderMetadata,
                recordingBatch.Queue == RenderPassQueue::AsyncCompute
                    ? lastAsyncComputePass
                    : lastCopyPass,
                debugSerializeAsyncCompute,
                directCommandList,
                renderTargets,
                resourceStatePlans);
            continue;
        }

        for (RenderPass* renderPass : recordingBatch.Passes)
        {
            Assert(renderPass != nullptr, "Direct recording batch contains a null pass.");
            Assert(renderPass->GetQueue() == RenderPassQueue::Direct,
                "Direct recording batch contains a non-direct pass.");
            PrepareDirectQueueDependencies(
                std::span<RenderPass* const>(&renderPass, 1u),
                directCommandList,
                resourceStatePlans);
            const auto statePlan = resourceStatePlans.find(renderPass);
            Assert(statePlan != resourceStatePlans.end(),
                "Direct render pass has no resource state plan.");
            // A command-list boundary is required only when the alias barrier
            // has a real predecessor recorded earlier in the graph.  A
            // first-use alias barrier (HasBefore == false) can stay in the
            // current list; submitting before every first-use transition
            // needlessly turns one direct frame into many queue submissions.
            //Modify Begin:2026-10-02 by Hui
            const bool requiresAliasingSubmissionBoundary = std::ranges::any_of(
                statePlan->second.AliasingOutputs,
                [](const PassAliasingTransition& transition)
                {
                    return transition.HasBefore;
                });
            if (requiresAliasingSubmissionBoundary)
            {
                // A pending alias barrier is emitted before its command list.
                // Keep a prior pass that may own the same heap on an earlier
                // list, so the barrier cannot move ahead of that pass.
                m_QueueScheduler.SubmitDirect(directCommandList);
            }
            //Modify End
            if (directCommandList == nullptr)
            {
                directCommandList = m_DirectCommandQueue->GetCommandList();
            }

            CommandList& commandList = *directCommandList;
            context.SetRenderTargetInfo({});
            DX12_CPU_PERFORMANCE_SCOPE(
                m_DiagnosticTelemetrySink,
                renderMetadata.m_FrameIndex,
                RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()),
                GetDiagnosticQueueName(RenderPassQueue::Direct),
                GetPassCorrelationId(*renderPass),
                renderPass->IsExternal() ? "render_graph_external_pass" : "render_graph_pass");
            DX12_CPU_RECORDING_PASS(
                m_DiagnosticTelemetrySink, renderMetadata.m_FrameIndex,
                GetPassCorrelationId(*renderPass), GetDiagnosticQueueName(RenderPassQueue::Direct));
            if (renderPass->IsExternal())
            {
                {
                    PIXScope(commandList, renderPass->GetPassName().c_str());
                    RenderPassContext passContext(commandList);
                    RecordPassBoundaryBarriers(
                        passContext,
                        *renderPass,
                        context,
                        renderMetadata.m_FrameIndex,
                        renderTargets,
                        resourceStatePlans);
                    RecordLocalAliasingBarriers(*renderPass, resourceStatePlans);
                    passContext.Finish();
                    m_Profiler.WriteMarker(
                        RenderPassQueue::Direct,
                        commandList,
                        "BeforeExternal." + RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()));
                }

                m_QueueScheduler.TrackPassResources(*renderPass, 0u, true);
                m_QueueScheduler.SubmitDirect(directCommandList);
                renderPass->ExecuteExternal(context);

                if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
                {
                    directCommandList = m_DirectCommandQueue->GetCommandList();
                    m_Profiler.WriteMarker(
                        RenderPassQueue::Direct,
                        *directCommandList,
                        "AfterExternal." + RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()));
                    m_QueueScheduler.SubmitDirect(directCommandList);
                }
            }
            else
            {
                PIXScope(commandList, renderPass->GetPassName().c_str());
                try
                {
                    RenderPassContext passContext(commandList);
                    {
                        DX12_CPU_RECORDING_SCOPE("rg.boundary");
                        RecordPassBoundaryBarriers(
                            passContext,
                            *renderPass,
                            context,
                            renderMetadata.m_FrameIndex,
                            renderTargets,
                            resourceStatePlans);
                    }
                    RecordLocalAliasingBarriers(*renderPass, resourceStatePlans);
                    std::unique_ptr<DX12Diagnostics::DiagnosticRenderPassScope> diagnosticScope;
                    {
                        DX12_CPU_RECORDING_SCOPE("rg.diagnostic_setup");
                        diagnosticScope = CreateDiagnosticRenderPassScope(*renderPass, renderMetadata.m_FrameIndex);
                    }
                    {
                        DX12_CPU_RECORDING_SCOPE("rg.execute");
                        renderPass->Execute(context, passContext);
                    }
                    {
                        DX12_CPU_RECORDING_SCOPE("rg.finish");
                        passContext.Finish();
                    }
                    if (diagnosticScope != nullptr)
                    {
                        DX12_CPU_RECORDING_SCOPE("rg.validation");
                        EmitShaderAccessValidation(*diagnosticScope);
                    }
                }
                catch (const std::exception& exception)
                {
                    throw std::runtime_error(
                        "RenderGraph direct pass '" +
                        RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()) +
                        "' execution failed: " + exception.what());
                }
                m_Profiler.WritePassTimestamp(RenderPassQueue::Direct, commandList, renderPass->GetPassName());
                m_QueueScheduler.TrackPassResources(
                    *renderPass,
                    0u,
                    HasCrossQueueExternalDependencies(statePlan->second));
            }
        }
    }

    flushPendingParallelBatches();

    if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
    {
        if (directCommandList == nullptr)
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
        }
        m_Profiler.ResolveQueueFrame(RenderPassQueue::Direct, *directCommandList);
        const uint64_t fenceValue = m_QueueScheduler.SubmitDirect(directCommandList);
        m_Profiler.EndQueueFrame(RenderPassQueue::Direct, fenceValue);
    }
    else if (directCommandList != nullptr)
    {
        m_QueueScheduler.SubmitDirect(directCommandList);
    }
    m_QueueScheduler.ValidateFrameResourceRetirements(*m_ResourcePool);
    if (HasDiagnosticTelemetrySink())
    {
        EmitTelemetry({
            .Category = "render_graph.frame",
            .Name = "end",
            .FrameIndex = renderMetadata.m_FrameIndex,
        });
    }
}

//Modify Begin:2026-10-02 by Hui
std::shared_ptr<CommandList> RenderGraph::RenderGraphCommandExecutor::RecordParallelPass(
    RenderPass& renderPass,
    const RenderMetadata& renderMetadata,
    const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    const RenderPassQueue queue = renderPass.GetQueue();
    Assert(queue != RenderPassQueue::Direct || !renderPass.IsExternal(),
        "External passes cannot record in parallel.");
    CommandQueue& commandQueue = queue == RenderPassQueue::Direct
        ? *m_DirectCommandQueue
        : GetCommandQueue(queue);
    auto commandList = commandQueue.GetCommandList();
    FrameContext context(m_ResourcePool, renderMetadata);
    const auto renderTargetIt = renderTargets.find(&renderPass);
    if (renderTargetIt != renderTargets.end())
    {
        context.SetRenderTargetInfo(renderTargetIt->second);
    }

    DX12_CPU_PERFORMANCE_SCOPE(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        "RenderGraph.ParallelTaskExecution",
        GetDiagnosticQueueName(queue),
        GetPassCorrelationId(renderPass),
        "render_graph_parallel");
    DX12_CPU_PERFORMANCE_SCOPE(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        RenderGraphProfiler::NarrowPassName(renderPass.GetPassName()),
        GetDiagnosticQueueName(queue),
        GetPassCorrelationId(renderPass),
        "render_graph_pass_parallel");
    DX12_CPU_RECORDING_PASS(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        GetPassCorrelationId(renderPass),
        GetDiagnosticQueueName(queue));

    try
    {
        RenderPassContext passContext(*commandList);
        {
            DX12_CPU_RECORDING_SCOPE("rg.boundary");
            RecordPassBoundaryBarriers(
                passContext,
                renderPass,
                context,
                renderMetadata.m_FrameIndex,
                renderTargets,
                resourceStatePlans);
        }
        std::unique_ptr<DX12Diagnostics::DiagnosticRenderPassScope> diagnosticScope;
        {
            DX12_CPU_RECORDING_SCOPE("rg.diagnostic_setup");
            diagnosticScope = CreateDiagnosticRenderPassScope(renderPass, renderMetadata.m_FrameIndex);
        }
        PIXScope(*commandList, renderPass.GetPassName().c_str());
        {
            DX12_CPU_RECORDING_SCOPE("rg.execute");
            renderPass.Execute(context, passContext);
        }
        {
            DX12_CPU_RECORDING_SCOPE("rg.finish");
            passContext.Finish();
        }
        if (diagnosticScope != nullptr)
        {
            DX12_CPU_RECORDING_SCOPE("rg.validation");
            EmitShaderAccessValidation(*diagnosticScope);
        }
    }
    catch (const std::exception& exception)
    {
        const char* queueName = queue == RenderPassQueue::Direct
            ? "direct"
            : queue == RenderPassQueue::AsyncCompute ? "async compute" : "copy";
        throw std::runtime_error(
            "RenderGraph parallel " + std::string(queueName) + " pass '" +
            RenderGraphProfiler::NarrowPassName(renderPass.GetPassName()) +
            "' execution failed: " + exception.what());
    }
    return commandList;
}

RenderGraph::RenderGraphCommandExecutor::PendingParallelBatch
RenderGraph::RenderGraphCommandExecutor::EnqueueParallelBatch(
    const RenderGraphRecordingBatch& batch,
    const RenderMetadata& renderMetadata,
    const RenderPass* lastQueuePass,
    const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(!batch.Passes.empty(), "Parallel recording batches require at least one pass.");
    Assert(std::ranges::all_of(
        batch.Passes,
        [&batch](const RenderPass* pass)
        {
            return pass != nullptr && pass->GetQueue() == batch.Queue &&
                !pass->IsExternal() && pass->IsParallelRecordingEligible();
        }),
        "Parallel recording batch contains an ineligible pass.");

    //Modify Begin:2026-10-02 by Hui
    DX12_CPU_PERFORMANCE_SCOPE(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        "RenderGraph.ParallelEnqueue",
        GetDiagnosticQueueName(batch.Queue),
        0u,
        "render_graph_parallel");
    //Modify End
    PendingParallelBatch pendingBatch = {};
    pendingBatch.Batch = &batch;
    pendingBatch.LastQueuePass = lastQueuePass;
    if (batch.Queue != RenderPassQueue::Direct && !m_Profiler.IsQueueFrameActive(batch.Queue))
    {
        pendingBatch.ProfilerCommandList = GetCommandQueue(batch.Queue).GetCommandList();
        m_Profiler.BeginQueueFrame(
            batch.Queue,
            renderMetadata.m_FrameIndex,
            *pendingBatch.ProfilerCommandList);
    }
    pendingBatch.RecordingTasks.reserve(batch.Passes.size());
    for (RenderPass* pass : batch.Passes)
    {
        pendingBatch.RecordingTasks.push_back(m_ParallelRecordingTaskScheduler.Enqueue(
            [this, pass, &renderMetadata, &renderTargets, &resourceStatePlans]()
            {
                return RecordParallelPass(
                    *pass,
                    renderMetadata,
                    renderTargets,
                    resourceStatePlans);
            }));
    }
    return pendingBatch;
}

void RenderGraph::RenderGraphCommandExecutor::ExecutePendingParallelBatch(
    PendingParallelBatch& pendingBatch,
    const RenderMetadata& renderMetadata,
    const bool debugSerializeAsyncCompute,
    std::shared_ptr<CommandList>& directCommandList,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(pendingBatch.Batch != nullptr, "Pending parallel batch has no recording batch.");
    const RenderGraphRecordingBatch& batch = *pendingBatch.Batch;
    if (batch.Queue == RenderPassQueue::Direct)
    {
        PrepareDirectQueueDependencies(batch.Passes, directCommandList, resourceStatePlans);
    }
    else
    {
        PrepareNonDirectBatchDependencies(batch, directCommandList, resourceStatePlans);
    }

    std::vector<std::shared_ptr<CommandList>> commandLists;
    commandLists.reserve(
        pendingBatch.RecordingTasks.size() +
        (directCommandList != nullptr ? 1u : 0u) +
        (pendingBatch.ProfilerCommandList != nullptr ? 1u : 0u));
    if (batch.Queue == RenderPassQueue::Direct)
    {
        if (directCommandList != nullptr)
        {
            commandLists.push_back(std::move(directCommandList));
        }
    }
    else if (pendingBatch.ProfilerCommandList != nullptr)
    {
        commandLists.push_back(std::move(pendingBatch.ProfilerCommandList));
    }

    //Modify Begin:2026-10-02 by Hui
    // Keep the future wait separate from command-list finalization.  A large
    // collect scope alone cannot distinguish worker starvation from main
    // thread bookkeeping, which makes parallel-recording regressions opaque.
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            renderMetadata.m_FrameIndex,
            "RenderGraph.ParallelCollect",
            GetDiagnosticQueueName(batch.Queue),
            0u,
            "render_graph_parallel");
        std::vector<std::shared_ptr<CommandList>> recordedCommandLists;
        recordedCommandLists.reserve(pendingBatch.RecordingTasks.size());
        std::exception_ptr recordingFailure;
        {
            DX12_CPU_PERFORMANCE_SCOPE(
                m_DiagnosticTelemetrySink,
                renderMetadata.m_FrameIndex,
                "RenderGraph.ParallelCollectWait",
                GetDiagnosticQueueName(batch.Queue),
                0u,
                "render_graph_parallel");
            for (auto& recordingTask : pendingBatch.RecordingTasks)
            {
                try
                {
                    recordedCommandLists.push_back(recordingTask.get());
                }
                catch (...)
                {
                    if (recordingFailure == nullptr)
                    {
                        recordingFailure = std::current_exception();
                    }
                }
            }
        }
        if (recordingFailure != nullptr)
        {
            std::rethrow_exception(recordingFailure);
        }

        Assert(recordedCommandLists.size() == batch.Passes.size(),
            "Parallel recording did not return one command list per pass.");

        // future::get() is complete here; this scope isolates timestamp, alias,
        // and vector bookkeeping from the worker wait measured above.
        {
            DX12_CPU_PERFORMANCE_SCOPE(
                m_DiagnosticTelemetrySink,
                renderMetadata.m_FrameIndex,
                "RenderGraph.ParallelCollectFinalize",
                GetDiagnosticQueueName(batch.Queue),
                0u,
                "render_graph_parallel");
            for (uint32_t passOffset = 0u; passOffset < recordedCommandLists.size(); ++passOffset)
            {
                std::shared_ptr<CommandList>& commandList = recordedCommandLists[passOffset];
                Assert(commandList != nullptr, "Parallel recording returned a null command list.");
                m_Profiler.WritePassTimestamp(
                    batch.Queue,
                    *commandList,
                    batch.Passes[passOffset]->GetPassName());
                if (batch.Queue == RenderPassQueue::Direct)
                {
                    RecordLocalAliasingBarriers(*batch.Passes[passOffset], resourceStatePlans);
                }
                commandLists.push_back(std::move(commandList));
            }
        }
        //Modify End
    }

    const bool containsLastQueuePass = pendingBatch.LastQueuePass != nullptr &&
        std::ranges::find(batch.Passes, pendingBatch.LastQueuePass) != batch.Passes.end();
    if (containsLastQueuePass)
    {
        Assert(!commandLists.empty(), "Profiler resolve requires at least one submitted command list.");
        m_Profiler.ResolveQueueFrame(batch.Queue, *commandLists.back());
    }

    std::set<CommandList*> uniqueCommandLists;
    for (const std::shared_ptr<CommandList>& commandList : commandLists)
    {
        Assert(commandList != nullptr, "Render graph submission contains a null command list.");
        Assert(uniqueCommandLists.insert(commandList.get()).second,
            "Render graph submission contains a duplicate command list.");
    }

    //Modify Begin:2026-10-02 by Hui
    DX12_CPU_PERFORMANCE_SCOPE(
        m_DiagnosticTelemetrySink,
        renderMetadata.m_FrameIndex,
        "RenderGraph.ParallelSubmit",
        GetDiagnosticQueueName(batch.Queue),
        0u,
        "render_graph_parallel");
    //Modify End
    const uint64_t fenceValue = batch.Queue == RenderPassQueue::Direct
        ? m_QueueScheduler.SubmitDirect(commandLists)
        : batch.Queue == RenderPassQueue::AsyncCompute
            ? m_QueueScheduler.SubmitAsyncCompute(commandLists, debugSerializeAsyncCompute)
            : m_QueueScheduler.SubmitCopy(commandLists, false);
    for (const RenderPass* pass : batch.Passes)
    {
        const auto statePlan = resourceStatePlans.find(pass);
        Assert(statePlan != resourceStatePlans.end(),
            "Parallel render pass has no resource state plan.");
        m_QueueScheduler.TrackPassResources(
            *pass,
            batch.Queue == RenderPassQueue::Direct ? 0u : fenceValue,
            HasCrossQueueExternalDependencies(statePlan->second));
    }
    if (containsLastQueuePass)
    {
        m_Profiler.EndQueueFrame(batch.Queue, fenceValue);
    }
    // Keep the direct queue's continuation list available for the next batch.
    if (batch.Queue == RenderPassQueue::Direct)
    {
        directCommandList.reset();
    }
}
//Modify End

void RenderGraph::RenderGraphCommandExecutor::PrepareDirectQueueDependencies(
    const std::span<RenderPass* const> passes,
    std::shared_ptr<CommandList>& directCommandList,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(!passes.empty(), "Direct queue dependency preparation requires at least one pass.");

    RenderGraphQueueFenceValues producerFences;
    for (const RenderPass* pass : passes)
    {
        Assert(pass != nullptr, "Direct queue dependency preparation received a null pass.");
        Assert(pass->GetQueue() == RenderPassQueue::Direct, "Only direct passes can enter a parallel recording batch.");
        const auto statePlan = resourceStatePlans.find(pass);
        Assert(statePlan != resourceStatePlans.end(),
            "Direct queue dependency preparation requires a resource state plan.");
        producerFences.Merge(m_QueueScheduler.GetCrossQueueProducerFences(
            *pass,
            statePlan->second,
            RenderPassQueue::Direct));
    }
    if (producerFences.IsEmpty())
    {
        m_QueueScheduler.ValidateDirectPassDependencies(
            passes,
            resourceStatePlans,
            producerFences);
        return;
    }

    if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
    {
        if (directCommandList == nullptr)
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
        }
        m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Queue Wait.Begin");
    }

    m_QueueScheduler.SubmitDirect(directCommandList);
    m_QueueScheduler.WaitForDependencies(RenderPassQueue::Direct, producerFences);
    m_QueueScheduler.ValidateDirectPassDependencies(
        passes,
        resourceStatePlans,
        producerFences);

    if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
    {
        directCommandList = m_DirectCommandQueue->GetCommandList();
        m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Queue Wait.End");
    }
}

void RenderGraph::RenderGraphCommandExecutor::ExecuteNonDirectBatch(
    const RenderGraphRecordingBatch& batch,
    const RenderMetadata& renderMetadata,
    const RenderPass* lastQueuePass,
    const bool debugSerializeAsyncCompute,
    std::shared_ptr<CommandList>& directCommandList,
    const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(!batch.Passes.empty(), "Non-direct recording batch cannot be empty.");
    Assert(batch.Queue != RenderPassQueue::Direct, "Non-direct batch cannot use the direct queue.");
    for (const RenderPass* pass : batch.Passes)
    {
        Assert(pass != nullptr, "Non-direct recording batch contains a null pass.");
        Assert(pass->GetQueue() == batch.Queue, "Non-direct recording batch mixes queue types.");
        Assert(!pass->IsExternal(), "External render passes must use the direct queue.");
    }

    PrepareNonDirectBatchDependencies(batch, directCommandList, resourceStatePlans);

    CommandQueue& commandQueue = GetCommandQueue(batch.Queue);
    auto commandList = commandQueue.GetCommandList();
    if (!m_Profiler.IsQueueFrameActive(batch.Queue))
    {
        m_Profiler.BeginQueueFrame(batch.Queue, renderMetadata.m_FrameIndex, *commandList);
    }

    FrameContext context(m_ResourcePool, renderMetadata);
    for (RenderPass* pass : batch.Passes)
    {
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            renderMetadata.m_FrameIndex,
            RenderGraphProfiler::NarrowPassName(pass->GetPassName()),
            GetDiagnosticQueueName(batch.Queue),
            GetPassCorrelationId(*pass),
            "render_graph_pass");
        DX12_CPU_RECORDING_PASS(
            m_DiagnosticTelemetrySink, renderMetadata.m_FrameIndex,
            GetPassCorrelationId(*pass), GetDiagnosticQueueName(batch.Queue));
        context.SetRenderTargetInfo({});
        try
        {
            RenderPassContext passContext(*commandList);
            {
                DX12_CPU_RECORDING_SCOPE("rg.boundary");
                RecordPassBoundaryBarriers(
                    passContext,
                    *pass,
                    context,
                    renderMetadata.m_FrameIndex,
                    renderTargets,
                    resourceStatePlans);
            }
            std::unique_ptr<DX12Diagnostics::DiagnosticRenderPassScope> diagnosticScope;
            {
                DX12_CPU_RECORDING_SCOPE("rg.diagnostic_setup");
                diagnosticScope = CreateDiagnosticRenderPassScope(*pass, renderMetadata.m_FrameIndex);
            }
            PIXScope(*commandList, pass->GetPassName().c_str());
            {
                DX12_CPU_RECORDING_SCOPE("rg.execute");
                pass->Execute(context, passContext);
            }
            {
                DX12_CPU_RECORDING_SCOPE("rg.finish");
                passContext.Finish();
            }
            if (diagnosticScope != nullptr)
            {
                DX12_CPU_RECORDING_SCOPE("rg.validation");
                EmitShaderAccessValidation(*diagnosticScope);
            }
        }
        catch (const std::exception& exception)
        {
            const char* queueName = batch.Queue == RenderPassQueue::AsyncCompute
                ? "async compute"
                : "copy";
            throw std::runtime_error(
                "RenderGraph " + std::string(queueName) + " pass '" +
                RenderGraphProfiler::NarrowPassName(pass->GetPassName()) +
                "' execution failed: " + exception.what());
        }
        m_Profiler.WritePassTimestamp(batch.Queue, *commandList, pass->GetPassName());
    }

    const bool containsLastQueuePass = lastQueuePass != nullptr &&
        std::ranges::find(batch.Passes, lastQueuePass) != batch.Passes.end();
    if (containsLastQueuePass)
    {
        m_Profiler.ResolveQueueFrame(batch.Queue, *commandList);
    }

    const uint64_t fenceValue = batch.Queue == RenderPassQueue::AsyncCompute
        ? m_QueueScheduler.SubmitAsyncCompute(commandList, debugSerializeAsyncCompute)
        : m_QueueScheduler.SubmitCopy(commandList, false);
    for (const RenderPass* pass : batch.Passes)
    {
        const auto statePlan = resourceStatePlans.find(pass);
        Assert(statePlan != resourceStatePlans.end(), "Non-direct pass has no resource state plan.");
        m_QueueScheduler.TrackPassResources(
            *pass,
            fenceValue,
            HasCrossQueueExternalDependencies(statePlan->second));
    }
    if (containsLastQueuePass)
    {
        m_Profiler.EndQueueFrame(batch.Queue, fenceValue);
    }
}

void RenderGraph::RenderGraphCommandExecutor::PrepareNonDirectBatchDependencies(
    const RenderGraphRecordingBatch& batch,
    std::shared_ptr<CommandList>& directCommandList,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(!batch.Passes.empty(), "Non-direct dependency preparation requires at least one pass.");
    Assert(batch.Queue != RenderPassQueue::Direct, "Non-direct dependency preparation cannot target the direct queue.");

    // Finalize earlier direct producers before querying their fence values.
    // The preamble is a separate direct submission, which also prevents its
    // pending alias barriers from moving ahead of earlier direct pass work.
    m_QueueScheduler.SubmitDirect(directCommandList);

    RenderGraphQueueFenceValues producerFences;
    for (const RenderPass* pass : batch.Passes)
    {
        Assert(pass != nullptr && pass->GetQueue() == batch.Queue,
            "Non-direct dependency preparation received an invalid pass.");
        const auto statePlan = resourceStatePlans.find(pass);
        Assert(statePlan != resourceStatePlans.end(),
            "Non-direct dependency preparation requires a resource state plan.");
        producerFences.Merge(m_QueueScheduler.GetCrossQueueProducerFences(
            *pass,
            statePlan->second,
            RenderPassQueue::Direct));
    }

    if (!producerFences.IsEmpty())
    {
        if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
        {
            if (directCommandList == nullptr)
            {
                directCommandList = m_DirectCommandQueue->GetCommandList();
            }
            m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Preamble Wait.Begin");
        }
        m_QueueScheduler.SubmitDirect(directCommandList);
        m_QueueScheduler.WaitForDependencies(RenderPassQueue::Direct, producerFences);
        if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
            m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Preamble Wait.End");
        }
    }

    if (directCommandList == nullptr)
    {
        directCommandList = m_DirectCommandQueue->GetCommandList();
    }

    CommandList& commandList = *directCommandList;
    for (const RenderPass* pass : batch.Passes)
    {
        ApplyDirectQueuePreamble(*pass, commandList, resourceStatePlans);
    }

    const uint64_t preambleFenceValue = m_QueueScheduler.SubmitDirect(directCommandList);
    m_QueueScheduler.WaitForDirectSubmission(batch.Queue, preambleFenceValue);
    m_QueueScheduler.ValidateNonDirectBatchDependencies(
        batch.Passes,
        batch.Queue,
        resourceStatePlans,
        producerFences,
        preambleFenceValue);
}

void RenderGraph::RenderGraphCommandExecutor::ApplyDirectQueuePreamble(
    const RenderPass& pass,
    CommandList& commandList,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(pass.GetQueue() != RenderPassQueue::Direct,
        "Direct-queue preambles are only generated for non-direct passes.");
    const auto planIt = resourceStatePlans.find(&pass);
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    Assert(planIt->second.DirectPreamble.has_value(), "Non-direct render pass has no direct-queue preamble plan.");
    const PassResourceStatePlan::NonDirectQueuePreamble& directPreamble = *planIt->second.DirectPreamble;
    RenderPassContext passContext(commandList);
    RenderGraphBarrierRecorder recorder(passContext);

    for (const PassResourceTransition& transition : directPreamble.CrossQueueInputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        recorder.Use(
            resource,
            transition.StateAfter,
            transition.Use,
            transition.InsertUavBarrier);
    }

    ApplyExternalResourceTransitions(passContext, directPreamble.ExternalResourceTransitions);

    for (const PassAliasingTransition& transition : directPreamble.AliasingOutputs)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.AfterId);
        recorder.AliasingBeforeFirstUse(resource);
        m_QueueScheduler.RecordAliasingBarrier(transition);
    }

    for (const PassResourceTransition& transition : directPreamble.OutputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        recorder.Use(
            resource,
            transition.StateAfter,
            transition.Use,
            transition.InsertUavBarrier);
    }

    passContext.Finish();

    m_Profiler.WriteMarker(
        RenderPassQueue::Direct,
        commandList,
        "Queue Prepare." + RenderGraphProfiler::NarrowPassName(pass.GetPassName()));
}

void RenderGraph::RenderGraphCommandExecutor::RecordLocalAliasingBarriers(
    const RenderPass& pass,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    const auto statePlan = resourceStatePlans.find(&pass);
    Assert(statePlan != resourceStatePlans.end(),
        "Aliasing barrier recording requires a resource state plan.");
    for (const PassAliasingTransition& transition : statePlan->second.AliasingOutputs)
    {
        m_QueueScheduler.RecordAliasingBarrier(transition);
    }
}

CommandQueue& RenderGraph::RenderGraphCommandExecutor::GetCommandQueue(const RenderPassQueue queue) const
{
    switch (queue)
    {
    case RenderPassQueue::AsyncCompute:
        return *m_AsyncComputeCommandQueue;
    case RenderPassQueue::Copy:
        return *m_CopyCommandQueue;
    case RenderPassQueue::Direct:
    default:
        Assert(false, "Non-direct executor requested the direct command queue.");
        return *m_DirectCommandQueue;
    }
}

void RenderGraph::RenderGraphCommandExecutor::ApplyExternalResourceTransitions(
    RenderPassContext& passContext,
    const std::span<const PassExternalResourceTransition> transitions)
{
    if (transitions.empty())
    {
        return;
    }
    DX12_CPU_RECORDING_SCOPE("rg.external_transitions");
    RenderGraphBarrierRecorder recorder(passContext);
    passContext.GetBarrierContext().ReserveResourceUses(transitions.size());
    const bool externalCommandList = passContext.GetCommandList().IsExternalCommandList();
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    const bool measureComponents = DX12Diagnostics::ActiveRecordingScope.Sink != nullptr;
    std::chrono::steady_clock::duration resolveDuration{};
    std::chrono::steady_clock::duration useDuration{};
    std::chrono::steady_clock::duration attributionDuration{};
    std::chrono::steady_clock::duration stableReadDuration{};
    std::chrono::steady_clock::duration transitionDuration{};
#endif
    std::vector<BarrierContext::StableReadOnlyUse> stableReadBatch;
    stableReadBatch.reserve(transitions.size());
    const auto flushStableReadBatch = [&]()
    {
        if (stableReadBatch.empty())
        {
            return;
        }
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
        const auto stableStart = measureComponents ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        passContext.GetBarrierContext().UseStableReadOnlyBatch(stableReadBatch);
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
        if (measureComponents)
        {
            const auto duration = std::chrono::steady_clock::now() - stableStart;
            stableReadDuration += duration;
            useDuration += duration;
        }
#endif
        stableReadBatch.clear();
    };
    for (const PassExternalResourceTransition& transition : transitions)
    {
        Assert(transition.Access != nullptr,
            "Render pass external resource transition must reference an access declaration.");
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
        const auto resolveStart = measureComponents ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        const Resource& resource = transition.ResolvedResource != nullptr
            ? *transition.ResolvedResource
            : transition.Access->Resolve();
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
        if (measureComponents)
        {
            resolveDuration += std::chrono::steady_clock::now() - resolveStart;
        }
        const auto useStart = measureComponents ? std::chrono::steady_clock::now() :
            std::chrono::steady_clock::time_point{};
#endif
        if (transition.StableReadOnly &&
            !externalCommandList)
        {
            stableReadBatch.push_back({ &resource, transition.StateAfter });
            continue;
        }
        flushStableReadBatch();
        if (transition.AttributionOnly &&
            !externalCommandList)
        {
            passContext.UseAttributionOnly(resource, transition.StateAfter, transition.Use);
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
            if (measureComponents)
            {
                attributionDuration += std::chrono::steady_clock::now() - useStart;
            }
#endif
        }
        else if (transition.Access->StaticResource != nullptr)
        {
            // Static external declarations are already flattened to leaf resources.
            passContext.Use(
                resource,
                transition.StateAfter,
                transition.Use,
                transition.InsertUavBarrier);
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
            if (measureComponents)
            {
                transitionDuration += std::chrono::steady_clock::now() - useStart;
            }
#endif
        }
        else
        {
            recorder.Use(
                resource,
                transition.StateAfter,
                transition.Use,
                transition.InsertUavBarrier);
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
            if (measureComponents)
            {
                transitionDuration += std::chrono::steady_clock::now() - useStart;
            }
#endif
        }
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
        if (measureComponents)
        {
            useDuration += std::chrono::steady_clock::now() - useStart;
        }
#endif
    }
    flushStableReadBatch();
#if DX12_RENDERER_DEBUG_PERFORMANCE_SCOPES
    if (measureComponents)
    {
        DX12Diagnostics::RecordAccumulatedRecordingStage("rg.external_resolve", resolveDuration);
        DX12Diagnostics::RecordAccumulatedRecordingStage("rg.external_use", useDuration);
        if (attributionDuration != std::chrono::steady_clock::duration{})
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage("rg.external_attribution", attributionDuration);
        }
        if (stableReadDuration != std::chrono::steady_clock::duration{})
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage("rg.external_stable_read", stableReadDuration);
        }
        if (transitionDuration != std::chrono::steady_clock::duration{})
        {
            DX12Diagnostics::RecordAccumulatedRecordingStage("rg.external_transition_use", transitionDuration);
        }
    }
#endif
}

void RenderGraph::RenderGraphCommandExecutor::RecordPassBoundaryBarriers(
    RenderPassContext& passContext,
    const RenderPass& renderPass,
    RenderContext& context,
    const uint64_t frameIndex,
    const std::unordered_map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::unordered_map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    CommandList& commandList = passContext.GetCommandList();
    RenderGraphBarrierRecorder recorder(passContext);
    const auto planIt = [&]()
    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.state_plan_lookup",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        return resourceStatePlans.find(&renderPass);
    }();
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    const PassResourceStatePlan& resourceStatePlan = planIt->second;

    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.input_transitions",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        for (const PassResourceTransition& transition : resourceStatePlan.InputTransitions)
        {
            const auto& resource = m_ResourcePool->GetResource(transition.Id);
            recorder.Use(
                resource,
                transition.StateAfter,
                transition.Use,
                transition.InsertUavBarrier);
        }
    }

    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.external_transitions",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        ApplyExternalResourceTransitions(passContext, resourceStatePlan.ExternalResourceTransitions);
    }

    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.aliasing_transitions",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        for (const PassAliasingTransition& transition : resourceStatePlan.AliasingOutputs)
        {
            const auto& resource = m_ResourcePool->GetResource(transition.AfterId);
            recorder.AliasingBeforeFirstUse(resource);
        }
    }

    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.output_transitions",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        for (const PassResourceTransition& transition : resourceStatePlan.OutputTransitions)
        {
            const auto& resource = m_ResourcePool->GetResource(transition.Id);
            recorder.Use(
                resource,
                transition.StateAfter,
                transition.Use,
                transition.InsertUavBarrier);
        }
    }

    const auto renderTargetIt = renderTargets.find(&renderPass);
    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.render_target",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        if (renderTargetIt != renderTargets.end())
        {
            const RenderTargetInfo& renderTargetInfo = renderTargetIt->second;
            context.SetRenderTargetInfo(renderTargetInfo);
            passContext.SetRenderTarget(
                *renderTargetInfo.m_RenderTarget,
                renderTargetInfo.m_ReadonlyDepth);
        }
    }

    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.barrier_flush",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        recorder.Flush();
    }

    // The initialization stage is intentionally kept after the barrier flush;
    // clears/discards must observe the state established above.
    {
        //Modify Begin:2026-10-02 by Hui
        DX12_CPU_PERFORMANCE_SCOPE(
            m_DiagnosticTelemetrySink,
            frameIndex,
            "rg.boundary.initialization",
            GetDiagnosticQueueName(renderPass.GetQueue()),
            GetPassCorrelationId(renderPass),
            "render_graph_boundary");
        //Modify End
        for (const ResourceId outputId : resourceStatePlan.InitOutputs)
        {
            const auto& description = m_ResourcePool->GetDescription(outputId);
            switch (description.GetInitAction())
            {
            case Clear:
                {
                    Assert(description.m_ResourceType == ResourceType::Texture, "Only textures support the clear init action.");
                    const auto renderPassOutput = std::ranges::find_if(
                        renderPass.GetOutputs(),
                        [outputId](const Output& output) { return output.m_Id == outputId; });
                    if (renderPassOutput != renderPass.GetOutputs().end() &&
                        renderPassOutput->m_Type == OutputType::RenderTarget)
                    {
                        commandList.ClearTexture(*m_ResourcePool->GetTexture(outputId), description.GetClearValue());
                    }
                    else if (renderPassOutput != renderPass.GetOutputs().end() &&
                        (renderPassOutput->m_Type == OutputType::DepthRead ||
                            renderPassOutput->m_Type == OutputType::DepthWrite))
                    {
                        const auto& texture = *m_ResourcePool->GetTexture(outputId);
                        const auto clearValue = description.GetClearValue().GetD3D12ClearValue()->DepthStencil;
                        commandList.ClearDepthStencilTexture(
                            texture,
                            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                            clearValue.Depth,
                            clearValue.Stencil);
                    }
                }
                break;
            case CopyDestination:
                break;
            case Discard:
                commandList.DiscardResource(m_ResourcePool->GetResource(outputId));
                break;
            case Preserve:
                break;
            default:
                Assert(false, "Unknown resource init action.");
                break;
            }
        }

        if (renderTargetIt != renderTargets.end())
        {
            commandList.SetAutomaticViewportAndScissorRect(*renderTargetIt->second.m_RenderTarget);
        }
    }
}
//Modify End
