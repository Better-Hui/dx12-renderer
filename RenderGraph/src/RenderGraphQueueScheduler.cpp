//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphQueueScheduler.h"

#include <algorithm>
#include <utility>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>

RenderGraph::RenderGraphQueueScheduler::RenderGraphQueueScheduler(
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue)
    : m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
{
    Assert(m_DirectCommandQueue != nullptr, "Render graph requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Render graph requires an async compute command queue.");
}

void RenderGraph::RenderGraphQueueScheduler::BeginFrame()
{
    if (m_AsyncComputeSubmitted)
    {
        m_DirectCommandQueue->Wait(*m_AsyncComputeCommandQueue, m_LastAsyncComputeFenceValue);
    }

    m_LastWriterQueues.clear();
    m_LastWriterFenceValues.clear();
    m_ResourceRetirements.clear();
    m_FrameSubmissionFences = {};
    m_PendingDirectResources.clear();
    m_AsyncComputeSubmitted = false;
    m_LastAsyncComputeFenceValue = 0;
}

uint64_t RenderGraph::RenderGraphQueueScheduler::SubmitDirect(std::shared_ptr<CommandList>& commandList)
{
    if (commandList == nullptr)
    {
        return 0;
    }

    const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandList(commandList);
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
    commandList.reset();
    return fenceValue;
}

//Modify Begin:2026-07-30 by BestHui
uint64_t RenderGraph::RenderGraphQueueScheduler::SubmitDirect(
    std::vector<std::shared_ptr<CommandList>>& commandLists)
{
    if (commandLists.empty())
    {
        return 0;
    }

    const uint64_t fenceValue = m_DirectCommandQueue->ExecuteCommandLists(commandLists);
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
    commandLists.clear();
    return fenceValue;
}
//Modify End

uint64_t RenderGraph::RenderGraphQueueScheduler::SubmitAsyncCompute(
    std::shared_ptr<CommandList>& commandList,
    const bool waitForCompletion)
{
    Assert(commandList != nullptr, "Cannot submit a null async compute command list.");
    const uint64_t fenceValue = m_AsyncComputeCommandQueue->ExecuteCommandList(commandList);
    m_FrameSubmissionFences.AsyncCompute = (std::max)(m_FrameSubmissionFences.AsyncCompute, fenceValue);
    commandList.reset();

    if (waitForCompletion)
    {
        m_AsyncComputeCommandQueue->WaitForFenceValue(fenceValue);
    }

    m_AsyncComputeSubmitted = true;
    m_LastAsyncComputeFenceValue = fenceValue;
    return fenceValue;
}

uint64_t RenderGraph::RenderGraphQueueScheduler::GetCrossQueueProducerFence(const RenderPass& pass) const
{
    uint64_t producerFenceValue = 0;
    const auto inspectResource = [&](const ResourceId resourceId)
    {
        const auto writer = m_LastWriterQueues.find(resourceId);
        if (writer == m_LastWriterQueues.end() || writer->second == pass.GetQueue())
        {
            return;
        }

        const auto fence = m_LastWriterFenceValues.find(resourceId);
        Assert(fence != m_LastWriterFenceValues.end(), "Render pass writer fence was not recorded.");
        Assert(fence->second != 0, "Cross-queue producer has not been submitted.");
        producerFenceValue = (std::max)(producerFenceValue, fence->second);
    };

    for (const Input& input : pass.GetInputs())
    {
        inspectResource(input.m_Id);
    }
    for (const Output& output : pass.GetOutputs())
    {
        inspectResource(output.m_Id);
    }
    return producerFenceValue;
}

bool RenderGraph::RenderGraphQueueScheduler::WasLastWrittenBy(
    const ResourceId resourceId,
    const RenderPassQueue queue) const
{
    const auto writer = m_LastWriterQueues.find(resourceId);
    return writer != m_LastWriterQueues.end() && writer->second == queue;
}

void RenderGraph::RenderGraphQueueScheduler::WaitForDirectSubmissionOnAsyncCompute(const uint64_t fenceValue) const
{
    if (fenceValue != 0)
    {
        m_AsyncComputeCommandQueue->Wait(*m_DirectCommandQueue, fenceValue);
    }
}

void RenderGraph::RenderGraphQueueScheduler::WaitForAsyncComputeSubmissionOnDirect(const uint64_t fenceValue)
{
    if (fenceValue == 0)
    {
        return;
    }

    m_DirectCommandQueue->Wait(*m_AsyncComputeCommandQueue, fenceValue);
    if (m_AsyncComputeSubmitted && fenceValue >= m_LastAsyncComputeFenceValue)
    {
        m_AsyncComputeSubmitted = false;
        m_LastAsyncComputeFenceValue = 0;
    }
}

void RenderGraph::RenderGraphQueueScheduler::TrackPassResources(
    const RenderPass& pass,
    const uint64_t fenceValue)
{
    const auto trackResource = [this, &pass, fenceValue](const ResourceId resourceId)
    {
        if (pass.GetQueue() == RenderPassQueue::AsyncCompute)
        {
            Assert(fenceValue != 0, "Async compute resource usage requires a submitted fence value.");
            m_ResourceRetirements[resourceId].AsyncCompute = (std::max)(
                m_ResourceRetirements[resourceId].AsyncCompute,
                fenceValue);
        }
        else
        {
            m_PendingDirectResources.insert(resourceId);
        }
    };

    for (const Input& input : pass.GetInputs())
    {
        trackResource(input.m_Id);
    }

    for (const Output& output : pass.GetOutputs())
    {
        trackResource(output.m_Id);
        m_LastWriterQueues[output.m_Id] = pass.GetQueue();
        m_LastWriterFenceValues[output.m_Id] = fenceValue;
    }
}

void RenderGraph::RenderGraphQueueScheduler::TrackExternalResource(
    const ResourceId resourceId,
    const RenderPassQueue queue)
{
    if (queue == RenderPassQueue::Direct)
    {
        m_PendingDirectResources.insert(resourceId);
        return;
    }

    Assert(false, "External async compute resource tracking requires an explicit fence value.");
}

const std::map<RenderGraph::ResourceId, RenderGraph::RenderGraphQueueFenceValues>&
RenderGraph::RenderGraphQueueScheduler::GetResourceRetirements() const
{
    return m_ResourceRetirements;
}

const RenderGraph::RenderGraphQueueFenceValues&
RenderGraph::RenderGraphQueueScheduler::GetFrameSubmissionFences() const
{
    return m_FrameSubmissionFences;
}
//Modify End
