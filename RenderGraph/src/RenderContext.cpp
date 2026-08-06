//Modify Begin:2026-07-30 by BestHui
#include "RenderContext.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

#include "RenderGraphResourceStateTracker.h"

//Modify Begin:2026-07-30 by BestHui
const std::shared_ptr<Texture>& RenderGraph::RenderContext::GetTexture(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetTexture(resourceId);
}

const std::shared_ptr<Buffer>& RenderGraph::RenderContext::GetBuffer(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetBuffer(resourceId);
}

const Resource& RenderGraph::RenderContext::GetResource(const ResourceId resourceId) const
{
    Assert(m_ResourcePool != nullptr, "Render context has no resource pool.");
    return m_ResourcePool->GetResource(resourceId);
}
//Modify End

void RenderGraph::RenderContext::TransitionResource(
    CommandList& commandList,
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter) const
{
    const ResourceStateTransition transition = { &resource, stateAfter };
    TransitionResources(commandList, std::span(&transition, 1));
}

void RenderGraph::RenderContext::TransitionResources(
    CommandList& commandList,
    const std::span<const ResourceStateTransition> transitions) const
{
    Assert(m_ResourceStateTracker != nullptr, "Render context has no resource state tracker.");
    for (const ResourceStateTransition& transition : transitions)
    {
        Assert(transition.Target != nullptr, "Resource state transition has no resource.");
        m_ResourceStateTracker->TransitionBarrier(*transition.Target, transition.StateAfter);
    }
    m_ResourceStateTracker->FlushBarriers(commandList);
}
//Modify End
