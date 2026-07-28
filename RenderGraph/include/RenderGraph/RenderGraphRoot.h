#pragma once

#include <map>
#include <memory>
#include <vector>

#include <DX12Library/Application.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Window.h>

#include "RenderPass.h"
#include "RenderMetadata.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

namespace RenderGraph
{
    class RenderGraphRoot
    {
    public:
        RenderGraphRoot(
            std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
            std::vector<TextureDescription>&& textures,
            std::vector<BufferDescription>&& buffers,
            std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by BestHui
            , std::vector<ResourceId> externalOutputs = { ResourceIds::GRAPH_OUTPUT }
//Modify End
        );

        void Execute(const RenderMetadata& renderMetadata);
        void Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId = ResourceIds::GRAPH_OUTPUT);
//Modify Begin:2026-07-28 by BestHui
        void PresentWithOverlay(const std::shared_ptr<Window>& pWindow, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void CopyTexture(const RenderMetadata& renderMetadata, ResourceId sourceId, ResourceId destinationId, bool waitForCompletion = false);
        void DrawToTexture(const RenderMetadata& renderMetadata, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void TransitionTexture(const RenderMetadata& renderMetadata, ResourceId resourceId, D3D12_RESOURCE_STATES stateAfter, bool waitForCompletion = false);
//Modify End
        void DrawToGraphOutput(const RenderMetadata& renderMetadata, const std::function<void(CommandList&)>& drawCallback);
//Modify Begin:2026-07-27 by BestHui
        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
//Modify End
        void MarkDirty();

    private:
        void RebuildIfNecessary(const RenderMetadata& renderMetadata);
        void CheckPotentiallyDirtyResources(const RenderMetadata& renderMetadata);
        void Build(const RenderMetadata& renderMetadata);
        void PrepareResourcesForRenderPass(CommandList& commandList, const RenderPass& renderPass, uint32_t renderPassIndex, RenderContext& context);

        D3D12_RESOURCE_STATES GetCurrentResourceState(const Resource& resource) const;
        void SetCurrentResourceState(const Resource& resource, D3D12_RESOURCE_STATES state);
        void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
        void UavBarrier(const Resource& resource);
        void AliasingBarrier(const Resource& resourceAfter);
        void FlushBarriers(const CommandList& commandList);

        bool IsResourceDefined(ResourceId id) const;

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;

        std::vector<std::unique_ptr<RenderPass>> m_RenderPassesDescription;
        std::vector<std::vector<RenderPass*>> m_RenderPassesSorted;
        std::vector<RenderPass*> m_RenderPassesBuilt;

        std::vector<TextureDescription> m_TextureDescriptions;
        std::vector<BufferDescription> m_BufferDescriptions;
        std::vector<TokenDescription> m_TokenDescriptions;
//Modify Begin:2026-07-28 by BestHui
        std::vector<ResourceId> m_ExternalOutputIds;
//Modify End

        std::shared_ptr<ResourcePool> m_ResourcePool;
        std::map<const RenderPass*, RenderTargetInfo> m_RenderTargets;
        std::shared_ptr<RenderTarget> m_GraphOutputRenderTarget;
        std::map<const Resource*, D3D12_RESOURCE_STATES> m_ResourceStates;
        std::vector<D3D12_RESOURCE_BARRIER> m_PendingBarriers;

        bool m_Dirty = true;
    };
}
