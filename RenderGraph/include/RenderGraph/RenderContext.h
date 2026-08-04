#pragma once

//Modify Begin:2026-07-30 by BestHui
#include <d3d12.h>
//Modify End

#include <memory>
//Modify Begin:2026-07-30 by BestHui
#include <span>

#include <DX12Library/ResourceStateTransition.h>
//Modify End
#include "RenderMetadata.h"
#include "RenderTargetInfo.h"
#include "ResourcePool.h"

namespace RenderGraph
{
//Modify Begin:2026-07-30 by BestHui
    class RenderGraphResourceStateTracker;
//Modify End

    struct RenderContext
    {
        std::shared_ptr<ResourcePool> m_ResourcePool = nullptr;
        RenderMetadata m_Metadata = {};
        RenderTargetInfo m_RenderTargetInfo = {};
//Modify Begin:2026-07-30 by BestHui
        RenderGraphResourceStateTracker* m_ResourceStateTracker = nullptr;

        void TransitionResource(
            CommandList& commandList,
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter) const;

        void TransitionResources(
            CommandList& commandList,
            std::span<const ResourceStateTransition> transitions) const;
//Modify End
    };
}
