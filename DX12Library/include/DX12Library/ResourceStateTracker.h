#pragma once

#include "ResourceStateRegistry.h"

#include <d3d12.h>

#include <memory>
#include <vector>

class CommandList;
class Resource;

class ResourceStateTracker
{
public:
//Modify Begin:2026-07-30 by Hui
    explicit ResourceStateTracker(std::shared_ptr<ResourceStateRegistry> resourceStateRegistry);
//Modify End
    virtual ~ResourceStateTracker();

    void ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier);

    void TransitionResource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    void TransitionResource(
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

//Modify Begin:2026-07-30 by Hui
    void NotifyResourceState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES state,
        UINT subResource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
//Modify End

    void UavBarrier(const Resource* resource = nullptr);
    void AliasBarrier(const Resource* beforeResource = nullptr, const Resource* afterResource = nullptr);
//Modify Begin:2026-08-10 by Hui
    void QueueAliasingBarrier(const Resource* beforeResource = nullptr, const Resource* afterResource = nullptr);
//Modify End

//Modify Begin:2026-07-30 by Hui
    uint32_t FlushPendingResourceBarriers(
        const CommandList& commandList,
        ResourceStateRegistry::SubmissionScope& submissionScope);
//Modify End
    void FlushResourceBarriers(const CommandList& commandList);
//Modify Begin:2026-07-30 by Hui
    void CommitFinalResourceStates(ResourceStateRegistry::SubmissionScope& submissionScope);
//Modify End
    void Reset();

private:
    using ResourceBarriersType = std::vector<D3D12_RESOURCE_BARRIER>;
//Modify Begin:2026-07-30 by Hui
    using ResourceStateMapType = ResourceStateRegistry::ResourceStateMap;
//Modify End

    ResourceBarriersType m_PendingResourceBarriers;
//Modify Begin:2026-08-10 by Hui
    ResourceBarriersType m_PendingAliasingBarriers;
//Modify End
    ResourceBarriersType m_ResourceBarriers;
    ResourceStateMapType m_FinalResourceStates;
//Modify Begin:2026-07-30 by Hui
    std::shared_ptr<ResourceStateRegistry> m_ResourceStateRegistry;
//Modify End
};
