#include "DX12LibPCH.h"

#include "ResourceStateTracker.h"

#include "CommandList.h"
#include "Resource.h"

#include <d3d12.h>
#include <d3dx12/d3dx12.h>

//Modify Begin:2026-07-30 by Hui
ResourceStateTracker::ResourceStateTracker(std::shared_ptr<ResourceStateRegistry> resourceStateRegistry)
    : m_ResourceStateRegistry(std::move(resourceStateRegistry))
{
    assert(m_ResourceStateRegistry != nullptr);
}
//Modify End

ResourceStateTracker::~ResourceStateTracker() = default;

//Modify Begin:2026-09-29 by Hui
namespace
{
    UINT GetSubresourceCount(ID3D12Resource* const resource)
    {
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return 1u;
        }

        Assert(desc.MipLevels != 0u, "Cannot enumerate a texture with no mip levels.");
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        ThrowIfFailed(resource->GetDevice(IID_PPV_ARGS(&device)));
        D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { desc.Format, 0u };
        ThrowIfFailed(device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo)));
        const uint64_t arraySize = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
            ? 1u
            : desc.DepthOrArraySize;
        const uint64_t count = static_cast<uint64_t>(desc.MipLevels) * arraySize * formatInfo.PlaneCount;
        Assert(count > 0u && count < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            "D3D12 resource subresource count is invalid.");
        return static_cast<UINT>(count);
    }
}

void ResourceStateTracker::ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
{
    if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
    {
        m_ResourceBarriers.push_back(barrier);
        return;
    }

    const auto& transition = barrier.Transition;
    const auto known = m_FinalResourceStates.find(transition.pResource);
    if (known == m_FinalResourceStates.end())
    {
        m_PendingResourceBarriers.push_back(barrier);
    }
    else
    {
        const auto& state = known->second;
        const auto append = [&](const UINT subresource)
        {
            D3D12_RESOURCE_BARRIER resolved = barrier;
            resolved.Transition.Subresource = subresource;
            if (!state.HasKnownState(subresource))
            {
                // The COMMON value in a partially known command-list state is
                // only a placeholder. Resolve this before-state at submission.
                m_PendingResourceBarriers.push_back(resolved);
                return;
            }
            resolved.Transition.StateBefore = state.GetSubresourceState(subresource);
            if (resolved.Transition.StateBefore != transition.StateAfter)
            {
                m_ResourceBarriers.push_back(resolved);
            }
        };

        if (transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
            (!state.SubresourceStates.empty() || !state.HasAllSubresourcesState))
        {
            const UINT count = GetSubresourceCount(transition.pResource);
            for (UINT subresource = 0u; subresource < count; ++subresource)
            {
                append(subresource);
            }
        }
        else
        {
            append(transition.Subresource);
        }
    }

    m_FinalResourceStates[transition.pResource].SetSubresourceState(
        transition.Subresource, transition.StateAfter);
}
//Modify End

void ResourceStateTracker::TransitionResource(ID3D12Resource* resource, const D3D12_RESOURCE_STATES stateAfter,
	const UINT subResource)
{
//Modify Begin:2026-08-12 by Hui
	Assert(resource != nullptr, "Cannot track a transition for a null D3D12 resource.");
	ResourceBarrier(
		CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource));
//Modify End
}

void ResourceStateTracker::TransitionResource(const Resource& resource, const D3D12_RESOURCE_STATES stateAfter,
	const UINT subResource)
{
	TransitionResource(resource.GetD3D12Resource().Get(), stateAfter, subResource);
}

//Modify Begin:2026-07-30 by Hui
void ResourceStateTracker::NotifyResourceState(
    ID3D12Resource* resource,
    const D3D12_RESOURCE_STATES state,
    const UINT subResource)
{
    Assert(resource != nullptr, "Cannot notify a null D3D12 resource state.");
    m_FinalResourceStates[resource].SetSubresourceState(subResource, state);
}
//Modify End


void ResourceStateTracker::UavBarrier(const Resource* resource)
{
	ID3D12Resource* pResource = resource != nullptr ? resource->GetD3D12Resource().Get() : nullptr;
	ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
}

//Modify Begin:2026-09-24 by Hui
void ResourceStateTracker::UavBarrier(ID3D12Resource* resource)
{
    ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(resource));
}
//Modify End

void ResourceStateTracker::AliasBarrier(const Resource* beforeResource, const Resource* afterResource)
{
	ID3D12Resource* pResourceBefore = beforeResource != nullptr ? beforeResource->GetD3D12Resource().Get() : nullptr;
	ID3D12Resource* pResourceAfter = afterResource != nullptr ? afterResource->GetD3D12Resource().Get() : nullptr;
	ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
}

//Modify Begin:2026-08-10 by Hui
void ResourceStateTracker::QueueAliasingBarrier(const Resource* beforeResource, const Resource* afterResource)
{
    ID3D12Resource* pResourceBefore = beforeResource != nullptr ? beforeResource->GetD3D12Resource().Get() : nullptr;
    ID3D12Resource* pResourceAfter = afterResource != nullptr ? afterResource->GetD3D12Resource().Get() : nullptr;
    m_PendingAliasingBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
}
//Modify End

