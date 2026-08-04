//Modify Begin:2026-07-30 by BestHui
#include "RenderContext.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

#include "RenderGraphResourceStateTracker.h"

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
