#include "RenderGraphRoot.h"

#include <algorithm>
#include <functional>
//Modify Begin:2026-07-30 by Hui
#include <utility>
//Modify End

#include <d3d12.h>

#include <DX12Library/Buffer.h>
#include <DX12Library/CommandListInternalAccess.h>
#include <DX12Library/DiagnosticTelemetry.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

//Modify Begin:2026-08-21 by Hui
#include <string>

namespace
{
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

    uint64_t GetDiagnosticPassId(const RenderGraph::RenderPass& pass)
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

    uint64_t GetDiagnosticSnapshotItemId(
        const uint64_t snapshotIndex,
        const uint64_t itemIndex,
        const uint64_t itemType)
    {
        uint64_t hash = 14695981039346656037ull;
        hash ^= snapshotIndex;
        hash *= 1099511628211ull;
        hash ^= itemIndex;
        hash *= 1099511628211ull;
        hash ^= itemType;
        return hash * 1099511628211ull;
    }
}
//Modify End

RenderGraph::RenderGraphRoot::RenderGraphRoot(
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<D3D12DeviceContext> deviceContext,
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
    std::shared_ptr<CommandQueue> copyCommandQueue,
//Modify End
    std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
    std::vector<TextureDescription>&& textures,
    std::vector<BufferDescription>&& buffers,
    std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by Hui
    , RenderGraphOutputResources outputs
//Modify End
)
//Modify Begin:2026-08-03 by Hui
    : m_DeviceContext(std::move(deviceContext))
    , m_Device(std::move(device))
    , m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
    , m_CopyCommandQueue(std::move(copyCommandQueue))
//Modify End
//Modify Begin:2026-07-30 by Hui
    , m_QueueScheduler(m_DirectCommandQueue, m_AsyncComputeCommandQueue, m_CopyCommandQueue)
//Modify End
    , m_RenderPassesDescription(std::move(renderPasses))
    , m_TextureDescriptions(std::move(textures))
    , m_BufferDescriptions(std::move(buffers))
    , m_TokenDescriptions(std::move(tokens))
//Modify Begin:2026-07-28 by Hui
    , m_ExternalOutputIds(std::move(outputs.External))
    , m_PresentationResourceId(outputs.Presentation)
//Modify End
//Modify Begin:2026-07-30 by Hui
    , m_ResourcePool(std::make_shared<ResourcePool>(
        m_DeviceContext,
        m_DirectCommandQueue,
        m_AsyncComputeCommandQueue,
        m_CopyCommandQueue))
//Modify End
{
//Modify Begin:2026-07-30 by Hui
    Assert(m_Device != nullptr, "Render graph requires a D3D12 device.");
    Assert(m_DeviceContext != nullptr, "Render graph requires a D3D12 device context.");
    Assert(m_DirectCommandQueue != nullptr, "Render graph requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph requires an async compute command queue.");
    Assert(m_CopyCommandQueue != nullptr, "Render graph requires a copy command queue.");
//Modify End
//Modify Begin:2026-07-28 by Hui
    if (std::ranges::find(m_ExternalOutputIds, m_PresentationResourceId) == m_ExternalOutputIds.end())
    {
        m_ExternalOutputIds.push_back(m_PresentationResourceId);
    }
    if (std::ranges::find(m_ExternalOutputIds, ResourceIds::GRAPH_OUTPUT) == m_ExternalOutputIds.end())
    {
        m_ExternalOutputIds.push_back(ResourceIds::GRAPH_OUTPUT);
    }
//Modify End

//Modify Begin:2026-08-07 by Hui
    m_CommandExecutor = std::make_unique<RenderGraphCommandExecutor>(
        m_DirectCommandQueue,
        m_AsyncComputeCommandQueue,
        m_CopyCommandQueue,
        m_ResourcePool,
        m_QueueScheduler,
        m_Profiler);
        m_Compiler = std::make_unique<RenderGraphCompiler>(
            m_Device,
        m_ResourcePool);
    m_Compiler->ValidateDefinition(
        m_RenderPassesDescription,
        m_TextureDescriptions,
        m_BufferDescriptions,
        m_TokenDescriptions);
//Modify End

    {
        const auto pCommandList = m_DirectCommandQueue->GetCommandList();

        {
            PIXScope(*pCommandList, L"Render Graph: Init");

            for (const auto& pRenderPass : m_RenderPassesDescription)
            {
                pRenderPass->Init(*pCommandList);
            }
        }

        m_DirectCommandQueue->ExecuteCommandList(pCommandList);
    }
}

