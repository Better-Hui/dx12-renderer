#include "DX12LibPCH.h"

#include "ExternalCommandContext.h"

#include "CommandList.h"
#include "D3D12DeviceContext.h"

#include <algorithm>

//Modify Begin:2026-09-29 by Hui
ExternalCommandContext::ExternalCommandContext(
    const D3D12_COMMAND_LIST_TYPE commandListType,
    std::shared_ptr<D3D12DeviceContext> deviceContext,
    ID3D12GraphicsCommandList2* const commandList)
    : m_CommandList(std::make_unique<CommandList>(commandListType, std::move(deviceContext), commandList))
    , m_BarrierContext(*m_CommandList)
{
    m_PreviousBarrierContext = m_CommandList->SetActiveBarrierContext(&m_BarrierContext);
}

ExternalCommandContext::~ExternalCommandContext() noexcept
{
    if (m_CommandList != nullptr && !m_Ended)
    {
        try
        {
            Abort();
        }
        catch (...)
        {
            // Destructors cannot report a barrier failure by throwing. Restore
            // the active-context stack at minimum; the host remains the owner
            // of command-list submission and can surface the device error.
            DetachBarrierContext();
            m_Ended = true;
            m_Failed = true;
        }
    }
    DetachBarrierContext();
    // The owner remains responsible for the native command-list lifetime.
    // End is intentionally explicit because it records final states.
}

CommandList& ExternalCommandContext::GetCommandList() const
{
    return *m_CommandList;
}

ID3D12GraphicsCommandList2* ExternalCommandContext::GetNativeCommandList() const
{
    return m_CommandList->GetGraphicsCommandList().Get();
}

void ExternalCommandContext::RegisterResource(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES initialState)
{
    // Keep the legacy registration entry point with an explicit round-trip
    // contract. End() restores the host's entry state even if local commands
    // temporarily transition the resource to another state.
    DeclareResource(ResourceAccess{
        .Resource = resource,
        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        .InitialState = initialState,
        .FirstState = initialState,
        .FinalState = initialState,
        .FirstAccessWrites = false,
        .FinalAccessWrites = false,
    });
}

void ExternalCommandContext::DeclareResource(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES initialState,
    const D3D12_RESOURCE_STATES firstState,
    const bool uavWrite,
    const UINT subresource)
{
    DeclareResource(ResourceAccess{
        .Resource = resource,
        .Subresource = subresource,
        .InitialState = initialState,
        .FirstState = firstState,
        .FinalState = firstState,
        .FirstAccessWrites = uavWrite,
        .FinalAccessWrites = uavWrite,
    });
}

