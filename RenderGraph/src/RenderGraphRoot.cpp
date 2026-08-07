#include "RenderGraphRoot.h"

#include <functional>
//Modify Begin:2026-07-30 by BestHui
#include <utility>
//Modify End

#include <d3d12.h>

#include <DX12Library/Buffer.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

RenderGraph::RenderGraphRoot::RenderGraphRoot(
//Modify Begin:2026-07-30 by BestHui
    std::shared_ptr<D3D12DeviceContext> deviceContext,
    Microsoft::WRL::ComPtr<ID3D12Device2> device,
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
//Modify End
    std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
    std::vector<TextureDescription>&& textures,
    std::vector<BufferDescription>&& buffers,
    std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by BestHui
    , std::vector<ResourceId> externalOutputs
//Modify End
)
//Modify Begin:2026-07-30 by BestHui
    : m_DeviceContext(std::move(deviceContext))
    , m_Device(std::move(device))
    , m_DirectCommandQueue(std::move(directCommandQueue))
//Modify Begin:2026-08-03 by BestHui
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
    , m_QueueScheduler(m_DirectCommandQueue, m_AsyncComputeCommandQueue)
//Modify End
    , m_RenderPassesDescription(std::move(renderPasses))
    , m_TextureDescriptions(std::move(textures))
    , m_BufferDescriptions(std::move(buffers))
    , m_TokenDescriptions(std::move(tokens))
//Modify Begin:2026-07-28 by BestHui
    , m_ExternalOutputIds(std::move(externalOutputs))
//Modify End
//Modify Begin:2026-07-30 by BestHui
    , m_ResourcePool(std::make_shared<ResourcePool>(m_DeviceContext, m_DirectCommandQueue, m_AsyncComputeCommandQueue))
//Modify End
{
//Modify Begin:2026-07-30 by BestHui
    Assert(m_Device != nullptr, "Render graph requires a D3D12 device.");
    Assert(m_DeviceContext != nullptr, "Render graph requires a D3D12 device context.");
    Assert(m_DirectCommandQueue != nullptr, "Render graph requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph requires an async compute command queue.");
//Modify End
//Modify Begin:2026-07-28 by BestHui
    if (std::ranges::find(m_ExternalOutputIds, ResourceIds::GRAPH_OUTPUT) == m_ExternalOutputIds.end())
    {
        m_ExternalOutputIds.push_back(ResourceIds::GRAPH_OUTPUT);
    }
//Modify End

//Modify Begin:2026-07-30 by BestHui
    m_CommandExecutor = std::make_unique<RenderGraphCommandExecutor>(
        m_DirectCommandQueue,
        m_AsyncComputeCommandQueue,
        m_ResourcePool,
        m_QueueScheduler,
        m_ResourceStateTracker,
        m_Profiler);
//Modify Begin:2026-08-07 by BestHui
    m_Compiler = std::make_unique<RenderGraphCompiler>(
        m_Device,
        m_ResourcePool,
        m_ResourceStateTracker);
    m_Compiler->ValidateDefinition(
        m_RenderPassesDescription,
        m_TextureDescriptions,
        m_BufferDescriptions,
        m_TokenDescriptions);
//Modify End
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

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End

    {
        PIXScope(*pCommandList, L"Render Graph: Prepare Present");

        if (pTexture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        }
        else
        {
            TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }

        FlushBarriers(*pCommandList);
    }

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
    pWindow->Present(*pTexture);
}

//Modify Begin:2026-07-28 by BestHui
void RenderGraph::RenderGraphRoot::PresentWithOverlay(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&)>& drawCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    auto& commandList = *pCommandList;

    {
        PIXScope(commandList, L"Render Graph: Prepare Display");

        if (pTexture->GetD3D12ResourceDesc().SampleDesc.Count > 1)
        {
            TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        }
        else
        {
            TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        FlushBarriers(commandList);

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

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
    pWindow->Present();
}

//Modify Begin:2026-08-07 by BestHui
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

        TransitionBarrier(*displayTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        for (const ResourceId resourceId : processorResourceIds)
        {
            Assert(resourceId != displayResourceId, "Display resource must not be duplicated in external processor resources.");
            TransitionBarrier(m_ResourcePool->GetResource(resourceId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        FlushBarriers(*commandList);

        const RenderTarget& backBufferRenderTarget = pWindow->GetRenderTarget();
        const std::shared_ptr<Texture>& backBuffer = backBufferRenderTarget.GetTexture(Color0);
        commandList->CopyResource(*backBuffer, *displayTexture);

        TransitionBarrier(*displayTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        FlushBarriers(*commandList);

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
//Modify End

void RenderGraph::RenderGraphRoot::PresentWithOverlayBlit(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
    const std::function<void(CommandList&)>& overlayCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    auto& commandList = *pCommandList;

    {
        PIXScope(commandList, L"Render Graph: Prepare Display Blit");

        TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        FlushBarriers(commandList);

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

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
    pWindow->Present();
}

void RenderGraph::RenderGraphRoot::TransitionTexture(
    const RenderMetadata& renderMetadata,
    const ResourceId resourceId,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool waitForCompletion)
{
    RebuildIfNecessary(renderMetadata);

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    TransitionBarrier(*pTexture, stateAfter);
    FlushBarriers(*pCommandList);

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    const uint64_t fenceValue = m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
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

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    auto& commandList = *pCommandList;
    const auto& source = m_ResourcePool->GetTexture(sourceId);
    const auto& destination = m_ResourcePool->GetTexture(destinationId);

    TransitionBarrier(*source, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionBarrier(*destination, D3D12_RESOURCE_STATE_COPY_DEST);
    FlushBarriers(commandList);

    commandList.CopyResource(*destination, *source);

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(sourceId, RenderPassQueue::Direct);
    m_QueueScheduler.TrackExternalResource(destinationId, RenderPassQueue::Direct);
    const uint64_t fenceValue = m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
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

//Modify Begin:2026-07-30 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    auto& commandList = *pCommandList;
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
    FlushBarriers(commandList);

    RenderTarget renderTarget;
    renderTarget.AttachTexture(Color0, pTexture);
    commandList.SetRenderTarget(renderTarget);
    commandList.SetAutomaticViewportAndScissorRect(renderTarget);

    drawCallback(commandList);

//Modify Begin:2026-07-30 by BestHui
    m_QueueScheduler.TrackExternalResource(resourceId, RenderPassQueue::Direct);
    m_QueueScheduler.SubmitDirect(pCommandList);
//Modify End
}
//Modify End

void RenderGraph::RenderGraphRoot::DrawToGraphOutput(const RenderMetadata& renderMetadata, const std::function<void(CommandList&)>& drawCallback)
{
//Modify Begin:2026-07-28 by BestHui
    DrawToTexture(renderMetadata, ResourceIds::GRAPH_OUTPUT, drawCallback);
//Modify End
}

//Modify Begin:2026-07-27 by BestHui
const std::shared_ptr<Texture>& RenderGraph::RenderGraphRoot::GetTexture(const ResourceId resourceId) const
{
    return m_ResourcePool->GetTexture(resourceId);
}
//Modify End

void RenderGraph::RenderGraphRoot::MarkDirty()
{
    m_Dirty = true;
}

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
void RenderGraph::RenderGraphRoot::TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter)
{
    m_ResourceStateTracker.TransitionBarrier(resource, stateAfter);
}

void RenderGraph::RenderGraphRoot::FlushBarriers(const CommandList& commandList)
{
    m_ResourceStateTracker.FlushBarriers(commandList);
}
