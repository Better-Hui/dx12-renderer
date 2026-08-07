//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphCommandExecutor.h"

#include "RenderGraphProfiler.h"
#include "RenderGraphQueueScheduler.h"
#include "RenderGraphResourceStateTracker.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Resource.h>
#include <DX12Library/Texture.h>

#include <algorithm>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
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
}

RenderGraph::RenderGraphCommandExecutor::RenderGraphCommandExecutor(
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
    std::shared_ptr<ResourcePool> resourcePool,
    RenderGraphQueueScheduler& queueScheduler,
    RenderGraphResourceStateTracker& resourceStateTracker,
    RenderGraphProfiler& profiler)
    : m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
    , m_ResourcePool(std::move(resourcePool))
    , m_QueueScheduler(queueScheduler)
    , m_ResourceStateTracker(resourceStateTracker)
    , m_Profiler(profiler)
{
    Assert(m_DirectCommandQueue != nullptr, "Render graph executor requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph executor requires an async compute command queue.");
    Assert(m_ResourcePool != nullptr, "Render graph executor requires a resource pool.");
}

void RenderGraph::RenderGraphCommandExecutor::Execute(
    const RenderMetadata& renderMetadata,
    const CompiledRenderGraph& compiledGraph,
    const bool debugSerializeAsyncCompute,
    const bool enableParallelDirectRecording)
{
    const std::vector<RenderPass*>& renderPasses = compiledGraph.GetRenderPasses();
    const std::vector<RenderGraphExecutionBatch>& executionBatches = compiledGraph.GetExecutionBatches();
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets = compiledGraph.GetRenderTargets();
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans = compiledGraph.GetResourceStatePlans();
    m_QueueScheduler.BeginFrame();

    auto directCommandList = m_DirectCommandQueue->GetCommandList();
    Assert(!m_ResourceStateTracker.HasPendingBarriers(), "Pending barriers were left from after the previous frame.");

    const RenderPass* lastAsyncComputePass = nullptr;
    for (const RenderPass* renderPass : renderPasses)
    {
        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            lastAsyncComputePass = renderPass;
        }
    }

    PIXScopeCPU(L"Render Graph: Execute");
    m_Profiler.BeginQueueFrame(RenderPassQueue::Direct, renderMetadata.m_FrameIndex, *directCommandList);

    FrameContext context(m_ResourcePool, renderMetadata);

    m_ResourcePool->BeginFrame(*directCommandList);

    uint32_t renderPassIndex = 0;
    for (const RenderGraphExecutionBatch& executionBatch : executionBatches)
    {
        if (enableParallelDirectRecording && executionBatch.ParallelRecordingEligible)
        {
            ExecuteParallelDirectBatch(
                executionBatch,
                renderMetadata,
                renderPassIndex,
                directCommandList,
                renderTargets,
                resourceStatePlans);
            renderPassIndex += static_cast<uint32_t>(executionBatch.Passes.size());
            continue;
        }

        for (RenderPass* renderPass : executionBatch.Passes)
        {
        Assert(!renderPass->IsExternal() || renderPass->GetQueue() == RenderPassQueue::Direct,
            "External render passes must use the direct queue.");

        if (renderPass->GetQueue() == RenderPassQueue::AsyncCompute)
        {
            for (const Input& input : renderPass->GetInputs())
            {
                Assert(IsAsyncComputeInput(input.m_Type),
                    "Async compute render passes only support read-only resource inputs.");
            }
            for (const Output& output : renderPass->GetOutputs())
            {
                Assert(IsAsyncComputeOutput(output.m_Type),
                    "Async compute render passes cannot write render targets or depth resources.");
            }

            PrepareQueueDependency(*renderPass, directCommandList, resourceStatePlans);

            auto computeCommandList = m_AsyncComputeCommandQueue->GetCommandList();
            if (!m_Profiler.IsQueueFrameActive(RenderPassQueue::AsyncCompute))
            {
                m_Profiler.BeginQueueFrame(
                    RenderPassQueue::AsyncCompute,
                    renderMetadata.m_FrameIndex,
                    *computeCommandList);
            }

            context.SetRenderTargetInfo({});
            PrepareResourcesForRenderPass(
                *computeCommandList,
                *renderPass,
                renderPassIndex,
                context,
                renderTargets,
                resourceStatePlans,
                true);
            try
            {
                renderPass->Execute(context, *computeCommandList);
            }
            catch (const std::exception& exception)
            {
                throw std::runtime_error(
                    "RenderGraph async compute pass '" +
                    RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()) +
                    "' execution failed: " + exception.what());
            }
            m_Profiler.WritePassTimestamp(RenderPassQueue::AsyncCompute, *computeCommandList, renderPass->GetPassName());

            const bool isLastAsyncComputePass = renderPass == lastAsyncComputePass;
            if (isLastAsyncComputePass)
            {
                m_Profiler.ResolveQueueFrame(RenderPassQueue::AsyncCompute, *computeCommandList);
            }

            const uint64_t computeFenceValue = m_QueueScheduler.SubmitAsyncCompute(
                computeCommandList,
                debugSerializeAsyncCompute);
            if (isLastAsyncComputePass)
            {
                m_Profiler.EndQueueFrame(RenderPassQueue::AsyncCompute, computeFenceValue);
            }
            m_QueueScheduler.TrackPassResources(*renderPass, computeFenceValue);
            ++renderPassIndex;
            continue;
        }

        PrepareQueueDependency(*renderPass, directCommandList, resourceStatePlans);
        if (directCommandList == nullptr)
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
        }

        CommandList& commandList = *directCommandList;
        context.SetRenderTargetInfo({});
        if (renderPass->IsExternal())
        {
            {
                PIXScope(commandList, renderPass->GetPassName().c_str());
                PrepareResourcesForRenderPass(
                    commandList,
                    *renderPass,
                    renderPassIndex,
                    context,
                    renderTargets,
                    resourceStatePlans);
                m_Profiler.WriteMarker(
                    RenderPassQueue::Direct,
                    commandList,
                    "BeforeExternal." + RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()));
            }

            m_QueueScheduler.TrackPassResources(*renderPass, 0u);
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
            PrepareResourcesForRenderPass(
                commandList,
                *renderPass,
                renderPassIndex,
                context,
                renderTargets,
                resourceStatePlans);
            try
            {
                renderPass->Execute(context, commandList);
            }
            catch (const std::exception& exception)
            {
                throw std::runtime_error(
                    "RenderGraph direct pass '" +
                    RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()) +
                    "' execution failed: " + exception.what());
            }
            m_Profiler.WritePassTimestamp(RenderPassQueue::Direct, commandList, renderPass->GetPassName());
            m_QueueScheduler.TrackPassResources(*renderPass, 0u);
        }

            ++renderPassIndex;
        }
    }

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
}