void ExternalCommandContext::DeclareResource(const ResourceAccess& access)
{
    ValidateResourceAccess(access);

    const ResourceAccessKey key{ access.Resource, access.Subresource };
    const auto declared = m_ResourceAccesses.find(key);
    if (declared != m_ResourceAccesses.end())
    {
        const ResourceAccess& previous = declared->second;
        Assert(
            previous.InitialState == access.InitialState &&
                previous.Subresource == access.Subresource &&
                previous.FirstState == access.FirstState &&
                previous.FinalState == access.FinalState &&
                previous.FirstAccessWrites == access.FirstAccessWrites &&
                previous.FinalAccessWrites == access.FinalAccessWrites,
            "An external resource was declared with a conflicting access contract.");
        return;
    }

    for (const auto& [declaredKey, previous] : m_ResourceAccesses)
    {
        const bool overlaps = declaredKey.Resource == access.Resource &&
            (declaredKey.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
             access.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
             declaredKey.Subresource == access.Subresource);
        static_cast<void>(previous);
        Assert(!overlaps, "Overlapping external resource declarations are not allowed.");
    }

    m_BarrierContext.DeclareExternalResource(
        access.Resource,
        access.InitialState,
        access.FirstState,
        access.FirstAccessWrites && access.FirstState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        access.Subresource);
    m_BarrierContext.Flush();
    if (std::ranges::find_if(
            m_DeclaredResources,
            [&key](const ResourceAccessKey& declaredKey) { return declaredKey == key; }) ==
        m_DeclaredResources.end())
    {
        m_DeclaredResources.push_back(key);
    }
    const auto existing = std::find_if(
        m_FinalResourceStates.begin(),
        m_FinalResourceStates.end(),
        [&access](const FinalResourceState& state)
        {
            return state.Resource == access.Resource && state.Subresource == access.Subresource;
        });
    if (existing == m_FinalResourceStates.end())
    {
        m_FinalResourceStates.push_back({
            access.Resource,
            access.Subresource,
            access.FinalState,
            access.FinalAccessWrites && access.FinalState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        });
    }
    else
    {
        Assert(
            existing->State == access.FinalState &&
                existing->UavAccess ==
                    (access.FinalAccessWrites && access.FinalState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            "An external resource was declared with conflicting final states.");
    }
    m_ResourceAccesses.emplace(key, access);
}

void ExternalCommandContext::DeclareResources(const std::span<const ResourceAccess> accesses)
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    Assert(!m_RecordingStarted,
        "External resources must be declared before external recording begins.");

    // Validate the complete batch before touching the registry, barrier
    // tracker, or native command list. This keeps a failed declaration batch
    // atomic from the caller's perspective.
    std::unordered_map<ResourceAccessKey, ResourceAccess, ResourceAccessKeyHash> batch;
    batch.reserve(accesses.size());
    for (const ResourceAccess& access : accesses)
    {
        ValidateResourceAccess(access);
        const ResourceAccessKey key{ access.Resource, access.Subresource };
        const auto existing = batch.find(key);
        if (existing != batch.end())
        {
            Assert(
                existing->second.InitialState == access.InitialState &&
                    existing->second.Subresource == access.Subresource &&
                    existing->second.FirstState == access.FirstState &&
                    existing->second.FinalState == access.FinalState &&
                    existing->second.FirstAccessWrites == access.FirstAccessWrites &&
                    existing->second.FinalAccessWrites == access.FinalAccessWrites,
                "An external resource was declared with a conflicting access contract in one batch.");
            continue;
        }
        const auto declared = m_ResourceAccesses.find(key);
        if (declared != m_ResourceAccesses.end())
        {
            const ResourceAccess& previous = declared->second;
            Assert(
                previous.InitialState == access.InitialState &&
                    previous.Subresource == access.Subresource &&
                    previous.FirstState == access.FirstState &&
                    previous.FinalState == access.FinalState &&
                    previous.FirstAccessWrites == access.FirstAccessWrites &&
                    previous.FinalAccessWrites == access.FinalAccessWrites,
                "An external resource was declared with a conflicting access contract.");
        }
        for (const auto& [declaredKey, previous] : m_ResourceAccesses)
        {
            const bool overlaps = declaredKey.Resource == access.Resource &&
                (declaredKey.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                 access.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                 declaredKey.Subresource == access.Subresource);
            static_cast<void>(previous);
            Assert(!overlaps, "Overlapping external resource declarations are not allowed.");
        }
        for (const auto& [batchKey, previous] : batch)
        {
            const bool overlaps = batchKey.Resource == access.Resource &&
                (batchKey.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                 access.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ||
                 batchKey.Subresource == access.Subresource);
            static_cast<void>(previous);
            Assert(!overlaps, "Overlapping external resource declarations conflict within one batch.");
        }
        batch.emplace(key, access);
    }

    for (const ResourceAccess& access : accesses)
    {
        if (!m_ResourceAccesses.contains({ access.Resource, access.Subresource }))
        {
            DeclareResource(access);
        }
    }
}

void ExternalCommandContext::ValidateResourceAccess(const ResourceAccess& access) const
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    Assert(!m_RecordingStarted,
        "External resources must be declared before external recording begins.");
    Assert(access.Resource != nullptr, "External resource access must reference a resource.");
}

void ExternalCommandContext::BeginRecording()
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    m_RecordingStarted = true;
}

void ExternalCommandContext::Transition(
    ID3D12Resource* const resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const bool uavBefore,
    const UINT subresource)
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    m_RecordingStarted = true;
    m_BarrierContext.Transition(resource, stateAfter, uavBefore, subresource);
    m_BarrierContext.Flush();
}

void ExternalCommandContext::UavBarrier(ID3D12Resource* const resource)
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    m_RecordingStarted = true;
    m_BarrierContext.Uav(resource);
    m_BarrierContext.Flush();
}

