//Modify Begin:2026-08-18 by Hui
#include "RenderGraphCommandExecutor.h"

#include "RenderGraphProfiler.h"
#include "RenderGraphQueueScheduler.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandListInternalAccess.h>
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

    for (const RenderGraphRecordingBatch& recordingBatch : recordingBatches)
    {
        Assert(!recordingBatch.Passes.empty(), "Render-graph recording batch cannot be empty.");
        Assert(recordingBatch.Passes.front()->GetQueue() == recordingBatch.Queue,
            "Render-graph recording batch queue does not match its passes.");

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

        if (enableParallelDirectRecording && recordingBatch.RecordInParallel)
        {
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
            Assert(renderPass != nullptr, "Direct recording batch contains a null pass.");
            Assert(renderPass->GetQueue() == RenderPassQueue::Direct,
                "Direct recording batch contains a non-direct pass.");
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

void RenderGraph::RenderGraphCommandExecutor::PrepareDirectQueueDependencies(
    const std::span<RenderPass* const> passes,
    std::shared_ptr<CommandList>& directCommandList)
{
    Assert(!passes.empty(), "Direct queue dependency preparation requires at least one pass.");

    RenderGraphQueueFenceValues producerFences;
    for (const RenderPass* pass : passes)
    {
        Assert(pass != nullptr, "Direct queue dependency preparation received a null pass.");
        Assert(pass->GetQueue() == RenderPassQueue::Direct, "Only direct passes can enter a parallel recording batch.");
        producerFences.Merge(m_QueueScheduler.GetCrossQueueProducerFences(
            *pass,
            RenderPassQueue::Direct));
    }
    if (producerFences.IsEmpty())
    {
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
    const std::map<const RenderPass*, RenderTargetInfo>& renderTargets,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
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
        context.SetRenderTargetInfo({});
        try
        {
            PrepareResourcesForRenderPass(
                *commandList,
                *pass,
                context,
                renderTargets,
                resourceStatePlans);
            PIXScope(*commandList, pass->GetPassName().c_str());
            pass->Execute(context, *commandList);
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
        m_QueueScheduler.TrackPassResources(*pass, fenceValue);
    }
    if (containsLastQueuePass)
    {
        m_Profiler.EndQueueFrame(batch.Queue, fenceValue);
    }
}

void RenderGraph::RenderGraphCommandExecutor::PrepareNonDirectBatchDependencies(
    const RenderGraphRecordingBatch& batch,
    std::shared_ptr<CommandList>& directCommandList,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(!batch.Passes.empty(), "Non-direct dependency preparation requires at least one pass.");
    Assert(batch.Queue != RenderPassQueue::Direct, "Non-direct dependency preparation cannot target the direct queue.");

    RenderGraphQueueFenceValues producerFences;
    for (const RenderPass* pass : batch.Passes)
    {
        Assert(pass != nullptr && pass->GetQueue() == batch.Queue,
            "Non-direct dependency preparation received an invalid pass.");
        producerFences.Merge(m_QueueScheduler.GetCrossQueueProducerFences(
            *pass,
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
}

void RenderGraph::RenderGraphCommandExecutor::ApplyDirectQueuePreamble(
    const RenderPass& pass,
    CommandList& commandList,
    const std::map<const RenderPass*, PassResourceStatePlan>& resourceStatePlans)
{
    Assert(pass.GetQueue() != RenderPassQueue::Direct,
        "Direct-queue preambles are only generated for non-direct passes.");
    const auto planIt = resourceStatePlans.find(&pass);
    Assert(planIt != resourceStatePlans.end(), "Render pass resource state plan was not built.");
    Assert(planIt->second.DirectPreamble.has_value(), "Non-direct render pass has no direct-queue preamble plan.");
    const PassResourceStatePlan::NonDirectQueuePreamble& directPreamble = *planIt->second.DirectPreamble;

    for (const PassResourceTransition& transition : directPreamble.CrossQueueInputTransitions)
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

    ApplyExternalResourceTransitions(commandList, directPreamble.ExternalResourceTransitions);

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
        "Queue Prepare." + RenderGraphProfiler::NarrowPassName(pass.GetPassName()));
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

    ApplyExternalResourceTransitions(commandList, resourceStatePlan.ExternalResourceTransitions);

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

    CommandListInternalAccess::FlushResourceBarriers(commandList);

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
//Modify End
