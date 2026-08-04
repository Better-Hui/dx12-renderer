//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

#include <map>
#include <vector>

class CommandList;
class Resource;

namespace RenderGraph
{
    class RenderGraphResourceStateTracker final
    {
    public:
        void Reset();
        bool HasPendingBarriers() const { return !m_PendingBarriers.empty(); }
        D3D12_RESOURCE_STATES GetCurrentResourceState(const Resource& resource) const;
        void SetCurrentResourceState(const Resource& resource, D3D12_RESOURCE_STATES state);
        void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
        void UavBarrier(const Resource& resource);
        void AliasingBarrier(const Resource& resourceAfter);
        void FlushBarriers(const CommandList& commandList);

    private:
        std::map<const Resource*, D3D12_RESOURCE_STATES> m_ResourceStates;
        std::vector<D3D12_RESOURCE_BARRIER> m_PendingBarriers;
    };
}
//Modify End