//Modify Begin:2026-07-30 by BestHui
void RenderGraph::RenderGraphCommandExecutor::ExecuteParallelDirectBatch(
    const RenderGraphExecutionBatch& batch,
    const RenderMetadata& renderMetadata,
    const uint32_t firstRenderPassIndex,
    std::shared_ptr<CommandList>& directCommandList,
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(batch.Passes.size() > 1u, "Parallel recording batches require at least two passes.");
    for (const RenderPass* renderPass : batch.Passes)
    {
        Assert(renderPass->GetQueue() == RenderPassQueue::Direct, "Parallel recording only supports the direct queue.");
        Assert(!renderPass->IsExternal(), "External passes cannot record in parallel.");
        Assert(renderPass->IsParallelRecordingEligible(), "Parallel recording batch contains a pass without an explicit safety declaration.");
        PrepareQueueDependency(*renderPass, directCommandList, resourceStatePlans);
    }

    if (directCommandList == nullptr)
    {
        directCommandList = m_DirectCommandQueue->GetCommandList();
    }

    CommandList& preambleCommandList = *directCommandList;
    for (uint32_t passOffset = 0; passOffset < batch.Passes.size(); ++passOffset)
    {
        FrameContext preambleContext(m_ResourcePool, renderMetadata);
        PrepareResourcesForRenderPass(
            preambleCommandList,
            *batch.Passes[passOffset],
            firstRenderPassIndex + passOffset,
            preambleContext,
            renderTargets,
            resourceStatePlans);
    }
    m_QueueScheduler.SubmitDirect(directCommandList);

    std::vector<std::future<std::shared_ptr<CommandList>>> recordingTasks;
    recordingTasks.reserve(batch.Passes.size());
    for (RenderPass* renderPass : batch.Passes)
    {
        recordingTasks.push_back(std::async(
            std::launch::async,
            [this, renderPass, &renderMetadata, &renderTargets]()
            {
                auto commandList = m_DirectCommandQueue->GetCommandList();
                FrameContext context(m_ResourcePool, renderMetadata);
                const auto renderTargetIt = renderTargets.find(renderPass);
                if (renderTargetIt != renderTargets.end())
                {
                    context.SetRenderTargetInfo(renderTargetIt->second);
                }

                try
                {
                    PIXScope(*commandList, renderPass->GetPassName().c_str());
                    renderPass->Execute(context, *commandList);
                }
                catch (const std::exception& exception)
                {
                    throw std::runtime_error(
                        "RenderGraph parallel direct pass '" +
                        RenderGraphProfiler::NarrowPassName(renderPass->GetPassName()) +
                        "' execution failed: " + exception.what());
                }
                return commandList;
            }));
    }

    std::vector<std::shared_ptr<CommandList>> recordedCommandLists;
    recordedCommandLists.reserve(recordingTasks.size());
    for (uint32_t passOffset = 0; passOffset < recordingTasks.size(); ++passOffset)
    {
        std::shared_ptr<CommandList> commandList = recordingTasks[passOffset].get();
        m_Profiler.WritePassTimestamp(
            RenderPassQueue::Direct,
            *commandList,
            batch.Passes[passOffset]->GetPassName());
        m_QueueScheduler.TrackPassResources(*batch.Passes[passOffset], 0u);
        recordedCommandLists.push_back(std::move(commandList));
    }
    m_QueueScheduler.SubmitDirect(recordedCommandLists);
}
//Modify End

