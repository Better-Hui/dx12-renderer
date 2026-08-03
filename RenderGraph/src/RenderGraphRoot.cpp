#include "RenderGraphRoot.h"

#include <algorithm>
#include <functional>
#include <set>
#include <queue>
#include <string>

#include <d3d12.h>
#include <d3dx12.h>

#include <DX12Library/Helpers.h>
//Modify Begin:2026-07-29 by BestHui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include "RenderContext.h"

namespace
{
//Modify Begin:2026-07-29 by BestHui
    std::string NarrowPassName(const std::wstring& passName)
    {
        std::string result;
        result.reserve(passName.size());
        for (const wchar_t character : passName)
        {
            result.push_back(character >= 0 && character < 128 ? static_cast<char>(character) : '?');
        }
        return result;
    }
//Modify End

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

//Modify Begin:2026-08-03 by BestHui
    bool IsAsyncComputeInput(const RenderGraph::InputType inputType)
    {
        return inputType == RenderGraph::InputType::Token ||
            inputType == RenderGraph::InputType::ShaderResource ||
            inputType == RenderGraph::InputType::NonPixelShaderResource ||
            inputType == RenderGraph::InputType::CopySource ||
            inputType == RenderGraph::InputType::IndirectArgument;
    }

    bool IsAsyncComputeOutput(const RenderGraph::OutputType outputType)
    {
        return outputType == RenderGraph::OutputType::Token ||
            outputType == RenderGraph::OutputType::UnorderedAccess ||
            outputType == RenderGraph::OutputType::CopyDestination;
    }
//Modify End

//Modify Begin:2026-08-03 by BestHui
    D3D12_RESOURCE_STATES GetQueueResourceState(
        const RenderGraph::RenderPassQueue queue,
        const D3D12_RESOURCE_STATES state)
    {
        if (queue == RenderGraph::RenderPassQueue::AsyncCompute &&
            state == D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE)
        {
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        return state;
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
    std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
    std::vector<TextureDescription>&& textures,
    std::vector<BufferDescription>&& buffers,
    std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by BestHui
    , std::vector<ResourceId> externalOutputs
//Modify End
)
    : m_DirectCommandQueue(Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT))
//Modify Begin:2026-08-03 by BestHui
    , m_AsyncComputeCommandQueue(Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE))
//Modify End
    , m_RenderPassesDescription(std::move(renderPasses))
    , m_TextureDescriptions(std::move(textures))
    , m_BufferDescriptions(std::move(buffers))
    , m_TokenDescriptions(std::move(tokens))
//Modify Begin:2026-07-28 by BestHui
    , m_ExternalOutputIds(std::move(externalOutputs))
//Modify End
    , m_ResourcePool(std::make_shared<ResourcePool>())
{
//Modify Begin:2026-07-28 by BestHui
    if (std::ranges::find(m_ExternalOutputIds, ResourceIds::GRAPH_OUTPUT) == m_ExternalOutputIds.end())
    {
        m_ExternalOutputIds.push_back(ResourceIds::GRAPH_OUTPUT);
    }
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
    RebuildIfNecessary(renderMetadata);

//Modify Begin:2026-08-03 by BestHui
    if (m_AsyncComputeSubmittedThisFrame)
    {
        m_DirectCommandQueue->Wait(*m_AsyncComputeCommandQueue, m_LastAsyncComputeFenceValue);
        m_AsyncComputeSubmittedThisFrame = false;
        m_LastAsyncComputeFenceValue = 0;
    }
    m_LastWriterQueues.clear();
    m_LastWriterFenceValues.clear();
//Modify End

//Modify Begin:2026-07-28 by BestHui
    auto pCommandList = m_DirectCommandQueue->GetCommandList();
//Modify End
    Assert(m_PendingBarriers.size() == 0, "Pending barriers were left from after the previous frame.");
//Modify Begin:2026-08-03 by BestHui
    bool directProfilerFrameStarted = false;
    bool asyncComputeProfilerFrameStarted = false;
    const RenderPass* lastAsyncComputePass = nullptr;
    for (const auto& renderPass : m_RenderPassesBuilt)
    {
        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            lastAsyncComputePass = renderPass;
        }
    }