void RenderGraph::RenderGraphRoot::Execute(const RenderMetadata& renderMetadata)
{
    RebuildIfNecessary(renderMetadata);
    Assert(m_CompiledGraph != nullptr, "Render graph has not been compiled.");
    m_CommandExecutor->Execute(
        renderMetadata,
        *m_CompiledGraph,
        m_DebugSerializeAsyncCompute,
        m_ParallelDirectCommandRecording);
}

void RenderGraph::RenderGraphRoot::Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

//Modify Begin:2026-07-30 by Hui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End

    {
        PIXScope(*pCommandList, L"Render Graph: Prepare Present");

        if (pTexture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            pCommandList->TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        }
        else
        {
            pCommandList->TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }

        CommandListInternalAccess::FlushResourceBarriers(*pCommandList);
    }

//Modify Begin:2026-07-30 by Hui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
    pWindow->Present(*pTexture);
}

//Modify Begin:2026-08-21 by Hui
void RenderGraph::RenderGraphRoot::SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept
{
    m_DiagnosticTelemetrySink = sink;
    m_QueueScheduler.SetDiagnosticTelemetrySink(sink);
    m_CommandExecutor->SetDiagnosticTelemetrySink(sink);
    m_DirectCommandQueue->SetDiagnosticTelemetrySink(sink);
    m_AsyncComputeCommandQueue->SetDiagnosticTelemetrySink(sink);
    m_CopyCommandQueue->SetDiagnosticTelemetrySink(sink);
}
//Modify End

//Modify Begin:2026-08-07 by Hui
void RenderGraph::RenderGraphRoot::PresentWithOverlay(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&)>& drawCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;

    {
        PIXScope(commandList, L"Render Graph: Prepare Display");

        if (pTexture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            commandList.TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        }
        else
        {
            commandList.TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        CommandListInternalAccess::FlushResourceBarriers(commandList);

        const RenderTarget& backBufferRenderTarget = pWindow->GetRenderTarget();
        const std::shared_ptr<Texture>& backBuffer = backBufferRenderTarget.GetTexture(Color0);
        if (pTexture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            commandList.ResolveSubresource(*backBuffer, *pTexture);
        }
        else
        {
            commandList.CopyResource(*backBuffer, *pTexture);
        }

        if (drawCallback)
        {
            commandList.SetRenderTarget(backBufferRenderTarget);
            commandList.SetAutomaticViewportAndScissorRect(backBufferRenderTarget);
            drawCallback(commandList);
        }
    }

    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
    pWindow->Present();
}

