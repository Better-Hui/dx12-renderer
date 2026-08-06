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
    class RenderGraphCommandExecutor;
//Modify End

    struct RenderContext
    {
        RenderMetadata m_Metadata = {};
        RenderTargetInfo m_RenderTargetInfo = {};
//Modify Begin:2026-07-30 by BestHui
        RenderGraphResourceStateTracker* m_ResourceStateTracker = nullptr;

        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
        const std::shared_ptr<Buffer>& GetBuffer(ResourceId resourceId) const;
        const Resource& GetResource(ResourceId resourceId) const;

        void TransitionResource(
            CommandList& commandList,
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter) const;

        void TransitionResources(
            CommandList& commandList,
            std::span<const ResourceStateTransition> transitions) const;
//Modify End

    private:
        friend class RenderGraphCommandExecutor;

        std::shared_ptr<ResourcePool> m_ResourcePool = nullptr;
    };
}