//Modify End

    {
        PIXScopeCPU(L"Render Graph: Execute");

//Modify Begin:2026-07-29 by BestHui
        if (m_GpuTimestampProfiler != nullptr && m_GpuTimestampProfiler->IsAvailable())
        {
            directProfilerFrameStarted = m_GpuTimestampProfiler->BeginFrame(renderMetadata.m_FrameIndex);
            if (directProfilerFrameStarted)
            {
                m_GpuTimestampProfiler->WriteTimestamp(*pCommandList, "RenderGraph.Begin");
            }
        }
//Modify End

        RenderContext context = {};
        context.m_ResourcePool = m_ResourcePool;
        context.m_Metadata = renderMetadata;

        uint32_t renderPassIndex = 0;
        m_ResourcePool->BeginFrame(*pCommandList);

//Modify Begin:2026-08-03 by BestHui
        const auto flushDirectCommandList = [&]()
        {
            if (pCommandList != nullptr)
            {
                const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(pCommandList);
                for (const auto& [resourceId, queue] : m_LastWriterQueues)
                {
                    if (queue == RenderPassQueue::Direct && m_LastWriterFenceValues[resourceId] == 0u)
                    {
                        m_LastWriterFenceValues[resourceId] = fenceValue;
                    }
                }
                pCommandList.reset();
                return fenceValue;
            }
            return uint64_t{ 0u };
        };

        const auto prepareQueueDependency = [&](const RenderPass& pass)
        {
            const RenderPassQueue queue = pass.GetQueue();
            bool dependsOnOtherQueue = false;
            uint64_t producerFenceValue = 0;
            auto inspectResource = [&](const ResourceId resourceId)
            {
                const auto writer = m_LastWriterQueues.find(resourceId);
                if (writer == m_LastWriterQueues.end() || writer->second == queue)
                {
                    return;
                }
                dependsOnOtherQueue = true;
                const auto fence = m_LastWriterFenceValues.find(resourceId);
                Assert(fence != m_LastWriterFenceValues.end(), "Render pass writer fence was not recorded.");
                producerFenceValue = (std::max)(producerFenceValue, fence->second);
            };
            for (const auto& input : pass.GetInputs()) inspectResource(input.m_Id);
            for (const auto& output : pass.GetOutputs()) inspectResource(output.m_Id);

            if (queue == RenderPassQueue::AsyncCompute && dependsOnOtherQueue)
            {
                const auto planIt = m_PassResourceStatePlans.find(&pass);
                Assert(planIt != m_PassResourceStatePlans.end(), "Render pass resource state plan was not built.");

                if (pCommandList == nullptr)
                {
                    pCommandList = m_DirectCommandQueue->GetCommandList();
                }

                for (const PassResourceTransition& transition : planIt->second.InputTransitions)
                {
                    const auto writer = m_LastWriterQueues.find(transition.Id);
                    if (writer == m_LastWriterQueues.end() || writer->second != RenderPassQueue::Direct)
                    {
                        continue;
                    }

                    const auto& resource = m_ResourcePool->GetResource(transition.Id);
                    resource.ForEachResourceRecursive([this, queue, &transition](const Resource& r)
                    {
                        TransitionBarrier(r, GetQueueResourceState(queue, transition.StateAfter));
                    });
                }
//Modify Begin:2026-08-03 by BestHui
                for (const PassResourceTransition& transition : planIt->second.OutputTransitions)
                {
                    const auto& resource = m_ResourcePool->GetResource(transition.Id);
                    resource.ForEachResourceRecursive([this, queue, &transition](const Resource& r)
                    {
                        TransitionBarrier(r, GetQueueResourceState(queue, transition.StateAfter));
                    });
                }
//Modify End
                FlushBarriers(*pCommandList);
//Modify Begin:2026-08-03 by BestHui
                pass.PrepareAsyncCompute(*pCommandList);
                if (directProfilerFrameStarted)
                {
                    const std::string markerName = "Async Prepare." + NarrowPassName(pass.GetPassName());
                    m_GpuTimestampProfiler->WriteTimestamp(*pCommandList, markerName.c_str());
                }
//Modify End
                producerFenceValue = flushDirectCommandList();
            }
            if (!dependsOnOtherQueue)
            {
                return;
            }
            if (queue == RenderPassQueue::AsyncCompute)
            {
                m_AsyncComputeCommandQueue->Wait(*m_DirectCommandQueue, producerFenceValue);
            }
            else
            {
                flushDirectCommandList();
                m_DirectCommandQueue->Wait(*m_AsyncComputeCommandQueue, producerFenceValue);
                m_AsyncComputeSubmittedThisFrame = false;
                m_LastAsyncComputeFenceValue = 0;
            }
        };

        const auto markPassOutputs = [&](const RenderPass& pass, const uint64_t fenceValue)
        {
            for (const auto& output : pass.GetOutputs())
            {
                m_LastWriterQueues[output.m_Id] = pass.GetQueue();
                m_LastWriterFenceValues[output.m_Id] = fenceValue;
            }
        };
//Modify End

        for (const auto& pRenderPass : m_RenderPassesBuilt)
        {
//Modify Begin:2026-08-03 by BestHui
            Assert(!pRenderPass->IsExternal() || pRenderPass->GetQueue() == RenderPassQueue::Direct,
                "External render passes must use the direct queue.");
            if (pRenderPass->GetQueue() == RenderPassQueue::AsyncCompute)
            {
                for (const auto& input : pRenderPass->GetInputs())
                {
                    Assert(IsAsyncComputeInput(input.m_Type),
                        "Async compute render passes only support read-only resource inputs.");
                }
                for (const auto& output : pRenderPass->GetOutputs())
                {
                    Assert(IsAsyncComputeOutput(output.m_Type),
                        "Async compute render passes cannot write render targets or depth resources.");
                }
                prepareQueueDependency(*pRenderPass);
                auto computeCommandList = m_AsyncComputeCommandQueue->GetCommandList();
                if (m_AsyncComputeGpuTimestampProfiler != nullptr && m_AsyncComputeGpuTimestampProfiler->IsAvailable())
                {
                    if (!asyncComputeProfilerFrameStarted)
                    {
                        asyncComputeProfilerFrameStarted = m_AsyncComputeGpuTimestampProfiler->BeginFrame(renderMetadata.m_FrameIndex);
                        if (asyncComputeProfilerFrameStarted)
                        {
                            m_AsyncComputeGpuTimestampProfiler->WriteTimestamp(*computeCommandList, "RenderGraph.Begin");
                        }
                    }
                }
                context.m_RenderTargetInfo = {};
                RenderContext& computeContext = context;
                PrepareResourcesForRenderPass(*computeCommandList, *pRenderPass, renderPassIndex, computeContext);
                pRenderPass->Execute(computeContext, *computeCommandList);
                if (m_AsyncComputeGpuTimestampProfiler != nullptr && m_AsyncComputeGpuTimestampProfiler->IsAvailable())
                {
                    const std::string passName = NarrowPassName(pRenderPass->GetPassName());
                    m_AsyncComputeGpuTimestampProfiler->WriteTimestamp(*computeCommandList, passName.c_str());
                }
                const bool isLastAsyncComputePass = pRenderPass == lastAsyncComputePass;
                if (isLastAsyncComputePass && asyncComputeProfilerFrameStarted)
                {
                    m_AsyncComputeGpuTimestampProfiler->WriteTimestamp(*computeCommandList, "RenderGraph.End");
                    m_AsyncComputeGpuTimestampProfiler->ResolveFrame(*computeCommandList);
                }
                const uint64_t computeFenceValue = m_AsyncComputeCommandQueue->ExecuteCommandList(computeCommandList);
                if (isLastAsyncComputePass && asyncComputeProfilerFrameStarted)
                {
                    m_AsyncComputeGpuTimestampProfiler->EndFrame(computeFenceValue);
                }
                m_AsyncComputeSubmittedThisFrame = true;
                m_LastAsyncComputeFenceValue = computeFenceValue;
                markPassOutputs(*pRenderPass, computeFenceValue);
                ++renderPassIndex;
                continue;
            }
//Modify End
            if (pCommandList == nullptr)
            {
                pCommandList = m_DirectCommandQueue->GetCommandList();
            }

            CommandList& cmd = *pCommandList;
            context.m_RenderTargetInfo = {};
//Modify Begin:2026-07-28 by BestHui
            if (pRenderPass->IsExternal())
            {
                {
                    PIXScope(cmd, pRenderPass->GetPassName().c_str());
                    PrepareResourcesForRenderPass(cmd, *pRenderPass, renderPassIndex, context);
//Modify Begin:2026-07-29 by BestHui
                    if (directProfilerFrameStarted)
                    {
                        const std::string passName = "BeforeExternal." + NarrowPassName(pRenderPass->GetPassName());
                        m_GpuTimestampProfiler->WriteTimestamp(cmd, passName.c_str());
                    }
//Modify End
                }
                m_DirectCommandQueue->ExecuteCommandList(pCommandList);
                pCommandList.reset();

                pRenderPass->ExecuteExternal(context);

//Modify Begin:2026-07-29 by BestHui
                if (directProfilerFrameStarted)
                {
                    pCommandList = m_DirectCommandQueue->GetCommandList();
                    const std::string passName = "AfterExternal." + NarrowPassName(pRenderPass->GetPassName());
                    m_GpuTimestampProfiler->WriteTimestamp(*pCommandList, passName.c_str());
//Modify Begin:2026-07-30 by BestHui
                    m_DirectCommandQueue->ExecuteCommandList(pCommandList);
                    pCommandList.reset();
//Modify End
                }
//Modify End
            }
            else
            {
                PIXScope(cmd, pRenderPass->GetPassName().c_str());
                PrepareResourcesForRenderPass(cmd, *pRenderPass, renderPassIndex, context);
                pRenderPass->Execute(context, cmd);
//Modify Begin:2026-07-29 by BestHui
                if (directProfilerFrameStarted)
                {
                    const std::string passName = NarrowPassName(pRenderPass->GetPassName());
                    m_GpuTimestampProfiler->WriteTimestamp(cmd, passName.c_str());
                }
//Modify End
            }
//Modify Begin:2026-08-03 by BestHui
            markPassOutputs(*pRenderPass, 0u);
//Modify End
//Modify End

            renderPassIndex++;
        }
    }