void RenderGraph::RenderGraphCommandExecutor::PrepareQueueDependency(
    const RenderPass& pass,
    std::shared_ptr<CommandList>& directCommandList,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    if (pass.GetQueue() == RenderPassQueue::AsyncCompute)
    {
        const auto planIt = resourceStatePlans.find(&pass);
        Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");

        if (directCommandList == nullptr)
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
        }

        for (const PassResourceTransition& transition : planIt->second.InputTransitions)
        {
            if (!m_QueueScheduler.WasLastWrittenBy(transition.Id, RenderPassQueue::Direct))
            {
                continue;
            }

            const auto& resource = m_ResourcePool->GetResource(transition.Id);
            resource.ForEachResourceRecursive([this, &transition](const Resource& nestedResource)
            {
                m_ResourceStateTracker.TransitionBarrier(
                    nestedResource,
                    GetQueueResourceState(RenderPassQueue::AsyncCompute, transition.StateAfter));
            });
        }

        for (const ResourceId outputId : planIt->second.AliasingOutputs)
        {
            const auto& resource = m_ResourcePool->GetResource(outputId);
            resource.ForEachResourceRecursive([this](const Resource& nestedResource)
            {
                m_ResourceStateTracker.AliasingBarrier(nestedResource);
            });
        }

        for (const PassResourceTransition& transition : planIt->second.OutputTransitions)
        {
            const auto& resource = m_ResourcePool->GetResource(transition.Id);
            resource.ForEachResourceRecursive([this, &transition](const Resource& nestedResource)
            {
                m_ResourceStateTracker.TransitionBarrier(
                    nestedResource,
                    GetQueueResourceState(RenderPassQueue::AsyncCompute, transition.StateAfter));
            });
        }

        m_ResourceStateTracker.FlushBarriers(*directCommandList);
        pass.PrepareAsyncCompute(*directCommandList);
        m_Profiler.WriteMarker(
            RenderPassQueue::Direct,
            *directCommandList,
            "Async Prepare." + RenderGraphProfiler::NarrowPassName(pass.GetPassName()));

        const uint64_t producerFenceValue = m_QueueScheduler.SubmitDirect(directCommandList);
        m_QueueScheduler.WaitForDirectSubmissionOnAsyncCompute(producerFenceValue);
        return;
    }

    const uint64_t producerFenceValue = m_QueueScheduler.GetCrossQueueProducerFence(pass);
    if (producerFenceValue == 0)
    {
        return;
    }

    if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
    {
        if (directCommandList == nullptr)
        {
            directCommandList = m_DirectCommandQueue->GetCommandList();
        }
        m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Async Wait.Begin");
    }

    m_QueueScheduler.SubmitDirect(directCommandList);
    m_QueueScheduler.WaitForAsyncComputeSubmissionOnDirect(producerFenceValue);

    if (m_Profiler.IsQueueFrameActive(RenderPassQueue::Direct))
    {
        directCommandList = m_DirectCommandQueue->GetCommandList();
        m_Profiler.WriteMarker(RenderPassQueue::Direct, *directCommandList, "Async Wait.End");
    }
}

