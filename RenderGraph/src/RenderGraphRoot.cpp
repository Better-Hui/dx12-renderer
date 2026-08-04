#include "RenderGraphRoot.h"

#include <algorithm>
#include <functional>
#include <set>
#include <queue>
//Modify Begin:2026-07-30 by BestHui
#include <utility>
//Modify End

#include <d3d12.h>
#include <d3dx12.h>

#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include "RenderContext.h"

namespace
{
//Modify Begin:2026-07-29 by BestHui
    bool TryGetInputTransition(
        const RenderGraph::InputType inputType,
        D3D12_RESOURCE_STATES& stateAfter,
        bool& insertUavBarrier)
    {
        insertUavBarrier = false;
        switch (inputType)
        {
        case RenderGraph::InputType::ShaderResource:
            stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            return true;
//Modify Begin:2026-08-03 by BestHui
        case RenderGraph::InputType::NonPixelShaderResource:
            stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            return true;
//Modify End
        case RenderGraph::InputType::UnorderedAccess:
            stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            insertUavBarrier = true;
            return true;
        case RenderGraph::InputType::ExternalAccess:
            stateAfter = D3D12_RESOURCE_STATE_COMMON;
            return true;
        case RenderGraph::InputType::CopySource:
            stateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            return true;
        case RenderGraph::InputType::IndirectArgument:
            stateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            return true;
        default:
            return false;
        }
    }

    bool TryGetOutputTransition(
        const RenderGraph::OutputType outputType,
        D3D12_RESOURCE_STATES& stateAfter,
        bool& insertUavBarrier)
    {
        insertUavBarrier = false;
        switch (outputType)
        {
        case RenderGraph::OutputType::CopyDestination:
            stateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            return true;
        case RenderGraph::OutputType::UnorderedAccess:
            stateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            insertUavBarrier = true;
            return true;
        case RenderGraph::OutputType::ExternalAccess:
            stateAfter = D3D12_RESOURCE_STATE_COMMON;
            return true;
        default:
            return false;
        }
    }
//Modify End

//Modify Begin:2026-07-28 by BestHui
    bool IsProducerOutput(const RenderGraph::OutputType outputType)
    {
        return outputType == RenderGraph::OutputType::Token ||
            outputType == RenderGraph::OutputType::RenderTarget ||
            outputType == RenderGraph::OutputType::DepthWrite ||
            outputType == RenderGraph::OutputType::UnorderedAccess ||
            outputType == RenderGraph::OutputType::ExternalAccess ||
            outputType == RenderGraph::OutputType::CopyDestination;
    }
//Modify End

