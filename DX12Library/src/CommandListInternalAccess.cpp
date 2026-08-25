//Modify Begin:2026-08-24 by Hui
#include "DX12LibPCH.h"

#include "CommandListInternalAccess.h"

#include "Resource.h"
#include "ResourceStateTracker.h"

void CommandListInternalAccess::TransitionBarrier(
    CommandList& commandList,
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const UINT subresource,
    const bool flushBarriers)
{
    TransitionBarrier(
        commandList,
        resource.GetD3D12Resource(),
        stateAfter,
        subresource,
        flushBarriers);
}

void CommandListInternalAccess::TransitionBarrier(
    CommandList& commandList,
    Microsoft::WRL::ComPtr<ID3D12Resource> resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const UINT subresource,
    const bool flushBarriers)
{
    Assert(resource != nullptr, "Cannot transition a null D3D12 resource.");
    commandList.m_PResourceStateTracker->ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Transition(
        resource.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        stateAfter,
        subresource));
    if (flushBarriers)
    {
        FlushResourceBarriers(commandList);
    }
}

void CommandListInternalAccess::UavBarrier(
    CommandList& commandList,
    const Resource& resource,
    const bool flushBarriers)
{
    UavBarrier(commandList, resource.GetD3D12Resource().Get(), flushBarriers);
}

void CommandListInternalAccess::UavBarrier(
    CommandList& commandList,
    ID3D12Resource* resource,
    const bool flushBarriers)
{
    Assert(resource != nullptr, "Cannot add a UAV barrier for a null D3D12 resource.");
    commandList.m_PResourceStateTracker->ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(resource));
    if (flushBarriers)
    {
        FlushResourceBarriers(commandList);
    }
}

void CommandListInternalAccess::AliasingBarrier(
    CommandList& commandList,
    const Resource& beforeResource,
    const Resource& afterResource,
    const bool flushBarriers)
{
    AliasingBarrier(
        commandList,
        beforeResource.GetD3D12Resource(),
        afterResource.GetD3D12Resource(),
        flushBarriers);
}

void CommandListInternalAccess::AliasingBarrier(
    CommandList& commandList,
    Microsoft::WRL::ComPtr<ID3D12Resource> beforeResource,
    Microsoft::WRL::ComPtr<ID3D12Resource> afterResource,
    const bool flushBarriers)
{
    commandList.m_PResourceStateTracker->ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(
        beforeResource.Get(),
        afterResource.Get()));
    if (flushBarriers)
    {
        FlushResourceBarriers(commandList);
    }
}

void CommandListInternalAccess::AliasingBarrierBeforeFirstUse(
    CommandList& commandList,
    const Resource& resourceAfter)
{
    commandList.m_PResourceStateTracker->AliasBarrier(nullptr, &resourceAfter);
    commandList.m_PResourceStateTracker->NotifyResourceState(
        resourceAfter.GetD3D12Resource().Get(),
        D3D12_RESOURCE_STATE_COMMON);
}
//Modify End