void RenderGraph::RenderGraphCommandExecutor::PrepareResourcesForRenderPass(
    CommandList& commandList,
    const RenderPass& renderPass,
    const uint32_t renderPassIndex,
    RenderContext& context,
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans,
    const bool skipAliasingOutputs)
{
    const auto planIt = resourceStatePlans.find(&renderPass);
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    const PassResourceStatePlan& resourceStatePlan = planIt->second;

    for (const PassResourceTransition& transition : resourceStatePlan.InputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([this, &renderPass, &transition](const Resource& nestedResource)
        {
            m_ResourceStateTracker.TransitionBarrier(
                nestedResource,
                GetQueueResourceState(renderPass.GetQueue(), transition.StateAfter));
            if (transition.InsertUavBarrier)
            {
                m_ResourceStateTracker.UavBarrier(nestedResource);
            }
        });
    }

    if (!skipAliasingOutputs)
    {
        for (const ResourceId outputId : resourceStatePlan.AliasingOutputs)
        {
            (void)renderPassIndex;
            const auto& resource = m_ResourcePool->GetResource(outputId);
            resource.ForEachResourceRecursive([this](const Resource& nestedResource)
            {
                m_ResourceStateTracker.AliasingBarrier(nestedResource);
            });
        }
    }

    const auto renderTargetIt = renderTargets.find(&renderPass);
    if (renderTargetIt != renderTargets.end())
    {
        const RenderTargetInfo& renderTargetInfo = renderTargetIt->second;
        context.SetRenderTargetInfo(renderTargetInfo);
        const auto& renderTarget = renderTargetInfo.m_RenderTarget;
        const auto& textures = renderTarget->GetTextures();

        for (size_t textureIndex = 0; textureIndex < 8; ++textureIndex)
        {
            const auto& texture = textures[textureIndex];
            if (texture != nullptr && texture->IsValid())
            {
                m_ResourceStateTracker.TransitionBarrier(*texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }

        const auto& depthStencil = renderTarget->GetTexture(DepthStencil);
        if (depthStencil != nullptr && depthStencil->IsValid())
        {
            const auto stateAfter = renderTargetInfo.m_ReadonlyDepth
                ? D3D12_RESOURCE_STATE_DEPTH_READ
                : D3D12_RESOURCE_STATE_DEPTH_WRITE;
            m_ResourceStateTracker.TransitionBarrier(*depthStencil, stateAfter);
        }
    }

    for (const PassResourceTransition& transition : resourceStatePlan.OutputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([this, &transition](const Resource& nestedResource)
        {
            m_ResourceStateTracker.TransitionBarrier(nestedResource, transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                m_ResourceStateTracker.UavBarrier(nestedResource);
            }
        });
    }

    m_ResourceStateTracker.FlushBarriers(commandList);

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
//Modify Begin:2026-08-05 by BestHui
        case Preserve:
            break;
//Modify End
        default:
            Assert(false, "Unknown resource init action.");
            break;
        }
    }

    if (renderTargetIt != renderTargets.end())
    {
        const auto& renderTarget = renderTargetIt->second.m_RenderTarget;
        commandList.SetRenderTarget(*renderTarget);
        commandList.SetAutomaticViewportAndScissorRect(*renderTarget);
    }
}
//Modify End