void RenderGraph::RenderGraphRoot::PresentWithExternalFrameProcessor(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId displayResourceId,
    ExternalFrameProcessor& processor,
    const std::function<void(CommandList&)>& overlayCallback)
{
    const auto& displayTexture = m_ResourcePool->GetTexture(displayResourceId);
    const std::span<const ResourceId> processorResourceIds = processor.GetRequiredResourceIds();
    auto commandList = m_DirectCommandQueue->GetCommandList();

    {
        PIXScope(*commandList, L"Render Graph: Prepare External Frame Processor");

        commandList->TransitionBarrier(*displayTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (const ResourceId resourceId : processorResourceIds)
        {
            Assert(resourceId != displayResourceId, "Display resource must not be duplicated in external processor resources.");
            commandList->TransitionBarrier(m_ResourcePool->GetResource(resourceId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        CommandListInternalAccess::FlushResourceBarriers(*commandList);

        const RenderTarget& backBufferRenderTarget = pWindow->GetRenderTarget();
        const std::shared_ptr<Texture>& backBuffer = backBufferRenderTarget.GetTexture(Color0);
        commandList->CopyResource(*backBuffer, *displayTexture);

        commandList->TransitionBarrier(*displayTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CommandListInternalAccess::FlushResourceBarriers(*commandList);

        processor.Process(*commandList, displayTexture);
        if (overlayCallback)
        {
            commandList->SetRenderTarget(backBufferRenderTarget);
            commandList->SetAutomaticViewportAndScissorRect(backBufferRenderTarget);
            overlayCallback(*commandList);
        }
    }

    m_QueueScheduler.TrackExternalResource(displayResourceId, RenderPassQueue::Direct);
    for (const ResourceId resourceId : processorResourceIds)
    {
        m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    }
    m_QueueScheduler.SubmitDirect(commandList);

    processor.BeforePresent();
    pWindow->Present();
    processor.AfterPresent();
}

void RenderGraph::RenderGraphRoot::PresentWithOverlayBlit(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
    const std::function<void(CommandList&)>& overlayCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;

    {
        PIXScope(commandList, L"Render Graph: Prepare Display Blit");

        commandList.TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        CommandListInternalAccess::FlushResourceBarriers(commandList);

        const RenderTarget& backBufferRenderTarget = pWindow->GetRenderTarget();
        commandList.SetRenderTarget(backBufferRenderTarget);
        commandList.SetAutomaticViewportAndScissorRect(backBufferRenderTarget);

        if (blitCallback)
        {
            blitCallback(commandList, pTexture);
        }
        if (overlayCallback)
        {
            overlayCallback(commandList);
        }
    }

    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
    pWindow->Present();
}

void RenderGraph::RenderGraphRoot::TransitionTexture(
    const RenderMetadata& renderMetadata,
    const ResourceId resourceId,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool waitForCompletion)
{
    RebuildIfNecessary(renderMetadata);

    auto pCommandList = m_DirectCommandQueue->GetCommandList();
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    pCommandList->TransitionBarrier(*pTexture, stateAfter);
    CommandListInternalAccess::FlushResourceBarriers(*pCommandList);

    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    const uint64_t fenceValue = m_QueueScheduler.SubmitDirect(pCommandList);
    if (waitForCompletion)
    {
        m_DirectCommandQueue->WaitForFenceValue(fenceValue);
    }
}

void RenderGraph::RenderGraphRoot::CopyTexture(
    const RenderMetadata& renderMetadata,
    const ResourceId sourceId,
    const ResourceId destinationId,
    const bool waitForCompletion)
{
    RebuildIfNecessary(renderMetadata);

    auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;
    const auto& source = m_ResourcePool->GetTexture(sourceId);
    const auto& destination = m_ResourcePool->GetTexture(destinationId);

    commandList.TransitionBarrier(*source, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList.TransitionBarrier(*destination, D3D12_RESOURCE_STATE_COPY_DEST);
    CommandListInternalAccess::FlushResourceBarriers(commandList);

    commandList.CopyResource(*destination, *source);

    m_QueueScheduler.TrackExternalResource(sourceId, RenderPassQueue::Direct);
    m_QueueScheduler.TrackExternalResource(destinationId, RenderPassQueue::Direct);
    const uint64_t fenceValue = m_QueueScheduler.SubmitDirect(pCommandList);
    if (waitForCompletion)
    {
        m_DirectCommandQueue->WaitForFenceValue(fenceValue);
    }
}

void RenderGraph::RenderGraphRoot::DrawToTexture(
    const RenderMetadata& renderMetadata,
    const ResourceId resourceId,
    const std::function<void(CommandList&)>& drawCallback)
{
    RebuildIfNecessary(renderMetadata);

    auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    commandList.TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
    CommandListInternalAccess::FlushResourceBarriers(commandList);

    RenderTarget renderTarget;
    renderTarget.AttachTexture(Color0, pTexture);
    commandList.SetRenderTarget(renderTarget);
    commandList.SetAutomaticViewportAndScissorRect(renderTarget);

    drawCallback(commandList);

    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
}
//Modify End

void RenderGraph::RenderGraphRoot::DrawToGraphOutput(const RenderMetadata& renderMetadata, const std::function<void(CommandList&)>& drawCallback)
{
//Modify Begin:2026-07-28 by Hui
    DrawToTexture(renderMetadata, ResourceIds::GRAPH_OUTPUT, drawCallback);
//Modify End
}

//Modify Begin:2026-07-27 by Hui
const std::shared_ptr<Texture>& RenderGraph::RenderGraphRoot::GetTexture(const ResourceId resourceId) const
{
    return m_ResourcePool->GetTexture(resourceId);
}
//Modify End

//Modify Begin:2026-08-10 by Hui
const RenderGraph::RenderGraphQueueFenceValues& RenderGraph::RenderGraphRoot::GetFrameSubmissionFences() const
{
    return m_QueueScheduler.GetFrameSubmissionFences();
}
//Modify End

void RenderGraph::RenderGraphRoot::MarkDirty()
{
    m_Dirty = true;
}

//Modify Begin:2026-08-21 by Hui
void RenderGraph::RenderGraphRoot::EmitCompiledGraphSnapshot(const RenderMetadata& renderMetadata) noexcept
{
    if (m_DiagnosticTelemetrySink == nullptr || m_CompiledGraph == nullptr)
    {
        return;
    }
    try
    {
        const uint64_t snapshotIndex = ++m_DiagnosticSnapshotIndex;
        const auto emit = [this](DiagnosticTelemetryEvent event)
        {
            m_DiagnosticTelemetrySink->RecordTelemetry(std::move(event));
        };
        emit({
            .Category = "render_graph.compile",
            .Name = "snapshot",
            .FrameIndex = renderMetadata.m_FrameIndex,
            .CorrelationId = snapshotIndex,
            .Fields = {
                { "snapshot_index", snapshotIndex },
                { "pass_count", static_cast<uint64_t>(m_CompiledGraph->GetRenderPasses().size()) },
                { "batch_count", static_cast<uint64_t>(m_CompiledGraph->GetRecordingBatches().size()) },
                { "screen_width", static_cast<uint64_t>(renderMetadata.m_ScreenWidth) },
                { "screen_height", static_cast<uint64_t>(renderMetadata.m_ScreenHeight) },
                { "display_width", static_cast<uint64_t>(renderMetadata.m_DisplayWidth) },
                { "display_height", static_cast<uint64_t>(renderMetadata.m_DisplayHeight) },
            },
        });

        uint64_t passIndex = 0;
        for (const RenderPass* pass : m_CompiledGraph->GetRenderPasses())
        {
            if (pass == nullptr)
            {
                continue;
            }
            std::vector<DiagnosticTelemetryField> fields = {
                { "snapshot_index", snapshotIndex },
                { "pass_index", passIndex++ },
                { "queue", std::string(GetDiagnosticQueueName(pass->GetQueue())) },
                { "input_count", static_cast<uint64_t>(pass->GetInputs().size()) },
                { "output_count", static_cast<uint64_t>(pass->GetOutputs().size()) },
                { "external_access_count", static_cast<uint64_t>(pass->GetExternalResourceAccesses().size()) },
                { "external", pass->IsExternal() },
                { "parallel_recording_eligible", pass->IsParallelRecordingEligible() },
            };
            const auto statePlan = m_CompiledGraph->GetResourceStatePlans().find(pass);
            if (statePlan != m_CompiledGraph->GetResourceStatePlans().end())
            {
                const PassResourceStatePlan& plan = statePlan->second;
                fields.push_back({ "state_plan.input_transition_count", static_cast<uint64_t>(plan.InputTransitions.size()) });
                fields.push_back({ "state_plan.output_transition_count", static_cast<uint64_t>(plan.OutputTransitions.size()) });
                fields.push_back({ "state_plan.external_transition_count", static_cast<uint64_t>(plan.ExternalResourceTransitions.size()) });
                fields.push_back({ "state_plan.aliasing_output_count", static_cast<uint64_t>(plan.AliasingOutputs.size()) });
                fields.push_back({ "state_plan.init_output_count", static_cast<uint64_t>(plan.InitOutputs.size()) });
                fields.push_back({ "state_plan.has_direct_preamble", plan.DirectPreamble.has_value() });
                for (size_t transitionIndex = 0; transitionIndex < plan.InputTransitions.size(); ++transitionIndex)
                {
                    const PassResourceTransition& transition = plan.InputTransitions[transitionIndex];
                    const std::string prefix = "state_plan.input." + std::to_string(transitionIndex);
                    fields.push_back({ prefix + ".resource_id", static_cast<uint64_t>(transition.Id) });
                    fields.push_back({ prefix + ".state_after", static_cast<uint64_t>(transition.StateAfter) });
                    fields.push_back({ prefix + ".uav_barrier", transition.InsertUavBarrier });
                }
                for (size_t transitionIndex = 0; transitionIndex < plan.OutputTransitions.size(); ++transitionIndex)
                {
                    const PassResourceTransition& transition = plan.OutputTransitions[transitionIndex];
                    const std::string prefix = "state_plan.output." + std::to_string(transitionIndex);
                    fields.push_back({ prefix + ".resource_id", static_cast<uint64_t>(transition.Id) });
                    fields.push_back({ prefix + ".state_after", static_cast<uint64_t>(transition.StateAfter) });
                    fields.push_back({ prefix + ".uav_barrier", transition.InsertUavBarrier });
                }
                if (plan.DirectPreamble.has_value())
                {
                    fields.push_back({
                        "state_plan.direct_preamble.cross_queue_input_count",
                        static_cast<uint64_t>(plan.DirectPreamble->CrossQueueInputTransitions.size()),
                    });
                    fields.push_back({
                        "state_plan.direct_preamble.output_transition_count",
                        static_cast<uint64_t>(plan.DirectPreamble->OutputTransitions.size()),
                    });
                    fields.push_back({
                        "state_plan.direct_preamble.aliasing_output_count",
                        static_cast<uint64_t>(plan.DirectPreamble->AliasingOutputs.size()),
                    });
                }
            }
            for (size_t inputIndex = 0; inputIndex < pass->GetInputs().size(); ++inputIndex)
            {
                const Input& input = pass->GetInputs()[inputIndex];
                const std::string prefix = "input." + std::to_string(inputIndex);
                fields.push_back({ prefix + ".id", static_cast<uint64_t>(input.m_Id) });
                fields.push_back({ prefix + ".name", NarrowDiagnosticName(ResourceIds::GetResourceName(input.m_Id)) });
                fields.push_back({ prefix + ".usage", static_cast<uint64_t>(input.m_Type) });
            }
            for (size_t outputIndex = 0; outputIndex < pass->GetOutputs().size(); ++outputIndex)
            {
                const Output& output = pass->GetOutputs()[outputIndex];
                const std::string prefix = "output." + std::to_string(outputIndex);
                fields.push_back({ prefix + ".id", static_cast<uint64_t>(output.m_Id) });
                fields.push_back({ prefix + ".name", NarrowDiagnosticName(ResourceIds::GetResourceName(output.m_Id)) });
                fields.push_back({ prefix + ".usage", static_cast<uint64_t>(output.m_Type) });
            }
            emit({
                .Category = "render_graph.pass",
                .Name = NarrowDiagnosticName(pass->GetPassName()),
                .FrameIndex = renderMetadata.m_FrameIndex,
                .CorrelationId = GetDiagnosticPassId(*pass),
                .Fields = std::move(fields),
            });
        }

        uint64_t batchIndex = 0;
        for (const RenderGraphRecordingBatch& batch : m_CompiledGraph->GetRecordingBatches())
        {
            const uint64_t currentBatchIndex = batchIndex++;
            const uint64_t correlationId = GetDiagnosticSnapshotItemId(snapshotIndex, currentBatchIndex, 1u);
            const bool queueHomogeneous = std::ranges::all_of(batch.Passes, [&batch](const RenderPass* pass)
            {
                return pass != nullptr && pass->GetQueue() == batch.Queue;
            });
            std::vector<DiagnosticTelemetryField> fields = {
                { "snapshot_index", snapshotIndex },
                { "batch_index", currentBatchIndex },
                { "queue", std::string(GetDiagnosticQueueName(batch.Queue)) },
                { "parallel", batch.RecordInParallel },
                { "pass_count", static_cast<uint64_t>(batch.Passes.size()) },
            };
            for (size_t index = 0; index < batch.Passes.size(); ++index)
            {
                if (batch.Passes[index] != nullptr)
                {
                    fields.push_back({
                        "pass." + std::to_string(index),
                        NarrowDiagnosticName(batch.Passes[index]->GetPassName()),
                    });
                }
            }
            emit({
                .Category = "assertion",
                .Name = "render_graph_batch_queue_homogeneous",
                .Severity = queueHomogeneous
                    ? DiagnosticTelemetrySeverity::Info
                    : DiagnosticTelemetrySeverity::Error,
                .FrameIndex = renderMetadata.m_FrameIndex,
                .CorrelationId = correlationId,
                .Fields = {
                    { "result", std::string(queueHomogeneous ? "pass" : "fail") },
                    { "message", std::string("Compiled recording batch contains exactly one queue type.") },
                    { "snapshot_index", snapshotIndex },
                    { "batch_index", currentBatchIndex },
                    { "pass_count", static_cast<uint64_t>(batch.Passes.size()) },
                },
            });
            emit({
                .Category = "render_graph.compile.batch",
                .Name = "batch",
                .FrameIndex = renderMetadata.m_FrameIndex,
                .CorrelationId = correlationId,
                .Fields = std::move(fields),
            });
        }

        m_ResourcePool->ForEachResource([&](const ResourceDescription& description)
        {
            const char* typeName = description.m_ResourceType == ResourceType::Texture
                ? "Texture"
                : description.m_ResourceType == ResourceType::Buffer ? "Buffer" : "Token";
            std::vector<DiagnosticTelemetryField> fields = {
                { "snapshot_index", snapshotIndex },
                { "resource_id", static_cast<uint64_t>(description.m_Id) },
                { "resource_name", NarrowDiagnosticName(ResourceIds::GetResourceName(description.m_Id)) },
                { "type", std::string(typeName) },
                { "total_size_bytes", description.m_TotalSize },
                { "alignment", description.m_Alignment },
                { "dedicated", description.m_DedicatedResource },
            };
            if (m_ResourcePool->HasResourceLifecycle(description.m_Id))
            {
                const auto& lifecycle = m_ResourcePool->GetResourceLifecycle(description.m_Id);
                fields.push_back({ "lifecycle.begin_pass_index", static_cast<uint64_t>(lifecycle.m_BeginPassIndex) });
                fields.push_back({ "lifecycle.end_pass_index", static_cast<uint64_t>(lifecycle.m_EndPassIndex) });
                fields.push_back({ "lifecycle.queue_mask", static_cast<uint64_t>(lifecycle.m_QueueMask) });
            }
            if (description.m_ResourceType == ResourceType::Texture)
            {
                fields.push_back({ "width", description.m_DxDesc.Width });
                fields.push_back({ "height", static_cast<uint64_t>(description.m_DxDesc.Height) });
                fields.push_back({ "format", static_cast<uint64_t>(description.m_DxDesc.Format) });
                fields.push_back({ "flags", static_cast<uint64_t>(description.m_DxDesc.Flags) });
                fields.push_back({ "mip_levels", static_cast<uint64_t>(description.m_DxDesc.MipLevels) });
            }
            else if (description.m_ResourceType == ResourceType::Buffer)
            {
                fields.push_back({ "element_count", description.m_ElementsCount });
                fields.push_back({ "stride", static_cast<uint64_t>(description.m_BufferDescription.m_Stride) });
                fields.push_back({ "kind", static_cast<uint64_t>(description.m_BufferDescription.m_Kind) });
                fields.push_back({ "usage", static_cast<uint64_t>(description.m_BufferDescription.m_Usage) });
            }
            emit({
                .Category = "render_graph.resource",
                .Name = NarrowDiagnosticName(ResourceIds::GetResourceName(description.m_Id)),
                .FrameIndex = renderMetadata.m_FrameIndex,
                .CorrelationId = description.m_Id,
                .Fields = std::move(fields),
            });
            return true;
        });
    }
    catch (const std::exception& exception)
    {
        m_DiagnosticTelemetrySink->RecordTelemetry({
            .Category = "render_graph.compile",
            .Name = "snapshot_failure",
            .Severity = DiagnosticTelemetrySeverity::Error,
            .FrameIndex = renderMetadata.m_FrameIndex,
            .Fields = { { "message", std::string(exception.what()) } },
        });
    }
}
//Modify End

void RenderGraph::RenderGraphRoot::RebuildIfNecessary(const RenderMetadata& renderMetadata)
{
    CheckPotentiallyDirtyResources(renderMetadata);

    if (m_Dirty)
    {
        m_CompiledGraph = std::make_unique<CompiledRenderGraph>(m_Compiler->Compile(
            m_RenderPassesDescription,
            m_TextureDescriptions,
            m_BufferDescriptions,
            m_TokenDescriptions,
            m_ExternalOutputIds,
            renderMetadata,
            m_QueueScheduler.GetResourceRetirements()));
//Modify Begin:2026-08-21 by Hui
        EmitCompiledGraphSnapshot(renderMetadata);
//Modify End
        m_Dirty = false;
    }
}

void RenderGraph::RenderGraphRoot::CheckPotentiallyDirtyResources(const RenderMetadata& renderMetadata)
{
    if (m_Dirty)
    {
        return;
    }

    m_ResourcePool->ForEachResource([this, &renderMetadata](const ResourceDescription& resourceDescription)
    {
        // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
        // ReSharper disable once CppIncompleteSwitchStatement
        switch (resourceDescription.m_ResourceType)
        {
        case ResourceType::Texture:
            {
                const auto& pTexture = m_ResourcePool->GetTexture(resourceDescription.m_Id);
                const auto d3d12Desc = pTexture->GetD3D12ResourceDesc();
                if (
                    resourceDescription.m_TextureDescription.m_WidthExpression(renderMetadata) != d3d12Desc.Width ||
                    resourceDescription.m_TextureDescription.m_HeightExpression(renderMetadata) != d3d12Desc.Height
                    )
                {
                    m_Dirty = true;
                    return false;
                }

                break;
            }

        case ResourceType::Buffer:
            {
                const auto& pBuffer = m_ResourcePool->GetBuffer(resourceDescription.m_Id);
                const auto d3d12Desc = pBuffer->GetD3D12ResourceDesc();
                if (resourceDescription.m_BufferDescription.m_SizeExpression(renderMetadata) * resourceDescription.m_BufferDescription.m_Stride != d3d12Desc.Width)
                {
                    m_Dirty = true;
                    return false;
                }

                break;
            }

        // these cannot invalidate the graph
        case ResourceType::Token:
        default:
            break;
        }

        return true;
    });
}
