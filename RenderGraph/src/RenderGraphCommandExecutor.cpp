//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphCommandExecutor.h"

#include "RenderGraphProfiler.h"
#include "RenderGraphQueueScheduler.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/RenderTarget.h>
#include <DX12Library/Resource.h>
#include <DX12Library/Texture.h>

#include <algorithm>
#include <exception>
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

}

RenderGraph::RenderGraphCommandExecutor::RenderGraphCommandExecutor(
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
    std::shared_ptr<ResourcePool> resourcePool,
    RenderGraphQueueScheduler& queueScheduler,
    RenderGraphProfiler& profiler)
    : m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
    , m_ResourcePool(std::move(resourcePool))
    , m_QueueScheduler(queueScheduler)
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
    const std::vector<RenderGraphRecordingBatch>& recordingBatches = compiledGraph.GetRecordingBatches();
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets = compiledGraph.GetRenderTargets();
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans = compiledGraph.GetResourceStatePlans();
    m_QueueScheduler.BeginFrame();

    auto directCommandList = m_DirectCommandQueue->GetCommandList();

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

    for (const RenderGraphRecordingBatch& recordingBatch : recordingBatches)
    {
        if (enableParallelDirectRecording && recordingBatch.RecordInParallel)
        {
            Assert(!recordingBatch.Passes.empty(), "Parallel recording batch cannot be empty.");
            PrepareDirectQueueDependencies(
                recordingBatch.Passes,
                directCommandList);
            ExecuteParallelDirectBatch(
                recordingBatch,
                renderMetadata,
                directCommandList,
                renderTargets,
                resourceStatePlans);
            continue;
        }

        for (RenderPass* renderPass : recordingBatch.Passes)
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
//Modify Begin:2026-08-13 by BestHui
            for (const ExternalResourceAccess& access : renderPass->GetExternalResourceAccesses())
            {
                Assert(access.Mode == ExternalResourceAccessMode::Read,
                    "Async compute external resources are read-only; write outputs must be RenderGraph resources.");
            }
//Modify End

            PrepareAsyncComputeDependency(*renderPass, directCommandList, resourceStatePlans);

            auto computeCommandList = m_AsyncComputeCommandQueue->GetCommandList();
            if (!m_Profiler.IsQueueFrameActive(RenderPassQueue::AsyncCompute))
            {
                m_Profiler.BeginQueueFrame(
                    RenderPassQueue::AsyncCompute,
                    renderMetadata.m_FrameIndex,
                    *computeCommandList);
            }

            context.SetRenderTargetInfo({});
            try
            {
                PrepareResourcesForRenderPass(
                    *computeCommandList,
                    *renderPass,
                    context,
                    renderTargets,
                    resourceStatePlans);
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
            continue;
        }

        PrepareDirectQueueDependencies(
            std::span<RenderPass* const>(&renderPass, 1u),
            directCommandList);
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
            try
            {
                PrepareResourcesForRenderPass(
                    commandList,
                    *renderPass,
                    context,
                    renderTargets,
                    resourceStatePlans);
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
    const RenderGraphRecordingBatch& batch,
    const RenderMetadata& renderMetadata,
    std::shared_ptr<CommandList>& directCommandList,
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(batch.Passes.size() > 1u, "Parallel recording batches require at least two passes.");
    for (const RenderPass* renderPass : batch.Passes)
    {
        Assert(renderPass != nullptr, "Parallel recording batch contains a null pass.");
        Assert(renderPass->GetQueue() == RenderPassQueue::Direct, "Parallel recording only supports the direct queue.");
        Assert(!renderPass->IsExternal(), "External passes cannot record in parallel.");
        Assert(renderPass->IsParallelRecordingEligible(), "Parallel recording batch contains an ineligible pass.");
    }

    std::vector<std::future<std::shared_ptr<CommandList>>> recordingTasks;
    recordingTasks.reserve(batch.Passes.size());
    for (uint32_t passOffset = 0u; passOffset < batch.Passes.size(); ++passOffset)
    {
        RenderPass* renderPass = batch.Passes[passOffset];
        recordingTasks.push_back(m_ParallelRecordingTaskScheduler.Enqueue(
            [this, renderPass, &renderMetadata, &renderTargets, &resourceStatePlans]()
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
                    PrepareResourcesForRenderPass(
                        *commandList,
                        *renderPass,
                        context,
                        renderTargets,
                        resourceStatePlans);
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
    recordedCommandLists.reserve(recordingTasks.size() + (directCommandList != nullptr ? 1u : 0u));
    if (directCommandList != nullptr)
    {
        recordedCommandLists.push_back(std::move(directCommandList));
    }

    std::exception_ptr recordingFailure;
    for (uint32_t passOffset = 0; passOffset < recordingTasks.size(); ++passOffset)
    {
        std::shared_ptr<CommandList> commandList;
        try
        {
            commandList = recordingTasks[passOffset].get();
        }
        catch (...)
        {
            if (recordingFailure == nullptr)
            {
                recordingFailure = std::current_exception();
            }
            continue;
        }

        m_Profiler.WritePassTimestamp(
            RenderPassQueue::Direct,
            *commandList,
            batch.Passes[passOffset]->GetPassName());
        m_QueueScheduler.TrackPassResources(*batch.Passes[passOffset], 0u);
        recordedCommandLists.push_back(std::move(commandList));
    }

    if (recordingFailure != nullptr)
    {
        std::rethrow_exception(recordingFailure);
    }

    m_QueueScheduler.SubmitDirect(recordedCommandLists);
}
//Modify End

void RenderGraph::RenderGraphCommandExecutor::PrepareDirectQueueDependencies(
    const std::span<RenderPass* const> passes,
    std::shared_ptr<CommandList>& directCommandList)
{
    Assert(!passes.empty(), "Direct queue dependency preparation requires at least one pass.");

    uint64_t producerFenceValue = 0u;
    for (const RenderPass* pass : passes)
    {
        Assert(pass != nullptr, "Direct queue dependency preparation received a null pass.");
        Assert(pass->GetQueue() == RenderPassQueue::Direct, "Only direct passes can enter a parallel recording batch.");
        producerFenceValue = (std::max)(
            producerFenceValue,
            m_QueueScheduler.GetCrossQueueProducerFence(*pass));
    }
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

void RenderGraph::RenderGraphCommandExecutor::PrepareAsyncComputeDependency(
    const RenderPass& pass,
    std::shared_ptr<CommandList>& directCommandList,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(pass.GetQueue() == RenderPassQueue::AsyncCompute, "Async compute dependency preparation requires an async compute pass.");
    const auto planIt = resourceStatePlans.find(&pass);
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    Assert(planIt->second.DirectPreamble.has_value(), "Async compute render pass has no direct queue preamble plan.");
    const PassResourceStatePlan::AsyncComputeDirectPreamble& directPreamble = *planIt->second.DirectPreamble;

    if (directCommandList == nullptr)
    {
        directCommandList = m_DirectCommandQueue->GetCommandList();
    }

    CommandList& commandList = *directCommandList;
    for (const PassResourceTransition& transition : directPreamble.DirectProducerInputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([&commandList, &transition](const Resource& nestedResource)
        {
            commandList.TransitionBarrier(
                nestedResource,
                transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                commandList.UavBarrier(nestedResource);
            }
        });
    }

//Modify Begin:2026-08-13 by BestHui
    ApplyExternalResourceTransitions(commandList, directPreamble.ExternalResourceTransitions);
//Modify End

    for (const ResourceId outputId : directPreamble.AliasingOutputs)
    {
        const auto& resource = m_ResourcePool->GetResource(outputId);
        resource.ForEachResourceRecursive([&commandList](const Resource& nestedResource)
        {
            commandList.AliasingBarrierBeforeFirstUse(nestedResource);
        });
    }

    for (const PassResourceTransition& transition : directPreamble.OutputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([&commandList, &transition](const Resource& nestedResource)
        {
            commandList.TransitionBarrier(
                nestedResource,
                transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                commandList.UavBarrier(nestedResource);
            }
        });
    }

    m_Profiler.WriteMarker(
        RenderPassQueue::Direct,
        commandList,
        "Async Prepare." + RenderGraphProfiler::NarrowPassName(pass.GetPassName()));

    const uint64_t producerFenceValue = m_QueueScheduler.SubmitDirect(directCommandList);
    m_QueueScheduler.WaitForDirectSubmissionOnAsyncCompute(producerFenceValue);
}

//Modify Begin:2026-08-13 by BestHui
void RenderGraph::RenderGraphCommandExecutor::ApplyExternalResourceTransitions(
    CommandList& commandList,
    const std::span<const PassExternalResourceTransition> transitions)
{
    for (const PassExternalResourceTransition& transition : transitions)
    {
        Assert(transition.Resource != nullptr && transition.Resource->IsValid(),
            "Render pass external resource transition must reference an initialized resource.");
        commandList.TransitionBarrier(*transition.Resource, transition.StateAfter);
        if (transition.InsertUavBarrier)
        {
            commandList.UavBarrier(*transition.Resource);
        }
    }
}
//Modify End

void RenderGraph::RenderGraphCommandExecutor::PrepareResourcesForRenderPass(
    CommandList& commandList,
    const RenderPass& renderPass,
    RenderContext& context,
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    const auto planIt = resourceStatePlans.find(&renderPass);
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    const PassResourceStatePlan& resourceStatePlan = planIt->second;

    for (const PassResourceTransition& transition : resourceStatePlan.InputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([&commandList, &renderPass, &transition](const Resource& nestedResource)
        {
            commandList.TransitionBarrier(
                nestedResource,
                transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                commandList.UavBarrier(nestedResource);
            }
        });
    }

//Modify Begin:2026-08-13 by BestHui
    ApplyExternalResourceTransitions(commandList, resourceStatePlan.ExternalResourceTransitions);
//Modify End

    for (const ResourceId outputId : resourceStatePlan.AliasingOutputs)
    {
        const auto& resource = m_ResourcePool->GetResource(outputId);
        resource.ForEachResourceRecursive([&commandList](const Resource& nestedResource)
        {
            commandList.AliasingBarrierBeforeFirstUse(nestedResource);
        });
    }

    for (const PassResourceTransition& transition : resourceStatePlan.OutputTransitions)
    {
        const auto& resource = m_ResourcePool->GetResource(transition.Id);
        resource.ForEachResourceRecursive([&commandList, &transition](const Resource& nestedResource)
        {
            commandList.TransitionBarrier(nestedResource, transition.StateAfter);
            if (transition.InsertUavBarrier)
            {
                commandList.UavBarrier(nestedResource);
            }
        });
    }

    const auto renderTargetIt = renderTargets.find(&renderPass);
    if (renderTargetIt != renderTargets.end())
    {
        const RenderTargetInfo& renderTargetInfo = renderTargetIt->second;
        context.SetRenderTargetInfo(renderTargetInfo);
        const auto& renderTarget = renderTargetInfo.m_RenderTarget;
        const auto& textures = renderTarget->GetTextures();
        for (uint32_t textureIndex = 0u; textureIndex < 8u; ++textureIndex)
        {
            if (textures[textureIndex] != nullptr && textures[textureIndex]->IsValid())
            {
                commandList.TransitionBarrier(*textures[textureIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }

        const auto& depthStencil = renderTarget->GetTexture(DepthStencil);
        if (depthStencil != nullptr && depthStencil->IsValid())
        {
            commandList.TransitionBarrier(
                *depthStencil,
                renderTargetInfo.m_ReadonlyDepth
                    ? D3D12_RESOURCE_STATE_DEPTH_READ
                    : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
    }

    commandList.FlushResourceBarriers();

    if (renderTargetIt != renderTargets.end())
    {
        commandList.SetRenderTarget(
            *renderTargetIt->second.m_RenderTarget,
            -1,
            0,
            true,
            renderTargetIt->second.m_ReadonlyDepth);
    }

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
        commandList.SetAutomaticViewportAndScissorRect(*renderTargetIt->second.m_RenderTarget);
    }
}
//Modify End
