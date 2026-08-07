#include "DX12LibPCH.h"

#include "ResourceStateTracker.h"

#include "CommandList.h"
#include "Resource.h"

#include <d3d12.h>
#include <d3dx12.h>

//Modify Begin:2026-07-30 by BestHui
ResourceStateTracker::ResourceStateTracker(std::shared_ptr<ResourceStateRegistry> resourceStateRegistry)
    : m_ResourceStateRegistry(std::move(resourceStateRegistry))
{
    assert(m_ResourceStateRegistry != nullptr);
}
//Modify End

ResourceStateTracker::~ResourceStateTracker() = default;

void ResourceStateTracker::ResourceBarrier(const D3D12_RESOURCE_BARRIER& barrier)
{
	if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
	{
		const D3D12_RESOURCE_TRANSITION_BARRIER& transitionBarrier = barrier.Transition;

		// First check if there is already a known "final" state for the given resource.
		// If there is, the resource has been used on the command list before and
		// already has a known state within the command list execution.
		const auto iter = m_FinalResourceStates.find(transitionBarrier.pResource);
		if (iter != m_FinalResourceStates.end())
		{
			const auto& resourceState = iter->second;
			// If the known final state of the resource is different...
			if (transitionBarrier.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
				!resourceState.SubresourceStates.empty())
			{
				// First transition all of the subresources if they are different than the StateAfter.
				for (const auto subresourceState : resourceState.SubresourceStates)
				{
					if (transitionBarrier.StateAfter != subresourceState.second)
					{
						D3D12_RESOURCE_BARRIER newBarrier = barrier;
						newBarrier.Transition.Subresource = subresourceState.first;
						newBarrier.Transition.StateBefore = subresourceState.second;
						m_ResourceBarriers.push_back(newBarrier);
					}
				}
			}
			else
			{
				const auto finalState = resourceState.GetSubresourceState(transitionBarrier.Subresource);
				if (transitionBarrier.StateAfter != finalState)
				{
					// Push a new transition barrier with the correct before state.
					D3D12_RESOURCE_BARRIER newBarrier = barrier;
					newBarrier.Transition.StateBefore = finalState;
					m_ResourceBarriers.push_back(newBarrier);
				}
			}
		}
		else // In this case, the resource is being used on the command list for the first time. 
		{
			// Add a pending barrier. The pending barriers will be resolved
			// before the command list is executed on the command queue.
			m_PendingResourceBarriers.push_back(barrier);
		}

		// Push the final known state (possibly replacing the previously known state for the subresource).
		m_FinalResourceStates[transitionBarrier.pResource].SetSubresourceState(
			transitionBarrier.Subresource, transitionBarrier.StateAfter);
	}
	else
	{
		// Just push non-transition barriers to the resource barriers array.
		m_ResourceBarriers.push_back(barrier);
	}
}

void ResourceStateTracker::TransitionResource(ID3D12Resource* resource, const D3D12_RESOURCE_STATES stateAfter,
	const UINT subResource)
{
	if (resource)
	{
		ResourceBarrier(
			CD3DX12_RESOURCE_BARRIER::Transition(resource, D3D12_RESOURCE_STATE_COMMON, stateAfter, subResource));
	}
}

void ResourceStateTracker::TransitionResource(const Resource& resource, const D3D12_RESOURCE_STATES stateAfter,
	const UINT subResource)
{
	TransitionResource(resource.GetD3D12Resource().Get(), stateAfter, subResource);
}


void ResourceStateTracker::UavBarrier(const Resource* resource)
{
	ID3D12Resource* pResource = resource != nullptr ? resource->GetD3D12Resource().Get() : nullptr;
	ResourceBarrier(CD3DX12_RESOURCE_BARRIER::UAV(pResource));
}

void ResourceStateTracker::AliasBarrier(const Resource* beforeResource, const Resource* afterResource)
{
	ID3D12Resource* pResourceBefore = beforeResource != nullptr ? beforeResource->GetD3D12Resource().Get() : nullptr;
	ID3D12Resource* pResourceAfter = afterResource != nullptr ? afterResource->GetD3D12Resource().Get() : nullptr;
	ResourceBarrier(CD3DX12_RESOURCE_BARRIER::Aliasing(pResourceBefore, pResourceAfter));
}

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

//Modify Begin:2026-07-30 by BestHui
uint32_t ResourceStateTracker::FlushPendingResourceBarriers(
    const CommandList& commandList,
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    ResourceStateMapType& resourceStates = submissionScope.GetStates();
	ResourceBarriersType resourceBarriers;
	resourceBarriers.reserve(m_PendingResourceBarriers.size());

    for (auto pendingBarrier : m_PendingResourceBarriers)
	{
		// Only transition barriers should be pending...
		if (pendingBarrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
		{
			auto pendingTransition = pendingBarrier.Transition;
            auto [iter, inserted] = resourceStates.try_emplace(
                pendingTransition.pResource,
                pendingTransition.StateBefore);
            if (inserted)
            {
                resourceBarriers.push_back(pendingBarrier);
            }
            else
            {
				// If all subresources are being transitioned, and there are multiple
				// subresources of the resource that are in a different state...

                auto& resourceState = iter->second;
				if (pendingTransition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
					!resourceState.SubresourceStates.empty()
					)
				{
					// Transition all subresources
					for (const auto subresourceState : resourceState.SubresourceStates)
					{
						if (pendingTransition.StateAfter != subresourceState.second)
						{
							D3D12_RESOURCE_BARRIER newBarrier = pendingBarrier;
							newBarrier.Transition.Subresource = subresourceState.first;
							newBarrier.Transition.StateBefore = subresourceState.second;
							resourceBarriers.push_back(newBarrier);
						}
					}
				}
				else
				{
					// No (sub)resources need to be transitioned. Just add a single transition barrier (if needed).
					const auto globalState = resourceState.GetSubresourceState(pendingTransition.Subresource);
					if (pendingTransition.StateAfter != globalState)
					{
						// Fix-up the before state based on current global state of the resource.
						pendingBarrier.Transition.StateBefore = globalState;
						resourceBarriers.push_back(pendingBarrier);
					}
				}
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
	return numBarriers;
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void ResourceStateTracker::CommitFinalResourceStates(
    ResourceStateRegistry::SubmissionScope& submissionScope)
{
    ResourceStateMapType& resourceStates = submissionScope.GetStates();
	for (const auto& resourceState : m_FinalResourceStates)
	{
		resourceStates[resourceState.first] = resourceState.second;
	}

	m_FinalResourceStates.clear();
}
//Modify End

void ResourceStateTracker::Reset()
{
	m_PendingResourceBarriers.clear();
	m_ResourceBarriers.clear();
	m_FinalResourceStates.clear();
}
