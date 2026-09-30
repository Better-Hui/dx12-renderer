#pragma once

//Modify Begin:2026-09-29 by Hui

#include "BarrierContext.h"

#include <d3d12.h>

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

class CommandList;
class D3D12DeviceContext;

/**
 * State-aware adapter for a command list owned by another renderer.
 * The adapter never resets, closes, submits, or changes allocator ownership.
 */
class ExternalCommandContext final
{
public:
    struct ResourceAccess
    {
        ID3D12Resource* Resource = nullptr;
        UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        D3D12_RESOURCE_STATES InitialState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES FirstState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES FinalState = D3D12_RESOURCE_STATE_COMMON;
        bool FirstAccessWrites = false;
        bool FinalAccessWrites = false;
    };

    struct FinalResourceState
    {
        ID3D12Resource* Resource = nullptr;
        UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        bool UavAccess = false;
    };

    ExternalCommandContext(
        D3D12_COMMAND_LIST_TYPE commandListType,
        std::shared_ptr<D3D12DeviceContext> deviceContext,
        ID3D12GraphicsCommandList2* commandList);
    // A forgotten context is conservatively aborted. Destruction never closes
    // or submits Unity's command list, but it does restore declared resources
    // to their host-owned entry states when possible.
    ~ExternalCommandContext() noexcept;

    ExternalCommandContext(const ExternalCommandContext&) = delete;
    ExternalCommandContext& operator=(const ExternalCommandContext&) = delete;

    CommandList& GetCommandList() const;
    ID3D12GraphicsCommandList2* GetNativeCommandList() const;
    BarrierContext& GetBarrierContext() { return m_BarrierContext; }
    std::span<const FinalResourceState> GetFinalResourceStates() const noexcept
    {
        return m_FinalResourceStates;
    }

    /** True after either End() or Abort() has completed. */
    bool IsFinished() const noexcept { return m_Ended; }
    /** True when End() published the plugin's final resource states. */
    bool IsEnded() const noexcept { return m_Ended && !m_Aborted && !m_Failed; }
    /** True when Abort() restored the host entry states. */
    bool IsAborted() const noexcept { return m_Ended && m_Aborted && !m_Failed; }
    /** True when finalization threw after the native list was modified. */
    bool IsFailed() const noexcept { return m_Ended && m_Failed; }
    /** True when the host-owned entry states were recorded again after abort/failure. */
    bool WereEntryStatesRestored() const noexcept { return m_EntryStatesRestored; }
    bool IsRecordingStarted() const noexcept { return m_RecordingStarted; }

    void DeclareResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState,
        D3D12_RESOURCE_STATES firstState,
        bool uavWrite = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    /**
     * Declare a complete external access lifetime. The caller supplies the
     * state Unity owns on entry and the state Unity expects after the plugin
     * returns. The context records both transitions and the final registry
     * state; it never closes or submits Unity's command list.
     */
    void DeclareResource(const ResourceAccess& access);
    /** Declare several complete access contracts before recording begins. */
    void DeclareResources(std::span<const ResourceAccess> accesses);
    /** Seal resource declarations before CommandContext/native recording. */
    void BeginRecording();
    void UseResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateAfter,
        ResourceUse use = ResourceUse::Read,
        bool uavBefore = false,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void UavBarrier(ID3D12Resource* resource);
    /**
     * Record a native D3D12 callback without bypassing CommandList cache
     * invalidation. The callback owns native pipeline and descriptor state;
     * resource state ownership remains with this context's declarations and
     * BarrierContext transitions.
     */
    void RecordNativeCommands(
        const std::function<void(ID3D12GraphicsCommandList2&)>& recordCommands);
    // Access declarations are complete before control returns to the caller.
    // This makes a declaration sufficient for raw native dispatch/draw code;
    // FlushBarriers remains available for explicit batching and diagnostics.
    void FlushBarriers();

    /** Call once after recording; the native command list remains open. */
    void End();

    /**
     * Abort recording and restore every declared resource to its host-owned
     * entry state. The native command list remains open and owned by the host.
     */
    void Abort();

private:
    struct ResourceAccessKey
    {
        ID3D12Resource* Resource = nullptr;
        UINT Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        bool operator==(const ResourceAccessKey& other) const noexcept
        {
            return Resource == other.Resource && Subresource == other.Subresource;
        }
    };

    struct ResourceAccessKeyHash
    {
        size_t operator()(const ResourceAccessKey& key) const noexcept
        {
            const size_t resourceHash = std::hash<ID3D12Resource*>{}(key.Resource);
            const size_t subresourceHash = std::hash<UINT>{}(key.Subresource);
            return resourceHash ^ (subresourceHash + static_cast<size_t>(0x9e3779b9u) +
                (resourceHash << 6u) + (resourceHash >> 2u));
        }
    };

    void ValidateResourceAccess(const ResourceAccess& access) const;
    void DeclareResourceInternal(const ResourceAccess& access);
    bool RestoreEntryStatesNoThrow() noexcept;
    void DetachBarrierContext() noexcept;

    std::unique_ptr<CommandList> m_CommandList;
    BarrierContext m_BarrierContext;
    BarrierContext* m_PreviousBarrierContext = nullptr;
    std::vector<FinalResourceState> m_FinalResourceStates;
    std::vector<ResourceAccessKey> m_DeclaredResources;
    std::unordered_map<ResourceAccessKey, ResourceAccess, ResourceAccessKeyHash> m_ResourceAccesses;
    bool m_Ended = false;
    bool m_Aborted = false;
    bool m_Failed = false;
    bool m_RecordingStarted = false;
    bool m_EntryStatesRestored = false;
};

//Modify End
