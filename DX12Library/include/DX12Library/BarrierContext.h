#pragma once

//Modify Begin:2026-09-30 by Hui

#include "PerformanceScope.h"

#include <d3d12.h>

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CommandList;
class Resource;
class ResourceStateRegistration;

// A resource-use intent is the common contract shared by graph passes,
// CommandContext bindings, and external command-list integrations. The state
// is still explicit because D3D12 states are queue and view specific.
enum class ResourceUse
{
    Read,
    Write,
    ReadWrite,
};

/**
 * Records resource-state work for one command list.
 *
 * Graph code uses this for ordinary pass-boundary transitions. Framework code
 * uses it for descriptor-driven, pass-local preparation. External integrations
 * can register an explicit entry state before recording their local work.
 */
class BarrierContext final
{
public:
    explicit BarrierContext(CommandList& commandList);

    /**
     * Record one resource use in the command-list context. This is the
     * canonical state entry point for all layers; it resolves transitions and
     * UAV ordering from the context's current local state.
     */
    void Use(
        const Resource& resource,
        D3D12_RESOURCE_STATES state,
        ResourceUse use = ResourceUse::Read,
        bool forceUavBarrier = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void Use(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES state,
        ResourceUse use = ResourceUse::Read,
        bool forceUavBarrier = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    /**
     * Register a resource use whose state was established by an earlier
     * ordered command list. This keeps lifetime and local state attribution
     * without emitting another transition.
     */
    void UseAttributionOnly(
        const Resource& resource,
        D3D12_RESOURCE_STATES state,
        ResourceUse use = ResourceUse::Read,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void UseStableReadOnly(
        const Resource& resource,
        D3D12_RESOURCE_STATES state,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void ReserveResourceUses(size_t resourceCount);

private:
    void PrepareResource(
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter,
        bool uavWrite = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void PrepareResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateAfter,
        bool uavWrite = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    void Transition(
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter,
        bool uavBefore = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void Transition(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateAfter,
        bool uavBefore = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

public:
    /** Register an external resource and prepare its first declared access. */
    void DeclareExternalResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState,
        D3D12_RESOURCE_STATES firstState,
        bool uavWrite = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    void Uav(const Resource& resource);
    void Uav(ID3D12Resource* resource);
    void AliasingBeforeFirstUse(const Resource& resource);

    /** Flush barriers already resolved on this command list. */
    void Flush();
    void Reset();
    void NotifyCommandRecorded() noexcept { ++m_OperationSerial; }

    /**
     * Finalize an external command-list recording without closing or submitting
     * the native list. This resolves pending barriers and commits final states.
     */
    void CommitExternalResourceStates();

    bool TryGetTrackedResourceState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES& state,
        bool& uavWrite,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) const noexcept;

    CommandList& GetCommandList() const { return m_CommandList; }

private:
    struct ResourceSubresourceKey
    {
        ID3D12Resource* Resource = nullptr;
        UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        bool operator==(const ResourceSubresourceKey& other) const noexcept
        {
            return Resource == other.Resource && Subresource == other.Subresource;
        }
    };

    struct ResourceSubresourceKeyHash
    {
        size_t operator()(const ResourceSubresourceKey& key) const noexcept
        {
            const size_t resourceHash = std::hash<ID3D12Resource*>{}(key.Resource);
            const size_t subresourceHash = std::hash<UINT>{}(key.Subresource);
            return resourceHash ^ (subresourceHash + static_cast<size_t>(0x9e3779b9u) +
                (resourceHash << 6u) + (resourceHash >> 2u));
        }
    };

    struct LocalResourceState
    {
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        bool UavWrite = false;
    };

    const LocalResourceState* FindLocalResourceState(
        ID3D12Resource* resource,
        UINT subresource) const noexcept;
    bool HasExternalDeclaration(ID3D12Resource* resource, UINT subresource) const noexcept;
    void RegisterExternalInitialState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState,
        UINT subresource);
    void SetLocalResourceState(
        ID3D12Resource* resource,
        UINT subresource,
        LocalResourceState state);

    CommandList& m_CommandList;
    struct UavAccess { uint64_t Operation; bool Writes; };
    uint64_t m_OperationSerial = 0;
    std::unordered_map<ID3D12Resource*, UavAccess> m_UavAccesses;
    std::unordered_map<ResourceSubresourceKey, LocalResourceState, ResourceSubresourceKeyHash>
        m_LocalResourceStates;
    std::unordered_map<ID3D12Resource*, std::unordered_set<UINT>> m_ExactSubresourcesByResource;
    // External callers provide the state owned by the host renderer. Keep the
    // first declaration stable for the lifetime of this recording context.
    std::unordered_map<ResourceSubresourceKey, D3D12_RESOURCE_STATES, ResourceSubresourceKeyHash>
        m_ExternalInitialStates;
    std::chrono::steady_clock::duration m_TrackLifetimeDuration{};
    std::chrono::steady_clock::duration m_PrepareResourceDuration{};
    std::chrono::steady_clock::duration m_HeapPropertiesDuration{};
};

//Modify End