void ExternalCommandContext::RecordNativeCommands(
    const std::function<void(ID3D12GraphicsCommandList2&)>& recordCommands)
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    Assert(static_cast<bool>(recordCommands), "External native recording callback is empty.");
    m_RecordingStarted = true;
    m_CommandList->ExecuteExternalCommandRecording(recordCommands);
}

void ExternalCommandContext::FlushBarriers()
{
    Assert(!m_Ended, "ExternalCommandContext has already ended recording.");
    m_BarrierContext.Flush();
}

void ExternalCommandContext::DetachBarrierContext() noexcept
{
    if (m_CommandList != nullptr &&
        m_CommandList->GetActiveBarrierContext() == &m_BarrierContext)
    {
        m_CommandList->SetActiveBarrierContext(m_PreviousBarrierContext);
    }
}

bool ExternalCommandContext::RestoreEntryStatesNoThrow() noexcept
{
    m_EntryStatesRestored = false;
    try
    {
        for (const auto& [key, access] : m_ResourceAccesses)
        {
            m_BarrierContext.PrepareResource(
                key.Resource,
                access.InitialState,
                false,
                key.Subresource);
        }
        m_BarrierContext.Flush();

        const std::shared_ptr<D3D12DeviceContext> deviceContext = m_CommandList->GetDeviceContext();
        if (deviceContext == nullptr)
        {
            return false;
        }
        auto submissionScope = deviceContext->GetResourceStateRegistry()->AcquireSubmissionScope();
        m_CommandList->FlushExternalResourceBarriers(submissionScope);

        m_FinalResourceStates.clear();
        m_FinalResourceStates.reserve(m_ResourceAccesses.size());
        for (const auto& [key, access] : m_ResourceAccesses)
        {
            m_FinalResourceStates.push_back({ key.Resource, key.Subresource, access.InitialState, false });
        }
        m_EntryStatesRestored = true;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void ExternalCommandContext::End()
{
    Assert(!m_Ended, "ExternalCommandContext::End may only be called once.");
    try
    {
        for (const FinalResourceState& finalState : m_FinalResourceStates)
        {
            m_BarrierContext.PrepareResource(
                finalState.Resource,
                finalState.State,
                finalState.UavAccess,
                finalState.Subresource);
        }
        m_BarrierContext.Flush();

        // Publish the state actually reached by CommandContext/local transitions.
        // This keeps Unity's state model correct even when the final access was
        // recorded after the declaration through the shared BarrierContext.
        for (const ResourceAccessKey& key : m_DeclaredResources)
        {
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            bool uavWrite = false;
            Assert(
                m_BarrierContext.TryGetTrackedResourceState(key.Resource, state, uavWrite, key.Subresource),
                "An external resource has no tracked final state.");
            const auto existing = std::find_if(
                m_FinalResourceStates.begin(),
                m_FinalResourceStates.end(),
                [&key](const FinalResourceState& finalState)
                {
                    return finalState.Resource == key.Resource && finalState.Subresource == key.Subresource;
                });
            if (existing == m_FinalResourceStates.end())
            {
                m_FinalResourceStates.push_back({ key.Resource, key.Subresource, state, uavWrite });
            }
            else
            {
                existing->State = state;
                existing->UavAccess = uavWrite;
            }
        }

        m_BarrierContext.CommitExternalResourceStates();
    }
    catch (...)
    {
        // End is terminal even on failure. Restore the host-owned entry states
        // while the context is still active, then detach it from Unity's list.
        RestoreEntryStatesNoThrow();
        DetachBarrierContext();
        m_Ended = true;
        m_Failed = true;
        throw;
    }

    DetachBarrierContext();
    m_Ended = true;
    m_Aborted = false;
    m_Failed = false;
    m_EntryStatesRestored = false;
}

void ExternalCommandContext::Abort()
{
    Assert(!m_Ended, "ExternalCommandContext::Abort may only be called once.");
    if (!RestoreEntryStatesNoThrow())
    {
        DetachBarrierContext();
        m_Ended = true;
        m_Failed = true;
        Assert(false, "Failed to restore external resource entry states.");
    }

    DetachBarrierContext();
    m_Ended = true;
    m_Aborted = true;
    m_Failed = false;
}
//Modify End