//Modify Begin:2026-08-03 by BestHui
//Modify Begin:2026-07-29 by BestHui
    if (directProfilerFrameStarted)
    {
//Modify Begin:2026-07-30 by BestHui
        if (pCommandList == nullptr)
        {
            pCommandList = m_DirectCommandQueue->GetCommandList();
        }
//Modify End
        m_GpuTimestampProfiler->WriteTimestamp(*pCommandList, "RenderGraph.End");
        m_GpuTimestampProfiler->ResolveFrame(*pCommandList);
        const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(pCommandList);
        m_GpuTimestampProfiler->EndFrame(fenceValue);
    }
    else if (pCommandList != nullptr)
    {
        m_DirectCommandQueue->ExecuteCommandList(pCommandList);
    }
//Modify End
}

void RenderGraph::RenderGraphRoot::Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();

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

    m_DirectCommandQueue->ExecuteCommandList(pCommandList);
    pWindow->Present(*pTexture);
}

//Modify Begin:2026-07-28 by BestHui
void RenderGraph::RenderGraphRoot::PresentWithOverlay(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&)>& drawCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();
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

    m_DirectCommandQueue->ExecuteCommandList(pCommandList);
    pWindow->Present();
}

void RenderGraph::RenderGraphRoot::PresentWithOverlayBlit(
    const std::shared_ptr<Window>& pWindow,
    const ResourceId resourceId,
    const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
    const std::function<void(CommandList&)>& overlayCallback)
{
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();
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

    m_DirectCommandQueue->ExecuteCommandList(pCommandList);
    pWindow->Present();
}