void ResourceStateTracker::FlushResourceBarriers(const CommandList& commandList)
{
	const UINT numBarriers = static_cast<UINT>(m_ResourceBarriers.size());
	if (numBarriers == 0)
	{
		return;
	}

	const auto d3d12CommandList = commandList.GetGraphicsCommandList();
	d3d12CommandList->ResourceBarrier(numBarriers, m_ResourceBarriers.data());
	m_ResourceBarriers.clear();
}

//Modify Begin:2026-09-29 by Hui
uint32_t ResourceStateTracker::FlushPendingResourceBarriers(
    const CommandList& commandList,
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    ResourceStateMapType& resourceStates = submissionScope.GetStates();
	ResourceBarriersType resourceBarriers;
	resourceBarriers.reserve(m_PendingAliasingBarriers.size() + m_PendingResourceBarriers.size());

    resourceBarriers.insert(
        resourceBarriers.end(),
        m_PendingAliasingBarriers.begin(),
        m_PendingAliasingBarriers.end());

    for (auto pendingBarrier : m_PendingResourceBarriers)
	{
		// Only transition barriers should be pending...
		if (pendingBarrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
		{
			auto pendingTransition = pendingBarrier.Transition;
            const auto iter = resourceStates.find(pendingTransition.pResource);
            Assert(
                iter != resourceStates.end(),
                "D3D12 resource state was not registered before its first transition.");

            const auto& resourceState = iter->second;
            const auto append = [&](const UINT subresource)
            {
                Assert(resourceState.HasKnownState(subresource),
                    "The true D3D12 before-state was not registered for this subresource.");
                D3D12_RESOURCE_BARRIER resolved = pendingBarrier;
                resolved.Transition.Subresource = subresource;
                resolved.Transition.StateBefore = resourceState.GetSubresourceState(subresource);
                if (resolved.Transition.StateBefore != pendingTransition.StateAfter)
                {
                    resourceBarriers.push_back(resolved);
                }
            };
            if (pendingTransition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                (!resourceState.SubresourceStates.empty() || !resourceState.HasAllSubresourcesState))
            {
                const UINT count = GetSubresourceCount(pendingTransition.pResource);
                for (UINT subresource = 0u; subresource < count; ++subresource)
                {
                    append(subresource);
                }
            }
            else
            {
                append(pendingTransition.Subresource);
            }
		}
	}

	UINT numBarriers = static_cast<UINT>(resourceBarriers.size());
	if (numBarriers > 0)
	{
		auto d3d12CommandList = commandList.GetGraphicsCommandList();
		d3d12CommandList->ResourceBarrier(numBarriers, resourceBarriers.data());
	}

	m_PendingResourceBarriers.clear();
    m_PendingAliasingBarriers.clear();
	return numBarriers;
}

bool ResourceStateTracker::HasPendingResourceBarriers() const noexcept
{
    return !m_PendingResourceBarriers.empty();
}

//Modify Begin:2026-10-02 by Hui
bool ResourceStateTracker::HasPendingSubmissionWork() const noexcept
{
    return !m_PendingResourceBarriers.empty() || !m_PendingAliasingBarriers.empty();
}
//Modify End

void ResourceStateTracker::CommitFinalResourceStates(
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    ResourceStateMapType& resourceStates = submissionScope.GetStates();
	for (const auto& resourceState : m_FinalResourceStates)
	{
        const auto iter = resourceStates.find(resourceState.first);
        Assert(
            iter != resourceStates.end(),
            "D3D12 resource state was not registered before its final state was committed.");
        const auto& finalState = resourceState.second;
        if (finalState.HasAllSubresourcesState)
        {
            iter->second = finalState;
        }
        else
        {
            // A per-subresource recording must preserve untouched registry
            // states, including the registry's default for other subresources.
            for (const auto& [subresource, state] : finalState.SubresourceStates)
            {
                iter->second.SetSubresourceState(subresource, state);
            }
        }
	}

	m_FinalResourceStates.clear();
}

void ResourceStateTracker::Reset()
{
	m_PendingResourceBarriers.clear();
    m_PendingAliasingBarriers.clear();
	m_ResourceBarriers.clear();
	m_FinalResourceStates.clear();
}

bool ResourceStateTracker::TryGetResourceState(
    ID3D12Resource* resource, const UINT subresource, D3D12_RESOURCE_STATES& state) const
{
    const auto found = m_FinalResourceStates.find(resource);
    if (found == m_FinalResourceStates.end() || !found->second.HasKnownState(subresource)) return false;
    state = found->second.GetSubresourceState(subresource);
    return true;
}

//Modify Begin:2026-10-01 by Hui
void ResourceStateTracker::ReserveResourceTracking(const size_t resourceCount)
{
    m_PendingResourceBarriers.reserve(resourceCount);
    m_ResourceBarriers.reserve(resourceCount);
    m_FinalResourceStates.reserve(resourceCount);
}
//Modify End
//Modify End
