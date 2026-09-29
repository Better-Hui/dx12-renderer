#include "DX12LibPCH.h"

#include "BarrierContext.h"

#include "CommandList.h"
#include "CommandListInternalAccess.h"
#include "D3D12DeviceContext.h"
#include "Resource.h"
#include "ResourceStateRegistry.h"

#include <algorithm>

//Modify Begin:2026-09-29 by Hui
BarrierContext::BarrierContext(CommandList& commandList)
    : m_CommandList(commandList)
{
}

void BarrierContext::PrepareResource(
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool uavWrite,
    const UINT subresource)
{
    Assert(resource.IsValid(), "BarrierContext cannot prepare an invalid resource.");
    CommandListInternalAccess::TrackResourceLifetime(m_CommandList, resource);
    PrepareResource(resource.GetD3D12Resource().Get(), stateAfter, uavWrite, subresource);
}

void BarrierContext::PrepareResource(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool uavWrite,
    const UINT subresource)
{
    Assert(resource != nullptr, "BarrierContext cannot prepare a null resource.");
    if (m_CommandList.IsExternalCommandList())
        Assert(HasExternalDeclaration(resource, subresource), "Resource access is outside the external declaration.");
    if (stateAfter == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        const auto previous = m_UavAccesses.find(resource);
        // A command list's first UAV use may follow writes in a previous list.
        if (previous == m_UavAccesses.end() ||
            (previous->second.Operation != m_OperationSerial && previous->second.Writes))
            Uav(resource);
        auto& access = m_UavAccesses[resource];
        access = { m_OperationSerial, uavWrite || (access.Operation == m_OperationSerial && access.Writes) };
    }

    Transition(resource, stateAfter, false, subresource);
    SetLocalResourceState(resource, subresource, { stateAfter, uavWrite });
}

void BarrierContext::Transition(
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool uavBefore,
    const UINT subresource)
{
    Assert(resource.IsValid(), "BarrierContext cannot transition an invalid resource.");
    CommandListInternalAccess::TrackResourceLifetime(m_CommandList, resource);
    Transition(resource.GetD3D12Resource().Get(), stateAfter, uavBefore, subresource);
}

void BarrierContext::Transition(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool uavBefore,
    const UINT subresource)
{
    Assert(resource != nullptr, "BarrierContext cannot transition a null resource.");
    Assert(m_CommandList.GetActiveBarrierContext() == this, "Cannot use an inactive barrier context.");
    if (m_CommandList.IsExternalCommandList())
    {
        Assert(
            HasExternalDeclaration(resource, subresource),
            "The external transition is not covered by a resource declaration.");
    }
    if (uavBefore)
    {
        Uav(resource);
    }
    D3D12_HEAP_PROPERTIES heap{};
    D3D12_HEAP_FLAGS flags{};
    if (SUCCEEDED(resource->GetHeapProperties(&heap, &flags)))
    {
        if (heap.Type == D3D12_HEAP_TYPE_UPLOAD)
        {
            Assert((stateAfter & ~D3D12_RESOURCE_STATE_GENERIC_READ) == 0, "Upload memory has a fixed GENERIC_READ state.");
            SetLocalResourceState(resource, subresource, { D3D12_RESOURCE_STATE_GENERIC_READ, false });
            return;
        }
        if (heap.Type == D3D12_HEAP_TYPE_READBACK)
        {
            Assert(stateAfter == D3D12_RESOURCE_STATE_COPY_DEST, "Readback memory has a fixed COPY_DEST state.");
            SetLocalResourceState(resource, subresource, { D3D12_RESOURCE_STATE_COPY_DEST, false });
            return;
        }
    }
    CommandListInternalAccess::TransitionBarrier(
        m_CommandList,
        Microsoft::WRL::ComPtr<ID3D12Resource>(resource),
        stateAfter,
        subresource);
    // The transition updates the tracker's command-list state exactly once.
    // This cache describes local access intent, not a second final-state owner.
    SetLocalResourceState(resource, subresource, { stateAfter, false });
}

void BarrierContext::RegisterExternalResource(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES initialState,
    const UINT subresource)
{
    Assert(m_CommandList.IsExternalCommandList(),
        "Explicit external resource registration requires an external command list.");
    Assert(resource != nullptr, "Cannot register a null external resource.");
    const ResourceSubresourceKey key{ resource, subresource };
    const auto existing = m_ExternalInitialStates.find(key);
    if (existing != m_ExternalInitialStates.end())
    {
        Assert(
            existing->second == initialState,
            "An external resource was declared with conflicting initial states.");
        return;
    }
    for (const auto& [declaredKey, declaredState] : m_ExternalInitialStates)
    {
        static_cast<void>(declaredState);
        const bool overlaps = declaredKey.Resource == resource &&
            (declaredKey.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
             subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
             declaredKey.Subresource == subresource);
        Assert(
            !overlaps,
            "Overlapping external resource declarations are not allowed.");
    }
    const auto deviceContext = m_CommandList.GetDeviceContext();
    Assert(deviceContext != nullptr, "External command list has no device context.");
    deviceContext->GetResourceStateRegistry()->SetResourceState(resource, initialState, subresource);
    CommandListInternalAccess::NotifyResourceState(m_CommandList, resource, initialState, subresource);
    SetLocalResourceState(resource, subresource, { initialState, false });
    m_ExternalInitialStates.emplace(key, initialState);
}