void RenderGraph::RenderGraphRoot::TransitionTexture(
    const RenderMetadata& renderMetadata,
    const ResourceId resourceId,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool waitForCompletion)
{
    RebuildIfNecessary(renderMetadata);

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    TransitionBarrier(*pTexture, stateAfter);
    FlushBarriers(*pCommandList);

    const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(pCommandList);
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

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;
    const auto& source = m_ResourcePool->GetTexture(sourceId);
    const auto& destination = m_ResourcePool->GetTexture(destinationId);

    TransitionBarrier(*source, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionBarrier(*destination, D3D12_RESOURCE_STATE_COPY_DEST);
    FlushBarriers(commandList);

    commandList.CopyResource(*destination, *source);

    const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(pCommandList);
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

    const auto pCommandList = m_DirectCommandQueue->GetCommandList();
    auto& commandList = *pCommandList;
    const auto& pTexture = m_ResourcePool->GetTexture(resourceId);

    TransitionBarrier(*pTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
    FlushBarriers(commandList);

    RenderTarget renderTarget;
    renderTarget.AttachTexture(Color0, pTexture);
    commandList.SetRenderTarget(renderTarget);
    commandList.SetAutomaticViewportAndScissorRect(renderTarget);

    drawCallback(commandList);

    m_DirectCommandQueue->ExecuteCommandList(pCommandList);
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
    const auto& application = Application::Get();
    const auto& pDevice = application.GetDevice();

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
        m_ResourcePool->Clear();

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
        m_ResourceStates.clear();

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
    const auto& result = m_ResourceStates.find(&resource);
    Assert(result != m_ResourceStates.end(), "Resource does not have a registered state");
    return result->second;
}

void RenderGraph::RenderGraphRoot::PrepareResourcesForRenderPass(CommandList& commandList, const RenderPass& renderPass, const uint32_t renderPassIndex, RenderContext& context)
{
//Modify Begin:2026-07-29 by BestHui
    const auto planIt = m_PassResourceStatePlans.find(&renderPass);
    Assert(planIt != m_PassResourceStatePlans.end(), "Render pass resource state plan was not built.");
    const PassResourceStatePlan& resourceStatePlan = planIt->second;

    for (const PassResourceTransition& transition : resourceStatePlan.InputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([this, &renderPass, &transition](const Resource& r)
        {
            TransitionBarrier(
                r,
                GetQueueResourceState(renderPass.GetQueue(), transition.StateAfter));
            if (transition.InsertUavBarrier)
            {
                UavBarrier(r);
            }
        });
    }

    for (const ResourceId outputId : resourceStatePlan.AliasingOutputs)
    {
        (void)renderPassIndex;
        const auto& resource = m_ResourcePool->GetResource(outputId);
        resource.ForEachResourceRecursive([this](const auto& r)
        {
            AliasingBarrier(r);
        });
    }
//Modify End

    // Render Target barriers
    const auto& renderTargetFindResult = m_RenderTargets.find(&renderPass);
    if (renderTargetFindResult != m_RenderTargets.end())
    {
        const auto& renderTargetInfo = renderTargetFindResult->second;
        context.m_RenderTargetInfo = renderTargetInfo;

        const auto& pRenderTarget = renderTargetInfo.m_RenderTarget;
        const auto& textures = pRenderTarget->GetTextures();

        // Render Target barriers for color attachments
        for (size_t i = 0; i < 8; ++i)
        {
            const auto& pRtTexture = textures[i];
            if (pRtTexture != nullptr && pRtTexture->IsValid())
            {
                TransitionBarrier(*pRtTexture, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }

        // Depth-Stencil barriers
        const auto& pRtDepthStencil = pRenderTarget->GetTexture(DepthStencil);
        if (pRtDepthStencil != nullptr && pRtDepthStencil->IsValid())
        {
            const auto stateAfter = renderTargetInfo.m_ReadonlyDepth ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
            TransitionBarrier(*pRtDepthStencil, stateAfter);
        }
    }


//Modify Begin:2026-07-29 by BestHui
    for (const PassResourceTransition& transition : resourceStatePlan.OutputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([this, &transition](const auto& r)
        {
            TransitionBarrier(r, transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                UavBarrier(r);
            }
        });
    }
//Modify End

    FlushBarriers(commandList);

    // Process init actions
//Modify Begin:2026-07-29 by BestHui
    for (const ResourceId outputId : resourceStatePlan.InitOutputs)
    {
        (void)renderPassIndex;
        const auto& description = m_ResourcePool->GetDescription(outputId);

            switch (description.GetInitAction())
            {
            case Clear:
                {
                    Assert(description.m_ResourceType == ResourceType::Texture, "Only textures support the clear init action.");

                    if (const auto renderPassOutput = std::ranges::find_if(renderPass.GetOutputs(), [outputId](const Output& output) { return output.m_Id == outputId; });
                        renderPassOutput != renderPass.GetOutputs().end() && renderPassOutput->m_Type == OutputType::RenderTarget)
                    {
                        const auto& texture = *m_ResourcePool->GetTexture(outputId);
                        commandList.ClearTexture(texture, description.GetClearValue());
                    }
                    else if (renderPassOutput != renderPass.GetOutputs().end() &&
                        (renderPassOutput->m_Type == OutputType::DepthRead || renderPassOutput->m_Type == OutputType::DepthWrite))
                    {
                        const auto& texture = *m_ResourcePool->GetTexture(outputId);
                        const auto dsClearValue = description.GetClearValue().GetD3D12ClearValue()->DepthStencil;
                        commandList.ClearDepthStencilTexture(texture, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, dsClearValue.Depth, dsClearValue.Stencil);
                    }
                }
                break;
            case CopyDestination:
                // don't do anything here, the copy in the pass should do the job
                break;
            case Discard:
                {
                    const auto& resource = m_ResourcePool->GetResource(outputId);
                    commandList.DiscardResource(resource);
                }
                break;
            default:
                Assert(false, "Unknown resource init action.");
                break;
            }
    }
//Modify End

    // Setup the render target
    if (renderTargetFindResult != m_RenderTargets.end())
    {
        const auto& renderTargetInfo = renderTargetFindResult->second;
        const auto& pRenderTarget = renderTargetInfo.m_RenderTarget;

        commandList.SetRenderTarget(*pRenderTarget);
        commandList.SetAutomaticViewportAndScissorRect(*pRenderTarget);
    }
}

void RenderGraph::RenderGraphRoot::SetCurrentResourceState(const Resource& resource, D3D12_RESOURCE_STATES state)
{
    if (const auto existingEntry = m_ResourceStates.find(&resource); existingEntry == m_ResourceStates.end())
    {
        m_ResourceStates.insert(std::pair{ &resource, state });
    }
    else
    {
        existingEntry->second = state;
    }
}

void RenderGraph::RenderGraphRoot::TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter)
{
    const auto stateBefore = GetCurrentResourceState(resource);
    if (stateBefore == stateAfter)
    {
        // no need for a barrier
        return;
    }

    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.GetD3D12Resource().Get(), stateBefore, stateAfter);
    m_PendingBarriers.push_back(barrier);

    SetCurrentResourceState(resource, stateAfter);
}

void RenderGraph::RenderGraphRoot::UavBarrier(const Resource& resource)
{
//Modify Begin:2026-07-28 by BestHui
    Assert(GetCurrentResourceState(resource) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Resource is supposed to be in UAV state to issue a UAV barrier.");
//Modify End

    // TODO: skip if there was a transition barrier after the previous UAV usage
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.GetD3D12Resource().Get());
    m_PendingBarriers.push_back(barrier);
}

void RenderGraph::RenderGraphRoot::AliasingBarrier(const Resource& resourceAfter)
{
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Aliasing(nullptr, resourceAfter.GetD3D12Resource().Get());
    m_PendingBarriers.push_back(barrier);
}

void RenderGraph::RenderGraphRoot::FlushBarriers(const CommandList& commandList)
{
    if (m_PendingBarriers.size() == 0)
    {
        return;
    }

    const auto& pDxCmd = commandList.GetGraphicsCommandList();
    pDxCmd->ResourceBarrier(static_cast<UINT>(m_PendingBarriers.size()), m_PendingBarriers.data());
    m_PendingBarriers.clear();
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
