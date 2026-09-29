#pragma once

//Modify Begin:2026-09-29 by Hui

#include <d3d12.h>

#include <memory>
#include <unordered_map>
#include <vector>

class CommandList;
class Resource;
class ResourceStateRegistration;

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

    /** Register and prime a resource whose state is owned by an external renderer. */
    void RegisterExternalResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

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
    void SetLocalResourceState(
        ID3D12Resource* resource,
        UINT subresource,
        LocalResourceState state);

    CommandList& m_CommandList;
    std::unordered_map<ResourceSubresourceKey, LocalResourceState, ResourceSubresourceKeyHash>
        m_LocalResourceStates;
    // External callers provide the state owned by the host renderer. Keep the
    // first declaration stable for the lifetime of this recording context.
    std::unordered_map<ResourceSubresourceKey, D3D12_RESOURCE_STATES, ResourceSubresourceKeyHash>
        m_ExternalInitialStates;
};

//Modify End