void BarrierContext::DeclareExternalResource(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES initialState,
    const D3D12_RESOURCE_STATES firstState,
    const bool uavWrite,
    const UINT subresource)
{
    RegisterExternalResource(resource, initialState, subresource);
    if (initialState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
        firstState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        // The host's last UAV access is not tracked by this command list.
        // A same-state transition would be elided, so order it explicitly.
        Uav(resource);
    }
    PrepareResource(resource, firstState, uavWrite, subresource);
}

void BarrierContext::Uav(const Resource& resource)
{
    Assert(resource.IsValid(), "BarrierContext cannot add a UAV barrier for an invalid resource.");
    Uav(resource.GetD3D12Resource().Get());
}

void BarrierContext::Uav(ID3D12Resource* const resource)
{
    Assert(resource != nullptr, "BarrierContext cannot add a UAV barrier for a null resource.");
    Assert(m_CommandList.GetActiveBarrierContext() == this, "Cannot use an inactive barrier context.");
    Assert(m_CommandList.GetCommandListType() != D3D12_COMMAND_LIST_TYPE_COPY,
        "UAV ordering requires a direct or compute command list.");
    if (m_CommandList.IsExternalCommandList())
    {
        Assert(
            std::any_of(m_ExternalInitialStates.begin(), m_ExternalInitialStates.end(),
                [resource](const auto& entry) { return entry.first.Resource == resource; }),
            "External resources must be declared before recording a UAV barrier.");
    }
    CommandListInternalAccess::UavBarrier(m_CommandList, resource);
    m_UavAccesses[resource] = { m_OperationSerial, false };
}

void BarrierContext::AliasingBeforeFirstUse(const Resource& resource)
{
    Assert(resource.IsValid(), "BarrierContext cannot alias an invalid resource.");
    CommandListInternalAccess::AliasingBarrierBeforeFirstUse(m_CommandList, resource);
}

void BarrierContext::Flush()
{
    CommandListInternalAccess::FlushResourceBarriers(m_CommandList);
}

void BarrierContext::Reset()
{
    m_LocalResourceStates.clear();
    m_ExternalInitialStates.clear();
    m_UavAccesses.clear();
    m_OperationSerial = 0;
}

void BarrierContext::CommitExternalResourceStates()
{
    Assert(m_CommandList.IsExternalCommandList(),
        "Only external command lists can commit external resource states.");
    auto registry = m_CommandList.GetDeviceContext()->GetResourceStateRegistry();
    auto submissionScope = registry->AcquireSubmissionScope();
    m_CommandList.FlushExternalResourceBarriers(submissionScope);
}

bool BarrierContext::TryGetTrackedResourceState(
    ID3D12Resource* const resource,
    D3D12_RESOURCE_STATES& state,
    bool& uavWrite,
    const UINT subresource) const noexcept
{
    const LocalResourceState* tracked = FindLocalResourceState(
        resource,
        subresource);
    if (tracked == nullptr)
    {
        return false;
    }

    if (!CommandListInternalAccess::TryGetResourceState(m_CommandList, resource, subresource, state))
        return false;
    uavWrite = tracked->UavWrite;
    return true;
}

const BarrierContext::LocalResourceState* BarrierContext::FindLocalResourceState(
    ID3D12Resource* const resource,
    const UINT subresource) const noexcept
{
    const ResourceSubresourceKey exact{ resource, subresource };
    if (const auto iterator = m_LocalResourceStates.find(exact);
        iterator != m_LocalResourceStates.end())
    {
        return &iterator->second;
    }

    if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        const ResourceSubresourceKey all{ resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES };
        if (const auto iterator = m_LocalResourceStates.find(all);
            iterator != m_LocalResourceStates.end())
        {
            return &iterator->second;
        }
    }
    return nullptr;
}

bool BarrierContext::HasExternalDeclaration(
    ID3D12Resource* const resource,
    const UINT subresource) const noexcept
{
    return m_ExternalInitialStates.contains({ resource, subresource }) ||
        (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
            m_ExternalInitialStates.contains({ resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES }));
}

void BarrierContext::SetLocalResourceState(
    ID3D12Resource* const resource,
    const UINT subresource,
    const LocalResourceState state)
{
    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        std::erase_if(
            m_LocalResourceStates,
            [resource](const auto& entry) { return entry.first.Resource == resource; });
    }
    m_LocalResourceStates[{ resource, subresource }] = state;
}
//Modify End