    bool DirectlyDependsOn(const RenderGraph::RenderPass& pass1, const RenderGraph::RenderPass& pass2)
    {
        for (const auto& input : pass1.GetInputs())
        {
            for (const auto& output : pass2.GetOutputs())
            {
//Modify Begin:2026-07-28 by BestHui
                if (IsProducerOutput(output.m_Type) && input.m_Id == output.m_Id)
//Modify End
                {
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<std::vector<RenderGraph::RenderPass*>> TopologicalSort(const std::vector<std::unique_ptr<RenderGraph::RenderPass>>& renderPassesDescription)
    {
        std::vector<RenderGraph::RenderPass*> tempList;
        tempList.reserve(renderPassesDescription.size());

        std::vector<std::vector<RenderGraph::RenderPass*>> result;

        for (const auto& pRenderPass : renderPassesDescription)
        {
            tempList.push_back(pRenderPass.get());
        }

        while (tempList.size() > 0)
        {
            std::vector<RenderGraph::RenderPass*> passesWithNoDependencies;

            for (const auto& pass : tempList)
            {
                bool hasDependencies = false;

                for (const auto& otherPass : tempList)
                {
                    if (pass == otherPass)
                    {
                        continue;
                    }


                    if (DirectlyDependsOn(*pass, *otherPass))
                    {
                        hasDependencies = true;
                        break;
                    }
                }

                if (!hasDependencies)
                {
                    passesWithNoDependencies.push_back(pass);
                }
            }

            if (passesWithNoDependencies.size() > 0)
            {
                for (const auto& pass : passesWithNoDependencies)
                {
                    std::erase(tempList, pass);
                }

                result.emplace_back(std::move(passesWithNoDependencies));
            }
            else
            {
                Assert(false, "Render graph has a loop.");
            }
        }

        return result;
    }

//Modify Begin:2026-07-28 by BestHui
    std::set<RenderGraph::RenderPass*> FindUnusedPasses(
        const std::vector<std::vector<RenderGraph::RenderPass*>>& sortedRenderPasses,
        const std::vector<RenderGraph::ResourceId>& externalOutputIds)
//Modify End
    {
        std::set<RenderGraph::ResourceId> usedResources;
//Modify Begin:2026-07-28 by BestHui
        for (const RenderGraph::ResourceId outputId : externalOutputIds)
        {
            usedResources.insert(outputId);
        }
//Modify End

        std::set<RenderGraph::RenderPass*> unusedPasses;

        // initially, all are marked as unused
        for (const auto& passList : sortedRenderPasses)
        {
            for (const auto& pPass : passList)
            {
                unusedPasses.insert(pPass);
            }
        }

        for (auto it = sortedRenderPasses.rbegin(); it != sortedRenderPasses.rend(); ++it)
        {
            const auto& passList = *it;

            for (const auto& pPass : passList)
            {
                const auto& outputs = pPass->GetOutputs();

                // check if any of the outputs is used
                const auto findResult = std::ranges::find_if(outputs,
//Modify Begin:2026-07-28 by BestHui
                    [&usedResources](const RenderGraph::Output& o) { return IsProducerOutput(o.m_Type) && usedResources.contains(o.m_Id); }
//Modify End
                );

                if (findResult != outputs.end())
                {
                    // if the pass is used, mark all its inputs as used as well
                    for (const auto& input : pPass->GetInputs())
                    {
                        usedResources.insert(input.m_Id);
                    }

                    unusedPasses.erase(pPass);
                }
            }
        }

        return unusedPasses;
    }

    RenderGraph::RenderTargetInfo CreateRenderTargetOrDefault(const RenderGraph::RenderPass& renderPass, const RenderGraph::ResourcePool& resources)
    {
        using namespace RenderGraph;

        RenderTargetInfo renderTargetInfo = {};
        uint32_t colorTexturesCount = 0;
        uint32_t depthTexturesCount = 0;

        for (const auto& output : renderPass.GetOutputs())
        {
            switch (output.m_Type)
            {
            case OutputType::RenderTarget:
                {
                    if (renderTargetInfo.m_RenderTarget == nullptr)
                    {
                        renderTargetInfo.m_RenderTarget = std::make_shared<RenderTarget>();
                    }

                    const auto& pTexture = resources.GetTexture(output.m_Id);

                    const auto attachmentPoint = static_cast<AttachmentPoint>(static_cast<uint32_t>(Color0) + colorTexturesCount);
                    renderTargetInfo.m_RenderTarget->AttachTexture(attachmentPoint, pTexture);

                    colorTexturesCount++;
                    Assert(colorTexturesCount <= 8, "Too many color textures for the same render target");

                    break;
                }

            case OutputType::DepthWrite:
            case OutputType::DepthRead:
                {
                    if (renderTargetInfo.m_RenderTarget == nullptr)
                    {
                        renderTargetInfo.m_RenderTarget = std::make_shared<RenderTarget>();
                    }

                    const auto& pTexture = resources.GetTexture(output.m_Id);

                    renderTargetInfo.m_RenderTarget->AttachTexture(DepthStencil, pTexture);
                    depthTexturesCount++;
                    Assert(depthTexturesCount == 1, "Too many depth textures for the same render target");

                    if (output.m_Type == OutputType::DepthRead)
                    {
                        renderTargetInfo.m_ReadonlyDepth = true;
                    }
                }
                break;
            default:
                break;
            }
        }

        return renderTargetInfo;
    }
}

RenderGraph::RenderGraphRoot::RenderGraphRoot(
//Modify Begin:2026-07-30 by BestHui
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
    : m_Device(std::move(device))
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
    , m_ResourcePool(std::make_shared<ResourcePool>(m_DirectCommandQueue, m_AsyncComputeCommandQueue))
//Modify End
{
//Modify Begin:2026-07-30 by BestHui
    Assert(m_Device != nullptr, "Render graph requires a D3D12 device.");
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


    m_RenderPassesSorted = std::move(TopologicalSort(m_RenderPassesDescription));

    // Ensure all resources are defined
    {
        for (const auto& pass : m_RenderPassesDescription)
        {
            for (const auto& input : pass->GetInputs())
            {
                Assert(IsResourceDefined(input.m_Id), "Input undefined.");
            }

            for (const auto& output : pass->GetOutputs())
            {
                Assert(IsResourceDefined(output.m_Id), "Output undefined.");
            }
        }
    }

}

void RenderGraph::RenderGraphRoot::Execute(const RenderMetadata& renderMetadata)
{
//Modify Begin:2026-07-30 by BestHui
    RebuildIfNecessary(renderMetadata);
    m_CommandExecutor->Execute(
        renderMetadata,
        m_RenderPassesBuilt,
        m_RenderTargets,
        m_PassResourceStatePlans,
        m_DebugSerializeAsyncCompute);
//Modify End
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
        Build(renderMetadata);
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
void RenderGraph::RenderGraphRoot::Build(const RenderMetadata& renderMetadata)
{
//Modify Begin:2026-07-30 by BestHui
    const auto& pDevice = m_Device;
//Modify End

//Modify Begin:2026-07-28 by BestHui
    const auto unusedPasses = FindUnusedPasses(m_RenderPassesSorted, m_ExternalOutputIds);
//Modify End

    // Populate the final render pass list
    m_RenderPassesBuilt.clear();

    for (const auto& innerList : m_RenderPassesSorted)
    {
        for (const auto& pRenderPass : innerList)
        {
            if (!unusedPasses.contains(pRenderPass))
            {
                m_RenderPassesBuilt.push_back(pRenderPass);
            }
        }
    }

    // Register resources
    {
//Modify Begin:2026-07-30 by BestHui
        m_ResourcePool->Clear(m_QueueScheduler.GetResourceRetirements());
//Modify End

        for (const auto& desc : m_TextureDescriptions)
        {
            m_ResourcePool->RegisterTexture(desc, m_RenderPassesBuilt, renderMetadata, pDevice);
        }

        for (const auto& desc : m_BufferDescriptions)
        {
            m_ResourcePool->RegisterBuffer(desc, m_RenderPassesBuilt, renderMetadata, pDevice);
        }

//Modify Begin:2026-07-28 by BestHui
        m_ResourcePool->InitHeaps(m_RenderPassesBuilt, pDevice, m_ExternalOutputIds);
//Modify End
    }

    // Create resources
    {
        m_ResourceStateTracker.Reset();

        // if the resource is pruned, it won't be registered
        for (const auto& desc : m_TextureDescriptions)
        {
//Modify Begin:2026-07-28 by BestHui
            if (m_ResourcePool->IsRegistered(desc.m_Id))
            {
                const auto& resourceDescription = m_ResourcePool->GetDescription(desc.m_Id);
                if (resourceDescription.m_DedicatedResource || m_ResourcePool->HasResourceLifecycle(desc.m_Id))
                {
                    const auto& pTexture = m_ResourcePool->CreateTexture(desc.m_Id);
                    SetCurrentResourceState(*pTexture, D3D12_RESOURCE_STATE_COMMON);
                }
            }
//Modify End
        }

        for (const auto& desc : m_BufferDescriptions)
        {
//Modify Begin:2026-07-28 by BestHui
            if (m_ResourcePool->IsRegistered(desc.m_Id) && m_ResourcePool->HasResourceLifecycle(desc.m_Id))
            {
                const auto& pBuffer = m_ResourcePool->CreateBuffer(desc.m_Id);
                pBuffer->ForEachResourceRecursive([this](const auto& r)
                {
                    SetCurrentResourceState(r, D3D12_RESOURCE_STATE_COMMON);
                });
            }
//Modify End
        }
    }

    // Create render targets
    m_RenderTargets.clear();

//Modify Begin:2026-07-28 by BestHui
    for (const auto& pRenderPass : m_RenderPassesBuilt)
    {
        RenderTargetInfo renderTargetInfo = CreateRenderTargetOrDefault(*pRenderPass, *m_ResourcePool);

        if (renderTargetInfo.m_RenderTarget != nullptr)
        {
            m_RenderTargets.insert(std::pair{ pRenderPass, renderTargetInfo });
        }
    }
//Modify End

    m_GraphOutputRenderTarget = std::make_shared<RenderTarget>();
    m_GraphOutputRenderTarget->AttachTexture(Color0, m_ResourcePool->GetTexture(ResourceIds::GRAPH_OUTPUT));

//Modify Begin:2026-07-29 by BestHui
    BuildPassResourceStatePlans();
//Modify End
}

//Modify Begin:2026-07-29 by BestHui
void RenderGraph::RenderGraphRoot::BuildPassResourceStatePlans()
{
    m_PassResourceStatePlans.clear();
    for (uint32_t renderPassIndex = 0; renderPassIndex < static_cast<uint32_t>(m_RenderPassesBuilt.size()); ++renderPassIndex)
    {
        const RenderPass* renderPass = m_RenderPassesBuilt[renderPassIndex];
        PassResourceStatePlan plan;

        for (const Input& input : renderPass->GetInputs())
        {
            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_COMMON;
            bool insertUavBarrier = false;
            if (TryGetInputTransition(input.m_Type, stateAfter, insertUavBarrier))
            {
                plan.InputTransitions.push_back({ input.m_Id, stateAfter, insertUavBarrier });
            }
        }

        for (const Output& output : renderPass->GetOutputs())
        {
            if (output.m_Type == OutputType::Token)
            {
                continue;
            }

            const auto& lifecycle = m_ResourcePool->GetResourceLifecycle(output.m_Id);
            if (lifecycle.m_BeginPassIndex == renderPassIndex)
            {
                const auto& description = m_ResourcePool->GetDescription(output.m_Id);
                if (!description.m_DedicatedResource)
                {
                    plan.AliasingOutputs.push_back(output.m_Id);
                }
                plan.InitOutputs.push_back(output.m_Id);
            }

            D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_COMMON;
            bool insertUavBarrier = false;
            if (TryGetOutputTransition(output.m_Type, stateAfter, insertUavBarrier))
            {
                plan.OutputTransitions.push_back({ output.m_Id, stateAfter, insertUavBarrier });
            }
        }

        m_PassResourceStatePlans.emplace(renderPass, std::move(plan));
    }
}
//Modify End

D3D12_RESOURCE_STATES RenderGraph::RenderGraphRoot::GetCurrentResourceState(const Resource& resource) const
{
    return m_ResourceStateTracker.GetCurrentResourceState(resource);
}

//Modify Begin:2026-07-30 by BestHui
void RenderGraph::RenderGraphRoot::SetCurrentResourceState(const Resource& resource, D3D12_RESOURCE_STATES state)
{
    m_ResourceStateTracker.SetCurrentResourceState(resource, state);
}

void RenderGraph::RenderGraphRoot::TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter)
{
    m_ResourceStateTracker.TransitionBarrier(resource, stateAfter);
}

void RenderGraph::RenderGraphRoot::UavBarrier(const Resource& resource)
{
    m_ResourceStateTracker.UavBarrier(resource);
}

void RenderGraph::RenderGraphRoot::AliasingBarrier(const Resource& resourceAfter)
{
    m_ResourceStateTracker.AliasingBarrier(resourceAfter);
}

void RenderGraph::RenderGraphRoot::FlushBarriers(const CommandList& commandList)
{
    m_ResourceStateTracker.FlushBarriers(commandList);
}

bool RenderGraph::RenderGraphRoot::IsResourceDefined(const ResourceId id) const
{
    for (const auto& texture : m_TextureDescriptions)
    {
        if (texture.m_Id == id)
            return true;
    }

    for (const auto& buffer : m_BufferDescriptions)
    {
        if (buffer.m_Id == id)
            return true;
    }

    for (const auto& token : m_TokenDescriptions)
    {
        if (token.m_Id == id)
            return true;
    }

    return false;
}
