//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphResourceStateTracker.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

#include <d3dx12.h>

void RenderGraph::RenderGraphResourceStateTracker::Reset()
{
    Assert(m_PendingBarriers.empty(), "Cannot reset resource states while barriers are pending.");
    m_ResourceStates.clear();
}

D3D12_RESOURCE_STATES RenderGraph::RenderGraphResourceStateTracker::GetCurrentResourceState(const Resource& resource) const
{
    const auto result = m_ResourceStates.find(&resource);
    Assert(result != m_ResourceStates.end(), "Resource does not have a registered state");
    return result->second;
}

void RenderGraph::RenderGraphResourceStateTracker::SetCurrentResourceState(
    const Resource& resource,
    const D3D12_RESOURCE_STATES state)
{
    m_ResourceStates[&resource] = state;
}

void RenderGraph::RenderGraphResourceStateTracker::TransitionBarrier(
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter)
{
    const D3D12_RESOURCE_STATES stateBefore = GetCurrentResourceState(resource);
    if (stateBefore == stateAfter)
    {
        return;
    }

    m_PendingBarriers.push_back(
        CD3DX12_RESOURCE_BARRIER::Transition(
            resource.GetD3D12Resource().Get(),
            stateBefore,
            stateAfter));
    SetCurrentResourceState(resource, stateAfter);
}

void RenderGraph::RenderGraphResourceStateTracker::UavBarrier(const Resource& resource)
{
    Assert(
        GetCurrentResourceState(resource) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        "Resource is supposed to be in UAV state to issue a UAV barrier.");

    m_PendingBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(resource.GetD3D12Resource().Get()));
}

void RenderGraph::RenderGraphResourceStateTracker::AliasingBarrier(const Resource& resourceAfter)
{
    m_PendingBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Aliasing(nullptr, resourceAfter.GetD3D12Resource().Get()));
}

void RenderGraph::RenderGraphResourceStateTracker::FlushBarriers(const CommandList& commandList)
{
    if (m_PendingBarriers.empty())
    {
        return;
    }

    commandList.GetGraphicsCommandList()->ResourceBarrier(
        static_cast<UINT>(m_PendingBarriers.size()),
        m_PendingBarriers.data());
    m_PendingBarriers.clear();
}
//Modify End
